// =============================================================================
// Project Valgo — Rendering Hardware Interface (RHI).
//
// The RHI is the *only* place in the engine that knows about a graphics API.
// Renderer, scene, assets and games talk to this interface exclusively.
//
// Backends
//   D3D12    : the shipping GPU backend (Windows). See src/rhi/d3d12.
//   Software : a real CPU rasteriser implementing the same shader programs.
//              It exists so that continuous validation, automated tests and
//              headless screenshot capture work on machines without a GPU —
//              and so that renderer behaviour can be diffed between backends.
//
// Shader program model (deliberate scope of this milestone)
//   Rather than shipping a full shader compiler and reflection system yet, the
//   engine exposes a small, versioned set of *engine shader programs*
//   (ShaderProgram). Each backend implements the identical program, and the
//   renderer binds parameters through DrawConstants/FrameConstants. Replacing
//   ShaderProgram with a general pipeline/shader-compiler API is an isolated
//   change inside the RHI and is tracked in docs/RHI_AND_BACKENDS.md.
// =============================================================================
#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "valgo/core/types.h"
#include "valgo/math/math.h"

namespace valgo::rhi {

class Buffer;
class Texture;

// -----------------------------------------------------------------------------
// Enumerations and small value types
// -----------------------------------------------------------------------------
enum class GraphicsApi : u32 {
    Auto = 0,
    D3D12 = 1,
    Software = 2,
};

std::string_view GraphicsApiName(GraphicsApi api);

enum class Format : u32 {
    Unknown = 0,
    RGBA8_UNorm,
    RGBA8_SRGB,
    BGRA8_UNorm,
    BGRA8_SRGB,
    Depth32_Float,
};

u32 BytesPerPixel(Format format);
bool IsDepthFormat(Format format);

enum class BufferUsage : u32 {
    None = 0,
    Vertex = 1u << 0u,
    Index = 1u << 1u,
    Uniform = 1u << 2u,
    Readback = 1u << 3u,
};

constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}
constexpr bool HasFlag(BufferUsage value, BufferUsage flag) {
    return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0u;
}

enum class TextureUsage : u32 {
    None = 0,
    Sampled = 1u << 0u,
    RenderTarget = 1u << 1u,
    DepthTarget = 1u << 2u,
    Readback = 1u << 3u,
};

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}
constexpr bool HasFlag(TextureUsage value, TextureUsage flag) {
    return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0u;
}

enum class PrimitiveTopology : u32 { TriangleList = 0, LineList = 1 };

enum class CullMode : u32 { None = 0, Back = 1, Front = 2 };

enum class CompareOp : u32 { Less = 0, LessEqual = 1, Greater = 2, Always = 3 };

enum class BlendMode : u32 { Opaque = 0, Alpha = 1 };

/// The engine shader program set. See the note at the top of this file.
enum class ShaderProgram : u32 {
    LitMesh = 0,    ///< Directional light + ambient + fog, optional texture.
    UnlitMesh = 1,  ///< Vertex/instance colour only (muzzle flashes, UI in 3D).
    DebugLine = 2,  ///< 3D coloured lines with depth testing.
    Sky = 3,        ///< Full-screen sky gradient with distance fog blend.
    Screen2D = 4,   ///< Screen-space coloured quads with alpha blending.
    Count = 5,
};

std::string_view ShaderProgramName(ShaderProgram program);

/// GPU vertex layout shared by all mesh programs.
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

struct LineVertex {
    Vec3 position;
    u32 color;  ///< R8G8B8A8, packed by PackColor().
};

struct ScreenVertex {
    Vec2 position;  ///< Pixel coordinates, origin top-left.
    Vec2 uv;
    u32 color;      ///< R8G8B8A8.
};

inline u32 PackColor(const Vec4& color) {
    const u32 r = static_cast<u32>(Saturate(color.x) * 255.0f + 0.5f);
    const u32 g = static_cast<u32>(Saturate(color.y) * 255.0f + 0.5f);
    const u32 b = static_cast<u32>(Saturate(color.z) * 255.0f + 0.5f);
    const u32 a = static_cast<u32>(Saturate(color.w) * 255.0f + 0.5f);
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

inline Vec4 UnpackColor(u32 packed) {
    return Vec4{static_cast<f32>(packed & 0xFFu) / 255.0f,
                static_cast<f32>((packed >> 8u) & 0xFFu) / 255.0f,
                static_cast<f32>((packed >> 16u) & 0xFFu) / 255.0f,
                static_cast<f32>((packed >> 24u) & 0xFFu) / 255.0f};
}

// -----------------------------------------------------------------------------
// Descriptors
// -----------------------------------------------------------------------------
struct BufferDesc {
    BufferUsage usage = BufferUsage::Vertex;
    u64 size = 0;
    u32 stride = 0;
    const void* initial_data = nullptr;
    std::string_view debug_name;
};

struct TextureDesc {
    u32 width = 0;
    u32 height = 0;
    Format format = Format::RGBA8_SRGB;
    TextureUsage usage = TextureUsage::Sampled;
    const void* initial_data = nullptr;
    u32 row_pitch = 0;  ///< 0 means tightly packed.
    std::string_view debug_name;
};

struct PipelineDesc {
    ShaderProgram program = ShaderProgram::LitMesh;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cull = CullMode::Back;
    BlendMode blend = BlendMode::Opaque;
    CompareOp depth_compare = CompareOp::LessEqual;
    bool depth_test = true;
    bool depth_write = true;

    bool operator==(const PipelineDesc& other) const;
};

/// Constant buffer b1: per-frame data shared by every draw in a frame.
/// Layout must stay 16-byte aligned and match the HLSL cbuffer exactly.
struct FrameConstants {
    Mat4 view_projection;
    Mat4 view;
    Mat4 projection;
    Mat4 inverse_view_projection;
    Vec4 camera_position;    ///< xyz = eye position, w = 1
    Vec4 light_direction;    ///< xyz = direction towards the light, w = intensity
    Vec4 light_color;        ///< rgb = colour, a = 1
    Vec4 ambient_sky;        ///< rgb = sky ambient, a = 1
    Vec4 ambient_ground;     ///< rgb = ground bounce, a = 1
    Vec4 fog_color;          ///< rgb, a = 1
    Vec4 fog_parameters;     ///< x = start, y = end, z = height falloff, w = enabled
    Vec4 viewport;           ///< x = width, y = height, z = 1/width, w = 1/height
    Vec4 clear_color;        ///< rgb, a = 1
    Vec4 sky_zenith;         ///< rgb, a = 1. Linear. Sky colour straight up.
    Vec4 sky_horizon;        ///< rgb, a = 1. Linear. Sky colour at the horizon.
};

static_assert(sizeof(FrameConstants) == (4 * 64) + (11 * 16), "FrameConstants must stay tightly packed");

/// Root constants b0: per-draw data (<= 128 bytes, the D3D12 root constant limit).
struct DrawConstants {
    Mat4 world;
    Vec4 color;   ///< rgb tint / albedo, a = opacity
    Vec4 params;  ///< x = unlit amount, y = texture weight, z = emissive, w = tiling
};

static_assert(sizeof(DrawConstants) == 96, "DrawConstants must fit in 96 bytes of root constants");

struct GeometryDraw {
    const Buffer* vertex_buffer = nullptr;
    const Buffer* index_buffer = nullptr;
    const Texture* base_color_texture = nullptr;
    u32 index_count = 0;
    u32 first_index = 0;
    DrawConstants constants{};
};

struct ClearValue {
    Vec4 color{0.05f, 0.06f, 0.08f, 1.0f};
    f32 depth = 1.0f;
};

/// Native window description. The RHI never includes platform headers.
struct NativeWindow {
    void* window_handle = nullptr;  ///< HWND on Windows, nullptr when headless.
    u32 width = 0;
    u32 height = 0;
    bool headless = false;
};

struct DeviceDesc {
    GraphicsApi api = GraphicsApi::Auto;
    std::string application_name = "Valgo";
    bool enable_validation = true;
};

struct DeviceInfo {
    GraphicsApi api = GraphicsApi::Software;
    std::string adapter_name;
    std::string api_version;
    bool software = false;
};

struct SwapChainDesc {
    u32 width = 1280;
    u32 height = 720;
    Format format = Format::BGRA8_SRGB;
    u32 buffer_count = 3;
    bool vsync = true;
};

// -----------------------------------------------------------------------------
// Resources
// -----------------------------------------------------------------------------
class Buffer {
public:
    virtual ~Buffer() = default;

    virtual BufferUsage usage() const = 0;
    virtual u64 size() const = 0;
    virtual u32 stride() const = 0;

    /// Uploads `size` bytes at `offset`. Dynamic/upload buffers only.
    virtual bool Write(const void* data, u64 size, u64 offset = 0) = 0;
};

class Texture {
public:
    virtual ~Texture() = default;

    virtual u32 width() const = 0;
    virtual u32 height() const = 0;
    virtual Format format() const = 0;
};

class CommandList {
public:
    virtual ~CommandList() = default;

    /// Starts recording a frame and uploads the per-frame constants.
    virtual void BeginFrame(const FrameConstants& constants, const ClearValue& clear) = 0;

    /// Resolves and binds an engine shader program with the given fixed state.
    virtual void SetPipeline(const PipelineDesc& pipeline) = 0;

    virtual void DrawGeometry(const GeometryDraw& draw) = 0;

    /// Submits 3D line segments (pairs of vertices per segment).
    virtual void DrawLines(std::span<const LineVertex> vertices) = 0;

    /// Submits screen-space triangles for HUD/overlay drawing.
    virtual void DrawScreenTriangles(std::span<const ScreenVertex> vertices) = 0;

    virtual void EndFrame() = 0;
};

class SwapChain {
public:
    virtual ~SwapChain() = default;

    virtual u32 width() const = 0;
    virtual u32 height() const = 0;
    virtual Format format() const = 0;

    /// Acquires the next back buffer and prepares the frame's command list.
    virtual bool BeginFrame() = 0;

    /// Submits recorded work and presents.
    virtual void Present() = 0;

    virtual bool Resize(u32 width, u32 height) = 0;

    virtual CommandList* command_list() = 0;

    /// Copies the last presented frame as tightly packed RGBA8. Backends that
    /// cannot read back (yet) return false.
    virtual bool ReadPixels(std::vector<u8>& out_rgba) = 0;
};

class Device {
public:
    virtual ~Device() = default;

    /// Creates a device for the requested API. Returns nullptr on failure (the
    /// caller logs and can fall back, e.g. D3D12 -> Software).
    static std::unique_ptr<Device> Create(const DeviceDesc& desc);

    virtual const DeviceInfo& info() const = 0;

    virtual std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) = 0;
    virtual std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) = 0;
    virtual std::unique_ptr<SwapChain> CreateSwapChain(const NativeWindow& window,
                                                       const SwapChainDesc& desc) = 0;

    virtual void WaitIdle() = 0;
};

/// Returns true when the given API is compiled into this build.
bool IsApiAvailable(GraphicsApi api);

/// Convenience: creates a device, falling back to the software backend.
std::unique_ptr<Device> CreateFallbackDevice(const DeviceDesc& desc, DeviceInfo& out_info);

}  // namespace valgo::rhi
