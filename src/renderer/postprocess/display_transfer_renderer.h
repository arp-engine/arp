#pragma once

#include "renderer/raster_viewport.h"
#include "renderer/renderer_common.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <array>
#include <stdexcept>

namespace arp::renderer {

// SdrDisplayTransfer/1. The caller owns frame-slot completion and must retire
// all uses before resize/destruction. No implicit tone mapping or exposure.
struct alignas(16) DisplayTransferPushConstants {
    std::array<float, 2> inputUvScaleBias{1, 0};
    std::uint32_t encodeSrgb = 0;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(DisplayTransferPushConstants) == 16);
static_assert(offsetof(DisplayTransferPushConstants, encodeSrgb) == 8);

class DisplayTransferRenderer final {
public:
    void create(rhi::GpuDevice& device,
        core::StringView shaderPath,
        rhi::TextureFormat outputFormat,
        rhi::Extent2D extent,
        std::uint32_t slots,
        rhi::semantics::TextureUvYTransform uvTransform)
    {
        createImpl(device, shaderPath, outputFormat, extent, slots, uvTransform, nullptr);
    }

    void create(rhi::GpuDevice& device,
        core::StringView shaderPath,
        rhi::TextureFormat outputFormat,
        rhi::Extent2D extent,
        std::uint32_t slots,
        rhi::semantics::TextureUvYTransform uvTransform,
        const ShaderPackagePreparationDesc& preparation)
    {
        rhi::ShaderPackageDesc candidate;
        const auto prepared = detail::prepareGraphicsShaderPackage(candidate,
            "DisplayTransfer",
            core::pmr::String(shaderPath, &core::defaultCpuMemoryResource()),
            sizeof(DisplayTransferPushConstants),
            preparation,
            rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
        require(prepared.success, prepared.diagnostics);
        createImpl(device, shaderPath, outputFormat, extent, slots, uvTransform, &candidate);
    }

private:
    void createImpl(rhi::GpuDevice& device,
        core::StringView shaderPath,
        rhi::TextureFormat outputFormat,
        rhi::Extent2D extent,
        std::uint32_t slots,
        rhi::semantics::TextureUvYTransform uvTransform,
        const rhi::ShaderPackageDesc* preparedPackage)
    {
        require(!device_ && slots > 0 && slots <= 8, "DISPLAY-TRANSFER-CREATE-STATE");
        switch (outputFormat) {
        case rhi::TextureFormat::Bgra8Unorm:
        case rhi::TextureFormat::Rgba8Unorm:
            push_.encodeSrgb = 1;
            break;
        case rhi::TextureFormat::Rgba8UnormSrgb:
            push_.encodeSrgb = 0; // Attachment performs the only encoding.
            break;
        default:
            throw std::runtime_error("DISPLAY-TRANSFER-SDR-TARGET-REQUIRED");
        }
        device_ = &device;
        rhi::semantics::writeTextureUvYScaleBias(
            uvTransform, &push_.inputUvScaleBias[0], &push_.inputUvScaleBias[1]);
        inputLayout_.name = "DisplayTransferInput";
        inputLayout_.bindings.push_back(
            {0, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment});
        inputLayout_.bindings.push_back(
            {1, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment});
        rhi::PipelineLayoutDesc layout;
        layout.descriptorLayouts.push_back(inputLayout_);
        layout.descriptorSets.push_back({0, inputLayout_.name});
        layout.pushConstantRanges.push_back(
            {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0, sizeof(push_)});
        layout_ = device.createPipelineLayout(layout);
        require(bool(layout_), "DISPLAY-TRANSFER-LAYOUT");
        shader_ = preparedPackage
            ? *preparedPackage
            : detail::makeGraphicsShaderPackage("DisplayTransfer",
                  core::pmr::String(shaderPath, &core::defaultCpuMemoryResource()),
                  sizeof(push_));
        require(shader_.generatedReflectionHydrated, "DISPLAY-TRANSFER-REFLECTION");
        for (const auto& binding : inputLayout_.bindings)
            shader_.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(0, binding));
        auto desc = detail::makeRenderPipelineDesc(
            "DisplayTransfer", *layout_, shader_, outputFormat, extent);
        useDynamicRasterViewport(desc);
        desc.cullMode = rhi::CullMode::None;
        desc.frontFace = rhi::FrontFace::CounterClockwise;
        desc.topology = rhi::PrimitiveTopology::TriangleList;
        rhi::ColorTargetState color;
        color.format = outputFormat;
        color.blendEnabled = false;
        color.colorBlend = {rhi::BlendFactor::One, rhi::BlendFactor::Zero, rhi::BlendOp::Add};
        color.alphaBlend = color.colorBlend;
        color.writeMask = rhi::ColorWriteMask::All;
        color.resolveTargetRequired = false;
        desc.colorTargets.push_back(color);
        desc.depthStencil.depthTestEnabled = false;
        desc.depthStencil.depthWriteEnabled = false;
        desc.depthStencil.stencilEnabled = false;
        desc.depthStencil.depthBiasEnabled = false;
        desc.multisample.sampleCount = 1;
        desc.multisample.sampleMask = UINT64_MAX;
        desc.multisample.alphaToCoverageEnabled = false;
        const auto valid = device.validateRenderPipeline(desc);
        require(valid.success, valid.diagnostics);
        pipeline_ = device.createRenderPipeline(desc);
        require(bool(pipeline_), "DISPLAY-TRANSFER-PIPELINE");
        rhi::SamplerDesc sampler;
        sampler.label = "DisplayTransfer.NearestClamp";
        sampler.minFilter = sampler.magFilter = rhi::SamplerFilter::Nearest;
        sampler.mipFilter = rhi::SamplerMipFilter::None;
        sampler.addressU = sampler.addressV = sampler.addressW =
            rhi::SamplerAddressMode::ClampToEdge;
        sampler.compareOp = rhi::CompareOp::Never;
        sampler.maxAnisotropy = 0;
        sampler_ = device.createSampler(sampler);
        require(bool(sampler_), "DISPLAY-TRANSFER-SAMPLER");
        slots_.resize(slots);
        resize(extent);
    }

public:
    void resize(rhi::Extent2D extent)
    {
        require(device_ && extent.width && extent.height, "DISPLAY-TRANSFER-EXTENT");
        constexpr std::uint64_t kTargetBudgetBytes = 256u * 1024u * 1024u;
        require(std::uint64_t(extent.width) * extent.height
                <= kTargetBudgetBytes / (slots_.size() * 8u),
            "DISPLAY-TRANSFER-TARGET-BUDGET");
        // Build every replacement first. Failure leaves the current resources
        // intact, including the immutable descriptors of the previous extent.
        const auto count = static_cast<std::uint32_t>(slots_.size());
        auto arena = device_->createDescriptorArena({"DisplayTransfer", count, count * 2});
        require(bool(arena), "DISPLAY-TRANSFER-ARENA");
        core::pmr::Vector<Slot> replacements{&core::defaultCpuMemoryResource()};
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            Slot replacement;
            auto& target = replacement.target;
            target.image = core::shareUnique(device_->createTexture({"DisplayTransfer.LinearScene",
                {extent.width, extent.height, 1},
                rhi::TextureFormat::Rgba16Float,
                rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
                rhi::MemoryUsage::GpuOnly,
                rhi::ResourceState::Undefined,
                1}));
            require(bool(target.image), "DISPLAY-TRANSFER-SCENE-TEXTURE");
            target.view = device_->createTextureView(*target.image,
                {"DisplayTransfer.LinearScene",
                    rhi::TextureFormat::Rgba16Float,
                    rhi::TextureUsage::Sampled});
            require(bool(target.view), "DISPLAY-TRANSFER-SCENE-VIEW");
            replacement.descriptors =
                arena->allocateDescriptorSet({inputLayout_.name, inputLayout_});
            require(bool(replacement.descriptors), "DISPLAY-TRANSFER-DESCRIPTORS");
            auto& set = *replacement.descriptors;
            checked(set.write({0,
                rhi::DescriptorType::SampledTexture,
                target.view->id(),
                rhi::DescriptorResourceKind::TextureView}));
            checked(set.write({1,
                rhi::DescriptorType::Sampler,
                sampler_->id(),
                rhi::DescriptorResourceKind::Sampler}));
            replacements.push_back(std::move(replacement));
        }
        slots_.swap(replacements);
        replacements.clear(); // Old descriptors retire before their arena.
        arena_ = std::move(arena);
        extent_ = extent;
    }

    const core::SharedPtr<rhi::GpuTexture>& linearTarget(std::uint32_t slot) const
    {
        return slots_.at(slot).target.image;
    }

    void record(rhi::RenderPassEncoder& pass, std::uint32_t slot) const
    {
        checked(bindRasterPipeline(pass, *pipeline_, extent_));
        checked(pass.bindDescriptorSet(0, *slots_.at(slot).descriptors));
        checked(pass.pushConstants(
            rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0, sizeof(push_), &push_));
        checked(pass.draw(3, 1, 0, 0));
    }

    const DisplayTransferPushConstants& pushConstants() const { return push_; }

private:
    static void require(bool value, core::StringView diagnostic)
    {
        if (!value)
            throw std::runtime_error(
                core::pmr::String(diagnostic, &core::defaultCpuMemoryResource()).c_str());
    }

    template <class Result> static void checked(const Result& result)
    {
        require(result.success, result.diagnostics);
    }

    struct Target {
        core::SharedPtr<rhi::GpuTexture> image;
        core::UniquePtr<rhi::GpuTextureView> view;
    };

    struct Slot {
        Target target;
        core::UniquePtr<rhi::DescriptorSet> descriptors;
    };

    rhi::GpuDevice* device_ = nullptr;
    rhi::Extent2D extent_{};
    DisplayTransferPushConstants push_;
    rhi::DescriptorLayoutDesc inputLayout_;
    rhi::ShaderPackageDesc shader_;
    core::UniquePtr<rhi::PipelineLayout> layout_;
    core::UniquePtr<rhi::RenderPipeline> pipeline_;
    core::UniquePtr<rhi::DescriptorArena> arena_;
    core::UniquePtr<rhi::GpuSampler> sampler_;
    core::pmr::Vector<Slot> slots_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::renderer
