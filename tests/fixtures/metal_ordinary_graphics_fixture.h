#pragma once
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_native_commands.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
using namespace arp::rhi;
using namespace arp::rhi_metal;

class OrdinaryGraphicsFixture final {
public:
    OrdinaryGraphicsFixture(const char* vs,
        const char* fs,
        bool arrays,
        DescriptorLayoutDesc authoredInputs = {},
        core::pmr::Vector<PushConstantRange> pushRanges = {},
        core::pmr::Vector<VertexBufferLayoutDesc> vertexLayouts = {},
        std::uint32_t arrayArgumentBuffers = 1u,
        PrimitiveTopology topology = PrimitiveTopology::TriangleList)
        : arrays(arrays), arrayArgumentBuffers(arrayArgumentBuffers)
    {
        quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        diagnostic.reserve(512);
        shader::SingleEntryShaderLoadBudget load{cpuBytes, cpuBytes * 4u, cpuBytes, 16u};
        auto vertex = shader::SingleEntryShaderArtifact::load(vs, load, quota, diagnostic);
        assert(vertex);
        std::shared_ptr<const shader::SingleEntryShaderArtifact> fragment;
        if (fs) {
            fragment = shader::SingleEntryShaderArtifact::load(fs, load, quota, diagnostic);
            assert(fragment);
        }
        auto desc = deviceDesc();
        device = createMetalRhiDevice(desc);
        assert(device);
        assert(device->capabilities().commandRetention.contractVersion == 0u);
        registry = metalRhiIssuingResourceRegistry(*device);
        const bool custom = !authoredInputs.name.empty() || !authoredInputs.bindings.empty();
        inputs = std::move(authoredInputs);
        if (!custom)
            inputs.name = "OrdinaryGraphicsPreparedInputs";
        if (!custom && arrays)
            inputs.bindings = {{2u, DescriptorType::SampledTexture, ShaderStage::Fragment},
                {7u, DescriptorType::SampledTexture, ShaderStage::Fragment, 3u}};
        else if (!custom)
            inputs.bindings = {{0u, DescriptorType::SampledTexture, ShaderStage::Fragment},
                {1u, DescriptorType::Sampler, ShaderStage::Fragment}};
        PipelineLayoutDesc authored;
        authored.descriptorLayouts = {inputs};
        authored.descriptorSets = {{setIndex(), inputs.name}};
        authored.pushConstantRanges = std::move(pushRanges);
        auto layout = device->createPipelineLayout(authored);
        assert(layout);
        RenderPipelineDesc state;
        state.label = "OrdinaryPreparedGraphics";
        state.layout = layout.get();
        if (fs)
            state.colorFormat = TextureFormat::Rgba8Unorm;
        else {
            state.depthStencil.format = TextureFormat::D32Float;
            state.depthStencil.depthTestEnabled = true;
            state.depthStencil.depthWriteEnabled = true;
        }
        state.dynamicViewportScissor = true;
        state.topology = topology;
        state.vertexBuffers = std::move(vertexLayouts);
        provider = MetalRhiOrdinaryGraphicsProvider::create(vertex,
            fragment,
            std::move(state),
            desc.capabilityContract,
            budget(),
            quota,
            diagnostic);
        if (!provider)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(provider);
        layout.reset();
        authored.descriptorLayouts.clear();
        authored.descriptorSets.clear();
        texture = device->createTexture({"Input",
            {1u, 1u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        assert(texture);
        view = device->createTextureView(
            *texture, {"View", TextureFormat::Rgba8Unorm, TextureUsage::Sampled});
        assert(view);
        sampler = device->createSampler({});
        assert(sampler);
        arena = device->createDescriptorArena({"Inputs", 4u, 16u});
        assert(arena);
        set = arena->allocateDescriptorSet({"Set", inputs});
        assert(set);
        if (!custom)
            write();
    }

    static MetalRhiDeviceDesc deviceDesc()
    {
        MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.memoryBudgetBytes = cpuBytes;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        return desc;
    }

    void write()
    {
        assert(
            set->write({arrays ? 2u : 0u,
                           DescriptorType::SampledTexture,
                           view->id(),
                           DescriptorResourceKind::TextureView})
                .success);
        if (arrays)
            for (unsigned i = 0; i < 3; ++i) {
                DescriptorBindingWrite w{7u,
                    DescriptorType::SampledTexture,
                    view->id(),
                    DescriptorResourceKind::TextureView};
                w.arrayElement = i;
                assert(set->write(w).success);
            }
        else
            assert(
                set->write({1u,
                               DescriptorType::Sampler,
                               sampler->id(),
                               DescriptorResourceKind::Sampler})
                    .success);
    }

    unsigned setIndex() const { return 0u; }

    MetalRhiOrdinaryBindingBudget budget() const
    {
        return {1u, true, cpuBytes, arrays ? arrayArgumentBuffers : 0u, arrays ? 65536u : 0u};
    }

    auto prepare(const std::uint32_t* offsets = nullptr, std::uint32_t count = 0u)
    {
        auto result = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
            provider, *set, setIndex(), registry, budget(), quota, diagnostic, offsets, count);
        if (!result)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(result);
        return result;
    }

    void reject(const MetalRhiOrdinaryBindingBudget& b,
        const char* expected,
        std::shared_ptr<MetalRhiResourceRegistry> selected = {},
        unsigned index = UINT32_MAX)
    {
        const auto before = quota->liveBytes();
        auto result = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(provider,
            *set,
            index == UINT32_MAX ? setIndex() : index,
            selected ? selected : registry,
            b,
            quota,
            diagnostic);
        if (result || diagnostic.find(expected) == diagnostic.npos)
            std::fprintf(stderr, "Expected %s: %s\n", expected, diagnostic.c_str());
        assert(!result && diagnostic.find(expected) != diagnostic.npos
            && quota->liveBytes() == before);
    }

    static constexpr std::uint64_t cpuBytes = 1024u * 1024u;
    bool arrays;
    std::uint32_t arrayArgumentBuffers;
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    std::shared_ptr<core::CpuMemoryQuota> quota;
    core::UniquePtr<GpuDevice> device;
    std::shared_ptr<MetalRhiResourceRegistry> registry;
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider;
    DescriptorLayoutDesc inputs;
    core::UniquePtr<GpuTexture> texture;
    core::UniquePtr<GpuTextureView> view;
    core::UniquePtr<GpuSampler> sampler;
    core::UniquePtr<DescriptorArena> arena;
    core::UniquePtr<DescriptorSet> set;
};

class OrdinaryGraphicsPassFixture final {
public:
    OrdinaryGraphicsPassFixture(const char* vs,
        const char* fs,
        bool arrays = false,
        DescriptorLayoutDesc inputs = {},
        core::pmr::Vector<PushConstantRange> pushRanges = {},
        core::pmr::Vector<VertexBufferLayoutDesc> vertexLayouts = {},
        std::uint32_t arrayArgumentBuffers = 1u,
        PrimitiveTopology topology = PrimitiveTopology::TriangleList)
        : graphics(vs,
              fs,
              arrays,
              std::move(inputs),
              std::move(pushRanges),
              std::move(vertexLayouts),
              arrayArgumentBuffers,
              topology)
    {
        const auto format = fs ? TextureFormat::Rgba8Unorm : TextureFormat::D32Float;
        target = graphics.device->createTexture({"PassTarget",
            {4u, 4u, 1u},
            format,
            TextureUsage::RenderAttachment | TextureUsage::CopySrc,
            MemoryUsage::GpuOnly,
            fs ? ResourceState::ColorAttachment : ResourceState::DepthAttachment});
        assert(target);
        TextureViewDesc targetView{"PassView", format, TextureUsage::RenderAttachment};
        targetView.aspect = fs ? TextureAspect::Color : TextureAspect::Depth;
        view = graphics.device->createTextureView(*target, targetView);
        assert(view);
        if (!fs) {
            desc.hasDepthStencilAttachment = true;
            desc.depthStencilAttachment.target = view->id();
            desc.depthStencilAttachment.format = format;
            desc.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
            desc.depthStencilAttachment.depthStoreOp = StoreOp::Store;
            return;
        }
        RenderPassColorAttachmentDesc attachment;
        attachment.target = view->id();
        attachment.format = TextureFormat::Rgba8Unorm;
        attachment.loadOp = LoadOp::Clear;
        attachment.storeOp = StoreOp::Store;
        attachment.clearColor = {0.2f, 0.4f, 0.6f, 0.8f};
        desc.colorAttachments.push_back(attachment);
    }

    auto prepare()
    {
        auto result = MetalRhiOrdinaryPreparedRenderPass::prepare(desc,
            graphics.provider,
            graphics.registry,
            graphics.budget(),
            graphics.quota,
            graphics.diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", graphics.diagnostic.c_str());
        assert(result);
        return result;
    }

    auto command(std::uint32_t owners)
    {
        const char* id = nullptr;
        auto storage = common::CommandRetentionStorage::prepare(
            {1u, true, owners, 0u, 0u, 0u, graphics.cpuBytes}, graphics.quota, id);
        assert(storage && !id);
        auto result = createMetalRhiNativeCommandPayload("BoundedOrdinaryGraphics",
            graphics.registry,
            &graphics.diagnostic,
            0u,
            0u,
            0u,
            std::move(storage));
        assert(result);
        return result;
    }

    auto readback()
    {
        auto output = graphics.device->createBuffer({"OrdinaryGraphicsPixels",
            4u * kTextureUploadRowPitchAlignment,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        assert(output);
        auto copyCommand = createMetalRhiNativeCommandPayload(
            "OrdinaryGraphicsReadback", graphics.registry, &graphics.diagnostic);
        assert(copyCommand);
        TextureToBufferCopyDesc copy;
        copy.sourceTexture = target->id();
        copy.destinationBuffer = output->id();
        copy.textureSubresources = {0u,
            1u,
            0u,
            1u,
            desc.hasDepthStencilAttachment ? TextureAspect::Depth : TextureAspect::Color};
        copy.bytesPerRow = kTextureUploadRowPitchAlignment;
        copy.rowsPerImage = 4u;
        copy.extent = {4u, 4u, 1u};
        MetalRhiNativeQueueStats queue;
        auto done = copyCommand->preparedCompletionState();
        assert(copyCommand->recordBlitPassBegin(graphics.diagnostic)
            && copyCommand->recordBlitTextureToBuffer(copy, graphics.diagnostic)
            && copyCommand->recordBlitPassEnd(graphics.diagnostic)
            && copyCommand->finish(graphics.diagnostic)
            && copyCommand->submit(queue, done, {}, 0u, {}, 0u, graphics.diagnostic));
        done->wait();
        std::array<std::uint8_t, 4u * kTextureUploadRowPitchAlignment> pixels{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*output),
            pixels.data(),
            pixels.size(),
            0u,
            pixels.size(),
            graphics.diagnostic));
        return pixels;
    }

    void verifyColor(const std::array<std::uint8_t, 4>& expected)
    {
        const auto pixels = readback();
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x)
                for (unsigned c = 0; c < 4; ++c) {
                    const auto actual = pixels[y * kTextureUploadRowPitchAlignment + x * 4u + c];
                    if (actual != expected[c])
                        std::fprintf(stderr,
                            "Pixel x=%u y=%u channel=%u actual=%u expected=%u\n",
                            x,
                            y,
                            c,
                            actual,
                            expected[c]);
                    assert(actual == expected[c]);
                }
    }

    void verifyDepth(float expected)
    {
        const auto pixels = readback();
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x) {
                float actual = 0.0f;
                std::memcpy(&actual,
                    pixels.data() + y * kTextureUploadRowPitchAlignment + x * sizeof(actual),
                    sizeof(actual));
                if (actual != expected)
                    std::fprintf(
                        stderr, "Depth x=%u y=%u actual=%f expected=%f\n", x, y, actual, expected);
                assert(actual == expected);
            }
    }

    void reject(const RenderPassDesc& invalid, const char* expected)
    {
        const auto before = graphics.quota->liveBytes();
        auto result = MetalRhiOrdinaryPreparedRenderPass::prepare(invalid,
            graphics.provider,
            graphics.registry,
            graphics.budget(),
            graphics.quota,
            graphics.diagnostic);
        if (result || graphics.diagnostic.find(expected) == graphics.diagnostic.npos)
            std::fprintf(stderr, "Expected %s: %s\n", expected, graphics.diagnostic.c_str());
        assert(!result && graphics.diagnostic.find(expected) != graphics.diagnostic.npos
            && graphics.quota->liveBytes() == before);
    }

    OrdinaryGraphicsFixture graphics;
    core::UniquePtr<GpuTexture> target;
    core::UniquePtr<GpuTextureView> view;
    RenderPassDesc desc;
};
} // namespace arp::test
