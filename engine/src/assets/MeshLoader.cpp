// engine/src/assets/MeshLoader.cpp — the ONLY TU that includes Assimp.
#include "forge/assets/Assets.hpp"
#include "forge/core/Log.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <stdexcept>
#include <string>

namespace forge::assets {

MeshData loadMesh(const std::string& path) {
    Assimp::Importer importer;
    // FlipUVs converts OBJ/GL's bottom-up V to our image convention (ADR-016).
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
                  aiProcess_FlipUVs | aiProcess_ImproveCacheLocality);
    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mNumMeshes == 0) {
        throw std::runtime_error("mesh import failed: " + path + " (" + importer.GetErrorString() +
                                 ")");
    }

    // First mesh only: scene graphs, materials, and multi-mesh files become
    // real once the sample game needs them — the plumbing below won't change.
    const aiMesh* mesh = scene->mMeshes[0];
    MeshData out;
    out.vertices.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex v{};
        v.position = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};
        v.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
        if (mesh->HasTextureCoords(0)) {
            v.uv = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
        }
        out.vertices.push_back(v);
    }
    out.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace& face = mesh->mFaces[f]; // triangulated: always 3
        out.indices.push_back(face.mIndices[0]);
        out.indices.push_back(face.mIndices[1]);
        out.indices.push_back(face.mIndices[2]);
    }

    FORGE_INFO("mesh imported: {} ({} vertices, {} triangles)", path, out.vertices.size(),
               out.indices.size() / 3);
    return out;
}

} // namespace forge::assets
