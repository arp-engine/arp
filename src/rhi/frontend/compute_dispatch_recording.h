#pragma once

#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/compute_dispatch_validation_domain.h"
#include "rhi/semantics/compute_dispatch_recording.h"

namespace arp::rhi {

// Borrowed by the existing mixed projection. The preparation request owns the
// same canonical domain; this input adds no inventory, journal or executor.
struct OriginalComputeDispatchRecordingInput {
    const GpuDevice& device;
    const std::shared_ptr<const ComputeDispatchValidationDomain>& domain;
};

class OriginalComputeDispatchRecordingAdmission final {
public:
    // Value custody for the existing command owner. Construction authenticates
    // the device while it is alive; later observations never borrow its facade.
    // These are two direct retained slots, not the complete dependency closure.
    class Frozen final {
        friend class OriginalComputeDispatchRecordingAdmission;

    public:
        static constexpr std::uint32_t directRetainedOwnerSlots = 2u;
        Frozen(const Frozen&) = default;
        Frozen(Frozen&&) = default;

        bool nativeExecutionProven() const noexcept { return false; }

        const char* diagnostic() const noexcept
        {
            if (failure_)
                return failure_;
            auto* id = OriginalCommandComputePipelineSource::validateOriginal(source_);
            if (!id) {
                const auto& creation = source_->originalPipeline()->originalComputeCreation();
                id = OriginalComputePipelineCreation::validateOriginal(creation);
                if (!id) {
                    const auto& entry = creation->definition().computeShader;
                    const Extent3D current{entry.computeWorkgroupSizeX,
                        entry.computeWorkgroupSizeY,
                        entry.computeWorkgroupSizeZ};
                    if (current != workgroup_)
                        id = "RHI-DISPATCH-RECORDING-FROZEN-LOCAL-SIZE";
                }
            }
            if (!id)
                id = inspectRetainedDomain(domain_, workgroup_, groups_);
            // Once the retained closure has been observed revoked, this value
            // cannot become a fresh recording observation after quota recovery.
            if (id)
                failure_ = id;
            return id;
        }

        // The original command position supplies this exact Source and tuple.
        // A different valid Source or covered tuple is still a substitution.
        const char* diagnostic(
            const std::shared_ptr<const OriginalCommandComputePipelineSource>& source,
            Extent3D groups) const noexcept
        {
            if (const auto* id = diagnostic())
                return id;
            return core::SharedOwnerProjection::sameOwner(source_, source) && groups_ == groups
                ? nullptr
                : "RHI-DISPATCH-RECORDING-FROZEN-INPUT";
        }

    private:
        Frozen(std::shared_ptr<const OriginalCommandComputePipelineSource> source,
            std::shared_ptr<const ComputeDispatchValidationDomain> domain,
            Extent3D workgroup,
            Extent3D groups) noexcept
            : source_(std::move(source)), domain_(std::move(domain)), workgroup_(workgroup),
              groups_(groups)
        {}

        const std::shared_ptr<const OriginalCommandComputePipelineSource> source_;
        const std::shared_ptr<const ComputeDispatchValidationDomain> domain_;
        const Extent3D workgroup_, groups_;
        mutable const char* failure_ = nullptr;
    };

    static const char* inspect(const OriginalComputeDispatchRecordingInput& input,
        const std::shared_ptr<const OriginalCommandComputePipelineSource>& source,
        Extent3D groups) noexcept
    {
        const auto decision =
            semantics::ComputeDispatchRecordingContract::lowering(input.device.backend());
        if (!decision.inspectionAvailable || decision.diagnostic)
            return decision.diagnostic;
        if (const auto* id =
                OriginalCommandComputePipelineSource::validateOriginal(source, input.device))
            return id;
        const auto& creation = source->originalPipeline()->originalComputeCreation();
        if (const auto* id = OriginalComputePipelineCreation::validateOriginal(creation))
            return id;
        const auto& entry = creation->definition().computeShader;
        return input.device.validateOriginalComputeDispatchDomain(input.domain,
            {entry.computeWorkgroupSizeX, entry.computeWorkgroupSizeY, entry.computeWorkgroupSizeZ},
            groups);
    }

    static std::optional<Frozen> freeze(const OriginalComputeDispatchRecordingInput& input,
        const std::shared_ptr<const OriginalCommandComputePipelineSource>& source,
        Extent3D groups,
        const char*& diagnostic) noexcept
    {
        diagnostic = inspect(input, source, groups);
        if (diagnostic)
            return std::nullopt;
        const auto& entry =
            source->originalPipeline()->originalComputeCreation()->definition().computeShader;
        return Frozen(source,
            input.domain,
            {entry.computeWorkgroupSizeX, entry.computeWorkgroupSizeY, entry.computeWorkgroupSizeZ},
            groups);
    }

private:
    static const char* inspectRetainedDomain(
        const std::shared_ptr<const ComputeDispatchValidationDomain>& domain,
        Extent3D workgroup,
        Extent3D groups) noexcept
    {
        if (!domain || !domain->sharedOwnerMatches(domain))
            return "RHI-DISPATCH-DOMAIN-OWNER";
        if (const auto* id = domain->diagnostic())
            return id;
        // The canonical, immutable pair was joined to the device by inspect().
        // Recheck that domain's original coverage, without exposing its facts.
        return ComputeDispatchValidationContract::contains(domain->request_, workgroup, groups);
    }
};

} // namespace arp::rhi
