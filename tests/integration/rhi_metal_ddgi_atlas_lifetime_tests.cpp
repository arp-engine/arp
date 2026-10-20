#include "renderer/gi/ddgi_renderer.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using namespace arp;
using namespace arp::rhi;

// Test-only owner for the two Slang fixtures; production DDGI usages stay intact.
class AtlasProgram {
public:
    AtlasProgram(GpuDevice& device, bool fill, bool alternate = false)
    {
        DescriptorLayoutDesc descriptors;
        descriptors.name = fill ? "AtlasFill" : "AtlasConsumer";
        DescriptorBindingDesc textureBinding{0u,
            fill ? DescriptorType::StorageTexture : DescriptorType::SampledTexture,
            ShaderStage::Compute};
        if (fill)
            textureBinding.storageTextureFormat = TextureFormat::Rgba16Float;
        descriptors.bindings.push_back(textureBinding);
        descriptors.bindings.push_back({1u,
            fill ? DescriptorType::UniformBuffer : DescriptorType::StorageBuffer,
            ShaderStage::Compute});
        PipelineLayoutDesc layoutDesc;
        layoutDesc.descriptorLayouts.push_back(descriptors);
        layoutDesc.descriptorSets.push_back({0u, descriptors.name});
        layout_ = device.createPipelineLayout(layoutDesc);
        const core::pmr::String prefix = fill ? ARP_ATLAS_SHADER_DIR "/ddgi_atlas_fill"
                                              : ARP_ATLAS_CONSUMER_DIR "/ddgi_atlas_consumer";
        package_.label = descriptors.name;
        package_.slangModulePath = prefix + ".slang";
        package_.entrypoints = {{package_.slangModulePath,
            "computeMain",
            ShaderStage::Compute,
            ShaderSourceLanguage::Slang}};
        package_.artifacts.spirvPath = prefix + ".spv";
        package_.artifacts.mslPath = prefix + ".metal";
        package_.artifacts.wgslPath = prefix + ".wgsl";
        package_.artifacts.reflectionPath = prefix + ".reflection.yaml";
        for (const auto& binding : descriptors.bindings)
            package_.reflection.descriptorBindings.push_back(
                makeShaderReflectionBinding(0u, binding));
        assert(renderer::hydrateShaderPackageReflectionFromArtifact(package_));
        assert(package_.reflection.pushConstantByteCount == 0u);
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.label = descriptors.name;
        pipelineDesc.layout = layout_.get();
        pipelineDesc.shaderPackage = &package_;
        pipelineDesc.computeShader = package_.entrypoints[0];
        pipeline_ = device.createComputePipeline(pipelineDesc);
        arena_ = device.createDescriptorArena({descriptors.name, 1u, 2u});
        assert(layout_ && pipeline_ && arena_);
        set_ = arena_->allocateDescriptorSet({descriptors.name, descriptors});
        assert(set_);
        textureType_ = textureBinding.type;
        if (fill) {
            const std::array<float, 4> bytes = alternate
                ? std::array<float, 4>{0.75f, 0.25f, 0.5f, 1.f}
                : std::array<float, 4>{0.25f, 0.5f, 0.75f, 1.f};
            fillValue_ = device.createBuffer({"AtlasFillValue",
                sizeof(bytes),
                ResourceUsage::UniformBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(
                fillValue_ && device.writeBuffer(*fillValue_, bytes.data(), sizeof(bytes)).success);
            assert(
                set_->write({1u,
                                DescriptorType::UniformBuffer,
                                fillValue_->id(),
                                DescriptorResourceKind::Buffer})
                    .success);
        }
    }

    void bind(const GpuTextureView& view, const GpuBuffer* output = nullptr)
    {
        assert(set_->write({0u, textureType_, view.id(), DescriptorResourceKind::TextureView})
                .success);
        if (output)
            assert(
                set_->write({1u,
                                DescriptorType::StorageBuffer,
                                output->id(),
                                DescriptorResourceKind::Buffer})
                    .success);
    }

    void record(CommandEncoder& encoder, unsigned width, unsigned height)
    {
        auto pass = encoder.beginComputePass({"AtlasFixture"});
        assert(pass && pass->setPipeline(*pipeline_).success
            && pass->bindDescriptorSet(0u, *set_).success);
        assert(pass->dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u).success
            && pass->end().success);
    }

private:
    ShaderPackageDesc package_;
    core::UniquePtr<PipelineLayout> layout_;
    core::UniquePtr<ComputePipeline> pipeline_;
    core::UniquePtr<DescriptorArena> arena_;
    core::UniquePtr<DescriptorSet> set_;
    core::UniquePtr<GpuBuffer> fillValue_;
    DescriptorType textureType_{};
};

bool verify(arp::rhi::GpuDevice& device, unsigned domain, bool shaderReader)
{
    using namespace arp;
    using namespace arp::rhi;
    renderer::DdgiRenderer ddgi(device);
    assert(
        ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                {core::makeShared<tests::FlatTerrainSurface>(), 64u, 64u},
                2u,
                2u,
                2u,
                8u,
                8u)
            .success);
    auto texture = domain == 0 ? ddgi.probeIrradiance(0)
        : domain == 1          ? ddgi.probeDepthMoments(0)
                               : ddgi.giOutput(0);
    assert(texture);
    std::weak_ptr<GpuTexture> wrapperLifetime = texture;
    const auto originalId = texture->id();
    const auto extent = texture->desc().extent;
    const auto aligned =
        core::alignCpuAllocationOffsetUp(extent.width * 8u, kTextureUploadRowPitchAlignment);
    assert(aligned.success && aligned.alignedValue <= std::numeric_limits<std::uint32_t>::max());
    const auto pitch = static_cast<std::uint32_t>(aligned.alignedValue);
    core::pmr::Vector<std::byte> input(pitch * extent.height);
    // Exact IEEE binary16 encodings of .25, .5, .75 and 1; no conversion oracle.
    constexpr std::array<std::uint16_t, 4> rgba{0x3400, 0x3800, 0x3a00, 0x3c00};
    for (unsigned y = 0; y < extent.height; ++y)
        for (unsigned x = 0; x < extent.width; ++x)
            std::memcpy(input.data() + y * pitch + x * 8u, rgba.data(), sizeof(rgba));
    const TextureSubresourceRange range{0u, 1u, 0u, 1u, TextureAspect::Color};
    {
        AtlasProgram fill(device, true);
        auto view = device.createTextureView(*texture,
            {"AtlasFill",
                TextureFormat::Rgba16Float,
                TextureUsage::Storage,
                TextureViewDimension::Texture2D,
                0u,
                1u,
                0u,
                1u,
                TextureAspect::Color});
        assert(view);
        fill.bind(*view);
        auto initialize = device.createCommandEncoder({"AtlasInitialize"});
        assert(initialize
            && initialize->transition(*texture, range, texture->state(), ResourceState::ShaderWrite)
                .success);
        fill.record(*initialize, extent.width, extent.height);
        assert(initialize
                ->transition(*texture,
                    range,
                    ResourceState::ShaderWrite,
                    shaderReader ? ResourceState::ShaderRead : ResourceState::CopySrc)
                .success);
        auto commands = initialize->finish();
        auto done = device.createFence();
        assert(device.queue().submit(*commands, done.get()).hasAcceptedWork());
        done->wait();
    }
    const auto readbackBytes =
        shaderReader ? extent.width * extent.height * sizeof(float) * 4u : input.size();
    auto readback = device.createBuffer({"AtlasRetirementReadback",
        readbackBytes,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    auto encoder = device.createCommandEncoder({"AtlasOldReader"});
    assert(readback && encoder);
    core::UniquePtr<AtlasProgram> consumer;
    core::UniquePtr<GpuTextureView> sampled;
    core::UniquePtr<GpuBuffer> values;
    if (shaderReader) {
        consumer = core::makeUnique<AtlasProgram>(device, false);
        sampled = device.createTextureView(*texture,
            {"AtlasSampled",
                TextureFormat::Rgba16Float,
                TextureUsage::Sampled,
                TextureViewDimension::Texture2D,
                0u,
                1u,
                0u,
                1u,
                TextureAspect::Color});
        values = device.createBuffer({"AtlasValues",
            readbackBytes,
            ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined});
        assert(sampled && values);
        consumer->bind(*sampled, values.get());
        assert(encoder->transition(*values, ResourceState::Undefined, ResourceState::ShaderWrite)
                .success);
        consumer->record(*encoder, extent.width, extent.height);
        assert(encoder->transition(*values, ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success);
        auto copy = encoder->beginTransferPass({"AtlasValuesReadback"});
        assert(copy
            && copy->copyBufferToBuffer({values->id(), readback->id(), 0u, 0u, readbackBytes})
                .success);
        assert(copy->end().success);
    } else {
        auto copy = encoder->beginTransferPass({"AtlasOldReader"});
        assert(copy
            && copy
                ->copyTextureToBuffer(
                    {texture->id(), readback->id(), range, 0u, pitch, extent.height, extent})
                .success
            && copy->end().success);
    }
    auto commands = encoder->finish();
    auto gate = device.createTimelineSemaphore(0u);
    auto owner = core::makeShared<core::UniquePtr<GpuFence>>(device.createFence());
    std::shared_ptr<GpuFence> completed(owner, owner->get());
    renderer::DdgiPushConstants pc{};
    for (unsigned axis = 0; axis < 3; ++axis)
        pc.probeGridSpacingStrength[axis] = 1;
    pc.updateParams[0] = 2;
    assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
    const auto originalToken = ddgi.preparedHistoryToken();
    assert(device.queue()
            .submit(*commands, gate.get(), 1u, nullptr, 0u, completed.get())
            .hasAcceptedWork());
    assert(ddgi.acceptPreparedInput(originalToken, completed));
    const bool pending = !completed->isSignaled();
    ddgi.destroy();
    consumer.reset();
    sampled.reset();
    values.reset();
    texture.reset();
    const bool wrapperDestroyed = wrapperLifetime.expired();
    const auto retainedBytes = device.memoryStats().allocatedBytes;
    // Recreate the actual renderer before releasing the old native reader.
    // Its initialization submits work but must not wait for the blocked queue.
    assert(
        ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                {core::makeShared<tests::FlatTerrainSurface>(), 64u, 64u},
                2u,
                2u,
                2u,
                16u,
                8u)
            .success);
    auto replacement = domain == 0 ? ddgi.probeIrradiance(0)
        : domain == 1              ? ddgi.probeDepthMoments(0)
                                   : ddgi.giOutput(0);
    const bool fresh = replacement->id() != originalId && !ddgi.hasValidGiHistory()
        && !ddgi.isPreparedInput(originalToken);
    const auto coexistingBytes = device.memoryStats().allocatedBytes;
    const bool pendingAfterRecreate = !completed->isSignaled();
    gate->signal(1u);
    completed->wait();
    core::pmr::Vector<std::byte> actual(readbackBytes);
    core::pmr::String diagnostic;
    assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*readback),
        actual.data(),
        actual.size(),
        0u,
        actual.size(),
        diagnostic));
    bool content = true;
    for (unsigned y = 0; y < extent.height; ++y) {
        for (unsigned x = 0; x < extent.width; ++x) {
            if (shaderReader) {
                constexpr std::array<float, 4> expected{0.25f, 0.5f, 0.75f, 1.f};
                content = std::memcmp(actual.data() + (y * extent.width + x) * sizeof(expected),
                              expected.data(),
                              sizeof(expected))
                        == 0
                    && content;
            } else {
                content =
                    std::memcmp(
                        actual.data() + y * pitch + x * 8u, input.data() + y * pitch + x * 8u, 8u)
                        == 0
                    && content;
            }
        }
    }
    std::fprintf(stderr,
        "DDGI atlas domain=%u shaderReader=%d accepted=1 pending=%d wrapperDestroyed=%d retainedBytes=%llu coexistingBytes=%llu pendingAfterRecreate=%d content=%d fresh=%d\n",
        domain,
        shaderReader,
        pending,
        wrapperDestroyed,
        static_cast<unsigned long long>(retainedBytes),
        static_cast<unsigned long long>(coexistingBytes),
        pendingAfterRecreate,
        content,
        fresh);
    return pending && wrapperDestroyed
        && retainedBytes >= readbackBytes + extent.width * extent.height * 8u
        && coexistingBytes > retainedBytes && pendingAfterRecreate && content && fresh;
}

bool verifyRebinding(GpuDevice& device, unsigned domain, unsigned firstSlot)
{
    renderer::DdgiRenderer ddgi(device);
    assert(
        ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                {core::makeShared<tests::FlatTerrainSurface>(), 64u, 64u},
                2u,
                2u,
                2u,
                8u,
                8u)
            .success);
    const TextureSubresourceRange range{0u, 1u, 0u, 1u, TextureAspect::Color};
    std::array<std::shared_ptr<GpuTexture>, 2> textures;
    std::array<std::weak_ptr<GpuTexture>, 2> lifetimes;
    std::array<core::UniquePtr<GpuTextureView>, 2> views;
    std::array<core::UniquePtr<GpuBuffer>, 2> outputs, readbacks;
    std::array<std::shared_ptr<GpuFence>, 2> completions;
    std::array<core::UniquePtr<CommandBuffer>, 2> commands;
    auto consumer = core::makeUnique<AtlasProgram>(device, false);
    unsigned width = 0u, height = 0u;
    for (unsigned version = 0; version < 2u; ++version) {
        const unsigned slot = version == 0u ? firstSlot : 1u - firstSlot;
        textures[version] = domain == 0u ? ddgi.probeIrradiance(slot)
            : domain == 1u               ? ddgi.probeDepthMoments(slot)
                                         : ddgi.giOutput(slot);
        auto& texture = *textures[version];
        lifetimes[version] = textures[version];
        width = texture.desc().extent.width;
        height = texture.desc().extent.height;
        {
            AtlasProgram fill(device, true, version == 1u);
            auto view = device.createTextureView(texture,
                {"AtlasVersionFill",
                    TextureFormat::Rgba16Float,
                    TextureUsage::Storage,
                    TextureViewDimension::Texture2D,
                    0u,
                    1u,
                    0u,
                    1u,
                    TextureAspect::Color});
            assert(view);
            fill.bind(*view);
            auto encoder = device.createCommandEncoder({"AtlasVersionFill"});
            assert(encoder
                && encoder->transition(texture, range, texture.state(), ResourceState::ShaderWrite)
                    .success);
            fill.record(*encoder, width, height);
            assert(encoder
                    ->transition(
                        texture, range, ResourceState::ShaderWrite, ResourceState::ShaderRead)
                    .success);
            auto initialize = encoder->finish();
            auto done = device.createFence();
            assert(initialize && done
                && device.queue().submit(*initialize, done.get()).hasAcceptedWork());
            done->wait();
        }
        const auto bytes = width * height * sizeof(float) * 4u;
        views[version] = device.createTextureView(texture,
            {"AtlasVersionRead",
                TextureFormat::Rgba16Float,
                TextureUsage::Sampled,
                TextureViewDimension::Texture2D,
                0u,
                1u,
                0u,
                1u,
                TextureAspect::Color});
        outputs[version] = device.createBuffer({"AtlasVersionValues",
            bytes,
            ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined});
        readbacks[version] = device.createBuffer({"AtlasVersionReadback",
            bytes,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        auto owner = core::makeShared<core::UniquePtr<GpuFence>>(device.createFence());
        completions[version] = std::shared_ptr<GpuFence>(owner, owner->get());
        assert(views[version] && outputs[version] && readbacks[version] && completions[version]);
    }
    auto gate = device.createTimelineSemaphore(0u);
    assert(gate);
    for (unsigned version = 0; version < 2u; ++version) {
        const unsigned slot = version == 0u ? firstSlot : 1u - firstSlot;
        // The exact same descriptor set is rewritten after A is accepted.
        // Both the sampled view and output buffer must be snapshotted per dispatch.
        consumer->bind(*views[version], outputs[version].get());
        auto encoder = device.createCommandEncoder({"AtlasVersionRead"});
        assert(encoder
            && encoder
                ->transition(
                    *outputs[version], ResourceState::Undefined, ResourceState::ShaderWrite)
                .success);
        consumer->record(*encoder, width, height);
        assert(encoder
                ->transition(*outputs[version], ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success);
        auto transfer = encoder->beginTransferPass({"AtlasVersionReadback"});
        assert(transfer
            && transfer
                ->copyBufferToBuffer({outputs[version]->id(),
                    readbacks[version]->id(),
                    0u,
                    0u,
                    readbacks[version]->desc().sizeBytes})
                .success
            && transfer->end().success);
        commands[version] = encoder->finish();
        renderer::DdgiPushConstants pc{};
        for (unsigned axis = 0; axis < 3u; ++axis)
            pc.probeGridSpacingStrength[axis] = 1.f;
        pc.updateParams[0] = 2.f;
        pc.scrollOffsetFrame[3] = static_cast<float>(slot);
        assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
        assert(device.queue()
                .submit(*commands[version],
                    gate.get(),
                    version + 1u,
                    nullptr,
                    0u,
                    completions[version].get())
                .hasAcceptedWork());
        assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), completions[version]));
    }
    const bool bothPending = !completions[0]->isSignaled() && !completions[1]->isSignaled();
    ddgi.destroy();
    consumer.reset();
    views = {};
    outputs = {};
    textures = {};
    const bool wrappersDestroyed = lifetimes[0].expired() && lifetimes[1].expired();
    bool contents = true, secondHeld = false;
    for (unsigned version = 0; version < 2u; ++version) {
        gate->signal(version + 1u);
        completions[version]->wait();
        if (version == 0u)
            secondHeld = !completions[1]->isSignaled();
        core::pmr::Vector<std::array<float, 4>> actual(width * height);
        core::pmr::String diagnostic;
        const auto bytes = readbacks[version]->desc().sizeBytes;
        assert(rhi_metal::readMetalRhiNativeBuffer(
            rhi_metal::metalRhiNativePayloadFor(*readbacks[version]),
            actual.data(),
            bytes,
            0u,
            bytes,
            diagnostic));
        const std::array<float, 4> expected = version == 0u
            ? std::array<float, 4>{0.25f, 0.5f, 0.75f, 1.f}
            : std::array<float, 4>{0.75f, 0.25f, 0.5f, 1.f};
        for (const auto& pixel : actual)
            contents = pixel == expected && contents;
    }
    std::fprintf(stderr,
        "DDGI atlas rebind domain=%u firstSlot=%u bothPending=%d wrappersDestroyed=%d secondHeld=%d contents=%d\n",
        domain,
        firstSlot,
        bothPending,
        wrappersDestroyed,
        secondHeld,
        contents);
    return bothPending && wrappersDestroyed && secondHeld && contents;
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
    bool correct = true;
    for (bool shaderReader : {false, true})
        for (unsigned domain = 0; domain < 3; ++domain)
            correct = verify(*device, domain, shaderReader) && correct;
    for (unsigned domain = 0; domain < 3u; ++domain)
        for (unsigned firstSlot : {0u, 1u})
            correct = verifyRebinding(*device, domain, firstSlot) && correct;
    device->waitIdle();
    const bool retired = device->memoryStats().allocatedBytes == 0u;
    std::fprintf(stderr, "DDGI atlas correct=%d retired=%d\n", correct, retired);
    return correct && retired ? 0 : 1;
}
