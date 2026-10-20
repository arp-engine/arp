#pragma once
#include "rhi/frontend/resource_leases.h"
#include "rhi/gpu_resources.h"

namespace arp::rhi {
// Immutable original cell observations, not a completion or execution issuer.
// Each visible mip/layer may have a different content generation.
class ARP_API OriginalCompletedDepthTextureSource final
    : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCompletedDepthTextureSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCells = 0u, maxCpuMetadataBytes = 0u;
        std::uint32_t maxRetainedOwners = 0u;
    };

    static constexpr std::uint32_t completeRetainedOwnerCount =
        OriginalCommandTextureViewSource::completeRetainedOwnerCount + 1u;

    OriginalCompletedDepthTextureSource(ConstructionKey,
        std::shared_ptr<const OriginalCommandTextureViewSource> source,
        Budget budget)
        : source_(std::move(source)), budget_(budget)
    {}

    static std::shared_ptr<const OriginalCompletedDepthTextureSource> prepareOriginal(
        std::shared_ptr<const OriginalCommandTextureViewSource>,
        Budget,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic);
    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCompletedDepthTextureSource>&) noexcept;
    const char* validate() const noexcept;

    const auto& originalViewSource() const noexcept { return source_; }

    const auto& cells() const noexcept { return cells_; }

    const auto& budget() const noexcept { return budget_; }

    // Source1 observes canonical cell control2. Independent Source2 observes
    // one queried-allocation control4 cell; neither profile issues completion.
    static constexpr const char* validateControl(
        std::uint32_t version, std::uint32_t control) noexcept
    {
        return (version == 1u && control == 2u) || (version == 2u && control == 4u)
            ? nullptr
            : "RHI-COMPLETED-DEPTH-SOURCE-CONTENT";
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* inspect(
        const OriginalCommandTextureViewSource&, const Budget&, std::uint64_t& cellCount) noexcept;
    const std::shared_ptr<const OriginalCommandTextureViewSource> source_;
    const Budget budget_;
    core::pmr::Vector<GpuTexture::ContentRangeExpectation> cells_{
        &core::defaultCpuMemoryResource()};
};
} // namespace arp::rhi
