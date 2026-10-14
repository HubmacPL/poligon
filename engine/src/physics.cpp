#include "poligon/physics.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Jolt.h must come first; it sets up what the other Jolt headers rely on.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "poligon/fatal_error.h"

namespace poligon {

namespace {

// Bodies are sorted into two groups: those that never move and those that do. Things that
// never move do not need to be tested against each other.
namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kCount = 2;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kCount = 2;
} // namespace BroadPhaseLayers

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::kStatic ? BroadPhaseLayers::kStatic : BroadPhaseLayers::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::kStatic ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer other) const override {
        return layer == Layers::kMoving || other == BroadPhaseLayers::kMoving;
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::kMoving || b == Layers::kMoving;
    }
};

void TraceToStdout(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    std::printf("Jolt: %s\n", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool AssertFailed(const char* expression, const char* message, const char* file, JPH::uint line) {
    const std::string text = std::string("Jolt assertion failed: ") + expression + " (" + (message != nullptr ? message : "") +
                             ") at " + file + ":" + std::to_string(line);
    FatalError(text.c_str());
}
#endif

// Jolt keeps a few process-wide tables. They are set up with the first PhysicsWorld and
// taken down with the last one.
int g_world_count = 0;

void AcquireJolt() {
    if (g_world_count++ > 0) {
        return;
    }
    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceToStdout;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = AssertFailed;
#endif
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

void ReleaseJolt() {
    if (--g_world_count > 0) {
        return;
    }
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

JPH::Vec3 ToJolt(const glm::vec3& v) {
    return JPH::Vec3(v.x, v.y, v.z);
}

JPH::Quat ToJolt(const glm::quat& q) {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

glm::vec3 ToGlm(JPH::Vec3Arg v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}

glm::quat ToGlm(JPH::QuatArg q) {
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

void HashBytes(std::uint64_t& hash, const void* data, size_t size) {
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;  // FNV-1a
    }
}

void HashVector(std::uint64_t& hash, const glm::vec3& v) {
    HashBytes(hash, &v.x, 3 * sizeof(float));
}

struct Character {
    JPH::Ref<JPH::CharacterVirtual> character;
    glm::vec3 walk_velocity{0.0f};
    bool jump = false;
    float step_height = 0.4f;
    float jump_speed = 4.5f;
};

} // namespace

struct PhysicsWorld::Impl {
    PhysicsConfig config;
    BroadPhaseLayerInterface broad_phase_layers;
    ObjectVsBroadPhaseLayerFilter object_vs_broad_phase;
    ObjectLayerPairFilter object_pairs;
    std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
    // One thread: results do not depend on which thread finishes first, and callbacks
    // arrive in a fixed order.
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
    std::unique_ptr<JPH::PhysicsSystem> system;

    // Indexed by handle id - 1. Removed entries stay, marked invalid, so that handle
    // numbers are never reused.
    std::vector<JPH::BodyID> bodies;
    std::vector<Character> characters;
    int body_count = 0;

    JPH::BodyID GetBody(BodyHandle handle, const char* caller) const {
        if (handle.id == 0 || handle.id > bodies.size() || bodies[handle.id - 1].IsInvalid()) {
            const std::string message = std::string("PhysicsWorld::") + caller + ": invalid or removed body handle";
            FatalError(message.c_str());
        }
        return bodies[handle.id - 1];
    }

    Character& GetCharacter(CharacterHandle handle, const char* caller) {
        if (handle.id == 0 || handle.id > characters.size() || characters[handle.id - 1].character == nullptr) {
            const std::string message = std::string("PhysicsWorld::") + caller + ": invalid or removed character handle";
            FatalError(message.c_str());
        }
        return characters[handle.id - 1];
    }

    BodyHandle AddBody(const JPH::ShapeSettings& shape_settings, const BodySettings& settings, const char* caller) {
        const JPH::ShapeSettings::ShapeResult result = shape_settings.Create();
        if (result.HasError()) {
            const std::string message = std::string("PhysicsWorld::") + caller + ": " + result.GetError().c_str();
            FatalError(message.c_str());
        }
        JPH::BodyCreationSettings creation(result.Get(), ToJolt(settings.position), ToJolt(glm::normalize(settings.rotation)),
                                           settings.dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                                           settings.dynamic ? Layers::kMoving : Layers::kStatic);
        creation.mFriction = settings.friction;
        creation.mRestitution = settings.bounciness;
        if (settings.dynamic) {
            if (settings.mass <= 0.0f) {
                FatalError("PhysicsWorld: a dynamic body needs a mass above zero");
            }
            // Take the given mass and work out from the shape how hard the body is to spin.
            creation.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            creation.mMassPropertiesOverride.mMass = settings.mass;
            creation.mLinearVelocity = ToJolt(settings.velocity);
        }
        const JPH::BodyID id = system->GetBodyInterface().CreateAndAddBody(
            creation, settings.dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (id.IsInvalid()) {
            FatalError("PhysicsWorld: the world is full (see PhysicsConfig::max_bodies)");
        }
        bodies.push_back(id);
        ++body_count;
        BodyHandle handle;
        handle.id = static_cast<std::uint32_t>(bodies.size());
        return handle;
    }
};

PhysicsWorld::PhysicsWorld(const PhysicsConfig& config) : impl_(std::make_unique<Impl>()) {
    if (config.max_bodies < 1) {
        FatalError("PhysicsWorld: max_bodies must be at least 1");
    }
    AcquireJolt();
    impl_->config = config;
    impl_->temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    impl_->jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
    impl_->system = std::make_unique<JPH::PhysicsSystem>();
    const JPH::uint max_bodies = static_cast<JPH::uint>(config.max_bodies);
    impl_->system->Init(max_bodies, 0, max_bodies * 4, max_bodies * 2, impl_->broad_phase_layers,
                        impl_->object_vs_broad_phase, impl_->object_pairs);
    impl_->system->SetGravity(ToJolt(config.gravity));
}

PhysicsWorld::~PhysicsWorld() {
    impl_->characters.clear();
    JPH::BodyInterface& bodies = impl_->system->GetBodyInterface();
    for (const JPH::BodyID id : impl_->bodies) {
        if (!id.IsInvalid()) {
            bodies.RemoveBody(id);
            bodies.DestroyBody(id);
        }
    }
    impl_->system.reset();
    impl_->jobs.reset();
    impl_->temp_allocator.reset();
    ReleaseJolt();
}

BodyHandle PhysicsWorld::AddBox(const glm::vec3& half_size, const BodySettings& settings) {
    if (half_size.x <= 0.0f || half_size.y <= 0.0f || half_size.z <= 0.0f) {
        FatalError("PhysicsWorld::AddBox: every half size must be above zero");
    }
    // Jolt rounds the edges of a box slightly; the rounding must fit inside the box.
    const float smallest = glm::min(half_size.x, glm::min(half_size.y, half_size.z));
    const JPH::BoxShapeSettings shape(ToJolt(half_size), glm::min(JPH::cDefaultConvexRadius, smallest * 0.5f));
    return impl_->AddBody(shape, settings, "AddBox");
}

BodyHandle PhysicsWorld::AddSphere(float radius, const BodySettings& settings) {
    if (radius <= 0.0f) {
        FatalError("PhysicsWorld::AddSphere: the radius must be above zero");
    }
    const JPH::SphereShapeSettings shape(radius);
    return impl_->AddBody(shape, settings, "AddSphere");
}

BodyHandle PhysicsWorld::AddCylinder(float half_height, float radius, const BodySettings& settings) {
    if (half_height <= 0.0f || radius <= 0.0f) {
        FatalError("PhysicsWorld::AddCylinder: half height and radius must be above zero");
    }
    const float smallest = glm::min(half_height, radius);
    const JPH::CylinderShapeSettings shape(half_height, radius, glm::min(JPH::cDefaultConvexRadius, smallest * 0.5f));
    return impl_->AddBody(shape, settings, "AddCylinder");
}

BodyHandle PhysicsWorld::AddCapsule(float half_height, float radius, const BodySettings& settings) {
    if (half_height <= 0.0f || radius <= 0.0f) {
        FatalError("PhysicsWorld::AddCapsule: half height and radius must be above zero");
    }
    const JPH::CapsuleShapeSettings shape(half_height, radius);
    return impl_->AddBody(shape, settings, "AddCapsule");
}

BodyHandle PhysicsWorld::AddMesh(const glm::vec3* vertices, int vertex_count, const std::uint32_t* indices, int index_count,
                                 const BodySettings& settings) {
    if (vertices == nullptr || indices == nullptr || vertex_count < 3 || index_count < 3 || index_count % 3 != 0) {
        FatalError("PhysicsWorld::AddMesh: needs at least one triangle and an index count divisible by 3");
    }
    if (settings.dynamic) {
        FatalError("PhysicsWorld::AddMesh: a triangle mesh cannot be dynamic; use boxes, spheres, cylinders or capsules");
    }
    JPH::VertexList vertex_list;
    vertex_list.reserve(static_cast<size_t>(vertex_count));
    for (int i = 0; i < vertex_count; ++i) {
        vertex_list.push_back(JPH::Float3(vertices[i].x, vertices[i].y, vertices[i].z));
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(static_cast<size_t>(index_count / 3));
    for (int i = 0; i < index_count; i += 3) {
        if (indices[i] >= static_cast<std::uint32_t>(vertex_count) || indices[i + 1] >= static_cast<std::uint32_t>(vertex_count) ||
            indices[i + 2] >= static_cast<std::uint32_t>(vertex_count)) {
            FatalError("PhysicsWorld::AddMesh: an index points past the last vertex");
        }
        triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));
    }
    const JPH::MeshShapeSettings shape(std::move(vertex_list), std::move(triangles));
    return impl_->AddBody(shape, settings, "AddMesh");
}

void PhysicsWorld::RemoveBody(BodyHandle body) {
    const JPH::BodyID id = impl_->GetBody(body, "RemoveBody");
    JPH::BodyInterface& bodies = impl_->system->GetBodyInterface();
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
    impl_->bodies[body.id - 1] = JPH::BodyID();
    --impl_->body_count;
}

int PhysicsWorld::GetBodyCount() const {
    return impl_->body_count;
}

std::uint32_t PhysicsWorld::GetLastBodyId() const {
    return static_cast<std::uint32_t>(impl_->bodies.size());
}

bool PhysicsWorld::IsBody(BodyHandle body) const {
    return body.id != 0 && body.id <= impl_->bodies.size() && !impl_->bodies[body.id - 1].IsInvalid();
}

bool PhysicsWorld::IsDynamic(BodyHandle body) const {
    return impl_->system->GetBodyInterface().GetMotionType(impl_->GetBody(body, "IsDynamic")) == JPH::EMotionType::Dynamic;
}

void PhysicsWorld::GetBounds(BodyHandle body, glm::vec3& bounds_min, glm::vec3& bounds_max) const {
    const JPH::AABox box =
        impl_->system->GetBodyInterface().GetTransformedShape(impl_->GetBody(body, "GetBounds")).GetWorldSpaceBounds();
    bounds_min = ToGlm(box.mMin);
    bounds_max = ToGlm(box.mMax);
}

glm::vec3 PhysicsWorld::GetPosition(BodyHandle body) const {
    return ToGlm(impl_->system->GetBodyInterface().GetPosition(impl_->GetBody(body, "GetPosition")));
}

glm::quat PhysicsWorld::GetRotation(BodyHandle body) const {
    return ToGlm(impl_->system->GetBodyInterface().GetRotation(impl_->GetBody(body, "GetRotation")));
}

glm::vec3 PhysicsWorld::GetVelocity(BodyHandle body) const {
    return ToGlm(impl_->system->GetBodyInterface().GetLinearVelocity(impl_->GetBody(body, "GetVelocity")));
}

glm::vec3 PhysicsWorld::GetAngularVelocity(BodyHandle body) const {
    return ToGlm(impl_->system->GetBodyInterface().GetAngularVelocity(impl_->GetBody(body, "GetAngularVelocity")));
}

glm::mat4 PhysicsWorld::GetTransform(BodyHandle body) const {
    const JPH::BodyID id = impl_->GetBody(body, "GetTransform");
    JPH::Vec3 position;
    JPH::Quat rotation;
    impl_->system->GetBodyInterface().GetPositionAndRotation(id, position, rotation);
    glm::mat4 transform = glm::mat4_cast(ToGlm(rotation));
    transform[3] = glm::vec4(ToGlm(position), 1.0f);
    return transform;
}

bool PhysicsWorld::IsAsleep(BodyHandle body) const {
    return !impl_->system->GetBodyInterface().IsActive(impl_->GetBody(body, "IsAsleep"));
}

void PhysicsWorld::SetPosition(BodyHandle body, const glm::vec3& position, const glm::quat& rotation) {
    impl_->system->GetBodyInterface().SetPositionAndRotation(impl_->GetBody(body, "SetPosition"), ToJolt(position),
                                                            ToJolt(glm::normalize(rotation)), JPH::EActivation::Activate);
}

void PhysicsWorld::SetVelocity(BodyHandle body, const glm::vec3& velocity) {
    impl_->system->GetBodyInterface().SetLinearVelocity(impl_->GetBody(body, "SetVelocity"), ToJolt(velocity));
}

void PhysicsWorld::AddImpulse(BodyHandle body, const glm::vec3& impulse) {
    impl_->system->GetBodyInterface().AddImpulse(impl_->GetBody(body, "AddImpulse"), ToJolt(impulse));
}

CharacterHandle PhysicsWorld::AddCharacter(const CharacterSettings& settings) {
    if (settings.radius <= 0.0f || settings.height <= settings.radius * 2.0f) {
        FatalError("PhysicsWorld::AddCharacter: the height must be more than twice the radius");
    }
    // A capsule whose lowest point is the character's position (its feet).
    const float half_height = settings.height * 0.5f - settings.radius;
    const JPH::RotatedTranslatedShapeSettings shape(JPH::Vec3(0.0f, half_height + settings.radius, 0.0f),
                                                    JPH::Quat::sIdentity(),
                                                    new JPH::CapsuleShape(half_height, settings.radius));
    const JPH::ShapeSettings::ShapeResult result = shape.Create();
    if (result.HasError()) {
        FatalError("PhysicsWorld::AddCharacter: cannot create the character's shape");
    }
    JPH::Ref<JPH::CharacterVirtualSettings> character_settings = new JPH::CharacterVirtualSettings();
    character_settings->mShape = result.Get();
    character_settings->mMaxSlopeAngle = glm::radians(settings.max_slope_degrees);
    // Only what touches the rounded bottom of the capsule counts as ground to stand on.
    character_settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -settings.radius);

    Character character;
    character.character = new JPH::CharacterVirtual(character_settings, ToJolt(settings.position), JPH::Quat::sIdentity(), 0,
                                                    impl_->system.get());
    character.step_height = settings.step_height;
    character.jump_speed = settings.jump_speed;
    impl_->characters.push_back(std::move(character));
    CharacterHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->characters.size());
    return handle;
}

void PhysicsWorld::RemoveCharacter(CharacterHandle character) {
    impl_->GetCharacter(character, "RemoveCharacter").character = nullptr;
}

void PhysicsWorld::SetCharacterInput(CharacterHandle character, const glm::vec3& walk_velocity, bool jump) {
    Character& entry = impl_->GetCharacter(character, "SetCharacterInput");
    entry.walk_velocity = {walk_velocity.x, 0.0f, walk_velocity.z};
    entry.jump = jump;
}

glm::vec3 PhysicsWorld::GetCharacterPosition(CharacterHandle character) const {
    return ToGlm(impl_->GetCharacter(character, "GetCharacterPosition").character->GetPosition());
}

glm::vec3 PhysicsWorld::GetCharacterVelocity(CharacterHandle character) const {
    return ToGlm(impl_->GetCharacter(character, "GetCharacterVelocity").character->GetLinearVelocity());
}

bool PhysicsWorld::IsCharacterOnGround(CharacterHandle character) const {
    return impl_->GetCharacter(character, "IsCharacterOnGround").character->GetGroundState() ==
           JPH::CharacterVirtual::EGroundState::OnGround;
}

void PhysicsWorld::SetCharacterPosition(CharacterHandle character, const glm::vec3& position) {
    impl_->GetCharacter(character, "SetCharacterPosition").character->SetPosition(ToJolt(position));
}

void PhysicsWorld::Step(float delta_seconds) {
    if (delta_seconds <= 0.0f) {
        FatalError("PhysicsWorld::Step: the step length must be above zero");
    }
    JPH::PhysicsSystem& system = *impl_->system;
    const JPH::Vec3 gravity = system.GetGravity();

    // Characters first, in the order they were added, then the bodies.
    for (Character& entry : impl_->characters) {
        if (entry.character == nullptr) {
            continue;
        }
        JPH::CharacterVirtual& character = *entry.character;
        // Keep the vertical speed the character has (falling, rising in a jump); standing
        // on something, take that thing's vertical speed instead.
        character.UpdateGroundVelocity();
        const bool on_ground = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        float vertical = character.GetLinearVelocity().GetY();
        if (on_ground) {
            vertical = character.GetGroundVelocity().GetY();
            if (entry.jump) {
                vertical += entry.jump_speed;
            }
        }
        // Standing on something, gravity is left out: it would make the character creep
        // down every slope. Walking downhill, the "stick to floor" step keeps it on the ground.
        if (!on_ground || entry.jump) {
            vertical += gravity.GetY() * delta_seconds;
        }
        character.SetLinearVelocity(JPH::Vec3(entry.walk_velocity.x, vertical, entry.walk_velocity.z));

        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        update.mWalkStairsStepUp = JPH::Vec3(0.0f, entry.step_height, 0.0f);
        update.mStickToFloorStepDown = JPH::Vec3(0.0f, -entry.step_height, 0.0f);
        character.ExtendedUpdate(delta_seconds, gravity, update, system.GetDefaultBroadPhaseLayerFilter(Layers::kMoving),
                                 system.GetDefaultLayerFilter(Layers::kMoving), JPH::BodyFilter(), JPH::ShapeFilter(),
                                 *impl_->temp_allocator);
        entry.jump = false;
    }

    const JPH::EPhysicsUpdateError error = system.Update(delta_seconds, 1, impl_->temp_allocator.get(), impl_->jobs.get());
    if (error != JPH::EPhysicsUpdateError::None) {
        FatalError("PhysicsWorld::Step: the world ran out of room for contacts (too many bodies touching at once)");
    }
}

RayHit PhysicsWorld::CastRay(const glm::vec3& origin, const glm::vec3& direction, float max_distance) const {
    RayHit hit;
    const float length = glm::length(direction);
    if (length <= 0.0f || max_distance <= 0.0f) {
        return hit;
    }
    const glm::vec3 unit = direction / length;
    const JPH::RRayCast ray(ToJolt(origin), ToJolt(unit * max_distance));
    JPH::RayCastResult result;
    if (!impl_->system->GetNarrowPhaseQuery().CastRay(ray, result)) {
        return hit;
    }
    hit.hit = true;
    hit.distance = result.mFraction * max_distance;
    hit.position = origin + unit * hit.distance;
    {
        const JPH::BodyLockRead lock(impl_->system->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded()) {
            hit.normal = ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ToJolt(hit.position)));
        }
    }
    for (size_t i = 0; i < impl_->bodies.size(); ++i) {
        if (impl_->bodies[i] == result.mBodyID) {
            hit.body.id = static_cast<std::uint32_t>(i + 1);
            break;
        }
    }
    return hit;
}

std::uint64_t PhysicsWorld::HashState() const {
    std::uint64_t hash = 14695981039346656037ull;
    const JPH::BodyInterface& bodies = impl_->system->GetBodyInterface();
    for (const JPH::BodyID id : impl_->bodies) {
        if (id.IsInvalid()) {
            continue;
        }
        JPH::Vec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(id, position, rotation);
        HashVector(hash, ToGlm(position));
        const glm::quat q = ToGlm(rotation);
        HashBytes(hash, &q, sizeof(q));
        HashVector(hash, ToGlm(bodies.GetLinearVelocity(id)));
        HashVector(hash, ToGlm(bodies.GetAngularVelocity(id)));
    }
    for (const Character& entry : impl_->characters) {
        if (entry.character != nullptr) {
            HashVector(hash, ToGlm(entry.character->GetPosition()));
            HashVector(hash, ToGlm(entry.character->GetLinearVelocity()));
        }
    }
    return hash;
}

} // namespace poligon
