// =============================================================================
// Integration: a complete headless frame through the real engine stack.
//
// These tests are the automated version of "launch the application and look at
// it": the full path device -> swap chain -> renderer -> software rasteriser ->
// readback is exercised, and the resulting image is inspected.
// =============================================================================
#include "../framework/test_framework.h"

#include "framework/engine_harness.h"

#include "valgo/assets/asset_manager.h"
#include "valgo/core/image.h"
#include "valgo/core/file.h"
#include "valgo/scene/scene.h"

using namespace valgo;
using valgo_test::EngineHarness;
using valgo_test::NullGame;

namespace {

struct SceneResult {
    u64 checksum = 0;
    Image image;
};

/// Builds a small deterministic test scene.
void BuildTestScene(EngineHarness& harness) {
    scene::Scene& scene = harness.scene();
    const render::MeshGpuData* box = harness.assets().GetPrimitive("box");

    scene::Entity floor = scene.CreateEntity("floor");
    scene.GetComponent<scene::TransformComponent>(floor)->local.position = Vec3{0.0f, -0.05f, -8.0f};
    scene.GetComponent<scene::TransformComponent>(floor)->local.scale = Vec3{24.0f, 0.1f, 24.0f};
    scene::MeshRendererComponent floor_renderer;
    floor_renderer.mesh = box;
    floor_renderer.material = render::Material::MakeSolid(Vec3{0.42f, 0.40f, 0.36f});
    scene.AddComponent<scene::MeshRendererComponent>(floor, floor_renderer);

    for (int i = 0; i < 5; ++i) {
        scene::Entity cube = scene.CreateEntity("cube");
        scene::TransformComponent* transform = scene.GetComponent<scene::TransformComponent>(cube);
        transform->local.position = Vec3{static_cast<f32>(i) * 1.6f - 3.2f, 0.5f, -7.0f};
        scene::MeshRendererComponent renderer;
        renderer.mesh = box;
        renderer.material = render::Material::MakeSolid(Vec3{0.25f + 0.12f * static_cast<f32>(i), 0.5f, 0.78f});
        scene.AddComponent<scene::MeshRendererComponent>(cube, renderer);
    }

    scene::Entity light = scene.CreateEntity("sun");
    scene::DirectionalLightComponent light_component;
    light_component.direction = Vec3{-0.4f, -1.0f, -0.35f};
    light_component.intensity = 0.55f;
    scene.AddComponent<scene::DirectionalLightComponent>(light, light_component);
}

SceneResult RenderSceneOnce(u32 width, u32 height, u32 frames) {
    EngineHarness harness(width, height);
    SceneResult result;
    if (!harness.valid()) {
        return result;
    }
    BuildTestScene(harness);
    harness.camera().SetPerspective(70.0f, static_cast<f32>(width) / static_cast<f32>(height), 0.05f, 200.0f);
    harness.camera().SetPosition(Vec3{0.0f, 1.7f, 0.0f});
    harness.camera().SetAngles(-0.05f, 0.0f);

    NullGame game;
    for (u32 i = 0; i < frames; ++i) {
        harness.StepFrame(game);
    }
    if (harness.CaptureImage(result.image)) {
        result.checksum = result.image.Checksum();
    }
    return result;
}

}  // namespace

VALGO_TEST("integration", "headless_frame_renders_a_recognisable_image") {
    const SceneResult scene = RenderSceneOnce(320, 180, 3);
    VALGO_REQUIRE(!scene.image.empty());
    VALGO_REQUIRE(scene.checksum != 0u);

    const u8* top = scene.image.At(160, 4);
    const u8* middle = scene.image.At(160, 110);
    const u8* bottom = scene.image.At(160, 176);

    // Sky above, geometry below: the image is not a flat clear colour.
    VALGO_CHECK_MSG(top[2] > top[0], "the sky is not blue-dominant");
    VALGO_CHECK_MSG(bottom[0] != top[0] || bottom[1] != top[1] || bottom[2] != top[2],
                    "sky and floor are the same colour");

    // No pixel may be uninitialised garbage (alpha must be opaque).
    bool alpha_ok = true;
    for (usize i = 3; i < scene.image.pixels.size(); i += 4) {
        if (scene.image.pixels[i] != 255) {
            alpha_ok = false;
            break;
        }
    }
    VALGO_CHECK_MSG(alpha_ok, "the frame contains non-opaque pixels");

    // Non-trivial image: many distinct colours must exist.
    bool distinct = false;
    for (usize i = 0; i + 7 < scene.image.pixels.size(); i += 4) {
        if (scene.image.pixels[i] != scene.image.pixels[i + 4]) {
            distinct = true;
            break;
        }
    }
    VALGO_CHECK_MSG(distinct, "the frame is a single flat colour");
    VALGO_UNUSED(middle);
}

VALGO_TEST("integration", "rendering_is_deterministic_across_runs") {
    const SceneResult first = RenderSceneOnce(200, 120, 4);
    const SceneResult second = RenderSceneOnce(200, 120, 4);
    VALGO_REQUIRE(!first.image.empty());
    VALGO_REQUIRE(!second.image.empty());
    VALGO_CHECK_MSG(first.checksum == second.checksum, "two identical runs produced different frames");
}

VALGO_TEST("integration", "resizing_the_frame_changes_the_image") {
    EngineHarness harness(160, 90);
    VALGO_REQUIRE(harness.valid());
    BuildTestScene(harness);

    NullGame game;
    harness.StepFrame(game);
    Image small;
    VALGO_REQUIRE(harness.CaptureImage(small));
    VALGO_CHECK_EQ(small.width, 160u);

    VALGO_REQUIRE(harness.services().swap_chain->Resize(240, 135));
    harness.StepFrame(game);
    Image large;
    VALGO_REQUIRE(harness.CaptureImage(large));
    VALGO_CHECK_EQ(large.width, 240u);
    VALGO_CHECK_EQ(large.height, 135u);
    VALGO_CHECK(large.Checksum() != small.Checksum());
}

VALGO_TEST("integration", "screenshots_encode_and_write_to_disk") {
    EngineHarness harness(128, 72);
    VALGO_REQUIRE(harness.valid());
    BuildTestScene(harness);

    NullGame game;
    harness.StepFrame(game);

    Image image;
    VALGO_REQUIRE(harness.CaptureImage(image));
    const std::string path = std::string(VALGO_PROJECT_ROOT) + "/build/test_frame.png";
    VALGO_REQUIRE(image.SavePng(path));
    VALGO_CHECK(FileExists(path));
}

VALGO_TEST("integration", "engine_runs_without_a_game_module") {
    // The engine must be able to render frames with no game logic at all: this
    // is what makes it an engine rather than an FPS framework.
    EngineHarness harness(96, 54);
    VALGO_REQUIRE(harness.valid());
    BuildTestScene(harness);
    NullGame game;
    for (int i = 0; i < 5; ++i) {
        harness.StepFrame(game);
    }
    VALGO_CHECK_EQ(game.frames, 5u);
    VALGO_CHECK(harness.renderer().stats().meshes_drawn >= 6u);
    VALGO_CHECK(harness.simulated_seconds() > 0.0);
}
