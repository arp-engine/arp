#pragma once
#include "cpu_quota_factory_fixture.h"
#include "metal_closed_consumer_fixture.h"
#include "metal_closed_consumer_issuance_fixture.h"
#include "metal_native_identity_fixture.h"
#include "rhi/backends/metal/metal_closed_consumer_images.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
// Reusable real Metal fresh-image lifecycle fixture. Test byte patterns and
// finite capacities are explicit authoring, never production fallback values.
class MetalClosedConsumerImagesFixture final {
    using Root = rhi_metal::MetalRhiClosedConsumerImages;
    using Contract = Root::Contract;
    using Content = Root::Content;
    using Geometry = Root::Geometry;

public:
    explicit MetalClosedConsumerImagesFixture(OrdinaryGraphicsFixture& graphics) : f_(graphics)
    {
        for (std::size_t i = 0u; i < inputs_.size(); ++i) {
            rhi::Extent3D extent;
            assert(!rhi::semantics::TextureSubresourceExtent::resolve(
                {7u, 5u, 1u}, 3u, static_cast<std::uint32_t>(i), extent));
            bytes_[i].assign(std::size_t{rhi::kTextureUploadRowPitchAlignment} * extent.height,
                std::uint8_t{0xa5u});
            for (std::uint32_t y = 0u; y < extent.height; ++y)
                for (std::uint32_t x = 0u; x < extent.width; ++x)
                    for (std::uint32_t c = 0u; c < 4u; ++c)
                        bytes_[i][y * rhi::kTextureUploadRowPitchAlignment + x * 4u + c] =
                            static_cast<std::uint8_t>(i * 71u + y * 13u + x * 7u + c * 3u);
            inputs_[i] = {rhi::TextureFormat::Rgba8Unorm,
                {7u, 5u, 1u},
                extent,
                3u,
                static_cast<std::uint32_t>(i),
                rhi::kTextureUploadRowPitchAlignment,
                extent.height,
                bytes_[i].data(),
                bytes_[i].size()};
            totalBytes_ += bytes_[i].size();
        }
    }

    void verify(std::shared_ptr<const Geometry> geometry,
        bool issuance = false,
        unsigned recordingStage = 0u)
    {
        using namespace rhi;
        using namespace rhi_metal;
        static_assert(
            !std::is_default_constructible_v<Root> && !std::is_copy_constructible_v<Root>);
        static_assert(!std::is_default_constructible_v<Root::Marker>
            && !std::is_copy_constructible_v<Root::Marker>);
        core::CpuMemoryQuotaScope scope(f_.quota);
        auto selected = geometry;
        std::uint64_t estimate = 0u;
        for (const auto& input : inputs_)
            estimate += estimateMetalRhiTextureBytes(Contract::parentDescriptor(input));
        const auto owners = 1u + geometry->childOwnerCount() + 3u
            + Contract::imageCount
                * (4u + 2u * MetalRhiResourceOwnerProjection::independentResourceOwnerCount);
        Root::Budget budget{
            {1u, true, totalBytes_, totalBytes_, Contract::commandUnits}, estimate, owners};
        const auto prepare = [&] {
            return Root::prepare(selected, *f_.device, inputs_, budget, f_.diagnostic);
        };
        if (issuance) {
            auto images = prepare();
            assert(images && !images->validate());
            MetalClosedConsumerIssuanceFixture(f_).verify(images, recordingStage);
            return;
        }
        const auto reject = [&](const char* id) {
            const auto cpu = f_.quota->liveBytes(), gpu = f_.device->memoryStats().allocatedBytes;
            auto root = prepare();
            if (root || f_.diagnostic != id)
                std::fprintf(stderr, "Images expected %s got %s\n", id, f_.diagnostic.c_str());
            assert(!root && f_.diagnostic == id && cpu == f_.quota->liveBytes()
                && gpu == f_.device->memoryStats().allocatedBytes);
        };
        budget.values.prototypeOptIn = false;
        reject("RHI-EXCLUSIVE-CONSUMER-IMAGES-BUDGET");
        budget.values.prototypeOptIn = true;
        --budget.values.maxCommands;
        reject("RHI-EXCLUSIVE-CONSUMER-IMAGES-BUDGET");
        ++budget.values.maxCommands;
        --budget.values.maxSourceBytes;
        reject("RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTE-BUDGET");
        ++budget.values.maxSourceBytes;
        --budget.values.maxSnapshotBytes;
        reject("RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTE-BUDGET");
        ++budget.values.maxSnapshotBytes;
        --budget.maxTextureBytes;
        reject("RHI-METAL-EXCLUSIVE-IMAGES-TEXTURE-BUDGET");
        ++budget.maxTextureBytes;
        --budget.maxChildOwners;
        reject("RHI-METAL-EXCLUSIVE-IMAGES-CHILD-OWNER-BUDGET");
        ++budget.maxChildOwners;
        const auto original = inputs_[0u];
        inputs_[0u].parentExtent = {UINT32_MAX, UINT32_MAX, 1u};
        inputs_[0u].mipCount = 32u;
        inputs_[0u].selectedMip = 31u;
        inputs_[0u].extent = {1u, 1u, 1u};
        inputs_[0u].rowsPerImage = 1u;
        inputs_[0u].byteCount = rhi::kTextureUploadRowPitchAlignment;
        reject("RHI-METAL-EXCLUSIVE-IMAGES-TEXTURE-BUDGET");
        inputs_[0u] = original;
        selected = std::shared_ptr<const Geometry>(geometry->pipeline(), geometry.get());
        reject("RHI-METAL-EXCLUSIVE-IMAGES-ORIGINAL-OWNER");
        selected = geometry;
        auto foreign = createMetalRhiDevice(OrdinaryGraphicsFixture::deviceDesc());
        assert(foreign);
        assert(!Root::prepare(geometry, *foreign, inputs_, budget, f_.diagnostic)
            && f_.diagnostic == "RHI-METAL-EXCLUSIVE-IMAGES-ISSUER-MISMATCH");
        foreign.reset();
        {
            auto warm = prepare();
            if (!warm)
                std::fprintf(stderr, "Images warm: %s\n", f_.diagnostic.c_str());
            assert(warm && !warm->validate());
        }
        CpuQuotaFactoryFixture factory(f_.quota);
        const auto peak = factory.measure(prepare);
        factory.verify(peak, prepare);
        assert(f_.diagnostic == "RHI-METAL-EXCLUSIVE-IMAGES-ALLOCATION-FAILED");
        const auto gpu = f_.device->memoryStats().allocatedBytes,
                   capacity = f_.device->memoryStats().budgetCapacityBytes;
        for (const auto available : {estimate, estimate - 1u}) {
            auto pressure = f_.device->createBuffer({"ImagesGpuBudget",
                capacity - gpu - available,
                ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(pressure);
            auto candidate = prepare();
            if (available == estimate)
                assert(candidate && f_.device->memoryStats().allocatedBytes == capacity);
            else
                assert(!candidate
                    && f_.diagnostic == "RHI-METAL-EXCLUSIVE-IMAGES-TEXTURE-CREATE-FAILED");
            candidate.reset();
            pressure.reset();
            assert(f_.device->memoryStats().allocatedBytes == gpu);
        }
        const auto submissions =
            inspectMetalRhiNativeQueueStats(f_.device->queue()).nativeQueueSubmitCount;
        auto root = prepare();
        assert(root && !root->validate() && root->childOwnerCount() == owners
            && root->ownedBytes() == estimate);
        assert(inspectMetalRhiNativeQueueStats(f_.device->queue()).nativeQueueSubmitCount
            == submissions + 1u);
        assert(!metalRhiNativePayloadFor(*f_.texture)->immutableRevision());
        for (std::size_t i = 0u; i < inputs_.size(); ++i) {
            assert(root->contents()[i]->bytes().size() == bytes_[i].size());
            assert(!std::memcmp(
                bytes_[i].data(), root->contents()[i]->bytes().data(), bytes_[i].size()));
            assert(root->markers()[i]->revision().revision
                && (i == 0u
                    || root->markers()[i]->revision().revision
                        > root->markers()[i - 1u]->revision().revision));
            bytes_[i].assign(bytes_[i].size(), std::uint8_t{0xeeu});
            readback(*root, i);
            writeGates(*root, i);
            mutations(root, i);
        }
        {
            core::CpuMemoryQuotaLease pressure(f_.quota, f_.cpuBytes - f_.quota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned i = 0u; i < 100u; ++i) {
                assert(!root->validate());
                assert(metalRhiTextureWriteDiagnostic(f_.registry, root->parent(0u)->id())
                    == std::string_view(writeDiagnostic));
            }
            std::pmr::set_default_resource(previous);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        assert(Root::executionDiagnostic()
            == std::string_view("RHI-METAL-EXCLUSIVE-IMAGES-CONSUMER-EXECUTION-UNAVAILABLE"));
        auto alias =
            f_.device->createTextureView(*root->parent(0u), Contract::viewDescriptor(inputs_[0u]));
        assert(alias);
        auto payload = metalRhiNativePayloadFor(*alias);
        std::weak_ptr<void> token = payload->resource.allocationReleaseToken;
        const auto marker = root->markers()[0u];
        root.reset();
        assert(!token.expired()
            && core::SharedOwnerProjection::sameOwner(payload->immutableRevision(), marker));
        assert(metalRhiTextureWriteDiagnostic(f_.registry, alias->id())
            == std::string_view(writeDiagnostic));
        f_.registry->unregisterResource(alias->id());
        f_.registry->registerTextureView(*alias);
        assert(metalRhiTextureWriteDiagnostic(f_.registry, alias->id())
            == std::string_view(writeDiagnostic));
        alias.reset();
        payload.reset();
        assert(token.expired() && f_.device->memoryStats().allocatedBytes == gpu);
        root = prepare();
        assert(root && !root->validate());
        token = root->images()[0u].parent().texture->resource.allocationReleaseToken;
        f_.device.reset();
        assert(root->validate() == std::string_view("RHI-METAL-EXCLUSIVE-IMAGES-ISSUER-RETIRED")
            && !token.expired());
        root.reset();
        assert(token.expired() && !inspectSharedMetalRhiCommandCapacity().charged);
        std::printf(
            "ExclusiveClosedConsumerImages/1: fresh3 NPOT selected mips, source/snapshot%llu texture%llu owners%zu coldPeak%llu; real upload Completed, exact poisoned readback, write escapes/aliases/native owners/issuer retirement, exact-one-less CPU/GPU/budgets,100 allocation0 PASS; no consumer/draw\n",
            static_cast<unsigned long long>(totalBytes_),
            static_cast<unsigned long long>(estimate),
            owners,
            static_cast<unsigned long long>(peak));
    }

private:
    static constexpr const char* writeDiagnostic = "RHI-METAL-IMMUTABLE-TEXTURE-WRITE-FORBIDDEN";

    rhi::ResourceBarrierDesc dependency(
        const Content& content, rhi::ResourceState before, rhi::ResourceState after) const
    {
        using namespace rhi;
        ResourceBarrierDesc desc;
        desc.resource = content.image().parent;
        desc.textureSubresources = content.image().range;
        desc.before = before;
        desc.after = after;
        const bool read = before == ResourceState::ShaderRead;
        desc.sourceStages = read ? PipelineStage::FragmentShader : PipelineStage::Transfer;
        desc.sourceAccess = read ? ResourceAccess::ShaderRead : ResourceAccess::TransferRead;
        desc.destinationStages = read ? PipelineStage::Transfer : PipelineStage::FragmentShader;
        desc.destinationAccess = read ? ResourceAccess::TransferRead : ResourceAccess::ShaderRead;
        return desc;
    }

    void readback(const Root& root, std::size_t i) const
    {
        using namespace rhi;
        using namespace rhi_metal;
        const auto& content = *root.contents()[i];
        auto buffer = f_.device->createBuffer({"SealedImageReadback",
            content.bytes().size(),
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        assert(buffer);
        core::ByteBuffer actual(
            content.bytes().size(), std::uint8_t{0xcdu}, &core::defaultCpuMemoryResource());
        assert(f_.device->writeBuffer(*buffer, actual.data(), actual.size()).success);
        buffer->sealHostWrites();
        auto encoder = f_.device->createCommandEncoder({});
        assert(encoder);
        assert(encoder
                ->transition(*root.parent(i),
                    dependency(content, ResourceState::ShaderRead, ResourceState::CopySrc))
                .success);
        auto pass = encoder->beginTransferPass({});
        assert(pass);
        TextureToBufferCopyDesc copy;
        copy.sourceTexture = content.image().parent;
        copy.destinationBuffer = buffer->id();
        copy.textureSubresources = content.image().range;
        copy.bytesPerRow = content.bytesPerRow();
        copy.rowsPerImage = content.rowsPerImage();
        copy.extent = content.extent();
        assert(pass->copyTextureToBuffer(copy).success && pass->end().success);
        assert(encoder
                ->transition(*root.parent(i),
                    dependency(content, ResourceState::CopySrc, ResourceState::ShaderRead))
                .success);
        auto command = encoder->finish();
        assert(command && command->commandCount() == 5u);
        auto native = metalRhiNativePayloadFor(*command);
        assert(native && native->stats()->nativeBlitCopyEncoded);
        assert(f_.device->queue().submit(*command).success
            && waitMetalRhiNativeQueueIdle(f_.diagnostic));
        assert(native->readbackWasSubmitted()
            && native->readbackCompletionStatus() == MetalReadbackCompletion::Completed);
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*buffer),
            actual.data(),
            actual.size(),
            0u,
            actual.size(),
            f_.diagnostic));
        for (std::uint32_t y = 0u; y < content.extent().height; ++y)
            assert(!std::memcmp(actual.data() + y * content.bytesPerRow(),
                content.bytes().data() + y * content.bytesPerRow(),
                content.extent().width * 4u));
        assert(!root.validate());
    }

    void writeGates(const Root& root, std::size_t i) const
    {
        using namespace rhi;
        using namespace rhi_metal;
        auto alias =
            f_.device->createTextureView(*root.parent(i), Contract::viewDescriptor(inputs_[i]));
        assert(alias);
        assert(core::SharedOwnerProjection::sameOwner(
            metalRhiNativePayloadFor(*alias)->immutableRevision(), root.markers()[i]));
        auto arena = f_.device->createDescriptorArena({"ImmutableDescriptors", 2u, 2u});
        assert(arena);
        DescriptorLayoutDesc layout;
        layout.name = "ImmutableDescriptors";
        layout.bindings = {{0u, DescriptorType::StorageTexture, ShaderStage::Fragment}};
        auto storage = arena->allocateDescriptorSet({"StorageDenied", layout});
        assert(storage);
        layout.bindings[0u].type = DescriptorType::SampledTexture;
        auto sampled = arena->allocateDescriptorSet({"SampledAllowed", layout});
        assert(sampled);
        for (const auto id : {root.parent(i)->id(), root.view(i)->id(), alias->id()}) {
            const auto storageDenied = storage->write({0u,
                DescriptorType::StorageTexture,
                id,
                DescriptorResourceKind::TextureView,
                ResourceState::ShaderWrite});
            if (storageDenied.diagnostics != writeDiagnostic)
                std::fprintf(stderr,
                    "Storage seal expected %s got %s\n",
                    writeDiagnostic,
                    storageDenied.diagnostics.c_str());
            assert(storageDenied.diagnostics == writeDiagnostic);
            if (id != root.parent(i)->id())
                assert(sampled
                        ->write({0u,
                            DescriptorType::SampledTexture,
                            id,
                            DescriptorResourceKind::TextureView})
                        .success);
            auto encoder = f_.device->createCommandEncoder({});
            assert(encoder);
            auto pass = encoder->beginTransferPass({});
            assert(pass);
            BufferToTextureCopyDesc fromBuffer;
            fromBuffer.destinationTexture = id;
            TextureToTextureCopyDesc fromTexture;
            fromTexture.destinationTexture = id;
            UploadTextureDesc upload;
            upload.destinationTexture = id;
            const auto deniedBuffer = pass->copyBufferToTexture(fromBuffer);
            assert(deniedBuffer.diagnostics == writeDiagnostic
                && deniedBuffer.recordedCommandCount == 1u);
            const auto deniedTexture = pass->copyTextureToTexture(fromTexture);
            assert(deniedTexture.diagnostics == writeDiagnostic
                && deniedTexture.recordedCommandCount == 1u);
            const auto deniedUpload = pass->uploadTexture(upload);
            assert(deniedUpload.diagnostics == writeDiagnostic
                && deniedUpload.recordedCommandCount == 1u);
            assert(pass->end().success);
            pass.reset();
            encoder.reset();
            auto native = createMetalRhiNativeCommandPayload(
                "NativeWriteDenial", f_.registry, &f_.diagnostic);
            assert(native);
            assert(!native->recordBlitBufferToTexture(fromBuffer, f_.diagnostic)
                && f_.diagnostic == writeDiagnostic);
            assert(!native->recordBlitTextureCopy(fromTexture, f_.diagnostic)
                && f_.diagnostic == writeDiagnostic);
            assert(!native->recordUploadStagingTextureCopy({}, 0u, upload, f_.diagnostic)
                && f_.diagnostic == writeDiagnostic);
            assert(!native->stats()->nativeBlitCopyEncoded
                && !native->stats()->nativeRenderEncoderCreated);
            for (unsigned attachment = 0u; attachment < 3u; ++attachment) {
                RenderPassDesc desc;
                if (attachment == 0u)
                    desc.colorAttachments = {
                        {"", id, TextureFormat::Rgba8Unorm, LoadOp::Clear, StoreOp::Store}};
                if (attachment == 1u) {
                    desc.hasDepthStencilAttachment = true;
                    desc.depthStencilAttachment.target = id;
                    desc.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
                }
                if (attachment == 2u) {
                    desc.colorAttachments = {{"",
                        f_.view->id(),
                        TextureFormat::Rgba8Unorm,
                        LoadOp::Clear,
                        StoreOp::Store,
                        true,
                        id}};
                }
                auto denied = f_.device->createCommandEncoder({});
                assert(denied && !denied->beginRenderPass(desc));
                assert(!native->recordRenderPassBegin(desc, f_.diagnostic)
                    && f_.diagnostic == writeDiagnostic
                    && !native->stats()->nativeRenderEncoderCreated);
            }
            native->abandonUnsubmitted();
        }
    }

    void mutations(const std::shared_ptr<const Root>& root, std::size_t i) const
    {
        using namespace rhi;
        using namespace rhi_metal;
        auto& contents = const_cast<Root::Contents&>(root->contents());
        const auto savedContent = contents[i];
        contents[i] = std::shared_ptr<const Content>(root, contents[i].get());
        assert(root->validate());
        contents[i] = savedContent;
        auto& markers = const_cast<Root::Markers&>(root->markers());
        const auto savedMarker = markers[i];
        markers[i] = markers[(i + 1u) % markers.size()];
        assert(root->validate());
        markers[i] = savedMarker;
        markers[i] = std::shared_ptr<const Root::Marker>(root, markers[i].get());
        assert(root->validate());
        markers[i] = savedMarker;
        auto& allocation = const_cast<ResourceAllocation&>(root->parent(i)->allocation());
        const auto reported = allocation.estimatedSizeBytes;
        ++allocation.estimatedSizeBytes;
        assert(root->validate());
        allocation.estimatedSizeBytes = reported;
        auto& desc = const_cast<TextureDesc&>(root->parent(i)->desc());
        const auto memory = desc.memory;
        desc.memory = MemoryUsage::CpuToGpu;
        assert(root->validate());
        desc.memory = memory;
        auto barrier =
            dependency(*root->contents()[i], ResourceState::ShaderRead, ResourceState::CopySrc);
        assert(setMetalRhiTextureStateForBarrier(*root->parent(i), barrier).success
            && root->validate());
        barrier =
            dependency(*root->contents()[i], ResourceState::CopySrc, ResourceState::ShaderRead);
        assert(setMetalRhiTextureStateForBarrier(*root->parent(i), barrier).success
            && !root->validate());
        MetalNativeIdentityMutationProbe::verify(root->images()[i].parent().texture->resource,
            root->images()[(i + 1u) % root->images().size()].parent().texture->resource,
            [&] {
                assert(root->validate());
            });
        MetalNativeIdentityMutationProbe::verify(root->images()[i].view().textureView->resource,
            root->images()[(i + 1u) % root->images().size()].view().textureView->resource,
            [&] {
                assert(root->validate());
            });
        for (const bool parent : {false, true}) {
            const auto id = parent ? root->parent(i)->id() : root->view(i)->id();
            f_.registry->unregisterResource(id);
            assert(root->validate());
            if (parent)
                f_.registry->registerTexture(*root->parent(i));
            else
                f_.registry->registerTextureView(*root->view(i));
            assert(!root->validate()
                && metalRhiTextureWriteDiagnostic(f_.registry, id)
                    == std::string_view(writeDiagnostic));
        }
    }

    OrdinaryGraphicsFixture& f_;
    std::array<core::ByteBuffer, Contract::imageCount> bytes_;
    Contract::Inputs inputs_;
    std::uint64_t totalBytes_ = 0u;
};
} // namespace arp::test
