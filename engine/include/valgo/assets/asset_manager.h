// =============================================================================
// Project Valgo — asset manager.
//
// Owns GPU meshes and textures and maps human readable names onto them. Games
// request assets by name ("primitive.box", "meshes/crate.obj") and never touch
// rhi::Buffer directly. Mesh pointers are stable for the manager's lifetime.
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "valgo/assets/mesh_library.h"
#include "valgo/render/mesh.h"
#include "valgo/rhi/rhi.h"

namespace valgo::assets {

class AssetManager {
public:
    explicit AssetManager(rhi::Device& device);
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /// Uploads CPU mesh data and registers it under `name`. Re-using a name
    /// replaces the previous mesh.
    const render::MeshGpuData* CreateMesh(std::string_view name, const MeshData& data);

    const render::MeshGpuData* FindMesh(std::string_view name) const;
    bool HasMesh(std::string_view name) const;

    /// Loads an OBJ file relative to the asset root (or an absolute path).
    const render::MeshGpuData* LoadObjMesh(std::string_view name, std::string_view relative_path);

    /// Returns a cached procedural primitive. Supported names:
    /// "box", "plane", "sphere", "cylinder", "grid".
    const render::MeshGpuData* GetPrimitive(std::string_view name);

    /// Creates (and caches) a 1x1 solid colour texture.
    const rhi::Texture* GetSolidTexture(std::string_view name, const Vec4& color);
    const rhi::Texture* GetWhiteTexture();

    void SetAssetRoot(std::string root);
    const std::string& asset_root() const { return asset_root_; }
    std::string ResolvePath(std::string_view relative_path) const;

    usize mesh_count() const { return meshes_.size(); }
    usize texture_count() const { return textures_.size(); }

    rhi::Device& device() { return device_; }

private:
    struct NamedTexture {
        std::string name;
        std::unique_ptr<rhi::Texture> texture;
    };

    rhi::Device& device_;
    std::string asset_root_;
    std::vector<std::unique_ptr<render::MeshGpuData>> meshes_;
    std::unordered_map<std::string, const render::MeshGpuData*> meshes_by_name_;
    std::vector<NamedTexture> textures_;
    std::unordered_map<std::string, const rhi::Texture*> textures_by_name_;
};

}  // namespace valgo::assets
