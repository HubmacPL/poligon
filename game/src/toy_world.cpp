#include "toy_world.h"

namespace toy {

namespace {

// xorshift32: a small random number generator whose whole state is one number kept in a component.
std::uint32_t NextRandom(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Random float in [low, high).
float RandomRange(std::uint32_t& state, float low, float high) {
    const float unit = static_cast<float>(NextRandom(state) >> 8) / 16777216.0f;
    return low + (high - low) * unit;
}

float ClampToUnit(float value) {
    if (value > 1.0f) {
        return 1.0f;
    }
    if (value < -1.0f) {
        return -1.0f;
    }
    return value;
}

void RegisterComponents(flecs::world& world) {
    world.component<Position>().member<glm::dvec3>("value");
    world.component<Rotation>().member<glm::quat>("value");
    world.component<Velocity>().member<glm::vec3>("value");
    world.component<Box>().member<glm::vec3>("size").member<poligon::Color>("color");
    world.component<Ground>().member<float>("size").member<poligon::Color>("color");
    world.component<Body>().member<std::uint32_t>("handle");
    world.component<Falling>();
    world.component<Lifetime>().member<std::int32_t>("ticks_left");
    world.component<Spawner>()
        .member<std::uint32_t>("random_state")
        .member<std::int32_t>("ticks_until_spawn")
        .member<std::int32_t>("spawn_interval")
        .member<std::int32_t>("spawned_count");
    world.component<ToyStats>().member<std::int32_t>("expired_count");
    world.component<Player>();
    world.component<PlayerInput>()
        .member<float>("move_x")
        .member<float>("move_z")
        .member<bool>("jump_pressed")
        .member<float>("turn_camera");
    world.component<CameraRig>().member<float>("yaw_degrees");
}

// The physics world is not a component (it is not plain data); systems reach it through
// the flecs world's context pointer, set in SetUpToyWorld.
poligon::PhysicsWorld& PhysicsOf(const flecs::world& world) {
    return *static_cast<poligon::PhysicsWorld*>(world.get_ctx());
}

glm::quat TurnedAroundY(float degrees) {
    return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
}

// Systems in one phase run in the order they are created here.
void CreateSystems(flecs::world& world) {
    world.system<const Body>("PlayerControlSystem")
        .kind(flecs::OnUpdate)
        .with<Player>()
        .each([](flecs::entity entity, const Body& body) {
            const float move_speed = 4.0f;
            const float jump_speed = 6.0f;
            const flecs::world world = entity.world();
            const PlayerInput& input = world.get<PlayerInput>();
            poligon::PhysicsWorld& physics = PhysicsOf(world);
            const poligon::BodyHandle handle{body.handle};
            // The player decides the sideways speed outright; falling is left to physics.
            glm::vec3 velocity = physics.GetVelocity(handle);
            velocity.x = input.move_x * move_speed;
            velocity.z = input.move_z * move_speed;
            if (input.jump_pressed) {
                velocity.y = jump_speed;
            }
            physics.SetVelocity(handle, velocity);
        });

    // A system without components to match: runs once per tick.
    world.system("CameraSystem").kind(flecs::OnUpdate).run([](flecs::iter& it) {
        flecs::world world = it.world();
        world.get_mut<CameraRig>().yaw_degrees += world.get<PlayerInput>().turn_camera;
    });

    world.system<Spawner, const Position>("SpawnSystem")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity spawner_entity, Spawner& spawner, const Position& position) {
            --spawner.ticks_until_spawn;
            if (spawner.ticks_until_spawn > 0) {
                return;
            }
            spawner.ticks_until_spawn = spawner.spawn_interval;
            ++spawner.spawned_count;

            const float size = RandomRange(spawner.random_state, 0.3f, 0.6f);
            Velocity velocity;
            velocity.value.x = RandomRange(spawner.random_state, -2.5f, 2.5f);
            velocity.value.y = RandomRange(spawner.random_state, 4.0f, 8.0f);
            velocity.value.z = RandomRange(spawner.random_state, -2.5f, 2.5f);
            const Rotation rotation{TurnedAroundY(RandomRange(spawner.random_state, 0.0f, 90.0f))};
            Box box;
            box.size = {size, size, size};
            box.color = {RandomRange(spawner.random_state, 0.3f, 1.0f), RandomRange(spawner.random_state, 0.3f, 1.0f),
                         RandomRange(spawner.random_state, 0.3f, 1.0f), 1.0f};

            // The body is created at once; bodies are therefore always added in the same order.
            poligon::BodySettings settings;
            settings.position = position.value;
            settings.rotation = rotation.value;
            settings.velocity = velocity.value;
            settings.dynamic = true;
            settings.mass = 20.0f * size * size * size;
            settings.bounciness = 0.4f;
            const poligon::BodyHandle body = PhysicsOf(spawner_entity.world()).AddBox(box.size * 0.5f, settings);

            // Creating an entity inside a system is queued and takes effect after the system.
            spawner_entity.world()
                .entity()
                .set<Position>(position)
                .set<Rotation>(rotation)
                .set<Velocity>(velocity)
                .set<Box>(box)
                .set<Body>({body.id})
                .set<Lifetime>({240})
                .add<Falling>();
        });

    // Physics: one step for the whole world, then copy the results into the components.
    world.system("PhysicsStepSystem").kind(flecs::OnValidate).run([](flecs::iter& it) {
        PhysicsOf(it.world()).Step(it.delta_time());
    });

    world.system<const Body, Position, Rotation, Velocity>("PhysicsReadSystem")
        .kind(flecs::OnValidate)
        .with<Falling>()
        .each([](flecs::entity entity, const Body& body, Position& position, Rotation& rotation, Velocity& velocity) {
            const poligon::PhysicsWorld& physics = PhysicsOf(entity.world());
            const poligon::BodyHandle handle{body.handle};
            position.value = physics.GetPosition(handle);
            rotation.value = physics.GetRotation(handle);
            velocity.value = physics.GetVelocity(handle);
        });

    world.system<Lifetime, const Body>("LifetimeSystem")
        .kind(flecs::PostUpdate)
        .each([](flecs::entity entity, Lifetime& lifetime, const Body& body) {
            --lifetime.ticks_left;
            if (lifetime.ticks_left <= 0) {
                const flecs::world world = entity.world();
                ++world.get_mut<ToyStats>().expired_count;
                PhysicsOf(world).RemoveBody(poligon::BodyHandle{body.handle});
                entity.destruct();
            }
        });
}

// A box that stands still, resting on the ground at (x, z) from the scene's origin.
void CreateStaticBox(flecs::world& world, const glm::dvec3& origin, const char* name, float x, float z, glm::vec3 size,
                     float turn_degrees, poligon::Color color) {
    poligon::BodySettings settings;
    settings.position = origin + glm::dvec3(x, size.y * 0.5f, z);
    settings.rotation = TurnedAroundY(turn_degrees);
    const poligon::BodyHandle body = PhysicsOf(world).AddBox(size * 0.5f, settings);
    world.entity(name)
        .set<Position>({settings.position})
        .set<Rotation>({settings.rotation})
        .set<Box>({size, color})
        .set<Body>({body.id});
}

void CreateEntities(flecs::world& world, const glm::dvec3& origin) {
    poligon::PhysicsWorld& physics = PhysicsOf(world);
    world.set<ToyStats>({0});
    world.set<PlayerInput>({0.0f, 0.0f, false, 0.0f});
    world.set<CameraRig>({0.0f});

    // The ground is a slab one meter thick with its top at height 0.
    const float ground_size = 30.0f;
    poligon::BodySettings ground_settings;
    ground_settings.position = origin + glm::dvec3(0.0, -0.5, 0.0);
    const poligon::BodyHandle ground = physics.AddBox({ground_size * 0.5f, 0.5f, ground_size * 0.5f}, ground_settings);
    world.entity("ground")
        .set<Position>({origin})
        .set<Ground>({ground_size, {0.25f, 0.45f, 0.20f, 1.0f}})
        .set<Body>({ground.id});

    // The box the player steers starts high up and falls.
    poligon::BodySettings player_settings;
    player_settings.position = origin + glm::dvec3(0.0, 10.5, 0.0);
    player_settings.dynamic = true;
    player_settings.mass = 20.0f;
    player_settings.bounciness = 0.5f;
    const poligon::BodyHandle player = physics.AddBox({0.5f, 0.5f, 0.5f}, player_settings);
    world.entity("big_box")
        .set<Position>({player_settings.position})
        .set<Rotation>({player_settings.rotation})
        .set<Velocity>({{0.0f, 0.0f, 0.0f}})
        .set<Box>({{1.0f, 1.0f, 1.0f}, {0.80f, 0.15f, 0.10f, 1.0f}})
        .set<Body>({player.id})
        .add<Falling>()
        .add<Player>();

    CreateStaticBox(world, origin, "pillar_a", -3.0f, -2.0f, {1.0f, 2.0f, 1.0f}, 20.0f, {0.75f, 0.60f, 0.30f, 1.0f});
    CreateStaticBox(world, origin, "crate", 3.0f, -1.0f, {1.5f, 1.0f, 1.5f}, -30.0f, {0.30f, 0.40f, 0.75f, 1.0f});
    CreateStaticBox(world, origin, "stone", -1.5f, 3.0f, {0.8f, 0.8f, 0.8f}, 45.0f, {0.85f, 0.85f, 0.85f, 1.0f});
    CreateStaticBox(world, origin, "pillar_b", 2.0f, 3.5f, {0.6f, 3.0f, 0.6f}, 0.0f, {0.55f, 0.30f, 0.60f, 1.0f});

    world.entity("spawner").set<Position>({origin + glm::dvec3(-4.0, 0.8, 4.0)}).set<Spawner>({2463534242u, 20, 20, 0});
}

} // namespace

void SetUpToyWorld(flecs::world& world, poligon::PhysicsWorld& physics, const glm::dvec3& origin) {
    world.set_ctx(&physics);
    poligon::RegisterEngineTypes(world);
    RegisterComponents(world);
    // Systems first: they must all exist before any entity can be deleted (see docs/en/ecs.md).
    CreateSystems(world);
    CreateEntities(world, origin);
}

ToyActions SetUpToyInput(poligon::Input& input) {
    ToyActions actions;
    actions.move_x = input.AddAction("move_x");
    actions.move_z = input.AddAction("move_z");
    actions.jump = input.AddAction("jump");
    actions.look_x = input.AddAction("look_x");
    actions.look_hold = input.AddAction("look_hold");

    input.BindKey(actions.move_x, poligon::Key::D, 1.0f);
    input.BindKey(actions.move_x, poligon::Key::A, -1.0f);
    input.BindKey(actions.move_x, poligon::Key::Right, 1.0f);
    input.BindKey(actions.move_x, poligon::Key::Left, -1.0f);
    input.BindKey(actions.move_z, poligon::Key::S, 1.0f);
    input.BindKey(actions.move_z, poligon::Key::W, -1.0f);
    input.BindKey(actions.move_z, poligon::Key::Down, 1.0f);
    input.BindKey(actions.move_z, poligon::Key::Up, -1.0f);
    input.BindKey(actions.jump, poligon::Key::Space);
    input.BindMouseMotion(actions.look_x, poligon::MouseAxis::X);
    input.BindMouseButton(actions.look_hold, poligon::MouseButton::Right);
    return actions;
}

void UpdatePlayerInput(flecs::world& world, const poligon::Input& input, const ToyActions& actions) {
    const float degrees_per_pixel = 0.25f;

    PlayerInput player_input;
    // Two keys for the same direction (D and Right) add up to 2, so limit the result.
    player_input.move_x = ClampToUnit(input.GetValue(actions.move_x));
    player_input.move_z = ClampToUnit(input.GetValue(actions.move_z));
    player_input.jump_pressed = input.WasPressed(actions.jump);
    player_input.turn_camera =
        input.IsDown(actions.look_hold) ? input.GetValue(actions.look_x) * degrees_per_pixel : 0.0f;
    world.set<PlayerInput>(player_input);
}

} // namespace toy
