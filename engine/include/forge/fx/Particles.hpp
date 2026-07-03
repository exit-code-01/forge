// engine/include/forge/fx/Particles.hpp
//
// Lean CPU burst particles (ADR-020): sparks, dust, impact puffs — VAULT's
// vocabulary. Particles render as tiny instances of an existing mesh through
// the normal DrawItem path (a few hundred draws is nothing at this scale);
// GPU instancing/billboards arrive when a profiler, not a feeling, asks.

#pragma once

#include "forge/renderer/Renderer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>

#include <random>
#include <vector>

namespace forge::fx {

class ParticleEmitter {
public:
    // Spray `count` particles from `origin`, upward-biased random velocity.
    void burst(const glm::vec3& origin, int count) {
        std::uniform_real_distribution<float> dir(-1.0f, 1.0f);
        std::uniform_real_distribution<float> up(1.5f, 4.5f);
        std::uniform_real_distribution<float> life(0.35f, 0.9f);
        std::uniform_real_distribution<float> size(0.03f, 0.09f);
        for (int i = 0; i < count; ++i) {
            Particle p;
            p.position = origin;
            p.velocity = {dir(m_rng) * 2.0f, up(m_rng), dir(m_rng) * 2.0f};
            p.maxLife = p.life = life(m_rng);
            p.size = size(m_rng);
            m_particles.push_back(p);
        }
    }

    void update(float dt) {
        constexpr glm::vec3 kGravity{0.0f, -9.81f, 0.0f};
        for (size_t i = 0; i < m_particles.size();) {
            Particle& p = m_particles[i];
            p.life -= dt;
            if (p.life <= 0.0f) {
                p = m_particles.back(); // swap-and-pop, ECS-style
                m_particles.pop_back();
                continue;
            }
            p.velocity += kGravity * dt;
            p.position += p.velocity * dt;
            ++i;
        }
    }

    // Particles shrink as they die — cheap fade without alpha blending.
    void appendDrawItems(std::vector<DrawItem>& out, MeshHandle mesh, TextureHandle texture) const {
        for (const Particle& p : m_particles) {
            const float scale = p.size * (p.life / p.maxLife);
            out.push_back(
                {mesh, texture,
                 glm::scale(glm::translate(glm::mat4(1.0f), p.position), glm::vec3(scale))});
        }
    }

    [[nodiscard]] size_t count() const { return m_particles.size(); }

private:
    struct Particle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float life = 0.0f;
        float maxLife = 1.0f;
        float size = 0.05f;
    };
    std::vector<Particle> m_particles;
    std::mt19937 m_rng{20260703}; // deterministic: same seed, same sparks
};

} // namespace forge::fx
