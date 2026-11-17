// engine/src/assets/MeshLoader.cpp — the ONLY TU that includes Assimp.
//
// Assimp reads OBJ (+ MTL) only since ADR-030; glTF goes through cgltf
// (GltfLoader.cpp), which keeps the spec's skin/animation semantics exact.
#include "ImportInternal.hpp"
#include "forge/core/Log.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace forge::assets {

namespace {

// FlipUVs converts OBJ/GL's bottom-up V to our image convention (ADR-016).
constexpr unsigned int kImportFlags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                      aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs |
                                      aiProcess_ImproveCacheLocality;

MeshData convertMesh(const aiMesh& mesh) {
    MeshData out;
    out.vertices.reserve(mesh.mNumVertices);
    for (unsigned int i = 0; i < mesh.mNumVertices; ++i) {
        Vertex v{};
        v.position = {mesh.mVertices[i].x, mesh.mVertices[i].y, mesh.mVertices[i].z};
        v.normal = {mesh.mNormals[i].x, mesh.mNormals[i].y, mesh.mNormals[i].z};
        if (mesh.HasTextureCoords(0)) {
            v.uv = {mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y};
        }
        out.vertices.push_back(v);
    }
    out.indices.reserve(static_cast<size_t>(mesh.mNumFaces) * 3);
    for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
        const aiFace& face = mesh.mFaces[f]; // triangulated: 3, or fewer for points/lines
        if (face.mNumIndices != 3) {
            continue;
        }
        out.indices.push_back(face.mIndices[0]);
        out.indices.push_back(face.mIndices[1]);
        out.indices.push_back(face.mIndices[2]);
    }
    return out;
}

glm::mat4 toGlm(const aiMatrix4x4& m) {
    // Assimp is row-major (a1..a4 = first ROW); glm is column-major.
    return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3, m.a4,
                     m.b4, m.c4, m.d4);
}

} // namespace

MeshData loadMesh(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, kImportFlags);
    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mNumMeshes == 0) {
        throw std::runtime_error("mesh import failed: " + path + " (" + importer.GetErrorString() +
                                 ")");
    }
    // First mesh only — the P5 single-mesh path. Whole files: loadModel.
    MeshData out = convertMesh(*scene->mMeshes[0]);
    FORGE_INFO("mesh imported: {} ({} vertices, {} triangles)", path, out.vertices.size(),
               out.indices.size() / 3);
    return out;
}

namespace detail {

ModelData loadObjModel(const std::string& path, const ImportOptions& options) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, kImportFlags);
    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mNumMeshes == 0 || scene->mRootNode == nullptr) {
        throw std::runtime_error("model import failed: " + path + " (" +
                                 importer.GetErrorString() + ")");
    }

    ModelData out;
    const std::filesystem::path dir = std::filesystem::path(path).parent_path();
    std::unordered_map<std::string, int32_t> imageByPath; // MTLs reuse map_Kd files
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial& source = *scene->mMaterials[i];
        MaterialData material;
        material.name = source.GetName().C_Str();
        // No MTL: Assimp invents "DefaultMaterial" (0.6 grey). That is not
        // an authored look — keep the entity's texture, as before ADR-030.
        material.authored = material.name != AI_DEFAULT_MATERIAL_NAME;
        aiColor4D kd(1.0f, 1.0f, 1.0f, 1.0f);
        if (material.authored && source.Get(AI_MATKEY_COLOR_DIFFUSE, kd) == AI_SUCCESS) {
            material.baseColor = {kd.r, kd.g, kd.b, 1.0f};
        }
        aiString texturePath;
        if (material.authored &&
            source.GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS) {
            const std::string file = (dir / texturePath.C_Str()).lexically_normal().string();
            if (const auto it = imageByPath.find(file); it != imageByPath.end()) {
                material.image = it->second;
            } else {
                try {
                    out.images.push_back(loadImage(file));
                    out.imageSources.push_back(file);
                    material.image = static_cast<int32_t>(out.images.size() - 1);
                    imageByPath.emplace(file, material.image);
                } catch (const std::exception& e) {
                    FORGE_WARN("model {}: material '{}' texture unreadable, untextured: {}", path,
                               material.name, e.what());
                }
            }
        }
        out.materials.push_back(std::move(material));
    }

    // Walk the node hierarchy: every mesh reference, its node transform baked.
    struct Frame {
        const aiNode* node;
        glm::mat4 parent;
    };
    std::vector<Frame> stack{{scene->mRootNode, glm::mat4(1.0f)}};
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        const glm::mat4 world = frame.parent * toGlm(frame.node->mTransformation);
        const std::string nodeName = frame.node->mName.C_Str();
        if (!isHidden(options, nodeName)) {
            for (unsigned int m = 0; m < frame.node->mNumMeshes; ++m) {
                const aiMesh& mesh = *scene->mMeshes[frame.node->mMeshes[m]];
                SubmeshData part;
                part.name = nodeName.empty() ? mesh.mName.C_Str() : nodeName;
                part.mesh = convertMesh(mesh);
                part.material = mesh.mMaterialIndex;
                if (part.mesh.indices.empty()) {
                    continue;
                }
                transformMesh(part.mesh, world);
                out.submeshes.push_back(std::move(part));
            }
        }
        for (unsigned int c = frame.node->mNumChildren; c-- > 0;) {
            stack.push_back({frame.node->mChildren[c], world});
        }
    }
    return out;
}

} // namespace detail

} // namespace forge::assets
