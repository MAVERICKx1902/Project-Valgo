#include "valgo/core/time.h"

#include <chrono>
#include <thread>

namespace valgo {
namespace {

using SteadyClock = std::chrono::steady_clock;

f64 SecondsSinceEpoch() {
    return std::chrono::duration<f64>(SteadyClock::now().time_since_epoch()).count();
}

}  // namespace

f64 MonotonicNowSeconds() { return SecondsSinceEpoch(); }

u64 MonotonicNowNanoseconds() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                SteadyClock::now().time_since_epoch())
                                .count());
}

void SleepMilliseconds(u32 milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

Stopwatch::Stopwatch() { Reset(); }

void Stopwatch::Reset() { start_seconds_ = MonotonicNowSeconds(); }

f64 Stopwatch::Seconds() const { return MonotonicNowSeconds() - start_seconds_; }

f64 Stopwatch::Milliseconds() const { return Seconds() * 1000.0; }

FrameTimer::FrameTimer(f64 fixed_step_seconds, f64 max_frame_seconds)
    : fixed_step_seconds_(fixed_step_seconds), max_frame_seconds_(max_frame_seconds) {}

void FrameTimer::SetFixedStepSeconds(f64 seconds) {
    if (seconds > 1.0e-6) {
        fixed_step_seconds_ = seconds;
    }
}

FrameContext FrameTimer::Advance() {
    const f64 now = MonotonicNowSeconds();

    FrameContext context;
    context.fixed_step_seconds = static_cast<f32>(fixed_step_seconds_);

    if (!started_) {
        started_ = true;
        last_time_seconds_ = now;
        accumulator_ = fixed_step_seconds_;  // Always simulate one step on frame 0.
        context.delta_seconds = fixed_step_seconds_;
    } else {
        f64 delta = now - last_time_seconds_;
        last_time_seconds_ = now;
        if (delta < 0.0) {
            delta = 0.0;
        }
        if (delta > max_frame_seconds_) {
            // A long stall (debugger, level load, window drag) must not become a
            // burst of hundreds of simulation steps.
            delta = max_frame_seconds_;
        }
        context.delta_seconds = delta;
        accumulator_ += delta;
    }

    total_seconds_ += context.delta_seconds;

    u32 steps = 0;
    constexpr u32 kMaxStepsPerFrame = 8;
    while (accumulator_ >= fixed_step_seconds_ && steps < kMaxStepsPerFrame) {
        accumulator_ -= fixed_step_seconds_;
        ++steps;
    }
    if (steps >= kMaxStepsPerFrame) {
        accumulator_ = 0.0;  // Drop the backlog rather than spiralling.
    }

    context.fixed_steps = steps;
    context.interpolation_alpha = static_cast<f32>(accumulator_ / fixed_step_seconds_);
    context.total_seconds = total_seconds_;
    context.frame_index = frame_index_++;
    return context;
}

}  // namespace valgo
