// The particle emitter (poligon/world/particles.h): the same seed and the same calls give the
// same state to the bit; another seed gives another; particles live their lifetime, move as
// their settings say and never outnumber max_particles.
#include <cmath>
#include <cstdio>
#include <string>

#include "poligon/world/particles.h"
#include "test_support.h"

int main() {
    using test::Expect;

    poligon::ParticleEmitterSettings rain;
    rain.rate = 600.0f;
    rain.max_particles = 500;
    rain.lifetime = 1.0f;
    rain.lifetime_variation = 0.2f;
    rain.box_size = {20.0f, 2.0f, 20.0f};
    rain.velocity = {1.0f, -8.0f, 0.0f};
    rain.velocity_variation = {0.3f, 0.5f, 0.3f};
    rain.acceleration = {0.0f, -9.81f, 0.0f};
    rain.drag = 0.2f;
    rain.rotation_speed = 1.0f;
    rain.rotation_speed_variation = 0.5f;
    rain.atlas_frames = 4;
    rain.frames_per_second = 6.0f;

    const auto run = [&rain](std::uint64_t seed, int ticks) {
        poligon::ParticleEmitter emitter(rain, seed);
        emitter.SetPosition({0.0f, 10.0f, 0.0f});
        for (int tick = 0; tick < ticks; ++tick) {
            if (tick == 30) {
                emitter.Burst(40);
            }
            if (tick == 60) {
                emitter.SetPosition({5.0f, 10.0f, -3.0f});  // the box follows the player
            }
            emitter.Update(1.0f / 60.0f);
        }
        return emitter;
    };
    const poligon::ParticleEmitter first = run(7, 180);
    const poligon::ParticleEmitter second = run(7, 180);
    const poligon::ParticleEmitter other_seed = run(8, 180);
    std::printf("  after 180 ticks: %d particles, hash %016llx\n", first.GetParticleCount(),
                static_cast<unsigned long long>(first.HashState()));
    Expect(first.DumpStateToJson() == second.DumpStateToJson() && first.HashState() == second.HashState(),
           "the same seed and calls give the same state to the bit");
    Expect(first.HashState() != other_seed.HashState(), "another seed gives another state");
    Expect(first.GetParticleCount() > 0 && first.GetParticleCount() <= rain.max_particles, "never more than max_particles");

    // One particle, no randomness in its motion: it falls as the settings say and dies on time.
    poligon::ParticleEmitterSettings one;
    one.rate = 0.0f;
    one.lifetime = 1.0f;
    one.lifetime_variation = 0.0f;
    one.velocity = {2.0f, 0.0f, 0.0f};
    one.acceleration = {0.0f, -10.0f, 0.0f};
    one.start_size = 0.1f;
    one.end_size = 0.3f;
    poligon::ParticleEmitter single(one, 1);
    single.Burst(1);
    for (int tick = 0; tick < 30; ++tick) {
        single.Update(1.0f / 60.0f);
    }
    const poligon::Particle& particle = single.GetParticles().front();
    // Half a second of steps of 1/60 s (velocity first, then position): x = 1, y = -10 * 0.5 * 0.5 * 31/30.
    Expect(single.GetParticleCount() == 1 && std::abs(particle.position.x - 1.0f) < 1e-4f &&
               std::abs(particle.position.y - (-1.25f * 31.0f / 30.0f)) < 1e-3f && std::abs(particle.size - 0.2f) < 1e-4f,
           "a particle moves and grows as its settings say");
    for (int tick = 0; tick < 31; ++tick) {
        single.Update(1.0f / 60.0f);
    }
    Expect(single.GetParticleCount() == 0, "and dies at the end of its lifetime");

    std::printf("particles_test finished\n");
    return test::ExitCode();
}
