#pragma once

#include "original_graphics_pbr_material_data_fixture.h"
#include "original_graphics_terrain27_data_fixture.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/gpu_device.h"

#include <cstring>

namespace arp::test {
// Ordinary public RHI reference only. This fixture creates no bindless Root,
// snapshot, authorization or content proof. Native reads observe a completed
// transfer; they never authorize any later public work.
template <class Data> class OrdinaryMaterialReferenceFixture final {
    static constexpr bool terrain = std::is_same_v<Data, OriginalGraphicsTerrain27DataFixture>;
    static constexpr std::uint32_t textureCount = Data::sampledTextureCount;
    static constexpr std::uint32_t samplerCount = terrain ? 1u : 2u;
    static constexpr std::uint32_t pitch = rhi::kTextureUploadRowPitchAlignment;
    inline static typename Data::Numeric reference_{};
    inline static bool ready_ = false;

    template <class Result> static void check(const Result& result)
    {
        if (!result.success)
            std::fprintf(stderr, "Ordinary material reference: %s\n", result.diagnostics.c_str());
        assert(result.success);
    }

public:
    // Two authored mip rows plus one mip1 row, each padded to the public
    // upload pitch. Terrain's two array pages each contribute a second layer.
    static constexpr std::uint64_t uploadStagingBytes =
        std::uint64_t(pitch) * (2u + 1u) * (textureCount + (terrain ? 2u : 0u));

    static void compare(const typename Data::Numeric& actual)
    {
        assert(ready_);
        for (std::size_t i = 0u; i < actual.size(); ++i) {
            if (!std::isfinite(actual[i])
                || std::abs(actual[i] - reference_[i])
                    > Data::tolerance * (1.f + std::abs(reference_[i]))) {
                std::fprintf(stderr,
                    "Traditional parity lane%zu Root%.9g reference%.9g\n",
                    i,
                    actual[i],
                    reference_[i]);
                assert(false);
            }
        }
    }

    template <class ReadBuffer>
    static void execute(rhi::GpuDevice& device, const char* artifactPrefix, ReadBuffer read)
    {
        using namespace rhi;
        ready_ = false;
        const auto baseline = device.memoryStats().allocatedBytes;
        {
            std::array<DescriptorLayoutDesc, 2u> descriptors;
            descriptors[0].name = "OrdinaryMaterialInputs";
            descriptors[0].bindings = {{0u,
                                           DescriptorType::ReadOnlyStorageBuffer,
                                           ShaderStage::Vertex | ShaderStage::Fragment},
                {1u, DescriptorType::UniformBuffer, ShaderStage::Vertex | ShaderStage::Fragment}};
            descriptors[1].name = "OrdinaryMaterialResources";
            for (std::uint32_t i = 0u; i < textureCount; ++i) {
                DescriptorBindingDesc binding{
                    i, DescriptorType::SampledTexture, ShaderStage::Fragment};
                binding.textureViewDimensionConstrained = true;
                binding.textureViewDimension = terrain && i >= 25u
                    ? TextureViewDimension::Texture2DArray
                    : TextureViewDimension::Texture2D;
                descriptors[1].bindings.push_back(binding);
            }
            for (std::uint32_t i = 0u; i < samplerCount; ++i)
                descriptors[1].bindings.push_back(
                    {textureCount + i, DescriptorType::Sampler, ShaderStage::Fragment});
            descriptors[1].bindings.push_back(
                {Data::resultBinding, DescriptorType::StorageBuffer, ShaderStage::Fragment});
            PipelineLayoutDesc layoutDesc;
            for (std::uint32_t i = 0u; i < descriptors.size(); ++i) {
                layoutDesc.descriptorLayouts.push_back(descriptors[i]);
                layoutDesc.descriptorSets.push_back({i, descriptors[i].name});
            }
            auto layout = device.createPipelineLayout(layoutDesc);
            assert(layout);
            ShaderPackageDesc package;
            const core::pmr::String prefix(artifactPrefix);
            package.label = "IndependentImplicitMaterialReference";
            package.slangModulePath = prefix + ".slang";
            package.entrypoints = {{package.slangModulePath,
                                       "vertexMain",
                                       ShaderStage::Vertex,
                                       ShaderSourceLanguage::Slang},
                {package.slangModulePath,
                    "fragmentMain",
                    ShaderStage::Fragment,
                    ShaderSourceLanguage::Slang}};
            package.artifacts = {
                prefix + ".spv", prefix + ".metal", prefix + ".wgsl", prefix + ".reflection.yaml"};
            for (std::uint32_t set = 0u; set < descriptors.size(); ++set)
                for (const auto& binding : descriptors[set].bindings)
                    package.reflection.descriptorBindings.push_back(
                        makeShaderReflectionBinding(set, binding));
            assert(renderer::hydrateShaderPackageReflectionFromArtifact(package));
            check(validateShaderPackage(package));
            RenderPipelineDesc pipelineDesc;
            pipelineDesc.label = package.label;
            pipelineDesc.layout = layout.get();
            pipelineDesc.shaderPackage = &package;
            pipelineDesc.vertexShader = package.entrypoints[0];
            pipelineDesc.fragmentShader = package.entrypoints[1];
            pipelineDesc.colorFormat = TextureFormat::Rgba8Unorm;
            pipelineDesc.cullMode = CullMode::None;
            pipelineDesc.hasViewport = pipelineDesc.hasScissor = true;
            pipelineDesc.viewport = {0.f, 0.f, 2.f, 2.f, 0.f, 1.f};
            pipelineDesc.scissor = {0, 0, 2u, 2u};
            check(device.validateRenderPipeline(pipelineDesc));
            auto pipeline = device.createRenderPipeline(pipelineDesc);
            assert(pipeline);
            auto arena = device.createDescriptorArena(
                {"OrdinaryReferenceArena", 2u, textureCount + samplerCount + 3u});
            assert(arena);
            std::array<core::UniquePtr<DescriptorSet>, 2u> sets;
            for (std::uint32_t i = 0u; i < sets.size(); ++i) {
                sets[i] = arena->allocateDescriptorSet({descriptors[i].name, descriptors[i]});
                assert(sets[i]);
            }
            const auto primary = Data::ordinary0();
            const auto selection = Data::ordinary1();
            static_assert(sizeof(primary) == Data::ordinaryBytes[0]);
            static_assert(sizeof(selection) == Data::ordinaryBytes[1]);
            auto materials = device.createBuffer({"OrdinaryMaterials",
                sizeof(primary),
                ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            auto selected = device.createBuffer({"OrdinarySelection",
                sizeof(selection),
                ResourceUsage::UniformBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            auto numeric = device.createBuffer({"OrdinaryNumeric",
                sizeof(reference_),
                ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderWrite});
            auto numericRead = device.createBuffer({"OrdinaryNumericRead",
                sizeof(reference_),
                ResourceUsage::CopyDst,
                MemoryUsage::GpuToCpu,
                ResourceState::CopyDst});
            auto colorRead = device.createBuffer({"OrdinaryColorRead",
                pitch * 2u,
                ResourceUsage::CopyDst,
                MemoryUsage::GpuToCpu,
                ResourceState::CopyDst});
            assert(materials && selected && numeric && numericRead && colorRead);
            check(device.writeBuffer(*materials, &primary, sizeof(primary)));
            check(device.writeBuffer(*selected, &selection, sizeof(selection)));
            typename Data::Numeric poison;
            poison.fill(-999.f);
            check(device.writeBuffer(*numeric, poison.data(), sizeof(poison)));
            check(sets[0]->write({0u,
                DescriptorType::ReadOnlyStorageBuffer,
                materials->id(),
                DescriptorResourceKind::Buffer}));
            check(sets[0]->write({1u,
                DescriptorType::UniformBuffer,
                selected->id(),
                DescriptorResourceKind::Buffer}));
            DescriptorBindingWrite resultWrite{Data::resultBinding,
                DescriptorType::StorageBuffer,
                numeric->id(),
                DescriptorResourceKind::Buffer};
            resultWrite.resourceState = ResourceState::ShaderWrite;
            check(sets[1]->write(resultWrite));
            std::array<core::UniquePtr<GpuTexture>, textureCount> textures;
            std::array<core::UniquePtr<GpuTextureView>, textureCount> views;
            auto encoder = device.createCommandEncoder({"OrdinaryMaterialReference"});
            assert(encoder);
            for (std::uint32_t i = 0u; i < textureCount; ++i) {
                const auto layers = terrain && i >= 25u ? 2u : 1u;
                TextureDesc desc{"OrdinaryReferenceImage",
                    {2u, 2u, 1u},
                    TextureFormat::Rgba8Unorm,
                    TextureUsage::Sampled | TextureUsage::CopyDst,
                    MemoryUsage::GpuOnly,
                    ResourceState::Undefined};
                desc.mipLevelCount = 2u;
                desc.arrayLayerCount = layers;
                textures[i] = device.createTexture(desc);
                assert(textures[i]);
                const auto all = defaultTextureSubresourceRange(desc);
                check(encoder->transition(
                    *textures[i], all, ResourceState::Undefined, ResourceState::CopyDst));
                core::pmr::Vector<std::uint8_t> packed(20u * layers);
                Data::fillTexture(terrain && i >= 25u ? i - 25u : i, layers, packed);
                for (std::uint32_t mip = 0u; mip < 2u; ++mip) {
                    const auto side = 2u >> mip;
                    core::pmr::Vector<std::uint8_t> padded(pitch * side * layers);
                    for (std::uint32_t layer = 0u; layer < layers; ++layer)
                        for (std::uint32_t row = 0u; row < side; ++row)
                            std::memcpy(padded.data() + (layer * side + row) * pitch,
                                packed.data()
                                    + (mip ? 16u * layers + layer * 4u : layer * 16u + row * 8u),
                                side * 4u);
                    UploadTextureDesc upload;
                    upload.destinationTexture = textures[i]->id();
                    upload.extent = {side, side, 1u};
                    upload.format = desc.format;
                    upload.subresources = {mip, 1u, 0u, layers, TextureAspect::Color};
                    upload.bytesPerRow = pitch;
                    upload.rowsPerImage = side;
                    upload.pixelData = padded.data();
                    upload.pixelDataSize = padded.size();
                    upload.backendOwnedStaging = true;
                    upload.frameSerial = 1u;
                    upload.resourceGeneration = 1u;
                    auto transfer = encoder->beginTransferPass({"OrdinaryReferenceUpload"});
                    assert(transfer);
                    check(transfer->uploadTexture(upload));
                    check(transfer->end());
                }
                ResourceBarrierDesc sampled;
                sampled.resource = textures[i]->id();
                sampled.textureSubresources = all;
                sampled.before = ResourceState::CopyDst;
                sampled.after = ResourceState::ShaderRead;
                sampled.sourceStages = PipelineStage::Transfer;
                sampled.destinationStages = PipelineStage::FragmentShader;
                sampled.sourceAccess = ResourceAccess::TransferWrite;
                sampled.destinationAccess = ResourceAccess::ShaderRead;
                check(encoder->transition(*textures[i], sampled));
                TextureViewDesc view{"OrdinaryReferenceView", desc.format, TextureUsage::Sampled};
                view.dimension = layers == 2u ? TextureViewDimension::Texture2DArray
                                              : TextureViewDimension::Texture2D;
                view.mipLevelCount = 2u;
                view.arrayLayerCount = layers;
                views[i] = device.createTextureView(*textures[i], view);
                assert(views[i]);
                check(sets[1]->write({i,
                    DescriptorType::SampledTexture,
                    views[i]->id(),
                    DescriptorResourceKind::TextureView}));
            }
            std::array<core::UniquePtr<GpuSampler>, samplerCount> samplers;
            for (std::uint32_t i = 0u; i < samplers.size(); ++i) {
                SamplerDesc sampler;
                sampler.label = "OrdinaryReferenceSampler";
                sampler.minFilter = sampler.magFilter =
                    i ? SamplerFilter::Linear : SamplerFilter::Nearest;
                sampler.mipFilter = SamplerMipFilter::Linear;
                sampler.addressU = sampler.addressV = sampler.addressW =
                    SamplerAddressMode::ClampToEdge;
                sampler.compareOp = CompareOp::Never;
                sampler.maxAnisotropy = 1.f;
                samplers[i] = device.createSampler(sampler);
                assert(samplers[i]);
                check(sets[1]->write({textureCount + i,
                    DescriptorType::Sampler,
                    samplers[i]->id(),
                    DescriptorResourceKind::Sampler}));
            }
            auto target = device.createTexture({"OrdinaryReferenceColor",
                {2u, 2u, 1u},
                TextureFormat::Rgba8Unorm,
                TextureUsage::RenderAttachment | TextureUsage::CopySrc,
                MemoryUsage::GpuOnly,
                ResourceState::Undefined});
            assert(target);
            check(encoder->transition(
                *target, ResourceState::Undefined, ResourceState::ColorAttachment));
            RenderPassDesc render;
            RenderPassColorAttachmentDesc color;
            color.target = target->id();
            color.format = TextureFormat::Rgba8Unorm;
            color.loadOp = LoadOp::Clear;
            color.storeOp = StoreOp::Store;
            color.clearColor = {0.f, 0.f, 0.f, 1.f};
            render.colorAttachments.push_back(color);
            auto pass = encoder->beginRenderPass(render);
            assert(pass);
            check(pass->setPipeline(*pipeline));
            for (std::uint32_t i = 0u; i < sets.size(); ++i)
                check(pass->bindDescriptorSet(i, *sets[i]));
            check(pass->draw(3u, 1u, 0u, 0u));
            check(pass->end());
            ResourceBarrierDesc written;
            written.resource = numeric->id();
            written.before = ResourceState::ShaderWrite;
            written.after = ResourceState::CopySrc;
            written.sourceStages = PipelineStage::FragmentShader;
            written.destinationStages = PipelineStage::Transfer;
            written.sourceAccess = ResourceAccess::ShaderWrite;
            written.destinationAccess = ResourceAccess::TransferRead;
            check(encoder->transition(*numeric, written));
            check(encoder->transition(
                *target, ResourceState::ColorAttachment, ResourceState::CopySrc));
            auto transfer = encoder->beginTransferPass({"OrdinaryReferenceReadback"});
            assert(transfer);
            check(transfer->copyBufferToBuffer(
                {numeric->id(), numericRead->id(), 0u, 0u, sizeof(reference_)}));
            check(transfer->copyTextureToBuffer({target->id(),
                colorRead->id(),
                defaultTextureSubresourceRange(target->desc()),
                0u,
                pitch,
                2u,
                target->desc().extent}));
            check(transfer->end());
            auto commands = encoder->finish();
            auto done = device.createFence();
            assert(
                commands && done && device.queue().submit(*commands, done.get()).hasAcceptedWork());
            done->wait();
            assert(done->isSignaled());
            read(*numericRead, reference_.data(), sizeof(reference_));
            Data::verify(reference_);
            std::array<std::uint8_t, pitch * 2u> pixels{};
            read(*colorRead, pixels.data(), pixels.size());
            for (std::size_t y = 0u; y < 2u; ++y)
                for (std::size_t x = 0u; x < 2u; ++x)
                    for (std::size_t c = 0u; c < 4u; ++c)
                        assert(std::abs(int(pixels[y * pitch + x * 4u + c])
                                   - int(Data::expectedColor(x)[c]))
                            <= 1);
        }
        device.waitIdle();
        assert(device.memoryStats().allocatedBytes == baseline);
        ready_ = true;
        std::printf(
            "BINDLESS_NATIVE_TRADITIONAL_REFERENCE_OK %s lanes128 pixels4 implicit-samples real-fence retirement\n",
            Data::packageName);
    }
};
} // namespace arp::test
