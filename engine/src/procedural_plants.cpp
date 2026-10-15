#include "poligon/procedural_plants.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace poligon {

namespace {

constexpr float kPi = 3.14159265f;
// Clumps lie this far apart along x in a model, so that MakePlantKind splits them.
constexpr float kClumpSpacing = 12.0f;
constexpr int kClumpsPerModel = 3;

class Random {
public:
    explicit Random(std::uint32_t seed) : state_(seed * 2654435761u + 0x9E3779B9u) {}
    float Next() {
        state_ = state_ * 747796405u + 2891336453u;
        std::uint32_t word = ((state_ >> ((state_ >> 28u) + 4u)) ^ state_) * 277803737u;
        word = (word >> 22u) ^ word;
        return static_cast<float>(word >> 8) / 16777216.0f;
    }
    float Between(float low, float high) { return low + (high - low) * Next(); }

private:
    std::uint32_t state_;
};

struct MeshBuilder {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;

    std::uint32_t Add(const glm::vec3& position, const glm::vec3& normal, const glm::vec2& uv) {
        vertices.push_back({position, glm::normalize(normal), uv});
        return static_cast<std::uint32_t>(vertices.size() - 1);
    }
    void Triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        indices.insert(indices.end(), {a, b, c});
    }
};

std::uint8_t ToByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void SetPixel(Image& image, int x, int y, glm::vec3 color, float alpha = 1.0f) {
    std::uint8_t* pixel = &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
    pixel[0] = ToByte(color.x);
    pixel[1] = ToByte(color.y);
    pixel[2] = ToByte(color.z);
    pixel[3] = ToByte(alpha);
}

// A leaf blade's picture: pale at the foot, green above, browning towards the tip, with
// fine lengthwise stripes. v = 0 is the tip.
Image MakeBladeImage(glm::vec3 foot, glm::vec3 middle, glm::vec3 tip, std::uint32_t seed) {
    Image image = MakeSolidImage(32, 128, 0, 0, 0);
    Random random(seed);
    float stripes[32];
    for (float& stripe : stripes) {
        stripe = random.Between(0.85f, 1.1f);
    }
    for (int y = 0; y < image.height; ++y) {
        const float along = 1.0f - static_cast<float>(y) / static_cast<float>(image.height - 1);  // 0 foot, 1 tip
        glm::vec3 color = along < 0.25f ? glm::mix(foot, middle, along / 0.25f) : glm::mix(middle, tip, std::pow((along - 0.25f) / 0.75f, 3.0f));
        for (int x = 0; x < image.width; ++x) {
            const float middle_rib = 1.0f - 0.25f * std::exp(-std::pow((static_cast<float>(x) - 15.5f) / 1.5f, 2.0f));
            SetPixel(image, x, y, color * stripes[x] * middle_rib);
        }
    }
    return image;
}

// A strap-shaped leaf from `foot`, leaning towards `lean` and bending more towards the tip.
void AddBlade(MeshBuilder& mesh, glm::vec3 foot, float height, float width, glm::vec2 lean, float bend, float twist) {
    const int segments = 6;
    const glm::vec3 lean_direction = glm::normalize(glm::vec3(lean.x, 0.0f, lean.y) + glm::vec3(1e-4f, 0.0f, 0.0f));
    const glm::vec3 side = glm::normalize(glm::vec3(std::cos(twist), 0.0f, std::sin(twist)));
    std::uint32_t previous[2] = {0, 0};
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const glm::vec3 center = foot + glm::vec3(0.0f, height * t * (1.0f - 0.25f * bend * t * t), 0.0f) +
                                 lean_direction * (bend * height * 0.5f * t * t);
        const glm::vec3 tangent = glm::normalize(glm::vec3(0.0f, height * (1.0f - 0.75f * bend * t * t), 0.0f) +
                                                 lean_direction * (bend * height * t));
        const float half = width * 0.5f * (1.0f - std::pow(t, 1.6f)) + 0.0005f;
        const glm::vec3 normal = glm::cross(side, tangent);
        const std::uint32_t left = mesh.Add(center - side * half, normal, {0.0f, 1.0f - t});
        const std::uint32_t right = mesh.Add(center + side * half, normal, {1.0f, 1.0f - t});
        if (i > 0) {
            mesh.Triangle(previous[0], previous[1], right);
            mesh.Triangle(previous[0], right, left);
        }
        previous[0] = left;
        previous[1] = right;
    }
}

// An upright cylinder from `foot` (for stems and cattail heads).
void AddCylinder(MeshBuilder& mesh, glm::vec3 foot, float height, float radius, int sides, float v_from, float v_to) {
    const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= sides; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(sides) * 2.0f * kPi;
        const glm::vec3 out(std::cos(angle), 0.0f, std::sin(angle));
        const float u = static_cast<float>(i) / static_cast<float>(sides);
        mesh.Add(foot + out * radius, out, {u, v_to});
        mesh.Add(foot + out * radius + glm::vec3(0.0f, height, 0.0f), out, {u, v_from});
    }
    for (int i = 0; i < sides; ++i) {
        const std::uint32_t a = first + static_cast<std::uint32_t>(i) * 2;
        mesh.Triangle(a, a + 1, a + 3);
        mesh.Triangle(a, a + 3, a + 2);
    }
}

// Two crossed cards facing out from `center` (a reed's plume).
void AddCrossedCards(MeshBuilder& mesh, glm::vec3 foot, float height, float width, float turn) {
    for (int card = 0; card < 2; ++card) {
        const float angle = turn + static_cast<float>(card) * kPi * 0.5f;
        const glm::vec3 side(std::cos(angle) * width * 0.5f, 0.0f, std::sin(angle) * width * 0.5f);
        const glm::vec3 normal(-std::sin(angle), 0.0f, std::cos(angle));
        const glm::vec3 top(0.0f, height, 0.0f);
        const std::uint32_t a = mesh.Add(foot - side, normal, {0.0f, 1.0f});
        const std::uint32_t b = mesh.Add(foot + side, normal, {1.0f, 1.0f});
        const std::uint32_t c = mesh.Add(foot + side + top, normal, {1.0f, 0.0f});
        const std::uint32_t d = mesh.Add(foot - side + top, normal, {0.0f, 0.0f});
        mesh.Triangle(a, b, c);
        mesh.Triangle(a, c, d);
    }
}

void AddPart(Model& model, MeshBuilder& builder, int material, int clump) {
    if (builder.vertices.empty()) {
        return;
    }
    ModelMesh mesh;
    mesh.name = model.materials[static_cast<size_t>(material)].name + "_" + std::to_string(clump);
    mesh.vertices = std::move(builder.vertices);
    mesh.indices = std::move(builder.indices);
    mesh.material_index = material;
    model.meshes.push_back(std::move(mesh));
    ModelInstance instance;
    instance.node_name = "clump" + std::to_string(clump) + "_" + std::to_string(model.meshes.size());
    instance.mesh_index = static_cast<int>(model.meshes.size() - 1);
    instance.transform = glm::translate(glm::mat4(1.0f), {static_cast<float>(clump) * kClumpSpacing, 0.0f, 0.0f});
    model.instances.push_back(instance);
    builder = MeshBuilder{};
}

void ComputeBounds(Model& model) {
    model.bounds_min = glm::vec3(1.0e30f);
    model.bounds_max = glm::vec3(-1.0e30f);
    for (const ModelInstance& instance : model.instances) {
        for (const MeshVertex& vertex : model.meshes[static_cast<size_t>(instance.mesh_index)].vertices) {
            const glm::vec3 position = glm::vec3(instance.transform * glm::vec4(vertex.position, 1.0f));
            model.bounds_min = glm::min(model.bounds_min, position);
            model.bounds_max = glm::max(model.bounds_max, position);
        }
    }
}

ModelMaterial MakeMaterial(const char* name, Image texture, float alpha_cutoff, float roughness) {
    ModelMaterial material;
    material.name = name;
    material.base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    material.base_color_texture = std::move(texture);
    material.alpha_cutoff = alpha_cutoff;
    material.double_sided = true;
    material.roughness = roughness;
    return material;
}

// A feathery plume picture: an elongated, ragged blob of brown-purple threads.
Image MakePlumeImage(std::uint32_t seed) {
    Image image = MakeSolidImage(64, 128, 0, 0, 0, 0);
    Random random(seed);
    for (int thread = 0; thread < 220; ++thread) {
        const float along = random.Next();
        const float spread = 0.45f * std::sin(kPi * std::min(along * 1.2f, 1.0f));
        float x = 0.5f + random.Between(-0.1f, 0.1f);
        float y = 0.08f + along * 0.85f;
        const float dx = random.Between(-1.0f, 1.0f) * spread * 0.02f;
        const glm::vec3 color = glm::mix(glm::vec3(0.36f, 0.27f, 0.22f), glm::vec3(0.55f, 0.45f, 0.36f), random.Next());
        for (int step = 0; step < 40; ++step) {
            x += dx;
            y += 0.002f;
            const int px = static_cast<int>(x * 63.0f);
            const int py = static_cast<int>(y * 127.0f);
            if (px >= 0 && px < 64 && py >= 0 && py < 128) {
                SetPixel(image, px, py, color, 1.0f);
            }
        }
    }
    BleedColorsIntoTransparentPixels(image, 4);
    return image;
}

} // namespace

Model MakeReedModel(std::uint32_t seed) {
    Model model;
    model.materials.push_back(MakeMaterial("reed_leaf",
                                           MakeBladeImage({0.62f, 0.62f, 0.42f}, {0.36f, 0.50f, 0.22f}, {0.62f, 0.55f, 0.35f}, seed), 0.0f,
                                           0.6f));
    model.materials.push_back(MakeMaterial("reed_plume", MakePlumeImage(seed + 1), 0.4f, 0.9f));
    Random random(seed);
    for (int clump = 0; clump < kClumpsPerModel; ++clump) {
        MeshBuilder leaves;
        MeshBuilder plumes;
        const int stems = 18 + static_cast<int>(random.Next() * 14.0f);
        const float clump_radius = random.Between(0.35f, 0.7f);
        for (int stem = 0; stem < stems; ++stem) {
            const float angle = random.Between(0.0f, 2.0f * kPi);
            const float distance = clump_radius * std::sqrt(random.Next());
            const glm::vec3 foot(std::cos(angle) * distance, -0.05f, std::sin(angle) * distance);
            const float height = random.Between(1.6f, 2.6f);
            const glm::vec2 lean(std::cos(angle) + random.Between(-0.4f, 0.4f), std::sin(angle) + random.Between(-0.4f, 0.4f));
            // A stem's leaves: two or three blades of different lengths from near the foot.
            const int blades = 2 + static_cast<int>(random.Next() * 2.0f);
            for (int blade = 0; blade < blades; ++blade) {
                AddBlade(leaves, foot + glm::vec3(0.0f, random.Between(0.0f, 0.3f), 0.0f), height * random.Between(0.5f, 0.95f),
                         random.Between(0.012f, 0.022f), lean + glm::vec2(random.Between(-0.6f, 0.6f), random.Between(-0.6f, 0.6f)),
                         random.Between(0.25f, 0.7f), random.Between(0.0f, 2.0f * kPi));
            }
            if (random.Next() < 0.45f) {
                AddCrossedCards(plumes, foot + glm::vec3(0.0f, height * 0.88f, 0.0f), random.Between(0.25f, 0.38f),
                                random.Between(0.1f, 0.15f), random.Between(0.0f, kPi));
            }
        }
        AddPart(model, leaves, 0, clump);
        AddPart(model, plumes, 1, clump);
    }
    ComputeBounds(model);
    return model;
}

Model MakeCattailModel(std::uint32_t seed) {
    Model model;
    model.materials.push_back(MakeMaterial("cattail_leaf",
                                           MakeBladeImage({0.55f, 0.60f, 0.40f}, {0.30f, 0.45f, 0.20f}, {0.40f, 0.42f, 0.25f}, seed), 0.0f,
                                           0.55f));
    // The head: velvety dark brown, a little lighter at the ends.
    Image head = MakeSolidImage(16, 64, 0, 0, 0);
    Random texture_random(seed + 3);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float shade = texture_random.Between(0.85f, 1.1f);
            const float end = (y < 4 || y > 59) ? 1.3f : 1.0f;
            SetPixel(head, x, y, glm::vec3(0.30f, 0.19f, 0.11f) * shade * end);
        }
    }
    model.materials.push_back(MakeMaterial("cattail_head", head, 0.0f, 0.95f));
    Random random(seed);
    for (int clump = 0; clump < kClumpsPerModel; ++clump) {
        MeshBuilder leaves;
        MeshBuilder heads;
        const float clump_radius = random.Between(0.3f, 0.55f);
        const int blades = 16 + static_cast<int>(random.Next() * 10.0f);
        for (int blade = 0; blade < blades; ++blade) {
            const float angle = random.Between(0.0f, 2.0f * kPi);
            const float distance = clump_radius * std::sqrt(random.Next());
            const glm::vec3 foot(std::cos(angle) * distance, -0.05f, std::sin(angle) * distance);
            AddBlade(leaves, foot, random.Between(1.1f, 1.9f), random.Between(0.018f, 0.03f),
                     {std::cos(angle) + random.Between(-0.3f, 0.3f), std::sin(angle) + random.Between(-0.3f, 0.3f)},
                     random.Between(0.1f, 0.35f), angle + kPi * 0.5f + random.Between(-0.3f, 0.3f));
        }
        const int stems = 3 + static_cast<int>(random.Next() * 4.0f);
        for (int stem = 0; stem < stems; ++stem) {
            const float angle = random.Between(0.0f, 2.0f * kPi);
            const float distance = clump_radius * 0.7f * std::sqrt(random.Next());
            const glm::vec3 foot(std::cos(angle) * distance, -0.05f, std::sin(angle) * distance);
            const float height = random.Between(1.3f, 2.0f);
            AddCylinder(leaves, foot, height, 0.004f, 4, 0.0f, 1.0f);
            const float head_length = random.Between(0.13f, 0.2f);
            AddCylinder(heads, foot + glm::vec3(0.0f, height - head_length - 0.12f, 0.0f), head_length, random.Between(0.012f, 0.016f), 8,
                        0.0f, 1.0f);
        }
        AddPart(model, leaves, 0, clump);
        AddPart(model, heads, 1, clump);
    }
    ComputeBounds(model);
    return model;
}

Model MakeWaterLilyModel(std::uint32_t seed) {
    Model model;
    // The leaf: green, darker towards the rim, with veins from the middle; a reddish rim.
    Image pad = MakeSolidImage(128, 128, 0, 0, 0);
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const glm::vec2 p((static_cast<float>(x) - 63.5f) / 63.5f, (static_cast<float>(y) - 63.5f) / 63.5f);
            const float r = glm::length(p);
            const float angle = std::atan2(p.y, p.x);
            const float vein = 1.0f - 0.18f * std::pow(std::max(std::cos(angle * 14.0f), 0.0f), 30.0f) * std::min(r * 3.0f, 1.0f);
            glm::vec3 color = glm::mix(glm::vec3(0.22f, 0.42f, 0.16f), glm::vec3(0.12f, 0.30f, 0.10f), std::min(r, 1.0f)) * vein;
            if (r > 0.92f) {
                color = glm::mix(color, glm::vec3(0.35f, 0.18f, 0.12f), (r - 0.92f) / 0.08f);
            }
            SetPixel(pad, x, y, color);
        }
    }
    ModelMaterial pad_material = MakeMaterial("lily_pad", pad, 0.0f, 0.35f);
    pad_material.double_sided = false;
    model.materials.push_back(pad_material);
    Image petal = MakeSolidImage(16, 32, 0, 0, 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float tip = 1.0f - static_cast<float>(y) / 31.0f;
            SetPixel(petal, x, y, glm::mix(glm::vec3(0.95f, 0.95f, 0.92f), glm::vec3(0.98f, 0.82f, 0.86f), tip * tip));
        }
    }
    model.materials.push_back(MakeMaterial("lily_petal", petal, 0.0f, 0.6f));
    model.materials.push_back(MakeMaterial("lily_center", MakeSolidImage(4, 4, 235, 190, 40), 0.0f, 0.7f));
    Random random(seed);
    for (int clump = 0; clump < kClumpsPerModel; ++clump) {
        MeshBuilder pads;
        MeshBuilder petals;
        MeshBuilder centers;
        const int leaves = 6 + static_cast<int>(random.Next() * 8.0f);
        const float clump_radius = random.Between(0.6f, 1.2f);
        for (int leaf = 0; leaf < leaves; ++leaf) {
            const float angle = random.Between(0.0f, 2.0f * kPi);
            const float distance = clump_radius * std::sqrt(random.Next());
            const glm::vec3 middle(std::cos(angle) * distance, random.Between(0.004f, 0.012f), std::sin(angle) * distance);
            const float radius = random.Between(0.07f, 0.13f);
            const float notch = random.Between(0.0f, 2.0f * kPi);
            const int steps = 20;
            const std::uint32_t center = pads.Add(middle, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f});
            for (int i = 0; i <= steps; ++i) {
                // Around the leaf, leaving out a narrow wedge (the notch).
                const float a = notch + 0.2f + static_cast<float>(i) / static_cast<float>(steps) * (2.0f * kPi - 0.4f);
                const float wave = 1.0f + 0.03f * std::sin(a * 5.0f + static_cast<float>(leaf));
                const glm::vec2 local(std::cos(a) * wave, std::sin(a) * wave);
                pads.Add(middle + glm::vec3(local.x * radius, 0.0f, local.y * radius), {0.0f, 1.0f, 0.0f},
                         {0.5f + local.x * 0.5f, 0.5f + local.y * 0.5f});
                if (i > 0) {
                    // Counter-clockwise seen from above.
                    pads.Triangle(center, center + static_cast<std::uint32_t>(i) + 1, center + static_cast<std::uint32_t>(i));
                }
            }
        }
        const int flowers = static_cast<int>(random.Next() * 3.0f);
        for (int flower = 0; flower < flowers; ++flower) {
            const float angle = random.Between(0.0f, 2.0f * kPi);
            const float distance = clump_radius * 0.7f * std::sqrt(random.Next());
            const glm::vec3 middle(std::cos(angle) * distance, 0.02f, std::sin(angle) * distance);
            for (int ring = 0; ring < 2; ++ring) {
                const int count = 10;
                const float rise = ring == 0 ? 0.55f : 1.0f;  // outer petals flatter
                const float length = ring == 0 ? 0.065f : 0.05f;
                for (int i = 0; i < count; ++i) {
                    const float a = (static_cast<float>(i) + 0.5f * static_cast<float>(ring)) / static_cast<float>(count) * 2.0f * kPi;
                    const glm::vec3 out(std::cos(a), 0.0f, std::sin(a));
                    const glm::vec3 side(-std::sin(a), 0.0f, std::cos(a));
                    const glm::vec3 tip = middle + (out + glm::vec3(0.0f, rise, 0.0f)) * (length / std::sqrt(1.0f + rise * rise));
                    const glm::vec3 normal = glm::cross(side, tip - middle);
                    const std::uint32_t a0 = petals.Add(middle - side * 0.008f, normal, {0.0f, 1.0f});
                    const std::uint32_t a1 = petals.Add(middle + side * 0.008f, normal, {1.0f, 1.0f});
                    const std::uint32_t a2 = petals.Add(tip + side * 0.014f, normal, {1.0f, 0.0f});
                    const std::uint32_t a3 = petals.Add(tip - side * 0.014f, normal, {0.0f, 0.0f});
                    petals.Triangle(a0, a1, a2);
                    petals.Triangle(a0, a2, a3);
                }
            }
            AddCylinder(centers, middle, 0.02f, 0.012f, 8, 0.0f, 1.0f);
        }
        AddPart(model, pads, 0, clump);
        AddPart(model, petals, 1, clump);
        AddPart(model, centers, 2, clump);
    }
    ComputeBounds(model);
    return model;
}

LoadedModel UploadLoadedModel(Renderer& renderer, const Model& model) {
    LoadedModel loaded;
    loaded.model = model;
    loaded.render = UploadModel(renderer, model);
    return loaded;
}

} // namespace poligon
