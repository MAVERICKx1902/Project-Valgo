// =============================================================================
// Project Valgo — engine build configuration.
//
// Fixed limits and build identity that both the engine and games may read. This
// header contains no game concepts: it is the engine describing itself.
// =============================================================================
#pragma once

#include "valgo/core/types.h"

namespace valgo {

inline constexpr u32 kEngineVersionMajor = 0;
inline constexpr u32 kEngineVersionMinor = 1;
inline constexpr u32 kEngineVersionPatch = 0;
inline constexpr const char* kEngineName = "Project Valgo Engine";

/// Limits chosen so per-frame allocations stay predictable in this milestone.
/// They are documented constants, not tuning knobs, until a real allocator lands.
inline constexpr u32 kMaxFramesInFlight = 3;
inline constexpr u32 kMaxSwapChainBuffers = 3;
inline constexpr f32 kDefaultFixedStepSeconds = 1.0f / 60.0f;
inline constexpr f32 kMaxFrameSeconds = 0.25f;  ///< Clamp for wall-clock hitches.

#if defined(VALGO_PLATFORM_WINDOWS)
inline constexpr const char* kPlatformName = "windows";
#elif defined(VALGO_PLATFORM_LINUX)
inline constexpr const char* kPlatformName = "linux";
#else
inline constexpr const char* kPlatformName = "unknown";
#endif

#if defined(VALGO_RHI_HAS_D3D12)
inline constexpr bool kHasDirect3D12 = true;
#else
inline constexpr bool kHasDirect3D12 = false;
#endif

#if defined(VALGO_RHI_HAS_SOFTWARE)
inline constexpr bool kHasSoftwareRasteriser = true;
#else
inline constexpr bool kHasSoftwareRasteriser = false;
#endif

}  // namespace valgo
