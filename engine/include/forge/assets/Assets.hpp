// engine/include/forge/assets/Assets.hpp
//
// The border crossing (ADR-016): outside live tool formats (OBJ, glTF, PNG),
// inside lives exactly what the engine wants — Vertex arrays and RGBA8
// texels. Importers produce plain CPU data; the Renderer consumes plain CPU
// data; neither ever sees the other's third-party types. That seam is also
// what makes hot reload cheap: reload the file, walk the same upload path.
//
// Assimp (OBJ), cgltf (glTF/GLB) and stb_image are IMPLEMENTATION DETAILS
// confined to src/assets/, one TU each. Paths are relative to the working
// directory until P10 adds a proper asset root; the sandbox and CI both run
// from the repo root.

#pragma once

#include "forge/anim/Skeleton.hpp"
#include "forge/renderer/Renderer.hpp" // forge::Vertex, forge::SkinVertex

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace forge::assets {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Imports the FIRST mesh in an OBJ file (the P5 path; the torus hot-reload
// demo still uses it). Triangulated; smooth normals generated when missing;
// UVs flipped to image convention (v = 0 at the top, matching our textures).
// Throws std::runtime_error with the importer's reason on failure.
[[nodiscard]] MeshData loadMesh(const std::string& path);

struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba; // 8-bit RGBA, tightly packed, row 0 = top
};

// PNG/JPEG via stb_image, always expanded to RGBA8. Throws on failure.
[[nodiscard]] ImageData loadImage(const std::string& path);
// Same, from an in-memory file (images embedded in a GLB). `name` is only
// used in log/error text. Throws on failure.
[[nodiscard]] ImageData loadImageFromMemory(const uint8_t* bytes, size_t size,
                                            const std::string& name);

// ---- Whole-file model import (ADR-030) ------------------------------------

// Per-model corrections, applied at import (assets/models/import.ini in the
// sandbox). Packs are often authored in centimetres or Z-up: fix it once
// here instead of in every spawn call.
struct ImportOptions {
    float scale = 1.0f;          // uniform; 0.01 turns a centimetre pack into metres
    glm::vec3 rotationDeg{0.0f}; // applied after scale, X then Y then Z (e.g. -90 0 0: Z-up)
    std::vector<std::string> hide; // node names whose meshes are skipped (accessory slots)
};

struct MaterialData {
    std::string name;
    glm::vec4 baseColor{1.0f}; // linear rgba multiplier (glTF baseColorFactor / MTL Kd)
    int32_t image = -1;        // index into ModelData::images; -1 = untextured
    // false = the file had no material of its own (an OBJ without MTL, a
    // glTF primitive without one): the spawning entity's texture shows, as
    // single-mesh models always behaved.
    bool authored = true;
};

struct SubmeshData {
    std::string name; // source node name (for logs)
    // Static part: node transform + import correction baked in. Skinned
    // part (skin non-empty): BIND-space vertices; the skeleton places them.
    MeshData mesh;
    std::vector<SkinVertex> skin; // empty = static; else one per vertex
    uint32_t material = 0;
};

struct ModelData {
    std::vector<SubmeshData> submeshes;
    std::vector<MaterialData> materials; // never empty when submeshes isn't
    std::vector<ImageData> images;
    // Parallel to images: the normalized file path an image came from, or ""
    // when it was embedded. Lets uploads share one GPU texture between the
    // many files of a pack that reference the same atlas (Kenney colormaps).
    std::vector<std::string> imageSources;
    glm::vec3 boundsMin{0.0f}; // after the import correction (metres, Y up);
    glm::vec3 boundsMax{0.0f}; // skinned models: the rest pose
    // Animated files (any skin or animation): every part is skinned against
    // this skeleton, and clips holds every animation in file order.
    std::shared_ptr<const anim::Skeleton> skeleton;
    std::vector<anim::SkeletalClip> clips;
};

// Every mesh in the file's node hierarchy, each with its material. .gltf /
// .glb go through cgltf (incl. skins + animation clips), everything else
// (OBJ + MTL) through Assimp. Same conventions as loadMesh: CCW front
// faces, v = 0 at the top. Throws on a
// file that cannot be read; an unreadable TEXTURE only logs and leaves that
// material untextured.
[[nodiscard]] ModelData loadModel(const std::string& path, const ImportOptions& options = {});

} // namespace forge::assets
