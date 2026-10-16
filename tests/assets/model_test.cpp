// Writes small model files (made here, so the repository needs no model assets), loads them
// and checks the result. Needs no graphics card.
// Usage: model_test OUTPUT_DIRECTORY
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "poligon/core/image.h"
#include "poligon/assets/model.h"
#include "poligon/assets/model_import.h"
#include "test_support.h"

namespace {

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

bool NearVec(glm::vec3 a, glm::vec3 b) {
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}

void WriteTextFile(const std::string& path, const std::string& content) {
    std::FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "wb") == 0 && file != nullptr) {
        std::fwrite(content.data(), 1, content.size(), file);
        std::fclose(file);
    }
}

std::string Base64(const std::vector<unsigned char>& bytes) {
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int b0 = bytes[i];
        const unsigned int b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const unsigned int b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        text += alphabet[b0 >> 2];
        text += alphabet[((b0 & 3) << 4) | (b1 >> 4)];
        text += i + 1 < bytes.size() ? alphabet[((b1 & 15) << 2) | (b2 >> 6)] : '=';
        text += i + 2 < bytes.size() ? alphabet[b2 & 63] : '=';
    }
    return text;
}

// A 2x2 m square standing on the ground, facing +Z, with a 2x2 texture: red and green on top,
// blue and white below. In OBJ files texture coordinate (0, 0) is the bottom-left corner.
void WriteObjModel(const std::string& directory, bool with_texture_file) {
    WriteTextFile(directory + "/model_test_quad.obj", "mtllib model_test_quad.mtl\n"
                                                      "o Quad\n"
                                                      "v -1 0 0\nv 1 0 0\nv 1 2 0\nv -1 2 0\n"
                                                      "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                                      "vn 0 0 1\n"
                                                      "usemtl Painted\n"
                                                      "f 1/1/1 2/2/1 3/3/1 4/4/1\n");
    WriteTextFile(directory + "/model_test_quad.mtl", std::string("newmtl Painted\nKd 0.5 0.25 1.0\nmap_Kd ") +
                                                          (with_texture_file ? "model_test_quad.png" : "missing.png") +
                                                          "\n");
    poligon::Image texture = poligon::MakeSolidImage(2, 2, 255, 255, 255);
    const std::uint8_t colors[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
    for (int i = 0; i < 4; ++i) {
        std::memcpy(&texture.pixels[static_cast<size_t>(i) * 4], colors[i], 3);
    }
    poligon::SaveImageToPng((directory + "/model_test_quad.png").c_str(), texture);
}

// A glTF file with one triangle on a child node: the parent is moved 10 along X, the child
// 5 along Y, so the triangle must end up at (10, 5, 0). It has no normals and no texture.
void WriteGltfModel(const std::string& directory) {
    const float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const unsigned short indices[3] = {0, 1, 2};
    std::vector<unsigned char> buffer(sizeof(positions) + sizeof(indices) + 2, 0);
    std::memcpy(buffer.data(), positions, sizeof(positions));
    std::memcpy(buffer.data() + sizeof(positions), indices, sizeof(indices));

    WriteTextFile(
        directory + "/model_test_triangle.gltf",
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"name\":\"Parent\",\"translation\":[10,0,0],\"children\":[1]},"
        "{\"name\":\"Child\",\"translation\":[0,5,0],\"mesh\":0}],"
        "\"meshes\":[{\"name\":\"Triangle\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}],"
        "\"materials\":[{\"name\":\"Blueish\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.2,0.4,0.6,1.0]}}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"buffers\":[{\"byteLength\":44,\"uri\":\"data:application/octet-stream;base64," +
            Base64(buffer) + "\"}]}");
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;
    if (argc != 2) {
        std::printf("usage: model_test OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::string directory = argv[1];
    std::string error;

    {
        WriteObjModel(directory, true);
        poligon::Model model;
        Expect(poligon::LoadModelFromFile((directory + "/model_test_quad.obj").c_str(), model, error), "OBJ loads");
        Expect(model.meshes.size() == 1 && model.instances.size() == 1, "one mesh, drawn once");
        Expect(model.GetTriangleCount() == 2 && model.GetVertexCount() == 4, "the square became two triangles");
        Expect(NearVec(model.bounds_min, {-1, 0, 0}) && NearVec(model.bounds_max, {1, 2, 0}), "bounds are right");
        Expect(model.warnings.empty(), "no warnings");

        if (model.meshes.size() == 1 && model.materials.size() >= 1) {
            const poligon::ModelMesh& mesh = model.meshes[0];
            const poligon::ModelMaterial& material = model.materials[static_cast<size_t>(mesh.material_index)];
            Expect(material.name == "Painted", "the material keeps its name");
            Expect(Near(material.base_color.r, 0.5f) && Near(material.base_color.g, 0.25f) &&
                       Near(material.base_color.b, 1.0f),
                   "the material keeps its color");
            Expect(material.base_color_texture.width == 2 && material.base_color_texture.height == 2,
                   "the texture next to the model is loaded");

            bool found_top_left = false;
            bool normals_face_z = true;
            for (const poligon::MeshVertex& vertex : mesh.vertices) {
                normals_face_z = normals_face_z && NearVec(vertex.normal, {0, 0, 1});
                if (NearVec(vertex.position, {-1, 2, 0})) {
                    found_top_left = Near(vertex.uv.x, 0.0f) && Near(vertex.uv.y, 0.0f);
                }
            }
            Expect(normals_face_z, "normals come from the file");
            Expect(found_top_left, "the top-left corner of the square has texture coordinate (0, 0)");

            // Front faces must be counter-clockwise when seen from where the normal points (+Z).
            const glm::vec3 a = mesh.vertices[mesh.indices[0]].position;
            const glm::vec3 b = mesh.vertices[mesh.indices[1]].position;
            const glm::vec3 c = mesh.vertices[mesh.indices[2]].position;
            Expect(glm::cross(b - a, c - a).z > 0.0f, "triangles are counter-clockwise");
        }
    }

    {
        WriteObjModel(directory, false);
        poligon::Model model;
        Expect(poligon::LoadModelFromFile((directory + "/model_test_quad.obj").c_str(), model, error),
               "a model with a missing texture still loads");
        Expect(model.warnings.size() == 1 && model.warnings[0].find("missing.png") != std::string::npos,
               "and warns about the missing file by name");
        Expect(!model.materials.empty() && model.materials.back().base_color_texture.width == 0,
               "its material simply has no texture");
        WriteObjModel(directory, true);  // leave the complete model behind for the viewer test
    }

    {
        WriteGltfModel(directory);
        poligon::Model model;
        Expect(poligon::LoadModelFromFile((directory + "/model_test_triangle.gltf").c_str(), model, error), "glTF loads");
        Expect(model.GetTriangleCount() == 1, "one triangle");
        Expect(model.instances.size() == 1 && model.instances[0].node_name == "Child", "the instance knows its node");
        Expect(NearVec(model.bounds_min, {10, 5, 0}) && NearVec(model.bounds_max, {11, 6, 0}),
               "parent and child transforms are both applied");
        if (!model.meshes.empty() && !model.materials.empty()) {
            const poligon::ModelMesh& mesh = model.meshes[0];
            const poligon::Color color = model.materials[static_cast<size_t>(mesh.material_index)].base_color;
            Expect(Near(color.r, 0.2f) && Near(color.g, 0.4f) && Near(color.b, 0.6f), "glTF base color is read");
            Expect(NearVec(mesh.vertices[0].normal, {0, 0, 1}), "missing normals are generated");
        }
    }

    {
        poligon::Model model;
        Expect(!poligon::LoadModelFromFile((directory + "/no_such_model.glb").c_str(), model, error),
               "a missing file is an error");
        Expect(error.find("no_such_model.glb") != std::string::npos, "the error names the file");

        WriteTextFile(directory + "/model_test_garbage.obj", "this is not a model\n");
        Expect(!poligon::LoadModelFromFile((directory + "/model_test_garbage.obj").c_str(), model, error),
               "a file without geometry is an error");
    }

    return test::ExitCode();
}
