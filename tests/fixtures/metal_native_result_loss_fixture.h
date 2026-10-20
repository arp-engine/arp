#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_original_device_native_loss.h"
#include "rhi/gpu_device.h"

#include <cstdio>
#include <thread>

namespace arp::test {
class MetalNativeResultLossFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void require(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(stderr,
                "Metal ResultLoss actual=%s expected=%s\n",
                actual ? actual : "success",
                expected);
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verify(const rhi_metal::MetalRhiDeviceDesc& desc)
    {
        using namespace rhi;
        using namespace rhi_metal;
        const OriginalDeviceNativeResultLossBudget budget{1u, true, 1u, 2u, bytes};
        const auto domain = quota(), metadata = quota();
        auto device = createMetalRhiDevice(desc);
        assert(device);
        require(device->prepareOriginalDeviceNativeResultLoss(budget, metadata),
            "RHI-ORIGINAL-DEVICE-LOSS-DOMAIN-UNPREPARED");
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        require(device->prepareOriginalDeviceNativeResultLoss(budget, domain),
            "RHI-ORIGINAL-DEVICE-LOSS-QUOTA-DOMAIN");
        {
            core::CpuMemoryQuotaLease pressure(metadata, bytes);
            assert(pressure.charged());
            require(device->prepareOriginalDeviceNativeResultLoss(budget, metadata),
                "RHI-ORIGINAL-RESULT-LOSS-ALLOCATION");
        }
        assert(!metadata->liveBytes()
            && !device->prepareOriginalDeviceNativeResultLoss(budget, metadata));
        const char* id = nullptr;
        auto observation = device->observeOriginalDeviceNativeLoss(id);
        assert(observation && !id);
        const auto source = sharedMetalRhiOriginalResultLossSource();
        assert(source && !source->lost());
        const auto registryQuota = quota();
        {
            OriginalResourceRegistrationBudget registration{9u, true, 1u, 1u, 1u, 32u, bytes};
            registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
            registration.samplers = {1u, true, 1u};
            require(device->prepareOriginalDescriptorNativeLossRegistration(
                        {1u, true}, registration, registryQuota, *observation),
                "RHI-ORIGINAL-DESCRIPTOR-LOSS-DOMAIN");
            require(device->prepareOriginalDescriptorNativeLossRegistration(
                        {2u, true}, registration, metadata, *observation),
                "RHI-ORIGINAL-DESCRIPTOR-LOSS-QUOTA-DOMAIN");
            assert(!device->prepareOriginalDescriptorNativeLossRegistration(
                {2u, true}, registration, registryQuota, *observation));
            const auto gpuBytes = device->memoryStats().allocatedBytes;
            BufferDesc bd;
            bd.sizeBytes = 16u;
            bd.usage = ResourceUsage::StorageBuffer;
            auto buffer = device->createBuffer(bd);
            assert(buffer && !device->validateOriginalResourceRegistration(*buffer));
            TextureDesc td;
            td.extent = {2u, 2u, 1u};
            td.format = TextureFormat::Rgba8Unorm;
            td.usage =
                TextureUsage::Sampled | TextureUsage::RenderAttachment | TextureUsage::CopySrc;
            td.initialState = ResourceState::Undefined;
            const auto nativeBytes = device->queryOriginalContentTextureAllocation(td, id, 4u);
            assert(nativeBytes && !id);
            const auto contentQuota = quota(), wrapperQuota = quota();
            OriginalTextureContentCreationBudget contentBudget{
                4u, true, 1u, 1u, 16u, 2u, bytes, 1u, nativeBytes};
            core::UniquePtr<GpuTexture> texture;
            {
                core::CpuMemoryQuotaScope scope(wrapperQuota);
                {
                    core::CpuMemoryQuotaLease pressure(contentQuota, bytes);
                    assert(pressure.charged());
                    assert(
                        !device->createOriginalContentTexture(td, contentBudget, contentQuota, id));
                    require(id, "RHI-CANONICAL-CONTENT-CREATION-ALLOCATION");
                }
                assert(!device->createOriginalContentTexture(td, contentBudget, wrapperQuota, id));
                require(id, "RHI-CANONICAL-CONTENT-CREATION-QUOTA-DOMAIN");
                auto insufficient = contentBudget;
                --insufficient.maxNativeAllocationBytes;
                assert(!device->createOriginalContentTexture(td, insufficient, contentQuota, id));
                require(id, "RHI-METAL-CONTENT-CREATION-NATIVE-BUDGET");
                assert(!contentQuota->liveBytes() && !wrapperQuota->liveBytes());
                texture = device->createOriginalContentTexture(td, contentBudget, contentQuota, id);
            }
            if (id)
                std::fprintf(stderr, "Metal original content: %s\n", id);
            assert(texture && !id && !device->validateOriginalResourceRegistration(*texture));
            assert(texture->originalContentExclusive()
                && texture->state() == ResourceState::Undefined);
            const auto content =
                texture->observeOriginalContent(defaultTextureSubresourceRange(td));
            assert(content);
            assert(content->controlVersion() == 4u && !content->contentGeneration()
                && content->state() == common::OriginalTextureContentControl::State::Uninitialized);
            auto nativeTexture = metalRhiNativePayloadFor(*texture);
            assert(nativeTexture
                && !validateMetalRhiOriginalContentAllocation(
                    nativeTexture->resource, td, nativeBytes));
            assert(texture->allocation().estimatedSizeBytes == nativeBytes);
            TextureViewDesc vd;
            vd.format = td.format;
            auto view = device->createTextureView(*texture, vd);
            assert(view && !device->validateOriginalResourceRegistration(*view));
            assert(nativeTexture->originalContentExclusive()
                && metalRhiNativePayloadFor(*view)->originalContentExclusive());
            require(metalRhiOriginalTextureAccessDiagnostic(
                        metalRhiIssuingResourceRegistry(*device), texture->id()),
                "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
            {
                auto encoder = device->createCommandEncoder({});
                assert(encoder);
                const auto transition = encoder->transition(
                    *texture, ResourceState::Undefined, ResourceState::ColorAttachment);
                assert(!transition.success);
                require(transition.diagnostics.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                core::pmr::String message;
                auto probe = createMetalRhiNativeCommandPayload("Original exclusive denial",
                    metalRhiIssuingResourceRegistry(*device),
                    &message);
                assert(probe);
                BufferToTextureCopyDesc upload;
                upload.destinationTexture = texture->id();
                assert(!probe->recordBlitBufferToTexture(upload, message));
                require(message.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                TextureToBufferCopyDesc read;
                read.sourceTexture = texture->id();
                assert(!probe->recordBlitTextureToBuffer(read, message));
                require(message.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                TextureToTextureCopyDesc copy;
                copy.sourceTexture = texture->id();
                assert(!probe->recordBlitTextureCopy(copy, message));
                require(message.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                RenderPassDesc pass;
                RenderPassColorAttachmentDesc target;
                target.target = view->id();
                target.format = td.format;
                target.loadOp = LoadOp::Clear;
                target.storeOp = StoreOp::Store;
                pass.colorAttachments.push_back(target);
                assert(!probe->recordRenderPassBegin(pass, message));
                require(message.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                assert(texture->state() == ResourceState::Undefined && !content->diagnostic());
            }
            auto sampler = device->createSampler({});
            assert(sampler && !device->validateOriginalResourceRegistration(*sampler));
            DescriptorArenaDesc ad;
            ad.maxSets = 1u;
            ad.maxDescriptors = 2u;
            auto arena = core::shareUnique(device->createOriginalDescriptorArena(ad));
            assert(arena);
            DescriptorSetDesc sd;
            sd.label = "Original loss descriptor";
            sd.layout.name = "Original loss layout";
            sd.layout.bindings.push_back(
                {0u, DescriptorType::StorageBuffer, ShaderStage::Fragment, 1u});
            sd.layout.bindings.push_back(
                {1u, DescriptorType::SampledTexture, ShaderStage::Fragment, 1u});
            auto set = device->allocateOriginalDescriptorSet(arena, sd);
            if (!set)
                std::fprintf(stderr,
                    "Metal loss set: %s\n",
                    device->lastOriginalResourceRegistrationDiagnostic().data());
            assert(set);
            const auto denied = set->write({1u,
                DescriptorType::SampledTexture,
                view->id(),
                DescriptorResourceKind::TextureView});
            assert(!denied.success);
            require(denied.diagnostics.c_str(), "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
            assert(!device->validateOriginalResourceRegistration(*arena)
                && !device->validateOriginalResourceRegistration(*set));
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{registryQuota}, [&]() noexcept -> const char* {
                    return device->validateOriginalResourceRegistration(*view);
                }));
            arena->reset();
            assert(device->validateOriginalResourceRegistration(*set));
            set.reset();
            arena.reset();
            sampler.reset();
            view.reset();
            texture.reset();
            buffer.reset();
            assert(!contentQuota->liveBytes());
            assert(device->memoryStats().allocatedBytes == gpuBytes + nativeBytes);
            nativeTexture.reset();
            assert(!wrapperQuota->liveBytes());
            assert(device->memoryStats().allocatedBytes == gpuBytes);
            for (const auto format : {TextureFormat::Rgba16Float, TextureFormat::D32Float}) {
                td.format = format;
                contentBudget.maxTexelBytes = format == TextureFormat::Rgba16Float ? 32u : 16u;
                contentBudget.maxNativeAllocationBytes =
                    device->queryOriginalContentTextureAllocation(td, id, 4u);
                assert(contentBudget.maxNativeAllocationBytes && !id);
                {
                    core::CpuMemoryQuotaScope scope(wrapperQuota);
                    texture =
                        device->createOriginalContentTexture(td, contentBudget, contentQuota, id);
                }
                if (id)
                    std::fprintf(stderr, "Metal original content format: %s\n", id);
                assert(texture && !id && texture->state() == ResourceState::Undefined
                    && texture->originalContentExclusive());
                nativeTexture = metalRhiNativePayloadFor(*texture);
                assert(!validateMetalRhiOriginalContentAllocation(
                    nativeTexture->resource, td, contentBudget.maxNativeAllocationBytes));
                texture.reset();
                nativeTexture.reset();
                assert(!wrapperQuota->liveBytes() && !contentQuota->liveBytes()
                    && device->memoryStats().allocatedBytes == gpuBytes);
            }
            // The existing independent whole-upload domain, still entirely
            // Uninitialized: creation is not an upload/completion receipt.
            for (const auto dimension : {TextureViewDimension::Texture2D,
                     TextureViewDimension::Texture2DArray,
                     TextureViewDimension::TextureCube,
                     TextureViewDimension::Texture3D})
                for (const auto format : {TextureFormat::Rgba8Unorm, TextureFormat::Rgba16Float}) {
                    TextureDesc uploadDesc;
                    uploadDesc.extent = {
                        4u, 4u, dimension == TextureViewDimension::Texture3D ? 4u : 1u};
                    uploadDesc.arrayLayerCount = dimension == TextureViewDimension::TextureCube ? 6u
                        : dimension == TextureViewDimension::Texture2DArray                     ? 2u
                                                                            : 1u;
                    uploadDesc.cubeCompatible = dimension == TextureViewDimension::TextureCube;
                    uploadDesc.mipLevelCount = 3u;
                    uploadDesc.format = format;
                    uploadDesc.usage =
                        TextureUsage::CopyDst | TextureUsage::CopySrc | TextureUsage::Sampled;
                    uploadDesc.initialState = ResourceState::Undefined;
                    std::uint64_t texelBytes = 0u;
                    assert(!semantics::TextureUploadDestinationValidation::validate(
                        uploadDesc, texelBytes, ResourceState::Undefined));
                    const auto nativeLimit =
                        device->queryOriginalContentTextureAllocation(uploadDesc, id, 3u);
                    if (id)
                        std::fprintf(stderr, "Metal upload creation query: %s\n", id);
                    assert(nativeLimit && !id);
                    const OriginalTextureContentCreationBudget uploadBudget{
                        3u, true, 1u, 1u, texelBytes, 2u, bytes, 0u, nativeLimit};
                    {
                        core::CpuMemoryQuotaScope scope(wrapperQuota);
                        auto less = uploadBudget;
                        --less.maxNativeAllocationBytes;
                        assert(!device->createOriginalContentTexture(
                            uploadDesc, less, contentQuota, id));
                        require(id, "RHI-METAL-CONTENT-CREATION-NATIVE-BUDGET");
                        less = uploadBudget;
                        --less.maxTexelBytes;
                        assert(!device->createOriginalContentTexture(
                            uploadDesc, less, contentQuota, id));
                        require(id, "RHI-CANONICAL-CONTENT-CREATION-BYTE-CAPACITY");
                        assert(!contentQuota->liveBytes() && !wrapperQuota->liveBytes());
                        texture = device->createOriginalContentTexture(
                            uploadDesc, uploadBudget, contentQuota, id);
                    }
                    if (id)
                        std::fprintf(stderr, "Metal upload creation: %s\n", id);
                    assert(
                        texture && !id && !device->validateOriginalResourceRegistration(*texture));
                    const auto original = texture->observeOriginalContent();
                    assert(original && original->controlVersion() == 3u
                        && !original->contentGeneration()
                        && original->state()
                            == common::OriginalTextureContentControl::State::Uninitialized);
                    nativeTexture = metalRhiNativePayloadFor(*texture);
                    assert(nativeTexture && nativeTexture->originalContentExclusive()
                        && !validateMetalRhiOriginalContentAllocation(
                            nativeTexture->resource, uploadDesc, nativeLimit));
                    assert(texture->allocation().estimatedSizeBytes == nativeLimit);
                    TextureViewDesc uploadView;
                    uploadView.format = format;
                    uploadView.usage = TextureUsage::Sampled;
                    uploadView.dimension = dimension;
                    uploadView.mipLevelCount = uploadDesc.mipLevelCount;
                    uploadView.arrayLayerCount = uploadDesc.arrayLayerCount;
                    view = device->createTextureView(*texture, uploadView);
                    assert(view && !device->validateOriginalResourceRegistration(*view)
                        && metalRhiNativePayloadFor(*view)->originalContentExclusive());
                    require(metalRhiOriginalTextureAccessDiagnostic(
                                metalRhiIssuingResourceRegistry(*device), texture->id()),
                        "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
                    auto ordinary = device->createCommandEncoder({});
                    assert(ordinary);
                    assert(!ordinary
                            ->transition(*texture, ResourceState::Undefined, ResourceState::CopyDst)
                            .success);
                    assert(!original->diagnostic() && texture->state() == ResourceState::Undefined);
                    ordinary.reset();
                    view.reset();
                    texture.reset();
                    assert(!contentQuota->liveBytes()
                        && device->memoryStats().allocatedBytes == gpuBytes + nativeLimit);
                    nativeTexture.reset();
                    assert(!wrapperQuota->liveBytes()
                        && device->memoryStats().allocatedBytes == gpuBytes);
                    std::printf(
                        "BINDLESS_METAL_ORIGINAL_UPLOAD_CREATION3 dimension%u format%u mips3 texels%llu native%llu Uninitialized ordinary-denied retired0\n",
                        static_cast<unsigned>(dimension),
                        static_cast<unsigned>(format),
                        static_cast<unsigned long long>(texelBytes),
                        static_cast<unsigned long long>(nativeLimit));
                }
        }
        const auto otherDomain = quota(), otherMetadata = quota();
        auto other = createMetalRhiDevice(desc);
        assert(other);
        assert(!other->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, otherDomain));
        require(other->prepareOriginalDeviceNativeResultLoss(budget, otherMetadata),
            "RHI-METAL-RESULT-LOSS-SOURCE-CAPACITY");
        assert(!otherMetadata->liveBytes());
        {
            core::pmr::String message;
            auto command = createMetalRhiNativeCommandPayload("Original result source healthy",
                metalRhiIssuingResourceRegistry(*device),
                &message);
            assert(command && command->finish(message));
            auto done = command->preparedCompletionState();
            MetalRhiNativeQueueStats queue;
            assert(command->submit(queue, done, {}, 0u, {}, 0u, message));
            done->wait();
            assert(done->isComplete() && !observation->diagnostic() && !source->lost());
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, metadata}, [&]() noexcept -> const char* {
                source->observeDeviceRemoval(metadata.get());
                return observation->diagnostic();
            }));
        assert(!device->detachOriginalDeviceNativeLoss());
        require(observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        device.reset();
        observation.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes() && !registryQuota->liveBytes());
        assert(!other->prepareOriginalDeviceNativeResultLoss(budget, otherMetadata));
        auto fresh = other->observeOriginalDeviceNativeLoss(id);
        assert(fresh && !id);
        // Inject only the negative result consumer, after genuine healthy GPU
        // completion. This is not hardware removal or a native execution proof.
        std::thread worker([&] {
            source->observeDeviceRemoval(source->device().get());
        });
        for (std::size_t i = 0u; i < 1024u; ++i)
            (void)fresh->diagnostic();
        worker.join();
        require(fresh->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        other.reset();
        require(fresh->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        fresh.reset();
        assert(!otherDomain->liveBytes() && !otherMetadata->liveBytes());
        device = createMetalRhiDevice(desc);
        assert(device);
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        require(device->prepareOriginalDeviceNativeResultLoss(budget, metadata),
            "RHI-METAL-RESULT-LOSS-DEVICE-LOST");
        device.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes());
        std::puts(
            "BINDLESS_METAL_NATIVE_RESULT_LOSS1 real-healthy-native-completion descriptor-resource-sampler-join2 content-creation4 native-reservation actual-allocation-bound injected-negative-only concurrent-terminal observer-detach quota0; no-hardware-loss-claim");
    }
};
} // namespace arp::test
