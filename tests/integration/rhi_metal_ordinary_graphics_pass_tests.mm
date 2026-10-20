#include "../fixtures/metal_native_identity_fixture.h"
#include "../fixtures/metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/gpu_extensions.h"

#include <Metal/Metal.h>
#include <cstring>

#ifdef NDEBUG
#error Native pass preparation tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;
using PassFixture = arp::test::OrdinaryGraphicsPassFixture;
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 5);
    @autoreleasepool {
        std::shared_ptr<core::CpuMemoryQuota> quota;
        {
            PassFixture f(argv[1], argv[2]);
            quota = f.graphics.quota;
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            auto prepared = f.prepare();
            assert(prepared->validateObservation().success);
            std::pmr::set_default_resource(previous);
            assert(prepared->quota() == quota && prepared->native().attachmentCount == 1u);
            assert(core::inspectCpuAllocationContaining(prepared.get()).quota == quota.get());
            auto* native =
                reinterpret_cast<MTLRenderPassDescriptor*>(prepared->native().descriptor.get());
            auto expected = reinterpret_cast<id<MTLTexture>>(
                metalRhiNativePayloadFor(*f.view)->resource.nativeObject.get());
            assert(native.colorAttachments[0].texture == expected
                && native.colorAttachments[0].loadAction == MTLLoadActionClear
                && native.colorAttachments[0].storeAction == MTLStoreActionStore);
            assert(native.colorAttachments[0].clearColor.red == 0.2f
                && native.colorAttachments[0].clearColor.alpha == 0.8f);
            for (const auto op : {LoadOp::Load, LoadOp::DontCare}) {
                f.desc.colorAttachments[0].loadOp = op;
                f.desc.colorAttachments[0].storeOp = StoreOp::DontCare;
                auto variant = f.prepare();
                auto* lowered =
                    reinterpret_cast<MTLRenderPassDescriptor*>(variant->native().descriptor.get());
                assert(lowered.colorAttachments[0].loadAction
                    == (op == LoadOp::Load ? MTLLoadActionLoad : MTLLoadActionDontCare));
                assert(lowered.colorAttachments[0].storeAction == MTLStoreActionDontCare);
            }
            f.desc.colorAttachments[0].loadOp = LoadOp::Clear;
            f.desc.colorAttachments[0].storeOp = StoreOp::Store;
            f.desc.colorAttachments[0].target = f.target->id();
            auto whole = f.prepare();
            assert(whole->validateObservation().success);
            auto* wholeNative =
                reinterpret_cast<MTLRenderPassDescriptor*>(whole->native().descriptor.get());
            assert(wholeNative.colorAttachments[0].texture
                == reinterpret_cast<id<MTLTexture>>(
                    metalRhiNativePayloadFor(*f.target)->resource.nativeObject.get()));
            assert(whole->native().attachments[0].resource.texture
                && !whole->native().attachments[0].resource.textureView);
            f.desc.colorAttachments[0].target = f.view->id();
            auto bad = f.desc;
            bad.layeredRendering = true;
            f.reject(bad, "DOMAIN-UNIMPLEMENTED");
            bad = f.desc;
            bad.colorAttachments[0].loadOp = static_cast<LoadOp>(255);
            f.reject(bad, "ATTACHMENT-OP-INVALID");
            bad = f.desc;
            bad.colorAttachments[0].storeOp = static_cast<StoreOp>(255);
            f.reject(bad, "ATTACHMENT-OP-INVALID");
            bad = f.desc;
            bad.colorAttachments[0].target = {};
            f.reject(bad, "ATTACHMENT-MISSING");
            bad = f.desc;
            bad.colorAttachments[0].format = TextureFormat::Bgra8Unorm;
            f.reject(bad, "ATTACHMENT-MISSING");
            bad = f.desc;
            bad.colorAttachments[0].resolveTarget = f.view->id();
            f.reject(bad, "RESOLVE-TARGET-FLAG-MISMATCH");
            bad = f.desc;
            bad.colorAttachments[0].hasResolveTarget = true;
            bad.colorAttachments[0].resolveTarget = f.view->id();
            f.reject(bad, "RESOLVE-ATTACHMENT-MISSING");
            bad = f.desc;
            bad.colorAttachments.resize(kMetalRenderPassColorSlots + 1u, bad.colorAttachments[0]);
            f.reject(bad, "UNSUPPORTED-OR-INVALID");
            bad = f.desc;
            bad.colorAttachments.push_back(bad.colorAttachments[0]);
            f.reject(bad, "PROVIDER-MISMATCH");
            auto small = f.graphics.device->createTexture({"Mismatch",
                {2u, 2u, 1u},
                TextureFormat::Rgba8Unorm,
                TextureUsage::RenderAttachment,
                MemoryUsage::GpuOnly,
                ResourceState::ColorAttachment});
            assert(small);
            auto smallView = f.graphics.device->createTextureView(*small,
                {"MismatchView", TextureFormat::Rgba8Unorm, TextureUsage::RenderAttachment});
            assert(smallView);
            bad = f.desc;
            bad.colorAttachments.push_back(bad.colorAttachments[0]);
            bad.colorAttachments[1].target = smallView->id();
            f.reject(bad, "EXTENT-MISMATCH");
            {
                core::CpuMemoryQuotaLease pressure(quota, f.graphics.cpuBytes - quota->liveBytes());
                f.reject(f.desc, "ALLOCATION-FAILED");
            }
            assert(!MetalRhiOrdinaryPreparedRenderPass::prepare(f.desc,
                f.graphics.provider,
                core::makeShared<MetalRhiResourceRegistry>(),
                f.graphics.budget(),
                quota,
                f.graphics.diagnostic));
            assert(!MetalRhiOrdinaryPreparedRenderPass::prepare(f.desc,
                f.graphics.provider,
                f.graphics.registry,
                f.graphics.budget(),
                core::makeShared<core::CpuMemoryQuota>(f.graphics.cpuBytes),
                f.graphics.diagnostic));
            f.desc.colorAttachments.clear();
            f.graphics.provider.reset();
            assert(prepared->validateObservation().success
                && native.colorAttachments[0].texture == expected);
            auto payload = metalRhiNativePayloadFor(*f.view);
            auto original = payload->resource.nativeObject;
            payload->resource.nativeObject.reset();
            assert(!prepared->validateObservation().success);
            payload->resource.nativeObject = original;
            assert(prepared->validateObservation().success);
            f.view.reset();
            assert(!prepared->validateObservation().success
                && native.colorAttachments[0].texture == expected);
            assert(whole->validateObservation().success);
        }
        assert(!quota->liveBytes());
        {
            PassFixture f(argv[1], argv[2]);
            auto prepared = f.prepare();
            f.target.reset();
            assert(!prepared->validateObservation().success);
        }
        for (unsigned mode = 0; mode < 2; ++mode) {
            PassFixture f(argv[mode ? 3 : 1], argv[mode ? 4 : 2], mode == 1);
            const auto* indexed = static_cast<BindlessCrossExtension*>(
                f.graphics.device->queryInterface(kExtensionBindlessCross));
            assert(indexed && indexed->capabilities().preparedGraphicsBindingContractVersion == 0u);
            std::array<core::UniquePtr<GpuTexture>, 2u> extraTextures;
            std::array<core::UniquePtr<GpuTextureView>, 2u> extraViews;
            const std::array<std::array<std::uint8_t, 4u>, 3u> colors{
                {{64u, 128u, 192u, 255u}, {128u, 192u, 64u, 255u}, {192u, 64u, 128u, 255u}}};
            auto binding = f.graphics.prepare();
            auto pass = f.prepare();
            // Initialize the sampled scalar input with exact upload metadata.
            std::array<std::uint8_t, kTextureUploadRowPitchAlignment> input{};
            input[0] = 64u;
            input[1] = 128u;
            input[2] = 192u;
            input[3] = 255u;
            auto uploadCommand =
                f.graphics.device->createCommandEncoder({"OrdinaryScalarInitialize"});
            assert(uploadCommand);
            auto uploadPass = uploadCommand->beginTransferPass({"OrdinaryScalarUpload"});
            assert(uploadPass);
            UploadTextureDesc upload;
            upload.destinationTexture = f.graphics.texture->id();
            upload.extent = {1u, 1u, 1u};
            upload.format = TextureFormat::Rgba8Unorm;
            upload.bytesPerRow = input.size();
            upload.rowsPerImage = 1u;
            upload.subresources = {0u, 1u, 0u, 1u, TextureAspect::Color};
            upload.pixelData = input.data();
            upload.pixelDataSize = input.size();
            upload.backendOwnedStaging = true;
            upload.frameSerial = 1u;
            upload.resourceGeneration = 1u;
            // The fixture sampled-only texture has no CopyDst usage; recreate it
            // through declared usage before publishing the new descriptor.
            f.graphics.view.reset();
            f.graphics.texture.reset();
            f.graphics.texture = f.graphics.device->createTexture({"UploadedInput",
                {1u, 1u, 1u},
                TextureFormat::Rgba8Unorm,
                TextureUsage::Sampled | TextureUsage::CopyDst,
                MemoryUsage::GpuOnly,
                ResourceState::CopyDst});
            assert(f.graphics.texture);
            f.graphics.view = f.graphics.device->createTextureView(*f.graphics.texture,
                {"UploadedView", TextureFormat::Rgba8Unorm, TextureUsage::Sampled});
            assert(f.graphics.view);
            f.graphics.write();
            binding = f.graphics.prepare();
            upload.destinationTexture = f.graphics.texture->id();
            assert(uploadPass->uploadTexture(upload).success);
            if (mode)
                for (unsigned i = 0; i < extraTextures.size(); ++i) {
                    extraTextures[i] = f.graphics.device->createTexture({"ArrayInput",
                        {1u, 1u, 1u},
                        TextureFormat::Rgba8Unorm,
                        TextureUsage::Sampled | TextureUsage::CopyDst,
                        MemoryUsage::GpuOnly,
                        ResourceState::CopyDst});
                    assert(extraTextures[i]);
                    extraViews[i] = f.graphics.device->createTextureView(*extraTextures[i],
                        {"ArrayView", TextureFormat::Rgba8Unorm, TextureUsage::Sampled});
                    assert(extraViews[i]);
                    std::memcpy(input.data(), colors[i + 1u].data(), 4u);
                    upload.destinationTexture = extraTextures[i]->id();
                    assert(uploadPass->uploadTexture(upload).success);
                    DescriptorBindingWrite write{7u,
                        DescriptorType::SampledTexture,
                        extraViews[i]->id(),
                        DescriptorResourceKind::TextureView};
                    write.arrayElement = i + 1u;
                    assert(f.graphics.set->write(write).success);
                }
            assert(uploadPass->end().success);
            assert(uploadCommand
                    ->transition(*f.graphics.texture,
                        {0u, 1u, 0u, 1u, TextureAspect::Color},
                        ResourceState::CopyDst,
                        ResourceState::ShaderRead)
                    .success);
            if (mode)
                for (const auto& texture : extraTextures)
                    assert(uploadCommand
                            ->transition(*texture,
                                {0u, 1u, 0u, 1u, TextureAspect::Color},
                                ResourceState::CopyDst,
                                ResourceState::ShaderRead)
                            .success);
            binding = f.graphics.prepare();
            std::memcpy(input.data(), colors[0].data(), 4u);
            auto uploaded = uploadCommand->finish();
            assert(uploaded);
            assert(f.graphics.device->queue().submit(*uploaded).success);
            f.graphics.device->waitIdle();
            const auto command = [&](unsigned owners) {
                CommandRetentionBudget budget{1u, true, owners, 0u, 0u, 0u, f.graphics.cpuBytes};
                const char* id = nullptr;
                auto storage =
                    common::CommandRetentionStorage::prepare(budget, f.graphics.quota, id);
                assert(storage && !id);
                auto result = createMetalRhiNativeCommandPayload("BoundedOrdinaryGraphics",
                    f.graphics.registry,
                    &f.graphics.diagnostic,
                    0u,
                    0u,
                    0u,
                    std::move(storage));
                assert(result);
                return result;
            };
            auto& native = *f.graphics.provider->native();
            assert(native.graphicsBufferFacts[0].stage == ShaderStage::Vertex
                && native.graphicsBufferFacts[0].version == 1u
                && native.graphicsBufferFacts[0].entries.empty());
            assert(native.graphicsBufferFacts[1].stage == ShaderStage::Fragment
                && native.graphicsBufferFacts[1].version == 1u
                && native.graphicsBufferFacts[1].entries.size() == (mode ? 1u : 0u));
            assert(
                native.graphicsTextureFactsVersion == 1u && !native.graphicsTextureFacts.empty());
            const auto rejectRecording = [&](const char* expected) {
                auto rejected = command(128u);
                assert(!rejected->recordOrdinaryPreparedRenderPassBegin(
                    pass, binding, f.graphics.diagnostic));
                if (f.graphics.diagnostic.find(expected) == f.graphics.diagnostic.npos)
                    std::fprintf(
                        stderr, "Expected %s: %s\n", expected, f.graphics.diagnostic.c_str());
                assert(f.graphics.diagnostic.find(expected) != f.graphics.diagnostic.npos);
                assert(!rejected->stats()->nativeRenderEncoderCreated
                    && !rejected->retentionStorage()->resourceOwnerCount());
                rejected->abandonUnsubmitted();
            };
            auto& reflected = const_cast<ShaderPackageDesc&>(f.graphics.provider->interface())
                                  .reflection.descriptorBindings.front();
            const auto originalReflection = reflected;
            for (const auto type :
                {DescriptorType::ReadOnlyStorageBuffer, DescriptorType::UniformBuffer}) {
                reflected.type = type;
                rejectRecording("REFLECTION-CAPTURE-MISMATCH");
                reflected = originalReflection;
            }
            for (const auto type : {DescriptorType::StorageBuffer,
                     DescriptorType::StorageTexture,
                     DescriptorType::InputAttachment}) {
                reflected.type = type;
                rejectRecording("RESOURCE-DOMAIN-UNIMPLEMENTED");
                reflected = originalReflection;
            }
            reflected.depthTexture = true;
            rejectRecording("RESOURCE-DOMAIN-UNIMPLEMENTED");
            reflected = originalReflection;
            native.graphicsTextureFactsVersion = 0u;
            rejectRecording("TEXTURE-FACTS-UNQUERIED");
            native.graphicsTextureFactsVersion = 1u;
            // Mutate the consumed fragment fact, including one individual array
            // element, to prove lookup uses native member index and stage.
            auto factIndex = native.graphicsTextureFacts.size();
            for (std::size_t i = 0; i < native.graphicsTextureFacts.size(); ++i)
                if (native.graphicsTextureFacts[i].stage == ShaderStage::Fragment
                    && (mode == 0u || native.graphicsTextureFacts[i].argumentIndex == 1u)) {
                    factIndex = i;
                    break;
                }
            assert(factIndex < native.graphicsTextureFacts.size());
            const auto originalFact = native.graphicsTextureFacts[factIndex];
            native.graphicsTextureFacts[factIndex].textureType = MTLTextureType3D;
            rejectRecording("TEXTURE-ABI-MISMATCH");
            native.graphicsTextureFacts[factIndex] = originalFact;
            native.graphicsTextureFacts[factIndex].scalarDataType = MTLDataTypeUInt;
            rejectRecording("TEXTURE-ABI-MISMATCH");
            native.graphicsTextureFacts[factIndex] = originalFact;
            native.graphicsTextureFacts[factIndex].depth = true;
            rejectRecording("TEXTURE-ABI-MISMATCH");
            native.graphicsTextureFacts[factIndex] = originalFact;
            native.graphicsTextureFacts[factIndex].stage = ShaderStage::Compute;
            rejectRecording("TEXTURE-ABI-MISMATCH");
            native.graphicsTextureFacts[factIndex] = originalFact;
            native.graphicsTextureFacts.push_back(originalFact);
            rejectRecording("TEXTURE-FACT-AMBIGUOUS");
            native.graphicsTextureFacts.pop_back();
            if (!mode) {
                auto sampler = metalRhiNativePayloadFor(*f.graphics.sampler);
                sampler->comparisonEnabled = true;
                rejectRecording("RESOURCE-DOMAIN-UNIMPLEMENTED");
                sampler->comparisonEnabled = false;
            }
            auto replacement = f.graphics.device->createTexture({"NativeIdentityReplacement",
                {1u, 1u, 1u},
                TextureFormat::Rgba8Unorm,
                TextureUsage::Sampled,
                MemoryUsage::GpuOnly,
                ResourceState::ShaderRead});
            assert(replacement);
            auto replacementView = f.graphics.device->createTextureView(*replacement,
                {"ReplacementView", TextureFormat::Rgba8Unorm, TextureUsage::Sampled});
            assert(replacementView);
            auto replacementSampler = f.graphics.device->createSampler({});
            assert(replacementSampler);
            const auto mutations = [&](auto&& rejected) {
                for (const auto& element : binding->payload()->descriptors->bindings) {
                    auto& resource = element.textureView ? element.textureView->resource
                                                         : element.sampler->resource;
                    const auto& other = element.textureView
                        ? metalRhiNativePayloadFor(*replacementView)->resource
                        : metalRhiNativePayloadFor(*replacementSampler)->resource;
                    test::MetalNativeIdentityMutationProbe::verify(resource, other, rejected);
                }
            };
            mutations([&] {
                rejectRecording("RESOURCE-NATIVE-IDENTITY-MISMATCH");
            });
            auto identityCommand = command(128u);
            assert(identityCommand->recordOrdinaryPreparedRenderPassBegin(
                pass, binding, f.graphics.diagnostic));
            const auto admittedOwners = identityCommand->retentionStorage()->resourceOwnerCount();
            const auto admittedReuse = identityCommand->stats()->nativeArgumentBufferReuseCount;
            const auto rejectConsumption = [&](const char* expected) {
                assert(!identityCommand->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic));
                assert(f.graphics.diagnostic.find(expected) != f.graphics.diagnostic.npos);
                assert(!identityCommand->recordOrdinaryPreparedGraphicsBinding(
                    binding, f.graphics.diagnostic));
                assert(f.graphics.diagnostic.find(expected) != f.graphics.diagnostic.npos);
                assert(!identityCommand->stats()->nativeDrawEncoded
                    && !identityCommand->recordingPoisoned()
                    && identityCommand->retentionStorage()->resourceOwnerCount() == admittedOwners
                    && identityCommand->stats()->nativeArgumentBufferReuseCount == admittedReuse);
            };
            mutations([&] {
                rejectConsumption("RESOURCE-NATIVE-IDENTITY-MISMATCH");
            });
            const auto otherNative =
                metalRhiNativePayloadFor(*replacementSampler)->resource.nativeObject;
            const auto ownerPipeline = binding->provider()->native();
            const auto group = std::const_pointer_cast<MetalRhiNativeDescriptorGroupPayload>(
                binding->payload()->descriptors);
            for (auto* field : {&ownerPipeline->shaderLibrary,
                     &ownerPipeline->fragmentShaderLibrary,
                     &ownerPipeline->vertexFunction,
                     &ownerPipeline->fragmentFunction,
                     &ownerPipeline->computeFunction,
                     &ownerPipeline->pipelineState,
                     &ownerPipeline->depthStencilState,
                     &group->nativeDevice,
                     &group->nativeObject}) {
                test::MetalNativeIdentityMutationProbe::verifyOwner(*field, otherNative, [&] {
                    rejectRecording("OWNER-PROJECTION-MISMATCH");
                    rejectConsumption("OWNER-PROJECTION-MISMATCH");
                });
            }
            test::MetalNativeIdentityMutationProbe::verifyOwner(
                ownerPipeline->nativeDevice, otherNative, [&] {
                    const auto observed = binding->validateObservation();
                    assert(!observed.success
                        && observed.diagnostics.find("OWNER-PROJECTION-MISMATCH")
                            != observed.diagnostics.npos);
                    assert(!identityCommand->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic));
                    assert(!identityCommand->recordOrdinaryPreparedGraphicsBinding(
                        binding, f.graphics.diagnostic));
                    assert(
                        identityCommand->retentionStorage()->resourceOwnerCount() == admittedOwners
                        && !identityCommand->stats()->nativeDrawEncoded);
                });
            if (mode) {
                auto stage = std::const_pointer_cast<MetalRhiNativePreparedFunctionBindings>(
                    binding->payload()->stages[1]);
                assert(stage->arrays.size() == 1u);
                auto& array = stage->arrays[0];
                const auto saved = array.buffer;
                const auto size = reinterpret_cast<id<MTLBuffer>>(saved.get()).length;
                auto replacementBuffer =
                    f.graphics.device->createBuffer({"ArgumentIdentityReplacement",
                        size,
                        ResourceUsage::UniformBuffer,
                        MemoryUsage::CpuToGpu,
                        ResourceState::ShaderRead});
                assert(replacementBuffer);
                const auto other =
                    metalRhiNativePayloadFor(*replacementBuffer)->resource.nativeObject;
                for (const auto& object : {other, std::shared_ptr<void>(other, saved.get())}) {
                    array.buffer = object;
                    rejectRecording("OWNER-PROJECTION-MISMATCH");
                    rejectConsumption("OWNER-PROJECTION-MISMATCH");
                    array.buffer = saved;
                }
            }
            assert(identityCommand->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic)
                && identityCommand->recordRenderPassEnd(f.graphics.diagnostic));
            identityCommand->abandonUnsubmitted();
            identityCommand.reset();
            std::printf(
                "ordinary Graphics native identity: mode=%u all scalar/array/sampler elements and generated storage reject before begin/bind/draw mutation\n",
                mode);
            auto probe = command(128u);
            assert(
                probe->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.graphics.diagnostic));
            const auto owners = probe->retentionStorage()->resourceOwnerCount();
            assert(owners > 1u && owners < 128u);
            assert(probe->recordRenderPassEnd(f.graphics.diagnostic));
            probe->abandonUnsubmitted();
            probe.reset();
            auto shortCommand = command(owners - 1u);
            assert(!shortCommand->recordOrdinaryPreparedRenderPassBegin(
                pass, binding, f.graphics.diagnostic));
            assert(!shortCommand->stats()->nativeRenderEncoderCreated
                && !shortCommand->retentionStorage()->resourceOwnerCount());
            shortCommand->abandonUnsubmitted();
            shortCommand.reset();
            for (const auto count : {0u, 1u}) {
                auto rejected = command(count);
                assert(!rejected->recordOrdinaryPreparedRenderPassBegin(
                    pass, binding, f.graphics.diagnostic));
                assert(!rejected->retentionStorage()->resourceOwnerCount()
                    && !rejected->stats()->nativeRenderEncoderCreated);
                rejected->abandonUnsubmitted();
            }
            auto recorded = command(owners);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            {
                core::CpuMemoryQuotaLease pressure(
                    f.graphics.quota, f.graphics.cpuBytes - f.graphics.quota->liveBytes());
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                assert(recorded->recordOrdinaryPreparedRenderPassBegin(
                    pass, binding, f.graphics.diagnostic));
                for (unsigned i = 0; i < 32u; ++i)
                    assert(recorded->recordOrdinaryPreparedGraphicsBinding(
                        binding, f.graphics.diagnostic));
                const auto reuse = recorded->stats()->nativeArgumentBufferReuseCount;
                native.graphicsTextureFacts[factIndex].scalarDataType = MTLDataTypeUInt;
                assert(!recorded->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic));
                assert(!recorded->recordOrdinaryPreparedGraphicsBinding(
                    binding, f.graphics.diagnostic));
                assert(recorded->retentionStorage()->resourceOwnerCount() == owners
                    && recorded->stats()->nativeArgumentBufferReuseCount == reuse);
                native.graphicsTextureFacts[factIndex] = originalFact;
                assert(recorded->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic));
                assert(recorded->recordRenderPassEnd(f.graphics.diagnostic));
                assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                std::pmr::set_default_resource(previous);
            }
            assert(recorded->finish(f.graphics.diagnostic));
            MetalRhiNativeQueueStats queue;
            auto completion = recorded->preparedCompletionState();
            assert(recorded->submit(queue, completion, {}, 0u, {}, 0u, f.graphics.diagnostic));
            completion->wait();
            // Query real GPU output bytes; this does not grant RenderDoc acceptance.
            auto readback = f.graphics.device->createBuffer({"OrdinaryPixels",
                4u * kTextureUploadRowPitchAlignment,
                ResourceUsage::CopyDst,
                MemoryUsage::GpuToCpu,
                ResourceState::CopyDst});
            assert(readback);
            auto copy = createMetalRhiNativeCommandPayload(
                "OrdinaryReadback", f.graphics.registry, &f.graphics.diagnostic);
            assert(copy);
            assert(copy->recordBlitPassBegin(f.graphics.diagnostic));
            TextureToBufferCopyDesc copyDesc;
            copyDesc.sourceTexture = f.target->id();
            copyDesc.destinationBuffer = readback->id();
            copyDesc.textureSubresources = {0u, 1u, 0u, 1u, TextureAspect::Color};
            copyDesc.bytesPerRow = kTextureUploadRowPitchAlignment;
            copyDesc.rowsPerImage = 4u;
            copyDesc.extent = {4u, 4u, 1u};
            assert(copy->recordBlitTextureToBuffer(copyDesc, f.graphics.diagnostic)
                && copy->recordBlitPassEnd(f.graphics.diagnostic)
                && copy->finish(f.graphics.diagnostic)
                && copy->submit(
                    queue, copy->preparedCompletionState(), {}, 0u, {}, 0u, f.graphics.diagnostic));
            copy->preparedCompletionState()->wait();
            std::array<std::uint8_t, 4u * kTextureUploadRowPitchAlignment> pixels{};
            assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
                pixels.data(),
                pixels.size(),
                0u,
                pixels.size(),
                f.graphics.diagnostic));
            for (unsigned y = 0; y < 4; ++y)
                for (unsigned x = 0; x < 4; ++x)
                    for (unsigned channel = 0; channel < 4; ++channel)
                        assert(pixels[y * kTextureUploadRowPitchAlignment + x * 4u + channel]
                            == (mode ? colors[channel == 3u ? 0u : x % colors.size()]
                                             [channel == 3u ? 0u : channel]
                                     : colors[0][channel]));
            std::printf(
                "bounded ordinary graphics: owners=%zu, begin/bind32/draw/end allocation0 at exhausted quota; exact 4x4 native pixels pass\n",
                owners);
            // Compose existing approved render/copy paths under one owner quota.
            const auto render = [&](const auto& cmd, const auto& selectedBinding) {
                assert(cmd->recordOrdinaryPreparedRenderPassBegin(
                           pass, selectedBinding, f.graphics.diagnostic)
                    && cmd->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic)
                    && cmd->recordRenderPassEnd(f.graphics.diagnostic));
            };
            auto unionProbe = command(128u);
            render(unionProbe, binding);
            assert(unionProbe->recordBlitPassBegin(f.graphics.diagnostic));
            assert(unionProbe->recordBlitTextureToBuffer(copyDesc, f.graphics.diagnostic));
            const auto unionOwners = unionProbe->retentionStorage()->resourceOwnerCount();
            assert(unionOwners > owners && unionOwners < 128u);
            assert(unionProbe->recordBlitPassEnd(f.graphics.diagnostic));
            render(unionProbe, binding);
            assert(unionProbe->retentionStorage()->resourceOwnerCount() == unionOwners);
            unionProbe->abandonUnsubmitted();
            unionProbe.reset();
            auto shortUnion = command(unionOwners - 1u);
            render(shortUnion, binding);
            const auto beforeCopy = shortUnion->retentionStorage()->resourceOwnerCount();
            assert(shortUnion->recordBlitPassBegin(f.graphics.diagnostic));
            assert(!shortUnion->recordBlitTextureToBuffer(copyDesc, f.graphics.diagnostic));
            assert(shortUnion->retentionStorage()->resourceOwnerCount() == beforeCopy
                && !shortUnion->stats()->nativeBlitCopyEncoded);
            assert(shortUnion->recordBlitPassEnd(f.graphics.diagnostic)
                && shortUnion->finish(f.graphics.diagnostic));
            shortUnion->abandonUnsubmitted();
            shortUnion.reset();
            for (const bool dropCommand : {false, true}) {
                auto selectedBinding = f.graphics.prepare();
                std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> observed =
                    selectedBinding;
                auto composed = command(unionOwners);
                // Reject illegal encoder overlap without consuming copy owners.
                assert(composed->recordOrdinaryPreparedRenderPassBegin(
                    pass, selectedBinding, f.graphics.diagnostic));
                assert(!composed->recordBlitPassBegin(f.graphics.diagnostic));
                assert(composed->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic)
                    && composed->recordRenderPassEnd(f.graphics.diagnostic));
                assert(composed->recordBlitPassBegin(f.graphics.diagnostic));
                const auto copyAllocations = core::defaultCpuAllocator().stats().allocationCount;
                {
                    core::CpuMemoryQuotaLease pressure(
                        f.graphics.quota, f.graphics.cpuBytes - f.graphics.quota->liveBytes());
                    auto* previous =
                        std::pmr::set_default_resource(std::pmr::null_memory_resource());
                    for (unsigned i = 0; i < 32u; ++i)
                        assert(
                            composed->recordBlitTextureToBuffer(copyDesc, f.graphics.diagnostic));
                    assert(composed->recordBlitPassEnd(f.graphics.diagnostic));
                    render(composed, selectedBinding);
                    assert(core::defaultCpuAllocator().stats().allocationCount == copyAllocations);
                    std::pmr::set_default_resource(previous);
                }
                assert(composed->retentionStorage()->resourceOwnerCount() == unionOwners
                    && composed->finish(f.graphics.diagnostic));
                auto gate = createMetalRhiNativeSharedEventPayload("OrdinaryCompositionWait");
                assert(gate && gate->nativeRuntimeBacked);
                auto done = composed->preparedCompletionState();
                selectedBinding.reset();
                assert(composed->submit(queue, done, gate, 1u, {}, 0u, f.graphics.diagnostic));
                if (dropCommand)
                    composed.reset();
                assert(!done->isComplete() && !observed.expired());
                signalMetalRhiNativeSharedEvent(gate, 1u);
                done->wait();
                assert(observed.expired());
                std::array<std::uint8_t, 4u * kTextureUploadRowPitchAlignment> combinedPixels{};
                assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
                    combinedPixels.data(),
                    combinedPixels.size(),
                    0u,
                    combinedPixels.size(),
                    f.graphics.diagnostic));
                for (unsigned y = 0; y < 4; ++y)
                    for (unsigned x = 0; x < 16; ++x)
                        assert(combinedPixels[y * kTextureUploadRowPitchAlignment + x]
                            == pixels[y * kTextureUploadRowPitchAlignment + x]);
            }
            for (const bool sealed : {false, true}) {
                auto selectedBinding = f.graphics.prepare();
                std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> observed =
                    selectedBinding;
                auto temporary = f.graphics.device->createBuffer({"CompositionCanceledDestination",
                    4u * kTextureUploadRowPitchAlignment,
                    ResourceUsage::CopyDst,
                    MemoryUsage::GpuToCpu,
                    ResourceState::CopyDst});
                assert(temporary);
                std::weak_ptr<MetalRhiNativeBufferPayload> transferOwner =
                    metalRhiNativePayloadFor(*temporary);
                auto destination = copyDesc;
                destination.destinationBuffer = temporary->id();
                auto composed = command(unionOwners);
                render(composed, selectedBinding);
                assert(composed->recordBlitPassBegin(f.graphics.diagnostic)
                    && composed->recordBlitTextureToBuffer(destination, f.graphics.diagnostic));
                if (sealed)
                    assert(composed->recordBlitPassEnd(f.graphics.diagnostic)
                        && composed->finish(f.graphics.diagnostic));
                selectedBinding.reset();
                temporary.reset();
                assert(!observed.expired() && !transferOwner.expired());
                composed->abandonUnsubmitted();
                assert(observed.expired() && transferOwner.expired());
            }
            for (const bool dropCommand : {false, true}) {
                auto selectedBinding = f.graphics.prepare();
                std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> observed =
                    selectedBinding;
                auto temporary = f.graphics.device->createBuffer({"CompositionDelayedDestination",
                    4u * kTextureUploadRowPitchAlignment,
                    ResourceUsage::CopyDst,
                    MemoryUsage::GpuToCpu,
                    ResourceState::CopyDst});
                assert(temporary);
                std::weak_ptr<MetalRhiNativeBufferPayload> transferOwner =
                    metalRhiNativePayloadFor(*temporary);
                auto destination = copyDesc;
                destination.destinationBuffer = temporary->id();
                auto composed = command(unionOwners);
                render(composed, selectedBinding);
                assert(composed->recordBlitPassBegin(f.graphics.diagnostic)
                    && composed->recordBlitTextureToBuffer(destination, f.graphics.diagnostic)
                    && composed->recordBlitPassEnd(f.graphics.diagnostic)
                    && composed->finish(f.graphics.diagnostic));
                auto gate = createMetalRhiNativeSharedEventPayload("CompositionUnionRetirement");
                assert(gate && gate->nativeRuntimeBacked);
                auto done = composed->preparedCompletionState();
                selectedBinding.reset();
                temporary.reset();
                assert(composed->submit(queue, done, gate, 1u, {}, 0u, f.graphics.diagnostic));
                if (dropCommand)
                    composed.reset();
                assert(!done->isComplete() && !observed.expired() && !transferOwner.expired());
                signalMetalRhiNativeSharedEvent(gate, 1u);
                done->wait();
                assert(observed.expired() && transferOwner.expired());
            }
            std::printf(
                "bounded ordinary composition: unionOwners=%zu; render/copy32/render allocation0 after blit begin, exact native readback and completion/cancel pass\n",
                unionOwners);
            for (const bool sealed : {false, true}) {
                auto canceledBinding = f.graphics.prepare();
                std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> observed =
                    canceledBinding;
                auto canceled = command(128u);
                assert(canceled->recordOrdinaryPreparedRenderPassBegin(
                    pass, canceledBinding, f.graphics.diagnostic));
                if (sealed)
                    assert(canceled->recordRenderPassEnd(f.graphics.diagnostic)
                        && canceled->finish(f.graphics.diagnostic));
                canceledBinding.reset();
                assert(!observed.expired());
                canceled->abandonUnsubmitted();
                assert(observed.expired());
            }
            for (const bool dropCommand : {false, true}) {
                auto heldBinding = f.graphics.prepare();
                std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> observed = heldBinding;
                std::weak_ptr<void> argument;
                if (mode)
                    argument = heldBinding->payload()->stages[1]->arrays[0].buffer;
                auto delayed = command(128u);
                assert(delayed->recordOrdinaryPreparedRenderPassBegin(
                    pass, heldBinding, f.graphics.diagnostic));
                assert(delayed->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic)
                    && delayed->recordRenderPassEnd(f.graphics.diagnostic)
                    && delayed->finish(f.graphics.diagnostic));
                auto gate = createMetalRhiNativeSharedEventPayload("OrdinaryGraphicsDelayedGate");
                assert(gate && gate->nativeRuntimeBacked);
                auto done = delayed->preparedCompletionState();
                heldBinding.reset();
                assert(delayed->submit(queue, done, gate, 1u, {}, 0u, f.graphics.diagnostic));
                if (dropCommand)
                    delayed.reset();
                assert(
                    !done->isComplete() && !observed.expired() && (!mode || !argument.expired()));
                signalMetalRhiNativeSharedEvent(gate, 1u);
                done->wait();
                assert(observed.expired() && (!mode || argument.expired()));
            }
            {
                auto failedBinding = f.graphics.prepare();
                std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> observed =
                    failedBinding;
                auto failed = command(128u);
                assert(failed->recordOrdinaryPreparedRenderPassBegin(
                    pass, failedBinding, f.graphics.diagnostic));
                assert(failed->recordRenderPassEnd(f.graphics.diagnostic)
                    && failed->finish(f.graphics.diagnostic));
                failedBinding.reset();
                auto gate = createMetalRhiNativeSharedEventPayload("OrdinaryGraphicsFailedGate");
                auto done = failed->preparedCompletionState();
                const auto before = queue.nativeQueueSubmitCount;
                core::CpuMemoryQuotaLease pressure(
                    f.graphics.quota, f.graphics.cpuBytes - f.graphics.quota->liveBytes());
                assert(!failed->submit(queue, done, gate, 1u, {}, 0u, f.graphics.diagnostic));
                assert(done->isComplete() && observed.expired()
                    && queue.nativeQueueSubmitCount == before);
            }
            {
                auto state = command(owners);
                assert(state->recordOrdinaryPreparedRenderPassBegin(
                    pass, binding, f.graphics.diagnostic));
                assert(!state->recordGraphicsPipelineBind(
                    binding->provider()->native(), f.graphics.diagnostic));
                assert(!state->recordPushConstants(
                    ShaderStage::Fragment, 0u, 4u, input.data(), f.graphics.diagnostic));
                assert(!state->recordNextSubpass(f.graphics.diagnostic));
                f.graphics.write();
                assert(!state->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic));
                assert(f.graphics.diagnostic.find("CONTENT-CHANGED") != f.graphics.diagnostic.npos);
                auto replacement = f.graphics.prepare();
                const auto priorOwners = state->retentionStorage()->resourceOwnerCount();
                const auto priorRebind = state->stats()->nativeArgumentBufferReuseCount;
                assert(!state->recordOrdinaryPreparedGraphicsBinding(
                    replacement, f.graphics.diagnostic));
                assert(state->retentionStorage()->resourceOwnerCount() == priorOwners
                    && state->stats()->nativeArgumentBufferReuseCount == priorRebind);
                assert(state->recordRenderPassEnd(f.graphics.diagnostic));
                state->abandonUnsubmitted();
                binding = replacement;
                auto repaired = command(128u);
                assert(repaired->recordOrdinaryPreparedRenderPassBegin(
                    pass, binding, f.graphics.diagnostic));
                f.graphics.write();
                assert(!repaired->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic));
                binding = f.graphics.prepare();
                assert(
                    repaired->recordOrdinaryPreparedGraphicsBinding(binding, f.graphics.diagnostic)
                    && repaired->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic)
                    && repaired->recordRenderPassEnd(f.graphics.diagnostic));
                repaired->abandonUnsubmitted();
            }
            {
                PipelineLayoutDesc authored;
                authored.descriptorLayouts = {f.graphics.inputs};
                authored.descriptorSets = {{0u, f.graphics.inputs.name}};
                auto layout = f.graphics.device->createPipelineLayout(authored);
                assert(layout);
                RenderPipelineDesc state;
                state.layout = layout.get();
                state.colorFormat = TextureFormat::Rgba8Unorm;
                state.dynamicViewportScissor = true;
                auto contract = semantics::nativeBackendCapabilityContract(Backend::Metal);
                contract.queriedFromNativeDevice = true;
                contract.queriedExtendedFeatures = true;
                auto otherProvider =
                    MetalRhiOrdinaryGraphicsProvider::create(f.graphics.provider->artifacts()[0],
                        f.graphics.provider->artifacts()[1],
                        std::move(state),
                        contract,
                        f.graphics.budget(),
                        f.graphics.quota,
                        f.graphics.diagnostic);
                assert(otherProvider);
                auto otherBinding = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(otherProvider,
                    *f.graphics.set,
                    0u,
                    f.graphics.registry,
                    f.graphics.budget(),
                    f.graphics.quota,
                    f.graphics.diagnostic);
                assert(otherBinding);
                layout.reset();
                auto transitions = command(128u);
                assert(transitions->recordOrdinaryPreparedRenderPassBegin(
                    pass, binding, f.graphics.diagnostic));
                assert(transitions->recordOrdinaryPreparedGraphicsBinding(
                           otherBinding, f.graphics.diagnostic)
                    && transitions->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic));
                assert(transitions->recordOrdinaryPreparedGraphicsBinding(
                           binding, f.graphics.diagnostic)
                    && transitions->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic)
                    && transitions->recordRenderPassEnd(f.graphics.diagnostic));
                assert(!transitions->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic));
                assert(transitions->recordOrdinaryPreparedRenderPassBegin(
                           pass, otherBinding, f.graphics.diagnostic)
                    == false);
                assert(transitions->recordOrdinaryPreparedRenderPassBegin(
                           pass, binding, f.graphics.diagnostic)
                    && transitions->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic)
                    && transitions->recordRenderPassEnd(f.graphics.diagnostic));
                transitions->abandonUnsubmitted();
            }
            {
                auto stale = command(128u);
                assert(stale->recordOrdinaryPreparedRenderPassBegin(
                    pass, binding, f.graphics.diagnostic));
                f.graphics.view.reset();
                assert(!stale->recordDraw(3u, 1u, 0u, 0u, f.graphics.diagnostic));
                assert(stale->recordRenderPassEnd(f.graphics.diagnostic));
                stale->abandonUnsubmitted();
            }
        }
    }
    std::puts(
        "ordinary pass preparation and private scalar/array recording: exact native state/data, quota/ownership, fail-closed observations, transitions and completion/cancellation pass; public versions0, RenderDoc acceptance pending");
}
