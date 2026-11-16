#include "test_map.h"

#include <string>

namespace valgo_fps {
namespace {

using valgo::Aabb;
using valgo::Vec3;
using valgo::assets::AssetManager;
using valgo::physics::kLayerProp;
using valgo::physics::kLayerWorld;
using valgo::physics::PhysicsWorld;
using valgo::render::Material;
using valgo::render::MeshGpuData;
using valgo::scene::Entity;
using valgo::scene::MeshRendererComponent;
using valgo::scene::Scene;
using valgo::scene::TransformComponent;

// Map palette: dusty industrial training yard.
constexpr Vec3 kFloorColor{0.40f, 0.385f, 0.355f};
constexpr Vec3 kWallColor{0.36f, 0.38f, 0.42f};
constexpr Vec3 kCrateColor{0.50f, 0.40f, 0.24f};
constexpr Vec3 kPlatformColor{0.38f, 0.40f, 0.43f};
constexpr Vec3 kLowWallColor{0.47f, 0.48f, 0.49f};
constexpr Vec3 kPillarColor{0.34f, 0.35f, 0.37f};
constexpr Vec3 kBarrelColor{0.42f, 0.27f, 0.18f};
constexpr Vec3 kStripeColor{0.55f, 0.42f, 0.12f};

struct MapBuilder {
    Scene& scene;
    PhysicsWorld& physics;
    AssetManager& assets;
    const MeshGpuData* box_mesh = nullptr;
    const MeshGpuData* cylinder_mesh = nullptr;

    Entity AddSolid(const Vec3& center, const Vec3& half_extents, const Vec3& color, u32 layer = kLayerWorld,
                    bool use_cylinder = false) {
        Entity entity = scene.CreateEntity("solid");
        TransformComponent* transform = scene.GetComponent<TransformComponent>(entity);
        transform->local.position = center;
        transform->local.scale = use_cylinder ? Vec3{half_extents.x * 2.0f, half_extents.y * 2.0f,
                                                     half_extents.x * 2.0f}
                                              : half_extents * 2.0f;

        MeshRendererComponent renderer;
        renderer.mesh = use_cylinder ? cylinder_mesh : box_mesh;
        renderer.material = Material::MakeSolid(color);
        scene.AddComponent<MeshRendererComponent>(entity, renderer);

        const Aabb bounds = Aabb::FromCenterExtents(center, half_extents);
        if (use_cylinder) {
            // Approximate cylindrical shapes with their bounding box for now; the
            // collision abstraction grows with the physics milestone.
            physics.AddStaticBox(bounds, entity.index, layer);
        } else {
            physics.AddStaticBox(bounds, entity.index, layer);
        }
        return entity;
    }

    Entity AddDecoration(const Vec3& center, const Vec3& scale, const Vec3& color, bool unlit = false) {
        Entity entity = scene.CreateEntity("decal");
        TransformComponent* transform = scene.GetComponent<TransformComponent>(entity);
        transform->local.position = center;
        transform->local.scale = scale;

        MeshRendererComponent renderer;
        renderer.mesh = box_mesh;
        renderer.material = unlit ? Material::MakeUnlit(color, 0.35f) : Material::MakeSolid(color);
        scene.AddComponent<MeshRendererComponent>(entity, renderer);
        return entity;
    }
};

}  // namespace

MapBuildResult BuildTrainingMap(Scene& scene, PhysicsWorld& physics, AssetManager& assets,
                                const MeshGpuData* crate_mesh) {
    MapBuildResult result;

    MapBuilder builder{scene, physics, assets};
    builder.box_mesh = assets.GetPrimitive("box");
    builder.cylinder_mesh = assets.GetPrimitive("cylinder");
    const MeshGpuData* floor_mesh = assets.GetPrimitive("grid");

    // --- Ground -----------------------------------------------------------
    Entity floor_entity = scene.CreateEntity("floor");
    {
        TransformComponent* transform = scene.GetComponent<TransformComponent>(floor_entity);
        transform->local.position = Vec3{0.0f, 0.0f, 0.0f};
        transform->local.scale = Vec3{1.6f, 1.0f, 1.6f};  // 32x32 grid -> 64x64 m

        MeshRendererComponent renderer;
        renderer.mesh = floor_mesh;
        renderer.material = Material::MakeSolid(kFloorColor);
        scene.AddComponent<MeshRendererComponent>(floor_entity, renderer);
        physics.AddStaticBox(Aabb{Vec3{-32.0f, -1.0f, -32.0f}, Vec3{32.0f, 0.0f, 32.0f}},
                             floor_entity.index, kLayerWorld);
    }

    // --- Perimeter walls --------------------------------------------------
    constexpr f32 kHalfX = 18.0f;
    constexpr f32 kHalfZ = 24.0f;
    constexpr f32 kWallHeight = 6.0f;
    constexpr f32 kWallHalfThickness = 0.35f;
    builder.AddSolid({0.0f, kWallHeight * 0.5f, -kHalfZ}, {kHalfX + 1.0f, kWallHeight * 0.5f, kWallHalfThickness},
                     kWallColor);
    builder.AddSolid({0.0f, kWallHeight * 0.5f, kHalfZ}, {kHalfX + 1.0f, kWallHeight * 0.5f, kWallHalfThickness},
                     kWallColor);
    builder.AddSolid({-kHalfX, kWallHeight * 0.5f, 0.0f}, {kWallHalfThickness, kWallHeight * 0.5f, kHalfZ},
                     kWallColor);
    builder.AddSolid({kHalfX, kWallHeight * 0.5f, 0.0f}, {kWallHalfThickness, kWallHeight * 0.5f, kHalfZ},
                     kWallColor);

    // --- Firing line markings (visual only) -------------------------------
    for (int lane = -2; lane <= 2; ++lane) {
        builder.AddDecoration({static_cast<f32>(lane) * 3.0f, 0.012f, -2.0f},
                              {0.12f, 0.006f, 12.0f}, kStripeColor, true);
    }
    builder.AddDecoration({0.0f, 0.012f, 5.2f}, {14.0f, 0.006f, 0.14f}, kStripeColor, true);

    // --- Cover crates and stacked barrels ---------------------------------
    struct CrateSpec {
        Vec3 center;
        Vec3 half_extents;
    };
    const CrateSpec crates[] = {
        {{-4.5f, 0.5f, 1.0f}, {0.6f, 0.5f, 0.6f}},
        {{-4.6f, 1.5f, 1.1f}, {0.45f, 0.5f, 0.45f}},
        {{4.2f, 0.5f, 0.2f}, {0.6f, 0.5f, 0.6f}},
        {{5.6f, 0.5f, -1.4f}, {0.6f, 0.5f, 0.6f}},
        {{5.5f, 1.5f, -1.4f}, {0.6f, 0.5f, 0.6f}},
        {{-7.5f, 0.5f, -6.0f}, {0.7f, 0.5f, 0.7f}},
        {{-1.5f, 0.5f, -8.5f}, {0.6f, 0.5f, 0.6f}},
        {{2.8f, 0.5f, -7.0f}, {0.6f, 0.5f, 0.6f}},
        {{8.5f, 0.5f, -10.0f}, {0.75f, 0.5f, 0.75f}},
        {{-11.0f, 0.5f, -12.0f}, {0.8f, 0.5f, 0.8f}},
    };
    for (const CrateSpec& crate : crates) {
        if (crate_mesh != nullptr) {
            Entity entity = scene.CreateEntity("crate");
            TransformComponent* transform = scene.GetComponent<TransformComponent>(entity);
            transform->local.position = crate.center;
            transform->local.scale = crate.half_extents * 2.0f;
            MeshRendererComponent renderer;
            renderer.mesh = crate_mesh;
            renderer.material = Material::MakeSolid(kCrateColor);
            scene.AddComponent<MeshRendererComponent>(entity, renderer);
            physics.AddStaticBox(Aabb::FromCenterExtents(crate.center, crate.half_extents), entity.index,
                                 kLayerProp);
        } else {
            builder.AddSolid(crate.center, crate.half_extents, kCrateColor, kLayerProp);
        }
    }

    // Barrels (cylinder visuals, box colliders).
    const Vec3 barrel_positions[] = {{6.8f, 0.6f, 3.4f}, {7.4f, 0.6f, 4.1f}, {-8.6f, 0.6f, -1.2f},
                                     {10.5f, 0.6f, -4.0f}, {-13.5f, 0.6f, -8.0f}};
    for (const Vec3& position : barrel_positions) {
        builder.AddSolid(position, {0.42f, 0.6f, 0.42f}, kBarrelColor, kLayerProp, true);
    }

    // --- Crouch wall: shoot over it only while standing, cross it by crouching
    builder.AddSolid({0.0f, 0.35f, 2.6f}, {3.4f, 0.35f, 0.28f}, kLowWallColor);

    // --- Step-up platform (tests the engine's step assist) ----------------
    // Each step rises 0.28 m; the engine step height is 0.38 m.
    constexpr f32 kStepRise = 0.28f;
    for (u32 i = 0; i < 4; ++i) {
        const f32 height = kStepRise * static_cast<f32>(i + 1);
        builder.AddSolid({-13.0f, height * 0.5f, -3.0f + static_cast<f32>(i) * 1.1f},
                         {1.6f, height * 0.5f, 0.55f}, kPlatformColor);
    }
    // Elevated platform at the top of the steps.
    builder.AddSolid({-13.0f, 0.56f, -7.4f}, {1.6f, 0.56f, 1.6f}, kPlatformColor);
    builder.AddSolid({-13.0f, 1.28f, -10.6f}, {1.6f, 0.72f, 1.6f}, kPlatformColor);
    // Railing decoration on the platform.
    for (f32 x = -14.4f; x <= -11.6f; x += 0.7f) {
        builder.AddDecoration({x, 1.05f, -12.05f}, {0.05f, 0.5f, 0.05f}, kLowWallColor);
    }

    // --- Jumpable gap: low hurdle ----------------------------------------
    builder.AddSolid({7.5f, 0.3f, -3.5f}, {1.2f, 0.3f, 0.3f}, kLowWallColor);

    // --- Pillars ----------------------------------------------------------
    const Vec3 pillar_positions[] = {{-9.0f, 2.5f, 6.0f}, {9.0f, 2.5f, 6.0f}, {0.0f, 2.5f, -14.0f}};
    for (const Vec3& position : pillar_positions) {
        builder.AddSolid(position, {0.5f, 2.5f, 0.5f}, kPillarColor);
    }

    // --- Range backstop wall ---------------------------------------------
    builder.AddSolid({0.0f, 1.6f, -19.0f}, {12.0f, 1.6f, 0.4f}, kWallColor);

    // --- Emissive marker strips on the backstop --------------------------
    for (int i = -2; i <= 2; ++i) {
        builder.AddDecoration({static_cast<f32>(i) * 3.0f, 3.35f, -18.55f}, {0.9f, 0.05f, 0.05f},
                              {0.9f, 0.62f, 0.2f}, true);
    }

    // --- Training dummies -------------------------------------------------
    result.target_positions = {
        {0.0f, 0.0f, -12.0f},   {-4.5f, 0.0f, -13.5f}, {4.5f, 0.0f, -14.5f},
        {-8.5f, 0.0f, -16.0f},  {8.5f, 0.0f, -16.5f},  {1.5f, 0.0f, -17.5f},
    };

    result.player_spawn = Vec3{0.0f, 0.0f, 8.0f};
    result.player_yaw = 0.0f;
    return result;
}

}  // namespace valgo_fps
