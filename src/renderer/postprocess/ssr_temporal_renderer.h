#pragma once

#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/postprocess/ssr_renderer.h"

#include <array>

namespace arp::renderer {

// SsrTemporal/1: row vectors map canonical clip coordinates between cameras.
// Explicit dot-products in Slang consume these rows without matrix decoration.
struct SsrTemporalParameters {
    float currentToPreviousClip[16]{};
    float previousToCurrentClip[16]{};
    float settings[4]{0.0f, 0.92f, 0.01f, 0.995f};
};

static_assert(sizeof(SsrTemporalParameters) == 144u);

// Owns a bounded 3-texture ping-pong history. Recording never advances history;
// only acceptance of this exact graph pass publishes its camera and read slot.
class SsrTemporalRenderer final : public render_graph::PassSubmissionObserver {
public:
    explicit SsrTemporalRenderer(rhi::GpuDevice& device) : device_(device) {}

    RendererResult create(const core::pmr::String& path, std::uint32_t width, std::uint32_t height)
    {
        if (!rhi::isFeatureSupported(device_.capabilities(), rhi::EngineFeature::ComputeDispatch)
            || !rhi::isFeatureSupported(
                device_.capabilities(), rhi::EngineFeature::Rgba16FloatStorageTexture))
            return {false, "SSR-TEMPORAL-CAPABILITY-UNAVAILABLE"};
        rhi::DescriptorLayoutDesc set;
        set.name = "SsrTemporalSet";
        for (std::uint32_t i = 0; i < 12; ++i) {
            rhi::DescriptorBindingDesc binding;
            binding.binding = i;
            binding.stages = rhi::ShaderStage::Compute;
            binding.type = i == 2                ? rhi::DescriptorType::Sampler
                : (i == 3 || i == 10 || i == 11) ? rhi::DescriptorType::StorageTexture
                                                 : rhi::DescriptorType::SampledTexture;
            binding.depthTexture = i == 1;
            if (binding.type == rhi::DescriptorType::StorageTexture)
                binding.storageTextureFormat = rhi::TextureFormat::Rgba16Float;
            set.bindings.push_back(binding);
        }
        set.bindings.push_back(
            {12u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Compute});
        set.bindings.push_back(
            {13u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Compute});
        rhi::PipelineLayoutDesc layout;
        layout.descriptorLayouts.push_back(set);
        layout.descriptorSets.push_back({0u, set.name});
        layout.pushConstantRanges.push_back(
            {rhi::ShaderStage::Compute, 0u, sizeof(SsrPushConstants)});
        layout_ = device_.createPipelineLayout(layout);
        if (!layout_)
            return {false, "SSR-TEMPORAL-LAYOUT-FAILED"};
        package_.label = "SsrTemporal";
        package_.slangModulePath = path;
        package_.entrypoints.push_back({path,
            "ssrTemporalComputeMain",
            rhi::ShaderStage::Compute,
            rhi::ShaderSourceLanguage::Slang});
        const auto base = path.substr(0, path.find_last_of('.'));
        package_.artifacts.spirvPath = base + ".spv";
        package_.artifacts.mslPath = base + ".metal";
        package_.artifacts.wgslPath = base + ".wgsl";
        package_.artifacts.reflectionPath = base + ".reflection.yaml";
        package_.reflection.pushConstantByteCount = sizeof(SsrPushConstants);
        package_.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
        package_.reflection.pushConstantStages = "CS";
        for (const auto& binding : set.bindings)
            package_.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(0u, binding));
        hydrateShaderPackageReflectionFromArtifact(package_);
        auto validated = rhi::validateShaderPackage(package_);
        if (!validated.success)
            return {false, validated.diagnostics};
        rhi::ComputePipelineDesc pd;
        pd.label = "SsrTemporalPipeline";
        pd.layout = layout_.get();
        pd.shaderPackage = &package_;
        pd.computeShader = package_.entrypoints.front();
        pipeline_ = device_.createComputePipeline(pd);
        arena_ = device_.createDescriptorArena({"SsrTemporalArena", 2u, 28u});
        if (!pipeline_ || !arena_)
            return {false, "SSR-TEMPORAL-PIPELINE-FAILED"};
        sampler_ = device_.createSampler({"SsrTemporalLinear",
            rhi::SamplerFilter::Linear,
            rhi::SamplerFilter::Linear,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge});
        if (!sampler_)
            return {false, "SSR-TEMPORAL-SAMPLER-FAILED"};
        for (unsigned i = 0; i < 2; ++i) {
            sets_[i] = arena_->allocateDescriptorSet({"SsrTemporalSet", set});
            parameters_[i] = device_.createBuffer({"SsrTemporalParameters",
                sizeof(SsrTemporalParameters),
                rhi::ResourceUsage::UniformBuffer,
                rhi::MemoryUsage::CpuToGpu});
            if (!sets_[i] || !parameters_[i])
                return {false, "SSR-TEMPORAL-INPUT-CREATE-FAILED"};
        }
        return resize(width, height);
    }

    RendererResult resize(std::uint32_t width, std::uint32_t height)
    {
        if (width == width_ && height == height_)
            return {true, {}};
        invalidate();
        for (auto& slot : textures_)
            for (auto& texture : slot) {
                auto created = device_.createTexture({"SsrTemporalHistory",
                    {width, height, 1u},
                    rhi::TextureFormat::Rgba16Float,
                    rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled,
                    rhi::MemoryUsage::GpuOnly,
                    rhi::ResourceState::Undefined,
                    1u});
                if (!created)
                    return {false, "SSR-TEMPORAL-HISTORY-CREATE-FAILED"};
                texture = core::shareUnique(std::move(created));
            }
        width_ = width;
        height_ = height;
        return {true, {}};
    }

    void invalidate()
    {
        valid_ = false;
        prepared_ = false;
        ++generation_;
    }

    bool historyValid() const { return valid_; }

    unsigned readIndex() const { return read_; }

    const auto& texture(unsigned slot, unsigned component) const
    {
        return textures_[slot][component];
    }

    render_graph::PassSubmissionToken prepare(const core::Mat4& canonicalVp)
    {
        candidate_ = canonicalVp;
        prepared_ = true;
        ++generation_;
        return {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)), generation_};
    }

    const char* validateSubmission(render_graph::PassSubmissionToken token) const noexcept override
    {
        return prepared_ && token.ownerId == reinterpret_cast<std::uintptr_t>(this)
                && token.generation == generation_
            ? nullptr
            : "SSR-TEMPORAL-SUBMISSION-STALE";
    }

    void onSubmitted(render_graph::PassSubmissionToken token,
        const std::shared_ptr<const rhi::GpuFence>&) noexcept override
    {
        if (validateSubmission(token)) {
            valid_ = false;
            return;
        }
        previous_ = candidate_;
        read_ = 1u - read_;
        valid_ = true;
        prepared_ = false;
    }

    RendererResult record(render_graph::RenderContext& context,
        const std::array<const rhi::GpuTextureView*, 13>& views,
        const SsrPushConstants& pc,
        std::uint32_t frameIndex)
    {
        if (!prepared_)
            return {false, "SSR-TEMPORAL-NOT-PREPARED"};
        const auto slot = frameIndex % 2;
        SsrTemporalParameters parameters;
        const auto currentToPrevious = core::multiply(previous_, core::inverse(candidate_));
        const auto previousToCurrent = core::multiply(candidate_, core::inverse(previous_));
        std::copy_n(currentToPrevious.m.data(), 16u, parameters.currentToPreviousClip);
        std::copy_n(previousToCurrent.m.data(), 16u, parameters.previousToCurrentClip);
        parameters.settings[0] = valid_ ? 1.0f : 0.0f;
        auto written = device_.writeBuffer(*parameters_[slot], &parameters, sizeof(parameters));
        if (!written.success)
            return {false, written.diagnostics};
        for (unsigned binding = 0; binding < 14; ++binding) {
            rhi::DescriptorBindingWrite write;
            write.binding = binding;
            write.type = binding == 2 ? rhi::DescriptorType::Sampler
                : binding == 12       ? rhi::DescriptorType::UniformBuffer
                : (binding == 3 || binding == 10 || binding == 11)
                ? rhi::DescriptorType::StorageTexture
                : rhi::DescriptorType::SampledTexture;
            if (binding == 2) {
                write.resource = sampler_->id();
                write.resourceKind = rhi::DescriptorResourceKind::Sampler;
            } else if (binding == 12) {
                write.resource = parameters_[slot]->id();
                write.resourceKind = rhi::DescriptorResourceKind::Buffer;
            } else {
                const auto* view = views[binding < 12 ? binding : 12];
                if (!view)
                    return {false, "SSR-TEMPORAL-VIEW-MISSING"};
                write.resource = view->id();
                write.resourceKind = rhi::DescriptorResourceKind::TextureView;
            }
            const auto descriptorWrite = sets_[slot]->write(write);
            if (!descriptorWrite.success)
                return {false, descriptorWrite.diagnostics};
        }
        auto pass = context.commands().beginComputePass({"SsrTemporal"});
        if (!pass)
            return {false, "SSR-TEMPORAL-BEGIN-FAILED"};
        auto result = pass->setPipeline(*pipeline_);
        if (result.success)
            result = pass->bindDescriptorSet(0u, *sets_[slot]);
        if (result.success)
            result = pass->pushConstants(rhi::ShaderStage::Compute, 0u, sizeof(pc), &pc);
        if (result.success)
            result = pass->dispatch((width_ + 7u) / 8u, (height_ + 7u) / 8u, 1u);
        const auto ended = pass->end();
        if (!result.success)
            return {false, result.diagnostics};
        return {ended.success, ended.diagnostics};
    }

private:
    rhi::GpuDevice& device_;
    rhi::ShaderPackageDesc package_;
    core::UniquePtr<rhi::PipelineLayout> layout_;
    core::UniquePtr<rhi::ComputePipeline> pipeline_;
    core::UniquePtr<rhi::DescriptorArena> arena_;
    core::UniquePtr<rhi::GpuSampler> sampler_;
    std::array<core::UniquePtr<rhi::DescriptorSet>, 2> sets_;
    std::array<core::UniquePtr<rhi::GpuBuffer>, 2> parameters_;
    std::array<std::array<std::shared_ptr<rhi::GpuTexture>, 3>, 2> textures_;
    core::Mat4 previous_ = core::Mat4::identity(), candidate_ = core::Mat4::identity();
    std::uint32_t width_ = 0, height_ = 0;
    unsigned read_ = 0;
    bool valid_ = false, prepared_ = false;
    std::uint64_t generation_ = 0;
};
} // namespace arp::renderer
