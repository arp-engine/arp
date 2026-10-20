#pragma once
#include "webgpu_map_handoff_fixture.h"
#include "webgpu_original_texture_readback_pair_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackDeclarationFixture final {
public:
    using Pair = WebGpuOriginalTextureReadbackPairFixture::Pair;

    static void verifyContract()
    {
        Pair::Mip output, sentinel;
        sentinel.bufferOffsetBytes = UINT64_MAX;
        output = sentinel;
        assert(std::string_view(Pair::mapMipOriginal({}, {}, 0u, {}, nullptr, output))
                == "RHI-READBACK-PAIR-OWNER"
            && output.matches(sentinel));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
            assert(Pair::lowering(backend).diagnostic
                && Pair::lowering(backend).nativeFactBindingImplemented
                    == (backend == rhi::Backend::WebGPU));
        std::puts(
            "BINDLESS_RHI_READBACK_DECLARATION_CONTRACT_OK immutable-original strict-initial no-access-authority");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyMap(const std::shared_ptr<const Pair>& pair)
    {
        const auto& destination = *pair->destination();
        WebGpuMapHandoffFixture::verifyMapStateSeparation(destination.nativeResource()->buffer,
            destination.unpublishedResultCapacityBytes(),
            [&] {
                const auto previous =
                    std::pmr::set_default_resource(std::pmr::null_memory_resource());
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(!pair->validateReadyDeclaration() && !destination.validateReadyDeclaration()
                    && !destination.source()->validateReadyDeclaration());
                assert(
                    std::string_view(pair->validate()) == "RHI-READBACK-DESTINATION-NATIVE-LAYOUT"
                    && !destination.resultPublished());
                Pair::Mip output, sentinel;
                sentinel.bufferOffsetBytes = UINT64_MAX;
                output = sentinel;
                assert(Pair::mapMipOriginal(pair,
                           pair->layout()->inputOwner(),
                           0u,
                           pair->layout()->mipCopies()[0],
                           destination.nativeResource()->ownerDevice,
                           output)
                    && output.matches(sentinel));
                assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                std::pmr::set_default_resource(previous);
            });
        assert(!pair->validate());
        std::puts(
            "BINDLESS_RHI_READBACK_DECLARATION_MAP_OK actual-pending-mapped-unmapped strict-copy-gate allocation0 unpublished");
    }

    // This intentionally stales the source's initial state. Run after all cold
    // preparation; no future factory may adopt this as unchanged initial state.
    static void verifyPendingAndRollback(const std::shared_ptr<const Pair>& pair)
    {
        const auto& destination = *pair->destination();
        auto& target = *destination.source()->resource().texture;
        auto& buffer = *destination.nativeResource();
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        core::MutationJournal journal, foreign;
        assert(!target.recordState(rhi::ResourceState::CopySrc, &journal)
            && !buffer.recordState(rhi::ResourceState::CopyDst, &journal));
        const auto targetPending = target.recordedState().observePending(journal);
        const auto bufferPending = buffer.recordedState().observePending(journal);
        assert(targetPending && bufferPending
            && target.recordedState().matchesPending(*targetPending, journal)
            && buffer.recordedState().matchesPending(*bufferPending, journal)
            && !target.recordedState().matchesPending(*targetPending, foreign));
        assert(!pair->validateReadyDeclaration() && !destination.validateReadyDeclaration()
            && !destination.source()->validateReadyDeclaration());
        assert(std::string_view(pair->validate()) == "RHI-READBACK-SOURCE-STATE-PENDING"
            && pair->executionDiagnostic());
        auto originalBytes = buffer.sizeBytes;
        ++buffer.sizeBytes;
        assert(pair->validateReadyDeclaration());
        buffer.sizeBytes = originalBytes;
        auto originalDimension = target.dimension;
        target.dimension = WGPUTextureDimension_1D;
        assert(pair->validateReadyDeclaration());
        target.dimension = originalDimension;
        auto originalAllocation = buffer.allocationReleaseToken;
        buffer.allocationReleaseToken.reset();
        assert(pair->validateReadyDeclaration());
        buffer.allocationReleaseToken = std::move(originalAllocation);
        assert(!pair->validateReadyDeclaration());
        journal.rollback();
        assert(!target.recordedState().pending() && !buffer.recordedState().pending()
            && !pair->validateReadyDeclaration());
        assert(!target.recordedState().matchesPending(*targetPending, journal)
            && std::string_view(pair->validate()) == "RHI-READBACK-SOURCE-STATE-STALE"
            && !destination.resultPublished());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        std::puts(
            "BINDLESS_RHI_READBACK_DECLARATION_NATIVE_OK owned-pending rollback-stale immutable-identity native-mutation allocation0 no-proof");
    }

    static void verifyInvalidation(const std::shared_ptr<const Pair>& pair)
    {
        pair->destination()->nativeResource()->invalidateRecordedState();
        assert(std::string_view(pair->validateReadyDeclaration())
            == "RHI-READBACK-DESTINATION-IDENTITY-CHANGED");
    }

    static void verifyLoss(const std::shared_ptr<const Pair>& pair)
    {
        assert(std::string_view(pair->validateReadyDeclaration())
                == "RHI-READBACK-DESTINATION-DEVICE-LOST"
            && std::string_view(pair->destination()->source()->validateReadyDeclaration())
                == "RHI-READBACK-SOURCE-DEVICE-LOST");
        std::puts(
            "BINDLESS_RHI_READBACK_DECLARATION_LOSS_OK actual-device-loss exact-original-retirement no-authority");
    }
#endif
};
} // namespace arp::test
