#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/resource_texture_validation.h"
#include "rhi/frontend/resource_whole_state_view.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class NormalizedTextureStateCreationFixture final {
    using Contract = rhi::NormalizedTextureStateCreationContract;
    using Control = rhi::common::RecordedTextureStateControl;
    using View = rhi::common::RecordedWholeResourceStateView;
    using S = rhi::ResourceState;

public:
    static void verify()
    {
        rhi::TextureDesc desc;
        desc.extent = {4u, 4u, 1u};
        desc.mipLevelCount = 2u;
        desc.arrayLayerCount = 2u;
        desc.format = rhi::TextureFormat::Depth24Stencil8;
        desc.initialState = S::CopySrc;
        const char* diagnostic = nullptr;
        auto plan = Contract::inspect(desc, 8u, diagnostic);
        assert(plan && !diagnostic && plan->geometry.cellCount() == 8u);
        assert(plan->storageBytes == 2u * 8u * sizeof(S));
        assert(!Contract::inspect(desc, 7u, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-TEXTURE-RANGE-CELL-CAPACITY");
        const auto make = [&](const std::shared_ptr<core::CpuMemoryQuota>& quota) {
            bool success = false;
            try {
                core::CpuMemoryQuotaScope scope(quota);
                Control control(desc, desc.initialState);
                success = true;
            } catch (const std::bad_alloc&) {
            }
            return success;
        };
        for (const auto capacity :
            {plan->storageBytes, plan->storageBytes - 1u, std::uint64_t{1u}}) {
            auto quota = core::makeShared<core::CpuMemoryQuota>(capacity);
            assert(make(quota) == (capacity == plan->storageBytes));
            assert(!quota->liveBytes());
        }
        // One explicit pressure-lease byte allows the shared hot-path probe to
        // exhaust the quota (a zero-byte CpuMemoryQuotaLease reserves one byte).
        auto quota = core::makeShared<core::CpuMemoryQuota>(plan->storageBytes + 1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            Control texture(desc, desc.initialState);
            assert(quota->liveBytes() == plan->storageBytes);
            const View view{texture};
            const auto observation = view.observe();
            assert(observation && view.matches(*observation));
            const rhi::TextureSubresourceRange depth{0u, 2u, 0u, 2u, rhi::TextureAspect::Depth};
            const rhi::TextureSubresourceRange stencil{0u, 2u, 0u, 2u, rhi::TextureAspect::Stencil};
            core::MutationJournal journal, foreign;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota}, [&]() noexcept -> const char* {
                    assert(!texture.writeWhole(S::CopyDst, &journal));
                    assert(texture.uniformState(depth) == S::CopyDst
                        && texture.uniformState(stencil) == S::CopyDst);
                    assert(!view.observe());
                    const auto pending = view.observePending(journal);
                    assert(pending && view.matchesPending(*pending, journal));
                    assert(
                        !view.observePending(foreign) && !view.matchesPending(*pending, foreign));
                    journal.rollback();
                    assert(!view.matches(*observation) && !view.matchesPending(*pending, journal));
                    assert(!texture.write(depth, S::CopySrc, S::CopyDst, nullptr));
                    assert(!view.uniformState() && !view.observe()
                        && !rhi::semantics::isKnownResourceState(view.state()));
                    const auto revision = texture.revision();
                    assert(std::string_view(texture.writeWhole(S::CopySrc, nullptr))
                        == "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM");
                    assert(texture.revision() == revision
                        && texture.uniformState(stencil) == S::CopySrc);
                    assert(!texture.write(depth, S::CopyDst, S::CopySrc, nullptr));
                    assert(!texture.writeWhole(S::CopyDst, &journal));
                    const auto committed = view.observePending(journal);
                    assert(committed);
                    journal.commit();
                    assert(view.observe() && !view.matchesPending(*committed, journal));
                    assert(!texture.writeWhole(S::CopySrc, &journal));
                    texture.invalidate();
                    journal.rollback();
                    assert(!view.active() && !view.observe() && !view.pending());
                    return nullptr;
                }));
        }
        assert(!quota->liveBytes());
        rhi::common::RecordedResourceStateControl buffer(S::CopySrc);
        const View bufferView{buffer};
        assert(bufferView.observe() && bufferView.identity() == &buffer);
        const View empty;
        assert(!empty && !empty.observe() && !empty.active());
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto lowering = Contract::lowering(backend);
            assert(lowering.normalizedControl && lowering.diagnostic
                && !lowering.nativeExecutionAvailable);
            assert(lowering.validatorOnly == (backend == rhi::Backend::Null));
        }
        std::puts(
            "BINDLESS_RHI_NORMALIZED_TEXTURE_CREATION_CONTRACT_OK descriptor-cells8 exact-two-arrays64 all-aspect-whole-view pending-foreign-rollback-stale nonuniform-refuse invalidation hot-allocation0 four-execution-gates-closed");
    }
};
} // namespace arp::test
