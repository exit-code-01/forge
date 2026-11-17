// engine/src/renderer/Model.cpp — CPU model data -> GPU parts (ADR-030).
// Uses only the Renderer's PUBLIC API: no Vulkan here.
#include "forge/renderer/Model.hpp"

#include "forge/assets/Assets.hpp"

#include <utility>

namespace forge {

Model uploadModel(Renderer& renderer, const assets::ModelData& data, std::string name,
                  TextureCache* cache) {
    Model model;
    model.name = std::move(name);
    model.boundsMin = data.boundsMin;
    model.boundsMax = data.boundsMax;
    model.skeleton = data.skeleton;
    if (data.skeleton) {
        model.clips = std::make_shared<const std::vector<anim::SkeletalClip>>(data.clips);
    }

    std::vector<TextureHandle> imageTextures;
    imageTextures.reserve(data.images.size());
    for (size_t i = 0; i < data.images.size(); ++i) {
        const assets::ImageData& image = data.images[i];
        const std::string source = i < data.imageSources.size() ? data.imageSources[i] : "";
        if (cache != nullptr && !source.empty()) {
            if (const auto it = cache->find(source); it != cache->end()) {
                imageTextures.push_back(it->second);
                continue;
            }
        }
        const TextureHandle texture = renderer.addTexture(image.width, image.height, image.rgba);
        // Never cache the white fallback: a later, emptier table may fit it.
        if (cache != nullptr && !source.empty() &&
            texture.value != Renderer::whiteTexture().value) {
            cache->emplace(source, texture);
        }
        imageTextures.push_back(texture);
    }
    for (const assets::SubmeshData& submesh : data.submeshes) {
        const assets::MaterialData& material = data.materials.at(submesh.material);
        ModelPart part;
        part.skinned = !submesh.skin.empty();
        part.mesh = part.skinned ? renderer.addSkinnedMesh(submesh.mesh.vertices,
                                                           submesh.mesh.indices, submesh.skin)
                                 : renderer.addMesh(submesh.mesh.vertices, submesh.mesh.indices);
        part.authored = material.authored;
        part.tint = glm::vec3(material.baseColor);
        part.texture = material.image >= 0 ? imageTextures.at(static_cast<size_t>(material.image))
                                           : Renderer::whiteTexture();
        model.parts.push_back(part);
        if (!part.skinned) { // retained for mesh colliders (ADR-032)
            const auto base = static_cast<uint32_t>(model.collisionVertices.size());
            for (const Vertex& v : submesh.mesh.vertices) {
                model.collisionVertices.push_back(v.position);
            }
            for (const uint32_t index : submesh.mesh.indices) {
                model.collisionIndices.push_back(base + index);
            }
        }
    }
    return model;
}

Model singleMeshModel(MeshHandle mesh, std::string name) {
    Model model;
    model.name = std::move(name);
    model.parts.push_back(ModelPart{mesh, Renderer::defaultTexture(), glm::vec3(1.0f), false});
    return model;
}

void appendModelDraws(std::vector<DrawItem>& out, const Model& model, const glm::mat4& world,
                      const ModelLook& look, uint32_t jointOffset) {
    for (const ModelPart& part : model.parts) {
        DrawItem item;
        item.mesh = part.mesh;
        item.model = world;
        item.emissive = look.emissive;
        item.roughness = look.roughness;
        item.metallic = look.metallic;
        item.jointOffset = part.skinned ? jointOffset : DrawItem::kNoJoints;
        if (part.authored && !look.textureOverride) {
            item.texture = part.texture;
            item.tint = part.tint * look.tint;
        } else {
            item.texture = look.texture;
            item.tint = look.tint;
        }
        out.push_back(item);
    }
}

} // namespace forge
