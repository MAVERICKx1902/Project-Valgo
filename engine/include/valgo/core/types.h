// =============================================================================
// Project Valgo — engine core: fundamental types.
//
// Layer: CORE (depends on nothing but the C++ standard library).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace valgo {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;
using isize = std::ptrdiff_t;
using byte = std::byte;

inline constexpr u32 kInvalidIndex = 0xFFFFFFFFu;

/// Non-copyable / non-movable helper mixin.
struct NonCopyable {
    NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

}  // namespace valgo

#define VALGO_UNUSED(x) (void)(x)
#define VALGO_STRINGIFY_IMPL(x) #x
#define VALGO_STRINGIFY(x) VALGO_STRINGIFY_IMPL(x)
#define VALGO_CONCAT_IMPL(a, b) a##b
#define VALGO_CONCAT(a, b) VALGO_CONCAT_IMPL(a, b)
