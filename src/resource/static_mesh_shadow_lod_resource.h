#pragma once

#include "assets/mesh_shadow_lod_source.h"
#include "resource/mesh_asset_resources.h"

#include <array>

namespace arp::resource {

// Immutable, validated source metadata copied into a prepared render session.
// Each range belongs to its own resident level; no cross-level offset equality.
using StaticMeshShadowLodSource = assets::MeshShadowLodSource;

// StaticMeshShadowLodAsset/1 (unpacked) and /2 (explicit source index order).
// Explicit Prototype, synchronous preparation only.
// The manager owns candidate CPU geometry and a generation-bound source relation;
// no GPU object or native backend state crosses this resource boundary.
class StaticMeshShadowLodResource final : public ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept
    {
        return "StaticMeshShadowLodResource";
    }

    StaticMeshShadowLodResource(core::pmr::String resourceId,
        core::pmr::String sourceResourceId,
        std::filesystem::path projectRoot,
        std::filesystem::path manifestPath);

    ResourceLoadResult load(ResourceManager&, const ResourceLoadContext&) override;
    std::size_t processedByteCount() const noexcept override;
    void appendContentFingerprint(core::StableHash64&) const noexcept override;

    const assets::MeshAsset& candidate(unsigned level) const;

    const core::pmr::Vector<std::uint32_t>& reorderedOriginalIndices() const
    {
        return reorderedOriginalIndices_;
    }

    std::uint32_t indexOrderVersion() const { return indexOrderVersion_; }

    const core::pmr::Vector<StaticMeshShadowLodSource>& sources() const { return sources_; }

    const core::pmr::String& sourceResourceId() const { return sourceResourceId_; }

    ResourceGeneration sourceGeneration() const { return sourceGeneration_; }

    double shadowTexelCeiling() const { return shadowTexelCeiling_; }

    double normalizedDepthCeiling() const { return normalizedDepthCeiling_; }

private:
    void resetPrepared();
    void prepare(ResourceManager&, const ResourceLoadContext&);
    core::pmr::String sourceResourceId_{&core::defaultCpuMemoryResource()};
    std::filesystem::path projectRoot_, manifestPath_;
    ResourceGeneration sourceGeneration_ = 0;
    std::array<assets::MeshAsset, 2> candidates_;
    core::pmr::Vector<StaticMeshShadowLodSource> sources_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> reorderedOriginalIndices_{&core::defaultCpuMemoryResource()};
    std::uint32_t indexOrderVersion_ = 0;
    std::array<std::uint8_t, 32> manifestDigest_{};
    double shadowTexelCeiling_ = 0, normalizedDepthCeiling_ = 0;
};

} // namespace arp::resource
