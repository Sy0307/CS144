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
  } else if ( first_index + data.size() >= unacceptable_index ) {
    // 如果当前数据的序号加上数据的长度大于期待的序号
    // 则将当前数据截断后存入缓冲区 , 并且不能视作最后的数据
    is_last_substring = false;
    data.resize( unacceptable_index - first_index );
  }
  if ( first_index > expecting_index_ )
    cache_bytes( first_index, move( data ), is_last_substring );
  else
    push_bytes( first_index, move( data ), is_last_substring );

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
  }
  expecting_index_ += data.size();
  output_.writer().push( move( data ) );

  if ( is_last_substring ) {
    output_.writer().close(); // 关闭流
    unordered_bytes_.clear(); // 清空缓冲区
    num_bytes_pending_ = 0;   // 重置当前存储的字节数
  }
}

void Reassembler::cache_bytes( uint64_t first_index, string data, bool is_last_substring )
{
  auto end = unordered_bytes_.end();
  auto left = lower_bound( unordered_bytes_.begin(), end, first_index, []( auto&& e, uint64_t idx ) -> bool {
    return idx > ( get<0>( e ) + get<1>( e ).size() );
  } );

  auto right = upper_bound( left, end, first_index + data.size(), []( uint64_t nxt_idx, auto&& e ) -> bool {
    return nxt_idx < get<0>( e );
  } );

  // right 指向是待合并区间的下一个元素

  // 分情况讨论 找到应该插入的位置（包括处理字符串）
  uint64_t next_idx = first_index + data.size();
  if ( left != end ) {
    auto& [l_point, data_, _] = *left;
    if ( const uint64_t r_point = l_point + data_.size(); r_point >= next_idx && first_index >= l_point ) {
      // data 本身已经存在了
      return;
    } else if ( next_idx < l_point ) {
      right = left;                                                    // data 和 data_ 之间没有交集
    } else if ( !( first_index <= l_point && next_idx >= r_point ) ) { // 不是独立的片段
      if ( first_index >= l_point ) {
        // data 和 data_ 有交集 , 且 data_ 是在已经缓存的数据的后面
        data.insert( 0, string_view( data_.c_str(), data_.size() - ( r_point - first_index ) ) );
      } else {
        data.resize( data.size() - ( next_idx - l_point ) );
        data.append( data_ );
      }
      first_index = min( first_index, l_point );
    }
  }

  next_idx = first_index + data.size();
  if ( right != left && unordered_bytes_.size() ) {
    auto& [l_point, data_, _] = *prev( right );
    const uint64_t r_point = l_point + data_.size();
    if ( r_point > next_idx ) {
      data.resize( data.size() - ( next_idx - l_point ) );
      data.append( data_ );
    }
  }

  for ( ; left != right; left = unordered_bytes_.erase( left ) ) {
    num_bytes_pending_ -= get<1>( *left ).size();
    is_last_substring |= get<2>( *left );
  }
  num_bytes_pending_ += data.size();
  unordered_bytes_.insert( left, make_tuple( first_index, move( data ), is_last_substring ) );
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