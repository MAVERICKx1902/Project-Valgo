#include "valgo/assets/asset_manager.h"

#include <array>

#include "valgo/assets/obj_loader.h"
#include "valgo/core/file.h"
#include "valgo/core/log.h"

namespace valgo::assets {
namespace {

constexpr const char* kCategory = "assets";

}  // namespace

AssetManager::AssetManager(rhi::Device& device) : device_(device) {}

AssetManager::~AssetManager() = default;

const render::MeshGpuData* AssetManager::CreateMesh(std::string_view name, const MeshData& data) {
    if (data.empty()) {
        LogError(kCategory, "refusing to create empty mesh '{}'", name);
        return nullptr;
    }

    auto mesh = std::make_unique<render::MeshGpuData>();
    mesh->name.assign(name);
    mesh->index_count = static_cast<u32>(data.indices.size());
    mesh->vertex_count = static_cast<u32>(data.vertices.size());
    mesh->bounds = data.bounds;

    rhi::BufferDesc vertex_desc;
    vertex_desc.usage = rhi::BufferUsage::Vertex;
    vertex_desc.size = data.vertices.size() * sizeof(rhi::Vertex);
    vertex_desc.stride = sizeof(rhi::Vertex);
    vertex_desc.initial_data = data.vertices.data();
    vertex_desc.debug_name = mesh->name;
    mesh->vertex_buffer = device_.CreateBuffer(vertex_desc);

    rhi::BufferDesc index_desc;
    index_desc.usage = rhi::BufferUsage::Index;
    index_desc.size = data.indices.size() * sizeof(u32);
    index_desc.stride = sizeof(u32);
    index_desc.initial_data = data.indices.data();
    index_desc.debug_name = mesh->name;
    mesh->index_buffer = device_.CreateBuffer(index_desc);

    if (mesh->vertex_buffer == nullptr || mesh->index_buffer == nullptr) {
        LogError(kCategory, "failed to upload mesh '{}'", name);
        return nullptr;
    }

    const render::MeshGpuData* raw = mesh.get();
    meshes_.push_back(std::move(mesh));
    meshes_by_name_[std::string(name)] = raw;
    LogDebug(kCategory, "mesh '{}' uploaded ({} vertices, {} indices)", name, raw->vertex_count,
             raw->index_count);
    return raw;
}

const render::MeshGpuData* AssetManager::FindMesh(std::string_view name) const {
    const auto it = meshes_by_name_.find(std::string(name));
    return it != meshes_by_name_.end() ? it->second : nullptr;
}

bool AssetManager::HasMesh(std::string_view name) const { return FindMesh(name) != nullptr; }

const render::MeshGpuData* AssetManager::LoadObjMesh(std::string_view name, std::string_view relative_path) {
    if (const render::MeshGpuData* existing = FindMesh(name)) {
        return existing;
    }
    const std::string path = ResolvePath(relative_path);
    const std::optional<ObjLoadResult> loaded = LoadObjFile(path);
    if (!loaded.has_value()) {
        return nullptr;
    }
    return CreateMesh(name, loaded->mesh);
}

const render::MeshGpuData* AssetManager::GetPrimitive(std::string_view name) {
    if (const render::MeshGpuData* existing = FindMesh(name)) {
        return existing;
    }

    if (name == "box") {
        return CreateMesh(name, MakeBox({0.5f, 0.5f, 0.5f}));
    }
    if (name == "plane") {
        return CreateMesh(name, MakePlane(20.0f, 20.0f));
    }
    if (name == "grid") {
        return CreateMesh(name, MakeSubdividedPlane(20.0f, 20));
    }
    if (name == "sphere") {
        return CreateMesh(name, MakeSphere(1.0f, 16, 32));
    }
    if (name == "cylinder") {
        return CreateMesh(name, MakeCylinder(0.5f, 2.0f, 24));
    }
    if (name == "quad") {
        return CreateMesh(name, MakeQuad(0.5f, 0.5f));
    }

    LogError(kCategory, "unknown primitive '{}'", name);
    return nullptr;
}

const rhi::Texture* AssetManager::GetSolidTexture(std::string_view name, const Vec4& color) {
    const auto existing = textures_by_name_.find(std::string(name));
    if (existing != textures_by_name_.end()) {
        return existing->second;
    }

    const std::array<u8, 4> pixel = {
        static_cast<u8>(Saturate(color.x) * 255.0f + 0.5f),
        static_cast<u8>(Saturate(color.y) * 255.0f + 0.5f),
        static_cast<u8>(Saturate(color.z) * 255.0f + 0.5f),
        static_cast<u8>(Saturate(color.w) * 255.0f + 0.5f),
    };

    rhi::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = rhi::Format::RGBA8_SRGB;
    desc.usage = rhi::TextureUsage::Sampled;
    desc.initial_data = pixel.data();
    desc.row_pitch = 4;
    desc.debug_name = name;

    auto texture = device_.CreateTexture(desc);
    if (texture == nullptr) {
        return nullptr;
    }

    NamedTexture entry;
    entry.name.assign(name);
    entry.texture = std::move(texture);
    const rhi::Texture* raw = entry.texture.get();
    textures_.push_back(std::move(entry));
    textures_by_name_[std::string(name)] = raw;
    return raw;
}

const rhi::Texture* AssetManager::GetWhiteTexture() {
    return GetSolidTexture("white", {1.0f, 1.0f, 1.0f, 1.0f});
}

void AssetManager::SetAssetRoot(std::string root) { asset_root_ = std::move(root); }

std::string AssetManager::ResolvePath(std::string_view relative_path) const {
    if (relative_path.empty()) {
        return std::string();
    }
    if (relative_path[0] == '/' || (relative_path.size() > 1 && relative_path[1] == ':')) {
        return std::string(relative_path);  // already absolute
    }
    return JoinPath(asset_root_, relative_path);
}

}  // namespace valgo::assets
