#pragma once

#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_common.h"
#include "renderer/volumetric_cloud/cloud_gpu_contract.h"
#include "rhi/backends/vulkan/vulkan_retained_command.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#include <array>
#include <cstring>

namespace arp::tests::cloud {

// Real L2 compute/transfer by default; explicit retained mode exercises the
// backend-private bounded command, including GPU-only output/state/copy into
// mapped staging. Shared by density and temporal oracles; no mocked execution.
class ComputeProbe final {
    friend class GraphicsProbe;

public:
    ComputeProbe(rhi::GpuDevice& device,
        const char* name,
        rhi::Extent2D extent,
        std::uint64_t inputBytes,
        bool previous,
        bool retained = false,
        bool sampledDepth = false)
        : device_(device), extent_(extent), inputBytes_(inputBytes), retained_(retained),
          sampledDepth_(sampledDepth), outputBytes_(std::uint64_t(extent.width) * extent.height
                                           * sizeof(renderer::cloud::CloudGpuSample))
    {
        using D = rhi::DescriptorType;
        bindings_.name = name;
        bindings_.bindings = {{0u, D::UniformBuffer, rhi::ShaderStage::Compute},
            {1u, D::ReadOnlyStorageBuffer, rhi::ShaderStage::Compute},
            {3u, D::StorageBuffer, rhi::ShaderStage::Compute}};
        if (previous)
            bindings_.bindings.push_back({2u, D::ReadOnlyStorageBuffer, rhi::ShaderStage::Compute});
        if (sampledDepth_) {
            assert(retained_ && !previous);
            rhi::DescriptorBindingDesc depth{2u, D::SampledTexture, rhi::ShaderStage::Compute};
            depth.textureViewDimensionConstrained = true;
            depth.textureViewDimension = rhi::TextureViewDimension::Texture2D;
            depth.depthTexture = true;
            depth.sampledTextureFilterable = false;
            bindings_.bindings.push_back(depth);
        }
        rhi::PipelineLayoutDesc ld;
        ld.descriptorLayouts.push_back(bindings_);
        ld.descriptorSets.push_back({0u, bindings_.name});
        layout_ = device_.createPipelineLayout(ld);
        assert(layout_);
        const auto path = core::pmr::String(ARP_CLOUD_SHADER_DIR) + "/" + name + ".slang";
        package_.label = name;
        package_.slangModulePath = path;
        package_.entrypoints.push_back(
            {path, "computeMain", rhi::ShaderStage::Compute, rhi::ShaderSourceLanguage::Slang});
        package_.artifacts.spirvPath = renderer::detail::replaceShaderExtension(path, ".spv");
        package_.artifacts.mslPath = renderer::detail::replaceShaderExtension(path, ".metal");
        package_.artifacts.wgslPath = renderer::detail::replaceShaderExtension(path, ".wgsl");
        package_.artifacts.reflectionPath =
            renderer::detail::replaceShaderExtension(path, ".reflection.yaml");
        assert(renderer::hydrateShaderPackageReflectionFromArtifact(package_));
        for (const auto& binding : bindings_.bindings)
            package_.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(0u, binding));
        rhi::ComputePipelineDesc pd;
        pd.label = name;
        pd.layout = layout_.get();
        pd.shaderPackage = &package_;
        pd.computeShader = package_.entrypoints.front();
        pipeline_ = device_.createComputePipeline(pd);
        arena_ = device_.createDescriptorArena({name, 1u, 4u});
        assert(pipeline_ && arena_);
        set_ = arena_->allocateDescriptorSet({name, bindings_});
        assert(set_);
        constants_ = buffer("Cloud.ProbeConstants",
            sizeof(renderer::cloud::CloudGpuConstants),
            rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu);
        input_ = buffer("Cloud.ProbeInput",
            inputBytes_,
            rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu);
        if (previous)
            previous_ = buffer("Cloud.ProbePrevious",
                outputBytes_,
                rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopyDst,
                rhi::MemoryUsage::CpuToGpu);
        output_ = buffer("Cloud.ProbeOutput",
            outputBytes_,
            rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc,
            rhi::MemoryUsage::GpuOnly,
            retained_ ? rhi::ResourceState::ShaderWrite : rhi::ResourceState::Undefined);
        staging_ = buffer("Cloud.ProbeReadback",
            outputBytes_,
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            retained_ ? rhi::ResourceState::CopyDst : rhi::ResourceState::Undefined);
        bind(0u, D::UniformBuffer, constants_);
        bind(1u, D::ReadOnlyStorageBuffer, input_);
        if (previous_)
            bind(2u, D::ReadOnlyStorageBuffer, previous_);
        bind(3u, D::StorageBuffer, output_);
        if (sampledDepth_) {
            depth_ = core::shareUnique(device_.createTexture({"Cloud.ProbeDepth",
                {extent.width, extent.height, 1u},
                rhi::TextureFormat::D32Float,
                rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment}));
            assert(depth_);
            rhi::TextureViewDesc vd;
            vd.label = "Cloud.ProbeDepthView";
            vd.format = rhi::TextureFormat::D32Float;
            vd.usage = rhi::TextureUsage::Sampled;
            vd.aspect = rhi::TextureAspect::Depth;
            depthView_ = device_.createTextureView(*depth_, vd);
            assert(depthView_
                && set_
                    ->write({2u,
                        D::SampledTexture,
                        depthView_->id(),
                        rhi::DescriptorResourceKind::TextureView})
                    .success);
        }
    }

    core::pmr::Vector<renderer::cloud::CloudGpuSample> run(
        const renderer::cloud::CloudGpuConstants& constants,
        const void* input,
        const void* previous = nullptr,
        float depth = 1.0f)
    {
        assert(device_.writeBuffer(*constants_, &constants, sizeof(constants)).success);
        assert(device_.writeBuffer(*input_, input, inputBytes_).success);
        if (previous_)
            assert(previous && device_.writeBuffer(*previous_, previous, outputBytes_).success);
        if (sampledDepth_)
            produceDepth(depth);
        if (retained_)
            return runRetained();
        render_graph::FrameGraph graph(device_);
        const auto outputHandle = graph.importBufferRef(output_);
        auto pass = graph.addPass("Cloud.ProbeCompute");
        pass.read(graph.importBufferRef(constants_), rhi::ResourceState::ShaderRead)
            .read(graph.importBufferRef(input_), rhi::ResourceState::ShaderRead)
            .write(outputHandle, rhi::ResourceState::ShaderWrite);
        if (previous_)
            pass.read(graph.importBufferRef(previous_), rhi::ResourceState::ShaderRead);
        pass.execute([&](render_graph::RenderContext& rc) {
            auto compute = rc.commands().beginComputePass({"Cloud.ProbeCompute"});
            assert(compute && compute->setPipeline(*pipeline_).success
                && compute->bindDescriptorSet(0u, *set_).success
                && compute->dispatch((extent_.width + 7u) / 8u, (extent_.height + 7u) / 8u, 1u)
                    .success
                && compute->end().success);
        });
        graph.addPass("Cloud.ProbeCopy")
            .read(outputHandle, rhi::ResourceState::CopySrc)
            .write(graph.importBufferRef(staging_), rhi::ResourceState::CopyDst)
            .externalSideEffects()
            .execute([&](render_graph::RenderContext& rc) {
                auto transfer = rc.commands().beginTransferPass({"Cloud.ProbeCopy"});
                assert(transfer
                    && transfer
                        ->copyBufferToBuffer({output_->id(), staging_->id(), 0u, 0u, outputBytes_})
                        .success
                    && transfer->end().success);
            });
        graph.execute();
        device_.waitIdle();
        return readback(*staging_);
    }

    void verifyRetainedTemporalChain(const renderer::cloud::CloudGpuConstants& constants,
        const core::pmr::Vector<renderer::cloud::CloudGpuSample>& expected,
        const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime,
        bool dropWrapper)
    {
        using namespace rhi_vulkan;
        assert(retained_ && sampledDepth_ && !previous_
            && expected.size() * sizeof(expected.front()) == outputBytes_);
        ComputeProbe next(device_, "cloud_temporal", extent_, outputBytes_, true, true);
        auto reset = constants;
        reset.previousCamera.w = 0.0f;
        assert(device_.writeBuffer(*next.constants_, &reset, sizeof(reset)).success);
        assert(device_.writeBuffer(*next.previous_, expected.data(), outputBytes_).success);
        next.bind(1u, rhi::DescriptorType::ReadOnlyStorageBuffer, output_);
        const std::array<const rhi::GpuBuffer*, 3u> rawBuffers{
            constants_.get(), input_.get(), output_.get()};
        const std::array<const rhi::GpuBuffer*, 4u> temporalBuffers{
            next.constants_.get(), output_.get(), next.previous_.get(), next.output_.get()};
        const std::array<VulkanRhiRetainedDescriptorSet, 1u> rawSets{{{0u, set_.get()}}};
        const std::array<VulkanRhiRetainedDescriptorSet, 1u> temporalSets{{{0u, next.set_.get()}}};
        const std::array<VulkanRhiRetainedSampledDepth, 1u> images{
            {{depthView_.get(), depth_.get()}}};
        VulkanRhiRetainedComputeDispatch raw{pipeline_.get(),
            rawSets,
            rawBuffers,
            (extent_.width + 7u) / 8u,
            (extent_.height + 7u) / 8u,
            1u};
        raw.sampledDepth = images;
        VulkanRhiRetainedComputeDispatch temporal{
            next.pipeline_.get(), temporalSets, temporalBuffers, raw.groupsX, raw.groupsY, 1u};
        rhi::ResourceBarrierDesc read;
        read.resource = output_->id();
        read.before = rhi::ResourceState::ShaderWrite;
        read.after = rhi::ResourceState::ShaderRead;
        read.sourceStages = read.destinationStages = rhi::PipelineStage::ComputeShader;
        read.sourceAccess = rhi::ResourceAccess::ShaderWrite;
        read.destinationAccess = rhi::ResourceAccess::ShaderRead;
        auto restore = read;
        std::swap(restore.before, restore.after);
        std::swap(restore.sourceAccess, restore.destinationAccess);
        constexpr std::uint64_t metadataBytes = 64u * 1024u;
        // Raw's seven owners + five new temporal owners + one readback owner.
        constexpr std::uint32_t owners = 7u + 5u + 1u;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        core::pmr::String diagnostic;
        diagnostic.reserve(512u);
        const auto prepare = [&](std::uint32_t count) {
            const char* error = nullptr;
            auto storage = rhi::common::CommandRetentionStorage::prepare(
                {1u, true, count, 0u, 0u, 0u, metadataBytes}, quota, error);
            assert(storage && !error);
            auto result = prepareVulkanRhiRetainedCommand(device_, std::move(storage), diagnostic);
            assert(result);
            return result;
        };
        {
            auto shortCommand = prepare(owners - 1u);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(recordDepthRoundTrip(*shortCommand, diagnostic)
                && shortCommand->dispatch(raw, diagnostic)
                && shortCommand->transition(*output_, read, diagnostic)
                && shortCommand->dispatch(temporal, diagnostic));
            const auto copies =
                inspectVulkanRhiNativeResourceStats(device_).nativeBufferCopyRecordCount;
            assert(!next.recordReadback(*shortCommand, diagnostic)
                && diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
                && shortCommand->retentionStorage()->resourceOwnerCount() == owners - 1u
                && inspectVulkanRhiNativeResourceStats(device_).nativeBufferCopyRecordCount
                    == copies
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        assert(!quota->liveBytes() && vulkanRhiBufferStateCommitted(*output_)
            && vulkanRhiBufferStateCommitted(*next.output_)
            && output_->state() == rhi::ResourceState::ShaderWrite
            && next.output_->state() == rhi::ResourceState::ShaderWrite);
        auto command = prepare(owners);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(recordDepthRoundTrip(*command, diagnostic) && command->dispatch(raw, diagnostic)
            && command->transition(*output_, read, diagnostic)
            && command->dispatch(temporal, diagnostic) && next.recordReadback(*command, diagnostic)
            && command->transition(*output_, restore, diagnostic)
            && command->retentionStorage()->resourceOwnerCount() == owners
            && command->finish(diagnostic)
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
        const auto beforeSubmit = core::defaultCpuAllocator().stats().allocationCount;
        assert(gate && command->submit({gate, 1u}, diagnostic)
            && core::defaultCpuAllocator().stats().allocationCount == beforeSubmit
            && !command->completed() && vulkanRhiBufferStateCommitted(*output_)
            && vulkanRhiBufferStateCommitted(*next.output_));
        if (dropWrapper)
            command.reset();
        assert(gate->hostSignal(1u) == VK_SUCCESS);
        if (command)
            assert(command->waitUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        else
            assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        retireCompletedVulkanRhiSubmittedCommands();
        command.reset();
        assert(!quota->liveBytes());
        const auto actual = next.readback(*next.staging_);
        assert(std::memcmp(actual.data(), expected.data(), outputBytes_) == 0);
        std::printf(
            "retained GPU-only raymarch -> temporal reset -> staging: %llu exact bytes, "
            "13 owners, one-short rollback, zero hot allocations, command dropped=%d PASS\n",
            static_cast<unsigned long long>(outputBytes_),
            dropWrapper);
    }

    void verifyRetainedRetirement(
        const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime, bool dropWrapper)
    {
        using namespace rhi_vulkan;
        assert(retained_);
        constexpr std::uint64_t metadataBytes = 64u * 1024u;
        core::pmr::String diagnostic;
        diagnostic.reserve(512u);
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* error = nullptr;
        const std::uint32_t count = (previous_ ? 6u : 5u) + (sampledDepth_ ? 2u : 0u) + 1u;
        auto storage = rhi::common::CommandRetentionStorage::prepare(
            {1u, true, count, 0u, 0u, 0u, metadataBytes}, quota, error);
        assert(storage && !error);
        const std::array<const rhi::GpuBuffer*, 4u> buffers{
            constants_.get(), input_.get(), output_.get(), previous_.get()};
        const std::array<VulkanRhiRetainedDescriptorSet, 1u> sets{{{0u, set_.get()}}};
        VulkanRhiRetainedComputeDispatch request{pipeline_.get(),
            sets,
            {buffers.data(), previous_ ? 4u : 3u},
            (extent_.width + 7u) / 8u,
            (extent_.height + 7u) / 8u,
            1u};
        const std::array<VulkanRhiRetainedSampledDepth, 1u> images{
            {{depthView_.get(), depth_.get()}}};
        if (sampledDepth_)
            request.sampledDepth = images;
        const std::weak_ptr<void> nativeSet = vulkanRhiNativePayload(*set_);
        const std::weak_ptr<void> nativePipeline = vulkanRhiNativePayload(*pipeline_);
        const std::weak_ptr<void> nativeInput = vulkanRhiNativePayload(*input_);
        const std::weak_ptr<void> nativeOutput = vulkanRhiNativePayload(*output_);
        const std::weak_ptr<void> nativeReadback = vulkanRhiNativePayload(*staging_);
        const std::weak_ptr<void> nativeDepth = depth_ ? vulkanRhiNativePayload(*depth_) : nullptr;
        const std::weak_ptr<void> nativeDepthView =
            depthView_ ? vulkanRhiNativePayload(*depthView_) : nullptr;
        {
            auto cancelled =
                prepareVulkanRhiRetainedCommand(device_, std::move(storage), diagnostic);
            assert(cancelled && recordDepthRoundTrip(*cancelled, diagnostic)
                && cancelled->dispatch(request, diagnostic)
                && recordReadback(*cancelled, diagnostic));
        }
        assert(!quota->liveBytes() && vulkanRhiBufferStateCommitted(*output_));
        storage = rhi::common::CommandRetentionStorage::prepare(
            {1u, true, count, 0u, 0u, 0u, metadataBytes}, quota, error);
        auto command = prepareVulkanRhiRetainedCommand(device_, std::move(storage), diagnostic);
        assert(command && recordDepthRoundTrip(*command, diagnostic)
            && command->dispatch(request, diagnostic) && recordReadback(*command, diagnostic)
            && command->finish(diagnostic));
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
        assert(gate && command->submit({gate, 1u}, diagnostic));
        set_.reset();
        arena_.reset();
        pipeline_.reset();
        layout_.reset();
        constants_.reset();
        input_.reset();
        output_.reset();
        staging_.reset();
        previous_.reset();
        depthView_.reset();
        depth_.reset();
        assert(!command->completed() && !nativeSet.expired() && !nativePipeline.expired()
            && !nativeInput.expired());
        assert(!command->waitUntil(std::chrono::steady_clock::now(), diagnostic));
        if (dropWrapper)
            command.reset();
        retireCompletedVulkanRhiSubmittedCommands();
        assert(!nativeSet.expired() && !nativePipeline.expired() && !nativeInput.expired());
        assert(!sampledDepth_ || (!nativeDepth.expired() && !nativeDepthView.expired()));
        assert(!nativeOutput.expired() && !nativeReadback.expired());
        assert(gate->hostSignal(1u) == VK_SUCCESS);
        if (command) {
            assert(command->waitUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
            assert(command->completed() && !command->retentionStorage());
        } else {
            assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        }
        retireCompletedVulkanRhiSubmittedCommands();
        assert(nativeSet.expired() && nativePipeline.expired() && nativeInput.expired());
        assert(nativeDepth.expired() && nativeDepthView.expired());
        assert(nativeOutput.expired() && nativeReadback.expired());
        command.reset();
        assert(!quota->liveBytes());
        std::printf(
            "retained cloud Compute cancelled/delayed native retirement, wrappers destroyed, command dropped=%d PASS\n",
            dropWrapper);
    }

private:
    bool recordDepthRoundTrip(
        rhi_vulkan::VulkanRhiRetainedCommand& command, core::pmr::String& diagnostic)
    {
        if (!sampledDepth_)
            return true;
        rhi::ResourceBarrierDesc state;
        state.resource = depth_->id();
        state.textureSubresources = rhi::defaultTextureSubresourceRange(depth_->desc());
        state.before = rhi::ResourceState::ShaderRead;
        state.after = rhi::ResourceState::DepthAttachment;
        state.sourceStages = rhi::PipelineStage::ComputeShader;
        state.destinationStages = rhi::PipelineStage::DepthStencil;
        state.sourceAccess = rhi::ResourceAccess::ShaderRead;
        state.destinationAccess = rhi::ResourceAccess::DepthStencilWrite;
        if (!command.transition(*depth_, state, diagnostic))
            return false;
        std::swap(state.before, state.after);
        std::swap(state.sourceStages, state.destinationStages);
        std::swap(state.sourceAccess, state.destinationAccess);
        return command.transition(*depth_, state, diagnostic);
    }

    bool recordReadback(
        rhi_vulkan::VulkanRhiRetainedCommand& command, core::pmr::String& diagnostic)
    {
        rhi::ResourceBarrierDesc barrier;
        barrier.resource = output_->id();
        barrier.before = rhi::ResourceState::ShaderWrite;
        barrier.after = rhi::ResourceState::CopySrc;
        barrier.sourceStages = rhi::PipelineStage::ComputeShader;
        barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = rhi::ResourceAccess::ShaderWrite;
        barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
        if (!command.transition(*output_, barrier, diagnostic)
            || !command.copy(*output_,
                *staging_,
                {output_->id(), staging_->id(), 0u, 0u, outputBytes_},
                diagnostic))
            return false;
        std::swap(barrier.before, barrier.after);
        std::swap(barrier.sourceStages, barrier.destinationStages);
        std::swap(barrier.sourceAccess, barrier.destinationAccess);
        return command.transition(*output_, barrier, diagnostic);
    }

    // Existing ordinary L2 producer only. Its metadata is deliberately outside
    // the private dispatch proof; no bounded whole-frame claim follows.
    void produceDepth(float value)
    {
        render_graph::FrameGraph graph(device_);
        const auto depth = graph.importTextureRef(depth_);
        graph.addPass("Cloud.ProbeDepthClear")
            .write(depth, rhi::ResourceState::DepthAttachment)
            .execute([&](render_graph::RenderContext& rc) {
                rhi::RenderPassDesc pass;
                pass.label = "Cloud.ProbeDepthClear";
                pass.hasDepthStencilAttachment = true;
                pass.depthStencilAttachment = {"Depth",
                    depth_->id(),
                    rhi::TextureFormat::D32Float,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store};
                pass.depthStencilAttachment.depthClearValue = value;
                auto encoder = rc.commands().beginRenderPass(pass);
                assert(encoder && encoder->end().success);
            });
        graph.addPass("Cloud.ProbeDepthRead")
            .read(depth, rhi::ResourceState::ShaderRead)
            .externalSideEffects()
            .execute([](render_graph::RenderContext&) {});
        graph.execute();
        device_.waitIdle();
    }

    core::pmr::Vector<renderer::cloud::CloudGpuSample> readback(const rhi::GpuBuffer& buffer)
    {
        const auto payload = rhi_vulkan::vulkanRhiNativePayload(buffer);
        core::pmr::String diagnostic;
        assert(payload && payload->backendInvalidateMappedRead(0u, outputBytes_, diagnostic));
        core::pmr::Vector<renderer::cloud::CloudGpuSample> result(
            outputBytes_ / sizeof(renderer::cloud::CloudGpuSample));
        std::memcpy(result.data(), payload->backendMappedData(), outputBytes_);
        return result;
    }

    std::shared_ptr<rhi::GpuBuffer> buffer(const char* label,
        std::uint64_t bytes,
        rhi::ResourceUsage usage,
        rhi::MemoryUsage memory,
        rhi::ResourceState state = rhi::ResourceState::Undefined)
    {
        const auto initial =
            memory == rhi::MemoryUsage::CpuToGpu ? rhi::ResourceState::ShaderRead : state;
        auto result =
            core::shareUnique(device_.createBuffer({label, bytes, usage, memory, initial}));
        assert(result);
        return result;
    }

    core::pmr::Vector<renderer::cloud::CloudGpuSample> runRetained()
    {
        using namespace rhi_vulkan;
        constexpr std::uint64_t metadataBytes = 64u * 1024u;
        core::pmr::String diagnostic;
        diagnostic.reserve(512u);
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const std::uint32_t dispatchOwners = (previous_ ? 6u : 5u) + (sampledDepth_ ? 2u : 0u);
        const std::uint32_t owners = dispatchOwners + 1u;
        const auto prepare = [&](std::uint32_t count) {
            const char* error = nullptr;
            auto storage = rhi::common::CommandRetentionStorage::prepare(
                {1u, true, count, 0u, 0u, 0u, metadataBytes}, quota, error);
            assert(storage && !error);
            auto command = prepareVulkanRhiRetainedCommand(device_, std::move(storage), diagnostic);
            if (!command)
                std::fprintf(stderr, "retained compute preparation: %s\n", diagnostic.c_str());
            assert(command);
            return command;
        };
        const std::array<const rhi::GpuBuffer*, 4u> buffers{
            constants_.get(), input_.get(), output_.get(), previous_.get()};
        const std::array<VulkanRhiRetainedDescriptorSet, 1u> sets{{{0u, set_.get()}}};
        VulkanRhiRetainedComputeDispatch request{pipeline_.get(),
            sets,
            {buffers.data(), previous_ ? 4u : 3u},
            (extent_.width + 7u) / 8u,
            (extent_.height + 7u) / 8u,
            1u};
        const std::array<VulkanRhiRetainedSampledDepth, 1u> images{
            {{depthView_.get(), depth_.get()}}};
        if (sampledDepth_)
            request.sampledDepth = images;
        if (!retainedProbes_) {
            for (const auto ceiling : {0u, 1u, dispatchOwners - 1u}) {
                auto command = prepare(ceiling);
                const auto before = inspectVulkanRhiNativeResourceStats(device_);
                assert(!command->dispatch(request, diagnostic)
                    && diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
                    && !command->retentionStorage()->resourceOwnerCount());
                const auto after = inspectVulkanRhiNativeResourceStats(device_);
                assert(
                    after.nativeCommandBufferRecordCount == before.nativeCommandBufferRecordCount);
            }
            assert(!quota->liveBytes());
            auto command = prepare(owners);
            const auto before = inspectVulkanRhiNativeResourceStats(device_);
            const auto invalid = [&](const VulkanRhiRetainedComputeDispatch& value) {
                assert(!command->dispatch(value, diagnostic)
                    && !command->retentionStorage()->resourceOwnerCount());
                assert(inspectVulkanRhiNativeResourceStats(device_).nativeCommandBufferRecordCount
                    == before.nativeCommandBufferRecordCount);
            };
            auto bad = request;
            bad.pipeline = nullptr;
            invalid(bad);
            bad = request;
            bad.groupsX = 0u;
            invalid(bad);
            bad.groupsX = UINT32_MAX;
            invalid(bad);
            bad = request;
            bad.sets = {};
            invalid(bad);
            bad = request;
            bad.buffers = {buffers.data(), 2u};
            invalid(bad);
            const std::array<const rhi::GpuBuffer*, 4u> duplicate{
                constants_.get(), input_.get(), input_.get(), previous_.get()};
            bad = request;
            bad.buffers = {duplicate.data(), request.buffers.size()};
            invalid(bad);
            const std::uint32_t unboundPush = 1u;
            bad = request;
            bad.pushConstants = &unboundPush;
            bad.pushConstantBytes = sizeof(unboundPush);
            invalid(bad);
            // Authentic payload in a foreign issuing registry is not an owner proof.
            auto foreign = createVulkanRhiBuffer(constants_->id(),
                constants_->desc(),
                {},
                vulkanRhiNativePayload(*constants_),
                core::makeShared<VulkanRhiResourceRegistry>());
            auto wrongBuffers = buffers;
            wrongBuffers[0] = foreign.get();
            bad = request;
            bad.buffers = {wrongBuffers.data(), request.buffers.size()};
            invalid(bad);
            rhi::ResourceBarrierDesc state;
            state.resource = output_->id();
            state.before = rhi::ResourceState::ShaderWrite;
            state.after = rhi::ResourceState::ShaderRead;
            assert(transitionVulkanRhiBuffer(*output_, state).success);
            invalid(request);
            std::swap(state.before, state.after);
            assert(transitionVulkanRhiBuffer(*output_, state).success);
            core::MutationJournal pendingBuffer;
            state.before = state.after = rhi::ResourceState::ShaderWrite;
            assert(transitionVulkanRhiBuffer(*output_, state, &pendingBuffer).success);
            invalid(request);
            pendingBuffer.rollback();
            quota->setLimitBytes(metadataBytes + 1u);
            invalid(request);
            quota->setLimitBytes(metadataBytes);
            if (sampledDepth_) {
                bad = request;
                bad.sampledDepth = {};
                invalid(bad);
                const std::array<VulkanRhiRetainedSampledDepth, 1u> missing{
                    {{depthView_.get(), nullptr}}};
                bad.sampledDepth = missing;
                invalid(bad);
                const std::array<VulkanRhiRetainedSampledDepth, 2u> duplicateViews{
                    {images[0], images[0]}};
                bad.sampledDepth = duplicateViews;
                invalid(bad);
                auto wrongParent = device_.createTexture(depth_->desc());
                assert(wrongParent);
                const std::array<VulkanRhiRetainedSampledDepth, 1u> wrongParentInput{
                    {{depthView_.get(), wrongParent.get()}}};
                bad.sampledDepth = wrongParentInput;
                invalid(bad);
                auto unusedView = device_.createTextureView(*depth_, depthView_->desc());
                assert(unusedView);
                const std::array<VulkanRhiRetainedSampledDepth, 2u> unusedViews{
                    {images[0], {unusedView.get(), depth_.get()}}};
                bad.sampledDepth = unusedViews;
                invalid(bad);
                auto foreignRegistry = core::makeShared<VulkanRhiResourceRegistry>();
                auto foreignView = createVulkanRhiTextureView(depthView_->id(),
                    depth_->id(),
                    depthView_->desc(),
                    vulkanRhiNativePayload(*depthView_),
                    foreignRegistry);
                const std::array<VulkanRhiRetainedSampledDepth, 1u> foreign{
                    {{foreignView.get(), depth_.get()}}};
                bad.sampledDepth = foreign;
                invalid(bad);
                rhi::ResourceBarrierDesc changed;
                changed.resource = depth_->id();
                changed.textureSubresources = rhi::defaultTextureSubresourceRange(depth_->desc());
                changed.before = rhi::ResourceState::ShaderRead;
                changed.after = rhi::ResourceState::DepthAttachment;
                assert(transitionVulkanRhiTexture(*depth_, changed).success);
                invalid(request);
                std::swap(changed.before, changed.after);
                assert(transitionVulkanRhiTexture(*depth_, changed).success);
                core::MutationJournal pendingDepth;
                changed.before = changed.after = rhi::ResourceState::ShaderRead;
                assert(transitionVulkanRhiTexture(*depth_, changed, &pendingDepth).success);
                invalid(request);
                pendingDepth.rollback();
            }
            command.reset();
            assert(!quota->liveBytes());
            retainedProbes_ = true;
        }
        auto command = prepare(owners);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const rhi::ResourceMemoryBarrierDesc dependency{output_->id(),
            rhi::PipelineStage::ComputeShader,
            rhi::PipelineStage::ComputeShader,
            rhi::ResourceAccess::ShaderWrite,
            rhi::ResourceAccess::ShaderWrite};
        assert(recordDepthRoundTrip(*command, diagnostic));
        for (unsigned repeat = 0u; repeat < 32u; ++repeat) {
            assert(command->dispatch(request, diagnostic));
            assert(command->dependency(*output_, dependency, diagnostic));
            assert(command->retentionStorage()->resourceOwnerCount() == dispatchOwners);
        }
        assert(recordReadback(*command, diagnostic)
            && command->retentionStorage()->resourceOwnerCount() == owners);
        assert(command->finish(diagnostic)
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        core::UniquePtr<rhi::GpuTextureView> replacementDepthView;
        if (sampledDepth_) {
            replacementDepthView = device_.createTextureView(*depth_, depthView_->desc());
            assert(replacementDepthView);
            const auto result = set_->write({2u,
                rhi::DescriptorType::SampledTexture,
                replacementDepthView->id(),
                rhi::DescriptorResourceKind::TextureView});
            assert(!result.success
                && result.diagnostics.find("RHI-DESCRIPTOR-RECORDED-STORAGE-IN-USE:") == 0u);
        }
        auto replacement = buffer("Cloud.ProbeReplacement",
            sizeof(renderer::cloud::CloudGpuConstants),
            rhi::ResourceUsage::UniformBuffer,
            rhi::MemoryUsage::CpuToGpu);
        const rhi::DescriptorBindingWrite rewrite{0u,
            rhi::DescriptorType::UniformBuffer,
            replacement->id(),
            rhi::DescriptorResourceKind::Buffer};
        assert(!set_->write(rewrite).success);
        assert(
            set_->write({0u,
                            rhi::DescriptorType::UniformBuffer,
                            constants_->id(),
                            rhi::DescriptorResourceKind::Buffer})
                .success);
        const auto beforeSubmit = core::defaultCpuAllocator().stats().allocationCount;
        assert(command->submit({}, diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == beforeSubmit);
        assert(command->waitUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        assert(command->completed() && !command->retentionStorage());
        if (sampledDepth_) {
            assert(
                set_->write({2u,
                                rhi::DescriptorType::SampledTexture,
                                replacementDepthView->id(),
                                rhi::DescriptorResourceKind::TextureView})
                    .success);
            assert(
                set_->write({2u,
                                rhi::DescriptorType::SampledTexture,
                                depthView_->id(),
                                rhi::DescriptorResourceKind::TextureView})
                    .success);
        }
        assert(set_->write(rewrite).success);
        bind(0u, rhi::DescriptorType::UniformBuffer, constants_);
        // Completion retires resource storage; the backend's submitted-owner
        // sweep releases its separate strong command metadata reference.
        retireCompletedVulkanRhiSubmittedCommands();
        command.reset();
        assert(!quota->liveBytes());
        assert(rhi_vulkan::vulkanRhiBufferStateCommitted(*output_));
        return readback(*staging_);
    }

    void bind(std::uint32_t binding,
        rhi::DescriptorType type,
        const std::shared_ptr<rhi::GpuBuffer>& buffer)
    {
        assert(set_->write({binding, type, buffer->id(), rhi::DescriptorResourceKind::Buffer})
                .success);
    }

    rhi::GpuDevice& device_;
    rhi::Extent2D extent_;
    std::uint64_t inputBytes_, outputBytes_;
    bool retained_ = false, retainedProbes_ = false;
    bool sampledDepth_ = false;
    rhi::DescriptorLayoutDesc bindings_;
    rhi::ShaderPackageDesc package_;
    core::UniquePtr<rhi::PipelineLayout> layout_;
    core::UniquePtr<rhi::ComputePipeline> pipeline_;
    core::UniquePtr<rhi::DescriptorArena> arena_;
    core::UniquePtr<rhi::DescriptorSet> set_;
    std::shared_ptr<rhi::GpuBuffer> constants_, input_, previous_, output_, staging_;
    std::shared_ptr<rhi::GpuTexture> depth_;
    core::UniquePtr<rhi::GpuTextureView> depthView_;
};

} // namespace arp::tests::cloud
