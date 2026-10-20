#pragma once
#include "ordinary_depth_comparison_oracle_fixture.h"
#include "original_ordinary_depth_root_fixture.h"
#include "webgpu_ordinary_depth_closure_fixture.h"
#include "webgpu_original_bindless_root_execution_fixture.h"
#include "webgpu_original_completed_preparation_fixture.h"

namespace arp::test {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
// The existing canonical producer supplies the genuine two-mip depth proof.
// Every scenario gets its own Root/attachment; cancellation cannot reuse a
// generation advanced by a prior journal. Execution uses the original executor.
class WebGpuOrdinaryDepthExecutionFixture final {
public:
    using Execution = WebGpuOriginalBindlessRootExecutionFixtureImpl<true,
        true,
        false,
        false,
        false,
        OrdinaryDepthComparisonOracleFixture,
        true,
        true>;

    static std::shared_ptr<const rhi_webgpu::WebGpuBindlessRootNativeClosure> verify(
        rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& depth,
        std::uint32_t scenario,
        bool fault,
        bool retainAfterCompletion = false)
    {
        using namespace rhi;
        assert(scenario <= static_cast<std::uint32_t>(Execution::Scenario::RetiredLifetime));
        const auto& quota = depth->originalMetadataQuota();
        const auto bytes = quota->limitBytes();
        const char* id = nullptr;
        WebGpuOriginalCompletedPreparationRequest uploader;
        uploader.initializeTexturePixels =
            OrdinaryDepthComparisonOracleFixture::initializeTexturePixels;
        TextureDesc desc;
        desc.format = TextureFormat::Rgba8Unorm;
        const auto parent = uploader.createCompletedTexture(device, desc, quota, 0u);
        assert(parent);
        TextureViewDesc vd;
        vd.format = desc.format;
        vd.usage = TextureUsage::Sampled;
        vd.mipLevelCount = desc.mipLevelCount;
        auto object = device.createTextureView(*parent->originalObject(), vd);
        assert(object);
        const auto lease = OriginalCommandTextureViewLease::prepareOriginal(
            object, {1u, true, 2u, bytes}, quota, id);
        assert(lease && !id);
        const auto color = OriginalCommandTextureViewSource::prepareOriginal(lease,
            parent,
            {1u, true, OriginalCommandTextureViewSource::completeRetainedOwnerCount, bytes},
            quota,
            id);
        assert(color && !id);
        const auto prepared = OriginalOrdinaryDepthRootFixture::prepare(device,
            package,
            depth,
            color,
            fault,
            2u,
            true,
            [](OriginalCommandAdmissionBudget& admission) {
                Execution{}.configureAdmission(admission);
            });
        const auto content = OriginalOrdinaryDepthRootFixture::combinedBudget(prepared, true);
        Execution execute;
        execute.scenario = static_cast<Execution::Scenario>(scenario);
        execute.fault = fault;
        const auto depthBefore = depth->cells();
        const auto colorBefore = parent->originalObject()->observeOriginalContent();
        assert(colorBefore);
        OriginalBindlessGraphicsStreamFixture::execute(
            prepared.root, prepared.inventory, device, execute, &content, 3u);
        if (execute.scenario == Execution::Scenario::RetiredLifetime) {
            assert(device.validateOriginalDeviceLifetime(device.originalDeviceLifetimeTicket()));
            assert(depth->validate());
            assert(!retainAfterCompletion);
            return {};
        }
        assert(!depth->validate() && depth->cells().size() == depthBefore.size());
        for (std::size_t i = 0u; i < depthBefore.size(); ++i)
            assert(depth->cells()[i].contentGeneration() == depthBefore[i].contentGeneration());
        const auto colorAfter = parent->originalObject()->observeOriginalContent();
        assert(colorAfter && colorBefore->contentGeneration() == colorAfter->contentGeneration());
        assert(!prepared.inventory->originalStateTransactionCount());
        if (retainAfterCompletion) {
            assert(execute.scenario == Execution::Scenario::Success
                || execute.scenario == Execution::Scenario::Reverse);
            auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto closure =
                rhi_webgpu::WebGpuBindlessRootNativeClosure::prepareOriginal(prepared.root,
                    prepared.inventory,
                    device,
                    WebGpuOrdinaryDepthClosureFixture::nativeBudget(prepared, depth, 20u),
                    nativeQuota,
                    id);
            if (id)
                std::fprintf(stderr, "post-consumer Closure20: %s\n", id);
            assert(closure && !id && !closure->validate());
            return closure;
        }
        return {};
    }
};
#endif
} // namespace arp::test
