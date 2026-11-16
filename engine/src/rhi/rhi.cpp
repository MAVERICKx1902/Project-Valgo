#include "valgo/rhi/rhi.h"

#include "valgo/core/log.h"

#if VALGO_RHI_HAS_D3D12
namespace valgo::rhi::d3d12 {
std::unique_ptr<Device> CreateDevice(const DeviceDesc& desc);
}  // namespace valgo::rhi::d3d12
#endif

#if VALGO_RHI_HAS_SOFTWARE
#include "rhi/software/software_rhi.h"
#endif

namespace valgo::rhi {
namespace {

constexpr const char* kCategory = "rhi";

}  // namespace

std::string_view GraphicsApiName(GraphicsApi api) {
    switch (api) {
        case GraphicsApi::Auto: return "auto";
        case GraphicsApi::D3D12: return "d3d12";
        case GraphicsApi::Software: return "software";
    }
    return "unknown";
}

std::string_view ShaderProgramName(ShaderProgram program) {
    switch (program) {
        case ShaderProgram::LitMesh: return "LitMesh";
        case ShaderProgram::UnlitMesh: return "UnlitMesh";
        case ShaderProgram::DebugLine: return "DebugLine";
        case ShaderProgram::Sky: return "Sky";
        case ShaderProgram::Screen2D: return "Screen2D";
        case ShaderProgram::Count: break;
    }
    return "unknown";
}

u32 BytesPerPixel(Format format) {
    switch (format) {
        case Format::RGBA8_UNorm:
        case Format::RGBA8_SRGB:
        case Format::BGRA8_UNorm:
        case Format::BGRA8_SRGB:
        case Format::Depth32_Float:
            return 4;
        case Format::Unknown:
            break;
    }
    return 0;
}

bool IsDepthFormat(Format format) { return format == Format::Depth32_Float; }

bool PipelineDesc::operator==(const PipelineDesc& other) const {
    return program == other.program && topology == other.topology && cull == other.cull &&
           blend == other.blend && depth_compare == other.depth_compare &&
           depth_test == other.depth_test && depth_write == other.depth_write;
}

bool IsApiAvailable(GraphicsApi api) {
    switch (api) {
        case GraphicsApi::Software:
#if VALGO_RHI_HAS_SOFTWARE
            return true;
#else
            return false;
#endif
        case GraphicsApi::D3D12:
#if VALGO_RHI_HAS_D3D12
            return true;
#else
            return false;
#endif
        case GraphicsApi::Auto:
            return true;
    }
    return false;
}

std::unique_ptr<Device> Device::Create(const DeviceDesc& desc) {
    GraphicsApi api = desc.api;
    if (api == GraphicsApi::Auto) {
#if VALGO_RHI_HAS_D3D12
        api = GraphicsApi::D3D12;
#else
        api = GraphicsApi::Software;
#endif
    }

    if (api == GraphicsApi::D3D12) {
#if VALGO_RHI_HAS_D3D12
        std::unique_ptr<Device> device = d3d12::CreateDevice(desc);
        if (device != nullptr) {
            return device;
        }
        LogWarn(kCategory, "Direct3D 12 device creation failed");
        if (desc.api == GraphicsApi::D3D12) {
            return nullptr;
        }
#else
        LogWarn(kCategory, "Direct3D 12 backend is not compiled into this build");
        if (desc.api == GraphicsApi::D3D12) {
            return nullptr;
        }
#endif
    }

    api = GraphicsApi::Software;
#if VALGO_RHI_HAS_SOFTWARE
    LogInfo(kCategory, "using the software rasteriser backend");
    return std::make_unique<software::SoftwareDevice>(desc);
#else
    LogError(kCategory, "no rendering backend is available in this build");
    VALGO_UNUSED(api);
    return nullptr;
#endif
}

std::unique_ptr<Device> CreateFallbackDevice(const DeviceDesc& desc, DeviceInfo& out_info) {
    DeviceDesc request = desc;
    std::unique_ptr<Device> device = Device::Create(request);
    if (device == nullptr) {
        LogWarn(kCategory, "falling back to the software rasteriser");
        request.api = GraphicsApi::Software;
        device = Device::Create(request);
    }
    if (device != nullptr) {
        out_info = device->info();
        LogInfo(kCategory, "rendering backend: {} ({})", GraphicsApiName(out_info.api), out_info.adapter_name);
    }
    return device;
}

}  // namespace valgo::rhi
