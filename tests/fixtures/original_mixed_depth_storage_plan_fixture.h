#pragma once
#include "original_mixed_image_snapshot_fixture.h"
#include "original_storage_execution_fixture.h"
#include "original_storage_texture_creation_fixture.h"

namespace arp::test {
// Real completed depth and authenticated storage Roots share a canonical stream.
// This fixture proves CPU planning/rollback, never mixed GPU execution.
struct OriginalStoragePlanningBackendFixture final {
    static constexpr bool callback = false;
    static constexpr unsigned nativePayloadObjects = 7u;
    inline static constexpr const char* marker = "NATIVE";
};

struct OriginalStorageCallbackPlanningBackendFixture final {
    static constexpr bool callback = true;
    static constexpr unsigned nativePayloadObjects = 3u;
    inline static constexpr const char* marker = "WEBGPU";
};

class OriginalMixedDepthStoragePlanFixture final {
    using Root = rhi::OriginalCommandBindlessRootSource;
    using Sources = rhi::OriginalCommandResourceSources;

    struct Context {
        const std::shared_ptr<const Root>& root;
        const std::shared_ptr<const Sources>& inventory;
    };

    inline static const Context* context_ = nullptr;

    static void inspectThreeEntries(rhi::GpuDevice& device,
        const std::shared_ptr<rhi::BindlessSnapshot>& snapshot,
        const std::array<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>, 3u>& entries,
        const core::pmr::Vector<Sources::Source>& inputs,
        const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        if (device.backend() == rhi::Backend::WebGPU)
            OriginalStorageExecutionFixture<OriginalStorageCallbackPlanningBackendFixture>::
                template run<true>(device, snapshot, entries, inputs, resources);
        else
            OriginalStorageExecutionFixture<OriginalStoragePlanningBackendFixture>::template run<
                true>(device, snapshot, entries, inputs, resources);
    }

    template <class Closure>
    static void consume(rhi::GpuDevice& device,
        const std::shared_ptr<rhi::BindlessSnapshot>& snapshot,
        const std::array<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>, 2u>& pair,
        const core::pmr::Vector<Sources::Source>& inputs,
        const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        assert(context_);
        OriginalMixedImageSnapshotFixture::verifyDevice<Closure>(
            device, context_->root, inputs, resources, &inspectThreeEntries, executionCheck);
        if (executionCheck)
            return;
        if (device.backend() == rhi::Backend::WebGPU)
            OriginalStorageExecutionFixture<OriginalStorageCallbackPlanningBackendFixture>::
                template run<true>(
                    device, snapshot, pair, inputs, resources, context_->root, context_->inventory);
        else
            OriginalStorageExecutionFixture<OriginalStoragePlanningBackendFixture>::template run<
                true>(
                device, snapshot, pair, inputs, resources, context_->root, context_->inventory);
    }

public:
    // Explicitly selected by separate execution CLI cases. Planning cases keep
    // this null and cannot acquire execution authority from their cold profile.
    inline static OriginalMixedImageSnapshotFixture::EntryCheck executionCheck = nullptr;

    static std::uint64_t additionalGpuBytes()
    {
#if defined(ARP_BINDLESS_STORAGE_ROOT_PACKAGE)
        core::pmr::String diagnostic;
        auto package = rhi::BindlessShaderPackage::load(ARP_BINDLESS_STORAGE_ROOT_PACKAGE,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        assert(package);
        std::uint64_t texels = 0u;
        assert(!rhi::semantics::TextureUploadDestinationValidation::footprint(
            OriginalStorageTextureCreationFixture::descriptor(), texels));
        return package->artifact().snapshotBytes + 2u * 4u * sizeof(std::uint32_t) + texels
            + OriginalMixedImageSnapshotFixture::additionalGpuBytes();
#else
        assert(false && "Mixed planning requires the authenticated StorageVector package");
        return 0u;
#endif
    }

    static void configureRegistration(rhi::OriginalResourceRegistrationBudget& b)
    {
        // Five buffers (snapshot+Results), one image, four descriptor views,
        // two stream attachment views, three arenas and five descriptor sets.
        // Shared mixed snapshot: three internal buffers, one Results buffer,
        // one original arena/set; no additional image or view identity.
        constexpr auto buffers = 9u, textures = 1u, views = 6u, arenas = 4u, sets = 6u;
        b.maxBuffers += buffers;
        b.maxTextures += textures;
        b.maxTextureViews += views;
        b.maxDescriptorArenas += arenas;
        b.maxDescriptorSets += sets;
        b.maxRetainedOwners += (buffers + textures + views + arenas)
                * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount
            + sets * rhi::OriginalResourceRegistrationContract::perDescriptorOwnerCount;
    }

    template <class Closure>
    static void verify(rhi::GpuDevice& device,
        const std::shared_ptr<const Root>& root,
        const std::shared_ptr<const Sources>& inventory)
    {
        using namespace rhi;
        assert(!context_);
        const bool native = device.backend() != Backend::WebGPU;
        const Context context{root, inventory};
        context_ = &context;
        const auto bytes = inventory->originalMetadataQuota()->limitBytes();
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(bytes);
        q[2] = inventory->originalMetadataQuota();
        const auto sourceBaseline = q[2]->liveBytes();
        const auto gpuBaseline = device.memoryStats().allocatedBytes;
        {
            const char* id = nullptr;
            const auto d = OriginalStorageTextureCreationFixture::descriptor();
            std::uint64_t texels = 0u;
            assert(!semantics::TextureUploadDestinationValidation::footprint(d, texels));
            const auto allocation =
                native ? device.queryOriginalContentTextureAllocation(d, id, 6u) : texels;
            assert(allocation && !id);
            core::UniquePtr<GpuTexture> texture;
            {
                core::CpuMemoryQuotaScope resources(q[0]);
                texture = device.createOriginalContentTexture(d,
                    {native ? 6u : 7u,
                        true,
                        1u,
                        1u,
                        texels,
                        4u,
                        bytes,
                        3u,
                        native ? allocation : 0u},
                    q[1],
                    id);
            }
            assert(texture && !id);
            auto parent = OriginalCommandTextureLease::prepareOriginal(
                texture, {1u, true, 2u, bytes}, q[2], id);
            assert(parent && !id);
            auto publication =
                OriginalTextureCellDescriptorFixture::prepareDevice(device, parent, q[0], nullptr);
            OriginalStoragePipelineFixture::verifyDevice(
                device, *publication, q[0], nullptr, &consume<Closure>, true);
        }
        context_ = nullptr;
        for (unsigned i = 0u; i < q.size(); ++i)
            assert(q[i]->liveBytes() == (i == 2u ? sourceBaseline : 0u));
        assert(device.memoryStats().allocatedBytes == gpuBaseline);
    }
};
} // namespace arp::test
