#pragma once
#include "core/ownership/shared_owner_projection.h"
#include "rhi/frontend/resource_device_lifetime.h"
#include "rhi/frontend/resource_device_native_loss.h"
#include "rhi/frontend/resource_registration.h"
#include "rhi/semantics/resource_lowering.h"

// region Resource native loss observation
namespace arp::test {
class OriginalObservedNativeLossBindingFixture;
}

namespace arp::rhi_webgpu {
class WebGpuCanonicalCallbackCustody;
}

namespace arp::rhi {
// OriginalNativeLossObservation/1: device-issued, read-only custody of the exact
// already-armed subscription and logical domain. It retains no GpuDevice and
// exposes no native handle, callback receiver, detach operation or GPU proof.
// Copy/observation/destruction allocate0 and require external serialization.
// Retention preserves terminal diagnostics after facade destruction; Shutdown/
// Retired/NativeLost remain distinct and never imply queue drain/completion.
class OriginalDeviceNativeLossObservation final {
    friend class GpuDevice;
    friend class arp::rhi_webgpu::WebGpuCanonicalCallbackCustody;
    friend class arp::test::OriginalObservedNativeLossBindingFixture;

public:
    static constexpr std::uint32_t contractVersion = 1u, retainedOwnerCount = 2u;
    OriginalDeviceNativeLossObservation() = default;

    const char* diagnostic() const noexcept
    {
        if (!domain_ || !binding_)
            return "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED";
        if (const auto* id = lifetime_.diagnostic())
            return id;
        if (!binding_->hasQuotaBoundSharedOwner(binding_))
            return "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-OWNER";
        return binding_->diagnostic();
    }

    const char* compareOriginal(const OriginalDeviceNativeLossObservation& other) const noexcept
    {
        if (const auto* id = diagnostic())
            return id;
        if (const auto* id = other.diagnostic())
            return id;
        return core::SharedOwnerProjection::sameOwner(domain_, other.domain_)
                && core::SharedOwnerProjection::sameOwner(binding_, other.binding_)
            ? nullptr
            : "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN";
    }

    bool nativeExecutionProven() const noexcept { return false; }

    // Read-only terminal diagnostics expose failed native suppression without
    // turning a logical terminal notification into completion authority.
    const char* terminalDiagnostic() const noexcept
    {
        return binding_ ? binding_->terminalDiagnostic()
                        : "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED";
    }

    bool overlapsMetadataOwner(const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        return binding_
            && core::SharedOwnerProjection::overlapsOwner(quota, binding_->originalMetadataQuota());
    }

private:
    OriginalDeviceNativeLossObservation(std::shared_ptr<OriginalDeviceLifetime> domain,
        OriginalDeviceLifetime::Ticket lifetime,
        std::shared_ptr<const OriginalDeviceNativeLossBinding> binding) noexcept
        : domain_(std::move(domain)), lifetime_(std::move(lifetime)), binding_(std::move(binding))
    {}

    const std::shared_ptr<OriginalDeviceLifetime> domain_;
    const OriginalDeviceLifetime::Ticket lifetime_;
    const std::shared_ptr<const OriginalDeviceNativeLossBinding> binding_;
};

// endregion Resource native loss observation

// region Resource native loss join validation
// region original_typed_native_loss_join

// Separate opt-in; a registration version or armed loss subscription alone
// never authorizes a typed native factory or scoped group.
struct OriginalTypedNativeLossJoinRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
};

class OriginalTypedNativeLossJoinContract final {
public:
    static constexpr std::uint32_t registrationVersion = 7u;
    static constexpr std::uint32_t tableRegistrationVersion = 8u;
    // /3 joins accepted synchronous pipelines and ordinary finite descriptors
    // to the same native-result loss subscription. It grants no scoped-group,
    // table, sampler, command or execution authority. /4 separately opts into
    // the same registry10's synchronous GeneralWrite3/4 complete-group join.
    static constexpr std::uint32_t synchronousRegistrationVersion = 10u;

    static constexpr const char* validate(const OriginalTypedNativeLossJoinRequest& request,
        const OriginalResourceRegistrationBudget& budget) noexcept
    {
        if (request.contractVersion != 1u && request.contractVersion != 2u
            && request.contractVersion != 3u && request.contractVersion != 4u)
            return "RHI-ORIGINAL-TYPED-LOSS-JOIN-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-TYPED-LOSS-JOIN-OPT-IN";
        if (budget.contractVersion
            != (request.contractVersion == 1u       ? registrationVersion
                    : request.contractVersion == 2u ? tableRegistrationVersion
                                                    : synchronousRegistrationVersion))
            return "RHI-ORIGINAL-TYPED-LOSS-REGISTRATION-VERSION";
        if (const auto* id = OriginalResourceRegistrationContract::validateIntent(budget))
            return id;
        if (!budget.maxPipelineLayouts
            || (!budget.maxComputePipelines && !budget.maxGraphicsPipelines))
            return "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-BUDGET";
        if (request.contractVersion >= 3u
            && (budget.maxDescriptorTables
                || (budget.samplers.requested()
                    && !OriginalResourceRegistrationContract::joinsSamplerNativeLoss(budget))
                || budget.heaps.requested()
                || (budget.queries.requested()
                    && !OriginalResourceRegistrationContract::joinsQueryNativeLoss(budget))))
            return "RHI-ORIGINAL-TYPED-LOSS-SYNC-PROFILE";
        if (request.contractVersion == 4u
            && (!budget.maxDescriptorArenas || !budget.maxDescriptorSets))
            return "RHI-ORIGINAL-TYPED-LOSS-SYNC-GROUP-BUDGET";
        if (request.contractVersion == 1u && budget.maxDescriptorTables)
            return "RHI-ORIGINAL-TYPED-LOSS-DESCRIPTOR-TABLE-CLOSED";
        if (request.contractVersion == 2u
            && (!budget.maxDescriptorTables || !budget.maxDescriptorArenas
                || budget.maxDescriptorSets))
            return "RHI-ORIGINAL-TABLE-REGISTRATION-DESCRIPTOR-BUDGET";
        return nullptr;
    }

    struct Decision {
        bool registrationAvailable = false, validatorOnly = false;
        bool acceptedPipelineJoinAvailable = false, scopedGroupJoinAvailable = false;
        bool recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TYPED-LOSS-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::ResourceLowering::OriginalTypedNativeLossJoin_lowering<Decision>(
            backend, version);
    }

    static constexpr const char* pipelineDiagnostic() noexcept
    {
        return "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-JOIN-MISSING";
    }

    static constexpr const char* groupDiagnostic() noexcept
    {
        return "RHI-ORIGINAL-TYPED-LOSS-GROUP-JOIN-MISSING";
    }
};

// Query registration joins the existing registry10 subscription only through
// this independent opt-in. Typed join /3 or /4 still grants no query execution.
// The original native candidate/runtime seal, bounded pool registration and
// original slot/completion journal remain required by their own contracts.
class OriginalQueryNativeLossJoinContract final {
public:
    struct Decision {
        bool validatorOnly = false, nativeSourceAvailable = false;
        bool recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-QUERY-LOSS-BACKEND";
    };

    static constexpr const char* validate(const OriginalResourceRegistrationBudget& budget) noexcept
    {
        if (const auto* id = OriginalResourceRegistrationContract::validateIntent(budget))
            return id;
        if (!OriginalResourceRegistrationContract::joinsQueryNativeLoss(budget))
            return "RHI-ORIGINAL-QUERY-LOSS-OPT-IN";
        return OriginalTypedNativeLossJoinContract::validate({3u, true}, budget);
    }

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::ResourceLowering::OriginalQueryNativeLossJoin_lowering<Decision>(backend);
    }
};

// Descriptor-only subscription join. Independent from the accepted-pipeline
// and scoped WebGPU group joins; no pipeline/table execution authority.
// Request /1 excludes samplers. Request /2 additionally admits independently
// opted-in original samplers to this same subscription; it grants no execution.
// Request /3 is the WebGPU original Root CPU descriptor factory join. It
// admits explicitly budgeted samplers and excludes pipeline/table registration. Physical groups are still
// authenticated independently by the original execution closure.
class OriginalDescriptorNativeLossRegistrationContract final {
public:
    static constexpr std::uint32_t registrationVersion = 9u;

    struct Decision {
        bool validatorOnly = false, registrationAvailable = false;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-DESCRIPTOR-LOSS-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::ResourceLowering::OriginalDescriptorNativeLossRegistration_lowering<
            Decision>(backend, version);
    }

    static constexpr const char* validate(const OriginalTypedNativeLossJoinRequest& request,
        const OriginalResourceRegistrationBudget& budget) noexcept
    {
        if (request.contractVersion != 1u && request.contractVersion != 2u
            && request.contractVersion != 3u)
            return "RHI-ORIGINAL-DESCRIPTOR-LOSS-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-DESCRIPTOR-LOSS-OPT-IN";
        if (budget.contractVersion != registrationVersion)
            return "RHI-ORIGINAL-DESCRIPTOR-LOSS-REGISTRATION-VERSION";
        if (const auto* id = OriginalResourceRegistrationContract::validateIntent(budget))
            return id;
        if (!budget.maxDescriptorArenas || !budget.maxDescriptorSets || budget.maxDescriptorTables
            || budget.maxPipelineLayouts || budget.maxComputePipelines
            || budget.maxGraphicsPipelines
            || (request.contractVersion == 1u && budget.samplers.requested()))
            return "RHI-ORIGINAL-DESCRIPTOR-LOSS-DOMAIN";
        if (request.contractVersion == 2u && !budget.samplers.requested())
            return "RHI-ORIGINAL-SAMPLER-LOSS-OPT-IN";
        return nullptr;
    }
};
} // namespace arp::rhi

// endregion original_typed_native_loss_join

// region original_sampler_native_loss_join
namespace arp::rhi {
// Sampler registration alone is CPU custody. Native loss requires the exact
// ordinary typed registration/subscription join and an authentic native source.
class OriginalSamplerNativeLossJoinContract final {
public:
    static constexpr const char* validateRegistration(
        const OriginalResourceRegistrationBudget& budget, std::uint32_t version = 1u) noexcept
    {
        if (version == 3u) {
            if (!OriginalResourceRegistrationContract::joinsSamplerNativeLoss(budget))
                return "RHI-ORIGINAL-SYNC-SAMPLER-LOSS-OPT-IN";
            return OriginalTypedNativeLossJoinContract::validate({3u, true}, budget);
        }
        if (version == 2u)
            return OriginalDescriptorNativeLossRegistrationContract::validate({2u, true}, budget);
        if (version != 1u)
            return "RHI-ORIGINAL-SAMPLER-LOSS-VERSION";
        if (budget.contractVersion != OriginalTypedNativeLossJoinContract::registrationVersion)
            return "RHI-ORIGINAL-SAMPLER-LOSS-REGISTRATION-PROFILE";
        if (!budget.samplers.requested())
            return "RHI-ORIGINAL-SAMPLER-LOSS-OPT-IN";
        if (const auto* id = budget.samplers.diagnostic())
            return id;
        return OriginalTypedNativeLossJoinContract::validate({1u, true}, budget);
    }

    struct Decision {
        std::uint32_t contractVersion = 0u;
        bool validatorOnly = false, nativeSourceAvailable = false;
        bool recordingAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-SAMPLER-LOSS-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::ResourceLowering::OriginalSamplerNativeLossJoin_lowering<Decision>(
            backend, version);
    }
};
} // namespace arp::rhi

// endregion original_sampler_native_loss_join
// endregion Resource native loss join validation
