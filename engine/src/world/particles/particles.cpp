#include "poligon/world/particles.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>

namespace poligon {

namespace {

// The ECS state dump's way of writing numbers: 9 significant digits tell any two floats apart.
void AppendFloat(std::string& text, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    text += buffer;
}

void AppendVec3(std::string& text, const glm::vec3& value) {
    text += '[';
    AppendFloat(text, value.x);
    text += ',';
    AppendFloat(text, value.y);
    text += ',';
    AppendFloat(text, value.z);
    text += ']';
}

// Doubles with 17 significant digits, as the ECS state dump writes them.
void AppendDvec3(std::string& text, const glm::dvec3& value) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "[%.17g,%.17g,%.17g]", value.x, value.y, value.z);
    text += buffer;
}

} // namespace

ParticleEmitter::ParticleEmitter(const ParticleEmitterSettings& settings, std::uint64_t seed) : settings_(settings), random_state_(seed) {}

void ParticleEmitter::SetPosition(const glm::dvec3& position) {
    position_ = position;
    if (glm::distance(position_, origin_) > 1000.0) {
        // Move the origin to the box: every particle moves back
        // by the same float offset, so the change is repeatable.
        const glm::vec3 step(origin_ - position_);
        for (Particle& particle : particles_) {
            particle.position += step;
        }
        origin_ = position_;
    }
}

const glm::dvec3& ParticleEmitter::GetPosition() const {
    return position_;
}

const glm::dvec3& ParticleEmitter::GetOrigin() const {
    return origin_;
}

void ParticleEmitter::SetSettings(const ParticleEmitterSettings& settings) {
    settings_ = settings;
}

const ParticleEmitterSettings& ParticleEmitter::GetSettings() const {
    return settings_;
}

void ParticleEmitter::SetEmitting(bool emitting) {
    emitting_ = emitting;
}

// SplitMix64 (Steele, Lea, Flood 2014): one 64-bit word of state, so the whole generator fits
// in the state dump, and the same numbers on every compiler (std::mt19937 has 2.5 KB of state,
// and the C++ library's distributions differ between implementations).
float ParticleEmitter::Random() {
    random_state_ += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = random_state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(z >> 40) * (1.0f / 16777216.0f);  // 24 bits: exact in a float
}

float ParticleEmitter::RandomSigned() {
    return Random() * 2.0f - 1.0f;
}

void ParticleEmitter::Start() {
    if (static_cast<int>(particles_.size()) >= settings_.max_particles) {
        return;
    }
    Particle particle;
    particle.position = glm::vec3(position_ - origin_) + glm::vec3(RandomSigned(), RandomSigned(), RandomSigned()) * settings_.box_size * 0.5f;
    particle.velocity = settings_.velocity + glm::vec3(RandomSigned(), RandomSigned(), RandomSigned()) * settings_.velocity_variation;
    particle.rotation = Random() * 6.2831853f;
    particle.size = settings_.start_size;
    particle.color = settings_.start_color;
    Life life;
    life.lifetime = std::max(settings_.lifetime * (1.0f + RandomSigned() * settings_.lifetime_variation), 0.001f);
    life.rotation_speed = settings_.rotation_speed + RandomSigned() * settings_.rotation_speed_variation;
    life.first_frame = std::floor(Random() * static_cast<float>(std::max(settings_.atlas_frames, 1)));
    particle.frame = life.first_frame;
    particles_.push_back(particle);
    lives_.push_back(life);
}

void ParticleEmitter::Update(float delta_seconds) {
    const float dt = std::max(delta_seconds, 0.0f);
    // Move and age; the dead are removed keeping the order of the rest (repeatable, and the
    // renderer sorts anyway).
    const float keep = std::exp(-settings_.drag * dt);
    const int frames = std::max(settings_.atlas_frames, 1);
    size_t alive = 0;
    for (size_t i = 0; i < particles_.size(); ++i) {
        Life& life = lives_[i];
        life.age += dt;
        if (life.age >= life.lifetime) {
            continue;
        }
        Particle& particle = particles_[i];
        particle.velocity = (particle.velocity + settings_.acceleration * dt) * keep;
        particle.position += particle.velocity * dt;
        particle.rotation += life.rotation_speed * dt;
        const float t = life.age / life.lifetime;
        particle.size = settings_.start_size + (settings_.end_size - settings_.start_size) * t;
        particle.color = {settings_.start_color.r + (settings_.end_color.r - settings_.start_color.r) * t,
                          settings_.start_color.g + (settings_.end_color.g - settings_.start_color.g) * t,
                          settings_.start_color.b + (settings_.end_color.b - settings_.start_color.b) * t,
                          settings_.start_color.a + (settings_.end_color.a - settings_.start_color.a) * t};
        if (settings_.frames_per_second > 0.0f) {
            particle.frame = std::fmod(life.first_frame + std::floor(life.age * settings_.frames_per_second), static_cast<float>(frames));
        }
        particles_[alive] = particle;
        lives_[alive] = life;
        ++alive;
    }
    particles_.resize(alive);
    lives_.resize(alive);

    if (emitting_ && settings_.rate > 0.0f) {
        carried_ += settings_.rate * dt;
        const int count = static_cast<int>(std::floor(carried_));
        carried_ -= static_cast<float>(count);
        for (int i = 0; i < count; ++i) {
            Start();
        }
    }
}

void ParticleEmitter::Burst(int count) {
    for (int i = 0; i < count; ++i) {
        Start();
    }
}

const std::vector<Particle>& ParticleEmitter::GetParticles() const {
    return particles_;
}

int ParticleEmitter::GetParticleCount() const {
    return static_cast<int>(particles_.size());
}

std::string ParticleEmitter::DumpStateToJson() const {
    std::string text = "{\"random_state\":";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%" PRIu64, random_state_);
    text += buffer;
    text += ",\"carried\":";
    AppendFloat(text, carried_);
    text += ",\"emitting\":";
    text += emitting_ ? "true" : "false";
    text += ",\"position\":";
    AppendDvec3(text, position_);
    text += ",\"origin\":";
    AppendDvec3(text, origin_);
    text += ",\"particles\":[";
    for (size_t i = 0; i < particles_.size(); ++i) {
        const Particle& particle = particles_[i];
        const Life& life = lives_[i];
        text += i == 0 ? "{" : ",{";
        text += "\"position\":";
        AppendVec3(text, particle.position);
        text += ",\"velocity\":";
        AppendVec3(text, particle.velocity);
        for (const auto& [name, value] : {std::pair<const char*, float>{"size", particle.size}, {"rotation", particle.rotation},
                                          {"frame", particle.frame}, {"age", life.age}, {"lifetime", life.lifetime},
                                          {"rotation_speed", life.rotation_speed}, {"first_frame", life.first_frame},
                                          {"r", particle.color.r}, {"g", particle.color.g}, {"b", particle.color.b},
                                          {"a", particle.color.a}}) {
            text += ",\"";
            text += name;
            text += "\":";
            AppendFloat(text, value);
        }
        text += '}';
    }
    text += "]}";
    return text;
}

ParticleEmitterSettings MakeRainSettings(float intensity, glm::vec2 wind) {
    const float amount = std::clamp(intensity, 0.0f, 1.0f);
    ParticleEmitterSettings rain;
    rain.lifetime = 2.0f;
    rain.lifetime_variation = 0.1f;
    rain.max_particles = static_cast<int>(4500.0f * amount) + 1;
    rain.rate = 2000.0f * amount;
    rain.box_size = {30.0f, 1.0f, 30.0f};
    rain.velocity = {wind.x, -7.0f, wind.y};
    rain.velocity_variation = {0.3f, 1.0f, 0.3f};
    rain.start_size = 0.008f;
    rain.end_size = 0.008f;
    rain.start_color = {0.75f, 0.78f, 0.82f, 0.45f};
    rain.end_color = rain.start_color;
    return rain;
}

ParticleEmitterSettings MakeDustSettings(float intensity) {
    const float amount = std::clamp(intensity, 0.0f, 1.0f);
    ParticleEmitterSettings dust;
    dust.lifetime = 6.0f;
    dust.lifetime_variation = 0.4f;
    dust.max_particles = static_cast<int>(3000.0f * amount) + 1;
    dust.rate = 420.0f * amount;
    dust.box_size = {24.0f, 8.0f, 24.0f};
    dust.velocity = {0.05f, 0.0f, 0.0f};
    dust.velocity_variation = {0.08f, 0.04f, 0.08f};
    dust.start_size = 0.025f;
    dust.end_size = 0.025f;
    // Fades in over its life (alpha 0 to 0.8) and disappears at its end; with many small
    // motes the moment one goes is not seen.
    dust.start_color = {1.0f, 0.95f, 0.85f, 0.0f};
    dust.end_color = {1.0f, 0.95f, 0.85f, 0.8f};
    dust.rotation_speed_variation = 1.0f;
    return dust;
}

Image MakeParticleDotImage(int size) {
    Image image = MakeSolidImage(size, size, 255, 255, 255);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
            const float alpha = std::max(0.0f, 1.0f - std::sqrt(u * u + v * v));
            image.pixels[static_cast<size_t>(y * size + x) * 4 + 3] = static_cast<std::uint8_t>(alpha * alpha * 255.0f);
        }
    }
    return image;
}

std::uint64_t ParticleEmitter::HashState() const {
    const std::string text = DumpStateToJson();
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (const char c : text) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 0x100000001B3ull;
    }
    return hash;
}

} // namespace poligon
