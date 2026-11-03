#include "resource/mesh_asset_resources.h"

#include "assets/glb_static_mesh_importer.h"
#include "assets/mesh_section_grouping.h"
#include "resource/resource_source_io.h"

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
try {
    std::cerr << "[MeshAssetResource::load] begin kind=" << static_cast<int>(sourceKind_) << '\n';
    mesh_ = core::makeUnique<assets::MeshAsset>();

    switch (sourceKind_) {
    case MeshSourceKind::GlbFile:
        std::cerr << "[MeshAssetResource::load] loadFromGlb\n";
        loadFromGlb(context);
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
        result.diagnostics = core::pmr::String(gridSamples_.diagnostic()
                ? gridSamples_.diagnostic()
                : "MeshAssetResource load failed: mesh geometry or bounds are invalid",
            &core::defaultCpuMemoryResource());
        return result;
    }

    // Admit metadata into ResourceManager
    std::cerr << "[MeshAssetResource::load] admit metadata\n";
    return ResourceBase::load(manager, context);
} catch (const std::bad_alloc&) {
    return resourceLoadFailure(*this, "MESH-GLB-IMPORT-BUDGET");
} catch (const std::exception& error) {
    return resourceLoadFailure(*this, error.what());
}

const char* MeshAssetResource::validateGlbSource() const
{
    if (!glbSource_)
        return "MESH-GLB-EXPLICIT-SOURCE-REQUIRED";
    const auto& source = *glbSource_;
    const auto& limits = source.limits;
    if (!source.maxImportBytes || source.maxImportBytes == UINT64_MAX || !limits.maxVertices
        || !limits.maxIndices || !limits.maxPrimitives || !limits.maxJsonDepth
        || limits.maxJsonDepth > 64 || !limits.maxJsonValues || limits.maxSourceBytes > UINT32_MAX)
        return "MESH-GLB-IMPORT-LIMITS";
    return validateResourceReadIntent(source.path, limits.maxSourceBytes, source.origin).diagnostic;
}

bool MeshAssetResource::collectFileReadIntents(ResourceManager&,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostic) const
{
    if (sourceKind_ != MeshSourceKind::GlbFile)
        return true;
    if (const auto* issue = validateGlbSource()) {
        diagnostic = issue;
        return false;
    }
    if (!appendResourceReadIntent(glbSource_->path,
            glbSource_->limits.maxSourceBytes,
            intents,
            diagnostic,
            glbSource_->origin))
        return false;
    intents.back().role = "mesh-glb";
    return true;
}

void MeshAssetResource::loadFromGlb(const ResourceLoadContext& context)
{
    core::SharedPtr<core::CpuMemoryQuota> quota;
    std::optional<core::CpuMemoryQuotaScope> importScope;
    if (glbSource_) {
        if (const auto* issue = validateGlbSource())
            throw core::DiagnosticError(issue);
        quota = core::makeShared<core::CpuMemoryQuota>(glbSource_->maxImportBytes);
        importScope.emplace(quota);
    }
    textureResourceIds_.clear();
    if (sourcePath_.empty()) {
        return;
    }

    sourceIdentities_.clear();
    sourceIdentityVersion_ = 0;
    sourceContentDigest_ = {};
    assets::StaticMeshImportResult result;
    if (glbSource_) {
        const auto bytes = readResourceSource(glbSource_->path,
            context,
            glbSource_->limits.maxSourceBytes,
            false,
            &glbSource_->origin);
        if (!bytes.success)
            throw core::DiagnosticError(bytes.diagnostics);
        const core::ArrayView<const std::uint8_t> input(bytes.bytes.data(), bytes.bytes.size());
        if (retainSourceIdentity_) {
            auto identified =
                assets::importStaticMeshWithSourceIdentityFromGlbBytes(input, glbSource_->limits);
            sourceIdentityVersion_ = identified.identityVersion;
            sourceContentDigest_ = identified.sourceContentDigest;
            sourceIdentities_ = std::move(identified.identities);
            result = std::move(identified.geometry);
        } else {
            result = assets::importStaticMeshFromGlbBytes(input, glbSource_->limits);
        }
        if (!result.success)
            throw core::DiagnosticError(result.diagnostics);
    } else if (context.requirePreloadedSources || context.sources) {
        throw core::DiagnosticError("MESH-GLB-EXPLICIT-SOURCE-REQUIRED");
    } else if (retainSourceIdentity_) {
        auto identified = assets::importStaticMeshWithSourceIdentityFromGlbFile(sourcePath_);
        sourceIdentityVersion_ = identified.identityVersion;
        sourceContentDigest_ = identified.sourceContentDigest;
        sourceIdentities_ = std::move(identified.identities);
        result = std::move(identified.geometry);
        if (!result.success)
            throw core::DiagnosticError(result.diagnostics);
    } else {
        result = assets::importStaticMeshFromGlbFile(sourcePath_);
    }
    if (result.success && result.mesh.validForIndexedDraw()) {
        if (groupSectionsByMaterial_) {
            assets::groupMeshSectionsByMaterial(result.mesh, retainSourceRanges_);
        } else if (retainSourceRanges_) {
            throw core::DiagnosticError("MESH-SOURCE-RANGES-REQUIRE-MATERIAL-GROUPING");
        }
        if (sectionSpatialPartition_.enabled()) {
            assets::partitionMeshSectionsSpatially(result.mesh, sectionSpatialPartition_);
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
        if (context.maxDecodedBytes && processedByteCount() > context.maxDecodedBytes)
            throw core::DiagnosticError("MESH-GLB-DECODE-BUDGET");
    }
}

void MeshAssetResource::generateProceduralGrid()
{
    const auto gridX = gridX_;
    const auto gridZ = gridZ_;
    const float size = gridSize_;
    const float baseHeight = gridBaseHeight_;

    // The explicit source is already materialized; no generator survives load.
    const bool useHeightField = explicitGridSamples_;
    if (useHeightField && !gridSamples_.valid())
        return;

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
                ? gridSamples_.samples()[z * (gridX + 1u) + x].height
                : baseHeight + 0.5f * std::sin(px * 0.08f + 0.3f) * std::cos(pz * 0.08f + 0.5f)
                    + 0.3f * std::sin(px * 0.15f + pz * 0.12f + 1.0f);

            assets::MeshVertex vert;
            vert.position[0] = px;
            vert.position[1] = h;
            vert.position[2] = pz;

            const float dx = terrain::TerrainMeshSamples::legacyNormalSampleSpacing;
            const float hx1 = useHeightField
                ? gridSamples_.samples()[z * (gridX + 1u) + x].offsetHeightX
                : baseHeight
                    + 0.5f * std::sin((px + dx) * 0.08f + 0.3f) * std::cos(pz * 0.08f + 0.5f)
                    + 0.3f * std::sin((px + dx) * 0.15f + pz * 0.12f + 1.0f);
            const float hz1 = useHeightField
                ? gridSamples_.samples()[z * (gridX + 1u) + x].offsetHeightZ
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
