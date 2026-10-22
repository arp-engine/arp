#pragma once

#include "core/math/half_float.h"
#include "renderer/raster_viewport.h"
#include "tests/fixtures/cloud/cloud_compute_probe.h"

#include <cmath>
#include <limits>

namespace arp::tests::cloud {

class GraphicsProbe final {
public:
    static inline const char* missingEntry = nullptr;
    static inline bool failQueueInvocation = false;

    static VKAPI_ATTR VkResult VKAPI_CALL queueSubmit(
        VkQueue queue, std::uint32_t count, const VkSubmitInfo* info, VkFence fence)
    {
        if (failQueueInvocation) {
            failQueueInvocation = false;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        return vkQueueSubmit(queue, count, info, fence);
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (missingEntry && std::strcmp(name, missingEntry) == 0)
            return nullptr;
        if (std::strcmp(name, "vkQueueSubmit") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(queueSubmit);
        return vkGetDeviceProcAddr(device, name);
    }

    static void verify(ComputeProbe& raw,
        const core::pmr::Vector<renderer::cloud::CloudGpuSample>& expected,
        const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime,
        bool dropWrapper,
        bool retireSource = true,
        bool unproven = false)
    {
        GraphicsProbe probe(raw);
        probe.run(expected, runtime, dropWrapper, retireSource, unproven);
    }

private:
    explicit GraphicsProbe(ComputeProbe& raw) : raw_(raw), device_(raw.device_)
    {
        using namespace rhi;
        assert(raw.retained_ && raw.sampledDepth_);
        bindings_.name = "Cloud.RetainedComposite";
        bindings_.bindings = {{0u, DescriptorType::UniformBuffer, ShaderStage::Fragment},
            {1u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Fragment},
            {2u, DescriptorType::SampledTexture, ShaderStage::Fragment},
            {3u, DescriptorType::SampledTexture, ShaderStage::Fragment}};
        for (unsigned i : {2u, 3u}) {
            auto& b = bindings_.bindings[i];
            b.textureViewDimensionConstrained = true;
            b.textureViewDimension = TextureViewDimension::Texture2D;
            b.sampledTextureFilterable = false;
            b.depthTexture = i == 3u;
        }
        PipelineLayoutDesc ld;
        ld.descriptorLayouts.push_back(bindings_);
        ld.descriptorSets.push_back({0u, bindings_.name});
        layout_ = device_.createPipelineLayout(ld);
        assert(layout_);
        const auto path = core::pmr::String(ARP_CLOUD_SHADER_DIR) + "/cloud_composite.slang";
        package_ = renderer::detail::makeGraphicsShaderPackage("Cloud.RetainedComposite", path, 0u);
        assert(renderer::hydrateShaderPackageReflectionFromArtifact(package_));
        for (const auto& b : bindings_.bindings)
            package_.reflection.descriptorBindings.push_back(makeShaderReflectionBinding(0u, b));
        auto pd = renderer::detail::makeRenderPipelineDesc(
            "Cloud.RetainedComposite", *layout_, package_, TextureFormat::Rgba16Float, raw.extent_);
        renderer::useDynamicRasterViewport(pd);
        pd.colorTargets = {{TextureFormat::Rgba16Float}};
        pd.depthStencil.depthTestEnabled = pd.depthStencil.depthWriteEnabled = false;
        pd.cullMode = CullMode::None;
        pipeline_ = device_.createRenderPipeline(pd);
        assert(pipeline_);
        // Immutable factory state must follow its original definition even after
        // the caller mutates/destroys that descriptor.
        pd.colorTargets[0].writeMask = ColorWriteMask::Red;
        rejectedPipeline_ = device_.createRenderPipeline(pd);
        assert(rejectedPipeline_);
        pd.colorTargets[0].writeMask = ColorWriteMask::All;
        arena_ = device_.createDescriptorArena({"Cloud.RetainedComposite", 1u, 4u});
        assert(arena_);
        set_ = arena_->allocateDescriptorSet({bindings_.name, bindings_});
        assert(set_);
        scene_ =
            texture("Cloud.RetainedScene", TextureUsage::Sampled | TextureUsage::RenderAttachment);
        target_ = texture("Cloud.RetainedCompositeTarget",
            TextureUsage::Sampled | TextureUsage::RenderAttachment | TextureUsage::CopySrc);
        TextureViewDesc vd;
        vd.label = "Cloud.RetainedSceneView";
        vd.format = TextureFormat::Rgba16Float;
        vd.usage = TextureUsage::Sampled;
        vd.aspect = TextureAspect::Color;
        sceneView_ = device_.createTextureView(*scene_, vd);
        targetView_ = device_.createTextureView(*target_, vd);
        assert(sceneView_ && targetView_);
        assert(
            set_->write({0u,
                            DescriptorType::UniformBuffer,
                            raw.constants_->id(),
                            DescriptorResourceKind::Buffer})
                .success);
        assert(
            set_->write({1u,
                            DescriptorType::ReadOnlyStorageBuffer,
                            raw.output_->id(),
                            DescriptorResourceKind::Buffer})
                .success);
        assert(
            set_->write({2u,
                            DescriptorType::SampledTexture,
                            sceneView_->id(),
                            DescriptorResourceKind::TextureView})
                .success);
        assert(
            set_->write({3u,
                            DescriptorType::SampledTexture,
                            raw.depthView_->id(),
                            DescriptorResourceKind::TextureView})
                .success);
        const auto bytes = (raw.extent_.height + 4u) * rowPitch;
        staging_ = raw.buffer("Cloud.RetainedCompositeReadback",
            bytes,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst);
        auto payload = rhi_vulkan::vulkanRhiNativePayload(*staging_);
        core::pmr::String diagnostic;
        assert(payload->backendMappedData());
        std::memset(payload->backendMappedData(), 0xa5, bytes);
        assert(payload->backendFlushMappedWrite(0u, bytes, diagnostic));
        // Existing ordinary cold producer is outside the bounded command claim.
        render_graph::FrameGraph graph(device_);
        const auto image = graph.importTextureRef(scene_);
        graph.addPass("Cloud.RetainedSceneSeed")
            .write(image, ResourceState::ColorAttachment)
            .execute([&](render_graph::RenderContext& context) {
                RenderPassDesc pass;
                pass.colorAttachments.push_back({"HDR scene",
                    scene_->id(),
                    TextureFormat::Rgba16Float,
                    LoadOp::Clear,
                    StoreOp::Store,
                    false,
                    {},
                    {.25f, .5f, 1.5f, .625f}});
                auto encoder = context.commands().beginRenderPass(pass);
                assert(encoder && encoder->end().success);
            });
        graph.addPass("Cloud.RetainedSceneRead")
            .read(image, ResourceState::ShaderRead)
            .externalSideEffects()
            .execute([](render_graph::RenderContext&) {});
        graph.execute();
        device_.waitIdle();
    }

    std::shared_ptr<rhi::GpuTexture> texture(const char* label, rhi::TextureUsage usage)
    {
        auto result = core::shareUnique(device_.createTexture({label,
            {raw_.extent_.width, raw_.extent_.height, 1u},
            rhi::TextureFormat::Rgba16Float,
            usage}));
        assert(result);
        return result;
    }

    static bool change(rhi_vulkan::VulkanRhiRetainedCommand& command,
        rhi::GpuBuffer& buffer,
        bool read,
        core::pmr::String& diagnostic)
    {
        rhi::ResourceBarrierDesc d;
        d.resource = buffer.id();
        d.before = rhi::ResourceState::ShaderWrite;
        d.after = rhi::ResourceState::ShaderRead;
        d.sourceStages = rhi::PipelineStage::ComputeShader;
        d.destinationStages = rhi::PipelineStage::FragmentShader;
        d.sourceAccess = rhi::ResourceAccess::ShaderWrite;
        d.destinationAccess = rhi::ResourceAccess::ShaderRead;
        if (!read) {
            std::swap(d.before, d.after);
            std::swap(d.sourceStages, d.destinationStages);
            std::swap(d.sourceAccess, d.destinationAccess);
        }
        return command.transition(buffer, d, diagnostic);
    }

    bool attachment(rhi_vulkan::VulkanRhiRetainedCommand& command,
        rhi::ResourceState after,
        core::pmr::String& diagnostic)
    {
        rhi::ResourceBarrierDesc d;
        d.resource = target_->id();
        d.textureSubresources = rhi::defaultTextureSubresourceRange(target_->desc());
        d.before = target_->state();
        d.after = after;
        d.sourceStages = d.before == rhi::ResourceState::Undefined ? rhi::PipelineStage::TopOfPipe
            : d.before == rhi::ResourceState::CopySrc              ? rhi::PipelineStage::Transfer
                                                      : rhi::PipelineStage::ColorAttachmentOutput;
        d.sourceAccess = d.before == rhi::ResourceState::Undefined ? rhi::ResourceAccess::None
            : d.before == rhi::ResourceState::CopySrc ? rhi::ResourceAccess::TransferRead
                                                      : rhi::ResourceAccess::ColorAttachmentWrite;
        d.destinationStages = after == rhi::ResourceState::CopySrc
            ? rhi::PipelineStage::Transfer
            : rhi::PipelineStage::ColorAttachmentOutput;
        d.destinationAccess = after == rhi::ResourceState::CopySrc
            ? rhi::ResourceAccess::TransferRead
            : rhi::ResourceAccess::ColorAttachmentWrite;
        return command.transition(*target_, d, diagnostic);
    }

    void run(const core::pmr::Vector<renderer::cloud::CloudGpuSample>& expected,
        const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime,
        bool dropWrapper,
        bool retireSource,
        bool unproven)
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        assert(expected.size() == std::size_t(raw_.extent_.width) * raw_.extent_.height);
        constexpr std::uint64_t metadataBytes = 64u * 1024u;
        // Target1 + raw7 + composite pipeline/set2 + HDR parent/view2 + readback1.
        constexpr std::uint32_t ownerCeiling = 13u;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        core::pmr::String diagnostic;
        diagnostic.reserve(512u);
        const auto prepare = [&](std::uint32_t owners) {
            const char* error = nullptr;
            auto storage = common::CommandRetentionStorage::prepare(
                {1u, true, owners, 0u, 0u, 0u, metadataBytes}, quota, error);
            assert(storage && !error);
            auto command = prepareVulkanRhiRetainedCommand(device_, std::move(storage), diagnostic);
            assert(command);
            RenderPassDesc pass;
            pass.colorAttachments.push_back({"Cloud composite",
                target_->id(),
                TextureFormat::Rgba16Float,
                LoadOp::Clear,
                StoreOp::Store,
                false,
                {},
                {0, 0, 0, 0}});
            assert(command->prepareClearStorePass(
                pass, *target_, defaultTextureSubresourceRange(target_->desc()), diagnostic));
            return command;
        };
        const std::array<VulkanRhiRetainedDescriptorSet, 1u> rawSets{{{0u, raw_.set_.get()}}},
            sets{{{0u, set_.get()}}};
        const std::array<const GpuBuffer*, 3u> rawBuffers{
            raw_.constants_.get(), raw_.input_.get(), raw_.output_.get()};
        const std::array<const GpuBuffer*, 2u> buffers{raw_.constants_.get(), raw_.output_.get()};
        const std::array<VulkanRhiRetainedSampledTexture, 1u> rawImages{
            {{raw_.depthView_.get(), raw_.depth_.get()}}};
        const std::array<VulkanRhiRetainedSampledTexture, 2u> images{
            {{sceneView_.get(), scene_.get()}, {raw_.depthView_.get(), raw_.depth_.get()}}};
        VulkanRhiRetainedComputeDispatch compute{raw_.pipeline_.get(),
            rawSets,
            rawBuffers,
            (raw_.extent_.width + 7u) / 8u,
            (raw_.extent_.height + 7u) / 8u,
            1u};
        compute.sampledDepth = rawImages;
        VulkanRhiRetainedGraphicsDraw draw{pipeline_.get(),
            {sets, buffers, images},
            {0, 0, float(raw_.extent_.width), float(raw_.extent_.height), 0, 1},
            {0, 0, raw_.extent_.width, raw_.extent_.height},
            3u,
            1u};
        TextureToBufferCopyDesc copy;
        copy.sourceTexture = target_->id();
        copy.destinationBuffer = staging_->id();
        copy.extent = target_->desc().extent;
        copy.textureSubresources = defaultTextureSubresourceRange(target_->desc());
        copy.bufferOffsetBytes = rowPitch;
        copy.bytesPerRow = rowPitch;
        copy.rowsPerImage = raw_.extent_.height + 2u;
        const auto beforeDraw = [&](VulkanRhiRetainedCommand& c) {
            return c.dispatch(compute, diagnostic) && change(c, *raw_.output_, true, diagnostic)
                && attachment(c, ResourceState::ColorAttachment, diagnostic);
        };
        const auto readback = [&](VulkanRhiRetainedCommand& c) {
            ResourceBarrierDesc destination;
            destination.resource = staging_->id();
            destination.before = destination.after = ResourceState::CopyDst;
            destination.sourceStages = destination.destinationStages = PipelineStage::Transfer;
            destination.sourceAccess = destination.destinationAccess =
                ResourceAccess::TransferWrite;
            return change(c, *raw_.output_, false, diagnostic)
                && attachment(c, ResourceState::CopySrc, diagnostic)
                && c.transition(*staging_, destination, diagnostic)
                && c.copy(*target_, *staging_, copy, diagnostic)
                && attachment(c, ResourceState::ColorAttachment, diagnostic);
        };
        // Projected owners are admitted atomically, including deduplication of
        // uniforms/output/depth already retained by the same Compute command.
        {
            auto c = prepare(ownerCeiling - 2u);
            assert(beforeDraw(*c));
            const auto before = inspectVulkanRhiNativeResourceStats(device_);
            const auto owners = c->retentionStorage()->resourceOwnerCount();
            ResourceBarrierDesc forbidden;
            forbidden.resource = raw_.output_->id();
            forbidden.before = ResourceState::ShaderRead;
            forbidden.after = ResourceState::ShaderWrite;
            forbidden.sourceStages = forbidden.destinationStages = PipelineStage::FragmentShader;
            forbidden.sourceAccess = ResourceAccess::ShaderRead;
            forbidden.destinationAccess = ResourceAccess::ShaderWrite;
            assert(!c->transition(*raw_.output_, forbidden, diagnostic)
                && diagnostic == "RHI-VULKAN-RETENTION-STATE-FRAGMENT-READ-ONLY");
            assert(owners == 8u && !c->drawPreparedPass(*target_, draw, diagnostic));
            assert(c->retentionStorage()->resourceOwnerCount() == owners
                && inspectVulkanRhiNativeResourceStats(device_).nativeCommandBufferRecordCount
                    == before.nativeCommandBufferRecordCount);
        }
        assert(!quota->liveBytes() && raw_.output_->state() == ResourceState::ShaderWrite
            && target_->state() == ResourceState::Undefined);
        {
            auto c = prepare(ownerCeiling - 1u);
            assert(beforeDraw(*c) && c->drawPreparedPass(*target_, draw, diagnostic));
            const auto before = inspectVulkanRhiNativeResourceStats(device_);
            assert(!readback(*c) && diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
                && c->retentionStorage()->resourceOwnerCount() == ownerCeiling - 1u
                && inspectVulkanRhiNativeResourceStats(device_).nativeImageToBufferCopyRecordCount
                    == before.nativeImageToBufferCopyRecordCount);
        }
        for (bool sealed : {false, true}) {
            auto c = prepare(ownerCeiling);
            assert(beforeDraw(*c));
            const auto invalid = [&](const VulkanRhiRetainedGraphicsDraw& request) {
                const auto before = inspectVulkanRhiNativeResourceStats(device_);
                const auto owners = c->retentionStorage()->resourceOwnerCount();
                assert(!c->drawPreparedPass(*target_, request, diagnostic));
                assert(owners == c->retentionStorage()->resourceOwnerCount()
                    && before.nativeCommandBufferRecordCount
                        == inspectVulkanRhiNativeResourceStats(device_)
                            .nativeCommandBufferRecordCount);
            };
            auto bad = draw;
            bad.pipeline = nullptr;
            invalid(bad);
            bad = draw;
            bad.pipeline = rejectedPipeline_.get();
            invalid(bad);
            bad = draw;
            bad.vertexCount = 4u;
            invalid(bad);
            bad = draw;
            bad.instanceCount = 2u;
            invalid(bad);
            bad = draw;
            bad.firstVertex = 1u;
            invalid(bad);
            bad = draw;
            bad.firstInstance = 1u;
            invalid(bad);
            bad = draw;
            bad.viewport.width -= 1;
            invalid(bad);
            bad = draw;
            bad.viewport.width = std::numeric_limits<float>::quiet_NaN();
            invalid(bad);
            bad = draw;
            bad.scissor.x = -1;
            invalid(bad);
            bad = draw;
            bad.bindings.sampledTextures = {};
            invalid(bad);
            bad = draw;
            bad.bindings.buffers = {buffers.data(), 1u};
            invalid(bad);
            bad = draw;
            bad.bindings.sets = {};
            invalid(bad);
            for (const auto* name : {"vkCmdBeginRenderPass",
                     "vkCmdEndRenderPass",
                     "vkCmdBindPipeline",
                     "vkCmdBindDescriptorSets",
                     "vkCmdSetViewport",
                     "vkCmdSetScissor",
                     "vkCmdDraw"}) {
                missingEntry = name;
                invalid(draw);
                missingEntry = nullptr;
            }
            const std::array<VulkanRhiRetainedSampledTexture, 2u> aliasedImages{
                {{targetView_.get(), target_.get()}, {raw_.depthView_.get(), raw_.depth_.get()}}};
            assert(
                set_->write({2u,
                                DescriptorType::SampledTexture,
                                targetView_->id(),
                                DescriptorResourceKind::TextureView})
                    .success);
            bad = draw;
            bad.bindings.sampledTextures = aliasedImages;
            invalid(bad);
            assert(diagnostic == "RHI-VULKAN-RETENTION-SAMPLED-DEPTH-STATE");
            assert(
                set_->write({2u,
                                DescriptorType::SampledTexture,
                                sceneView_->id(),
                                DescriptorResourceKind::TextureView})
                    .success);
            // Foreign same-value state journal must not authorize reads.
            core::MutationJournal foreign;
            ResourceBarrierDesc same;
            same.resource = scene_->id();
            same.before = same.after = ResourceState::ShaderRead;
            same.textureSubresources = defaultTextureSubresourceRange(scene_->desc());
            assert(transitionVulkanRhiTexture(*scene_, same, &foreign).success);
            invalid(draw);
            foreign.rollback();
            const auto drawn = c->drawPreparedPass(*target_, draw, diagnostic);
            if (!drawn)
                std::fprintf(stderr, "retained Graphics positive draw: %s\n", diagnostic.c_str());
            assert(drawn);
            invalid(draw); // Exactly one use of the prepared pass.
            assert(c->retentionStorage()->resourceOwnerCount() == ownerCeiling - 1u);
            assert(readback(*c));
            if (sealed)
                assert(c->finish(diagnostic));
        }
        assert(!quota->liveBytes() && raw_.output_->state() == ResourceState::ShaderWrite
            && target_->state() == ResourceState::Undefined);
        auto command = prepare(ownerCeiling);
        const auto before = inspectVulkanRhiNativeResourceStats(device_);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(beforeDraw(*command));
        assert(command->drawPreparedPass(*target_, draw, diagnostic));
        assert(readback(*command)
            && command->retentionStorage()->resourceOwnerCount() == ownerCeiling);
        assert(command->finish(diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        const auto after = inspectVulkanRhiNativeResourceStats(device_);
        assert(after.nativeRenderPassCreateCount == before.nativeRenderPassCreateCount
            && after.nativeRenderAttachmentViewCreateCount
                == before.nativeRenderAttachmentViewCreateCount
            && after.nativeFramebufferCreateCount == before.nativeFramebufferCreateCount
            && after.nativeRenderPipelineCreateCount == before.nativeRenderPipelineCreateCount
            && after.nativeComputePipelineCreateCount == before.nativeComputePipelineCreateCount
            && after.nativePipelineLayoutCreateCount == before.nativePipelineLayoutCreateCount
            && after.nativeDrawRecordCount == before.nativeDrawRecordCount + 1u);
        std::array<std::weak_ptr<void>, ownerCeiling> owners{vulkanRhiNativePayload(*target_),
            vulkanRhiNativePayload(*raw_.pipeline_),
            vulkanRhiNativePayload(*raw_.set_),
            vulkanRhiNativePayload(*raw_.constants_),
            vulkanRhiNativePayload(*raw_.input_),
            vulkanRhiNativePayload(*raw_.output_),
            vulkanRhiNativePayload(*raw_.depth_),
            vulkanRhiNativePayload(*raw_.depthView_),
            vulkanRhiNativePayload(*pipeline_),
            vulkanRhiNativePayload(*set_),
            vulkanRhiNativePayload(*scene_),
            vulkanRhiNativePayload(*sceneView_),
            vulkanRhiNativePayload(*staging_)};
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
        assert(gate);
        const auto submitAllocations = core::defaultCpuAllocator().stats().allocationCount;
        if (unproven) {
            assert(retireSource);
            failQueueInvocation = true;
            assert(!command->submit({gate, 1u}, diagnostic) && !failQueueInvocation
                && diagnostic == "RHI-VULKAN-RETENTION-SUBMIT-UNPROVEN"
                && !vulkanRhiBufferStateCommitted(*raw_.output_)
                && !vulkanRhiBufferStateCommitted(*staging_)
                && !vulkanRhiTextureStateCommitted(*target_));
            assert(core::defaultCpuAllocator().stats().allocationCount == submitAllocations);
            // The following cold retry diagnostics own their strings. Measure
            // the submit path before constructing those diagnostic results.
            ResourceBarrierDesc retry;
            retry.resource = raw_.output_->id();
            retry.before = retry.after = ResourceState::ShaderWrite;
            assert(!transitionVulkanRhiBuffer(*raw_.output_, retry).success);
            retry.resource = target_->id();
            retry.before = retry.after = ResourceState::ColorAttachment;
            retry.textureSubresources = defaultTextureSubresourceRange(target_->desc());
            assert(!transitionVulkanRhiTexture(*target_, retry).success);
        } else {
            assert(command->submit({gate, 1u}, diagnostic));
            assert(core::defaultCpuAllocator().stats().allocationCount == submitAllocations);
        }
        if (retireSource) {
            raw_.pipeline_.reset();
            raw_.set_.reset();
            raw_.arena_.reset();
            raw_.layout_.reset();
            raw_.constants_.reset();
            raw_.input_.reset();
            raw_.output_.reset();
            raw_.depthView_.reset();
            raw_.depth_.reset();
            raw_.staging_.reset();
        }
        pipeline_.reset();
        rejectedPipeline_.reset();
        set_.reset();
        arena_.reset();
        layout_.reset();
        sceneView_.reset();
        scene_.reset();
        targetView_.reset();
        target_.reset();
        assert(!command->completed()
            && !command->waitUntil(std::chrono::steady_clock::now(), diagnostic));
        if (dropWrapper)
            command.reset();
        retireCompletedVulkanRhiSubmittedCommands();
        for (const auto& owner : owners)
            assert(!owner.expired());
        if (unproven) {
            command.reset();
            assert(quota->liveBytes());
            assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        } else {
            assert(gate->hostSignal(1u) == VK_SUCCESS);
            if (command)
                assert(command->waitUntil(
                    std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
            else
                assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        }
        retireCompletedVulkanRhiSubmittedCommands();
        command.reset();
        assert(!quota->liveBytes());
        for (std::size_t i = 0; i + 1u < owners.size(); ++i)
            assert(owners[i].expired() == (retireSource || i == 0u || i >= 8u));
        const auto bytes = staging_->desc().sizeBytes;
        auto payload = vulkanRhiNativePayload(*staging_);
        assert(payload->backendInvalidateMappedRead(0u, bytes, diagnostic));
        const auto* data = static_cast<const std::uint8_t*>(payload->backendMappedData());
        if (unproven) {
            for (std::uint64_t i = 0; i < bytes; ++i)
                assert(data[i] == 0xa5);
            payload.reset();
            staging_.reset();
            assert(owners.back().expired());
            std::printf(
                "retained Graphics unproven queue invocation: terminal buffer/texture state, 13 owners quarantined until actual idle, unchanged staging commandDropped=%d PASS\n",
                dropWrapper);
            return;
        }
        float worst = 0;
        for (std::uint64_t i = 0; i < bytes; ++i) {
            const auto row = i >= rowPitch ? (i - rowPitch) / rowPitch : UINT64_MAX;
            const auto column = i >= rowPitch ? (i - rowPitch) % rowPitch : UINT64_MAX;
            if (row >= raw_.extent_.height || column >= raw_.extent_.width * 8u)
                assert(data[i] == 0xa5);
        }
        for (unsigned y = 0; y < raw_.extent_.height; ++y)
            for (unsigned x = 0; x < raw_.extent_.width; ++x) {
                const auto& integral = expected[y * raw_.extent_.width + x].radianceTransmittance;
                const float reference[]{integral.x + integral.w * .25f,
                    integral.y + integral.w * .5f,
                    integral.z + integral.w * 1.5f,
                    .625f};
                std::uint16_t pixel[4];
                std::memcpy(pixel, data + rowPitch + y * rowPitch + x * 8u, sizeof(pixel));
                for (unsigned k = 0; k < 4; ++k) {
                    const auto value = core::halfToFloat(pixel[k]);
                    assert(std::isfinite(value));
                    worst = std::max(worst, std::fabs(value - reference[k]));
                    assert(std::fabs(value - reference[k]) < .002f);
                }
                assert(pixel[3] == core::floatToHalf(.625f, core::HalfFloatRounding::NearestEven));
            }
        payload.reset();
        staging_.reset();
        assert(owners.back().expired());
        std::printf(
            "retained raymarch -> Graphics cloud_composite -> padded readback: %u pixels maxError=%g 13 exact owners zero hot allocations delayed retirement commandDropped=%d PASS\n",
            raw_.extent_.width * raw_.extent_.height,
            double(worst),
            dropWrapper);
    }

    static constexpr std::uint64_t rowPitch = 256u;
    ComputeProbe& raw_;
    rhi::GpuDevice& device_;
    rhi::DescriptorLayoutDesc bindings_;
    rhi::ShaderPackageDesc package_;
    core::UniquePtr<rhi::PipelineLayout> layout_;
    core::UniquePtr<rhi::RenderPipeline> pipeline_, rejectedPipeline_;
    core::UniquePtr<rhi::DescriptorArena> arena_;
    core::UniquePtr<rhi::DescriptorSet> set_;
    std::shared_ptr<rhi::GpuTexture> scene_, target_;
    core::UniquePtr<rhi::GpuTextureView> sceneView_, targetView_;
    std::shared_ptr<rhi::GpuBuffer> staging_;
};

} // namespace arp::tests::cloud
