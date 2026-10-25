#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "poligon/core/color.h"
#include "poligon/core/image.h"
#include "poligon/core/math.h"
#include "poligon/render/particles.h"

namespace poligon {

// Particles that move: an emitter makes them in a box, moves them every tick and lets them die.
// Rain and snow (a box over the player that follows them), dust in the light, an animal's
// breath, flies, falling leaves. The simulation is the game's: call Update in FixedUpdate
// with the tick's length; the same seed and the same calls give the same particles to the
// bit (its random numbers are its own, not the C++ library's). The renderer only draws them:
// Renderer::DrawParticles(texture, emitter.GetParticles().data(), count, options).

struct ParticleEmitterSettings {
    // Particles started per second (spread evenly; the fraction carries to the next tick).
    float rate = 50.0f;
    // At most this many live at once; beyond it no new ones start.
    int max_particles = 1000;
    // Seconds a particle lives, varying by up to this share either way.
    float lifetime = 2.0f;
    float lifetime_variation = 0.3f;
    // Particles start anywhere in this box (full size in meters) around the emitter's position.
    glm::vec3 box_size{0.0f};
    // Starting velocity in meters per second, and how much each axis varies either way.
    glm::vec3 velocity{0.0f};
    glm::vec3 velocity_variation{0.0f};
    // Added to the velocity every second (gravity, wind), and the share of the velocity lost
    // per second (air drag: 0 none, 1 about two thirds gone after a second).
    glm::vec3 acceleration{0.0f};
    float drag = 0.0f;
    // Size and color over the particle's life, from start to end (linear colors; alpha fades).
    float start_size = 0.1f;
    float end_size = 0.1f;
    Color start_color{1.0f, 1.0f, 1.0f, 1.0f};
    Color end_color{1.0f, 1.0f, 1.0f, 0.0f};
    // Turning in radians per second, varying by up to this much either way; the start angle
    // is random.
    float rotation_speed = 0.0f;
    float rotation_speed_variation = 0.0f;
    // Pictures of the texture's atlas: each particle shows one picked at random, or, with
    // frames_per_second above 0, walks through them from a random one.
    int atlas_frames = 1;
    float frames_per_second = 0.0f;
};

class ParticleEmitter {
public:
    explicit ParticleEmitter(const ParticleEmitterSettings& settings = ParticleEmitterSettings{}, std::uint64_t seed = 1);

    // Where the box is (its middle), in the world. Moving it moves where new particles start,
    // not live ones.
    void SetPosition(const glm::dvec3& position);
    const glm::dvec3& GetPosition() const;
    // Particles are kept in float relative to this point (see poligon/core/math.h): draw them
    // with Renderer::DrawParticles(texture, GetOrigin(), ...). It follows the box: when the box
    // has gone more than 1 km from it, the origin moves to the box and every particle is
    // moved by the same step, so floats stay small wherever the emitter travels.
    const glm::dvec3& GetOrigin() const;
    // Settings can change at any time (stronger rain); live particles keep how they started.
    void SetSettings(const ParticleEmitterSettings& settings);
    const ParticleEmitterSettings& GetSettings() const;
    // false stops new particles; live ones finish their lives.
    void SetEmitting(bool emitting);

    // Moves every particle by `delta_seconds`, removes the dead, starts new ones.
    void Update(float delta_seconds);
    // Starts `count` particles at once (a puff of breath, a burst of leaves).
    void Burst(int count);

    // The live particles relative to GetOrigin(), ready for Renderer::DrawParticles.
    const std::vector<Particle>& GetParticles() const;
    int GetParticleCount() const;

    // The emitter's whole state (random generator, carried fraction, every particle with its
    // age) as JSON with exact numbers, like the ECS state dump; two emitters in the same state
    // give the same text. And its 64-bit FNV-1a hash.
    std::string DumpStateToJson() const;
    std::uint64_t HashState() const;

private:
    struct Life {
        float age = 0.0f;
        float lifetime = 1.0f;
        float rotation_speed = 0.0f;
        float first_frame = 0.0f;
    };
    float Random();          // 0 <= value < 1
    float RandomSigned();    // -1 <= value < 1
    void Start();

    ParticleEmitterSettings settings_;
    glm::dvec3 position_{0.0};
    glm::dvec3 origin_{0.0};
    bool emitting_ = true;
    std::uint64_t random_state_ = 0;
    float carried_ = 0.0f;
    std::vector<Particle> particles_;
    std::vector<Life> lives_;
};

// Ready settings for weather around the camera (WeatherSettings in render_settings.h). Move the
// emitter with the camera every tick: SetPosition(camera position) for both.
// Rain: drops falling at about 7 m/s through a box 30 m wide reaching 8 m above the camera,
// slanted by the wind (meters per second along x and z); draw them with ParticleOptions
// stretch 0.03 (streaks) and no texture. intensity 1: about 4000 drops alive.
ParticleEmitterSettings MakeRainSettings(float intensity, glm::vec2 wind);
// Dust: small motes drifting slowly in a box 24 x 8 x 24 m around the camera, fading in and
// out; drawn lit (ParticleLighting::Simple) they show where the sun shines and vanish in
// shade, as in shafts of light. intensity 1: about 2500 motes alive. Texture: MakeParticleDotImage.
ParticleEmitterSettings MakeDustSettings(float intensity);
// A round white dot fading to its edge (alpha), `size` x `size` pixels: a picture for dust,
// snow or sparks, whose color comes from the particles.
Image MakeParticleDotImage(int size);

} // namespace poligon
