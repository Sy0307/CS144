#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + ISN_.has_value();
  if ( message.RST ) {
    reassembler_.reader().set_error();
  } else if ( checkpoint > 0 && checkpoint <= UINT32_MAX && message.seqno == ISN_ ) {
    // 如果checkpoint > 0，说明已经接收到数据，且ISN_有值，说明已经接收到SYN包
    return;
  }

  if ( !ISN_.has_value() ) {
    if ( !message.SYN )
      return;
    ISN_ = message.seqno;
  }
  const uint64_t abso_seqno = message.seqno.unwrap( *ISN_, checkpoint );
  reassembler_.insert( abso_seqno == 0 ? abso_seqno : abso_seqno - 1, move( message.payload ), message.FIN );
  // 如果abso_seqno == 0，说明是SYN包，不需要插入数据，否则需要插入数据
}

TCPReceiverMessage TCPReceiver::send() const
{
  const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + ISN_.has_value();
  const uint64_t capacity = reassembler_.writer().available_capacity();
  const uint16_t windows_size = capacity > UINT16_MAX ? UINT16_MAX : capacity;

  if ( !ISN_.has_value() ) {
    return { {}, windows_size, reassembler_.writer().has_error() };
  }

  auto wrap_ = Wrap32::wrap( checkpoint + reassembler_.writer().is_closed(), *ISN_ );
  return { wrap_, windows_size, reassembler_.writer().has_error() };
}
