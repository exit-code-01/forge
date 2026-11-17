// engine/src/assets/ModelImport.cpp — format-agnostic model import glue
// (ADR-030). No third-party includes: Assimp and cgltf stay in their TUs.
#include "ImportInternal.hpp"
#include "forge/core/Log.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

namespace forge::assets {

namespace detail {

glm::mat4 importCorrection(const ImportOptions& options) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, glm::radians(options.rotationDeg.z), {0.0f, 0.0f, 1.0f});
    m = glm::rotate(m, glm::radians(options.rotationDeg.y), {0.0f, 1.0f, 0.0f});
    m = glm::rotate(m, glm::radians(options.rotationDeg.x), {1.0f, 0.0f, 0.0f});
    return glm::scale(m, glm::vec3(options.scale));
}

bool isHidden(const ImportOptions& options, std::string_view nodeName) {
    return !nodeName.empty() &&
           std::find(options.hide.begin(), options.hide.end(), nodeName) != options.hide.end();
}

void transformMesh(MeshData& mesh, const glm::mat4& transform) {
    const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(transform));
    for (Vertex& v : mesh.vertices) {
        v.position = glm::vec3(transform * glm::vec4(v.position, 1.0f));
        const glm::vec3 n = normalMatrix * v.normal;
        const float len = glm::length(n);
        v.normal = len > 1e-12f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    // A mirroring transform turns CCW triangles CW: swap two corners back.
    if (glm::determinant(glm::mat3(transform)) < 0.0f) {
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
        }
    }
}

void generateNormals(MeshData& mesh) {
    for (Vertex& v : mesh.vertices) {
        v.normal = glm::vec3(0.0f);
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Vertex& a = mesh.vertices[mesh.indices[i]];
        Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        // Unnormalized cross product = area-weighted face normal.
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (Vertex& v : mesh.vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

} // namespace detail

ModelData loadModel(const std::string& path, const ImportOptions& options) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    ModelData model = (ext == ".gltf" || ext == ".glb") ? detail::loadGltfModel(path, options)
                                                        : detail::loadObjModel(path, options);
    if (model.submeshes.empty()) {
        throw std::runtime_error("model import failed: " + path + " (no triangle meshes)");
    }
    if (model.materials.empty()) {
        model.materials.push_back(MaterialData{"default", glm::vec4(1.0f), -1, false});
    }
    for (SubmeshData& part : model.submeshes) {
        part.material = std::min(part.material, static_cast<uint32_t>(model.materials.size() - 1));
    }

    // The per-model import correction (units / up-axis), applied once:
    // baked into static parts; skinned parts get it as the skeleton's root.
    const glm::mat4 correction = detail::importCorrection(options);
    if (correction != glm::mat4(1.0f)) {
        for (SubmeshData& part : model.submeshes) {
            if (part.skin.empty()) {
                detail::transformMesh(part.mesh, correction);
            }
        }
    }
    // Skinned parts are measured in the REST pose, skinned on the CPU.
    std::vector<glm::mat4> rest;
    if (model.skeleton) {
        anim::restPalette(*model.skeleton, rest);
    }
    const auto restPosition = [&](const SubmeshData& part, size_t i) {
        const glm::vec3 p = part.mesh.vertices[i].position;
        if (part.skin.empty() || rest.empty()) {
            return p;
        }
        const SkinVertex& sv = part.skin[i];
        glm::vec3 out(0.0f);
        for (int k = 0; k < 4; ++k) {
            const size_t joint = sv.joints[k];
            if (sv.weights[k] > 0.0f && joint < rest.size()) {
                out += sv.weights[k] * glm::vec3(rest[joint] * glm::vec4(p, 1.0f));
            }
        }
        return out;
    };

    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    size_t vertexCount = 0;
    size_t triangleCount = 0;
    for (const SubmeshData& part : model.submeshes) {
        for (size_t i = 0; i < part.mesh.vertices.size(); ++i) {
            const glm::vec3 p = restPosition(part, i);
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        vertexCount += part.mesh.vertices.size();
        triangleCount += part.mesh.indices.size() / 3;
    }
    model.boundsMin = lo;
    model.boundsMax = hi;
    const glm::vec3 size = hi - lo;
    // Sizing is never guesswork: every load states the model's extent.
    FORGE_INFO("model imported: {} ({} parts, {} materials, {} textures, {} vertices, {} "
               "triangles, scale {}) bounds min ({:.3f}, {:.3f}, {:.3f}) max ({:.3f}, {:.3f}, "
               "{:.3f}) size ({:.3f} x {:.3f} x {:.3f})",
               path, model.submeshes.size(), model.materials.size(), model.images.size(),
               vertexCount, triangleCount, options.scale, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z,
               size.x, size.y, size.z);
    if (model.skeleton) {
        FORGE_INFO("model animated: {} ({} nodes, {} joints, {} clips)", path,
                   model.skeleton->nodes.size(), model.skeleton->jointCount(),
                   model.clips.size());
    }
    return model;
}

} // namespace forge::assets
