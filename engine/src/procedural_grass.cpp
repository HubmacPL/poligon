// Grass made by code (MakeGrassModel, declared in poligon/procedural_plants.h): tufts of real
// blades rather than pictures of blades on cards, so they look right from every side and
// bend blade by blade in the wind.
#include <algorithm>
#include <cmath>
#include <string>

#include "poligon/procedural_plants.h"

namespace poligon {

namespace {

constexpr float kPi = 3.14159265f;

class GrassRandom {
public:
    explicit GrassRandom(std::uint32_t seed) : state_(seed * 2654435761u + 0x6A09E667u) {}
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

std::uint8_t GrassByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// The blades' picture: a gradient up the blade (u across, v = 0 at the tip), from a darker,
// yellower foot to the blade's green and a dry tip, in eight columns of slightly different
// greens so that tufts are not all one color. A blade's u picks its column.
Image MakeBladesImage(std::uint32_t seed) {
    Image image = MakeSolidImage(64, 128, 0, 0, 0);
    GrassRandom random(seed);
    for (int column = 0; column < 8; ++column) {
        const glm::vec3 green(random.Between(0.22f, 0.32f), random.Between(0.34f, 0.44f), random.Between(0.12f, 0.18f));
        const glm::vec3 dry(random.Between(0.55f, 0.65f), random.Between(0.5f, 0.58f), random.Between(0.3f, 0.36f));
        for (int y = 0; y < image.height; ++y) {
            const float along = 1.0f - static_cast<float>(y) / static_cast<float>(image.height - 1);  // 0 foot, 1 tip
            glm::vec3 color = glm::mix(green * 0.75f, green, std::min(along * 3.0f, 1.0f));
            color = glm::mix(color, dry, std::pow(std::max(along - 0.7f, 0.0f) / 0.3f, 2.0f) * 0.6f);
            for (int x = column * 8; x < column * 8 + 8; ++x) {
                const float rib = (x % 8 == 3 || x % 8 == 4) ? 1.08f : 1.0f;
                std::uint8_t* pixel = &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
                pixel[0] = GrassByte(color.x * rib);
                pixel[1] = GrassByte(color.y * rib);
                pixel[2] = GrassByte(color.z * rib);
                pixel[3] = 255;
            }
        }
    }
    return image;
}

// One blade: a narrow strip of `segments` pieces from `foot`, leaning and curving away,
// narrowing to a point.
void AddGrassBlade(std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices, glm::vec3 foot, float height, float width,
                   glm::vec2 lean, float curve, float u, int segments) {
    const glm::vec3 lean_direction = glm::normalize(glm::vec3(lean.x, 0.0f, lean.y) + glm::vec3(1e-4f, 0.0f, 0.0f));
    const glm::vec3 side = glm::normalize(glm::cross(lean_direction, glm::vec3(0.0f, 1.0f, 0.0f)));
    std::uint32_t previous[2] = {0, 0};
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const glm::vec3 center = foot + glm::vec3(0.0f, height * t * (1.0f - 0.35f * curve * t), 0.0f) + lean_direction * (curve * height * 0.55f * t * t);
        const glm::vec3 tangent = glm::normalize(glm::vec3(0.0f, height * (1.0f - 0.7f * curve * t), 0.0f) + lean_direction * (curve * height * 1.1f * t));
        const float half = width * 0.5f * (1.0f - t * t);
        // The blade's normal points mostly up, a little to its face, so that grass is lit like
        // the ground it grows from (the material keeps it on back faces too).
        const glm::vec3 normal = glm::normalize(glm::cross(side, tangent) * 0.5f + glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec2 uv_left(u - 0.045f, 1.0f - t);
        const glm::vec2 uv_right(u + 0.045f, 1.0f - t);
        if (i == segments) {
            vertices.push_back({center, normal, {u, 0.0f}});
            const std::uint32_t tip = static_cast<std::uint32_t>(vertices.size() - 1);
            indices.insert(indices.end(), {previous[0], previous[1], tip});
            break;
        }
        vertices.push_back({center - side * half, normal, uv_left});
        vertices.push_back({center + side * half, normal, uv_right});
        const std::uint32_t left = static_cast<std::uint32_t>(vertices.size() - 2);
        const std::uint32_t right = left + 1;
        if (i > 0) {
            indices.insert(indices.end(), {previous[0], previous[1], right, previous[0], right, left});
        }
        previous[0] = left;
        previous[1] = right;
    }
}

} // namespace

Model MakeGrassModel(std::uint32_t seed, int tufts, float height_min, float height_max, int blades_min, int blades_max) {
    Model model;
    ModelMaterial material;
    material.name = "grass_blades";
    material.base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    material.base_color_texture = MakeBladesImage(seed);
    material.double_sided = true;
    material.back_faces_keep_normal = true;
    material.casts_ambient_occlusion = false;
    material.roughness = 0.7f;
    model.materials.push_back(material);
    GrassRandom random(seed);
    for (int tuft = 0; tuft < tufts; ++tuft) {
        ModelMesh mesh;
        mesh.name = "tuft_" + std::to_string(tuft);
        const int blades = blades_min + static_cast<int>(random.Next() * static_cast<float>(blades_max - blades_min + 1));
        const float tuft_height = random.Between(height_min, height_max);
        const float spread = random.Between(0.04f, 0.1f);
        for (int blade = 0; blade < blades; ++blade) {
            const float angle = random.Between(0.0f, 2.0f * kPi);
            const float distance = spread * std::sqrt(random.Next());
            const glm::vec3 foot(std::cos(angle) * distance, -0.02f, std::sin(angle) * distance);
            const float column = std::floor(random.Next() * 8.0f);
            AddGrassBlade(mesh.vertices, mesh.indices, foot, tuft_height * random.Between(0.55f, 1.0f), random.Between(0.006f, 0.011f),
                          {std::cos(angle) + random.Between(-0.5f, 0.5f), std::sin(angle) + random.Between(-0.5f, 0.5f)},
                          random.Between(0.15f, 0.8f), (column + 0.5f) / 8.0f, 3);
        }
        mesh.material_index = 0;
        model.meshes.push_back(std::move(mesh));
        ModelInstance instance;
        instance.node_name = "tuft_" + std::to_string(tuft);
        instance.mesh_index = tuft;
        // Tufts lie 2 m apart, so that MakePlantKind with split_variants gives one per tuft.
        instance.transform = glm::translate(glm::mat4(1.0f), {static_cast<float>(tuft) * 2.0f, 0.0f, 0.0f});
        model.instances.push_back(instance);
    }
    model.bounds_min = glm::vec3(-0.5f, -0.02f, -0.5f);
    model.bounds_max = glm::vec3(static_cast<float>(tufts) * 2.0f, height_max, 0.5f);
    return model;
}

Model MakeFlowerModel(std::uint32_t seed) {
    Model model;
    // Leaves and stems share the blades' picture; petals are plain white, yellow and purple
    // (three materials), the middles yellow.
    ModelMaterial leaves;
    leaves.name = "flower_leaves";
    leaves.base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    leaves.base_color_texture = MakeBladesImage(seed + 7u);
    leaves.double_sided = true;
    leaves.back_faces_keep_normal = true;
    leaves.casts_ambient_occlusion = false;
    leaves.roughness = 0.7f;
    model.materials.push_back(leaves);
    const glm::vec3 petal_colors[3] = {{0.92f, 0.92f, 0.88f}, {0.95f, 0.78f, 0.15f}, {0.55f, 0.35f, 0.75f}};
    const char* petal_names[3] = {"petals_white", "petals_yellow", "petals_purple"};
    for (int i = 0; i < 3; ++i) {
        ModelMaterial petals;
        petals.name = petal_names[i];
        petals.base_color = {petal_colors[i].x, petal_colors[i].y, petal_colors[i].z, 1.0f};
        petals.base_color_texture = MakeSolidImage(4, 4, 255, 255, 255);
        petals.double_sided = true;
        petals.back_faces_keep_normal = true;
        petals.casts_ambient_occlusion = false;
        petals.roughness = 0.6f;
        model.materials.push_back(petals);
    }
    GrassRandom random(seed);
    // Three clumps, one per petal color, 2 m apart.
    for (int clump = 0; clump < 3; ++clump) {
        ModelMesh green;
        green.name = "flower_leaves_" + std::to_string(clump);
        green.material_index = 0;
        ModelMesh heads;
        heads.name = std::string(petal_names[clump]) + "_" + std::to_string(clump);
        heads.material_index = 1 + clump;
        const int stems = 3 + static_cast<int>(random.Next() * 4.0f);
        for (int stem = 0; stem < stems; ++stem) {
            const float angle = random.Between(0.0f, 2.0f * kPi);
            const glm::vec3 foot(std::cos(angle) * random.Between(0.0f, 0.08f), -0.02f, std::sin(angle) * random.Between(0.0f, 0.08f));
            const float height = random.Between(0.25f, 0.55f);
            const glm::vec2 lean(random.Between(-0.3f, 0.3f), random.Between(-0.3f, 0.3f));
            AddGrassBlade(green.vertices, green.indices, foot, height, 0.004f, lean, 0.1f, 0.5f / 8.0f, 3);
            for (int leaf = 0; leaf < 2; ++leaf) {
                AddGrassBlade(green.vertices, green.indices, foot, height * random.Between(0.3f, 0.5f), random.Between(0.012f, 0.02f),
                              {std::cos(angle + leaf * kPi), std::sin(angle + leaf * kPi)}, 0.6f, 2.5f / 8.0f, 3);
            }
            // The flower: five petals around the stem's top, opening upwards, and a middle.
            const glm::vec3 top = foot + glm::vec3(lean.x * height * 0.45f, height * 0.97f, lean.y * height * 0.45f);
            const int petals = 5;
            const float size = random.Between(0.015f, 0.028f);
            const std::uint32_t center = static_cast<std::uint32_t>(heads.vertices.size());
            heads.vertices.push_back({top + glm::vec3(0.0f, 0.004f, 0.0f), {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
            for (int petal = 0; petal < petals * 2; ++petal) {
                const float a = static_cast<float>(petal) / static_cast<float>(petals * 2) * 2.0f * kPi;
                const float r = (petal % 2 == 0 ? size : size * 0.45f);
                heads.vertices.push_back({top + glm::vec3(std::cos(a) * r, r * 0.35f, std::sin(a) * r), {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
            }
            for (int petal = 0; petal < petals * 2; ++petal) {
                const std::uint32_t a = center + 1 + static_cast<std::uint32_t>(petal);
                const std::uint32_t b = center + 1 + static_cast<std::uint32_t>((petal + 1) % (petals * 2));
                heads.indices.insert(heads.indices.end(), {center, b, a});
            }
        }
        for (ModelMesh* mesh : {&green, &heads}) {
            model.meshes.push_back(std::move(*mesh));
            ModelInstance instance;
            instance.node_name = "flower_" + std::to_string(clump) + "_" + std::to_string(model.meshes.size());
            instance.mesh_index = static_cast<int>(model.meshes.size() - 1);
            instance.transform = glm::translate(glm::mat4(1.0f), {static_cast<float>(clump) * 2.0f, 0.0f, 0.0f});
            model.instances.push_back(instance);
        }
    }
    model.bounds_min = glm::vec3(-0.5f, -0.02f, -0.5f);
    model.bounds_max = glm::vec3(5.0f, 0.6f, 0.5f);
    return model;
}

} // namespace poligon
