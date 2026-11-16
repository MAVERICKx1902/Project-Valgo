// =============================================================================
// Project Valgo — engine core: time and frame timing.
// =============================================================================
#pragma once

#include "valgo/core/types.h"

namespace valgo {

/// Monotonic clock, in seconds since an unspecified epoch.
f64 MonotonicNowSeconds();

/// Monotonic clock, in nanoseconds.
u64 MonotonicNowNanoseconds();

/// Sleeps the calling thread for at least the requested duration.
void SleepMilliseconds(u32 milliseconds);

/// Simple elapsed-time measurement with Reset()/Seconds() semantics.
class Stopwatch {
public:
    Stopwatch();

    void Reset();
    f64 Seconds() const;
    f64 Milliseconds() const;

private:
    f64 start_seconds_ = 0.0;
};

/// Per-frame timing handed to game modules and the renderer.
struct FrameContext {
    u64 frame_index = 0;
    f64 delta_seconds = 0.0;     ///< Clamped wall-clock delta for this frame.
    f64 total_seconds = 0.0;     ///< Seconds since the application started.
    u32 fixed_steps = 0;         ///< Number of fixed simulation steps to run.
    f32 fixed_step_seconds = 1.0f / 60.0f;
    f32 interpolation_alpha = 0.0f;  ///< Blend factor between fixed steps.
};

/// Converts wall-clock deltas into a stable fixed-step simulation schedule.
///
/// Fixed-step simulation keeps movement, collision and weapon timing
/// deterministic — a prerequisite for reproducible tests and, later, for
/// authoritative networked play.
class FrameTimer {
public:
    explicit FrameTimer(f64 fixed_step_seconds = 1.0 / 60.0, f64 max_frame_seconds = 0.25);

    /// Call once per rendered frame. Returns the timing for that frame.
    FrameContext Advance();

    f64 fixed_step_seconds() const { return fixed_step_seconds_; }
    void SetFixedStepSeconds(f64 seconds);

private:
    f64 fixed_step_seconds_;
    f64 max_frame_seconds_;
    f64 accumulator_ = 0.0;
    f64 total_seconds_ = 0.0;
    f64 last_time_seconds_ = 0.0;
    u64 frame_index_ = 0;
    bool started_ = false;
};

}  // namespace valgo
