// =============================================================================
// Project Valgo — Direct3D 12 RHI backend (Windows).
//
// Frames in flight
//   Each of the three frame slots owns a command allocator, a command list and a
//   committed upload buffer for FrameConstants. Acquiring a slot waits for that
//   slot's fence, so neither the allocator nor any per-frame upload buffer can
//   still be in use when it is reset or reused. No global device stall is needed
//   to present a frame.
//
// Root signature (shared by every program)
//   b0  root constants : DrawConstants (96 bytes, 24 DWORDs)
//   b1  root CBV       : FrameConstants (432 bytes; too large for root constants)
//   t0  descriptor tbl : base colour texture (sRGB), slot 0 is a white texture
//   s0  static sampler : linear wrap
//
// Resource policy for this milestone
//   Geometry buffers are committed UPLOAD heaps: always CPU writable, readable by
//   the GPU, no barriers. A GPU-local ring with staging copies is milestone 2
//   work and is recorded in docs/RHI_AND_BACKENDS.md.
//
// Colour
//   Render targets are sRGB. The renderer uploads linear colours; the hardware
//   encodes on write and blends in linear space, which is exactly what the
//   software backend does in software.
// =============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "valgo/core/log.h"
#include "valgo/rhi/rhi.h"

#if defined(VALGO_RHI_HAS_D3D12)

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include "d3d12_shaders.h"

namespace valgo::rhi::d3d12 {
namespace {

using Microsoft::WRL::ComPtr;

constexpr const char* kCategory = "rhi.d3d12";
constexpr u32 kFramesInFlight = 3;
constexpr u32 kSrvDescriptorCount = 32;
constexpr u32 kWhiteTextureSrv = 0;

/// Format is positional-only, so the hexadecimal code is pre-formatted.
std::string HexCode(HRESULT result) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", static_cast<unsigned>(result));
    return buffer;
}

void LogHresult(HRESULT result, const char* what) {
    LogError(kCategory, "{} failed (HRESULT {})", what, HexCode(result));
}

bool Checked(HRESULT result, const char* what) {
    if (SUCCEEDED(result)) {
        return true;
    }
    LogHresult(result, what);
    return false;
}

DXGI_FORMAT ToDxgi(Format format) {
    switch (format) {
        case Format::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case Format::Depth32_Float: return DXGI_FORMAT_D32_FLOAT;
        case Format::Unknown: break;
    }
    return DXGI_FORMAT_UNKNOWN;
}

D3D12_COMPARISON_FUNC ToComparison(CompareOp op) {
    switch (op) {
        case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
        case CompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
        case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
}

D3D12_CULL_MODE ToCull(CullMode mode) {
    switch (mode) {
        case CullMode::None: return D3D12_CULL_MODE_NONE;
        case CullMode::Back: return D3D12_CULL_MODE_BACK;
        case CullMode::Front: return D3D12_CULL_MODE_FRONT;
    }
    return D3D12_CULL_MODE_BACK;
}

}  // namespace

class D3D12Device;
class D3D12SwapChain;

/// Descriptors of the render targets the command list clears and binds.
struct FrameTargets {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    u32 width = 1;
    u32 height = 1;
};

// -----------------------------------------------------------------------------
// Buffer
// -----------------------------------------------------------------------------
class D3D12Buffer final : public Buffer {
public:
    D3D12Buffer(ComPtr<ID3D12Resource> resource, BufferUsage usage, u64 size, u32 stride, void* mapped)
        : resource_(std::move(resource)), usage_(usage), size_(size), stride_(stride), mapped_(mapped) {}

    BufferUsage usage() const override { return usage_; }
    u64 size() const override { return size_; }
    u32 stride() const override { return stride_; }

    bool Write(const void* data, u64 size, u64 offset) override {
        if (data == nullptr || mapped_ == nullptr || offset > size_ || size > size_ - offset) {
            return false;
        }
        std::memcpy(static_cast<u8*>(mapped_) + offset, data, static_cast<size_t>(size));
        return true;
    }

    ID3D12Resource* resource() const { return resource_.Get(); }

    D3D12_VERTEX_BUFFER_VIEW VertexView() const {
        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = resource_->GetGPUVirtualAddress();
        view.SizeInBytes = static_cast<UINT>(size_);
        view.StrideInBytes = stride_;
        return view;
    }

    D3D12_INDEX_BUFFER_VIEW IndexView() const {
        D3D12_INDEX_BUFFER_VIEW view{};
        view.BufferLocation = resource_->GetGPUVirtualAddress();
        view.SizeInBytes = static_cast<UINT>(size_);
        view.Format = DXGI_FORMAT_R32_UINT;
        return view;
    }

private:
    ComPtr<ID3D12Resource> resource_;
    BufferUsage usage_ = BufferUsage::None;
    u64 size_ = 0;
    u32 stride_ = 0;
    void* mapped_ = nullptr;
};

// -----------------------------------------------------------------------------
// Texture
// -----------------------------------------------------------------------------
class D3D12Texture final : public Texture {
public:
    D3D12Texture(ComPtr<ID3D12Resource> resource, u32 width, u32 height, Format format, u32 srv_index)
        : resource_(std::move(resource)), width_(width), height_(height), format_(format),
          srv_index_(srv_index) {}

    u32 width() const override { return width_; }
    u32 height() const override { return height_; }
    Format format() const override { return format_; }

    u32 srv_index() const { return srv_index_; }

private:
    ComPtr<ID3D12Resource> resource_;  // keeps the SRV valid for the texture's lifetime
    u32 width_ = 0;
    u32 height_ = 0;
    Format format_ = Format::RGBA8_SRGB;
    u32 srv_index_ = kWhiteTextureSrv;
};

// -----------------------------------------------------------------------------
// Device
// -----------------------------------------------------------------------------
class D3D12Device final : public Device {
public:
    explicit D3D12Device(const DeviceDesc& desc);
    ~D3D12Device() override;

    const DeviceInfo& info() const override { return info_; }
    std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override;
    std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) override;
    std::unique_ptr<SwapChain> CreateSwapChain(const NativeWindow& window, const SwapChainDesc& desc) override;
    void WaitIdle() override;

    struct FrameSlot {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Resource> frame_constants;
        FrameConstants* mapped_constants = nullptr;
        u64 fence_value = 0;
        bool in_use = false;
    };

    ID3D12Device* native_device() const { return device_.Get(); }
    ID3D12CommandQueue* queue() const { return queue_.Get(); }
    ID3D12RootSignature* root_signature() const { return root_signature_.Get(); }
    ID3D12DescriptorHeap* srv_heap() const { return srv_heap_.Get(); }
    ID3D12DescriptorHeap* rtv_heap() const { return rtv_heap_.Get(); }
    ID3D12DescriptorHeap* dsv_heap() const { return dsv_heap_.Get(); }
    u32 srv_stride() const { return srv_stride_; }

    /// Waits for the slot's last frame and resets its allocator.
    FrameSlot& AcquireFrameSlot(u32 index);

    /// Signals the fence for a slot after its command list was submitted.
    void SignalSlot(FrameSlot& slot);

    ID3D12PipelineState* GetOrCreatePipeline(const PipelineDesc& desc, DXGI_FORMAT rtv_format);
    u32 AllocateSrvSlot();

private:
    bool Initialise(const DeviceDesc& desc);
    bool CreateDeviceAndQueue(const DeviceDesc& desc);
    bool CreateDescriptorHeaps();
    bool CreateRootSignature();
    bool CreateWhiteTexture();
    bool UploadTextureData(ID3D12Resource* texture, const TextureDesc& desc, u32 row_pitch);
    void WriteShaderResourceView(u32 slot, ID3D12Resource* resource, Format format);
    void CopyToReadback(ID3D12Resource* source, ID3D12Resource* readback);

    ComPtr<IDXGIFactory4> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12DescriptorHeap> srv_heap_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    ComPtr<ID3D12Resource> white_texture_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    u64 fence_value_ = 0;
    u32 srv_stride_ = 0;
    u32 next_srv_slot_ = kWhiteTextureSrv + 1;
    std::array<FrameSlot, kFramesInFlight> slots_{};
    std::vector<std::pair<PipelineDesc, ComPtr<ID3D12PipelineState>>> pipelines_;
    std::vector<ComPtr<ID3DBlob>> shader_blobs_;
    DeviceInfo info_{};
    bool validation_ = false;

    friend class D3D12SwapChain;
    friend class D3D12CommandList;
};

// -----------------------------------------------------------------------------
// Command list
// -----------------------------------------------------------------------------
class D3D12CommandList final : public CommandList {
public:
    D3D12CommandList(D3D12Device* device, D3D12Device::FrameSlot* slot, const FrameTargets* targets)
        : device_(device), slot_(slot), targets_(targets) {}

    void BeginFrame(const FrameConstants& constants, const ClearValue& clear) override;
    void SetPipeline(const PipelineDesc& pipeline) override;
    void DrawGeometry(const GeometryDraw& draw) override;
    void DrawLines(std::span<const LineVertex> vertices) override;
    void DrawScreenTriangles(std::span<const ScreenVertex> vertices) override;
    void EndFrame() override;

    ID3D12GraphicsCommandList* list() const { return slot_->list.Get(); }

    /// Frees per-frame upload buffers. Safe once the slot's fence has completed.
    void ReleaseTransientBuffers() { transient_buffers_.clear(); }

private:
    void BindRendererState();
    void UploadAndDraw(std::span<const u8> bytes, u32 stride, u32 vertex_count);

    D3D12Device* device_ = nullptr;
    D3D12Device::FrameSlot* slot_ = nullptr;
    const FrameTargets* targets_ = nullptr;
    PipelineDesc pipeline_{};
    std::vector<std::unique_ptr<Buffer>> transient_buffers_;
};

// -----------------------------------------------------------------------------
// Swap chain
// -----------------------------------------------------------------------------
class D3D12SwapChain final : public SwapChain {
public:
    D3D12SwapChain(D3D12Device* device, const NativeWindow& window, const SwapChainDesc& desc);
    ~D3D12SwapChain() override;

    u32 width() const override { return width_; }
    u32 height() const override { return height_; }
    Format format() const override { return format_; }
    bool BeginFrame() override;
    void Present() override;
    bool Resize(u32 width, u32 height) override;
    CommandList* command_list() override { return command_list_.get(); }
    bool ReadPixels(std::vector<u8>& out_rgba) override;

private:
    bool CreateSwapChain(const SwapChainDesc& desc);
    bool CreateDepthBuffer();
    bool CreateRenderTargetViews();
    void ReleaseViews();

    D3D12Device* device_ = nullptr;
    HWND window_ = nullptr;
    ComPtr<IDXGISwapChain3> swap_chain_;
    std::array<ComPtr<ID3D12Resource>, kFramesInFlight> render_targets_{};
    ComPtr<ID3D12Resource> depth_target_;
    ComPtr<ID3D12Resource> readback_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles_[kFramesInFlight]{};
    FrameTargets targets_{};
    std::unique_ptr<D3D12CommandList> command_list_;
    u32 buffer_count_ = 2;
    u32 width_ = 1280;
    u32 height_ = 720;
    Format format_ = Format::BGRA8_SRGB;
    bool vsync_ = true;
    u32 frame_index_ = 0;
    D3D12Device::FrameSlot* current_slot_ = nullptr;
};

// =============================================================================
// D3D12Device implementation
// =============================================================================
D3D12Device::D3D12Device(const DeviceDesc& desc) {
    info_.api = GraphicsApi::D3D12;
    info_.software = false;

    if (!Initialise(desc)) {
        device_.Reset();
        queue_.Reset();
        info_.adapter_name.clear();
        return;
    }
    info_.api_version = validation_ ? "Direct3D 12 (validation)" : "Direct3D 12";
    LogInfo(kCategory, "Direct3D 12 device ready: {}", info_.adapter_name);
}

D3D12Device::~D3D12Device() {
    WaitIdle();
    for (FrameSlot& slot : slots_) {
        if (slot.frame_constants != nullptr && slot.mapped_constants != nullptr) {
            slot.frame_constants->Unmap(0, nullptr);
            slot.mapped_constants = nullptr;
        }
    }
    if (fence_event_ != nullptr) {
        CloseHandle(fence_event_);
    }
}

bool D3D12Device::Initialise(const DeviceDesc& desc) {
    validation_ = desc.enable_validation;

    UINT factory_flags = 0;
    if (validation_) {
        ComPtr<ID3D12Debug> debug_controller;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
            debug_controller->EnableDebugLayer();
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
    HRESULT result = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory_));
    if (FAILED(result) && factory_flags != 0) {
        factory_flags = 0;
        result = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory_));
    }
    if (!Checked(result, "CreateDXGIFactory2")) {
        return false;
    }

    if (!CreateDeviceAndQueue(desc)) {
        return false;
    }

    if (!Checked(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence")) {
        return false;
    }
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fence_event_ == nullptr) {
        LogError(kCategory, "CreateEventW failed");
        return false;
    }

    for (FrameSlot& slot : slots_) {
        if (!Checked(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     IID_PPV_ARGS(&slot.allocator)),
                     "CreateCommandAllocator")) {
            return false;
        }
        if (!Checked(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(),
                                                nullptr, IID_PPV_ARGS(&slot.list)),
                     "CreateCommandList")) {
            return false;
        }
        slot.list->Close();

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC buffer_desc{};
        buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer_desc.Width = 512;  // FrameConstants is 432 bytes; round up for alignment
        buffer_desc.Height = 1;
        buffer_desc.DepthOrArraySize = 1;
        buffer_desc.MipLevels = 1;
        buffer_desc.SampleDesc.Count = 1;
        buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!Checked(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                      IID_PPV_ARGS(&slot.frame_constants)),
                     "CreateCommittedResource(frame constants)")) {
            return false;
        }
        void* mapped = nullptr;
        if (FAILED(slot.frame_constants->Map(0, nullptr, &mapped))) {
            LogError(kCategory, "failed to map the frame constant buffer");
            return false;
        }
        slot.mapped_constants = static_cast<FrameConstants*>(mapped);
    }

    return CreateDescriptorHeaps() && CreateRootSignature() && CreateWhiteTexture();
}

bool D3D12Device::CreateDeviceAndQueue(const DeviceDesc& desc) {
    VALGO_UNUSED(desc);
    // Prefer a discrete adapter; software adapters are served by the CPU backend.
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; factory_->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 adapter_desc{};
        if (FAILED(adapter->GetDesc1(&adapter_desc))) {
            continue;
        }
        if ((adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                        nullptr))) {
            if (!Checked(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                           IID_PPV_ARGS(&device_)),
                         "D3D12CreateDevice")) {
                continue;
            }

            char name[256] = {};
            const int converted = WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1, name,
                                                     static_cast<int>(sizeof(name) - 1), nullptr, nullptr);
            info_.adapter_name = converted > 0 ? name : "Direct3D 12 adapter";

            D3D12_COMMAND_QUEUE_DESC queue_desc{};
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            if (!Checked(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)),
                         "CreateCommandQueue")) {
                device_.Reset();
                continue;
            }
            return true;
        }
    }
    LogError(kCategory, "no Direct3D 12 capable adapter found");
    return false;
}

bool D3D12Device::CreateDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = kSrvDescriptorCount;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!Checked(device_->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap_)), "CreateDescriptorHeap(SRV)")) {
        return false;
    }
    srv_stride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = kFramesInFlight;
    if (!Checked(device_->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_)), "CreateDescriptorHeap(RTV)")) {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsv_desc{};
    dsv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_desc.NumDescriptors = 1;
    if (!Checked(device_->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap_)), "CreateDescriptorHeap(DSV)")) {
        return false;
    }
    return true;
}

bool D3D12Device::CreateRootSignature() {
    D3D12_ROOT_PARAMETER parameters[3]{};

    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.RegisterSpace = 0;
    parameters[0].Constants.Num32BitValues = sizeof(DrawConstants) / sizeof(u32);
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 1;
    parameters[1].Descriptor.RegisterSpace = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &range;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters = 3;
    signature.pParameters = parameters;
    signature.NumStaticSamplers = 1;
    signature.pStaticSamplers = &sampler;
    signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                      D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1,
                                                       &serialized, &errors);
    if (!Checked(result, "D3D12SerializeRootSignature")) {
        if (errors != nullptr) {
            LogError(kCategory, "{}", static_cast<const char*>(errors->GetBufferPointer()));
        }
        return false;
    }
    return Checked(device_->CreateRootSignature(0, serialized->GetBufferPointer(),
                                                serialized->GetBufferSize(), IID_PPV_ARGS(&root_signature_)),
                   "CreateRootSignature");
}

void D3D12Device::WriteShaderResourceView(u32 slot, ID3D12Resource* resource, Format format) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = ToDxgi(format);
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(slot) * srv_stride_;
    device_->CreateShaderResourceView(resource, &srv, handle);
}

bool D3D12Device::CreateWhiteTexture() {
    const u32 white = 0xFFFFFFFFu;
    TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = Format::RGBA8_UNorm;
    desc.usage = TextureUsage::Sampled;
    desc.initial_data = &white;
    desc.row_pitch = 4;
    desc.debug_name = "white";

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = 1;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (!Checked(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource_desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&white_texture_)),
                 "CreateCommittedResource(white texture)")) {
        return false;
    }
    if (!UploadTextureData(white_texture_.Get(), desc, desc.row_pitch)) {
        return false;
    }
    WriteShaderResourceView(kWhiteTextureSrv, white_texture_.Get(), Format::RGBA8_UNorm);
    return true;
}

bool D3D12Device::UploadTextureData(ID3D12Resource* texture, const TextureDesc& desc, u32 row_pitch) {
    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    upload_heap.CreationNodeMask = 1;
    upload_heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = static_cast<UINT64>(row_pitch) * desc.height;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> upload;
    if (!Checked(device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                  IID_PPV_ARGS(&upload)),
                 "CreateCommittedResource(texture upload)")) {
        return false;
    }

    void* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, &mapped))) {
        return false;
    }
    const auto* source = static_cast<const u8*>(desc.initial_data);
    for (u32 y = 0; y < desc.height; ++y) {
        std::memcpy(static_cast<u8*>(mapped) + static_cast<size_t>(y) * row_pitch,
                    source + static_cast<size_t>(y) * row_pitch, row_pitch);
    }
    upload->Unmap(0, nullptr);

    WaitIdle();
    FrameSlot& slot = slots_[0];
    slot.allocator->Reset();
    slot.list->Reset(slot.allocator.Get(), nullptr);

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = texture;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION source_location{};
    source_location.pResource = upload.Get();
    source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source_location.PlacedFootprint.Offset = 0;
    source_location.PlacedFootprint.Footprint.Format = ToDxgi(desc.format);
    source_location.PlacedFootprint.Footprint.Width = desc.width;
    source_location.PlacedFootprint.Footprint.Height = desc.height;
    source_location.PlacedFootprint.Footprint.Depth = 1;
    source_location.PlacedFootprint.Footprint.RowPitch = row_pitch;
    slot.list->CopyTextureRegion(&destination, 0, 0, 0, &source_location, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    slot.list->ResourceBarrier(1, &barrier);
    slot.list->Close();

    ID3D12CommandList* lists[] = {slot.list.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitIdle();
    return true;
}

u32 D3D12Device::AllocateSrvSlot() {
    // Slot 0 is reserved for the white texture; the rest is a simple ring.
    const u32 slot = next_srv_slot_;
    next_srv_slot_ = slot + 1;
    if (next_srv_slot_ >= kSrvDescriptorCount) {
        next_srv_slot_ = kWhiteTextureSrv + 1;
    }
    return slot;
}

ID3D12PipelineState* D3D12Device::GetOrCreatePipeline(const PipelineDesc& desc, DXGI_FORMAT rtv_format) {
    for (const auto& entry : pipelines_) {
        if (entry.first == desc) {
            return entry.second.Get();
        }
    }
    if (desc.program == ShaderProgram::Count) {
        return nullptr;
    }

    const std::string vertex_entry(VertexEntryPoint(desc.program));
    const std::string pixel_entry(PixelEntryPoint(desc.program));
    const char* source = GetShaderLibrary();
    const SIZE_T source_size = std::strlen(source);
    const UINT flags = validation_ ? D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION
                                   : D3DCOMPILE_OPTIMIZATION_LEVEL3;

    ComPtr<ID3DBlob> vertex_blob;
    ComPtr<ID3DBlob> pixel_blob;
    ComPtr<ID3DBlob> errors;

    HRESULT result = D3DCompile(source, source_size, "valgo_engine.hlsl", nullptr, nullptr,
                                vertex_entry.c_str(), "vs_5_1", flags, 0, &vertex_blob, &errors);
    if (FAILED(result)) {
        LogError(kCategory, "shader compile failed ({}): {}", vertex_entry,
                 errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
        return nullptr;
    }
    errors.Reset();
    result = D3DCompile(source, source_size, "valgo_engine.hlsl", nullptr, nullptr, pixel_entry.c_str(),
                        "ps_5_1", flags, 0, &pixel_blob, &errors);
    if (FAILED(result)) {
        LogError(kCategory, "shader compile failed ({}): {}", pixel_entry,
                 errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
        return nullptr;
    }

    D3D12_INPUT_ELEMENT_DESC layout[3]{};
    UINT layout_count = 0;
    switch (desc.program) {
        case ShaderProgram::LitMesh:
        case ShaderProgram::UnlitMesh:
            layout[0] = {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
            layout[1] = {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
            layout[2] = {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
            layout_count = 3;
            break;
        case ShaderProgram::DebugLine:
            layout[0] = {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
            layout[1] = {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 12,
                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
            layout_count = 2;
            break;
        case ShaderProgram::Screen2D:
            layout[0] = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
            layout[1] = {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
            layout[2] = {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 16,
                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
            layout_count = 3;
            break;
        case ShaderProgram::Sky:
            layout_count = 0;  // SV_VertexID only
            break;
        case ShaderProgram::Count:
            return nullptr;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_signature_.Get();
    pso.VS = {vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize()};
    pso.PS = {pixel_blob->GetBufferPointer(), pixel_blob->GetBufferSize()};
    pso.InputLayout = {layout, layout_count};
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = ToCull(desc.cull);
    pso.RasterizerState.FrontCounterClockwise = TRUE;  // Valgo front faces are CCW
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = desc.depth_test ? TRUE : FALSE;
    pso.DepthStencilState.DepthWriteMask = desc.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL
                                                           : D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = ToComparison(desc.depth_compare);
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.BlendState.RenderTarget[0].BlendEnable = desc.blend == BlendMode::Alpha ? TRUE : FALSE;
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = desc.topology == PrimitiveTopology::LineList
                                    ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                                    : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = rtv_format;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    pso.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

    ComPtr<ID3D12PipelineState> state;
    if (!Checked(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&state)),
                 "CreateGraphicsPipelineState")) {
        return nullptr;
    }
    shader_blobs_.push_back(vertex_blob);
    shader_blobs_.push_back(pixel_blob);
    pipelines_.emplace_back(desc, state);
    return pipelines_.back().second.Get();
}

D3D12Device::FrameSlot& D3D12Device::AcquireFrameSlot(u32 index) {
    FrameSlot& slot = slots_[index % kFramesInFlight];
    if (slot.in_use && fence_->GetCompletedValue() < slot.fence_value) {
        fence_->SetEventOnCompletion(slot.fence_value, fence_event_);
        WaitForSingleObject(fence_event_, INFINITE);
    }
    slot.allocator->Reset();
    slot.list->Reset(slot.allocator.Get(), nullptr);
    slot.in_use = true;
    return slot;
}

void D3D12Device::SignalSlot(FrameSlot& slot) {
    slot.fence_value = ++fence_value_;
    if (FAILED(queue_->Signal(fence_.Get(), slot.fence_value))) {
        LogError(kCategory, "queue Signal failed");
    }
}

std::unique_ptr<Buffer> D3D12Device::CreateBuffer(const BufferDesc& desc) {
    if (device_ == nullptr || desc.size == 0) {
        return nullptr;
    }
    if (desc.usage == BufferUsage::Readback) {
        LogError(kCategory, "readback buffers are created internally by the swap chain");
        return nullptr;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = desc.size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> resource;
    if (!Checked(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource_desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                  IID_PPV_ARGS(&resource)),
                 "CreateCommittedResource(buffer)")) {
        return nullptr;
    }
    void* mapped = nullptr;
    if (FAILED(resource->Map(0, nullptr, &mapped))) {
        LogError(kCategory, "failed to map an upload buffer");
        return nullptr;
    }
    auto buffer = std::make_unique<D3D12Buffer>(resource, desc.usage, desc.size, desc.stride, mapped);
    if (desc.initial_data != nullptr && !buffer->Write(desc.initial_data, desc.size, 0)) {
        return nullptr;
    }
    return buffer;
}

std::unique_ptr<Texture> D3D12Device::CreateTexture(const TextureDesc& desc) {
    if (device_ == nullptr || desc.width == 0 || desc.height == 0 || desc.format == Format::Unknown ||
        IsDepthFormat(desc.format)) {
        LogError(kCategory, "CreateTexture rejected an invalid descriptor");
        return nullptr;
    }
    const u32 row_pitch = desc.row_pitch != 0 ? desc.row_pitch : desc.width * BytesPerPixel(desc.format);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = desc.width;
    resource_desc.Height = desc.height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = ToDxgi(desc.format);
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ComPtr<ID3D12Resource> resource;
    if (!Checked(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource_desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&resource)),
                 "CreateCommittedResource(texture)")) {
        return nullptr;
    }

    if (desc.initial_data != nullptr) {
        if (!UploadTextureData(resource.Get(), desc, row_pitch)) {
            return nullptr;
        }
    } else {
        // No data: transition out of COPY_DEST so the SRV is immediately valid.
        WaitIdle();
        FrameSlot& slot = slots_[0];
        slot.allocator->Reset();
        slot.list->Reset(slot.allocator.Get(), nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        slot.list->ResourceBarrier(1, &barrier);
        slot.list->Close();
        ID3D12CommandList* lists[] = {slot.list.Get()};
        queue_->ExecuteCommandLists(1, lists);
        WaitIdle();
    }

    const u32 srv_index = AllocateSrvSlot();
    WriteShaderResourceView(srv_index, resource.Get(), desc.format);
    return std::make_unique<D3D12Texture>(resource, desc.width, desc.height, desc.format, srv_index);
}

std::unique_ptr<SwapChain> D3D12Device::CreateSwapChain(const NativeWindow& window,
                                                       const SwapChainDesc& desc) {
    if (device_ == nullptr) {
        return nullptr;
    }
    if (window.window_handle == nullptr || window.headless) {
        LogWarn(kCategory, "headless runs use the software backend");
        return nullptr;
    }
    auto swap_chain = std::make_unique<D3D12SwapChain>(this, window, desc);
    if (swap_chain->width() == 0) {
        return nullptr;
    }
    return swap_chain;
}

void D3D12Device::WaitIdle() {
    if (queue_ == nullptr || fence_ == nullptr || fence_event_ == nullptr) {
        return;
    }
    const u64 value = ++fence_value_;
    if (FAILED(queue_->Signal(fence_.Get(), value))) {
        return;
    }
    if (fence_->GetCompletedValue() < value) {
        fence_->SetEventOnCompletion(value, fence_event_);
        WaitForSingleObject(fence_event_, INFINITE);
    }
}

void D3D12Device::CopyToReadback(ID3D12Resource* source, ID3D12Resource* readback) {
    if (source == nullptr || readback == nullptr) {
        return;
    }
    WaitIdle();
    FrameSlot& slot = slots_[0];
    slot.allocator->Reset();
    slot.list->Reset(slot.allocator.Get(), nullptr);
    D3D12_RESOURCE_BARRIER to_copy{};
    to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy.Transition.pResource = source;
    to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    slot.list->ResourceBarrier(1, &to_copy);
    slot.list->CopyResource(readback, source);
    D3D12_RESOURCE_BARRIER to_present = to_copy;
    to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    slot.list->ResourceBarrier(1, &to_present);
    slot.list->Close();
    ID3D12CommandList* lists[] = {slot.list.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitIdle();
}

// =============================================================================
// D3D12CommandList implementation
// =============================================================================
void D3D12CommandList::BeginFrame(const FrameConstants& constants, const ClearValue& clear) {
    ID3D12GraphicsCommandList* list = slot_->list.Get();
    *slot_->mapped_constants = constants;

    ID3D12DescriptorHeap* heaps[] = {device_->srv_heap()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(device_->root_signature());
    list->SetGraphicsRootConstantBufferView(
        1, slot_->frame_constants->GetGPUVirtualAddress());
    BindRendererState();

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = targets_->rtv;
    list->ClearRenderTargetView(rtv, &clear.color.x, 0, nullptr);
    list->ClearDepthStencilView(targets_->dsv, D3D12_CLEAR_FLAG_DEPTH, clear.depth, 0, 0, nullptr);
    list->OMSetRenderTargets(1, &rtv, FALSE, &targets_->dsv);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<f32>(targets_->width);
    viewport.Height = static_cast<f32>(targets_->height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, static_cast<LONG>(targets_->width), static_cast<LONG>(targets_->height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
}

void D3D12CommandList::BindRendererState() {
    ID3D12GraphicsCommandList* list = slot_->list.Get();
    D3D12_GPU_DESCRIPTOR_HANDLE white = device_->srv_heap()->GetGPUDescriptorHandleForHeapStart();
    list->SetGraphicsRootDescriptorTable(2, white);
}

void D3D12CommandList::SetPipeline(const PipelineDesc& pipeline) {
    pipeline_ = pipeline;
    ID3D12PipelineState* state = device_->GetOrCreatePipeline(pipeline, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    if (state == nullptr) {
        return;
    }
    ID3D12GraphicsCommandList* list = slot_->list.Get();
    list->SetPipelineState(state);
    list->IASetPrimitiveTopology(pipeline.topology == PrimitiveTopology::LineList
                                     ? D3D12_PRIMITIVE_TOPOLOGY_LINELIST
                                     : D3D12_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void D3D12CommandList::DrawGeometry(const GeometryDraw& draw) {
    ID3D12GraphicsCommandList* list = slot_->list.Get();
    list->SetGraphicsRoot32BitConstants(0, sizeof(DrawConstants) / sizeof(u32), &draw.constants, 0);

    u32 srv_index = kWhiteTextureSrv;
    if (const auto* texture = dynamic_cast<const D3D12Texture*>(draw.base_color_texture)) {
        srv_index = texture->srv_index();
    }
    D3D12_GPU_DESCRIPTOR_HANDLE srv = device_->srv_heap()->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += static_cast<UINT64>(srv_index) * device_->srv_stride();
    list->SetGraphicsRootDescriptorTable(2, srv);

    if (pipeline_.program == ShaderProgram::Sky) {
        list->DrawInstanced(3, 1, 0, 0);
        return;
    }

    const auto* vertex_buffer = dynamic_cast<const D3D12Buffer*>(draw.vertex_buffer);
    const auto* index_buffer = dynamic_cast<const D3D12Buffer*>(draw.index_buffer);
    if (vertex_buffer == nullptr || index_buffer == nullptr || draw.index_count == 0) {
        return;
    }
    const D3D12_VERTEX_BUFFER_VIEW vertex_view = vertex_buffer->VertexView();
    const D3D12_INDEX_BUFFER_VIEW index_view = index_buffer->IndexView();
    list->IASetVertexBuffers(0, 1, &vertex_view);
    list->IASetIndexBuffer(&index_view);
    list->DrawIndexedInstanced(draw.index_count, 1, draw.first_index, 0, 0);
}

void D3D12CommandList::UploadAndDraw(std::span<const u8> bytes, u32 stride, u32 vertex_count) {
    BufferDesc desc;
    desc.usage = BufferUsage::Vertex;
    desc.size = bytes.size();
    desc.stride = stride;
    std::unique_ptr<Buffer> buffer = device_->CreateBuffer(desc);
    if (buffer == nullptr) {
        return;
    }
    if (!buffer->Write(bytes.data(), bytes.size(), 0)) {
        return;
    }

    const auto* d3d_buffer = static_cast<const D3D12Buffer*>(buffer.get());
    const D3D12_VERTEX_BUFFER_VIEW view = d3d_buffer->VertexView();

    DrawConstants constants{};
    constants.world = Mat4::Identity();
    constants.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    constants.params = Vec4{1.0f, 0.0f, 0.0f, 1.0f};

    ID3D12GraphicsCommandList* list = slot_->list.Get();
    list->SetGraphicsRoot32BitConstants(0, sizeof(DrawConstants) / sizeof(u32), &constants, 0);
    BindRendererState();
    list->IASetVertexBuffers(0, 1, &view);
    list->DrawInstanced(vertex_count, 1, 0, 0);

    transient_buffers_.push_back(std::move(buffer));
}

void D3D12CommandList::DrawLines(std::span<const LineVertex> vertices) {
    if (vertices.size() < 2) {
        return;
    }
    UploadAndDraw(std::span<const u8>(reinterpret_cast<const u8*>(vertices.data()),
                                      vertices.size() * sizeof(LineVertex)),
                  sizeof(LineVertex), static_cast<u32>(vertices.size()));
}

void D3D12CommandList::DrawScreenTriangles(std::span<const ScreenVertex> vertices) {
    if (vertices.size() < 3) {
        return;
    }
    UploadAndDraw(std::span<const u8>(reinterpret_cast<const u8*>(vertices.data()),
                                      vertices.size() * sizeof(ScreenVertex)),
                  sizeof(ScreenVertex), static_cast<u32>(vertices.size()));
}

void D3D12CommandList::EndFrame() {
    if (ID3D12GraphicsCommandList* list = slot_->list.Get()) {
        list->Close();
    }
}

// =============================================================================
// D3D12SwapChain implementation
// =============================================================================
D3D12SwapChain::D3D12SwapChain(D3D12Device* device, const NativeWindow& window, const SwapChainDesc& desc)
    : device_(device), window_(static_cast<HWND>(window.window_handle)) {
    width_ = window.width != 0 ? window.width : desc.width;
    height_ = window.height != 0 ? window.height : desc.height;
    vsync_ = desc.vsync;
    buffer_count_ = std::min(std::max(desc.buffer_count, 2u), kFramesInFlight);

    if (!CreateSwapChain(desc) || !CreateDepthBuffer() || !CreateRenderTargetViews()) {
        width_ = 0;
        height_ = 0;
        return;
    }
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    targets_.width = width_;
    targets_.height = height_;
    FrameSlot& slot = device_->AcquireFrameSlot(frame_index_);
    current_slot_ = &slot;
    command_list_ = std::make_unique<D3D12CommandList>(device_, &slot, &targets_);
}

D3D12SwapChain::~D3D12SwapChain() {
    command_list_.reset();
    if (device_ != nullptr) {
        device_->WaitIdle();
    }
    ReleaseViews();
}

bool D3D12SwapChain::CreateSwapChain(const SwapChainDesc& desc) {
    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Width = width_;
    swap_desc.Height = height_;
    swap_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    swap_desc.Stereo = FALSE;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = buffer_count_;
    swap_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swap_desc.Flags = 0;

    ComPtr<IDXGISwapChain1> swap_chain;
    if (!Checked(device_->factory_->CreateSwapChainForHwnd(device_->queue(), window_, &swap_desc, nullptr,
                                                          nullptr, &swap_chain),
                 "CreateSwapChainForHwnd")) {
        return false;
    }
    device_->factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
    if (!Checked(swap_chain.As(&swap_chain_), "QueryInterface(IDXGISwapChain3)")) {
        return false;
    }
    for (u32 i = 0; i < buffer_count_; ++i) {
        if (!Checked(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i])),
                     "GetBuffer(swap chain)")) {
            return false;
        }
    }
    format_ = Format::BGRA8_SRGB;  // the views are created as sRGB
    VALGO_UNUSED(desc);
    return true;
}

bool D3D12SwapChain::CreateDepthBuffer() {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    return Checked(device_->native_device()->CreateCommittedResource(
                       &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                       IID_PPV_ARGS(&depth_target_)),
                   "CreateCommittedResource(depth)");
}

bool D3D12SwapChain::CreateRenderTargetViews() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = device_->rtv_heap()->GetCPUDescriptorHandleForHeapStart();
    const u32 stride =
        device_->native_device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;  // matches the swap chain format
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (u32 i = 0; i < buffer_count_; ++i) {
        if (render_targets_[i] == nullptr) {
            return false;
        }
        rtv_handles_[i] = rtv;
        rtv.ptr += stride;
        device_->native_device()->CreateRenderTargetView(render_targets_[i].Get(), &rtv_desc, rtv_handles_[i]);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = device_->dsv_heap()->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->native_device()->CreateDepthStencilView(depth_target_.Get(), &dsv_desc, dsv);

    targets_.dsv = dsv;
    targets_.width = width_;
    targets_.height = height_;
    return true;
}

void D3D12SwapChain::ReleaseViews() {
    for (ComPtr<ID3D12Resource>& target : render_targets_) {
        target.Reset();
    }
    depth_target_.Reset();
    readback_.Reset();
    for (D3D12_CPU_DESCRIPTOR_HANDLE& handle : rtv_handles_) {
        handle = {};
    }
}

bool D3D12SwapChain::BeginFrame() {
    if (swap_chain_ == nullptr) {
        return false;
    }
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();

    D3D12Device::FrameSlot& slot = device_->AcquireFrameSlot(frame_index_);
    current_slot_ = &slot;
    if (command_list_ == nullptr) {
        command_list_ = std::make_unique<D3D12CommandList>(device_, &slot, &targets_);
    } else {
        command_list_->ReleaseTransientBuffers();
    }
    targets_.rtv = rtv_handles_[frame_index_];
    targets_.dsv = device_->dsv_heap()->GetCPUDescriptorHandleForHeapStart();
    targets_.width = width_;
    targets_.height = height_;
    return true;
}

void D3D12SwapChain::Present() {
    if (swap_chain_ == nullptr || command_list_ == nullptr || current_slot_ == nullptr) {
        return;
    }
    command_list_->EndFrame();
    ID3D12CommandList* lists[] = {command_list_->list()};
    device_->queue()->ExecuteCommandLists(1, lists);
    device_->SignalSlot(*current_slot_);
    // Transient per-frame buffers are freed on the next AcquireFrameSlot() after
    // this fence, which is why they live in the command list rather than here.
    if (FAILED(swap_chain_->Present(vsync_ ? 1u : 0u, 0))) {
        LogError(kCategory, "Present failed; device removed reason {}", 
                 static_cast<u32>(device_->native_device()->GetDeviceRemovedReason()));
    }
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

bool D3D12SwapChain::Resize(u32 width, u32 height) {
    if (swap_chain_ == nullptr || width == 0 || height == 0) {
        return false;
    }
    if (width == width_ && height == height_) {
        return true;
    }
    command_list_.reset();
    device_->WaitIdle();
    ReleaseViews();

    width_ = width;
    height_ = height;
    if (!Checked(swap_chain_->ResizeBuffers(buffer_count_, width_, height_,
                                           DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, 0),
                 "ResizeBuffers")) {
        return false;
    }
    for (u32 i = 0; i < buffer_count_; ++i) {
        if (!Checked(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i])), "GetBuffer(resize)")) {
            return false;
        }
    }
    if (!CreateDepthBuffer() || !CreateRenderTargetViews()) {
        return false;
    }
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    return true;
}

bool D3D12SwapChain::ReadPixels(std::vector<u8>& out_rgba) {
    if (swap_chain_ == nullptr) {
        return false;
    }
    ID3D12Resource* back_buffer = render_targets_[frame_index_].Get();
    if (back_buffer == nullptr) {
        return false;
    }
    const u64 row_pitch = static_cast<u64>(width_) * 4u;
    const u64 required = row_pitch * height_;

    if (readback_ == nullptr) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = required;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!Checked(device_->native_device()->CreateCommittedResource(
                         &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                         IID_PPV_ARGS(&readback_)),
                     "CreateCommittedResource(readback)")) {
            return false;
        }
    }

    device_->CopyToReadback(back_buffer, readback_.Get());

    D3D12_RANGE read_range{0, static_cast<SIZE_T>(required)};
    void* mapped = nullptr;
    if (FAILED(readback_->Map(0, &read_range, &mapped))) {
        return false;
    }
    out_rgba.resize(static_cast<size_t>(required));
    const auto* source = static_cast<const u8*>(mapped);
    for (u32 y = 0; y < height_; ++y) {
        const u8* source_row = source + static_cast<u64>(y) * row_pitch;
        u8* target_row = out_rgba.data() + static_cast<u64>(y) * row_pitch;
        for (u32 x = 0; x < width_; ++x) {
            target_row[x * 4 + 0] = source_row[x * 4 + 2];
            target_row[x * 4 + 1] = source_row[x * 4 + 1];
            target_row[x * 4 + 2] = source_row[x * 4 + 0];
            target_row[x * 4 + 3] = 255;
        }
    }
    const D3D12_RANGE no_write{0, 0};
    readback_->Unmap(0, &no_write);
    return true;
}

std::unique_ptr<Device> CreateDevice(const DeviceDesc& desc) {
    auto device = std::make_unique<D3D12Device>(desc);
    if (device->info().adapter_name.empty()) {
        return nullptr;
    }
    return device;
}

}  // namespace valgo::rhi::d3d12

#else  // !VALGO_RHI_HAS_D3D12

namespace valgo::rhi::d3d12 {

std::unique_ptr<Device> CreateDevice(const DeviceDesc& desc) {
    VALGO_UNUSED(desc);
    LogError("rhi.d3d12", "Direct3D 12 support is not compiled into this build");
    return nullptr;
}

}  // namespace valgo::rhi::d3d12

#endif  // VALGO_RHI_HAS_D3D12
