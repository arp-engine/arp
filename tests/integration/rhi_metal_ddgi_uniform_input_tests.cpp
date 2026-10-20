#include "renderer/gi/ddgi_renderer.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/ddgi_uniform_inputs.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <array>
#include <cassert>
#include <cstdio>

bool verify(arp::rhi::GpuDevice& device)
{
    using namespace arp;
    using namespace arp::rhi;
    using Inputs = tests::DdgiUniformInputs;
    tests::CapabilityOverrideDevice observed(device, nullptr);
    observed.observeBuffer(Inputs::label(true), 0u);
    observed.observeBuffer(Inputs::label(false), 1u);
    renderer::DdgiRenderer ddgi(observed);
    assert(
        ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                {core::makeShared<tests::FlatTerrainSurface>(), 64u, 64u},
                2u,
                2u,
                2u,
                8u,
                8u)
            .success);
    renderer::DdgiPushConstants pc{};
    for (unsigned axis = 0; axis < 3; ++axis)
        pc.probeGridSpacingStrength[axis] = 1;
    pc.updateParams[0] = 2;
    DescriptorLayoutDesc descriptors;
    descriptors.name = "SharedUniformConsumer";
    descriptors.bindings = {{0u, DescriptorType::UniformBuffer, ShaderStage::Compute},
        {1u, DescriptorType::UniformBuffer, ShaderStage::Compute},
        {2u, DescriptorType::StorageBuffer, ShaderStage::Compute}};
    PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back(descriptors);
    layoutDesc.descriptorSets.push_back({0u, descriptors.name});
    auto layout = device.createPipelineLayout(layoutDesc);
    assert(layout);
    // Consume both complete physical blocks as float4 bytes, not matrices.
    const core::pmr::String prefix = ARP_INPUT_SHADER_DIR "/ddgi_uniform_consumer";
    ShaderPackageDesc package;
    package.label = "SharedUniformConsumer";
    package.slangModulePath = prefix + ".slang";
    package.entrypoints = {{package.slangModulePath,
        "computeMain",
        ShaderStage::Compute,
        ShaderSourceLanguage::Slang}};
    package.artifacts.spirvPath = prefix + ".spv";
    package.artifacts.mslPath = prefix + ".metal";
    package.artifacts.wgslPath = prefix + ".wgsl";
    package.artifacts.reflectionPath = prefix + ".reflection.yaml";
    for (const auto& binding : descriptors.bindings)
        package.reflection.descriptorBindings.push_back(makeShaderReflectionBinding(0u, binding));
    assert(renderer::hydrateShaderPackageReflectionFromArtifact(package));
    assert(package.reflection.pushConstantByteCount == 0u);
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.label = "SharedUniformConsumer";
    pipelineDesc.layout = layout.get();
    pipelineDesc.shaderPackage = &package;
    pipelineDesc.computeShader = package.entrypoints[0];
    auto pipeline = device.createComputePipeline(pipelineDesc);
    auto output = device.createBuffer({"SharedUniformOutput",
        sizeof(Inputs::Bytes),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    auto readback = device.createBuffer({"SharedUniformReadback",
        sizeof(Inputs::Bytes),
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    auto arena = device.createDescriptorArena({"SharedUniformConsumer", 1u, 3u});
    assert(pipeline && output && readback && arena);
    auto set = arena->allocateDescriptorSet({"SharedUniformConsumer", descriptors});
    assert(set);
    for (unsigned binding = 0; binding < 2; ++binding) {
        const auto* uniform = observed.observedBuffer(binding);
        assert(uniform && uniform->desc().sizeBytes == (binding == 0 ? 96u : 272u));
        assert(
            set->write({binding,
                           DescriptorType::UniformBuffer,
                           uniform->id(),
                           DescriptorResourceKind::Buffer})
                .success);
    }
    assert(
        set->write(
               {2u, DescriptorType::StorageBuffer, output->id(), DescriptorResourceKind::Buffer})
            .success);
    const auto record = [&] {
        auto encoder = device.createCommandEncoder({"SharedUniformConsumer"});
        assert(encoder
            && encoder->transition(*output, output->state(), ResourceState::ShaderWrite).success);
        auto pass = encoder->beginComputePass({"SharedUniformConsumer"});
        assert(pass && pass->setPipeline(*pipeline).success
            && pass->bindDescriptorSet(0u, *set).success);
        assert(pass->dispatch(1u, 1u, 1u).success && pass->end().success);
        assert(encoder->transition(*output, ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success);
        auto copy = encoder->beginTransferPass({"SharedUniformReadback"});
        assert(copy
            && copy
                ->copyBufferToBuffer({output->id(), readback->id(), 0u, 0u, sizeof(Inputs::Bytes)})
                .success);
        assert(copy->end().success);
        return encoder->finish();
    };
    const auto fence = [&] {
        auto owner = core::makeShared<core::UniquePtr<GpuFence>>(device.createFence());
        return std::shared_ptr<GpuFence>(owner, owner->get());
    };
    const auto read = [&] {
        Inputs::Bytes sampled{};
        core::pmr::String diagnostic;
        assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*readback),
            sampled.data(),
            sizeof(sampled),
            0u,
            sizeof(sampled),
            diagnostic));
        return sampled;
    };
    const auto sample = [&] {
        pc.scrollOffsetFrame[3] = 0;
        assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
        auto commands = record();
        auto completed = fence();
        assert(commands && device.queue().submit(*commands, completed.get()).hasAcceptedWork());
        assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), completed));
        completed->wait();
        return read();
    };
    bool correct = true;
    const Inputs a(0.25f), b(0.75f);
    for (bool sky : {true, false}) {
        for (unsigned firstSlot : {0u, 1u}) {
            assert(a.write(ddgi, true).success && a.write(ddgi, false).success);
            auto gate = device.createTimelineSemaphore(0u);
            auto first = fence(), second = fence();
            assert(gate && first && second);
            pc.scrollOffsetFrame[3] = static_cast<float>(firstSlot);
            assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
            auto commandsA = record();
            assert(device.queue()
                    .submit(*commandsA, gate.get(), 1u, nullptr, 0u, first.get())
                    .hasAcceptedWork());
            assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), first));
            pc.scrollOffsetFrame[3] = static_cast<float>(1u - firstSlot);
            assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
            auto commandsB = record();
            assert(device.queue()
                    .submit(*commandsB, gate.get(), 2u, nullptr, 0u, second.get())
                    .hasAcceptedWork());
            const auto token = ddgi.preparedHistoryToken();
            assert(ddgi.acceptPreparedInput(token, second));
            const auto writes = observed.bufferWriteAttempts();
            const auto allocations = observed.bufferCreateAttempts();
            const bool bothPending = !first->isSignaled() && !second->isSignaled();
            const auto unchanged = a.write(ddgi, sky);
            const auto busyBoth = b.write(ddgi, sky);
            const bool noWriteBoth = writes == observed.bufferWriteAttempts()
                && allocations == observed.bufferCreateAttempts()
                && ddgi.preparedHistoryToken().generation == token.generation;
            gate->signal(1u);
            first->wait();
            const auto bytesA = read();
            const bool secondPending = !second->isSignaled();
            const auto busyOne = b.write(ddgi, sky);
            const bool noWriteOne = writes == observed.bufferWriteAttempts()
                && allocations == observed.bufferCreateAttempts()
                && ddgi.preparedHistoryToken().generation == token.generation;
            gate->signal(2u);
            second->wait();
            const auto bytesB = read();
            const bool held = bothPending && secondPending && unchanged.success && !busyBoth.success
                && !busyOne.success && busyBoth.diagnostics == Inputs::busyDiagnostic(sky)
                && busyOne.diagnostics == Inputs::busyDiagnostic(sky) && noWriteBoth && noWriteOne
                && bytesA == a.bytes() && bytesB == a.bytes();
            assert(b.write(ddgi, sky).success);
            assert(!ddgi.isPreparedInput(token));
            auto expected = a;
            if (sky)
                expected.sky = b.sky;
            else
                expected.csm = b.csm;
            const bool resumed = sample() == expected.bytes();
            std::fprintf(stderr,
                "Metal shared uniform sky=%d firstSlot=%u held=%d resumed=%d fullBytes=%zu\n",
                sky,
                firstSlot,
                held,
                resumed,
                sizeof(Inputs::Bytes));
            correct = held && resumed && correct;
        }
        for (bool afterWrite : {false, true}) {
            for (bool oldValue : {false, true}) {
                assert(a.write(ddgi, true).success && a.write(ddgi, false).success);
                assert(sample() == a.bytes());
                assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
                const auto token = ddgi.preparedHistoryToken();
                observed.failBufferWrites(Inputs::label(sky), afterWrite);
                const auto failed = b.write(ddgi, sky);
                assert(!failed.success
                    && failed.diagnostics.find(afterWrite ? "TEST-BUFFER-UPLOAD-FAILED-AFTER-WRITE"
                                                          : "TEST-BUFFER-UPLOAD-FAILED")
                        != std::string_view::npos);
                const auto rejected = ddgi.prepareProbeUpdateSchedule(pc, 1u);
                assert(!rejected.success && rejected.diagnostics == Inputs::invalidDiagnostic(sky));
                assert(
                    ddgi.preparedHistoryToken().generation == 0u && !ddgi.isPreparedInput(token));
                observed.failBufferWrites({});
                const auto writes = observed.bufferWriteAttempts();
                assert((oldValue ? a : b).write(ddgi, sky).success);
                assert(observed.bufferWriteAttempts() == writes + 1u);
                auto expected = a;
                if (!oldValue) {
                    if (sky)
                        expected.sky = b.sky;
                    else
                        expected.csm = b.csm;
                }
                const bool recovered = sample() == expected.bytes();
                assert(!ddgi.isPreparedInput(token));
                std::fprintf(stderr,
                    "Metal shared uniform sky=%d afterWrite=%d oldValue=%d recovered=%d fullBytes=%zu\n",
                    sky,
                    afterWrite,
                    oldValue,
                    recovered,
                    sizeof(Inputs::Bytes));
                correct = recovered && correct;
            }
        }
    }
    return correct;
}

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
    const bool correct = verify(*device);
    device->waitIdle();
    const bool retired = device->memoryStats().allocatedBytes == 0u;
    std::fprintf(stderr, "DDGI shared uniforms correct=%d retired=%d\n", correct, retired);
    return correct && retired ? 0 : 1;
}
