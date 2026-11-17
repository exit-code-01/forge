// engine/include/forge/renderer/Model.hpp
//
// The engine's "model" (ADR-030): what a file in assets/models becomes on
// the GPU — a list of parts, each a mesh with its own material (texture +
// base-colour tint). The host spawns a model exactly like it used to spawn
// a single mesh; appendModelDraws() turns one entity into one DrawItem per
// part, all under the entity's transform.
//
// THE OVERRIDE RULE (one place, here):
//   * Entity tint MULTIPLIES every part's own tint; entity emissive is ADDED
//     to every part.
//   * A part with no material of its own (an OBJ without MTL — every legacy
//     VAULT asset — or a material-less glTF primitive) shows the ENTITY's
//     texture, exactly as single-mesh models always did.
//   * A part with an authored material shows its own texture (white when the
//     material is untextured) times its base colour — UNLESS the entity was
//     given a named texture (spawn's texture argument resolved, or
//     forge.scene.setTexture). A named texture is an explicit override: every
//     part then shows that texture as authored (part base colours dropped),
//     which keeps the colour-language swaps working on imported models.
//     Spawn with texture "" to keep a model's own materials.

#pragma once

#include "forge/anim/Skeleton.hpp"
#include "forge/renderer/Renderer.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace forge {

namespace assets {
struct ModelData;
}

struct ModelPart {
    MeshHandle mesh;
    TextureHandle texture; // the material's own (Renderer::whiteTexture() if untextured)
    glm::vec3 tint{1.0f};  // material base colour, linear
    bool authored = false; // false: the entity's texture shows (see the rule above)
    bool skinned = false;  // mesh carries a skin stream (animated models)
};

struct Model {
    std::string name;
    std::vector<ModelPart> parts;
    glm::vec3 boundsMin{-0.5f}; // model space, after the import correction
    glm::vec3 boundsMax{0.5f};
    // Animated models: shared by every entity that spawns this model; each
    // entity owns an anim::Animator over them. Null/empty for static models.
    std::shared_ptr<const anim::Skeleton> skeleton;
    std::shared_ptr<const std::vector<anim::SkeletalClip>> clips;
    // CPU copy for mesh colliders (ADR-032): every STATIC part's triangles,
    // merged, in model space with the import correction baked in — exactly
    // the geometry the parts draw. Empty for skinned parts and the cube.
    std::vector<glm::vec3> collisionVertices;
    std::vector<uint32_t> collisionIndices;
};

// Per-entity look layered over a model (the host's MeshRendererC carries it).
struct ModelLook {
    TextureHandle texture;        // entity texture (checker when none was named)
    bool textureOverride = false; // a NAMED texture was given — see the rule
    glm::vec3 emissive{0.0f};
    glm::vec3 tint{1.0f};
    float roughness = DrawItem::kDefaultRoughness; // ADR-033: whole entity, every part
    float metallic = DrawItem::kDefaultMetallic;
};

// File-backed images already on the GPU, by normalized path: pass the same
// cache to every uploadModel so a pack's shared atlas uploads once.
using TextureCache = std::unordered_map<std::string, TextureHandle>;

// Uploads every part's mesh and every image (once per model; images shared
// between materials upload once, and once per `cache` if one is given).
// Load-time API, like addMesh.
[[nodiscard]] Model uploadModel(Renderer& renderer, const assets::ModelData& data, std::string name,
                                TextureCache* cache = nullptr);

// One mesh as a one-part, material-less model (the built-in cube, sparks).
[[nodiscard]] Model singleMeshModel(MeshHandle mesh, std::string name);

// Appends one DrawItem per part under `world`, applying the override rule.
// jointOffset: where this entity's palette starts in the frame's joint
// array (skinned parts only; kNoJoints draws the bind pose).
void appendModelDraws(std::vector<DrawItem>& out, const Model& model, const glm::mat4& world,
                      const ModelLook& look, uint32_t jointOffset = DrawItem::kNoJoints);

} // namespace forge
