#include "reassembler.hh"
#include <algorithm>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{

  Writer& bytes_writer = output_.writer();
  const uint64_t unacceptable_index = expecting_index_ + bytes_writer.available_capacity();

  if ( bytes_writer.is_closed() || bytes_writer.available_capacity() == 0 || first_index >= unacceptable_index ) {
    // 如果流已经关闭，或者缓冲区已满，或者当前数据的序号大于期待的序号加上缓冲区的大小
    // 则直接丢弃当前数据
    return;
  } else if ( first_index + data.size() > expecting_index_ ) {
    // 如果当前数据的序号加上数据的长度大于期待的序号
    // 则将当前数据截断后存入缓冲区 , 并且不能视作最后的数据
    is_last_substring = false;
    data.resize( unacceptable_index - first_index );
  }
  if ( first_index > expecting_index_ ) {
    cache_bytes( first_index, move( data ), is_last_substring );
  } else {
    push_bytes( first_index, move( data ), is_last_substring );
  }
  flush_buffer();
}

uint64_t Reassembler::bytes_pending() const
{
  return num_bytes_pending_;
}

void Reassembler::push_bytes( uint64_t first_index, string data, bool is_last_substring )
{
  if ( first_index < expecting_index_ ) {
    data.erase( 0, expecting_index_ - first_index ); // 删除前面的无效数据
    expecting_index_ += data.size();
    output_.writer().push( move( data ) );
  }

  if ( is_last_substring ) {
    output_.writer().close(); // 关闭流
    unordered_bytes_.clear(); // 清空缓冲区
    num_bytes_pending_ = 0;   // 重置当前存储的字节数
  }
}

void Reassembler::cache_bytes( uint64_t first_index, string data, bool is_last_substring )
{
  auto end = unordered_bytes_.end();
  auto left = lower_bound( unordered_bytes_.begin(), end, first_index, []( const auto&& e, uint64_t idx ) -> bool {
    return idx > ( get<0>( e ) + get<1>( e ).size() );
  } );

  auto right = upper_bound(
    left, end, first_index + data.size(), []( uint64_t nxt_idx, auto&& e ) { return nxt_idx < get<0>( e ); } );

  const uint64_t next_idx = first_index + data.size();
  if ( left != end ) {
    auto& [l_point, data_, _] = *left;
    if ( const uint64_t r_point = l_point + data_.size(); r_point >= first_index )
  }
}

void Reassembler::flush_buffer()
{
  while ( !unordered_bytes_.empty() ) {
    auto& [idx, data, last] = unordered_bytes_.front();
    if ( idx > expecting_index_ ) {
      break;
    }
    num_bytes_pending_ -= data.size();
    push_bytes( idx, move( data ), last );
    if ( !unordered_bytes_.empty() ) {
      unordered_bytes_.pop_front();
    }
  }
}