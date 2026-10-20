#include "platform/frame_input_admission.h"
#include "renderer/gi/ddgi_renderer.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>

namespace {
void verifyPendingInput(arp::rhi::GpuDevice& device, bool updateHistory)
{
    using namespace arp;
    using namespace arp::rhi;
    renderer::DdgiRenderer ddgi(device);
    const auto created = ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
        {core::makeShared<tests::FlatTerrainSurface>(), 64u, 64u},
        2u,
        2u,
        2u,
        8u,
        8u);
    if (!created.success)
        std::fprintf(stderr, "%s\n", created.diagnostics.c_str());
    assert(created.success);
    renderer::DdgiPushConstants push{};
    for (unsigned axis = 0u; axis != 3u; ++axis) {
        push.probeGridOriginHysteresis[axis] = -1.0f;
        push.probeGridSpacingStrength[axis] = 1.0f;
        push.probeCountRayCount[axis] = 2.0f;
    }
    push.updateParams[0] = 8.0f;
    assert(ddgi.prepareProbeUpdateSchedule(push, 1u).success);
    const auto tokenA = ddgi.preparedHistoryToken();
    DescriptorLayoutDesc setLayout;
    setLayout.name = "SamplingConsumer";
    setLayout.bindings = {{0u, DescriptorType::UniformBuffer, ShaderStage::Compute},
        {1u, DescriptorType::StorageBuffer, ShaderStage::Compute},
        {2u,
            DescriptorType::StorageTexture,
            ShaderStage::Compute,
            1u,
            false,
            false,
            false,
            TextureViewDimension::Texture2D,
            false,
            TextureFormat::Rgba16Float}};
    PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back(setLayout);
    layoutDesc.descriptorSets.push_back({0u, setLayout.name});
    auto layout = device.createPipelineLayout(layoutDesc);
    assert(layout);
    const core::pmr::String prefix = ARP_INPUT_SHADER_DIR "/ddgi_sampling_consumer";
    ShaderPackageDesc package;
    package.label = "SamplingConsumer";
    package.slangModulePath = prefix + ".slang";
    package.entrypoints = {{package.slangModulePath,
        "computeMain",
        ShaderStage::Compute,
        ShaderSourceLanguage::Slang}};
    package.entrypoints.push_back({package.slangModulePath,
        "cacheReadMain",
        ShaderStage::Compute,
        ShaderSourceLanguage::Slang});
    package.artifacts.spirvPath = prefix + ".spv";
    package.artifacts.mslPath = prefix + ".metal";
    package.artifacts.wgslPath = prefix + ".wgsl";
    package.artifacts.reflectionPath = prefix + ".reflection.yaml";
    for (const auto& binding : setLayout.bindings)
        package.reflection.descriptorBindings.push_back(makeShaderReflectionBinding(0u, binding));
    assert(renderer::hydrateShaderPackageReflectionFromArtifact(package));
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.label = "SamplingConsumer";
    pipelineDesc.layout = layout.get();
    pipelineDesc.shaderPackage = &package;
    pipelineDesc.computeShader = package.entrypoints[0];
    auto pipeline = device.createComputePipeline(pipelineDesc);
    pipelineDesc.computeShader = package.entrypoints[1];
    auto reader = device.createComputePipeline(pipelineDesc);
    auto cache = device.createTexture({"DepthCachePrecision",
        {256u, 1u, 1u},
        TextureFormat::Rgba16Float,
        TextureUsage::Storage,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    assert(cache && reader);
    auto cacheView = device.createTextureView(
        *cache, {"DepthCachePrecision", TextureFormat::Rgba16Float, TextureUsage::Storage});
    assert(cacheView);
    constexpr unsigned outputBytes = 257u * 16u;
    auto output = device.createBuffer({"SamplingOutput",
        outputBytes,
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    auto readback = device.createBuffer({"SamplingReadback",
        outputBytes,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    auto arena = device.createDescriptorArena({"SamplingConsumer", 1u, 3u});
    assert(pipeline && output && readback && arena);
    auto set = arena->allocateDescriptorSet({"SamplingConsumer", setLayout});
    assert(set);
    assert(
        set->write({0u,
                       DescriptorType::UniformBuffer,
                       ddgi.samplingConstantsBufferId(0u),
                       DescriptorResourceKind::Buffer})
            .success);
    assert(
        set->write(
               {1u, DescriptorType::StorageBuffer, output->id(), DescriptorResourceKind::Buffer})
            .success);
    assert(
        set->write({2u,
                       DescriptorType::StorageTexture,
                       cacheView->id(),
                       DescriptorResourceKind::TextureView})
            .success);
    const auto record = [&] {
        auto encoder = device.createCommandEncoder({"SamplingConsumer"});
        assert(encoder->transition(*output, output->state(), ResourceState::ShaderWrite).success);
        const TextureSubresourceRange cacheRange{0u, 1u, 0u, 1u, TextureAspect::Color};
        assert(encoder->transition(*cache, cacheRange, cache->state(), ResourceState::ShaderWrite)
                .success);
        auto pass = encoder->beginComputePass({"SamplingConsumer"});
        assert(pass && pass->setPipeline(*pipeline).success);
        assert(pass->bindDescriptorSet(0u, *set).success);
        assert(pass->dispatch(4u, 1u, 1u).success && pass->end().success);
        assert(encoder
                ->transition(
                    *cache, cacheRange, ResourceState::ShaderWrite, ResourceState::ShaderRead)
                .success);
        auto readPass = encoder->beginComputePass({"DepthCachePrecisionRead"});
        assert(readPass && readPass->setPipeline(*reader).success);
        assert(readPass->bindDescriptorSet(0u, *set).success);
        assert(readPass->dispatch(4u, 1u, 1u).success && readPass->end().success);
        assert(encoder->transition(*output, ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success);
        auto copy = encoder->beginTransferPass({"SamplingReadback"});
        assert(copy
            && copy->copyBufferToBuffer({output->id(), readback->id(), 0u, 0u, outputBytes})
                .success);
        assert(copy->end().success);
        return encoder->finish();
    };
    const auto fence = [&] {
        auto owner = core::makeShared<core::UniquePtr<GpuFence>>(device.createFence());
        return std::shared_ptr<GpuFence>(owner, owner->get());
    };
    const auto read = [&](float expected) {
        std::array<float, 257u * 4u> sampled{};
        core::pmr::String diagnostic;
        assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*readback),
            sampled.data(),
            sizeof(sampled),
            0u,
            sizeof(sampled),
            diagnostic));
        assert(sampled[0] == expected && sampled[1] == -1.0f && sampled[2] == -1.0f
            && sampled[3] == 0.0f);
        float largestUncorrectedError = 0.0f;
        for (unsigned i = 1u; i <= 256u; ++i) {
            if (sampled[i * 4u] != 0.0f)
                std::fprintf(stderr,
                    "Depth cache sample=%u remaining=%g rawError=%g\n",
                    i,
                    sampled[i * 4u],
                    sampled[i * 4u + 2u]);
            assert(sampled[i * 4u] == 0.0f);
            assert(sampled[i * 4u + 1u] > 0.0001f);
            largestUncorrectedError = std::max(largestUncorrectedError, sampled[i * 4u + 2u]);
        }
        assert(largestUncorrectedError > 0.0002f);
        std::printf(
            "DDGI cache depth: 256 same-surface accepts, 256 disocclusion rejects, old max error=%g\n",
            largestUncorrectedError);
        return sampled[0];
    };
    // The admission policy consumes the real Metal input readiness. Only its
    // slot-acquisition fixture is Null; this is not a native window claim.
    auto slotDevice = createNullDevice();
    auto swapchain =
        slotDevice->createSwapchain({"Admission", {16u, 16u}, TextureFormat::Bgra8Unorm, 2u});
    platform::FrameInputAdmission admission;
    auto commands = record();
    auto completedA = fence();
    auto gate = device.createTimelineSemaphore(0u);
    assert(commands && completedA && gate && swapchain);
    assert(device.queue()
            .submit(*commands, gate.get(), 1u, nullptr, 0u, completedA.get())
            .hasAcceptedWork());
    const bool acceptedA = updateHistory ? ddgi.acceptPreparedHistory(tokenA, completedA)
                                         : ddgi.acceptPreparedInput(tokenA, completedA);
    const bool pendingA = !completedA->isSignaled();
    const auto deferred = admission.acquire(*swapchain, [&](std::uint32_t slot) {
        return ddgi.inputSlotReady(slot);
    });
    push.scrollOffsetFrame[3] = 2.0f;
    push.probeGridOriginHysteresis[0] = 1.0f;
    const auto preparedB = ddgi.prepareProbeUpdateSchedule(push, 1u);
    gate->signal(1u); // Always unblock before asserting observations after submit.
    completedA->wait();
    device.waitIdle();
    assert(acceptedA && pendingA && deferred.status == AcquireStatus::NotReady);
    assert(!preparedB.success && preparedB.diagnostics == "DDGI-INPUT-SLOT-NOT-READY");
    const auto actualA = read(-1.0f);
    const auto admitted = admission.acquire(*swapchain, [&](std::uint32_t slot) {
        return ddgi.inputSlotReady(slot);
    });
    assert(admitted.success && admitted.frameSerial == deferred.frameSerial);
    assert(swapchain->discardFrame(admitted).success);
    assert(ddgi.prepareProbeUpdateSchedule(push, 1u).success);
    const auto tokenB = ddgi.preparedHistoryToken();
    auto commandsB = record();
    auto completedB = fence();
    assert(device.queue().submit(*commandsB, completedB.get()).hasAcceptedWork());
    assert(updateHistory ? ddgi.acceptPreparedHistory(tokenB, completedB)
                         : ddgi.acceptPreparedInput(tokenB, completedB));
    completedB->wait();
    device.waitIdle();
    const auto actualB = read(1.0f);
    if (!updateHistory)
        assert(ddgi.currentReadIndex() == 0 && ddgi.acceptedProbeScheduleGeneration() == 0u);
    std::printf(
        "DDGI input updateHistory=%d acceptedA=1 pendingA=1 deferred=1 actualA=%g resumedB=1 actualB=%g\n",
        updateHistory,
        actualA,
        actualB);
}
} // namespace

int main()
{
    using namespace arp;
    rhi_metal::MetalRhiDeviceDesc desc;
    desc.capabilityContract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    // Matches the established 2x2x2 DDGI fixture envelope; largest initial
    // upload is the 87,408-byte terrain hierarchy. No additional input slots.
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 128u * 1024u;
    desc.uploadStagingMaxPageCount = 8u;
    auto device = rhi_metal::createMetalRhiDevice(desc);
    assert(device);
    verifyPendingInput(*device, true);
    verifyPendingInput(*device, false);
    device->waitIdle();
    assert(device->memoryStats().allocatedBytes == 0u);
    std::puts("DDGI input real Metal completion/reuse: PASS, liveBytes=0");
}
