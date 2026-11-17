// engine/src/assets/ImportInternal.hpp — shared by the importer TUs only.
//
// Each third-party library keeps its own TU (ADR-016): MeshLoader.cpp is
// Assimp (OBJ), GltfLoader.cpp is cgltf (glTF/GLB), ImageLoader.cpp is stb.
// ModelImport.cpp holds the format-agnostic glue: dispatch, the per-model
// import correction, transform baking, bounds and the load-time log line.
#pragma once

#include "forge/assets/Assets.hpp"

#include <glm/mat4x4.hpp>

#include <string>
#include <string_view>

namespace forge::assets::detail {

// Per-format loaders. Both return submeshes with NODE transforms baked in
// but WITHOUT the import correction — loadModel applies that uniformly.
[[nodiscard]] ModelData loadObjModel(const std::string& path, const ImportOptions& options);
[[nodiscard]] ModelData loadGltfModel(const std::string& path, const ImportOptions& options);

// scale, then rotation X, Y, Z (degrees) — the per-model import correction.
[[nodiscard]] glm::mat4 importCorrection(const ImportOptions& options);
[[nodiscard]] bool isHidden(const ImportOptions& options, std::string_view nodeName);

// Bakes `transform` into positions and normals (inverse-transpose), and
// flips the winding when the transform mirrors, so CCW stays front-facing.
void transformMesh(MeshData& mesh, const glm::mat4& transform);
// Area-weighted smooth normals, for files that carry none.
void generateNormals(MeshData& mesh);

} // namespace forge::assets::detail
