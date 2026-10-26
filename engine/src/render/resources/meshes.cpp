// Meshes on the graphics card.

#include "../renderer/renderer_internal.h"

namespace poligon {

MeshHandle Renderer::CreateMesh(const MeshVertex* vertices, int vertex_count, const std::uint32_t* indices,
                                int index_count) {
    if (vertices == nullptr || indices == nullptr || vertex_count < 3 || index_count < 3 || index_count % 3 != 0) {
        FatalError("Renderer::CreateMesh: needs at least one triangle and an index count divisible by 3");
    }
    for (int i = 0; i < index_count; ++i) {
        if (indices[i] >= static_cast<std::uint32_t>(vertex_count)) {
            FatalError("Renderer::CreateMesh: an index points past the last vertex");
        }
    }

    Mesh mesh;
    mesh.vertex_buffer = impl_->CreateBufferWithData(
        SDL_GPU_BUFFERUSAGE_VERTEX, vertices, static_cast<Uint32>(static_cast<size_t>(vertex_count) * sizeof(MeshVertex)));
    mesh.index_buffer = impl_->CreateBufferWithData(
        SDL_GPU_BUFFERUSAGE_INDEX, indices, static_cast<Uint32>(static_cast<size_t>(index_count) * sizeof(std::uint32_t)));
    mesh.index_count = static_cast<Uint32>(index_count);
    mesh.bounds_min = vertices[0].position;
    mesh.bounds_max = vertices[0].position;
    for (int i = 1; i < vertex_count; ++i) {
        mesh.bounds_min = glm::min(mesh.bounds_min, vertices[i].position);
        mesh.bounds_max = glm::max(mesh.bounds_max, vertices[i].position);
    }
    impl_->meshes.push_back(mesh);

    MeshHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->meshes.size());
    return handle;
}

MeshHandle Renderer::CreateSkinnedMesh(const MeshVertex* vertices, const SkinVertex* skin, int vertex_count,
                                       const std::uint32_t* indices, int index_count) {
    if (skin == nullptr) {
        FatalError("Renderer::CreateSkinnedMesh: needs joints and weights for every vertex");
    }
    const MeshHandle handle = CreateMesh(vertices, vertex_count, indices, index_count);
    impl_->meshes[handle.id - 1].skin_buffer = impl_->CreateBufferWithData(
        SDL_GPU_BUFFERUSAGE_VERTEX, skin, static_cast<Uint32>(static_cast<size_t>(vertex_count) * sizeof(SkinVertex)));
    return handle;
}

MeshHandle Renderer::CreateCubeMesh() {
    // Six faces, four vertices each, so every face has its own flat normal and its own texture corners.
    struct Face {
        glm::vec3 normal;
        glm::vec3 right;
        glm::vec3 up;
    };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},  {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };

    MeshVertex vertices[24];
    std::uint32_t indices[36];
    for (int face_index = 0; face_index < 6; ++face_index) {
        const Face& face = faces[face_index];
        const glm::vec3 center = face.normal * 0.5f;
        const glm::vec3 half_right = face.right * 0.5f;
        const glm::vec3 half_up = face.up * 0.5f;
        // Seen from outside: bottom-left, bottom-right, top-right, top-left.
        MeshVertex* corner = &vertices[face_index * 4];
        corner[0] = {center - half_right - half_up, face.normal, {0.0f, 1.0f}};
        corner[1] = {center + half_right - half_up, face.normal, {1.0f, 1.0f}};
        corner[2] = {center + half_right + half_up, face.normal, {1.0f, 0.0f}};
        corner[3] = {center - half_right + half_up, face.normal, {0.0f, 0.0f}};

        const std::uint32_t base = static_cast<std::uint32_t>(face_index * 4);
        std::uint32_t* triangle = &indices[face_index * 6];
        triangle[0] = base;
        triangle[1] = base + 1;
        triangle[2] = base + 2;
        triangle[3] = base;
        triangle[4] = base + 2;
        triangle[5] = base + 3;
    }
    return CreateMesh(vertices, 24, indices, 36);
}

MeshHandle Renderer::CreatePlaneMesh() {
    const glm::vec3 up{0, 1, 0};
    const MeshVertex vertices[4] = {
        {{-0.5f, 0, 0.5f}, up, {0.0f, 1.0f}},
        {{0.5f, 0, 0.5f}, up, {1.0f, 1.0f}},
        {{0.5f, 0, -0.5f}, up, {1.0f, 0.0f}},
        {{-0.5f, 0, -0.5f}, up, {0.0f, 0.0f}},
    };
    const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    return CreateMesh(vertices, 4, indices, 6);
}

void Renderer::DestroyMesh(MeshHandle mesh) {
    if (impl_->in_frame) {
        FatalError("Renderer::DestroyMesh: do not destroy meshes between BeginFrame and EndFrame");
    }
    if (mesh.id == 0 || mesh.id > impl_->meshes.size() || impl_->meshes[mesh.id - 1].vertex_buffer == nullptr) {
        FatalError("Renderer::DestroyMesh: invalid or already destroyed mesh handle");
    }
    Mesh& entry = impl_->meshes[mesh.id - 1];
    ReleaseTrackedBuffer(impl_->device, entry.vertex_buffer);
    ReleaseTrackedBuffer(impl_->device, entry.index_buffer);
    if (entry.skin_buffer != nullptr) {
        ReleaseTrackedBuffer(impl_->device, entry.skin_buffer);
    }
    entry = Mesh{};
}

} // namespace poligon
