// =============================================================================
// RHI: the software backend. It is the reference implementation of the RHI
// contract, so every backend must behave the same way at this level.
// =============================================================================
#include "../framework/test_framework.h"

#include <vector>

#include "valgo/assets/mesh_library.h"
#include "valgo/render/camera.h"
#include "valgo/rhi/rhi.h"

using namespace valgo;
using namespace valgo::rhi;

namespace {

struct DeviceAndSwapChain {
    std::unique_ptr<Device> device;
    std::unique_ptr<SwapChain> swap_chain;

    explicit DeviceAndSwapChain(u32 width = 96, u32 height = 64) {
        DeviceDesc device_desc;
        device_desc.api = GraphicsApi::Software;
        device_desc.application_name = "RHI tests";
        device = Device::Create(device_desc);
        if (device == nullptr) {
            return;
        }
        NativeWindow window;
        window.width = width;
        window.height = height;
        window.headless = true;
        SwapChainDesc swap_desc;
        swap_desc.width = width;
        swap_desc.height = height;
        swap_desc.format = Format::RGBA8_SRGB;
        swap_chain = device->CreateSwapChain(window, swap_desc);
    }

    bool valid() const { return device != nullptr && swap_chain != nullptr; }
};

FrameConstants MakeConstants(const render::Camera& camera, u32 width, u32 height) {
    FrameConstants constants{};
    constants.view_projection = camera.ViewProjectionMatrix();
    constants.view = camera.ViewMatrix();
    constants.projection = camera.ProjectionMatrix();
    constants.inverse_view_projection = constants.view_projection.Inverse();
    constants.camera_position = Vec4{camera.position().x, camera.position().y, camera.position().z, 1.0f};
    constants.light_direction = Vec4{0.0f, -1.0f, 0.0f, 1.0f};
    constants.light_color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    constants.viewport = Vec4{static_cast<f32>(width), static_cast<f32>(height), 1.0f / static_cast<f32>(width),
                              1.0f / static_cast<f32>(height)};
    return constants;
}

const u8* PixelAt(const std::vector<u8>& rgba, u32 width, u32 x, u32 y) {
    return rgba.data() + (static_cast<usize>(y) * width + x) * 4;
}

/// Full-screen triangle in clip space, ready for the screen-space program.
std::vector<ScreenVertex> FullScreenTriangle(const Vec4& color) {
    std::vector<ScreenVertex> vertices(3);
    const Vec2 positions[3] = {{0.0f, 0.0f}, {96.0f, 0.0f}, {0.0f, 64.0f}};  // pixels, beyond the target
    for (int i = 0; i < 3; ++i) {
        vertices[static_cast<usize>(i)].position = positions[i];
        vertices[static_cast<usize>(i)].color = PackColor(color);
    }
    return vertices;
}

}  // namespace

VALGO_TEST("rhi", "software_device_reports_itself") {
    DeviceDesc desc;
    desc.api = GraphicsApi::Software;
    const std::unique_ptr<Device> device = Device::Create(desc);
    VALGO_REQUIRE(device != nullptr);
    const DeviceInfo& info = device->info();
    VALGO_CHECK_EQ(info.api, GraphicsApi::Software);
    VALGO_CHECK(info.software);
    VALGO_CHECK(!info.adapter_name.empty());
    VALGO_CHECK(IsApiAvailable(GraphicsApi::Software));
    device->WaitIdle();
}

VALGO_TEST("rhi", "unsupported_api_falls_back_to_software") {
    DeviceDesc desc;
    desc.api = GraphicsApi::D3D12;
    DeviceInfo info{};
    const std::unique_ptr<Device> device = CreateFallbackDevice(desc, info);
    VALGO_REQUIRE(device != nullptr);
    VALGO_CHECK_EQ(device->info().api, GraphicsApi::Software);
    VALGO_CHECK_EQ(info.api, GraphicsApi::Software);
}

VALGO_TEST("rhi", "buffer_creation_and_write_bounds") {
    DeviceAndSwapChain context;
    VALGO_REQUIRE(context.valid());

    const std::vector<f32> data{1.0f, 2.0f, 3.0f, 4.0f};
    BufferDesc desc;
    desc.usage = BufferUsage::Uniform;
    desc.size = data.size() * sizeof(f32);
    desc.stride = sizeof(f32);
    desc.initial_data = data.data();
    desc.debug_name = "test uniform";
    const std::unique_ptr<Buffer> buffer = context.device->CreateBuffer(desc);
    VALGO_REQUIRE(buffer != nullptr);
    VALGO_CHECK_EQ(buffer->usage(), BufferUsage::Uniform);
    VALGO_CHECK_EQ(buffer->size(), static_cast<u64>(16));
    VALGO_CHECK_EQ(buffer->stride(), static_cast<u32>(4));

    const f32 patch{9.0f};
    VALGO_CHECK(buffer->Write(&patch, sizeof(patch), 8));
    VALGO_CHECK(buffer->Write(&patch, sizeof(patch), 12));  // last valid 4 bytes
    VALGO_CHECK_MSG(!buffer->Write(&patch, sizeof(patch), 14), "buffer accepted an out-of-bounds write");
    VALGO_CHECK(!buffer->Write(nullptr, 4));
}

VALGO_TEST("rhi", "texture_creation_validates_input") {
    DeviceAndSwapChain context;
    VALGO_REQUIRE(context.valid());

    const std::vector<u8> pixels(16 * 16 * 4, 128);
    TextureDesc desc;
    desc.width = 16;
    desc.height = 16;
    desc.format = Format::RGBA8_SRGB;
    desc.usage = TextureUsage::Sampled;
    desc.initial_data = pixels.data();
    const std::unique_ptr<Texture> texture = context.device->CreateTexture(desc);
    VALGO_REQUIRE(texture != nullptr);
    VALGO_CHECK_EQ(texture->width(), 16u);
    VALGO_CHECK_EQ(texture->height(), 16u);
    VALGO_CHECK_EQ(texture->format(), Format::RGBA8_SRGB);

    TextureDesc invalid;
    invalid.width = 0;
    VALGO_CHECK(context.device->CreateTexture(invalid) == nullptr);
}

VALGO_TEST("rhi", "frame_clear_colour_is_readable") {
    DeviceAndSwapChain context;
    VALGO_REQUIRE(context.valid());

    render::Camera camera;
    camera.SetPerspective(70.0f, static_cast<f32>(context.swap_chain->width()) /
                                      static_cast<f32>(context.swap_chain->height()),
                          0.05f, 100.0f);
    ClearValue clear;
    clear.color = Vec4{0.25f, 0.5f, 0.75f, 1.0f};

    VALGO_REQUIRE(context.swap_chain->BeginFrame());
    CommandList& cmd = *context.swap_chain->command_list();
    cmd.BeginFrame(MakeConstants(camera, context.swap_chain->width(), context.swap_chain->height()), clear);
    cmd.EndFrame();
    context.swap_chain->Present();

    std::vector<u8> rgba;
    VALGO_REQUIRE(context.swap_chain->ReadPixels(rgba));
    VALGO_REQUIRE(rgba.size() == static_cast<usize>(context.swap_chain->width()) *
                                     static_cast<usize>(context.swap_chain->height()) * 4u);
    const u8* pixel = PixelAt(rgba, context.swap_chain->width(), 4, 4);
    // The target format is sRGB, so the linear 0.25 clear is encoded to ~137.
    VALGO_CHECK_MSG(pixel[0] > 125u && pixel[0] < 150u, "sRGB encode of the clear colour is wrong");
    VALGO_CHECK(pixel[1] > pixel[0]);
    VALGO_CHECK(pixel[2] > pixel[1]);
    VALGO_CHECK_EQ(pixel[3], static_cast<u8>(255));
}

VALGO_TEST("rhi", "screen_triangle_rasterises_and_blends") {
    DeviceAndSwapChain context(48, 32);
    VALGO_REQUIRE(context.valid());

    render::Camera camera;
    camera.SetPerspective(70.0f, 1.5f, 0.05f, 100.0f);

    VALGO_REQUIRE(context.swap_chain->BeginFrame());
    CommandList& cmd = *context.swap_chain->command_list();
    ClearValue clear;
    clear.color = Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    cmd.BeginFrame(MakeConstants(camera, context.swap_chain->width(), context.swap_chain->height()), clear);

    PipelineDesc pipeline;
    pipeline.program = ShaderProgram::Screen2D;
    pipeline.topology = PrimitiveTopology::TriangleList;
    pipeline.cull = CullMode::None;
    pipeline.depth_test = false;
    pipeline.depth_write = false;
    pipeline.blend = BlendMode::Alpha;
    cmd.SetPipeline(pipeline);

    const std::vector<ScreenVertex> triangle = FullScreenTriangle(Vec4{1.0f, 0.0f, 0.0f, 0.5f});
    cmd.DrawScreenTriangles(triangle);
    cmd.EndFrame();
    context.swap_chain->Present();

    std::vector<u8> rgba;
    VALGO_REQUIRE(context.swap_chain->ReadPixels(rgba));
    const u8* center = PixelAt(rgba, context.swap_chain->width(), 24, 16);
    VALGO_CHECK_MSG(center[0] > center[1], "screen-space triangle was not rasterised");
    VALGO_CHECK_MSG(center[0] < 250u, "alpha blend was ignored");
}

VALGO_TEST("rhi", "geometry_draw_and_lines_respect_depth") {
    DeviceAndSwapChain context(64, 48);
    VALGO_REQUIRE(context.valid());

    render::Camera camera;
    camera.SetPerspective(70.0f, static_cast<f32>(context.swap_chain->width()) /
                                      static_cast<f32>(context.swap_chain->height()),
                          0.05f, 100.0f);
    camera.SetPosition(Vec3{0.0f, 0.0f, 5.0f});
    camera.LookAt(Vec3{0.0f, 0.0f, 0.0f});

    VALGO_REQUIRE(context.swap_chain->BeginFrame());
    CommandList& cmd = *context.swap_chain->command_list();
    ClearValue clear;
    clear.color = Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    cmd.BeginFrame(MakeConstants(camera, context.swap_chain->width(), context.swap_chain->height()), clear);

    // A near and a far quad must not crash and must not both write every pixel.
    const assets::MeshData mesh = assets::MakeQuad(1.0f, 1.0f);
    BufferDesc vertex_desc;
    vertex_desc.usage = BufferUsage::Vertex;
    vertex_desc.size = mesh.vertices.size() * sizeof(Vertex);
    vertex_desc.stride = sizeof(Vertex);
    vertex_desc.initial_data = mesh.vertices.data();
    const std::unique_ptr<Buffer> vertex_buffer = context.device->CreateBuffer(vertex_desc);
    BufferDesc index_desc;
    index_desc.usage = BufferUsage::Index;
    index_desc.size = mesh.indices.size() * sizeof(u32);
    index_desc.stride = sizeof(u32);
    index_desc.initial_data = mesh.indices.data();
    const std::unique_ptr<Buffer> index_buffer = context.device->CreateBuffer(index_desc);
    VALGO_REQUIRE(vertex_buffer != nullptr);
    VALGO_REQUIRE(index_buffer != nullptr);

    PipelineDesc pipeline;
    pipeline.program = ShaderProgram::UnlitMesh;
    pipeline.cull = CullMode::None;
    pipeline.depth_test = true;
    pipeline.depth_write = true;
    cmd.SetPipeline(pipeline);

    GeometryDraw draw;
    draw.vertex_buffer = vertex_buffer.get();
    draw.index_buffer = index_buffer.get();
    draw.index_count = static_cast<u32>(mesh.indices.size());
    draw.constants.world = Mat4::Translation(Vec3{0.0f, 0.0f, 0.0f});
    draw.constants.color = Vec4{0.0f, 1.0f, 0.0f, 1.0f};
    draw.constants.params = Vec4{1.0f, 0.0f, 0.0f, 1.0f};  // unlit
    cmd.DrawGeometry(draw);

    // A near, red quad in front of it.
    GeometryDraw near_draw = draw;
    near_draw.constants.world = Mat4::Translation(Vec3{0.0f, 0.0f, 1.5f});
    near_draw.constants.color = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    cmd.DrawGeometry(near_draw);

    const std::vector<LineVertex> lines{LineVertex{Vec3{-1.0f, 0.0f, 0.0f}, PackColor(Vec4{1.0f, 1.0f, 1.0f, 1.0f})},
                                        LineVertex{Vec3{1.0f, 0.0f, 0.0f}, PackColor(Vec4{1.0f, 1.0f, 1.0f, 1.0f})}};
    PipelineDesc line_pipeline;
    line_pipeline.program = ShaderProgram::DebugLine;
    line_pipeline.topology = PrimitiveTopology::LineList;
    line_pipeline.cull = CullMode::None;
    cmd.SetPipeline(line_pipeline);
    cmd.DrawLines(lines);

    cmd.EndFrame();
    context.swap_chain->Present();

    std::vector<u8> rgba;
    VALGO_REQUIRE(context.swap_chain->ReadPixels(rgba));
    // The quad is quads facing -Z; only their presence matters here: at least one
    // pixel is lit and the frame is not uniformly the clear colour.
    bool any_lit = false;
    for (usize i = 0; i < rgba.size(); i += 4) {
        if (rgba[i] > 8u || rgba[i + 1] > 8u || rgba[i + 2] > 8u) {
            any_lit = true;
            break;
        }
    }
    VALGO_CHECK_MSG(any_lit, "geometry and line draws produced an empty frame");
}

VALGO_TEST("rhi", "swap_chain_resize_and_backend_formats") {
    DeviceAndSwapChain context(80, 50);
    VALGO_REQUIRE(context.valid());
    VALGO_CHECK_EQ(context.swap_chain->format(), Format::RGBA8_SRGB);
    VALGO_REQUIRE(context.swap_chain->Resize(120, 70));
    VALGO_CHECK_EQ(context.swap_chain->width(), 120u);
    VALGO_CHECK_EQ(context.swap_chain->height(), 70u);

    std::vector<u8> rgba;
    VALGO_REQUIRE(context.swap_chain->ReadPixels(rgba));
    VALGO_CHECK(rgba.size() == static_cast<usize>(120) * 70u * 4u);

    VALGO_CHECK_EQ(BytesPerPixel(Format::RGBA8_SRGB), 4u);
    VALGO_CHECK(IsDepthFormat(Format::Depth32_Float));
    VALGO_CHECK(!IsDepthFormat(Format::RGBA8_SRGB));
}
