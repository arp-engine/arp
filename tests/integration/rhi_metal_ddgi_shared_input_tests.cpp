#include "renderer/gi/ddgi_renderer.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace {
struct SharedLightSample {
    std::array<float, 8> lights;
    std::array<std::uint32_t, 64> influences;
};

static_assert(arp::renderer::kDdgiMaxLocalLightsPerProbe == 8u);
static_assert(sizeof(SharedLightSample) == 72u * sizeof(std::uint32_t));

bool influencesMatch(const SharedLightSample& sample, bool illuminated)
{
    for (std::size_t index = 0u; index < sample.influences.size(); ++index) {
        const auto expected =
            illuminated && index % 8u == 0u ? 0u : arp::renderer::kDdgiInvalidLocalLightIndex;
        if (sample.influences[index] != expected)
            return false;
    }
    return true;
}

bool verifySharedLight(arp::rhi::GpuDevice& device, bool mutateOtherSlot)
{
    using namespace arp;
    using namespace arp::rhi;
    tests::CapabilityOverrideDevice influenceObserver(device, nullptr);
    influenceObserver.observeBuffer("DdgiProbeLocalLightInfluences");
    tests::CapabilityOverrideDevice observed(influenceObserver, nullptr);
    observed.observeBuffer("DdgiLocalLights");
    renderer::DdgiRenderer ddgi(observed);
    const auto created = ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
        {core::makeShared<tests::FlatTerrainSurface>(), 64u, 64u},
        2u,
        2u,
        2u,
        8u,
        8u);
    if (!created.success)
        std::fprintf(stderr, "%s\n", created.diagnostics.c_str());
    assert(created.success && observed.observedBuffer() && influenceObserver.observedBuffer());
    auto& lightBuffer = *observed.observedBuffer();
    auto& influenceBuffer = *influenceObserver.observedBuffer();
    renderer::DdgiLocalLightDesc light;
    light.position[0] = -1.0f;
    light.range = 8.0f;
    light.radiance[0] = 0.25f;
    light.intensity = 2.0f;
    assert(ddgi.setLocalLights(&light, 1u).success);
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
    setLayout.name = "SharedInputConsumer";
    setLayout.bindings = {{0u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute},
        {1u, DescriptorType::StorageBuffer, ShaderStage::Compute},
        {2u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute}};
    PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back(setLayout);
    layoutDesc.descriptorSets.push_back({0u, setLayout.name});
    auto layout = device.createPipelineLayout(layoutDesc);
    assert(layout);
    const core::pmr::String prefix = ARP_INPUT_SHADER_DIR "/ddgi_shared_input_consumer";
    ShaderPackageDesc package;
    package.label = "SharedInputConsumer";
    package.slangModulePath = prefix + ".slang";
    package.entrypoints = {{package.slangModulePath,
        "computeMain",
        ShaderStage::Compute,
        ShaderSourceLanguage::Slang}};
    package.artifacts.spirvPath = prefix + ".spv";
    package.artifacts.mslPath = prefix + ".metal";
    package.artifacts.wgslPath = prefix + ".wgsl";
    package.artifacts.reflectionPath = prefix + ".reflection.yaml";
    for (const auto& binding : setLayout.bindings)
        package.reflection.descriptorBindings.push_back(makeShaderReflectionBinding(0u, binding));
    assert(renderer::hydrateShaderPackageReflectionFromArtifact(package));
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.label = "SharedInputConsumer";
    pipelineDesc.layout = layout.get();
    pipelineDesc.shaderPackage = &package;
    pipelineDesc.computeShader = package.entrypoints[0];
    auto pipeline = device.createComputePipeline(pipelineDesc);
    auto output = device.createBuffer({"SharedInputOutput",
        sizeof(SharedLightSample),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    auto readback = device.createBuffer({"SharedInputReadback",
        sizeof(SharedLightSample),
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    auto arena = device.createDescriptorArena({"SharedInputConsumer", 1u, 3u});
    assert(pipeline && output && readback && arena);
    auto set = arena->allocateDescriptorSet({"SharedInputConsumer", setLayout});
    assert(set);
    assert(
        set->write({0u,
                       DescriptorType::ReadOnlyStorageBuffer,
                       lightBuffer.id(),
                       DescriptorResourceKind::Buffer})
            .success);
    assert(
        set->write(
               {1u, DescriptorType::StorageBuffer, output->id(), DescriptorResourceKind::Buffer})
            .success);
    assert(
        set->write({2u,
                       DescriptorType::ReadOnlyStorageBuffer,
                       influenceBuffer.id(),
                       DescriptorResourceKind::Buffer})
            .success);
    const auto record = [&] {
        auto encoder = device.createCommandEncoder({"SharedInputConsumer"});
        assert(encoder);
        if (lightBuffer.state() != ResourceState::ShaderRead) {
            const auto lightTransition =
                encoder->transition(lightBuffer, lightBuffer.state(), ResourceState::ShaderRead);
            if (!lightTransition.success)
                std::fprintf(stderr,
                    "Shared light transition state=%u: %s\n",
                    static_cast<unsigned>(lightBuffer.state()),
                    lightTransition.diagnostics.c_str());
            assert(lightTransition.success);
        }
        if (influenceBuffer.state() != ResourceState::ShaderRead)
            assert(encoder
                    ->transition(
                        influenceBuffer, influenceBuffer.state(), ResourceState::ShaderRead)
                    .success);
        assert(encoder->transition(*output, output->state(), ResourceState::ShaderWrite).success);
        auto pass = encoder->beginComputePass({"SharedInputConsumer"});
        assert(pass && pass->setPipeline(*pipeline).success);
        assert(pass->bindDescriptorSet(0u, *set).success);
        assert(pass->dispatch(1u, 1u, 1u).success && pass->end().success);
        assert(encoder->transition(*output, ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success);
        auto copy = encoder->beginTransferPass({"SharedInputReadback"});
        assert(copy
            && copy
                ->copyBufferToBuffer(
                    {output->id(), readback->id(), 0u, 0u, sizeof(SharedLightSample)})
                .success);
        assert(copy->end().success);
        return encoder->finish();
    };
    const auto fence = [&] {
        auto owner = core::makeShared<core::UniquePtr<GpuFence>>(device.createFence());
        return std::shared_ptr<GpuFence>(owner, owner->get());
    };
    const auto read = [&] {
        SharedLightSample values{};
        core::pmr::String diagnostic;
        assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*readback),
            &values,
            sizeof(values),
            0u,
            sizeof(values),
            diagnostic));
        return values;
    };
    auto commands = record();
    auto completedA = fence();
    auto gate = device.createTimelineSemaphore(0u);
    assert(commands && completedA && gate);
    assert(device.queue()
            .submit(*commands, gate.get(), 1u, nullptr, 0u, completedA.get())
            .hasAcceptedWork());
    const bool acceptedA = ddgi.acceptPreparedInput(tokenA, completedA);
    const bool pendingA = !completedA->isSignaled();
    const bool otherSlotReady = ddgi.inputSlotReady(1u);
    const auto newlyExposedA = ddgi.telemetry().newlyExposedProbeCount;
    bool changed = false, preparedB = false;
    bool busyCorrect = true, receiptPreserved = true;
    if (mutateOtherSlot) {
        light.position[0] = 1.0f;
        light.radiance[0] = 0.75f;
        changed = ddgi.setLocalLights(&light, 1u).success;
        push.scrollOffsetFrame[3] = 1.0f;
        // All B probes lie outside the light range; A still needs index zero.
        push.probeGridOriginHysteresis[0] = 32.0f;
        const auto attempted = ddgi.prepareProbeUpdateSchedule(push, 1u);
        preparedB = attempted.success;
        busyCorrect =
            !attempted.success && attempted.diagnostics == "DDGI-SHARED-LIGHT-INPUT-NOT-READY";
        const auto stillA = ddgi.preparedHistoryToken();
        receiptPreserved = stillA.ownerId == tokenA.ownerId
            && stillA.generation == tokenA.generation
            && ddgi.telemetry().newlyExposedProbeCount == newlyExposedA;
    } else {
        push.scrollOffsetFrame[3] = 1.0f;
        preparedB = ddgi.prepareProbeUpdateSchedule(push, 1u).success;
    }
    // Release the real wait before judging any observation; failed assertions
    // must not leave the native queue hung in the fixture's destructor.
    gate->signal(1u);
    completedA->wait();
    device.waitIdle();
    const auto sampled = read();
    const std::array<float, 8> expectedA{-1.0f, 0.0f, 0.0f, 8.0f, 0.25f, 1.0f, 1.0f, 2.0f};
    const bool correct = sampled.lights == expectedA && influencesMatch(sampled, true);
    std::fprintf(stderr,
        "DDGI shared light mutate=%d acceptedA=%d pendingA=%d otherSlotReady=%d changed=%d preparedB=%d actualPosition=%g actualRadiance=%g expectedPosition=-1 expectedRadiance=0.25 correct=%d\n",
        mutateOtherSlot,
        acceptedA,
        pendingA,
        otherSlotReady,
        changed,
        preparedB,
        sampled.lights[0],
        sampled.lights[4],
        correct);
    bool resumed = true;
    if (mutateOtherSlot) {
        assert(ddgi.sharedInputsReady());
        assert(ddgi.prepareProbeUpdateSchedule(push, 1u).success);
        const auto tokenB = ddgi.preparedHistoryToken();
        auto commandsB = record();
        auto completedB = fence();
        assert(device.queue().submit(*commandsB, completedB.get()).hasAcceptedWork());
        assert(ddgi.acceptPreparedInput(tokenB, completedB));
        completedB->wait();
        device.waitIdle();
        auto expectedB = expectedA;
        expectedB[0] = 1.0f;
        expectedB[4] = 0.75f;
        const auto sampledB = read();
        resumed = sampledB.lights == expectedB && influencesMatch(sampledB, false);
        std::fprintf(stderr,
            "DDGI shared light recovery: busy=%d receiptPreserved=%d resumedB=%d\n",
            busyCorrect,
            receiptPreserved,
            resumed);
        // Real upload followed by a test-only failure report: the old cache
        // key must not survive physical influence storage changing underneath it.
        observed.failBufferWrites("DdgiProbeLocalLightInfluences", true);
        push.probeGridOriginHysteresis[0] = -1.0f;
        const auto failedUpload = ddgi.prepareProbeUpdateSchedule(push, 1u);
        assert(!failedUpload.success
            && failedUpload.diagnostics.find("TEST-BUFFER-UPLOAD-FAILED-AFTER-WRITE")
                != std::string::npos);
        assert(ddgi.preparedHistoryToken().generation == 0u);
        observed.failBufferWrites({});
        push.probeGridOriginHysteresis[0] = 32.0f;
        assert(ddgi.prepareProbeUpdateSchedule(push, 1u).success);
        auto retryCommands = record();
        auto retryCompleted = fence();
        assert(device.queue().submit(*retryCommands, retryCompleted.get()).hasAcceptedWork());
        assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), retryCompleted));
        retryCompleted->wait();
        device.waitIdle();
        const auto retried = read();
        const bool cacheRestored = retried.lights == expectedB && influencesMatch(retried, false);
        std::fprintf(stderr,
            "DDGI shared light partial upload old-key recovery: correct=%d firstInfluence=%u\n",
            cacheRestored,
            retried.influences[0]);
        resumed = resumed && cacheRestored;
    }
    assert(ddgi.setLocalLights(nullptr, 0u).success);
    push.scrollOffsetFrame[3] = 2.0f;
    push.probeGridOriginHysteresis[0] = -1.0f;
    assert(ddgi.prepareProbeUpdateSchedule(push, 1u).success);
    const auto tokenC = ddgi.preparedHistoryToken();
    auto commandsC = record();
    auto completedC = fence();
    assert(device.queue().submit(*commandsC, completedC.get()).hasAcceptedWork());
    assert(ddgi.acceptPreparedInput(tokenC, completedC));
    completedC->wait();
    device.waitIdle();
    const auto sampledC = read();
    const bool removed =
        sampledC.lights == std::array<float, 8>{} && influencesMatch(sampledC, false);
    std::fprintf(stderr, "DDGI shared light removal: correct=%d\n", removed);
    return acceptedA && pendingA && otherSlotReady && correct && busyCorrect && receiptPreserved
        && resumed && removed && (mutateOtherSlot ? changed : preparedB);
}
} // namespace

int main()
{
    using namespace arp;
    rhi_metal::MetalRhiDeviceDesc desc;
    desc.capabilityContract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 128u * 1024u;
    desc.uploadStagingMaxPageCount = 8u;
    auto device = rhi_metal::createMetalRhiDevice(desc);
    assert(device);
    const bool control = verifySharedLight(*device, false);
    const bool mutation = verifySharedLight(*device, true);
    device->waitIdle();
    const bool retired = device->memoryStats().allocatedBytes == 0u;
    std::fprintf(stderr,
        "DDGI shared light: control=%d mutation=%d retired=%d\n",
        control,
        mutation,
        retired);
    return control && mutation && retired ? 0 : 1;
}
