#pragma once
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_single_entry_input.h"

namespace arp::rhi_webgpu {
// A backend-private cold one-to-many projection. It grants no device/native
// actor authority and cannot be used as WebGpuBindingSpace/5.
class WebGpuOriginalDepthTableExpansionBindingSpace final
    : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalDepthTableExpansionBindingSpace;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr core::StringView contractVersion =
        "WebGpuOriginalDepthTableExpansionBindingSpace/1";

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCpuMetadataBytes = 0u;
        std::uint32_t maxPhysicalBindings = 0u;
    };

    struct Entry {
        rhi::ShaderReflectionBinding logical;
        std::uint32_t arrayElement = 0u, physicalSet = 0u, physicalBinding = 0u, group = 0u,
                      binding = 0u, physicalCardinality = 0u;
        core::pmr::String physicalName{&core::defaultCpuMemoryResource()};
    };

    WebGpuOriginalDepthTableExpansionBindingSpace(ConstructionKey,
        Budget budget,
        std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> input) noexcept
        : budget_(budget), compute_(std::move(input))
    {}

    WebGpuOriginalDepthTableExpansionBindingSpace(ConstructionKey,
        Budget budget,
        std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> input) noexcept
        : budget_(budget), graphics_(std::move(input))
    {}

    WebGpuOriginalDepthTableExpansionBindingSpace(
        const WebGpuOriginalDepthTableExpansionBindingSpace&) = delete;
    static std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace> prepare(
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String&);
    static std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace> prepare(
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String&);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-TABLE-SPACE-OWNER";
        if (self->originalMetadataQuota()->limitBytes() != self->budget_.maxCpuMetadataBytes)
            return "RHI-WEBGPU-TABLE-SPACE-QUOTA-LIMIT";
        return self->compute_
            ? rhi::OriginalSingleEntryPipelineInput::validateTableExpansion(self->compute_)
            : rhi::OriginalGraphicsPipelineInput::validateTableExpansion(self->graphics_);
    }

    const auto& computeInput() const noexcept { return compute_; }

    const auto& graphicsInput() const noexcept { return graphics_; }

    const auto& quota() const noexcept { return originalMetadataQuota(); }

    const Entry& entry(std::size_t i) const { return entries_.at(i); }

    std::uint32_t entryCount() const noexcept { return count_; }

    std::uint32_t physicalGroupCount() const noexcept { return groupCount_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    template <class Input>
    static std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace> prepareImpl(
        const std::shared_ptr<const Input>&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String&);
    void populate(
        const shader::OriginalDepthTableExpansionArtifact&, const rhi::ShaderPackageDesc&);
    const Budget budget_;
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> compute_;
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> graphics_;
    // Closed-program maximum: three expanded depth elements and Compute Output.
    std::array<Entry, 4> entries_;
    std::uint32_t count_ = 0u, groupCount_ = 0u;
};
} // namespace arp::rhi_webgpu
