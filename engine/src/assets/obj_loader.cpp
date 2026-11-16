#include "valgo/assets/obj_loader.h"

#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "valgo/core/file.h"
#include "valgo/core/log.h"

namespace valgo::assets {
namespace {

constexpr const char* kCategory = "assets.obj";

struct ObjIndex {
    i32 position = -1;
    i32 uv = -1;
    i32 normal = -1;

    bool operator==(const ObjIndex& other) const {
        return position == other.position && uv == other.uv && normal == other.normal;
    }
};

struct ObjIndexHash {
    usize operator()(const ObjIndex& index) const {
        usize hash = static_cast<usize>(index.position) * 73856093u;
        hash ^= static_cast<usize>(index.uv + 1) * 19349663u;
        hash ^= static_cast<usize>(index.normal + 1) * 83492791u;
        return hash;
    }
};

/// Resolves 1-based and negative OBJ indices into 0-based indices.
i32 ResolveIndex(i32 obj_index, usize count) {
    if (obj_index > 0) {
        const i32 resolved = obj_index - 1;
        return resolved < static_cast<i32>(count) ? resolved : -1;
    }
    if (obj_index < 0) {
        const i32 resolved = static_cast<i32>(count) + obj_index;
        return resolved >= 0 ? resolved : -1;
    }
    return -1;
}

bool ParseFaceVertex(const std::string& token, ObjIndex& out) {
    out = ObjIndex{};
    std::string_view view(token);
    i32 component = 0;
    std::string current;
    auto flush = [&]() {
        if (current.empty()) {
            ++component;
            return;
        }
        const i32 value = std::atoi(current.c_str());
        if (component == 0) {
            out.position = value;
        } else if (component == 1) {
            out.uv = value;
        } else if (component == 2) {
            out.normal = value;
        }
        ++component;
        current.clear();
    };
    for (char character : view) {
        if (character == '/') {
            flush();
        } else {
            current.push_back(character);
        }
    }
    flush();
    return out.position != 0;
}

}  // namespace

std::optional<ObjLoadResult> ParseObj(const std::string& text, std::string_view source_name) {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;

    ObjLoadResult result;
    std::unordered_map<ObjIndex, u32, ObjIndexHash> vertex_map;

    std::istringstream stream(text);
    std::string line;
    usize line_number = 0;
    bool error = false;

    auto get_vertex = [&](const ObjIndex& index) -> u32 {
        const auto existing = vertex_map.find(index);
        if (existing != vertex_map.end()) {
            return existing->second;
        }

        const i32 position_index = ResolveIndex(index.position, positions.size());
        if (position_index < 0) {
            return kInvalidIndex;
        }
        rhi::Vertex vertex{};
        vertex.position = positions[static_cast<usize>(position_index)];

        if (index.normal != 0) {
            const i32 normal_index = ResolveIndex(index.normal, normals.size());
            if (normal_index >= 0) {
                vertex.normal = normals[static_cast<usize>(normal_index)];
                result.had_normals = true;
            }
        }
        if (index.uv != 0) {
            const i32 uv_index = ResolveIndex(index.uv, uvs.size());
            if (uv_index >= 0) {
                vertex.uv = uvs[static_cast<usize>(uv_index)];
                result.had_uvs = true;
            }
        }

        const u32 new_index = static_cast<u32>(result.mesh.vertices.size());
        result.mesh.vertices.push_back(vertex);
        vertex_map.emplace(index, new_index);
        return new_index;
    };

    while (std::getline(stream, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream line_stream(line);
        std::string keyword;
        line_stream >> keyword;

        if (keyword == "v") {
            Vec3 position;
            if (!(line_stream >> position.x >> position.y >> position.z)) {
                LogError(kCategory, "{}:{}: malformed vertex", source_name, line_number);
                error = true;
                break;
            }
            positions.push_back(position);
        } else if (keyword == "vn") {
            Vec3 normal;
            if (!(line_stream >> normal.x >> normal.y >> normal.z)) {
                LogError(kCategory, "{}:{}: malformed normal", source_name, line_number);
                error = true;
                break;
            }
            normals.push_back(Normalize(normal));
        } else if (keyword == "vt") {
            Vec2 uv;
            if (!(line_stream >> uv.x >> uv.y)) {
                LogError(kCategory, "{}:{}: malformed texture coordinate", source_name, line_number);
                error = true;
                break;
            }
            uvs.push_back(uv);
        } else if (keyword == "f") {
            std::vector<u32> face_vertices;
            std::string token;
            while (line_stream >> token) {
                ObjIndex index;
                if (!ParseFaceVertex(token, index)) {
                    LogError(kCategory, "{}:{}: malformed face vertex '{}'", source_name, line_number, token);
                    error = true;
                    break;
                }
                const u32 vertex_index = get_vertex(index);
                if (vertex_index == kInvalidIndex) {
                    LogError(kCategory, "{}:{}: face references an out-of-range vertex", source_name,
                             line_number);
                    error = true;
                    break;
                }
                face_vertices.push_back(vertex_index);
            }
            if (error) {
                break;
            }
            if (face_vertices.size() < 3) {
                continue;
            }
            ++result.source_face_count;
            // Fan-triangulate polygons.
            for (usize i = 1; i + 1 < face_vertices.size(); ++i) {
                result.mesh.indices.push_back(face_vertices[0]);
                result.mesh.indices.push_back(face_vertices[i]);
                result.mesh.indices.push_back(face_vertices[i + 1]);
            }
        }
    }

    if (error || result.mesh.vertices.empty() || result.mesh.indices.empty()) {
        return std::nullopt;
    }

    if (!result.had_normals) {
        result.mesh.RecomputeSmoothNormals();
    }
    result.mesh.RecomputeBounds();
    return result;
}

std::optional<ObjLoadResult> LoadObjFile(const std::string& path) {
    const std::optional<std::string> text = ReadTextFile(path);
    if (!text.has_value()) {
        LogError(kCategory, "cannot open '{}'", path);
        return std::nullopt;
    }
    return ParseObj(*text, path);
}

}  // namespace valgo::assets
