// engine/include/forge/anim/Clip.hpp
//
// Keyframe TRANSFORM animation (ADR-020): position tracks sampled with
// linear interpolation — doors, platforms, bobbing drones: VAULT's entire
// animation vocabulary. The character pass (ADR-027) added euler rotation
// tracks: rigid-rig character work (viewmodel wind-ups, drone banking,
// prop machinery) needs orientation curves, not skinning. Euler lerp is
// the affordable answer for the small arcs rigid rigs make — keep steps
// under 180 degrees per key pair; quaternion slerp arrives with skeletal
// animation if an organic character ever demands it. This sampling layer
// is also the foundation GPU skinning stands on later.

#pragma once

#include <glm/vec3.hpp>

#include <cmath>
#include <vector>

namespace forge::anim {

struct Vec3Key {
    float time = 0.0f; // seconds; keys must be sorted ascending
    glm::vec3 value{0.0f};
};

struct Clip {
    std::vector<Vec3Key> positionKeys;
    std::vector<Vec3Key> eulerKeys; // degrees; shares the clip's timeline

    // Both tracks loop on the CLIP's duration (the longer track), so a
    // clip whose rotation ends before its position stays in sync.
    [[nodiscard]] float duration() const {
        const float p = positionKeys.empty() ? 0.0f : positionKeys.back().time;
        const float e = eulerKeys.empty() ? 0.0f : eulerKeys.back().time;
        return p > e ? p : e;
    }

    // Linear interpolation between neighboring keys; loops by default.
    [[nodiscard]] glm::vec3 samplePosition(float time, bool loop = true) const {
        return sampleTrack(positionKeys, time, loop);
    }

    // Empty track samples to zero — the identity rotation.
    [[nodiscard]] glm::vec3 sampleEuler(float time, bool loop = true) const {
        return sampleTrack(eulerKeys, time, loop);
    }

private:
    [[nodiscard]] glm::vec3 sampleTrack(const std::vector<Vec3Key>& keys, float time,
                                        bool loop) const {
        if (keys.empty()) {
            return glm::vec3(0.0f);
        }
        const float end = duration();
        if (loop && end > 0.0f) {
            time = std::fmod(time, end);
        }
        if (time <= keys.front().time) {
            return keys.front().value;
        }
        if (time >= keys.back().time) {
            return keys.back().value;
        }
        for (size_t i = 1; i < keys.size(); ++i) {
            if (time < keys[i].time) {
                const Vec3Key& a = keys[i - 1];
                const Vec3Key& b = keys[i];
                const float span = b.time - a.time;
                const float alpha = span > 0.0f ? (time - a.time) / span : 0.0f;
                return a.value + (b.value - a.value) * alpha;
            }
        }
        return keys.back().value;
    }
};

} // namespace forge::anim
