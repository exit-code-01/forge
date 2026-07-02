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
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
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

} // namespace forge
