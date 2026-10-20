#include "resource/mesh_asset_resources.h"

#include "assets/glb_static_mesh_importer.h"
#include "assets/mesh_section_grouping.h"

#include <cmath>
#include <iostream>
#include <sstream>

namespace arp::resource {

namespace {

void flattenSubmeshes(assets::MeshAsset& mesh)
{
    if (mesh.submeshes.empty() && !mesh.indices.empty()) {
        mesh.submeshes.push_back({0u, static_cast<uint32_t>(mesh.indices.size()), 0u});
    }
}

} // anonymous namespace

ResourceLoadResult MeshAssetResource::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    std::cerr << "[MeshAssetResource::load] begin kind=" << static_cast<int>(sourceKind_) << '\n';
    mesh_ = std::make_unique<assets::MeshAsset>();

    switch (sourceKind_) {
    case MeshSourceKind::GlbFile:
        std::cerr << "[MeshAssetResource::load] loadFromGlb\n";
        loadFromGlb();
        break;
    case MeshSourceKind::ProceduralGrid:
        std::cerr << "[MeshAssetResource::load] generateProceduralGrid\n";
        generateProceduralGrid();
        break;
    case MeshSourceKind::ProceduralCube:
        std::cerr << "[MeshAssetResource::load] generateProceduralCube\n";
        generateProceduralCube();
        break;
    case MeshSourceKind::CustomMesh:
        std::cerr << "[MeshAssetResource::load] adopt CustomMesh\n";
        // The mesh was moved in at construction; take ownership.
        if (customMesh_) {
            mesh_ = std::move(customMesh_);
        }
        break;
    }

    if (!mesh_ || !mesh_->validForIndexedDraw() || !mesh_->recomputeBounds()) {
        std::cerr << "[MeshAssetResource::load] invalid indexed mesh\n";
        ResourceLoadResult result;
        result.success = false;
        result.state = ResourceState::Failed;
        result.resourceId = reference().id;
        result.resourceType = reference().type;
        result.diagnostics =
                core::pmr::String("MeshAssetResource load failed: mesh geometry or bounds are invalid",
                &core::defaultCpuMemoryResource());
        return result;
    }

    // Admit metadata into ResourceManager
    std::cerr << "[MeshAssetResource::load] admit metadata\n";
    return ResourceBase::load(manager, context);
}

void MeshAssetResource::loadFromGlb()
{
    textureResourceIds_.clear();
    if (sourcePath_.empty()) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(sourcePath_, ec)) {
        return;
    }

    auto result = assets::importStaticMeshFromGlbFile(sourcePath_);
    if (result.success && result.mesh.validForIndexedDraw()) {
        if (groupSectionsByMaterial_) {
            assets::groupMeshSectionsByMaterial(result.mesh);
        }
        *mesh_ = std::move(result.mesh);
        materials_ = std::move(result.materials);
        textures_ = std::move(result.textures);
        images_ = std::move(result.images);
        textureResourceIds_.reserve(textures_.size());
        const std::string_view meshResourceId(reference().id.data(), reference().id.size());
        for (std::size_t index = 0u; index < textures_.size(); ++index) {
            textureResourceIds_.push_back(deriveResourceUuid(
                meshResourceId, "TextureResource/glb", static_cast<std::uint64_t>(index)));
        }
        flattenSubmeshes(*mesh_);
    }
}

void MeshAssetResource::generateProceduralGrid()
{
    const auto gridX = gridX_;
    const auto gridZ = gridZ_;
    const float size = gridSize_;
    const float baseHeight = gridBaseHeight_;

    // When the resource was constructed with a (noise/biome) heightfield, the
    // rendered grid samples it directly — guaranteeing the mesh matches the
    // gameplay-sampled elevation (the movement system reads the same
    // TerrainHeightField instance). Otherwise fall back to the historical
    // sine formula for the legacy hardcoded path.
    const bool useHeightField =
        (proceduralHeightField_.layerCount + proceduralHeightField_.noiseLayerCount) > 0u;

    const float half = size * 0.5f;
    const float stepX = size / static_cast<float>(gridX);
    const float stepZ = size / static_cast<float>(gridZ);

    mesh_->vertices.reserve((gridX + 1) * (gridZ + 1));
    mesh_->indices.reserve(gridX * gridZ * 6);

    for (std::size_t z = 0; z <= gridZ; ++z) {
        for (std::size_t x = 0; x <= gridX; ++x) {
            const float px = -half + static_cast<float>(x) * stepX;
            const float pz = -half + static_cast<float>(z) * stepZ;
            const float h = useHeightField
                ? proceduralHeightField_.sample(px, pz)
                : baseHeight + 0.5f * std::sin(px * 0.08f + 0.3f) * std::cos(pz * 0.08f + 0.5f)
                    + 0.3f * std::sin(px * 0.15f + pz * 0.12f + 1.0f);

            assets::MeshVertex vert;
            vert.position[0] = px;
            vert.position[1] = h;
            vert.position[2] = pz;

            const float dx = 0.1f;
            const float hx1 = useHeightField ? proceduralHeightField_.sample(px + dx, pz)
                                             : baseHeight
                    + 0.5f * std::sin((px + dx) * 0.08f + 0.3f) * std::cos(pz * 0.08f + 0.5f)
                    + 0.3f * std::sin((px + dx) * 0.15f + pz * 0.12f + 1.0f);
            const float hz1 = useHeightField ? proceduralHeightField_.sample(px, pz + dx)
                                             : baseHeight
                    + 0.5f * std::sin(px * 0.08f + 0.3f) * std::cos((pz + dx) * 0.08f + 0.5f)
                    + 0.3f * std::sin(px * 0.15f + (pz + dx) * 0.12f + 1.0f);
            float nx = -(hx1 - h) / dx;
            float nz = -(hz1 - h) / dx;
            float ny = 1.0f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 0.0001f) {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            vert.normal[0] = nx;
            vert.normal[1] = ny;
            vert.normal[2] = nz;
            vert.uv[0] = static_cast<float>(x) / static_cast<float>(gridX) * uvScale_;
            vert.uv[1] = static_cast<float>(z) / static_cast<float>(gridZ) * uvScale_;
            vert.tangent[0] = 1.0f;
            vert.tangent[1] = 0.0f;
            vert.tangent[2] = 0.0f;
            vert.tangent[3] = 1.0f;
            mesh_->vertices.push_back(vert);
        }
    }

    for (std::size_t z = 0; z < gridZ; ++z) {
        for (std::size_t x = 0; x < gridX; ++x) {
            const auto tl = static_cast<std::uint32_t>(z * (gridX + 1) + x);
            const auto tr = tl + 1;
            const auto bl = static_cast<std::uint32_t>((z + 1) * (gridX + 1) + x);
            const auto br = bl + 1;
            mesh_->indices.push_back(tl);
            mesh_->indices.push_back(bl);
            mesh_->indices.push_back(tr);
            mesh_->indices.push_back(tr);
            mesh_->indices.push_back(bl);
            mesh_->indices.push_back(br);
        }
    }

    flattenSubmeshes(*mesh_);
}

void MeshAssetResource::generateProceduralCube()
{
    std::cerr << "[MeshAssetResource::generateProceduralCube] begin size=" << cubeSize_ << '\n';
    const float h = cubeSize_ * 0.5f;

    struct Vert {
        float px, py, pz, nx, ny, nz, u, v, tx, ty, tz, tw;
    };

    const Vert verts[24] = {
        {-h, -h, h, 0, 0, 1, 0, 1, 1, 0, 0, 1},
        {h, -h, h, 0, 0, 1, 1, 1, 1, 0, 0, 1},
        {-h, h, h, 0, 0, 1, 0, 0, 1, 0, 0, 1},
        {h, h, h, 0, 0, 1, 1, 0, 1, 0, 0, 1},
        {h, -h, -h, 0, 0, -1, 0, 1, -1, 0, 0, 1},
        {-h, -h, -h, 0, 0, -1, 1, 1, -1, 0, 0, 1},
        {h, h, -h, 0, 0, -1, 0, 0, -1, 0, 0, 1},
        {-h, h, -h, 0, 0, -1, 1, 0, -1, 0, 0, 1},
        {-h, h, h, 0, 1, 0, 0, 1, 1, 0, 0, 1},
        {h, h, h, 0, 1, 0, 1, 1, 1, 0, 0, 1},
        {-h, h, -h, 0, 1, 0, 0, 0, 1, 0, 0, 1},
        {h, h, -h, 0, 1, 0, 1, 0, 1, 0, 0, 1},
        {-h, -h, -h, 0, -1, 0, 0, 1, 1, 0, 0, 1},
        {h, -h, -h, 0, -1, 0, 1, 1, 1, 0, 0, 1},
        {-h, -h, h, 0, -1, 0, 0, 0, 1, 0, 0, 1},
        {h, -h, h, 0, -1, 0, 1, 0, 1, 0, 0, 1},
        {h, -h, h, 1, 0, 0, 0, 1, 0, 0, -1, 1},
        {h, -h, -h, 1, 0, 0, 1, 1, 0, 0, -1, 1},
        {h, h, h, 1, 0, 0, 0, 0, 0, 0, -1, 1},
        {h, h, -h, 1, 0, 0, 1, 0, 0, 0, -1, 1},
        {-h, -h, -h, -1, 0, 0, 0, 1, 0, 0, 1, 1},
        {-h, -h, h, -1, 0, 0, 1, 1, 0, 0, 1, 1},
        {-h, h, -h, -1, 0, 0, 0, 0, 0, 0, 1, 1},
        {-h, h, h, -1, 0, 0, 1, 0, 0, 0, 1, 1},
    };

    for (const auto& v : verts) {
        assets::MeshVertex mv;
        mv.position[0] = v.px;
        mv.position[1] = v.py;
        mv.position[2] = v.pz;
        mv.normal[0] = v.nx;
        mv.normal[1] = v.ny;
        mv.normal[2] = v.nz;
        mv.uv[0] = v.u;
        mv.uv[1] = v.v;
        mv.tangent[0] = v.tx;
        mv.tangent[1] = v.ty;
        mv.tangent[2] = v.tz;
        mv.tangent[3] = v.tw;
        mesh_->vertices.push_back(mv);
    }

    const std::uint32_t indices[36] = {
        0,
        1,
        2,
        2,
        1,
        3,
        4,
        5,
        6,
        6,
        5,
        7,
        8,
        9,
        10,
        10,
        9,
        11,
        12,
        13,
        14,
        14,
        13,
        15,
        16,
        17,
        18,
        18,
        17,
        19,
        20,
        21,
        22,
        22,
        21,
        23,
    };
    mesh_->indices.assign(indices, indices + 36);
    flattenSubmeshes(*mesh_);
    std::cerr << "[MeshAssetResource::generateProceduralCube] done verts=" << mesh_->vertices.size()
              << " idx=" << mesh_->indices.size() << '\n';
}

} // namespace arp::resource
