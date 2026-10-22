#pragma once

#include "assets/glb_static_mesh_importer.h"
#include "assets/mesh_source_ranges.h"

#include <cstring>

namespace arp::assets {
// Compare complete indexed attributes across independently packed mesh banks.
// Both ranges must already have passed validateMeshSourceRanges.
inline void validateExactMeshSourceRangeAttributes(const MeshAsset& source,
    const MeshSourceRange& a,
    const MeshAsset& candidate,
    const MeshSourceRange& b)
{
    if (a.indices.indexCount != b.indices.indexCount)
        throw core::DiagnosticError("MESH-SOURCE-EXACT-RANGE-COUNT-MISMATCH");
    for (std::uint32_t i = 0; i < a.indices.indexCount; ++i) {
        const auto& av = source.vertices[source.indices[a.indices.firstIndex + i]];
        const auto& bv = candidate.vertices[candidate.indices[b.indices.firstIndex + i]];
        if (std::memcmp(av.position, bv.position, sizeof(av.position)) != 0
            || std::memcmp(av.normal, bv.normal, sizeof(av.normal)) != 0
            || std::memcmp(av.uv, bv.uv, sizeof(av.uv)) != 0
            || std::memcmp(av.tangent, bv.tangent, sizeof(av.tangent)) != 0)
            throw core::DiagnosticError("MESH-SOURCE-EXACT-RANGE-ATTRIBUTES-MISMATCH");
    }
}

inline std::uint64_t meshSourceIdentityKey(const StaticMeshSourceIdentity& identity)
{
    return (std::uint64_t(identity.node) << 32u) | identity.primitive;
}

// Bind retained material-grouped ranges to explicit importer identities. Each
// interval and identity occurs once and coverage is complete before use.
inline core::pmr::Vector<std::uint32_t> bindMeshSourceIdentities(
    const MeshAsset& mesh, const core::pmr::Vector<StaticMeshSourceIdentity>& identities)
{
    validateMeshSourceRanges(mesh);
    if (identities.empty() || mesh.sourceRanges.size() != identities.size()
        || identities.size() > UINT32_MAX)
        throw core::DiagnosticError("MESH-SOURCE-IDENTITY-COUNT-INVALID");
    core::pmr::UnorderedMap<std::uint32_t, std::uint32_t> offsets{
        &core::defaultCpuMemoryResource()};
    core::pmr::UnorderedSet<std::uint64_t> keys{&core::defaultCpuMemoryResource()};
    std::size_t covered = 0;
    for (std::size_t i = 0; i < identities.size(); ++i) {
        const auto& identity = identities[i];
        const auto& interval = identity.sourceIndices;
        if (!interval.indexCount || interval.indexCount % 3 || interval.firstIndex != covered
            || interval.indexCount > mesh.indices.size() - covered
            || !offsets.emplace(interval.firstIndex, static_cast<std::uint32_t>(i)).second
            || !keys.insert(meshSourceIdentityKey(identity)).second)
            throw core::DiagnosticError("MESH-SOURCE-IDENTITY-PARTITION-INVALID");
        covered += interval.indexCount;
    }
    if (covered != mesh.indices.size())
        throw core::DiagnosticError("MESH-SOURCE-IDENTITY-COVERAGE-INVALID");
    core::pmr::Vector<std::uint32_t> result{&core::defaultCpuMemoryResource()};
    result.reserve(identities.size());
    for (const auto& range : mesh.sourceRanges) {
        const auto found = offsets.find(range.sourceFirstIndex);
        if (found == offsets.end())
            throw core::DiagnosticError("MESH-SOURCE-IDENTITY-RANGE-MISSING");
        const auto& original = identities[found->second].sourceIndices;
        if (range.indices.indexCount != original.indexCount
            || range.indices.materialSlot != original.materialSlot)
            throw core::DiagnosticError("MESH-SOURCE-IDENTITY-RANGE-MISMATCH");
        result.push_back(found->second);
        offsets.erase(found);
    }
    if (!offsets.empty())
        throw core::DiagnosticError("MESH-SOURCE-IDENTITY-RANGE-DUPLICATE");
    return result;
}
} // namespace arp::assets
