#pragma once
#include "rhi/backends/metal/metal_prepared_function_ownership.h"
#include "rhi/backends/metal/metal_zero_base_indirect_source.h"

namespace arp::rhi_metal {
// Six queried scalar bindings from an explicitly selected Emulated package.
// No descriptor packet, output generation lease or execution authority.
class MetalRhiZeroBaseIndirectPipeline final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiZeroBaseIndirectPipeline;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using RoleContract = rhi::CanonicalZeroBaseIndirectSourceContract;
    using Facts = std::array<MetalRhiBufferBindingFact, RoleContract::bindingCount>;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxChildOwners = 0u;
    };

    MetalRhiZeroBaseIndirectPipeline(ConstructionKey,
        std::shared_ptr<const MetalRhiZeroBaseIndirectSource> source,
        core::UniquePtr<rhi::ShaderPackageDesc> interface,
        core::UniquePtr<rhi::PipelineLayout> layout,
        std::shared_ptr<MetalRhiNativePipelinePayload> native,
        Facts facts,
        Budget budget)
        : source_(std::move(source)), interface_(std::move(interface)), layout_(std::move(layout)),
          native_(std::move(native)), facts_(facts), budget_(budget)
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiZeroBaseIndirectPipeline(const MetalRhiZeroBaseIndirectPipeline&) = delete;
    static std::shared_ptr<const MetalRhiZeroBaseIndirectPipeline> prepare(
        std::shared_ptr<const MetalRhiZeroBaseIndirectSource>,
        const rhi::semantics::NativeBackendCapabilityContract&,
        const MetalRhiOrdinaryBindingBudget&,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;

    const auto& source() const noexcept { return source_; }

    const auto& native() const noexcept { return native_; }

    const auto& facts() const noexcept { return facts_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-ZERO-BASE-PIPELINE-PREPARATION-ONLY";
    }

private:
    static const char* capture(const MetalRhiZeroBaseIndirectSource&,
        const MetalRhiNativePipelinePayload&,
        Facts&) noexcept;

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(source_);
        for (std::size_t i = 0u; i < source_->childOwnerCount(); ++i)
            visit(source_->childOwnerAt(i));
        for (auto& owner : MetalRhiPreparedFunctionOwnership::pipelineOwners(native_))
            if (owner)
                visit(std::move(owner));
    }

    std::shared_ptr<const MetalRhiZeroBaseIndirectSource> source_;
    core::UniquePtr<rhi::ShaderPackageDesc> interface_;
    core::UniquePtr<rhi::PipelineLayout> layout_;
    std::shared_ptr<MetalRhiNativePipelinePayload> native_;
    Facts facts_;
    Budget budget_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
