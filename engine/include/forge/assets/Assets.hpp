// engine/include/forge/assets/Assets.hpp
//
// The border crossing (ADR-016): outside live tool formats (OBJ, glTF, PNG),
// inside lives exactly what the engine wants — Vertex arrays and RGBA8
// texels. Importers produce plain CPU data; the Renderer consumes plain CPU
// data; neither ever sees the other's third-party types. That seam is also
// what makes hot reload cheap: reload the file, walk the same upload path.
//
// Assimp and stb_image are IMPLEMENTATION DETAILS confined to src/assets/.
// Paths are relative to the working directory until P10 adds a proper asset
// root; the sandbox and CI both run from the repo root.

#pragma once

#include "forge/renderer/Renderer.hpp" // forge::Vertex

#include <cstdint>
#include <string>
#include <vector>

namespace forge::assets {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Imports the FIRST mesh in the file (OBJ and glTF are the enabled
// importers). Triangulated; smooth normals generated when missing; UVs
// flipped to image convention (v = 0 at the top, matching our textures).
// Throws std::runtime_error with the importer's reason on failure.
[[nodiscard]] MeshData loadMesh(const std::string& path);

struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba; // 8-bit RGBA, tightly packed, row 0 = top
};

// PNG/JPEG via stb_image, always expanded to RGBA8. Throws on failure.
[[nodiscard]] ImageData loadImage(const std::string& path);

} // namespace forge::assets
