#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

// RetransmissonTimer class implementation

// bool RetransmissonTimer::is_active() const noexcept
// {
//   return is_active_;
// }

// bool RetransmissonTimer::is_expired() const noexcept
// {
//   return is_active_ && time_passed_ >= RTO_ms_;
// }

RetransmissionTimer& RetransmissionTimer::active() noexcept
{
  is_active_ = true;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::timeout() noexcept
{
  RTO_ <<= 1;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::reset() noexcept
{
  time_passed_ = 0;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::tick( uint64_t ms_since_last_tick ) noexcept
{
  if ( is_active_ ) {
    time_passed_ += ms_since_last_tick;
  }
  return *this;
}

// RetransmissonTimer class implementation closed

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return num_bytes_in_flight_; // 返回
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return retransmisson_cnt_;
}

TCPSenderMessage TCPSender::make_message( uint64_t seqno, string payload, bool SYN, bool FIN ) const
{
  return { .seqno = Wrap32::wrap( seqno, isn_ ),
           .SYN = SYN,
           .payload = move( payload ),
           .FIN = FIN,
           .RST = input_.reader().has_error() };
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return make_message( next_seqno_, {}, false );
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( timer_.tick( ms_since_last_tick ).is_expired() ) // 过期
  {
    transmit( outstanding_bytes_.front() );
    if ( windows_size_ == 0 )
      timer_.reset();
    else
      timer_.timeout().reset();
    ++retransmisson_cnt_;
  }
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  windows_size_ = msg.window_size;
  if ( !msg.ackno.has_value() ) {
    if ( msg.window_size == 0 )
      input_.set_error();
    return;
  }

  const uint64_t expected_ackno = msg.ackno->unwrap( isn_, next_seqno_ );
  if ( expected_ackno > next_seqno_ ) {
    return;
  }

  bool is_ack = false;

  while ( outstanding_bytes_.size() ) {
    auto& buffer_msg = outstanding_bytes_.front();

    const uint64_t final_seqno = acked_seqno_ + buffer_msg.sequence_length() - buffer_msg.SYN;
    // // 对方期待的下一字节不大于队首的字节序号，或者队首分组只有部分字节被确认
    if ( expected_ackno <= acked_seqno_ || expected_ackno < final_seqno )
      break;

    is_ack = true;
    auto ack_len = buffer_msg.sequence_length() - syn_flag_;
    num_bytes_in_flight_ -= ack_len; // 减去已经确认的字节数
    acked_seqno_ += ack_len;         // 更新已经确认的字节数

    syn_flag_ = sent_syn_ ? syn_flag_ : expected_ackno <= acked_seqno_;
    // 更新syn_flag_,如果已经发送过syn,则不再更新，否则如果对方期待的下一字节小于等于已经确认的字节数，则更新
    outstanding_bytes_.pop();
  }

  if ( is_ack ) // 如果有确认
  {
    if ( outstanding_bytes_.empty() ) {
      timer_ = RetransmissionTimer { initial_RTO_ms_ };
    } else {
      // 重置定时器
      timer_ = move( RetransmissionTimer { initial_RTO_ms_ }.active() );
    }
    retransmisson_cnt_ = 0;
  }
}

void TCPSender::push( const TransmitFunction& transmit )
{
  Reader& bytes_reader = input_.reader();
  fin_flag_ |= bytes_reader.is_finished();

  if ( sent_fin_ )
    return; // 如果已经发送过fin,则不再发送任何数据

  const size_t window_size = windows_size_ == 0 ? 1 : windows_size_;

  for ( string payload {}; num_bytes_in_flight_ < window_size && !sent_fin_;
        payload.clear() ) // 不断组装数据 直到窗口满了或者没有数据可读
  {
    string_view bytes_view = bytes_reader.peek();
    if ( sent_syn_ && bytes_view.empty() && !fin_flag_ ) // 如果已经发送过syn,且没有数据可读,则发送fin
      break;

    while ( payload.size() + num_bytes_in_flight_ + ( !sent_syn_ ) < window_size
            && payload.size() < TCPConfig::MAX_PAYLOAD_SIZE ) { // 负载上限
      if ( bytes_view.empty() || fin_flag_ )
        break; // 没数据读了，或者流关闭了

      // 如果当前读取的字节分组长度超过限制
      if ( const uint64_t available_size
           = min( static_cast<uint64_t>( TCPConfig::MAX_PAYLOAD_SIZE - payload.size() ),
                  window_size - ( payload.size() + num_bytes_in_flight_ + ( !sent_syn_ ) ) );
           bytes_view.size() > available_size ) // 那么这个分组需要被截断
        bytes_view.remove_suffix( bytes_view.size() - available_size );

      payload.append( bytes_view );
      bytes_reader.pop( bytes_view.size() );
      // 从流中弹出字符后要检查流是否关闭
      fin_flag_ |= bytes_reader.is_finished();
      bytes_view = bytes_reader.peek();
    }

    auto& msg = outstanding_bytes_.emplace(
      make_message( next_seqno_, move( payload ), sent_syn_ ? syn_flag_ : true, fin_flag_ ) );

    const size_t margin = sent_syn_ ? syn_flag_ : 0;

    if ( fin_flag_ && ( msg.sequence_length() - margin ) + num_bytes_in_flight_ > window_size ) {
      msg.FIN = false;
    } else if ( fin_flag_ ) {
      sent_fin_ = true;
    }
    // 计算真实的报文长度
    const size_t correct_len = msg.sequence_length() - margin;

    num_bytes_in_flight_ += correct_len;
    next_seqno_ += correct_len;
    sent_syn_ = sent_syn_ ? sent_syn_ : true;
    transmit( msg );
    if ( correct_len )
      timer_.active();
  }
}
