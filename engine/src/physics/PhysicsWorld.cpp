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
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <thread>
#include <unordered_map>

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
    float characterAirTime = 0.0f; // seconds since last grounded (coyote)
    bool characterJumped = false;  // a jump is in flight (no double coyote)
    glm::vec3 characterPush{0.0f}; // one-frame external velocity
    glm::vec3 characterLastVelocity{0.0f};

    // Kinematic targets: consumed tick by tick inside update().
    std::unordered_map<uint32_t, glm::vec3> kinematicTargets;
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
    BoxDesc desc;
    desc.halfExtents = halfExtents;
    desc.position = position;
    desc.type = type;
    desc.restitution = restitution;
    return addBox(desc);
}

BodyId PhysicsWorld::addBox(const BoxDesc& desc) {
    const glm::vec3& halfExtents = desc.halfExtents;
    const bool isStatic = desc.type == BodyType::Static;
    // Convex radius (edge rounding for stable contacts) must fit inside the
    // box — thin slabs need it shrunk or BoxShape rejects them.
    const float minHalf = std::min({halfExtents.x, halfExtents.y, halfExtents.z});
    const float convexRadius = std::min(JPH::cDefaultConvexRadius, minHalf * 0.5f);

    JPH::EMotionType motion = JPH::EMotionType::Static;
    if (desc.type == BodyType::Dynamic) {
        motion = JPH::EMotionType::Dynamic;
    } else if (desc.type == BodyType::Kinematic) {
        motion = JPH::EMotionType::Kinematic;
    }
    JPH::Ref<JPH::BoxShape> shape =
        new JPH::BoxShape(JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z), convexRadius);
    shape->SetDensity(desc.density);
    const JPH::Quat rotation =
        desc.yawDeg != 0.0f
            ? JPH::Quat::sRotation(JPH::Vec3::sAxisY(), JPH::DegreesToRadians(desc.yawDeg))
            : JPH::Quat::sIdentity();
    JPH::BodyCreationSettings settings(
        shape, JPH::RVec3(desc.position.x, desc.position.y, desc.position.z), rotation, motion,
        isStatic ? layers::kNonMoving : layers::kMoving);
    settings.mRestitution = desc.restitution;
    settings.mFriction = desc.friction;
    if (desc.lockRotation) {
        settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY |
                                JPH::EAllowedDOFs::TranslationZ;
    }

    const JPH::BodyID id = m_impl->system->GetBodyInterface().CreateAndAddBody(
        settings, isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    return {id.GetIndexAndSequenceNumber()};
}

std::optional<BodyId> PhysicsWorld::createStaticMesh(const std::vector<glm::vec3>& vertices,
                                                     const std::vector<uint32_t>& indices,
                                                     const glm::vec3& position,
                                                     const glm::quat& rotation,
                                                     const glm::vec3& scale, float friction,
                                                     float restitution) {
    // Weld coincident vertices (0.1 mm grid). Exported models duplicate a
    // vertex per face (flat shading, UV seams) and per part; Jolt only
    // finds a mesh's INACTIVE internal edges through shared indices, and an
    // active internal edge is exactly the "character trips on a flat seam"
    // ghost contact.
    struct KeyHash {
        size_t operator()(const std::array<int64_t, 3>& k) const noexcept {
            const auto h = [](int64_t v) { return std::hash<int64_t>{}(v); };
            return h(k[0]) ^ (h(k[1]) * 0x9E3779B97F4A7C15ULL) ^ (h(k[2]) * 0xC2B2AE3D27D4EB4FULL);
        }
    };
    std::unordered_map<std::array<int64_t, 3>, uint32_t, KeyHash> welded;
    welded.reserve(vertices.size());
    JPH::VertexList joltVertices;
    std::vector<uint32_t> remap(vertices.size(), 0);
    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3 p = vertices[i] * scale;
        const std::array<int64_t, 3> key{std::llround(static_cast<double>(p.x) * 1e4),
                                         std::llround(static_cast<double>(p.y) * 1e4),
                                         std::llround(static_cast<double>(p.z) * 1e4)};
        const auto [it, inserted] =
            welded.try_emplace(key, static_cast<uint32_t>(joltVertices.size()));
        if (inserted) {
            joltVertices.push_back(JPH::Float3(p.x, p.y, p.z));
        }
        remap[i] = it->second;
    }
    // A mirroring scale turns CCW triangles CW: swap two corners back.
    const bool mirrored = scale.x * scale.y * scale.z < 0.0f;
    JPH::IndexedTriangleList triangles;
    triangles.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() ||
            indices[i + 2] >= vertices.size()) {
            continue;
        }
        const uint32_t a = remap[indices[i]];
        const uint32_t b = remap[indices[i + (mirrored ? 2 : 1)]];
        const uint32_t c = remap[indices[i + (mirrored ? 1 : 2)]];
        triangles.emplace_back(a, b, c);
    }
    const size_t welds = vertices.size() - joltVertices.size();

    JPH::MeshShapeSettings meshSettings(std::move(joltVertices), std::move(triangles));
    meshSettings.SetEmbedded(); // stack-owned; only the built shape is ref-counted
    if (meshSettings.mIndexedTriangles.empty()) { // Sanitize() dropped every degenerate
        FORGE_ERROR("physics: static mesh has no usable triangles ({} indices)", indices.size());
        return std::nullopt;
    }
    const JPH::ShapeSettings::ShapeResult result = meshSettings.Create();
    if (result.HasError()) {
        FORGE_ERROR("physics: static mesh rejected: {}", result.GetError().c_str());
        return std::nullopt;
    }
    JPH::BodyCreationSettings settings(
        result.Get(), JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w).Normalized(),
        JPH::EMotionType::Static, layers::kNonMoving);
    settings.mFriction = friction;
    settings.mRestitution = restitution;
    const JPH::BodyID id = m_impl->system->GetBodyInterface().CreateAndAddBody(
        settings, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) {
        FORGE_ERROR("physics: static mesh body not created (body cap reached?)");
        return std::nullopt;
    }
    FORGE_INFO("physics: static mesh collider, {} triangles, {} vertices ({} welded)",
               meshSettings.mIndexedTriangles.size(), meshSettings.mTriangleVertices.size(), welds);
    return BodyId{id.GetIndexAndSequenceNumber()};
}

void PhysicsWorld::moveKinematic(BodyId id, const glm::vec3& targetPosition) {
    if (m_impl->system->GetBodyInterface().GetMotionType(JPH::BodyID(id.value)) ==
        JPH::EMotionType::Kinematic) {
        m_impl->kinematicTargets[id.value] = targetPosition;
    }
}

BodyType PhysicsWorld::bodyType(BodyId id) const {
    switch (m_impl->system->GetBodyInterface().GetMotionType(JPH::BodyID(id.value))) {
    case JPH::EMotionType::Dynamic:
        return BodyType::Dynamic;
    case JPH::EMotionType::Kinematic:
        return BodyType::Kinematic;
    default:
        return BodyType::Static;
    }
}

void PhysicsWorld::update(float dtSeconds) {
    constexpr float kFixedStep = 1.0f / 60.0f;
    constexpr float kMaxFrameDt = 0.1f; // debugger pause != 40 catch-up steps
    m_impl->accumulator += std::min(dtSeconds, kMaxFrameDt);
    int stepsLeft = static_cast<int>(m_impl->accumulator / kFixedStep);
    JPH::BodyInterface& bodies = m_impl->system->GetBodyInterface();
    while (m_impl->accumulator >= kFixedStep) {
        // Kinematic bodies chase their targets with a REAL velocity spread
        // over this frame's remaining ticks — that velocity is what carries
        // riders and shoves crates. Arrived bodies get zero velocity.
        for (const auto& [raw, target] : m_impl->kinematicTargets) {
            const JPH::BodyID body(raw);
            bodies.MoveKinematic(body, JPH::RVec3(target.x, target.y, target.z),
                                 bodies.GetRotation(body),
                                 kFixedStep * static_cast<float>(std::max(stepsLeft, 1)));
        }
        m_impl->system->Update(kFixedStep, 1, m_impl->tempAllocator.get(), m_impl->jobSystem.get());
        m_impl->accumulator -= kFixedStep;
        --stepsLeft;
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
    if (bodies.GetMotionType(body) != JPH::EMotionType::Static) {
        bodies.SetLinearAndAngularVelocity(body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    }
    // A teleported kinematic body forgets where it was heading.
    if (const auto it = m_impl->kinematicTargets.find(id.value);
        it != m_impl->kinematicTargets.end()) {
        it->second = position;
    }
}

void PhysicsWorld::setLinearVelocity(BodyId id, const glm::vec3& velocity) {
    m_impl->system->GetBodyInterface().SetLinearVelocity(
        JPH::BodyID(id.value), JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

std::optional<PhysicsWorld::RaycastHit> PhysicsWorld::raycast(const glm::vec3& origin,
                                                              const glm::vec3& direction,
                                                              float maxDistance) const {
    const JPH::RRayCast ray{JPH::RVec3(origin.x, origin.y, origin.z),
                            JPH::Vec3(direction.x, direction.y, direction.z) * maxDistance};
    JPH::RayCastResult hit;
    if (m_impl->system->GetNarrowPhaseQuery().CastRay(ray, hit)) {
        return RaycastHit{BodyId{hit.mBodyID.GetIndexAndSequenceNumber()},
                          hit.mFraction * maxDistance};
    }
    return std::nullopt;
}

glm::vec3 PhysicsWorld::linearVelocity(BodyId id) const {
    const JPH::Vec3 v = m_impl->system->GetBodyInterface().GetLinearVelocity(JPH::BodyID(id.value));
    return {v.GetX(), v.GetY(), v.GetZ()};
}

void PhysicsWorld::removeBody(BodyId id) {
    JPH::BodyInterface& bodies = m_impl->system->GetBodyInterface();
    const JPH::BodyID body(id.value);
    m_impl->kinematicTargets.erase(id.value);
    bodies.RemoveBody(body);
    bodies.DestroyBody(body);
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

bool PhysicsWorld::moveCharacter(const glm::vec3& horizontalVelocity, bool jump, float dtSeconds) {
    constexpr float kCoyoteSeconds = 0.1f;
    constexpr float kJumpSpeed = 5.5f;
    JPH::CharacterVirtual& character = *m_impl->character;
    const bool grounded =
        character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

    // Moving ground (kinematic platforms): inherit its velocity while we
    // stand on it, so a carousel CARRIES the player instead of sliding out
    // from under their feet.
    character.UpdateGroundVelocity();
    JPH::Vec3 ground = JPH::Vec3::sZero();
    bool jumped = false;
    if (grounded) {
        ground = character.GetGroundVelocity();
        m_impl->characterAirTime = 0.0f;
        m_impl->characterJumped = false;
        m_impl->characterVerticalVelocity = 0.0f;
    } else {
        m_impl->characterAirTime += dtSeconds;
        m_impl->characterVerticalVelocity -= 9.81f * dtSeconds;
    }
    // Coyote time: the jump still fires just after walking off an edge.
    if (jump && !m_impl->characterJumped &&
        (grounded || m_impl->characterAirTime <= kCoyoteSeconds)) {
        m_impl->characterVerticalVelocity = kJumpSpeed;
        m_impl->characterJumped = true;
        jumped = true;
    }
    const glm::vec3 push = m_impl->characterPush;
    m_impl->characterPush = glm::vec3(0.0f);
    const float wantY = m_impl->characterVerticalVelocity + push.y +
                        (jumped ? std::max(ground.GetY(), 0.0f) : ground.GetY());
    character.SetLinearVelocity(JPH::Vec3(horizontalVelocity.x + push.x + ground.GetX(), wantY,
                                          horizontalVelocity.z + push.z + ground.GetZ()));

    // ExtendedUpdate = move + slide + stick-to-floor + walk-stairs, the
    // whole character-controller kitchen in one Jolt call.
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, 0.35f, 0.0f);
    if (jumped || wantY > 0.5f) {
        // Leaving the ground on purpose: stick-to-floor would yank us back.
        updateSettings.mStickToFloorStepDown = JPH::Vec3::sZero();
    }
    character.ExtendedUpdate(dtSeconds, m_impl->system->GetGravity(), updateSettings,
                             m_impl->system->GetDefaultBroadPhaseLayerFilter(layers::kMoving),
                             m_impl->system->GetDefaultLayerFilter(layers::kMoving),
                             JPH::BodyFilter{}, JPH::ShapeFilter{}, *m_impl->tempAllocator);

    // Head-bonk fix: the solver clips our velocity against a ceiling, but the
    // integrator above kept its own upward speed — without this the player
    // hangs under a door frame until gravity eats the stale jump velocity.
    const JPH::Vec3 actual = character.GetLinearVelocity();
    if (m_impl->characterVerticalVelocity > 0.0f && actual.GetY() < wantY * 0.5f) {
        m_impl->characterVerticalVelocity = 0.0f;
    }
    m_impl->characterLastVelocity = {actual.GetX(), actual.GetY(), actual.GetZ()};
    return jumped;
}

void PhysicsWorld::pushCharacter(const glm::vec3& velocity) { m_impl->characterPush += velocity; }

glm::vec3 PhysicsWorld::characterVelocity() const { return m_impl->characterLastVelocity; }

glm::vec3 PhysicsWorld::characterPosition() const {
    const JPH::RVec3 p = m_impl->character->GetPosition();
    return {p.GetX(), p.GetY(), p.GetZ()};
}

void PhysicsWorld::setCharacterPosition(const glm::vec3& position) {
    m_impl->character->SetPosition(JPH::RVec3(position.x, position.y, position.z));
    m_impl->character->SetLinearVelocity(JPH::Vec3::sZero());
    m_impl->characterVerticalVelocity = 0.0f; // else the next frame re-applies the fall
    m_impl->characterPush = glm::vec3(0.0f);
    m_impl->characterAirTime = 0.0f;
    m_impl->characterJumped = false;
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
    const JPH::BodyInterface& bodyInterface = m_impl->system->GetBodyInterface();
    for (const JPH::BodyID& id : collector.mHits) {
        // Kinematic platforms share the moving broadphase layer but are not
        // "weight" — a lift must never press a plate or shatter glass.
        if (bodyInterface.GetMotionType(id) == JPH::EMotionType::Dynamic) {
            out.push_back({id.GetIndexAndSequenceNumber()});
        }
    }
    return out;
}

bool PhysicsWorld::characterGrounded() const {
    return m_impl->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

} // namespace forge
