#pragma once
#include "rhi/backends/webgpu/webgpu_original_query_owner.h"
#include "rhi/backends/webgpu/webgpu_original_query_source_gate.h"

namespace arp::rhi_webgpu {
struct WebGpuOriginalQueryBindingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxNativeOwners = 0u, maxQuerySlots = 0u, maxScopeIds = 0u;
    std::uint64_t maxLabelBytes = 0u;
};

// A fixed-size immutable value retains original quota-bound graphs. No new storage.
class WebGpuOriginalQueryBinding final {
public:
    struct Lowering {
        bool validatorOnly = false, declarationImplemented = false;
        const char* diagnostic = "RHI-QUERY-BINDING-BACKEND";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-QUERY-BINDING-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {false, false, "RHI-QUERY-BINDING-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-QUERY-BINDING-EXECUTION-UNPROVEN"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    static const char* validateBudget(const WebGpuOriginalQueryBindingBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-QUERY-BINDING-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-QUERY-BINDING-OPT-IN";
        if (!b.maxNativeOwners || !b.maxQuerySlots)
            return "RHI-QUERY-BINDING-BUDGET";
        return nullptr;
    }

    static std::optional<WebGpuOriginalQueryBinding> captureOriginal(
        const std::optional<rhi::QueryPoolOriginalInputArguments>& input,
        std::shared_ptr<const WebGpuOriginalQueryOwner> owner,
        const WebGpuOriginalQueryBindingBudget& b,
        const char*& diagnostic) noexcept
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        diagnostic = validateOriginalDeclaration(input, owner);
        if (diagnostic)
            return {};
        const auto& original = owner->originalInput();
        if (original->scalarFields().count() > b.maxQuerySlots
            || original->scopeIds().size() > b.maxScopeIds
            || original->label().size() > b.maxLabelBytes) {
            diagnostic = "RHI-QUERY-BINDING-CAPACITY";
            return {};
        }
        return WebGpuOriginalQueryBinding(*input, std::move(owner));
    }

    const auto& originalCommand() const noexcept { return input_; }

    const auto& originalOwner() const noexcept { return owner_; }

    const char* validateReady() const noexcept
    {
        if (const auto* id = validateOriginalDeclaration(input_, owner_))
            return id;
        return WebGpuOriginalQueryOwner::validateReadyOriginal(owner_);
    }

    bool matchesOriginal(const rhi::QueryPoolOriginalInputArguments& input,
        const std::shared_ptr<const WebGpuOriginalQueryOwner>& owner) const noexcept
    {
        return owner && owner->hasQuotaBoundSharedOwner(owner)
            && owner_->hasQuotaBoundSharedOwner(owner_)
            && core::SharedOwnerProjection::sameOwner(owner, owner_) && input_.matches(input);
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validateReady())
            return id;
        return WebGpuOriginalQuerySourceGate::admissionDiagnostic(
            WebGpuOriginalQuerySourceGate::decideOriginal(1u, true, input_));
    }

    bool recordingSupported() const noexcept { return false; }

private:
    WebGpuOriginalQueryBinding(rhi::QueryPoolOriginalInputArguments input,
        std::shared_ptr<const WebGpuOriginalQueryOwner> owner)
        : input_(std::move(input)), owner_(std::move(owner))
    {}

    static const char* validateOriginalDeclaration(
        const std::optional<rhi::QueryPoolOriginalInputArguments>& input,
        const std::shared_ptr<const WebGpuOriginalQueryOwner>& owner) noexcept
    {
        if (!input)
            return "RHI-QUERY-BINDING-INPUT";
        if (const auto* id = input->validateDeclaration())
            return id;
        if (!owner || !owner->hasQuotaBoundSharedOwner(owner))
            return "RHI-QUERY-BINDING-OWNER";
        if (!owner->decision().creationEligible()
            || !owner->decision().matchesOriginal(input->originalInput()))
            return "RHI-QUERY-BINDING-ORIGINAL-INPUT";
        switch (owner->status()) {
        case WebGpuOriginalQueryOwner::Status::Prepared:
        case WebGpuOriginalQueryOwner::Status::Pending:
        case WebGpuOriginalQueryOwner::Status::CreationReady:
            return nullptr;
        case WebGpuOriginalQueryOwner::Status::Rejected:
        case WebGpuOriginalQueryOwner::Status::DeviceLost:
            return owner->validate();
        }
        return "RHI-QUERY-BINDING-STATUS";
    }

    const rhi::QueryPoolOriginalInputArguments input_;
    const std::shared_ptr<const WebGpuOriginalQueryOwner> owner_;
};
} // namespace arp::rhi_webgpu
