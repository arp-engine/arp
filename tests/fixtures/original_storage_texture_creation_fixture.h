#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_storage_content_plan_fixture.h"
#include "original_texture_cell_descriptor_fixture.h"
#include "rhi/gpu_device.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalStorageTextureCreationFixture final {
    static constexpr std::uint64_t cpuBytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Storage content creation: %s\n", id);
        assert(!id);
    }

public:
    static rhi::TextureDesc descriptor()
    {
        rhi::TextureDesc d;
        d.label = "original storage pyramid creation";
        d.extent = {4u, 4u, 1u};
        d.mipLevelCount = 3u;
        d.format = rhi::TextureFormat::Rgba16Float;
        d.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage
            | rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
        return d;
    }

    static void verifyContract(rhi::GpuDevice& nullDevice)
    {
        OriginalTextureCellDescriptorFixture::verifyContract();
        OriginalStorageSnapshotFixture::verifyContract();
        using namespace rhi;
        using C = OriginalTextureContentCreationContract;
        auto d = descriptor();
        std::uint64_t required = 0u;
        require(semantics::TextureUploadDestinationValidation::footprint(d, required));
        assert(required == 168u); // (4*4 + 2*2 + 1)*RGBA16F.
        for (auto version : {6u, 7u}) {
            OriginalTextureContentCreationBudget b{
                version, true, 1u, 1u, required, 4u, cpuBytes, 3u, version == 6u ? required : 0u};
            std::uint64_t texels = 77u;
            require(C::inspect(d, b, texels));
            assert(texels == required);
            for (unsigned field = 0u; field < 5u; ++field) {
                auto bad = b;
                if (field == 0u)
                    --bad.maxTexelBytes;
                if (field == 1u)
                    --bad.maxContentCells;
                if (field == 2u)
                    bad.prototypeOptIn = false;
                if (field == 3u)
                    bad.maxUploadGenerations = 0u;
                if (field == 4u)
                    bad.maxNativeAllocationBytes = version == 6u ? 0u : 1u;
                assert(C::inspect(d, bad, texels) && !texels);
            }
            for (auto usage : {TextureUsage::Sampled | TextureUsage::RenderAttachment,
                     TextureUsage::Storage | TextureUsage::RenderAttachment,
                     TextureUsage::Storage | TextureUsage::Sampled,
                     d.usage | TextureUsage::CopyDst}) {
                auto bad = d;
                bad.usage = usage;
                assert(C::inspect(bad, b, texels) && !texels);
            }
            for (auto format :
                {TextureFormat::Rgba8Unorm, TextureFormat::R32Float, TextureFormat::D32Float}) {
                auto bad = d;
                bad.format = format;
                assert(C::inspect(bad, b, texels) && !texels);
            }
            auto bad = d;
            bad.sampleCount = 4u;
            assert(C::inspect(bad, b, texels));
            bad = d;
            bad.arrayLayerCount = 2u;
            assert(C::inspect(bad, b, texels));
            bad = d;
            bad.initialState = ResourceState::ShaderWrite;
            assert(C::inspect(bad, b, texels));
            for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto decision = C::lowering(backend, version);
                assert(decision.contractVersion == version && decision.diagnostic
                    && !decision.nativeContentExecutionAvailable
                    && decision.exclusiveCreationAvailable
                        == (version == 6u ? backend == Backend::Vulkan || backend == Backend::Metal
                                          : backend == Backend::WebGPU));
            }
            const char* id = nullptr;
            auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!nullDevice.createOriginalContentTexture(d, b, quota, id) && id);
            assert(!quota->liveBytes()
                && allocations == core::defaultCpuAllocator().stats().allocationCount);
            // Legacy content plans retain their exact profiles. Creation does
            // not authorize clear/store, arbitrary dispatch or CPU completion.
            for (std::uint32_t plan = 1u; plan <= 12u; ++plan)
                assert(!C::cellControlMatchesPlan(version, plan));
        }
        for (auto version : {2u, 4u, 5u}) {
            std::uint64_t texels = 77u;
            assert(C::inspectCellLayout(d, 3u, texels, version) && !texels);
        }
        std::puts(
            "BINDLESS_RHI_STORAGE_CREATION67_CONTRACT_OK RGBA16F cells3 no-implicit-initialization legacy-refused four-backend no-execution");
    }

    // Resource creation remains cold unless the caller explicitly supplies the
    // separately gated public execution check. Creation alone proves no dispatch.
    static void verifyDevice(core::UniquePtr<rhi::GpuDevice> device,
        OriginalTextureCellDescriptorFixture::NativeExportCheck exportCheck = nullptr,
        OriginalStoragePipelineFixture::RootCheck rootCheck = nullptr,
        OriginalStoragePipelineFixture::ExecuteCheck executionCheck = nullptr)
    {
        using namespace rhi;
        assert(device && device->backend() != Backend::Null);
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const bool native = device->backend() != Backend::WebGPU;
        require(device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, q[0]));
        if (native)
            require(
                device->prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, cpuBytes}, q[1]));
        else
            require(
                device->prepareOriginalDeviceNativeLoss({{1u, true, 1u, 1u, cpuBytes}, 1u}, q[1]));
        constexpr auto buffers = 3u + 2u; // snapshot buffers + independent Results
        const auto views =
            1u + 2u * 2u + (executionCheck ? 2u : 0u); // attachment + sampled/storage for each mip
        constexpr auto arenas = 2u + 1u, sets = 2u * 2u + 1u; // cell publications + snapshot
        const auto owners = OriginalResourceRegistrationContract::fixedOwnerCount
            + (buffers + 1u + views + arenas)
                * OriginalResourceRegistrationContract::perResourceOwnerCount
            + sets * OriginalResourceRegistrationContract::perDescriptorOwnerCount;
        OriginalResourceRegistrationBudget registration{
            9u, true, buffers, 1u, views, owners, cpuBytes};
        registration.maxDescriptorArenas = arenas;
        registration.maxDescriptorSets = sets;
        {
            const char* lossId = nullptr;
            const auto loss = device->observeOriginalDeviceNativeLoss(lossId);
            require(lossId);
            assert(loss);
            require(device->prepareOriginalDescriptorNativeLossRegistration(
                {native ? 1u : 3u, true}, registration, q[2], *loss));
        }
        const auto d = descriptor();
        const char* id = nullptr;
        std::uint64_t texels = 0u;
        require(semantics::TextureUploadDestinationValidation::footprint(d, texels));
        const auto allocation =
            native ? device->queryOriginalContentTextureAllocation(d, id, 6u) : texels;
        require(id);
        assert(allocation);
        OriginalTextureContentCreationBudget budget{
            native ? 6u : 7u, true, 1u, 1u, texels, 4u, cpuBytes, 3u, native ? allocation : 0u};
        const auto baseline = device->memoryStats().allocatedBytes;
        const auto make = [&] {
            core::CpuMemoryQuotaScope resource(q[3]);
            return device->createOriginalContentTexture(d, budget, q[4], id);
        };
        for (unsigned field = 0u; field < (native ? 3u : 2u); ++field) {
            auto bad = budget;
            if (field == 0u)
                --bad.maxTexelBytes;
            if (field == 1u)
                --bad.maxContentCells;
            if (field == 2u)
                --bad.maxNativeAllocationBytes;
            core::CpuMemoryQuotaScope resource(q[3]);
            assert(!device->createOriginalContentTexture(d, bad, q[4], id) && id);
            assert(!q[3]->liveBytes() && !q[4]->liveBytes()
                && device->memoryStats().allocatedBytes == baseline);
        }
        for (auto domain : {2u, 3u, 4u}) {
            CpuQuotaFactoryFixture pressure(q[domain]);
            const auto peak = pressure.measure(make);
            pressure.verify(peak, make);
            assert(device->memoryStats().allocatedBytes == baseline);
        }
        auto texture = make();
        require(id);
        assert(texture && texture->originalContentExclusive() && !texture->observeOriginalContent()
            && texture->state() == ResourceState::Undefined
            && texture->allocation().estimatedSizeBytes == allocation);
        for (auto usage :
            {TextureUsage::RenderAttachment, TextureUsage::Storage, TextureUsage::Sampled}) {
            TextureViewDesc vd;
            vd.format = d.format;
            vd.usage = usage;
            core::CpuMemoryQuotaScope resource(q[3]);
            auto view = device->createTextureView(*texture, vd);
            assert(view);
        }
        for (std::uint32_t mip = 0u; mip < d.mipLevelCount; ++mip) {
            const auto content =
                texture->observeOriginalContent({mip, 1u, 0u, 1u, TextureAspect::Color});
            assert(content
                && content->state() == common::OriginalTextureContentControl::State::Uninitialized
                && !content->contentGeneration() && !content->nativeExecutionProven());
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
                return texture->validateOriginalContent(*content);
            }));
        }
        auto parent = OriginalStorageContentPlanFixture::verifyDevice(
            *device, texture, q[3], exportCheck, rootCheck);
        // Keep the untouched mip0 observation before an execution check may
        // retire the device; fresh observations must fail after retirement.
        auto content = parent->originalObject()->observeOriginalContent(
            {0u, 1u, 0u, 1u, TextureAspect::Color});
        assert(content);
        if (executionCheck) {
            auto publication = OriginalTextureCellDescriptorFixture::prepareDevice(
                *device, parent, q[3], exportCheck);
            OriginalStoragePipelineFixture::verifyDevice(
                *device, *publication, q[3], nullptr, executionCheck, true);
        }
        const auto backend = device->backend();
        device.reset();
        assert(content->diagnostic());
        texture.reset();
        parent.reset();
        content.reset(); // Release the weak registration receipt/control block.
        for (const auto& owner : q)
            assert(!owner->liveBytes());
        std::printf(
            "BINDLESS_RHI_STORAGE_CREATION67_DEVICE_OK backend=%u logical=%llu allocation=%llu queried=%u cells3 uninitialized exclusive budget-rollback hot0 shutdown quota0\n",
            static_cast<unsigned>(backend),
            static_cast<unsigned long long>(texels),
            static_cast<unsigned long long>(allocation),
            native);
    }
};
} // namespace arp::test
