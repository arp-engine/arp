#pragma once

#include "core/container/array_view.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/semantics/ordinary_sampled_color.h"

namespace arp::rhi {
class OriginalTextureUploadPublication;

// GeneralWrite1 (whole) and GeneralWrite2 (whole/finite) publish one complete
// scalar group through the existing scoped
// candidate transaction. Creation evidence is not command/completion authority.
struct OriginalGeneralDescriptorWriteRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxBindings = 0u, maxRetainedOwnerSlots = 0u, maxCallbacks = 0u;
    std::array<std::uint64_t, 3u> maxCpuBytes{};
    std::uint64_t maxRetainedAllocationBytes = 0u;
    // Independent TypedNativeLossJoin/4; registry10 alone grants no group join.
    std::uint32_t lossGroupJoinVersion = 0u;
    bool lossGroupJoinOptIn = false;
    semantics::OriginalOrdinarySampledColorContract::Intent sampledColor;
    core::ArrayView<std::shared_ptr<const OriginalTextureUploadPublication>> sampledUploads;
};

class OriginalGeneralDescriptorWriteContract final {
public:
    using Shared = OriginalFiniteDescriptorWriteContract;
    using Facts = Shared::Facts;
    using Decision = Shared::Decision;
    static constexpr auto callbackCount = Shared::callbackCount;
    static constexpr auto quotaCount = Shared::quotaCount;
    // Existing state/group/pipeline/arena/quota/callback custody plus the opaque
    // general declaration owner. Per binding reserves the complete view-parent
    // closure (the largest canonical dependency), its three metadata quota
    // owners, three native observation slots and the published native binding.
    static constexpr auto fixedOwnerSlots = Shared::fixedOwnerSlots + 1u;
    static constexpr auto bindingOwnerSlots =
        OriginalCommandTextureViewSource::completeRetainedOwnerCount + 3u + 3u + 1u;

    // Synchronous native textures retain two payload owners plus two complete
    // device/object/allocation identity observations (three owners each), plus
    // the published binding identity captured by native group preparation.
    static constexpr auto synchronousBindingOwnerSlots =
        OriginalCommandTextureViewSource::completeRetainedOwnerCount + 3u + 2u + 2u * 3u + 1u + 3u;

    // The retained native pipeline payload plus its device/state identity and
    // selected shader library/function owners are fixed synchronous custody.
    static constexpr auto synchronousFixedOwnerSlots = fixedOwnerSlots + 1u + 2u + 3u;

    static constexpr std::uint64_t ownerSlots(
        std::uint32_t bindings, std::uint32_t version = 1u) noexcept
    {
        return std::uint64_t(version >= 3u ? synchronousFixedOwnerSlots : fixedOwnerSlots)
            + std::uint64_t(bindings)
            * (version >= 3u ? synchronousBindingOwnerSlots : bindingOwnerSlots);
    }

    static constexpr const char* validate(const OriginalGeneralDescriptorWriteRequest& r) noexcept
    {
        if (const auto* id =
                semantics::OriginalOrdinarySampledColorContract::validate(r.sampledColor))
            return id;
        if (!r.sampledColor.empty() && r.contractVersion != 3u)
            return "RHI-ORDINARY-SAMPLED-COLOR-GROUP-PROFILE";
        if (r.sampledColor.empty() && !r.sampledUploads.empty())
            return "RHI-ORDINARY-SAMPLED-COLOR-UPLOAD-PROFILE";
        if (!r.sampledColor.empty() && r.sampledUploads.empty())
            return "RHI-ORDINARY-SAMPLED-COLOR-UPLOAD-COVERAGE";
        if (!r.sampledUploads.empty()
            && (!r.sampledUploads.data() || r.sampledUploads.size() > r.maxBindings))
            return "RHI-ORDINARY-SAMPLED-COLOR-UPLOAD-CAPACITY";
        if (r.contractVersion != 1u && r.contractVersion != 2u && r.contractVersion != 3u
            && r.contractVersion != 4u)
            return "RHI-ORIGINAL-GENERAL-WRITE-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-GENERAL-WRITE-OPT-IN";
        if ((r.lossGroupJoinVersion || r.lossGroupJoinOptIn)
            && (r.contractVersion < 3u || r.lossGroupJoinVersion != 4u || !r.lossGroupJoinOptIn))
            return "RHI-ORIGINAL-GENERAL-WRITE-LOSS-JOIN-INTENT";
        if (!r.maxBindings || ownerSlots(r.maxBindings, r.contractVersion) > r.maxRetainedOwnerSlots
            || r.maxCallbacks != (r.contractVersion >= 3u ? 0u : callbackCount)
            || !r.maxRetainedAllocationBytes)
            return "RHI-ORIGINAL-GENERAL-WRITE-BUDGET";
        for (const auto bytes : r.maxCpuBytes)
            if (!bytes)
                return "RHI-ORIGINAL-GENERAL-WRITE-CPU-BUDGET";
        return nullptr;
    }

    static constexpr Decision decide(Backend backend, Facts f = {}) noexcept
    {
        return semantics::DescriptorLowering::OriginalGeneralDescriptorWrite_decide<Decision>(
            backend, f);
    }

    // GeneralWrite3 is synchronous, cold, fresh-set-only and read-only Graphics.
    // It has no WebGPU error-scope participants and grants no command authority.
    static constexpr Decision synchronousGraphicsLowering(Backend backend) noexcept
    {
        return semantics::DescriptorLowering::OriginalSynchronousGraphicsWrite_lowering<Decision>(
            backend);
    }

    // /4 reuses the fresh-set atomic transaction for the selected Compute
    // entry, including full finite storage-buffer read/write footprints.
    // Publication alone grants neither Dispatch nor initialization authority.
    static constexpr Decision synchronousComputeLowering(Backend backend) noexcept
    {
        return semantics::DescriptorLowering::OriginalSynchronousComputeWrite_lowering<Decision>(
            backend);
    }
};
} // namespace arp::rhi
