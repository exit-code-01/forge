// engine/include/forge/anim/Clip.hpp
//
// Keyframe TRANSFORM animation (ADR-020): position tracks sampled with
// linear interpolation — doors, platforms, bobbing drones: VAULT's entire
// animation vocabulary. This sampling layer is also the foundation GPU
// skinning stands on later; skinning itself is deferred until the game's
// art pass proves it needs an organic character.

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

    [[nodiscard]] float duration() const {
        return positionKeys.empty() ? 0.0f : positionKeys.back().time;
    }

    // Linear interpolation between neighboring keys; loops by default.
    [[nodiscard]] glm::vec3 samplePosition(float time, bool loop = true) const {
        if (positionKeys.empty()) {
            return glm::vec3(0.0f);
        }
        const float end = duration();
        if (loop && end > 0.0f) {
            time = std::fmod(time, end);
        }
        if (time <= positionKeys.front().time) {
            return positionKeys.front().value;
        }
        if (time >= end) {
            return positionKeys.back().value;
        }
        for (size_t i = 1; i < positionKeys.size(); ++i) {
            if (time < positionKeys[i].time) {
                const Vec3Key& a = positionKeys[i - 1];
                const Vec3Key& b = positionKeys[i];
                const float span = b.time - a.time;
                const float alpha = span > 0.0f ? (time - a.time) / span : 0.0f;
                return a.value + (b.value - a.value) * alpha;
            }
        }
        return positionKeys.back().value;
    }
};

} // namespace forge::anim
