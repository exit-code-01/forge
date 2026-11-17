// engine/include/forge/anim/Skeleton.hpp
//
// Skeletal animation (ADR-030) — the organic-character half of the anim
// layer; Clip.hpp stays the rigid-prop half. Data comes from glTF (cgltf,
// src/assets/GltfLoader.cpp); this header is plain data + the Animator:
//
//   Skeleton      the file's node hierarchy (rest pose, parents first), the
//                 joints that deform meshes (node + inverse bind matrix), and
//                 the per-model import correction as a root matrix.
//   SkeletalClip  per-node translation / rotation / scale tracks, seconds.
//                 LINEAR (vec3 lerp, quaternion slerp) and STEP; glTF
//                 CUBICSPLINE is imported as LINEAR through its key values
//                 (tangents dropped).
//   Animator      one per animated entity: plays a clip (loop, speed), cross-
//                 fades between two clips over a set time, and writes the
//                 joint palette the skinned shaders read:
//                     palette[j] = root * global(jointNode[j]) * inverseBind[j]
//
// Rigid parts of an animated file (a sword parented to a hand node, a
// node-animated door leaf) are joints too: one joint, weight 1, identity
// inverse bind — the same GPU path serves both.

#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace forge::anim {

struct Skeleton {
    struct Node {
        std::string name;
        int32_t parent = -1; // index into nodes; parents always precede children
        glm::vec3 translation{0.0f}; // rest pose, local to the parent
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };
    std::vector<Node> nodes;
    std::vector<int32_t> jointNodes;    // joint j -> node index
    std::vector<glm::mat4> inverseBind; // joint j: bind (mesh) space -> joint space
    glm::mat4 root{1.0f};               // import correction (scale / up-axis), above all

    [[nodiscard]] size_t jointCount() const { return jointNodes.size(); }
};

enum class Interpolation : uint8_t { Linear, Step };

template <typename T> struct Track {
    std::vector<float> times; // seconds, ascending
    std::vector<T> values;
    Interpolation interpolation = Interpolation::Linear;
};

struct NodeChannel {
    int32_t node = -1;
    Track<glm::vec3> translation; // empty track = node keeps its rest value
    Track<glm::quat> rotation;
    Track<glm::vec3> scale;
};

struct SkeletalClip {
    std::string name;
    float duration = 0.0f; // seconds (the last key of any track)
    std::vector<NodeChannel> channels;
};

struct PlayOptions {
    bool loop = true;
    float speed = 1.0f;     // 1 = authored rate; negative plays backwards
    float fade = 0.2f;      // seconds to cross-fade from the current clip (0 = cut)
    bool restart = false;   // replaying the CURRENT clip restarts it only if set
};

// Rest-pose palette (no clip applied): what an un-animated entity draws, and
// what the importer skins on the CPU to report bind-pose bounds.
void restPalette(const Skeleton& skeleton, std::vector<glm::mat4>& out);

class Animator {
public:
    Animator() = default;
    Animator(std::shared_ptr<const Skeleton> skeleton,
             std::shared_ptr<const std::vector<SkeletalClip>> clips);

    // Starts `clip` (cross-fading from whatever plays now). Returns false if
    // the clip is unknown — the current animation keeps playing. Playing the
    // clip that is already current only updates loop/speed (unless restart).
    bool play(std::string_view clip, const PlayOptions& options = {});
    void update(float dtSeconds);
    // Appends jointCount() matrices (model space) for the skinned shaders.
    void appendPalette(std::vector<glm::mat4>& out) const;

    [[nodiscard]] bool valid() const { return m_skeleton != nullptr; }
    [[nodiscard]] size_t jointCount() const;
    [[nodiscard]] bool playing() const { return m_current.clip >= 0; }
    [[nodiscard]] float time() const { return m_current.time; }     // seconds into the clip
    [[nodiscard]] float duration() const;                           // of the current clip
    [[nodiscard]] std::string_view clipName() const;                // "" when none

private:
    struct Layer {
        int32_t clip = -1;
        float time = 0.0f;
        float speed = 1.0f;
        bool loop = true;
    };
    struct Pose { // local TRS per node
        glm::vec3 t;
        glm::quat r;
        glm::vec3 s;
    };
    void advance(Layer& layer, float dt) const;
    void samplePose(const Layer& layer, std::vector<Pose>& pose) const;

    std::shared_ptr<const Skeleton> m_skeleton;
    std::shared_ptr<const std::vector<SkeletalClip>> m_clips;
    Layer m_current;
    Layer m_previous;       // fading OUT while m_current fades in
    float m_fadeTime = 0.0f;
    float m_fadeDuration = 0.0f;
    // Scratch, reused every frame (appendPalette is hot and const).
    mutable std::vector<Pose> m_poseA;
    mutable std::vector<Pose> m_poseB;
    mutable std::vector<glm::mat4> m_global;
};

} // namespace forge::anim
