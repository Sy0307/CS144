#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <queue>

class RetransmissonTimer
{
public:
  RetransmissonTimer( uint64_t RTO_ms ) : RTO_ms_( RTO_ms ) {}
  bool is_active() const noexcept;
  bool is_expired() const noexcept;
  RetransmissonTimer& active() noexcept;

  RetransmissonTimer& timeout() noexcept;

  RetransmissonTimer& reset() noexcept;
  RetransmissonTimer& tick( uint64_t ms_since_last_tick ) noexcept;

private:
  uint64_t RTO_ms_;
  uint64_t time_passed_ {};
  bool is_active_ {};
};

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), timer_( initial_RTO_ms )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
  Writer& writer() { return input_.writer(); }
  const Writer& writer() const { return input_.writer(); }

  // Access input stream reader, but const-only (can't read from outside)
  const Reader& reader() const { return input_.reader(); }

private:
  TCPSenderMessage make_message( uint64_t seqno, std::string payload, bool SYN, bool FIN = false ) const;

  // Variables initialized in constructor
  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;

  uint16_t windows_size_ { 1 };
  uint64_t next_seqno_ {};
  uint64_t acked_seqno_ {};
  bool syn_flag_ {}, fin_flag_ {}, sent_syn {};

  RetransmissonTimer timer_;
  uint64_t retransmisson_cnt_ {};

  std::queue<TCPSenderMessage> outstanding_queue_ {};
  uint64_t num_bytes_in_flight_ {};
};
