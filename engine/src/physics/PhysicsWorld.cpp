// engine/src/physics/PhysicsWorld.cpp
//
// The ONLY translation unit that includes Jolt. All of Jolt's init ceremony
// (allocator, factory, type registry, layer taxonomy) lives here, once.
#include "forge/physics/PhysicsWorld.hpp"
#include "forge/core/Log.hpp"

// Jolt.h must precede every other Jolt header — it defines the macros they need.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <memory>
#include <thread>

namespace forge {

namespace {

// Jolt's collision taxonomy: OBJECT layers are what bodies wear, BROADPHASE
// layers are how the acceleration structure buckets them. Two of each is the
// canonical minimum — the whole point is that static-vs-static pairs never
// even reach the narrow phase.
namespace layers {
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
} // namespace layers

namespace bp {
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kCount = 2;
} // namespace bp

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override { return bp::kCount; }
    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == layers::kNonMoving ? bp::kNonMoving : bp::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == bp::kNonMoving ? "NON_MOVING" : "MOVING";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer,
                                     JPH::BroadPhaseLayer bpLayer) const override {
        // Only static-vs-static is skipped.
        return layer == layers::kMoving || bpLayer == bp::kMoving;
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == layers::kMoving || b == layers::kMoving;
    }
};

// Process-wide Jolt globals, refcounted like GLFW in Window.cpp: first world
// initializes, last world tears down. Main-thread only by our own contract.
int s_joltRefCount = 0;

} // namespace

struct PhysicsWorld::Impl {
    // Declaration order matters: the filters must outlive the system.
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhase;
    ObjectLayerPairFilter objectPairFilter;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> system;
    float accumulator = 0.0f;

    // VAULT week 1: the one player character (see header rationale).
    JPH::Ref<JPH::CharacterVirtual> character;
    float characterVerticalVelocity = 0.0f;
};

PhysicsWorld::PhysicsWorld() : m_impl(std::make_unique<Impl>()) {
    if (s_joltRefCount++ == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    // 10 MiB scratch arena for the solver's per-step working set.
    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    const auto hardware = std::thread::hardware_concurrency();
    const int workers = std::max(1, static_cast<int>(hardware) - 1); // leave main thread a core
    m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);

    m_impl->system = std::make_unique<JPH::PhysicsSystem>();
    // Capacities sized for the demo era; these are hard caps, revisit at P10.
    m_impl->system->Init(1024, 0, 1024, 1024, m_impl->broadPhaseLayers, m_impl->objectVsBroadPhase,
                         m_impl->objectPairFilter);

    FORGE_INFO("physics: Jolt up, {} workers, gravity ({:.1f}, {:.1f}, {:.1f})", workers,
               m_impl->system->GetGravity().GetX(), m_impl->system->GetGravity().GetY(),
               m_impl->system->GetGravity().GetZ());
}

PhysicsWorld::~PhysicsWorld() {
    m_impl.reset(); // world (and its Jolt objects) die before the globals
    if (--s_joltRefCount == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

BodyId PhysicsWorld::addBox(const glm::vec3& halfExtents, const glm::vec3& position, BodyType type,
                            float restitution) {
    const bool dynamic = type == BodyType::Dynamic;
    // Convex radius (edge rounding for stable contacts) must fit inside the
    // box — thin slabs need it shrunk or BoxShape rejects them.
    const float minHalf = std::min({halfExtents.x, halfExtents.y, halfExtents.z});
    const float convexRadius = std::min(JPH::cDefaultConvexRadius, minHalf * 0.5f);

    JPH::BodyCreationSettings settings(
        new JPH::BoxShape(JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z), convexRadius),
        JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(),
        dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
        dynamic ? layers::kMoving : layers::kNonMoving);
    settings.mRestitution = restitution;

    const JPH::BodyID id = m_impl->system->GetBodyInterface().CreateAndAddBody(
        settings, dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    return {id.GetIndexAndSequenceNumber()};
}

void PhysicsWorld::update(float dtSeconds) {
    constexpr float kFixedStep = 1.0f / 60.0f;
    constexpr float kMaxFrameDt = 0.1f; // debugger pause != 40 catch-up steps
    m_impl->accumulator += std::min(dtSeconds, kMaxFrameDt);
    while (m_impl->accumulator >= kFixedStep) {
        m_impl->system->Update(kFixedStep, 1, m_impl->tempAllocator.get(), m_impl->jobSystem.get());
        m_impl->accumulator -= kFixedStep;
    }
    // Render sees the last completed tick; interpolation between ticks is
    // P8 polish, invisible at 60 Hz render / 60 Hz sim.
}

glm::mat4 PhysicsWorld::bodyTransform(BodyId id) const {
    const JPH::RMat44 m =
        m_impl->system->GetBodyInterface().GetWorldTransform(JPH::BodyID(id.value));
    glm::mat4 out{1.0f};
    for (int c = 0; c < 4; ++c) {
        const JPH::Vec4 column = m.GetColumn4(c);
        out[c] = glm::vec4(column.GetX(), column.GetY(), column.GetZ(), column.GetW());
    }
    return out;
}

void PhysicsWorld::addLinearVelocity(BodyId id, const glm::vec3& velocity) {
    // Activates a sleeping body — a kicked object must wake up.
    m_impl->system->GetBodyInterface().AddLinearVelocity(
        JPH::BodyID(id.value), JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

void PhysicsWorld::teleport(BodyId id, const glm::vec3& position) {
    JPH::BodyInterface& bodies = m_impl->system->GetBodyInterface();
    const JPH::BodyID body(id.value);
    bodies.SetPositionAndRotation(body, JPH::RVec3(position.x, position.y, position.z),
                                  bodies.GetRotation(body), JPH::EActivation::Activate);
    if (bodies.GetMotionType(body) == JPH::EMotionType::Dynamic) {
        bodies.SetLinearAndAngularVelocity(body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    }
}

void PhysicsWorld::setLinearVelocity(BodyId id, const glm::vec3& velocity) {
    m_impl->system->GetBodyInterface().SetLinearVelocity(
        JPH::BodyID(id.value), JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

std::optional<BodyId> PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                            float maxDistance) const {
    const JPH::RRayCast ray{JPH::RVec3(origin.x, origin.y, origin.z),
                            JPH::Vec3(direction.x, direction.y, direction.z) * maxDistance};
    JPH::RayCastResult hit;
    if (m_impl->system->GetNarrowPhaseQuery().CastRay(ray, hit)) {
        return BodyId{hit.mBodyID.GetIndexAndSequenceNumber()};
    }
    return std::nullopt;
}

void PhysicsWorld::createCharacter(const glm::vec3& position, float radius,
                                   float cylinderHalfHeight) {
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = new JPH::CapsuleShape(cylinderHalfHeight, radius);
    settings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
    m_impl->character =
        new JPH::CharacterVirtual(settings, JPH::RVec3(position.x, position.y, position.z),
                                  JPH::Quat::sIdentity(), 0, m_impl->system.get());
    FORGE_INFO("physics: character controller up (r {:.2f}, height {:.2f})", radius,
               2.0f * (cylinderHalfHeight + radius));
}

void PhysicsWorld::moveCharacter(const glm::vec3& horizontalVelocity, bool jump, float dtSeconds) {
    JPH::CharacterVirtual& character = *m_impl->character;
    const bool grounded =
        character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    if (grounded) {
        m_impl->characterVerticalVelocity = jump ? 5.5f : 0.0f;
    } else {
        m_impl->characterVerticalVelocity -= 9.81f * dtSeconds;
    }
    character.SetLinearVelocity(
        JPH::Vec3(horizontalVelocity.x, m_impl->characterVerticalVelocity, horizontalVelocity.z));

    // ExtendedUpdate = move + slide + stick-to-floor + walk-stairs, the
    // whole character-controller kitchen in one Jolt call.
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, 0.35f, 0.0f);
    character.ExtendedUpdate(dtSeconds, m_impl->system->GetGravity(), updateSettings,
                             m_impl->system->GetDefaultBroadPhaseLayerFilter(layers::kMoving),
                             m_impl->system->GetDefaultLayerFilter(layers::kMoving),
                             JPH::BodyFilter{}, JPH::ShapeFilter{}, *m_impl->tempAllocator);
}

glm::vec3 PhysicsWorld::characterPosition() const {
    const JPH::RVec3 p = m_impl->character->GetPosition();
    return {p.GetX(), p.GetY(), p.GetZ()};
}

std::vector<BodyId> PhysicsWorld::overlapBox(const glm::vec3& center,
                                             const glm::vec3& halfExtents) const {
    const JPH::AABox box(
        JPH::Vec3(center.x - halfExtents.x, center.y - halfExtents.y, center.z - halfExtents.z),
        JPH::Vec3(center.x + halfExtents.x, center.y + halfExtents.y, center.z + halfExtents.z));
    JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
    // MOVING broadphase layer == dynamic bodies only (the plate never
    // wants to count itself or the walls).
    m_impl->system->GetBroadPhaseQuery().CollideAABox(
        box, collector, JPH::SpecifiedBroadPhaseLayerFilter(JPH::BroadPhaseLayer(1)));
    std::vector<BodyId> out;
    out.reserve(collector.mHits.size());
    for (const JPH::BodyID& id : collector.mHits) {
        out.push_back({id.GetIndexAndSequenceNumber()});
    }
    return out;
}

bool PhysicsWorld::characterGrounded() const {
    return m_impl->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

} // namespace forge
