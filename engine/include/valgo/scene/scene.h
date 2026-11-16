// =============================================================================
// Project Valgo — scene graph and entity/component storage.
//
// A deliberately small entity-component system: entity handles carry a
// generation counter (destroyed entities can never be reused by accident),
// components live in dense arrays per type, and transforms form a hierarchy.
//
// The scene knows nothing about gameplay: it stores transforms, meshes,
// materials and lights, and the renderer interprets them.
// =============================================================================
#pragma once

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "valgo/core/assert.h"
#include "valgo/core/types.h"
#include "valgo/math/math.h"
#include "valgo/render/mesh.h"

namespace valgo::scene {

/// Lightweight entity handle (index + generation).
struct Entity {
    u32 index = kInvalidIndex;
    u32 generation = 0;

    bool IsValid() const { return index != kInvalidIndex; }

    friend bool operator==(const Entity& a, const Entity& b) {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const Entity& a, const Entity& b) { return !(a == b); }
};

struct NameComponent {
    std::string name;
};

struct TransformComponent {
    Transform local{};
    Transform world{};  ///< Rebuilt by Scene::UpdateWorldTransforms().
    Entity parent{};
};

struct MeshRendererComponent {
    const render::MeshGpuData* mesh = nullptr;
    render::Material material{};
    bool visible = true;
    bool cast_shadow = true;
};

struct DirectionalLightComponent {
    Vec3 direction{-0.45f, -1.0f, -0.35f};  ///< Direction the light travels.
    Vec3 color{1.0f, 0.96f, 0.9f};
    f32 intensity = 1.0f;
};

/// Type-erased component storage interface (needed by the storage map).
class IComponentStorage {
public:
    virtual ~IComponentStorage() = default;
    virtual usize size() const = 0;
    virtual void Remove(Entity entity) = 0;
    virtual bool Has(Entity entity) const = 0;
    virtual void Clear() = 0;
};

/// Dense component storage for a single component type.
template <typename T>
class ComponentStorage final : public IComponentStorage {
public:
    T& Add(Entity entity, T value = {}) {
        const auto existing = lookup_.find(entity.index);
        if (existing != lookup_.end()) {
            dense_[existing->second] = std::move(value);
            return dense_[existing->second];
        }
        lookup_.emplace(entity.index, dense_.size());
        entities_.push_back(entity);
        dense_.push_back(std::move(value));
        return dense_.back();
    }

    T* Get(Entity entity) {
        const auto it = lookup_.find(entity.index);
        if (it == lookup_.end() || entities_[it->second].generation != entity.generation) {
            return nullptr;
        }
        return &dense_[it->second];
    }

    const T* Get(Entity entity) const {
        const auto it = lookup_.find(entity.index);
        if (it == lookup_.end() || entities_[it->second].generation != entity.generation) {
            return nullptr;
        }
        return &dense_[it->second];
    }

    bool Has(Entity entity) const override { return Get(entity) != nullptr; }

    void Remove(Entity entity) override {
        const auto it = lookup_.find(entity.index);
        if (it == lookup_.end() || entities_[it->second].generation != entity.generation) {
            return;
        }
        const usize index = it->second;
        const usize last = dense_.size() - 1;
        if (index != last) {
            dense_[index] = std::move(dense_[last]);
            entities_[index] = entities_[last];
            lookup_[entities_[index].index] = index;
        }
        dense_.pop_back();
        entities_.pop_back();
        lookup_.erase(entity.index);
    }

    usize size() const override { return dense_.size(); }
    bool empty() const { return dense_.empty(); }

    void Clear() override {
        dense_.clear();
        entities_.clear();
        lookup_.clear();
    }

    std::vector<T>& components() { return dense_; }
    const std::vector<T>& components() const { return dense_; }
    std::span<const Entity> entities() const { return entities_; }

private:
    std::vector<T> dense_;
    std::vector<Entity> entities_;
    std::unordered_map<u32, usize> lookup_;
};

class Scene {
public:
    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // --- Entities -----------------------------------------------------------
    Entity CreateEntity(std::string_view name = {});
    void DestroyEntity(Entity entity);
    bool IsAlive(Entity entity) const;
    usize entity_count() const { return alive_count_; }
    std::vector<Entity> Entities() const;

    Entity FindByName(std::string_view name) const;

    // --- Components ---------------------------------------------------------
    template <typename T>
    T& AddComponent(Entity entity, T value = {}) {
        VALGO_ASSERT(IsAlive(entity), "AddComponent on a dead entity");
        return GetStorage<T>().Add(entity, std::move(value));
    }

    template <typename T>
    T* GetComponent(Entity entity) {
        if (!IsAlive(entity)) {
            return nullptr;
        }
        return GetStorage<T>().Get(entity);
    }

    template <typename T>
    const T* GetComponent(Entity entity) const {
        if (!IsAlive(entity)) {
            return nullptr;
        }
        const auto it = storages_.find(std::type_index(typeid(T)));
        if (it == storages_.end()) {
            return nullptr;
        }
        return static_cast<const ComponentStorage<T>*>(it->second.get())->Get(entity);
    }

    template <typename T>
    bool HasComponent(Entity entity) const {
        const T* component = GetComponent<T>(entity);
        return component != nullptr;
    }

    template <typename T>
    void RemoveComponent(Entity entity) {
        const auto it = storages_.find(std::type_index(typeid(T)));
        if (it != storages_.end()) {
            it->second->Remove(entity);
        }
    }

    template <typename T>
    ComponentStorage<T>& GetStorage() {
        const std::type_index key(typeid(T));
        auto it = storages_.find(key);
        if (it == storages_.end()) {
            auto storage = std::make_unique<ComponentStorage<T>>();
            ComponentStorage<T>* raw = storage.get();
            storages_.emplace(key, std::move(storage));
            return *raw;
        }
        return *static_cast<ComponentStorage<T>*>(it->second.get());
    }

    template <typename T>
    const ComponentStorage<T>& GetStorage() const {
        const auto it = storages_.find(std::type_index(typeid(T)));
        VALGO_ASSERT(it != storages_.end(), "component storage does not exist");
        return *static_cast<const ComponentStorage<T>*>(it->second.get());
    }

    template <typename T>
    bool HasStorage() const {
        return storages_.find(std::type_index(typeid(T))) != storages_.end();
    }

    /// Iterates every entity that owns component T. Do not create or destroy
    /// entities while iterating.
    template <typename T, typename Fn>
    void ForEach(Fn&& function) {
        const auto it = storages_.find(std::type_index(typeid(T)));
        if (it == storages_.end()) {
            return;
        }
        auto& storage = *static_cast<ComponentStorage<T>*>(it->second.get());
        auto& components = storage.components();
        const std::span<const Entity> entities = storage.entities();
        for (usize i = 0; i < components.size(); ++i) {
            function(entities[i], components[i]);
        }
    }

    template <typename T, typename Fn>
    void ForEach(Fn&& function) const {
        const auto it = storages_.find(std::type_index(typeid(T)));
        if (it == storages_.end()) {
            return;
        }
        const auto& storage = *static_cast<const ComponentStorage<T>*>(it->second.get());
        const auto& components = storage.components();
        const std::span<const Entity> entities = storage.entities();
        for (usize i = 0; i < components.size(); ++i) {
            function(entities[i], components[i]);
        }
    }

    // --- Hierarchy ----------------------------------------------------------
    void SetParent(Entity child, Entity parent);
    Entity ParentOf(Entity entity) const;
    std::span<const Entity> ChildrenOf(Entity entity) const;

    /// Recomputes world transforms for the whole hierarchy.
    void UpdateWorldTransforms();

    void Clear();

private:
    struct EntitySlot {
        u32 generation = 0;
        bool alive = false;
    };

    void DestroyComponents(Entity entity);

    std::vector<EntitySlot> slots_;
    std::vector<u32> free_slots_;
    usize alive_count_ = 0;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> storages_;

    // Cached child lists per entity index (index == child list owner).
    std::unordered_map<u32, std::vector<Entity>> children_;
    static const std::vector<Entity> kNoChildren;
};

}  // namespace valgo::scene
