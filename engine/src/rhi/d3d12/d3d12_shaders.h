// =============================================================================
// Project Valgo — Direct3D 12 shader library (HLSL).
//
// One HLSL source file holds every engine shader program. Programs are compiled
// at startup with D3DCompile (d3dcompiler is linked by CMake) so the backend has
// no build-time shader toolchain dependency in this milestone; offline
// compilation into DXIL and a shader cache are tracked for milestone 2.
//
// Conventions shared with the software backend:
//   * cbuffer b1 FrameConstants and root constants b0 DrawConstants match the
//     C++ structures byte for byte (see valgo/rhi/rhi.h).
//   * Colours arriving in constants are already linear (the renderer converts
//     authored sRGB colours). Packed vertex colours (debug lines, 2D overlay)
//     are sRGB and are decoded here, exactly like the software rasteriser.
//   * The swap chain is an sRGB format, so the hardware encodes the final image.
// =============================================================================
#pragma once

#include <string_view>

#include "valgo/rhi/rhi.h"

namespace valgo::rhi::d3d12 {

/// Entry point names inside GetShaderLibrary().
std::string_view VertexEntryPoint(ShaderProgram program);
std::string_view PixelEntryPoint(ShaderProgram program);

/// The complete HLSL source for all programs.
const char* GetShaderLibrary();

}  // namespace valgo::rhi::d3d12
