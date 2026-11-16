// =============================================================================
// Scene: entity lifetime, component storage, hierarchy and traversal.
// =============================================================================
#include "../framework/test_framework.h"

#include "valgo/scene/scene.h"

using namespace valgo;
using valgo::scene::Entity;
using valgo::scene::MeshRendererComponent;
using valgo::scene::NameComponent;
using valgo::scene::TransformComponent;

VALGO_TEST("scene", "entity_creation_and_destruction") {
    scene::Scene scene;
    VALGO_CHECK_EQ(scene.entity_count(), static_cast<usize>(0));

    const Entity first = scene.CreateEntity("first");
    const Entity second = scene.CreateEntity("second");
    VALGO_CHECK_EQ(scene.entity_count(), static_cast<usize>(2));
    VALGO_CHECK(first.IsValid());
    VALGO_CHECK(scene.IsAlive(first));
    VALGO_CHECK(first != second);

    VALGO_REQUIRE(scene.GetComponent<NameComponent>(first) != nullptr);
    VALGO_CHECK_EQ(scene.GetComponent<NameComponent>(first)->name, std::string("first"));
    VALGO_CHECK_EQ(scene.FindByName("second"), second);
    VALGO_CHECK(!scene.FindByName("missing").IsValid());

    scene.DestroyEntity(first);
    VALGO_CHECK_EQ(scene.entity_count(), static_cast<usize>(1));
    VALGO_CHECK(!scene.IsAlive(first));
    VALGO_CHECK(scene.GetComponent<NameComponent>(first) == nullptr);

    // Index reuse must bump the generation so stale handles stay invalid.
    const Entity reused = scene.CreateEntity("reused");
    VALGO_CHECK_EQ(reused.index, first.index);
    VALGO_CHECK_MSG(reused != first, "stale entity handle became valid again");
    VALGO_CHECK(!scene.IsAlive(first));
    VALGO_CHECK(scene.IsAlive(reused));
}

VALGO_TEST("scene", "component_add_get_remove") {
    scene::Scene scene;
    const Entity entity = scene.CreateEntity("prop");
    // Every entity is created with a transform and a name (documented contract).
    VALGO_REQUIRE(scene.GetComponent<TransformComponent>(entity) != nullptr);
    VALGO_CHECK(scene.HasComponent<NameComponent>(entity));
    VALGO_CHECK(!scene.HasComponent<MeshRendererComponent>(entity));

    TransformComponent& transform = *scene.GetComponent<TransformComponent>(entity);
    transform.local.position = Vec3{1.0f, 2.0f, 3.0f};
    VALGO_CHECK(scene.HasComponent<TransformComponent>(entity));
    VALGO_CHECK_NEAR(scene.GetComponent<TransformComponent>(entity)->local.position.y, 2.0f, 1.0e-5f);

    MeshRendererComponent renderer;
    renderer.visible = false;
    scene.AddComponent<MeshRendererComponent>(entity, renderer);
    VALGO_CHECK(!scene.GetComponent<MeshRendererComponent>(entity)->visible);

    usize visited = 0;
    scene.ForEach<TransformComponent>([&](Entity, TransformComponent&) { ++visited; });
    VALGO_CHECK_EQ(visited, static_cast<usize>(1));

    scene.RemoveComponent<MeshRendererComponent>(entity);
    VALGO_CHECK(!scene.HasComponent<MeshRendererComponent>(entity));

    // Destroying an entity removes all of its components.
    scene.DestroyEntity(entity);
    VALGO_CHECK_EQ(scene.GetStorage<TransformComponent>().size(), static_cast<usize>(0));
}

VALGO_TEST("scene", "hierarchy_updates_world_transforms") {
    scene::Scene scene;
    const Entity root = scene.CreateEntity("root");
    const Entity child = scene.CreateEntity("child");
    const Entity grandchild = scene.CreateEntity("grandchild");

    scene.AddComponent<TransformComponent>(root).local.position = Vec3{10.0f, 0.0f, 0.0f};
    TransformComponent& child_transform = scene.AddComponent<TransformComponent>(child);
    child_transform.local.position = Vec3{0.0f, 2.0f, 0.0f};
    child_transform.local.rotation = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, kHalfPi);
    scene.AddComponent<TransformComponent>(grandchild).local.position = Vec3{0.0f, 0.0f, -3.0f};

    scene.SetParent(child, root);
    scene.SetParent(grandchild, child);
    VALGO_CHECK_EQ(scene.ParentOf(child), root);
    VALGO_CHECK_EQ(scene.ChildrenOf(root).size(), static_cast<usize>(1));

    scene.UpdateWorldTransforms();
    VALGO_CHECK_NEAR(scene.GetComponent<TransformComponent>(child)->world.position.y, 2.0f, 1.0e-4f);
    // Child rotated 90 degrees about +Y, so its local -Z maps to -X in world space.
    const Vec3 grandchild_world = scene.GetComponent<TransformComponent>(grandchild)->world.position;
    VALGO_CHECK_NEAR(grandchild_world.x, 7.0f, 1.0e-4f);
    VALGO_CHECK_NEAR(grandchild_world.y, 2.0f, 1.0e-4f);
    VALGO_CHECK_NEAR(grandchild_world.z, 0.0f, 1.0e-4f);

    // Reparenting under the root keeps transforms coherent after an update.
    scene.SetParent(grandchild, root);
    scene.UpdateWorldTransforms();
    const Vec3 reparented = scene.GetComponent<TransformComponent>(grandchild)->world.position;
    VALGO_CHECK_NEAR(reparented.x, 10.0f, 1.0e-4f);
    VALGO_CHECK_NEAR(reparented.z, -3.0f, 1.0e-4f);
}

VALGO_TEST("scene", "destroying_a_parent_keeps_children_alive") {
    scene::Scene scene;
    const Entity parent = scene.CreateEntity("parent");
    const Entity child = scene.CreateEntity("child");
    scene.AddComponent<TransformComponent>(parent);
    scene.AddComponent<TransformComponent>(child);
    scene.SetParent(child, parent);

    scene.DestroyEntity(parent);
    VALGO_CHECK(scene.IsAlive(child));
    VALGO_CHECK(!scene.ParentOf(child).IsValid());
    scene.UpdateWorldTransforms();
    VALGO_CHECK(scene.GetComponent<TransformComponent>(child) != nullptr);
}

VALGO_TEST("scene", "clear_removes_everything") {
    scene::Scene scene;
    for (int i = 0; i < 32; ++i) {
        const Entity entity = scene.CreateEntity("bulk");
        scene.AddComponent<TransformComponent>(entity);
    }
    VALGO_CHECK_EQ(scene.entity_count(), static_cast<usize>(32));
    scene.Clear();
    VALGO_CHECK_EQ(scene.entity_count(), static_cast<usize>(0));
    VALGO_CHECK_EQ(scene.Entities().size(), static_cast<usize>(0));
    VALGO_CHECK_EQ(scene.GetStorage<TransformComponent>().size(), static_cast<usize>(0));

    // The scene must remain usable after a clear.
    const Entity fresh = scene.CreateEntity("fresh");
    VALGO_CHECK(scene.IsAlive(fresh));
}
