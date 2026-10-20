#pragma once
#include "rhi/frontend/command_model.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/command_input_admission_lowering.h"
#include "rhi/semantics/descriptor_lowering.h"

#include <algorithm>

// region original_descriptor_native_identity
namespace arp::rhi {
// OriginalDescriptorNativeIdentity/1: creation metadata only. This never grants
// native recording, submission or execution authority, even for a real payload.
struct OriginalDescriptorNativeIdentityRequest {
    enum class Profile {
        None,
        FiniteWrite1,
        WholeWrite1,
        FiniteTargetMemory1,
        GeneralWrite1,
        GeneralWrite2
    };
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    Profile profile = Profile::None;
};

class OriginalDescriptorNativeIdentityContract final {
public:
    using Request = OriginalDescriptorNativeIdentityRequest;

    static constexpr const char* validateIntent(const Request& request) noexcept
    {
        if (request.contractVersion != 1u
            && !(request.contractVersion == 2u
                && request.profile == Request::Profile::FiniteTargetMemory1)
            && !(
                request.contractVersion == 3u && request.profile == Request::Profile::GeneralWrite1)
            && !(request.contractVersion == 4u
                && request.profile == Request::Profile::GeneralWrite2))
            return "RHI-ORIGINAL-GROUP-IDENTITY-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-GROUP-IDENTITY-OPT-IN";
        if ((request.contractVersion == 1u && request.profile != Request::Profile::FiniteWrite1
                && request.profile != Request::Profile::WholeWrite1)
            || (request.contractVersion == 2u
                && request.profile != Request::Profile::FiniteTargetMemory1)
            || (request.contractVersion == 3u && request.profile != Request::Profile::GeneralWrite1)
            || (request.contractVersion == 4u
                && request.profile != Request::Profile::GeneralWrite2))
            return "RHI-ORIGINAL-GROUP-IDENTITY-PROFILE";
        return nullptr;
    }

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, identityAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-GROUP-IDENTITY-BACKEND";
    };

    // Version-controlled original creation contract, not device capability or
    // an ordinary descriptor observer. Real instance validation is mandatory.
    static constexpr Decision lowering(Backend backend, Request::Profile profile) noexcept
    {
        return semantics::DescriptorLowering::OriginalDescriptorNativeIdentity_lowering<Decision>(
            backend, profile);
    }
};

// endregion original_descriptor_native_identity

// region original_render_descriptor_usage

// OriginalRenderDescriptorUsage1: complete render-binding buffer references,
// independent of Draw consumption, state mutation and native execution.
class OriginalRenderDescriptorUsageContract final {
public:
    using Budget = OriginalRenderDescriptorUsageBudget;

    using Decision = arp::rhi::semantics::OriginalCommandDescriptorConsumptionLowering::Decision;

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::DescriptorLowering::OriginalRenderDescriptorUsage_lowering<Decision>(
            backend, version);
    }

    static constexpr const char* validateBudget(const Budget& b) noexcept { return b.diagnostic(); }
};

// endregion original_render_descriptor_usage

// region descriptor_consumption

// Immutable owning projection of the L2 facts used at draw/dispatch consumption.
// No names, nested PMR copies, or native facts participate in this projection.
class DescriptorConsumptionContract final {
public:
    DescriptorConsumptionContract(
        const PipelineLayoutDesc& layout, const ShaderReflectionDesc& reflection)
        : bindings_(reflection.descriptorBindings.begin(),
              reflection.descriptorBindings.end(),
              &core::defaultCpuMemoryResource())
    {
        pushRanges_.assign(layout.pushConstantRanges.begin(), layout.pushConstantRanges.end());
        sets_.reserve(layout.descriptorSets.size());
        tables_.reserve(layout.descriptorTables.size());
        for (const auto& slot : layout.descriptorSets)
            sets_.push_back(slot.set);
        for (const auto& slot : layout.descriptorTables)
            tables_.push_back(slot.set);
    }

    DescriptorConsumptionContract(const DescriptorConsumptionContract&) = delete;
    DescriptorConsumptionContract& operator=(const DescriptorConsumptionContract&) = delete;
    DescriptorConsumptionContract(DescriptorConsumptionContract&&) noexcept = default;

    const core::pmr::Vector<ShaderReflectionBinding>& bindings() const noexcept
    {
        return bindings_;
    }

    bool matchesBindings(const ShaderReflectionDesc& reflection) const noexcept
    {
        const auto& current = reflection.descriptorBindings;
        return bindings_.size() == current.size()
            && std::equal(bindings_.begin(),
                bindings_.end(),
                current.begin(),
                [](const auto& a, const auto& b) {
                    return a.set == b.set && a.binding == b.binding && a.type == b.type
                        && a.stages == b.stages && a.arrayCount == b.arrayCount
                        && a.subpassAttachmentIndex == b.subpassAttachmentIndex
                        && a.depthTexture == b.depthTexture;
                });
    }

    const core::pmr::Vector<PushConstantRange>& pushRanges() const noexcept { return pushRanges_; }

    bool declaresSet(std::uint32_t set) const noexcept
    {
        return std::find(sets_.begin(), sets_.end(), set) != sets_.end();
    }

    bool declaresTable(std::uint32_t set) const noexcept
    {
        return std::find(tables_.begin(), tables_.end(), set) != tables_.end();
    }

private:
    core::pmr::Vector<ShaderReflectionBinding> bindings_;
    core::pmr::Vector<PushConstantRange> pushRanges_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> sets_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> tables_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::rhi

// endregion descriptor_consumption
