#include "valgo/core/random.h"

namespace valgo {

Random::Random(u64 seed) { Seed(seed); }

void Random::Seed(u64 seed, u64 stream) {
    state_ = 0u;
    inc_ = (stream << 1u) | 1u;
    NextU32();
    state_ += seed;
    NextU32();
}

u32 Random::NextU32() {
    const u64 old_state = state_;
    state_ = old_state * 6364136223846793005ULL + inc_;
    const u32 xorshifted = static_cast<u32>(((old_state >> 18u) ^ old_state) >> 27u);
    const u32 rot = static_cast<u32>(old_state >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
}

f32 Random::NextFloat() {
    // 24 bits of mantissa: exactly representable in a float.
    const u32 bits = NextU32() >> 8u;
    return static_cast<f32>(bits) * (1.0f / 16777216.0f);
}

f32 Random::NextFloat(f32 min, f32 max) { return min + NextFloat() * (max - min); }

i32 Random::NextInt(i32 min_inclusive, i32 max_exclusive) {
    if (max_exclusive <= min_inclusive) {
        return min_inclusive;
    }
    const u32 range = static_cast<u32>(max_exclusive - min_inclusive);
    return min_inclusive + static_cast<i32>(NextU32() % range);
}

bool Random::Chance(f32 probability) { return NextFloat() < probability; }

}  // namespace valgo
