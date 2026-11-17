// engine/src/anim/Animator.cpp — clip sampling, cross-fade, joint palette (ADR-030).
#include "forge/anim/Skeleton.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace forge::anim {

namespace {

// Shortest-arc slerp (q and -q are the same rotation: pick the near one),
// falling back to a normalized lerp when the two are nearly parallel.
glm::quat slerpShortest(const glm::quat& a, glm::quat b, float t) {
    float cosTheta = glm::dot(a, b);
    if (cosTheta < 0.0f) {
        b = -b;
        cosTheta = -cosTheta;
    }
    if (cosTheta > 0.9995f) {
        return glm::normalize(a * (1.0f - t) + b * t);
    }
    const float theta = std::acos(cosTheta);
    const float sinTheta = std::sin(theta);
    return (a * std::sin((1.0f - t) * theta) + b * std::sin(t * theta)) / sinTheta;
}

glm::vec3 interpolate(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; }
glm::quat interpolate(const glm::quat& a, const glm::quat& b, float t) {
    return slerpShortest(a, b, t);
}

template <typename T> T sampleTrack(const Track<T>& track, float time, const T& fallback) {
    const std::vector<float>& times = track.times;
    if (times.empty() || track.values.size() < times.size()) {
        return fallback;
    }
    if (time <= times.front()) {
        return track.values.front();
    }
    if (time >= times.back()) {
        return track.values[times.size() - 1];
    }
    const auto upper = std::upper_bound(times.begin(), times.end(), time);
    const auto b = static_cast<size_t>(upper - times.begin());
    const size_t a = b - 1;
    if (track.interpolation == Interpolation::Step) {
        return track.values[a];
    }
    const float span = times[b] - times[a];
    const float alpha = span > 0.0f ? (time - times[a]) / span : 0.0f;
    return interpolate(track.values[a], track.values[b], alpha);
}

glm::mat4 compose(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
    glm::mat4 m = glm::mat4_cast(r);
    m[0] *= s.x;
    m[1] *= s.y;
    m[2] *= s.z;
    m[3] = glm::vec4(t, 1.0f);
    return m;
}

void palette(const Skeleton& skeleton, const std::vector<glm::mat4>& global,
             std::vector<glm::mat4>& out) {
    for (size_t j = 0; j < skeleton.jointNodes.size(); ++j) {
        const auto node = static_cast<size_t>(skeleton.jointNodes[j]);
        out.push_back(skeleton.root * global[node] * skeleton.inverseBind[j]);
    }
}

} // namespace

void restPalette(const Skeleton& skeleton, std::vector<glm::mat4>& out) {
    std::vector<glm::mat4> global(skeleton.nodes.size());
    for (size_t i = 0; i < skeleton.nodes.size(); ++i) {
        const Skeleton::Node& n = skeleton.nodes[i];
        const glm::mat4 local = compose(n.translation, n.rotation, n.scale);
        global[i] = n.parent >= 0 ? global[static_cast<size_t>(n.parent)] * local : local;
    }
    palette(skeleton, global, out);
}

Animator::Animator(std::shared_ptr<const Skeleton> skeleton,
                   std::shared_ptr<const std::vector<SkeletalClip>> clips)
    : m_skeleton(std::move(skeleton)), m_clips(std::move(clips)) {}

size_t Animator::jointCount() const { return m_skeleton ? m_skeleton->jointCount() : 0; }

float Animator::duration() const {
    return m_current.clip >= 0 ? (*m_clips)[static_cast<size_t>(m_current.clip)].duration : 0.0f;
}

std::string_view Animator::clipName() const {
    return m_current.clip >= 0
               ? std::string_view((*m_clips)[static_cast<size_t>(m_current.clip)].name)
               : std::string_view();
}

bool Animator::play(std::string_view clip, const PlayOptions& options) {
    if (!m_skeleton || !m_clips) {
        return false;
    }
    int32_t index = -1;
    for (size_t i = 0; i < m_clips->size(); ++i) {
        if ((*m_clips)[i].name == clip) {
            index = static_cast<int32_t>(i);
            break;
        }
    }
    if (index < 0) {
        return false;
    }
    if (index == m_current.clip && !options.restart) {
        m_current.loop = options.loop;
        m_current.speed = options.speed;
        return true;
    }
    const bool fade = options.fade > 0.0f && m_current.clip >= 0;
    if (fade) {
        // Interrupting a fade: keep whichever layer dominates the pose now.
        const bool previousDominates =
            m_previous.clip >= 0 && m_fadeDuration > 0.0f && m_fadeTime < 0.5f * m_fadeDuration;
        m_previous = previousDominates ? m_previous : m_current;
        m_fadeTime = 0.0f;
        m_fadeDuration = options.fade;
    } else {
        m_previous = Layer{};
        m_fadeDuration = 0.0f;
    }
    m_current = Layer{index, 0.0f, options.speed, options.loop};
    if (options.speed < 0.0f) {
        m_current.time = (*m_clips)[static_cast<size_t>(index)].duration;
    }
    return true;
}

void Animator::advance(Layer& layer, float dt) const {
    if (layer.clip < 0) {
        return;
    }
    const float length = (*m_clips)[static_cast<size_t>(layer.clip)].duration;
    layer.time += dt * layer.speed;
    if (length <= 0.0f) {
        layer.time = 0.0f;
    } else if (layer.loop) {
        layer.time = std::fmod(layer.time, length);
        if (layer.time < 0.0f) {
            layer.time += length;
        }
    } else {
        layer.time = std::clamp(layer.time, 0.0f, length); // holds the last pose
    }
}

void Animator::update(float dtSeconds) {
    advance(m_current, dtSeconds);
    if (m_previous.clip >= 0) {
        advance(m_previous, dtSeconds);
        m_fadeTime += dtSeconds;
        if (m_fadeTime >= m_fadeDuration) {
            m_previous = Layer{};
        }
    }
}

void Animator::samplePose(const Layer& layer, std::vector<Pose>& pose) const {
    const Skeleton& skeleton = *m_skeleton;
    pose.resize(skeleton.nodes.size());
    for (size_t i = 0; i < skeleton.nodes.size(); ++i) {
        const Skeleton::Node& n = skeleton.nodes[i];
        pose[i] = Pose{n.translation, n.rotation, n.scale};
    }
    if (layer.clip < 0) {
        return;
    }
    for (const NodeChannel& channel : (*m_clips)[static_cast<size_t>(layer.clip)].channels) {
        if (channel.node < 0 || static_cast<size_t>(channel.node) >= pose.size()) {
            continue;
        }
        Pose& p = pose[static_cast<size_t>(channel.node)];
        p.t = sampleTrack(channel.translation, layer.time, p.t);
        p.r = glm::normalize(sampleTrack(channel.rotation, layer.time, p.r));
        p.s = sampleTrack(channel.scale, layer.time, p.s);
    }
}

void Animator::appendPalette(std::vector<glm::mat4>& out) const {
    if (!m_skeleton) {
        return;
    }
    const Skeleton& skeleton = *m_skeleton;
    samplePose(m_current, m_poseA);
    if (m_previous.clip >= 0 && m_fadeDuration > 0.0f) {
        // Cross-fade: previous (weight 1-w) -> current (weight w).
        samplePose(m_previous, m_poseB);
        const float w = std::clamp(m_fadeTime / m_fadeDuration, 0.0f, 1.0f);
        for (size_t i = 0; i < m_poseA.size(); ++i) {
            m_poseA[i].t = interpolate(m_poseB[i].t, m_poseA[i].t, w);
            m_poseA[i].r = slerpShortest(m_poseB[i].r, m_poseA[i].r, w);
            m_poseA[i].s = interpolate(m_poseB[i].s, m_poseA[i].s, w);
        }
    }
    m_global.resize(skeleton.nodes.size());
    for (size_t i = 0; i < skeleton.nodes.size(); ++i) {
        const glm::mat4 local = compose(m_poseA[i].t, m_poseA[i].r, m_poseA[i].s);
        const int32_t parent = skeleton.nodes[i].parent;
        m_global[i] = parent >= 0 ? m_global[static_cast<size_t>(parent)] * local : local;
    }
    palette(skeleton, m_global, out);
}

} // namespace forge::anim
