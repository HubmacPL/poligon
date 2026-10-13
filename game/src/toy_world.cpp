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
    world.component<Position>().member<glm::vec3>("value");
    world.component<Velocity>().member<glm::vec3>("value");
    world.component<Box>()
        .member<glm::vec3>("size")
        .member<float>("rotation_y_degrees")
        .member<poligon::Color>("color");
    world.component<Ground>().member<float>("size").member<poligon::Color>("color");
    world.component<Falling>();
    world.component<Bouncy>().member<float>("bounce_factor").member<std::int32_t>("bounce_count");
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

// Systems in one phase run in the order they are created here.
void CreateSystems(flecs::world& world) {
    world.system<Velocity>("PlayerControlSystem")
        .kind(flecs::OnUpdate)
        .with<Player>()
        .each([](flecs::entity entity, Velocity& velocity) {
            const float move_speed = 4.0f;
            const float jump_speed = 6.0f;
            const PlayerInput& input = entity.world().get<PlayerInput>();
            velocity.value.x = input.move_x * move_speed;
            velocity.value.z = input.move_z * move_speed;
            if (input.jump_pressed) {
                velocity.value.y = jump_speed;
            }
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
            Box box;
            box.size = {size, size, size};
            box.rotation_y_degrees = RandomRange(spawner.random_state, 0.0f, 90.0f);
            box.color = {RandomRange(spawner.random_state, 0.3f, 1.0f), RandomRange(spawner.random_state, 0.3f, 1.0f),
                         RandomRange(spawner.random_state, 0.3f, 1.0f), 1.0f};

            // Creating an entity inside a system is queued and takes effect after the system.
            spawner_entity.world()
                .entity()
                .set<Position>(position)
                .set<Velocity>(velocity)
                .set<Box>(box)
                .set<Bouncy>({0.6f, 0})
                .set<Lifetime>({240})
                .add<Falling>();
        });

    world.system<Velocity>("GravitySystem")
        .kind(flecs::OnUpdate)
        .with<Falling>()
        .each([](flecs::iter& it, size_t, Velocity& velocity) { velocity.value.y += -9.81f * it.delta_time(); });

    world.system<Position, const Velocity>("MoveSystem")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t, Position& position, const Velocity& velocity) {
            position.value += velocity.value * it.delta_time();
        });

    world.system<Position, Velocity, Bouncy>("BounceSystem")
        .kind(flecs::OnUpdate)
        .each([](Position& position, Velocity& velocity, Bouncy& bouncy) {
            if (position.value.y < 0.0f) {
                position.value.y = -position.value.y;
                velocity.value.y = -velocity.value.y * bouncy.bounce_factor;
                ++bouncy.bounce_count;
            }
        });

    world.system<Lifetime>("LifetimeSystem")
        .kind(flecs::PostUpdate)
        .each([](flecs::entity entity, Lifetime& lifetime) {
            --lifetime.ticks_left;
            if (lifetime.ticks_left <= 0) {
                ++entity.world().get_mut<ToyStats>().expired_count;
                entity.destruct();
            }
        });
}

void CreateEntities(flecs::world& world) {
    world.set<ToyStats>({0});
    world.set<PlayerInput>({0.0f, 0.0f, false, 0.0f});
    world.set<CameraRig>({0.0f});

    world.entity("ground").set<Ground>({30.0f, {0.25f, 0.45f, 0.20f, 1.0f}});

    world.entity("big_box")
        .set<Position>({{0.0f, 10.0f, 0.0f}})
        .set<Velocity>({{0.0f, 0.0f, 0.0f}})
        .set<Box>({{1.0f, 1.0f, 1.0f}, 0.0f, {0.80f, 0.15f, 0.10f, 1.0f}})
        .set<Bouncy>({0.8f, 0})
        .add<Falling>()
        .add<Player>();

    world.entity("pillar_a")
        .set<Position>({{-3.0f, 0.0f, -2.0f}})
        .set<Box>({{1.0f, 2.0f, 1.0f}, 20.0f, {0.75f, 0.60f, 0.30f, 1.0f}});
    world.entity("crate")
        .set<Position>({{3.0f, 0.0f, -1.0f}})
        .set<Box>({{1.5f, 1.0f, 1.5f}, -30.0f, {0.30f, 0.40f, 0.75f, 1.0f}});
    world.entity("stone")
        .set<Position>({{-1.5f, 0.0f, 3.0f}})
        .set<Box>({{0.8f, 0.8f, 0.8f}, 45.0f, {0.85f, 0.85f, 0.85f, 1.0f}});
    world.entity("pillar_b")
        .set<Position>({{2.0f, 0.0f, 3.5f}})
        .set<Box>({{0.6f, 3.0f, 0.6f}, 0.0f, {0.55f, 0.30f, 0.60f, 1.0f}});

    world.entity("spawner").set<Position>({{-4.0f, 0.5f, 4.0f}}).set<Spawner>({2463534242u, 20, 20, 0});
}

} // namespace

void SetUpToyWorld(flecs::world& world) {
    poligon::RegisterEngineTypes(world);
    RegisterComponents(world);
    // Systems first: they must all exist before any entity can be deleted (see docs/ecs.md).
    CreateSystems(world);
    CreateEntities(world);
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
