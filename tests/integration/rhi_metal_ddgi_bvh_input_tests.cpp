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
enum class Domain { Flat, Static, Dynamic };

bool verifyBvh(arp::rhi::GpuDevice& device, Domain domain, bool mutate)
{
    using namespace arp;
    using namespace arp::rhi;
    tests::CapabilityOverrideDevice nodes(device, nullptr);
    nodes.observeBuffer(domain == Domain::Dynamic ? "DdgiDynamicTlasNodes" : "DdgiSceneBvhNodes");
    tests::CapabilityOverrideDevice leaves(nodes, nullptr);
    leaves.observeBuffer(
        domain == Domain::Dynamic ? "DdgiDynamicTlasLeaves" : "DdgiSceneBvhLeaves");
    renderer::DdgiRenderer ddgi(leaves);
    const auto created = ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
        {core::makeShared<tests::FlatTerrainSurface>(), 64u, 64u},
        2u,
        2u,
        2u,
        8u,
        8u);
    assert(created.success && leaves.observedBuffer() && nodes.observedBuffer());
    auto& leafBuffer = *leaves.observedBuffer();
    auto& nodeBuffer = *nodes.observedBuffer();
    renderer::DdgiSceneBvhTriangle triangle{};
    triangle.v0[0] = -1.0f;
    triangle.v1[0] = 1.0f;
    triangle.v2[2] = 1.0f;
    triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = -1.0f;
    triangle.stableId = 17u;
    renderer::DdgiSceneBvhInput bvh;
    bvh.geometryRevision = 1u;
    bvh.staticGeometryRevision = 1u;
    bvh.dynamicGeometryRevision = 1u;
    if (domain == Domain::Flat) {
        bvh.triangles = &triangle;
        bvh.triangleCount = 1u;
    }
    if (domain == Domain::Static) {
        bvh.staticTriangles = &triangle;
        bvh.staticTriangleCount = 1u;
    }
    if (domain == Domain::Dynamic) {
        bvh.dynamicTriangles = &triangle;
        bvh.dynamicTriangleCount = 1u;
    }
    const auto initial = ddgi.updateSceneBvh(bvh);
    assert(initial.success && initial.triangleCount == 1u);
    renderer::DdgiPushConstants push{};
    for (unsigned axis = 0u; axis != 3u; ++axis) {
        push.probeGridSpacingStrength[axis] = 1.0f;
        push.probeCountRayCount[axis] = 2.0f;
    }
    push.updateParams[0] = 8.0f;
    assert(ddgi.prepareProbeUpdateSchedule(push, 1u).success);
    const auto token = ddgi.preparedHistoryToken();
    DescriptorLayoutDesc descriptors;
    descriptors.name = "BvhInputConsumer";
    descriptors.bindings = {{0u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute},
        {1u, DescriptorType::StorageBuffer, ShaderStage::Compute},
        {2u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute}};
    PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back(descriptors);
    layoutDesc.descriptorSets.push_back({0u, descriptors.name});
    auto layout = device.createPipelineLayout(layoutDesc);
    assert(layout);
    const core::pmr::String prefix = ARP_INPUT_SHADER_DIR "/ddgi_bvh_input_consumer";
    ShaderPackageDesc package;
    package.label = "BvhInputConsumer";
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
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.label = "BvhInputConsumer";
    pipelineDesc.layout = layout.get();
    pipelineDesc.shaderPackage = &package;
    pipelineDesc.computeShader = package.entrypoints[0];
    auto pipeline = device.createComputePipeline(pipelineDesc);
    auto output = device.createBuffer({"BvhOutput",
        64u,
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    auto readback = device.createBuffer({"BvhReadback",
        64u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    auto arena = device.createDescriptorArena({"BvhInputConsumer", 1u, 3u});
    assert(pipeline && output && readback && arena);
    auto set = arena->allocateDescriptorSet({"BvhInputConsumer", descriptors});
    assert(set);
    assert(
        set->write({0u,
                       DescriptorType::ReadOnlyStorageBuffer,
                       leafBuffer.id(),
                       DescriptorResourceKind::Buffer})
            .success);
    assert(
        set->write(
               {1u, DescriptorType::StorageBuffer, output->id(), DescriptorResourceKind::Buffer})
            .success);
    assert(
        set->write({2u,
                       DescriptorType::ReadOnlyStorageBuffer,
                       nodeBuffer.id(),
                       DescriptorResourceKind::Buffer})
            .success);
    const auto record = [&] {
        auto encoder = device.createCommandEncoder({"BvhInputConsumer"});
        assert(encoder);
        for (auto* input : {&leafBuffer, &nodeBuffer})
            if (input->state() != ResourceState::ShaderRead)
                assert(
                    encoder->transition(*input, input->state(), ResourceState::ShaderRead).success);
        assert(encoder->transition(*output, output->state(), ResourceState::ShaderWrite).success);
        auto pass = encoder->beginComputePass({"BvhInputConsumer"});
        assert(pass && pass->setPipeline(*pipeline).success);
        assert(pass->bindDescriptorSet(0u, *set).success);
        assert(pass->dispatch(1u, 1u, 1u).success && pass->end().success);
        assert(encoder->transition(*output, ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success);
        auto copy = encoder->beginTransferPass({"BvhReadback"});
        assert(
            copy && copy->copyBufferToBuffer({output->id(), readback->id(), 0u, 0u, 64u}).success);
        assert(copy->end().success);
        return encoder->finish();
    };
    auto commands = record();
    auto fenceOwner = core::makeShared<core::UniquePtr<GpuFence>>(device.createFence());
    const std::shared_ptr<GpuFence> completion(fenceOwner, fenceOwner->get());
    auto gate = device.createTimelineSemaphore(0u);
    assert(commands && completion && gate);
    assert(device.queue()
            .submit(*commands, gate.get(), 1u, nullptr, 0u, completion.get())
            .hasAcceptedWork());
    const bool accepted = ddgi.acceptPreparedInput(token, completion);
    const bool pending = !completion->isSignaled();
    const bool otherSlot = ddgi.inputSlotReady(1u);
    const auto reuse = ddgi.updateSceneBvh(bvh);
    const bool reused = reuse.success && reuse.reused && reuse.uploadBytes == 0u;
    renderer::DdgiRenderer::SceneBvhUpdateResult changed;
    if (mutate) {
        triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 1.0f;
        bvh.geometryRevision = 2u;
        if (domain == Domain::Static)
            bvh.staticGeometryRevision = 2u;
        else
            bvh.dynamicGeometryRevision = 2u;
        changed = ddgi.updateSceneBvh(bvh);
    }
    gate->signal(1u);
    completion->wait();
    device.waitIdle();
    std::array<float, 16> sampled{};
    core::pmr::String diagnostic;
    assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*readback),
        sampled.data(),
        sizeof(sampled),
        0u,
        sizeof(sampled),
        diagnostic));
    const bool correct =
        sampled[1] == -1.0f && sampled[5] == -1.0f && sampled[9] == -1.0f && sampled[13] == -1.0f;
    std::fprintf(stderr,
        "BVH domain=%u mutate=%d accepted=%d pending=%d otherSlot=%d updated=%d bytes=%llu leafY=%g nodeMinY=%g expected=-1 correct=%d diagnostic=%s\n",
        static_cast<unsigned>(domain),
        mutate,
        accepted,
        pending,
        otherSlot,
        changed.success,
        static_cast<unsigned long long>(changed.uploadBytes),
        sampled[1],
        sampled[9],
        correct,
        changed.diagnostic.c_str());
    const bool busy = !mutate
        || (!changed.success
            && std::string_view(changed.diagnostic) == "DDGI-SHARED-BVH-INPUT-NOT-READY");
    bool resumed = true;
    if (mutate) {
        const auto updated = ddgi.updateSceneBvh(bvh);
        assert(updated.success);
        push.scrollOffsetFrame[3] = 1.0f;
        assert(ddgi.prepareProbeUpdateSchedule(push, 2u).success);
        auto commandsB = record();
        auto ownerB = core::makeShared<core::UniquePtr<GpuFence>>(device.createFence());
        const std::shared_ptr<GpuFence> completionB(ownerB, ownerB->get());
        assert(device.queue().submit(*commandsB, completionB.get()).hasAcceptedWork());
        assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), completionB));
        completionB->wait();
        device.waitIdle();
        assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*readback),
            sampled.data(),
            sizeof(sampled),
            0u,
            sizeof(sampled),
            diagnostic));
        resumed =
            sampled[1] == 1.0f && sampled[5] == 1.0f && sampled[9] == 1.0f && sampled[13] == 1.0f;
    }
    std::fprintf(stderr,
        "BVH domain=%u mutate=%d noChangeReuse=%d busy=%d resumed=%d\n",
        static_cast<unsigned>(domain),
        mutate,
        reused,
        busy,
        resumed);
    return accepted && pending && otherSlot && correct && reused && busy && resumed;
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
    bool controls = true, mutations = true;
    for (const auto domain : {Domain::Flat, Domain::Static, Domain::Dynamic}) {
        controls = verifyBvh(*device, domain, false) && controls;
        mutations = verifyBvh(*device, domain, true) && mutations;
    }
    device->waitIdle();
    const bool retired = device->memoryStats().allocatedBytes == 0u;
    std::fprintf(
        stderr, "BVH input controls=%d mutations=%d retired=%d\n", controls, mutations, retired);
    return controls && mutations && retired ? 0 : 1;
}
