#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

// RetransmissonTimer class implementation

bool RetransmissonTimer::is_active() const noexcept
{
  return is_active_;
}

bool RetransmissonTimer::is_expired() const noexcept
{
  return is_active_ && time_passed_ >= RTO_ms_;
}

RetransmissonTimer& RetransmissonTimer::active() noexcept
{
  is_active_ = true;
  return *this;
}

RetransmissonTimer& RetransmissonTimer::timeout() noexcept
{
  RTO_ms_ <<= 1;
  return *this;
}

RetransmissonTimer& RetransmissonTimer::reset() noexcept
{
  time_passed_ = 0;
  return *this;
}

RetransmissonTimer& RetransmissonTimer::tick( uint64_t ms_since_last_tick ) noexcept
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
           .FIN = FIN,
           .payload = move( payload ),
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
    transmit( outstanding_queue_.front() );
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

  const uint64_t expected_ackno = msg.ackno->unwrap( isn_ , next_seqno_ );
  if ( expected_ackno > next_seqno_ ) {
    return;
  }

  bool is_ack = false;

  while ( outstanding_queue_.size() ) {
    auto& buffer_msg = outstanding_queue_.front();

    const uint64_t final_seqno = acked_seqno_ + buffer_msg.sequence_length() - buffer_msg.SYN;
    // // 对方期待的下一字节不大于队首的字节序号，或者队首分组只有部分字节被确认
    if ( expected_ackno <= acked_seqno_ || expected_ackno < final_seqno )
      break;

    is_ack = true;
    auto ack_len = buffer_msg.sequence_length() - syn_flag_;
    num_bytes_in_flight_ -= ack_len; // 减去已经确认的字节数
    acked_seqno_ += ack_len;         // 更新已经确认的字节数

    syn_flag_ = sent_syn ? syn_flag_ : expected_ackno <= acked_seqno_;
    // 更新syn_flag_,如果已经发送过syn,则不再更新，否则如果对方期待的下一字节小于等于已经确认的字节数，则更新
    outstanding_queue_.pop();
  }

  if ( is_ack ) // 如果有确认
  {
    if ( outstanding_queue_.empty() ) {
      timer_ = RetransmissonTimer { initial_RTO_ms_ };
    } else {
      // 重置定时器
      timer_ = move( RetransmissonTimer { initial_RTO_ms_ }.active() );
    }
    retransmisson_cnt_ = 0;
  }
}
