#include "assets/assimp_model_importer.h"

#include "core/memory/cpu_allocator.h"

#ifndef ARP_HAS_ASSIMP
#define ARP_HAS_ASSIMP 0
#endif

#if ARP_HAS_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif

namespace arp::assets {

namespace {

AnimatedMeshImportResult fail(const std::pmr::string& diagnostics)
{
    AnimatedMeshImportResult result;
    result.diagnostics = diagnostics;
    return result;
}

#if ARP_HAS_ASSIMP

std::pmr::string aiName(const aiString& value)
{
    return std::pmr::string(value.C_Str());
}

aiMatrix4x4 findMeshTransform(const aiNode* node, unsigned int meshIndex)
{
    if (node == nullptr) {
        return {};
    }
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        if (node->mMeshes[i] == meshIndex) {
            return node->mTransformation;
        }
    }
    for (unsigned int child = 0; child < node->mNumChildren; ++child) {
        auto childTransform = findMeshTransform(node->mChildren[child], meshIndex);
        if (childTransform != aiMatrix4x4{}) {
            return node->mTransformation * childTransform;
        }
    }
    return {};
}

#endif

} // namespace

bool AssimpModelImporter::assimpAvailable() const
{
    return ARP_HAS_ASSIMP != 0;
}

AnimatedMeshImportResult AssimpModelImporter::importFile(
    const std::filesystem::path& path, const AssimpModelImportOptions& options) const
{
#if !ARP_HAS_ASSIMP
    (void)path;
    (void)options;
    return fail("Assimp is required but was not available at configure time");
#else
    Assimp::Importer importer;
    const auto* scene = importer.ReadFile(path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals
            | aiProcess_CalcTangentSpace | aiProcess_LimitBoneWeights
            | aiProcess_ImproveCacheLocality);

    if (scene == nullptr) {
        std::pmr::string diagnostics("Assimp failed to read '");
        diagnostics += path.string();
        diagnostics += "': ";
        diagnostics += importer.GetErrorString();
        return fail(diagnostics);
    }
    if (scene->mRootNode == nullptr || scene->mNumMeshes == 0) {
        return fail("Assimp returned a scene with no meshes");
    }

    AnimatedMeshAsset asset;

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const auto* source = scene->mMeshes[meshIndex];
        if (source == nullptr || source->mNumVertices == 0 || source->mNumFaces == 0) {
            continue;
        }

        const auto meshTransform = findMeshTransform(scene->mRootNode, meshIndex);
        const auto firstVertex = static_cast<uint32_t>(asset.mesh.vertices.size());
        const auto firstIndex = static_cast<uint32_t>(asset.mesh.indices.size());

        for (unsigned int vi = 0; vi < source->mNumVertices; ++vi) {
            const auto pos = meshTransform * source->mVertices[vi];
            MeshVertex vert;
            vert.position[0] = pos.x;
            vert.position[1] = pos.y;
            vert.position[2] = pos.z;

            if (source->HasNormals()) {
                auto n = source->mNormals[vi];
                float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (len > 0.0001f) {
                    n.x /= len;
                    n.y /= len;
                    n.z /= len;
                }
                vert.normal[0] = n.x;
                vert.normal[1] = n.y;
                vert.normal[2] = n.z;
            }

            if (source->HasTextureCoords(0)) {
                vert.uv[0] = source->mTextureCoords[0][vi].x;
                vert.uv[1] = source->mTextureCoords[0][vi].y;
            }

            if (source->HasTangentsAndBitangents()) {
                auto t = source->mTangents[vi];
                float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
                if (len > 0.0001f) {
                    t.x /= len;
                    t.y /= len;
                    t.z /= len;
                }
                vert.tangent[0] = t.x;
                vert.tangent[1] = t.y;
                vert.tangent[2] = t.z;
                vert.tangent[3] = 1.0f;
            }

            asset.mesh.vertices.push_back(vert);
        }

        for (unsigned int fi = 0; fi < source->mNumFaces; ++fi) {
            const auto& face = source->mFaces[fi];
            for (unsigned int i = 0; i < face.mNumIndices; ++i) {
                asset.mesh.indices.push_back(firstVertex + face.mIndices[i]);
            }
        }

        if (source->mNumFaces > 0) {
            asset.mesh.submeshes.push_back(Submesh{
                firstIndex,
                static_cast<uint32_t>(asset.mesh.indices.size()) - firstIndex,
                source->mMaterialIndex,
            });
        }
    }

    if (!asset.mesh.validForIndexedDraw()) {
        return fail("Assimp import did not produce a drawable mesh");
    }

    AnimatedMeshImportResult result;
    result.success = true;
    result.asset = std::move(asset);
    return result;
#endif
}

} // namespace arp::assets
