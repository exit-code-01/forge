// engine/include/forge/physics/PhysicsWorld.hpp
//
// The physics simulation: a parallel world of shapes, masses, and velocities
// that advances in FIXED 1/60s ticks (variable steps make collision response
// non-deterministic and springy — the accumulator in update() converts messy
// frame times into clean ticks). Rendering subscribes: ask bodyTransform()
// where things ended up, hand the matrices to the Renderer.
//
// Jolt is an IMPLEMENTATION DETAIL (pimpl, PRIVATE link) — the same seam
// discipline as GLFW behind Window and Vulkan behind Renderer. Consumers see
// glm types and opaque handles, nothing else.
//
// Units: meters, kilograms, seconds, Y-up — matching the renderer exactly.
// THREADING: all calls main-thread; Jolt parallelizes INTERNALLY via its
// own job system. Cross-system threading is a P8+ conversation.

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace forge {

enum class BodyType {
    Static, // infinite mass, never moves: floors, walls
    Dynamic // simulated: gravity, collisions, impulses
};

// Opaque body handle (index + generation packed inside, so stale handles
// are detectable by the backend — same philosophy as ecs::Entity).
struct BodyId {
    uint32_t value = 0;
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) = delete;
    PhysicsWorld& operator=(PhysicsWorld&&) = delete;

    // Box collider. halfExtents in meters; restitution 0 = clay, 1 = superball.
    BodyId addBox(const glm::vec3& halfExtents, const glm::vec3& position, BodyType type,
                  float restitution = 0.3f);

    // Advance the simulation by a frame's worth of real time. Internally
    // steps at a fixed 60 Hz; dt is clamped so a debugger pause can't cause
    // a catch-up death spiral.
    void update(float dtSeconds);

    // Where the body ended up: rotation + translation, ready for drawFrame().
    // Scale is NOT included — physics bodies are rigid; scale your mesh.
    [[nodiscard]] glm::mat4 bodyTransform(BodyId id) const;

    // Instant velocity change, mass-independent (demo/gameplay kicks).
    // Wakes the body if it was sleeping.
    void addLinearVelocity(BodyId id, const glm::vec3& velocity);

    // Editor move: set position directly, clearing velocities (a dragged
    // object should not remember it was falling). Rotation is preserved.
    // Works on static bodies too (colliders follow editor placement).
    void teleport(BodyId id, const glm::vec3& position);

    // ---- VAULT week 1 additions (flagged ENGINE GAPs, kept minimal) ----

    // Gravity-glove support: hard-set velocity (mass-independent spring
    // carry, throws) and "what is the crosshair pointing at".
    void setLinearVelocity(BodyId id, const glm::vec3& velocity);
    [[nodiscard]] std::optional<BodyId> raycast(const glm::vec3& origin, const glm::vec3& direction,
                                                float maxDistance) const;

    // THE character controller (Jolt CharacterVirtual): capsule centered at
    // `position`, total height = 2*(cylinderHalfHeight + radius). Exactly
    // one — VAULT has one player; a roster is a later design conversation.
    void createCharacter(const glm::vec3& position, float radius, float cylinderHalfHeight);
    // Per frame: horizontal velocity (y ignored; gravity/jump handled
    // internally), jump edge, real frame dt. Walks steps up to ~0.35 m.
    void moveCharacter(const glm::vec3& horizontalVelocity, bool jump, float dtSeconds);
    [[nodiscard]] glm::vec3 characterPosition() const; // capsule center
    [[nodiscard]] bool characterGrounded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace forge
