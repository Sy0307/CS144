#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + n;
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  constexpr uint64_t upper = static_cast<uint64_t>( UINT32_MAX ) + 1;
  const uint32_t checkpoint_mod = wrap( checkpoint, zero_point ).raw_value_;
  uint32_t distance = raw_value_ - checkpoint_mod;
  if ( distance <= ( upper >> 1 ) || checkpoint + distance < upper )
    return checkpoint + distance;
  return checkpoint + distance - upper;
}
