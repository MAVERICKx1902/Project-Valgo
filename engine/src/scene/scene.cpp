#include "valgo/scene/scene.h"

namespace valgo::scene {

const std::vector<Entity> Scene::kNoChildren{};

Entity Scene::CreateEntity(std::string_view name) {
    u32 index = kInvalidIndex;
    if (!free_slots_.empty()) {
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        index = static_cast<u32>(slots_.size());
        slots_.push_back({});
    }

    EntitySlot& slot = slots_[index];
    slot.alive = true;
    ++alive_count_;

    const Entity entity{index, slot.generation};

    // Every entity has a transform and a name so components can assume they
    // exist. This costs little and removes a large class of null checks.
    TransformComponent transform;
    transform.local = Transform{};
    transform.world = Transform{};
    GetStorage<TransformComponent>().Add(entity, transform);

    NameComponent name_component;
    name_component.name.assign(name);
    GetStorage<NameComponent>().Add(entity, std::move(name_component));

    return entity;
}

void Scene::DestroyEntity(Entity entity) {
    if (!IsAlive(entity)) {
        return;
    }

    // Children are re-parented to the destroyed entity's parent, never orphaned
    // into an inconsistent world transform.
    const Entity parent = ParentOf(entity);
    const auto children_it = children_.find(entity.index);
    if (children_it != children_.end()) {
        const std::vector<Entity> children = children_it->second;
        for (const Entity& child : children) {
            SetParent(child, parent);
        }
        children_.erase(entity.index);
    }
    if (parent.IsValid()) {
        auto parent_children = children_.find(parent.index);
        if (parent_children != children_.end()) {
            auto& list = parent_children->second;
            list.erase(std::remove(list.begin(), list.end(), entity), list.end());
        }
    }

    DestroyComponents(entity);

    EntitySlot& slot = slots_[entity.index];
    slot.alive = false;
    slot.generation = slot.generation + 1u;
    free_slots_.push_back(entity.index);
    --alive_count_;
}

void Scene::DestroyComponents(Entity entity) {
    // Every storage is type erased, so removal needs no per-type list.
    for (auto& [key, storage] : storages_) {
        (void)key;
        storage->Remove(entity);
    }
}

bool Scene::IsAlive(Entity entity) const {
    if (!entity.IsValid() || entity.index >= slots_.size()) {
        return false;
    }
    const EntitySlot& slot = slots_[entity.index];
    return slot.alive && slot.generation == entity.generation;
}

std::vector<Entity> Scene::Entities() const {
    std::vector<Entity> entities;
    entities.reserve(alive_count_);
    for (u32 index = 0; index < slots_.size(); ++index) {
        if (slots_[index].alive) {
            entities.push_back(Entity{index, slots_[index].generation});
        }
    }
    return entities;
}

Entity Scene::FindByName(std::string_view name) const {
    const auto it = storages_.find(std::type_index(typeid(NameComponent)));
    if (it == storages_.end()) {
        return {};
    }
    const auto& storage = *static_cast<const ComponentStorage<NameComponent>*>(it->second.get());
    const auto& components = storage.components();
    const std::span<const Entity> entities = storage.entities();
    for (usize i = 0; i < components.size(); ++i) {
        if (components[i].name == name) {
            return entities[i];
        }
    }
    return {};
}

void Scene::SetParent(Entity child, Entity parent) {
    if (!IsAlive(child) || (parent.IsValid() && !IsAlive(parent)) || child == parent) {
        return;
    }

    TransformComponent* transform = GetComponent<TransformComponent>(child);
    if (transform == nullptr) {
        return;
    }

    // Reject cycles: walk up from the prospective parent.
    for (Entity walk = parent; walk.IsValid();) {
        if (walk == child) {
            return;
        }
        const TransformComponent* walk_transform = GetComponent<TransformComponent>(walk);
        if (walk_transform == nullptr) {
            break;
        }
        walk = walk_transform->parent;
    }

    const Entity previous = transform->parent;
    if (previous.IsValid()) {
        auto list_it = children_.find(previous.index);
        if (list_it != children_.end()) {
            auto& list = list_it->second;
            list.erase(std::remove(list.begin(), list.end(), child), list.end());
        }
    }

    transform->parent = parent;
    if (parent.IsValid()) {
        children_[parent.index].push_back(child);
    }
}

Entity Scene::ParentOf(Entity entity) const {
    const TransformComponent* transform = GetComponent<TransformComponent>(entity);
    return transform != nullptr ? transform->parent : Entity{};
}

std::span<const Entity> Scene::ChildrenOf(Entity entity) const {
    const auto it = children_.find(entity.index);
    if (it == children_.end()) {
        return {};
    }
    return std::span<const Entity>(it->second.data(), it->second.size());
}

void Scene::UpdateWorldTransforms() {
    // Resolve roots first, then recurse. Iterative depth is fine for the scene
    // sizes this milestone targets; a topological order cache comes with the
    // editor milestone.
    auto compute = [this](Entity entity, const Transform& parent_world, auto&& self) -> void {
        TransformComponent* transform = GetComponent<TransformComponent>(entity);
        if (transform == nullptr) {
            return;
        }
        const Mat4 parent_matrix = parent_world.ToMatrix();
        transform->world = transform->local;
        transform->world.position = parent_matrix.TransformPoint(transform->local.position);
        transform->world.rotation = (parent_world.rotation * transform->local.rotation).Normalized();
        transform->world.scale = parent_world.scale * transform->local.scale;

        const auto children_it = children_.find(entity.index);
        if (children_it == children_.end()) {
            return;
        }
        for (const Entity& child : children_it->second) {
            self(child, transform->world, self);
        }
    };

    for (u32 index = 0; index < slots_.size(); ++index) {
        if (!slots_[index].alive) {
            continue;
        }
        const Entity entity{index, slots_[index].generation};
        const TransformComponent* transform = GetComponent<TransformComponent>(entity);
        if (transform != nullptr && transform->parent.IsValid()) {
            continue;  // handled when its root is visited
        }
        compute(entity, Transform{}, compute);
    }
}

void Scene::Clear() {
    for (auto& [key, storage] : storages_) {
        (void)key;
        storage->Clear();
    }
    slots_.clear();
    free_slots_.clear();
    children_.clear();
    alive_count_ = 0;
}

}  // namespace valgo::scene
