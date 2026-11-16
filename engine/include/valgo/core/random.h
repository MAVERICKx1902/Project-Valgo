// =============================================================================
// Project Valgo — engine core: pseudo random numbers.
//
// PCG32: small, fast and statistically good enough for gameplay variation
// (recoil spread, spawn jitter). Never used for anything security related.
// =============================================================================
#pragma once

#include "valgo/core/types.h"

namespace valgo {

class Random {
public:
    explicit Random(u64 seed = 0x853c49e6748fea9bULL);

    void Seed(u64 seed, u64 stream = 54u);

    u32 NextU32();
    f32 NextFloat();                    ///< [0, 1)
    f32 NextFloat(f32 min, f32 max);    ///< [min, max)
    i32 NextInt(i32 min_inclusive, i32 max_exclusive);
    bool Chance(f32 probability);

private:
    u64 state_ = 0;
    u64 inc_ = 1;
};

}  // namespace valgo
