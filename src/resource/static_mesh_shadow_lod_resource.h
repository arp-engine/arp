#pragma once

#include "assets/mesh_shadow_lod_source.h"
#include "resource/mesh_asset_resources.h"

#include <array>
#include <optional>

namespace arp::resource {

// Immutable, validated source metadata copied into a prepared render session.
// Each range belongs to its own resident level; no cross-level offset equality.
using StaticMeshShadowLodSource = assets::MeshShadowLodSource;

// Acquisition budgets are independent of the retained geometry/decode budget.
struct StaticMeshShadowLodSourceDesc {
    core::pmr::String projectRoot;
    core::pmr::String manifestPath;
    ResourceFileOrigin origin;
    std::size_t maxSourceBytes = 0;
    std::uint64_t maxImportBytes = 0;
    std::uint32_t maxJsonDepth = 0, maxJsonValues = 0;
};

// StaticMeshShadowLodAsset/1 (unpacked) and /2 (explicit source index order).
// Explicit Prototype; source descriptors support staged asynchronous acquisition.
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
    StaticMeshShadowLodResource(core::pmr::String resourceId,
        core::pmr::String sourceResourceId,
        StaticMeshShadowLodSourceDesc source);

    ResourceLoadResult load(ResourceManager&, const ResourceLoadContext&) override;
    bool planFileReadIntents(ResourceManager&,
        const ResourceLoadContext&,
        core::pmr::Vector<ResourceFileReadIntent>&,
        core::pmr::String&) const override;
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
    void validateSourceDesc() const;
    void observeOriginal(ResourceManager&) const;
    core::pmr::String sourceResourceId_{&core::defaultCpuMemoryResource()};
    std::filesystem::path projectRoot_, manifestPath_;
    std::optional<StaticMeshShadowLodSourceDesc> sourceDesc_;
    core::SharedPtr<core::CpuMemoryQuota> importQuota_;
    mutable const ResourceManager* plannedManager_ = nullptr;
    mutable ResourceGeneration plannedGeneration_ = 0;
    ResourceGeneration sourceGeneration_ = 0;
    std::array<assets::MeshAsset, 2> candidates_;
    core::pmr::Vector<StaticMeshShadowLodSource> sources_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> reorderedOriginalIndices_{&core::defaultCpuMemoryResource()};
    std::uint32_t indexOrderVersion_ = 0;
    std::array<std::uint8_t, 32> manifestDigest_{};
    double shadowTexelCeiling_ = 0, normalizedDepthCeiling_ = 0;
};

} // namespace arp::resource
