// =============================================================================
// Project Valgo — Wavefront OBJ importer.
//
// Scope: positions, normals, texture coordinates and faces (triangles and
// polygons). Groups/materials are ignored for now and tracked as pipeline work.
// This exists so the asset pipeline can be validated end-to-end with real files
// before the full importer (glTF) lands.
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include "valgo/assets/mesh_library.h"

namespace valgo::assets {

struct ObjLoadResult {
    MeshData mesh;
    bool had_normals = false;
    bool had_uvs = false;
    usize source_face_count = 0;
};

/// Parses OBJ text. Returns std::nullopt on malformed input.
std::optional<ObjLoadResult> ParseObj(const std::string& text, std::string_view source_name);

/// Loads an OBJ file from disk. `source_name` is used for diagnostics.
std::optional<ObjLoadResult> LoadObjFile(const std::string& path);

}  // namespace valgo::assets
