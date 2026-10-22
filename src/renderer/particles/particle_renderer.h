#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/materials/texture_upload.h"
#include "renderer/mesh/mesh_gpu_buffer.h"
#include "renderer/particles/particle_rhi_draw_commands.h"
#include "renderer/particles/particle_texture_resource.h"
#include "renderer/pipeline/custom_render_frame_workload.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_common.h"

#include <cmath>
#include <limits>
#include <memory>

namespace arp::renderer {

// Depth descriptor set index (texture set is at index 1, defined in particle_rhi_draw_commands.h)
inline constexpr std::uint32_t kParticleDepthSetIndex = 2u;

struct MeshParticlePushConstants {
    float viewProjection[16]{};
    std::uint32_t visualFamily = static_cast<std::uint32_t>(ParticleVisualFamily::MeshParticle);
    std::uint32_t pad0 = 0u;
    std::uint32_t pad1 = 0u;
    std::uint32_t pad2 = 0u;
};

struct MeshParticleVertex {
    float worldPosition[3]{};
    float color[4]{};
    float uv[2]{};
};

namespace detail {

inline rhi::PipelineLayoutDesc particlePipelineLayoutDesc(const bool includeDepthSet = true,
    const std::uint32_t pushConstantBytes = sizeof(ParticlePushConstants))
{
    rhi::PipelineLayoutDesc desc;
    auto depthTexture = rhi::DescriptorBindingDesc{
        0, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment};
    depthTexture.depthTexture = true;
    auto depthSampler =
        rhi::DescriptorBindingDesc{1, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment};
    // WebGPU depth textures may only be sampled through a non-filtering
    // sampler. The particle shader uses explicit LOD for soft-particle depth.
    depthSampler.sampledTextureFilterable = false;
    desc.descriptorLayouts.push_back({
        "ParticleTextureSet",
        {
            {0, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {1, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment},
        },
    });
    if (includeDepthSet) {
        desc.descriptorLayouts.push_back({
            "ParticleDepthSet",
            {
                depthTexture,
                depthSampler,
            },
        });
    }
    desc.descriptorSets.push_back({kParticleTextureSetIndex, "ParticleTextureSet"});
    if (includeDepthSet) {
        desc.descriptorSets.push_back({kParticleDepthSetIndex, "ParticleDepthSet"});
    }
    desc.pushConstantRanges.push_back({
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        pushConstantBytes,
    });
    return desc;
}

inline rhi::PipelineLayoutDesc meshParticlePipelineLayoutDesc()
{
    rhi::PipelineLayoutDesc desc;
    desc.pushConstantRanges.push_back({
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0,
        sizeof(MeshParticlePushConstants),
    });
    return desc;
}

inline void enableParticleAlphaBlend(rhi::RenderPipelineDesc& desc)
{
    if (desc.colorTargets.empty()) {
        desc.colorTargets.push_back({
            desc.colorFormat,
            false,
            {},
            {},
            rhi::ColorWriteMask::All,
            false,
        });
    }
    auto& target = desc.colorTargets[0];
    target.blendEnabled = true;
    target.colorBlend = {
        rhi::BlendFactor::One,
        rhi::BlendFactor::OneMinusSrcAlpha,
        rhi::BlendOp::Add,
    };
    target.alphaBlend = {
        rhi::BlendFactor::One,
        rhi::BlendFactor::OneMinusSrcAlpha,
        rhi::BlendOp::Add,
    };
}

inline void enableParticleAdditiveBlend(rhi::RenderPipelineDesc& desc)
{
    if (desc.colorTargets.empty()) {
        desc.colorTargets.push_back({
            desc.colorFormat,
            false,
            {},
            {},
            rhi::ColorWriteMask::All,
            false,
        });
    }
    auto& target = desc.colorTargets[0];
    target.blendEnabled = true;
    target.colorBlend = {
        rhi::BlendFactor::SrcAlpha,
        rhi::BlendFactor::One,
        rhi::BlendOp::Add,
    };
    target.alphaBlend = {
        rhi::BlendFactor::SrcAlpha,
        rhi::BlendFactor::One,
        rhi::BlendOp::Add,
    };
}

inline void applyParticlePipelineDefaults(rhi::RenderPipelineDesc& desc)
{
    desc.topology = rhi::PrimitiveTopology::TriangleList;
    desc.cullMode = rhi::CullMode::None;
    desc.frontFace = rhi::FrontFace::CounterClockwise;
    desc.depthStencil.depthTestEnabled = false;
    desc.depthStencil.depthWriteEnabled = false;
}

inline RendererResult uploadParticleTextureRgba8(rhi::GpuDevice& device,
    rhi::GpuTexture& texture,
    const std::uint8_t* rgba8,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t bytesPerRow,
    std::uint32_t rowsPerImage)
{
    return uploadTextureRgba8(device, texture, rgba8, width, height, bytesPerRow, rowsPerImage);
}

// The particle depth binding is required even when the caller does not supply
// a scene depth texture.  A newly allocated depth image is Undefined on native
// backends, so publish a real far-depth value through the normal RHI render
// pass path before exposing it as a sampled texture.
inline RendererResult initializeParticleDepthFallback(
    rhi::GpuDevice& device, rhi::GpuTexture& texture)
{
    auto encoder = device.createCommandEncoder({texture.desc().label + "Clear"});
    if (!encoder) {
        return RendererResult{false, "particle depth fallback encoder creation failed"};
    }

    rhi::TextureSubresourceRange range;
    range.aspect = rhi::TextureAspect::Depth;
    const auto toDepth = encoder->transition(
        texture, range, rhi::ResourceState::Undefined, rhi::ResourceState::DepthAttachment);
    if (!toDepth.success) {
        return RendererResult{false, toDepth.diagnostics};
    }

    rhi::RenderPassDesc passDesc;
    passDesc.label = texture.desc().label + "Clear";
    passDesc.hasDepthStencilAttachment = true;
    passDesc.depthStencilAttachment = rhi::RenderPassDepthStencilAttachmentDesc{
        texture.desc().label,
        texture.id(),
        texture.desc().format,
        rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::DontCare,
    };
    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass) {
        return RendererResult{false, encoder->lastValidation().diagnostics};
    }
    const auto passEnded = pass->end();
    if (!passEnded.success) {
        return RendererResult{false, passEnded.diagnostics};
    }

    const auto toShader = encoder->transition(
        texture, range, rhi::ResourceState::DepthAttachment, rhi::ResourceState::ShaderRead);
    if (!toShader.success) {
        return RendererResult{false, toShader.diagnostics};
    }
    auto commands = encoder->finish();
    if (!commands) {
        return RendererResult{false, encoder->lastValidation().diagnostics};
    }
    const auto submitted = device.queue().submit(*commands);
    return RendererResult{submitted.success, submitted.diagnostics};
}

} // namespace detail

class ParticleRenderer final {
public:
    // kParticleTextureSetIndex is defined in particle_rhi_draw_commands.h as 1u
    static constexpr std::uint32_t kParticleDescriptorSetSlots = 2u;

    // kParticleDepthSetIndex is defined at namespace scope as 2u

    struct ParticleTextureBinding {
        using ExplicitTextureUpload =
            arp::renderer::CustomParticleGraphPayload::TextureBindingRequest::ExplicitTextureUpload;

        rhi::GpuTexture* texture = nullptr;
        const particles::ParticleTextureResourceData* textureResource = nullptr;
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
        ExplicitTextureUpload explicitUpload;

        bool valid() const noexcept
        {
            return (texture != nullptr && texture->id().isValid()) || textureResource != nullptr
                || explicitUpload.valid();
        }
    };

    explicit ParticleRenderer(rhi::GpuDevice& device) : device_(device) {}

    RendererResult create(const ParticleRendererDesc& desc)
    {
        reset();
        maxParticles_ = desc.maxParticles;

        const auto validated =
            detail::validateRendererCreateDesc(desc.shaderPath, desc.colorFormat, desc.extent);
        if (!validated.success) {
            return validated;
        }
        if (desc.maxParticles == 0) {
            return RendererResult{false, "particle budget is empty"};
        }
        if (desc.maxParticles > std::numeric_limits<std::uint32_t>::max() / 6u) {
            return RendererResult{false, "particle budget exceeds renderer capacity"};
        }

        auto layoutDesc = detail::particlePipelineLayoutDesc();
        pipelineLayout_ = device_.createPipelineLayout(layoutDesc);
        if (!pipelineLayout_) {
            return RendererResult{false, "particle RHI pipeline layout creation failed"};
        }

        shaderPackage_ = detail::makeGraphicsShaderPackage(
            "ParticleRhi", desc.shaderPath, sizeof(ParticlePushConstants));
        const auto reflectionHydration =
            detail::validateGeneratedReflectionHydration(shaderPackage_);
        if (!reflectionHydration.success) {
            return reflectionHydration;
        }
        for (const auto& binding : layoutDesc.descriptorLayouts[0].bindings) {
            shaderPackage_.reflection.descriptorBindings.push_back(
                rhi::makeShaderReflectionBinding(kParticleTextureSetIndex, binding));
        }
        if (layoutDesc.descriptorLayouts.size() > 1u) {
            for (const auto& binding : layoutDesc.descriptorLayouts[1].bindings) {
                shaderPackage_.reflection.descriptorBindings.push_back(
                    rhi::makeShaderReflectionBinding(kParticleDepthSetIndex, binding));
            }
        }
        const auto shaderValidation = rhi::validateShaderPackage(shaderPackage_);
        if (!shaderValidation.success) {
            return RendererResult{false, shaderValidation.diagnostics};
        }

        // Alpha blend pipeline (default)
        {
            auto pipelineDesc = detail::makeRenderPipelineDesc("ParticleRhiPipeline",
                *pipelineLayout_,
                shaderPackage_,
                desc.colorFormat,
                desc.extent);
            detail::applyParticlePipelineDefaults(pipelineDesc);
            detail::enableParticleAlphaBlend(pipelineDesc);
            rhi::VertexBufferLayoutDesc particleVertexLayout;
            particleVertexLayout.slot = 0;
            particleVertexLayout.strideBytes = sizeof(ParticleVertex);
            particleVertexLayout.inputRate = rhi::VertexInputRate::Vertex;
            particleVertexLayout.attributes.push_back(
                {0, offsetof(ParticleVertex, worldPosition), rhi::VertexFormat::Float32x3});
            particleVertexLayout.attributes.push_back(
                {1, offsetof(ParticleVertex, corner), rhi::VertexFormat::Float32x2});
            particleVertexLayout.attributes.push_back(
                {2, offsetof(ParticleVertex, color), rhi::VertexFormat::Float32x4});
            particleVertexLayout.attributes.push_back(
                {3, offsetof(ParticleVertex, size), rhi::VertexFormat::Float32x4});
            pipelineDesc.vertexBuffers.push_back(particleVertexLayout);
            useDynamicRasterViewport(pipelineDesc);
            pipelineAlpha_ = device_.createRenderPipeline(pipelineDesc);
            if (!pipelineAlpha_) {
                return RendererResult{false, "particle RHI alpha render pipeline creation failed"};
            }
        }

        // Additive blend pipeline
        {
            auto pipelineDesc = detail::makeRenderPipelineDesc("ParticleRhiPipelineAdditive",
                *pipelineLayout_,
                shaderPackage_,
                desc.colorFormat,
                desc.extent);
            detail::applyParticlePipelineDefaults(pipelineDesc);
            detail::enableParticleAdditiveBlend(pipelineDesc);
            rhi::VertexBufferLayoutDesc particleVertexLayoutAdditive;
            particleVertexLayoutAdditive.slot = 0;
            particleVertexLayoutAdditive.strideBytes = sizeof(ParticleVertex);
            particleVertexLayoutAdditive.inputRate = rhi::VertexInputRate::Vertex;
            particleVertexLayoutAdditive.attributes.push_back(
                {0, offsetof(ParticleVertex, worldPosition), rhi::VertexFormat::Float32x3});
            particleVertexLayoutAdditive.attributes.push_back(
                {1, offsetof(ParticleVertex, corner), rhi::VertexFormat::Float32x2});
            particleVertexLayoutAdditive.attributes.push_back(
                {2, offsetof(ParticleVertex, color), rhi::VertexFormat::Float32x4});
            particleVertexLayoutAdditive.attributes.push_back(
                {3, offsetof(ParticleVertex, size), rhi::VertexFormat::Float32x4});
            pipelineDesc.vertexBuffers.push_back(particleVertexLayoutAdditive);
            useDynamicRasterViewport(pipelineDesc);
            pipelineAdditive_ = device_.createRenderPipeline(pipelineDesc);
            if (!pipelineAdditive_) {
                return RendererResult{
                    false, "particle RHI additive render pipeline creation failed"};
            }
        }

        // Arena must accommodate double-buffered ParticleTextureSet + single ParticleDepthSet
        constexpr std::uint32_t kParticleTextureSetCount = kParticleDescriptorSetSlots;
        constexpr std::uint32_t kParticleDepthSetCount = 1u;
        constexpr std::uint32_t kArenaMaxSets = kParticleTextureSetCount + kParticleDepthSetCount;
        constexpr std::uint32_t kArenaMaxDescriptors = kArenaMaxSets * 2u;
        descriptorArena_ = device_.createDescriptorArena({
            "ParticleDescriptorArena",
            kArenaMaxSets,
            kArenaMaxDescriptors,
        });
        if (!descriptorArena_) {
            return RendererResult{false, "particle RHI descriptor arena creation failed"};
        }
        for (std::uint32_t slot = 0; slot < kParticleDescriptorSetSlots; ++slot) {
            descriptorSets_[slot] = descriptorArena_->allocateDescriptorSet({
                "ParticleTextureSet",
                layoutDesc.descriptorLayouts[0],
            });
            if (!descriptorSets_[slot]) {
                return RendererResult{false, "particle RHI descriptor set allocation failed"};
            }
        }
        sampler_ = device_.createSampler({
            "ParticleLinearClamp",
            rhi::SamplerFilter::Linear,
            rhi::SamplerFilter::Linear,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
        });
        if (!sampler_) {
            return RendererResult{false, "particle RHI sampler creation failed"};
        }
        depthSampler_ = device_.createSampler({
            "ParticleDepthNearestClamp",
            rhi::SamplerFilter::Nearest,
            rhi::SamplerFilter::Nearest,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::SamplerAddressMode::ClampToEdge,
            rhi::CompareOp::Never,
            rhi::SamplerMipFilter::Nearest,
        });
        if (!depthSampler_) {
            return RendererResult{false, "particle RHI depth sampler creation failed"};
        }
        fallbackTexture_ = device_.createTexture({
            "ParticleFallbackWhiteTexture",
            {1u, 1u, 1u},
            rhi::TextureFormat::Rgba8Unorm,
            rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined,
        });
        if (!fallbackTexture_) {
            return RendererResult{false, "particle RHI fallback texture creation failed"};
        }
        static constexpr std::array<std::uint8_t, 256> kFallbackWhitePixels = [] {
            std::array<std::uint8_t, 256> pixels{};
            pixels[0] = 255u;
            pixels[1] = 255u;
            pixels[2] = 255u;
            pixels[3] = 255u;
            return pixels;
        }();
        const auto fallbackUploaded = detail::uploadParticleTextureRgba8(
            device_, *fallbackTexture_, kFallbackWhitePixels.data(), 1u, 1u, 256u, 1u);
        if (!fallbackUploaded.success) {
            return RendererResult{
                false,
                "particle RHI fallback texture upload failed: " + fallbackUploaded.diagnostics,
            };
        }
        // Write sampler into all descriptor sets once (it never changes)
        for (std::uint32_t slot = 0; slot < kParticleDescriptorSetSlots; ++slot) {
            const auto samplerWritten = descriptorSets_[slot]->write({
                1,
                rhi::DescriptorType::Sampler,
                sampler_->id(),
                rhi::DescriptorResourceKind::Sampler,
            });
            if (!samplerWritten.success) {
                return RendererResult{false, samplerWritten.diagnostics};
            }
        }
        for (std::uint32_t slot = 0; slot < kParticleDescriptorSetSlots; ++slot) {
            const auto fallbackReady = ensureTextureDescriptor(nullptr, slot, false);
            if (!fallbackReady.success) {
                return fallbackReady;
            }
        }

        // Allocate depth descriptor set (single slot, not double-buffered)
        depthDescriptorSet_ = descriptorArena_->allocateDescriptorSet({
            "ParticleDepthSet",
            layoutDesc.descriptorLayouts[1],
        });
        if (!depthDescriptorSet_) {
            return RendererResult{false, "particle RHI depth descriptor set allocation failed"};
        }
        depthFallbackTexture_ = device_.createTexture({
            "ParticleDepthFallbackTexture",
            {1u, 1u, 1u},
            rhi::TextureFormat::D32Float,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined,
        });
        if (!depthFallbackTexture_) {
            return RendererResult{false, "particle RHI depth fallback texture creation failed"};
        }
        const auto depthFallbackInitialized =
            detail::initializeParticleDepthFallback(device_, *depthFallbackTexture_);
        if (!depthFallbackInitialized.success) {
            return RendererResult{
                false,
                "particle RHI depth fallback initialization failed: "
                    + depthFallbackInitialized.diagnostics,
            };
        }
        auto depthFallbackView = device_.createTextureView(*depthFallbackTexture_,
            {
                "ParticleDepthFallbackView",
                depthFallbackTexture_->desc().format,
                rhi::TextureUsage::Sampled,
                rhi::TextureViewDimension::Texture2D,
                0u,
                1u,
                0u,
                1u,
                rhi::TextureAspect::Depth,
            });
        if (!depthFallbackView) {
            return RendererResult{
                false, "particle RHI depth fallback texture view creation failed"};
        }
        {
            const auto depthTexWritten = depthDescriptorSet_->write({
                0,
                rhi::DescriptorType::SampledTexture,
                depthFallbackView->id(),
                rhi::DescriptorResourceKind::TextureView,
            });
            if (!depthTexWritten.success) {
                return RendererResult{false, depthTexWritten.diagnostics};
            }
        }
        {
            const auto depthSamplerWritten = depthDescriptorSet_->write({
                1,
                rhi::DescriptorType::Sampler,
                depthSampler_->id(),
                rhi::DescriptorResourceKind::Sampler,
            });
            if (!depthSamplerWritten.success) {
                return RendererResult{false, depthSamplerWritten.diagnostics};
            }
        }
        depthFallbackView_ = std::move(depthFallbackView);

        maxUploadBytes_ =
            static_cast<std::uint64_t>(desc.maxParticles) * 6u * sizeof(ParticleVertex);
        vertexBuffer_ = device_.createBuffer({
            "ParticleVertices",
            maxUploadBytes_,
            rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined,
        });
        uploadBuffer_ = device_.createBuffer({
            "ParticleUploadSource",
            maxUploadBytes_,
            rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::Undefined,
        });
        if (!vertexBuffer_ || !uploadBuffer_) {
            return RendererResult{false, "particle RHI vertex upload buffer creation failed"};
        }

        // Trail/Beam shader package and pipeline
        {
            auto layoutDesc =
                detail::particlePipelineLayoutDesc(false, sizeof(TrailBeamPushConstants));
            pipelineLayoutTrailBeam_ = device_.createPipelineLayout(layoutDesc);
            if (!pipelineLayoutTrailBeam_) {
                return RendererResult{false, "trail/beam RHI pipeline layout creation failed"};
            }

            const auto& tbPath =
                desc.trailBeamShaderPath.empty() ? desc.shaderPath : desc.trailBeamShaderPath;
            shaderPackageTrailBeam_ = detail::makeGraphicsShaderPackage(
                "TrailBeamRhi", tbPath, sizeof(TrailBeamPushConstants));
            const auto trailReflectionHydration =
                detail::validateGeneratedReflectionHydration(shaderPackageTrailBeam_);
            if (!trailReflectionHydration.success) {
                return trailReflectionHydration;
            }
            shaderPackageTrailBeam_.reflection.descriptorBindings.push_back({
                kParticleTextureSetIndex,
                0,
                rhi::DescriptorType::SampledTexture,
                rhi::ShaderStage::Fragment,
            });
            shaderPackageTrailBeam_.reflection.descriptorBindings.push_back({
                kParticleTextureSetIndex,
                1,
                rhi::DescriptorType::Sampler,
                rhi::ShaderStage::Fragment,
            });
            const auto shaderValidation = rhi::validateShaderPackage(shaderPackageTrailBeam_);
            if (!shaderValidation.success) {
                return RendererResult{false, shaderValidation.diagnostics};
            }

            auto pipelineDesc = detail::makeRenderPipelineDesc("TrailBeamPipeline",
                *pipelineLayoutTrailBeam_,
                shaderPackageTrailBeam_,
                desc.colorFormat,
                desc.extent);
            detail::applyParticlePipelineDefaults(pipelineDesc);
            detail::enableParticleAlphaBlend(pipelineDesc);
            rhi::VertexBufferLayoutDesc trailBeamVertexLayout;
            trailBeamVertexLayout.slot = 0;
            trailBeamVertexLayout.strideBytes = sizeof(TrailBeamVertex);
            trailBeamVertexLayout.inputRate = rhi::VertexInputRate::Vertex;
            trailBeamVertexLayout.attributes.push_back(
                {0, offsetof(TrailBeamVertex, worldPosition), rhi::VertexFormat::Float32x3});
            trailBeamVertexLayout.attributes.push_back(
                {1, offsetof(TrailBeamVertex, corner), rhi::VertexFormat::Float32x2});
            trailBeamVertexLayout.attributes.push_back(
                {2, offsetof(TrailBeamVertex, size), rhi::VertexFormat::Float32x1});
            trailBeamVertexLayout.attributes.push_back(
                {3, offsetof(TrailBeamVertex, trailIndex), rhi::VertexFormat::Float32x1});
            trailBeamVertexLayout.attributes.push_back(
                {4, offsetof(TrailBeamVertex, normalizedAge), rhi::VertexFormat::Float32x1});
            trailBeamVertexLayout.attributes.push_back(
                {5, offsetof(TrailBeamVertex, color), rhi::VertexFormat::Float32x4});
            pipelineDesc.vertexBuffers.push_back(trailBeamVertexLayout);
            useDynamicRasterViewport(pipelineDesc);
            pipelineTrailBeam_ = device_.createRenderPipeline(pipelineDesc);
            if (!pipelineTrailBeam_) {
                return RendererResult{false, "trail/beam RHI render pipeline creation failed"};
            }

            // Trail/Beam vertex budget: assume up to maxParticles segments × 6 verts each
            maxUploadBytesTrailBeam_ =
                static_cast<std::uint64_t>(desc.maxParticles) * 6u * sizeof(TrailBeamVertex);
            vertexBufferTrailBeam_ = device_.createBuffer({
                "TrailBeamVertices",
                maxUploadBytesTrailBeam_,
                rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
                rhi::MemoryUsage::GpuOnly,
                rhi::ResourceState::Undefined,
            });
            uploadBufferTrailBeam_ = device_.createBuffer({
                "TrailBeamUploadSource",
                maxUploadBytesTrailBeam_,
                rhi::ResourceUsage::CopySrc,
                rhi::MemoryUsage::CpuToGpu,
                rhi::ResourceState::Undefined,
            });
            if (!vertexBufferTrailBeam_ || !uploadBufferTrailBeam_) {
                return RendererResult{false, "trail/beam RHI vertex upload buffer creation failed"};
            }
        }

        // Mesh particle shader package and prototype renderer path
        {
            auto markMeshParticleUnavailable = [this](const char* diagnostics) {
                pipelineMeshParticle_.reset();
                pipelineLayoutMeshParticle_.reset();
                vertexBufferMeshParticle_.reset();
                indexBufferMeshParticle_.reset();
                uploadBufferMeshParticleVertices_.reset();
                uploadBufferMeshParticleIndices_.reset();
                meshParticleUnavailableDiagnostics_ = diagnostics == nullptr
                    ? "mesh particle RHI renderer is not ready"
                    : diagnostics;
            };

            pipelineLayoutMeshParticle_ =
                device_.createPipelineLayout(detail::meshParticlePipelineLayoutDesc());
            if (!pipelineLayoutMeshParticle_) {
                markMeshParticleUnavailable("mesh particle RHI pipeline layout creation failed");
            } else {
                const auto& meshShaderPath = desc.meshParticleShaderPath.empty()
                    ? core::pmr::String("mesh_particle.slang", &core::defaultCpuMemoryResource())
                    : desc.meshParticleShaderPath;
                shaderPackageMeshParticle_ = detail::makeGraphicsShaderPackage(
                    "MeshParticleRhi", meshShaderPath, sizeof(MeshParticlePushConstants));
                const auto meshReflectionHydration =
                    detail::validateGeneratedReflectionHydration(shaderPackageMeshParticle_);
                if (!meshReflectionHydration.success) {
                    markMeshParticleUnavailable(meshReflectionHydration.diagnostics.c_str());
                }
                const auto shaderValidation =
                    rhi::validateShaderPackage(shaderPackageMeshParticle_);
                if (!meshReflectionHydration.success) {
                    // The hydration diagnostic is already the authoritative
                    // failure for this optional renderer path.
                } else if (!shaderValidation.success) {
                    markMeshParticleUnavailable(shaderValidation.diagnostics.c_str());
                } else {
                    auto pipelineDesc = detail::makeRenderPipelineDesc("MeshParticlePipeline",
                        *pipelineLayoutMeshParticle_,
                        shaderPackageMeshParticle_,
                        desc.colorFormat,
                        desc.extent);
                    detail::applyParticlePipelineDefaults(pipelineDesc);
                    detail::enableParticleAlphaBlend(pipelineDesc);
                    rhi::VertexBufferLayoutDesc meshParticleVertexLayout;
                    meshParticleVertexLayout.slot = 0;
                    meshParticleVertexLayout.strideBytes = sizeof(MeshParticleVertex);
                    meshParticleVertexLayout.inputRate = rhi::VertexInputRate::Vertex;
                    meshParticleVertexLayout.attributes.push_back({0,
                        offsetof(MeshParticleVertex, worldPosition),
                        rhi::VertexFormat::Float32x3});
                    meshParticleVertexLayout.attributes.push_back(
                        {1, offsetof(MeshParticleVertex, color), rhi::VertexFormat::Float32x4});
                    meshParticleVertexLayout.attributes.push_back(
                        {2, offsetof(MeshParticleVertex, uv), rhi::VertexFormat::Float32x2});
                    pipelineDesc.vertexBuffers.push_back(meshParticleVertexLayout);
                    useDynamicRasterViewport(pipelineDesc);
                    pipelineMeshParticle_ = device_.createRenderPipeline(pipelineDesc);
                    if (!pipelineMeshParticle_) {
                        markMeshParticleUnavailable(
                            "mesh particle RHI render pipeline creation failed");
                    } else {
                        fallbackMeshParticleAsset_ = assets::MeshAsset::debugTriangle();
                        maxUploadBytesMeshParticleVertices_ =
                            static_cast<std::uint64_t>(desc.maxParticles)
                            * fallbackMeshParticleAsset_.vertices.size()
                            * sizeof(MeshParticleVertex);
                        maxUploadBytesMeshParticleIndices_ =
                            static_cast<std::uint64_t>(desc.maxParticles)
                            * fallbackMeshParticleAsset_.indices.size() * sizeof(std::uint32_t);
                        vertexBufferMeshParticle_ = device_.createBuffer({
                            "MeshParticleVertices",
                            maxUploadBytesMeshParticleVertices_,
                            rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
                            rhi::MemoryUsage::GpuOnly,
                            rhi::ResourceState::Undefined,
                        });
                        indexBufferMeshParticle_ = device_.createBuffer({
                            "MeshParticleIndices",
                            maxUploadBytesMeshParticleIndices_,
                            rhi::ResourceUsage::IndexBuffer | rhi::ResourceUsage::CopyDst,
                            rhi::MemoryUsage::GpuOnly,
                            rhi::ResourceState::Undefined,
                        });
                        uploadBufferMeshParticleVertices_ = device_.createBuffer({
                            "MeshParticleVertexUploadSource",
                            maxUploadBytesMeshParticleVertices_,
                            rhi::ResourceUsage::CopySrc,
                            rhi::MemoryUsage::CpuToGpu,
                            rhi::ResourceState::Undefined,
                        });
                        uploadBufferMeshParticleIndices_ = device_.createBuffer({
                            "MeshParticleIndexUploadSource",
                            maxUploadBytesMeshParticleIndices_,
                            rhi::ResourceUsage::CopySrc,
                            rhi::MemoryUsage::CpuToGpu,
                            rhi::ResourceState::Undefined,
                        });
                        if (!vertexBufferMeshParticle_ || !indexBufferMeshParticle_
                            || !uploadBufferMeshParticleVertices_
                            || !uploadBufferMeshParticleIndices_) {
                            markMeshParticleUnavailable(
                                "mesh particle RHI upload buffer creation failed");
                        }
                    }
                }
            }
        }

        return RendererResult{true, ""};
    }

    /// Pre-upload a particle texture and bind it to the descriptor sets for
    /// both slots (0 and 1). Call ONCE after the texture data is available and
    /// before any frame that draws textured particles. This avoids submitting
    /// command buffers inside FrameGraph execute callbacks.
    RendererResult prepareTexture(const ParticleTextureBinding* binding)
    {
        if (!binding || !binding->explicitUpload.valid()) {
            return RendererResult{true, ""};
        }
        for (std::uint32_t slot = 0u; slot < kParticleDescriptorSetSlots; ++slot) {
            const auto ready = ensureTextureDescriptor(binding, slot, true);
            if (!ready.success)
                return ready;
        }
        return RendererResult{true, ""};
    }

    RendererResult drawFrame(const RenderFrameContext& frame,
        const core::pmr::Vector<ParticleInstance>& particles,
        const SceneConstants& scene,
        bool sortBackToFront = true,
        const ParticleTextureBinding* textureBinding = nullptr)
    {
        if (particles.empty()) {
            return RendererResult{true, ""};
        }
        auto encoder = device_.createCommandEncoder({"ParticleRenderer"});
        if (!encoder) {
            return RendererResult{false, "particle RHI command encoder creation failed"};
        }
        const auto recorded =
            recordFrameSet(*encoder, frame, particles, scene, sortBackToFront, textureBinding);
        if (!recorded.success) {
            return recorded;
        }
        return detail::submitEncodedFrame(device_, std::move(encoder));
    }

    RendererResult recordPass(render_graph::RenderContext& context,
        const RenderFrameContext& frame,
        const core::pmr::Vector<ParticleInstance>& particles,
        const SceneConstants& scene,
        bool sortBackToFront = true,
        const ParticleTextureBinding* textureBinding = nullptr)
    {
        if (particles.empty()) {
            return RendererResult{true, ""};
        }
        return recordFrameSet(
            context.commands(), frame, particles, scene, sortBackToFront, textureBinding);
    }

    const rhi::RenderPipeline* graphicsPipeline() const { return pipelineAlpha_.get(); }

private:
    void reset()
    {
        shaderPackage_ = {};
        shaderPackageTrailBeam_ = {};
        shaderPackageMeshParticle_ = {};
        pipelineAlpha_.reset();
        pipelineAdditive_.reset();
        pipelineTrailBeam_.reset();
        pipelineMeshParticle_.reset();
        pipelineLayout_.reset();
        pipelineLayoutTrailBeam_.reset();
        pipelineLayoutMeshParticle_.reset();
        descriptorArena_.reset();
        for (auto& ds : descriptorSets_) {
            ds.reset();
        }
        sampler_.reset();
        depthSampler_.reset();
        fallbackTexture_.reset();
        depthFallbackTexture_.reset();
        for (auto& texture : uploadedTextures_) {
            texture.reset();
        }
        for (auto& view : textureViews_) {
            view.reset();
        }
        for (auto& bound : boundTextures_) {
            bound = {};
        }
        vertexBuffer_.reset();
        uploadBuffer_.reset();
        vertexBufferTrailBeam_.reset();
        uploadBufferTrailBeam_.reset();
        vertexBufferMeshParticle_.reset();
        indexBufferMeshParticle_.reset();
        uploadBufferMeshParticleVertices_.reset();
        uploadBufferMeshParticleIndices_.reset();
        depthDescriptorSet_.reset();
        depthFallbackView_.reset();
        fallbackMeshParticleAsset_ = {};
        maxUploadBytes_ = 0;
        maxUploadBytesTrailBeam_ = 0;
        maxUploadBytesMeshParticleVertices_ = 0;
        maxUploadBytesMeshParticleIndices_ = 0;
    }

    RendererResult ensureTextureDescriptor(
        const ParticleTextureBinding* binding, std::uint32_t slot, bool required)
    {
        if (slot >= kParticleDescriptorSetSlots || !descriptorSets_[slot] || !sampler_) {
            return RendererResult{false, "particle RHI descriptor set is not ready"};
        }

        rhi::GpuTexture* texture = nullptr;
        if (binding != nullptr && binding->texture != nullptr) {
            texture = binding->texture;
        } else if (binding != nullptr && binding->textureResource != nullptr) {
            const auto& textureResource = *binding->textureResource;
            if (!textureResource.valid()) {
                return RendererResult{
                    false,
                    "particle texture resource binding produced invalid RGBA8 payload",
                };
            }
            if (textureResource.intent.resourceUsage != "particle.sprite.sampled"
                || textureResource.intent.uploadPolicy != "renderer-owned-staging"
                || textureResource.intent.requiredFormat != "Rgba8Unorm"
                || textureResource.intent.requiredDescriptorSet != "ParticleTextureSet") {
                return RendererResult{
                    false,
                    "particle texture resource binding violates ParticleTextureSet upload contract",
                };
            }
            const auto textureLabel = binding->resourceId.empty()
                ? textureResource.textureResourceId
                : binding->resourceId;
            const bool needsUpload = !uploadedTextures_[slot]
                || uploadedTextures_[slot]->desc().label != textureLabel
                || uploadedTextures_[slot]->desc().extent.width != textureResource.width
                || uploadedTextures_[slot]->desc().extent.height != textureResource.height
                || uploadedTextures_[slot]->desc().format != rhi::TextureFormat::Rgba8Unorm;
            if (needsUpload) {
                uploadedTextures_[slot] = device_.createTexture({
                    textureLabel,
                    {
                        textureResource.width,
                        textureResource.height,
                        1u,
                    },
                    rhi::TextureFormat::Rgba8Unorm,
                    rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
                    rhi::MemoryUsage::GpuOnly,
                    rhi::ResourceState::Undefined,
                });
                if (!uploadedTextures_[slot]) {
                    return RendererResult{
                        false,
                        "particle texture resource GPU texture creation failed",
                    };
                }
                const auto uploaded = detail::uploadParticleTextureRgba8(device_,
                    *uploadedTextures_[slot],
                    textureResource.rgba8.data(),
                    textureResource.width,
                    textureResource.height,
                    textureResource.width * 4u,
                    textureResource.height);
                if (!uploaded.success) {
                    return uploaded;
                }
                boundTextures_[slot] = {};
            }
            texture = uploadedTextures_[slot].get();
        } else if (binding != nullptr && binding->explicitUpload.valid()) {
            const auto textureLabel = binding->resourceId.empty()
                ? core::pmr::String("ParticleExplicitTexture", &core::defaultCpuMemoryResource())
                : binding->resourceId;
            const bool needsUpload = !uploadedTextures_[slot]
                || uploadedTextures_[slot]->desc().label != textureLabel
                || uploadedTextures_[slot]->desc().extent.width != binding->explicitUpload.width
                || uploadedTextures_[slot]->desc().extent.height != binding->explicitUpload.height
                || uploadedTextures_[slot]->desc().format != binding->explicitUpload.format;
            if (needsUpload) {
                uploadedTextures_[slot] = device_.createTexture({
                    textureLabel,
                    {
                        binding->explicitUpload.width,
                        binding->explicitUpload.height,
                        1u,
                    },
                    binding->explicitUpload.format,
                    rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
                    rhi::MemoryUsage::GpuOnly,
                    rhi::ResourceState::Undefined,
                });
                if (!uploadedTextures_[slot]) {
                    return RendererResult{
                        false,
                        "particle explicit texture creation failed",
                    };
                }
                const auto uploaded = detail::uploadParticleTextureRgba8(device_,
                    *uploadedTextures_[slot],
                    binding->explicitUpload.rgba8,
                    binding->explicitUpload.width,
                    binding->explicitUpload.height,
                    binding->explicitUpload.bytesPerRow,
                    binding->explicitUpload.rowsPerImage);
                if (!uploaded.success) {
                    return uploaded;
                }
                boundTextures_[slot] = {};
            }
            texture = uploadedTextures_[slot].get();
        } else if (binding != nullptr && !binding->resourceId.empty()) {
            return RendererResult{
                false,
                "particle texture resourceId requires resolved particle texture resource data or explicit texture upload",
            };
        } else if (!required) {
            texture = fallbackTexture_.get();
        }
        if (texture == nullptr) {
            return RendererResult{
                false,
                "particle textured draw requires explicit sampled texture binding",
            };
        }
        if ((texture->desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None) {
            return RendererResult{
                false,
                "particle texture binding must reference a sampled texture",
            };
        }
        if (boundTextures_[slot] == texture->id()) {
            return RendererResult{true, ""};
        }

        textureViews_[slot] = device_.createTextureView(*texture,
            {
                binding != nullptr && !binding->resourceId.empty()
                    ? binding->resourceId
                    : core::pmr::String("ParticleTextureView", &core::defaultCpuMemoryResource()),
                texture->desc().format,
                rhi::TextureUsage::Sampled,
                rhi::TextureViewDimension::Texture2D,
            });
        if (!textureViews_[slot]) {
            return RendererResult{false, "particle RHI texture view creation failed"};
        }
        const auto textureWritten = descriptorSets_[slot]->write({
            0,
            rhi::DescriptorType::SampledTexture,
            textureViews_[slot]->id(),
            rhi::DescriptorResourceKind::TextureView,
        });
        if (!textureWritten.success) {
            return RendererResult{false, textureWritten.diagnostics};
        }
        boundTextures_[slot] = texture->id();
        return RendererResult{true, ""};
    }

    rhi::RenderPipeline* pipelineForFamily(ParticleVisualFamily family) const
    {
        switch (family) {
        case ParticleVisualFamily::SpriteAdditive:
        case ParticleVisualFamily::LightningThin:
            return pipelineAdditive_.get();
        case ParticleVisualFamily::Trail:
        case ParticleVisualFamily::Beam:
            return pipelineTrailBeam_.get();
        case ParticleVisualFamily::MeshParticle:
            return pipelineMeshParticle_.get();
        case ParticleVisualFamily::SpriteAlpha:
        case ParticleVisualFamily::FallbackLegacy:
        case ParticleVisualFamily::Unknown:
        default:
            return pipelineAlpha_.get();
        }
    }

    const assets::MeshAsset* resolvePrototypeMeshAsset(const core::pmr::String& meshAssetId) const
    {
        if (meshAssetId == "debris_rock") {
            return &fallbackMeshParticleAsset_;
        }
        return nullptr;
    }

    RendererResult buildMeshParticleGeometry(const MeshParticleDrawPlan& plan,
        core::pmr::Vector<MeshParticleVertex>& outVertices,
        core::pmr::Vector<std::uint32_t>& outIndices) const
    {
        const auto* meshAsset = resolvePrototypeMeshAsset(plan.meshAssetId);
        if (!meshAsset) {
            return RendererResult{
                false,
                "mesh particle renderer prototype only supports meshAssetId 'debris_rock'; other mesh assets require explicit renderer-owned mesh resource binding",
            };
        }
        if (!meshAsset->validForIndexedDraw()) {
            return RendererResult{
                false, "mesh particle prototype fallback mesh is not valid for indexed draw"};
        }

        const auto perParticleVertexCount = meshAsset->vertices.size();
        const auto perParticleIndexCount = meshAsset->indices.size();
        outVertices.clear();
        outIndices.clear();
        outVertices.reserve(plan.instances.size() * perParticleVertexCount);
        outIndices.reserve(plan.instances.size() * perParticleIndexCount);

        for (std::size_t instanceIndex = 0; instanceIndex < plan.instances.size();
            ++instanceIndex) {
            const auto& inst = plan.instances[instanceIndex];
            const auto vertexBase = static_cast<std::uint32_t>(outVertices.size());
            const auto cosR = std::cos(inst.rotation);
            const auto sinR = std::sin(inst.rotation);

            for (const auto& srcVertex : meshAsset->vertices) {
                MeshParticleVertex dstVertex;
                const auto localX = srcVertex.position[0] * inst.scale;
                const auto localY = srcVertex.position[1] * inst.scale;
                const auto localZ = srcVertex.position[2] * inst.scale;
                dstVertex.worldPosition[0] = inst.position[0] + localX * cosR - localY * sinR;
                dstVertex.worldPosition[1] = inst.position[1] + localX * sinR + localY * cosR;
                dstVertex.worldPosition[2] = inst.position[2] + localZ;
                dstVertex.color[0] = inst.color[0];
                dstVertex.color[1] = inst.color[1];
                dstVertex.color[2] = inst.color[2];
                dstVertex.color[3] = inst.color[3];
                dstVertex.uv[0] = srcVertex.uv[0];
                dstVertex.uv[1] = srcVertex.uv[1];
                outVertices.push_back(dstVertex);
            }
            for (const auto index : meshAsset->indices) {
                outIndices.push_back(vertexBase + index);
            }
        }

        return RendererResult{true, ""};
    }

    RendererResult recordFrameSet(rhi::CommandEncoder& encoder,
        const RenderFrameContext& frame,
        const core::pmr::Vector<ParticleInstance>& particles,
        const SceneConstants& scene,
        bool sortBackToFront,
        const ParticleTextureBinding* textureBinding)
    {
        if (!pipelineAlpha_ || !vertexBuffer_ || !uploadBuffer_) {
            return RendererResult{false, "particle RHI renderer is not ready"};
        }

        const auto frameValid = detail::validateFrameTarget(frame);
        if (!frameValid.success) {
            return frameValid;
        }

        // Separate sprite particles from trail/beam/mesh particles
        core::pmr::Vector<ParticleInstance> spriteParticles{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<ParticleInstance> trailBeamParticles{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<ParticleInstance> meshParticles{&core::defaultCpuMemoryResource()};
        bool hasTrailBeam = false;
        bool hasMeshParticles = false;
        for (const auto& p : particles) {
            if (p.visualFamily == ParticleVisualFamily::Trail
                || p.visualFamily == ParticleVisualFamily::Beam) {
                trailBeamParticles.push_back(p);
                hasTrailBeam = true;
            } else if (p.visualFamily == ParticleVisualFamily::MeshParticle) {
                meshParticles.push_back(p);
                hasMeshParticles = true;
            } else {
                spriteParticles.push_back(p);
            }
        }

        // Check if any particle needs depth fade
        bool requiresDepthFade = false;
        for (const auto& p : particles) {
            if (p.depthFadeEnabled != 0u) {
                requiresDepthFade = true;
                break;
            }
        }
        // Resolve depth texture descriptor set when depth fade is required.
        // Depth texture sampling is a standard GPU capability supported by all
        // backends (Vulkan VK_IMAGE_ASPECT_DEPTH_BIT, Metal depth2d<float>,
        // WebGPU texture_depth_2d). The depthTexture flag on the binding
        // descriptor tells the WebGPU backend to use WGPUTextureSampleType_Depth;
        // other backends infer depth vs color from the texture view's format.
        // The particle shader statically declares set 2 even when the
        // push-constant branch disables depth fading. Bind the deterministic
        // fallback set for every draw; only its resource is replaced when a
        // frame supplies a real depth texture.
        rhi::DescriptorSet* activeDepthSet = depthDescriptorSet_.get();
        if (!activeDepthSet) {
            return RendererResult{false, "particle depth descriptor set is not ready"};
        }
        if (requiresDepthFade) {
            if (!depthDescriptorSet_) {
                return RendererResult{false,
                    "particle depth fade requires depth descriptor set, but it was not allocated"};
            }
            if (frame.depthTexture != nullptr) {
                // Create or update depth texture view
                const auto depthView = device_.createTextureView(*frame.depthTexture,
                    {
                        "ParticleDepthView",
                        frame.depthTexture->desc().format,
                        rhi::TextureUsage::Sampled,
                        rhi::TextureViewDimension::Texture2D,
                        0u, // baseMipLevel
                        1u, // mipLevelCount
                        0u, // baseArrayLayer
                        1u, // arrayLayerCount
                        rhi::TextureAspect::Depth,
                    });
                if (!depthView) {
                    return RendererResult{false, "particle RHI depth texture view creation failed"};
                }
                {
                    const auto depthWritten = depthDescriptorSet_->write({
                        0,
                        rhi::DescriptorType::SampledTexture,
                        depthView->id(),
                        rhi::DescriptorResourceKind::TextureView,
                    });
                    if (!depthWritten.success) {
                        return RendererResult{false, depthWritten.diagnostics};
                    }
                }
                // Sampler stays from fallback initialization
            } else {
                return RendererResult{
                    false,
                    "particle depth fade requires a depth texture binding in the frame context, "
                    "but no depth texture is available; depth-aware particle rendering requires "
                    "the renderer to provide a depth texture via RenderFrameContext::depthTexture",
                };
            }
        }

        // --- Sprite path (existing) ---
        if (!spriteParticles.empty()) {
            const auto drawSet =
                buildParticleDrawPlanSet(spriteParticles, scene, maxParticles_, sortBackToFront);
            if (!drawSet.success) {
                return RendererResult{false, drawSet.diagnostics};
            }

            for (const auto& plan : drawSet.plans) {
                if (plan.verticesSizeBytes() > maxUploadBytes_) {
                    return RendererResult{
                        false, "particle RHI vertex upload exceeds renderer budget"};
                }
            }

            const std::uint32_t descriptorSlot =
                frame.frame.frameSerial % kParticleDescriptorSetSlots;
            bool requiresTexture = false;
            for (const auto& plan : drawSet.plans) {
                if (plan.pushConstants.isTextured != 0u) {
                    requiresTexture = true;
                    break;
                }
            }
            const auto textureReady =
                ensureTextureDescriptor(textureBinding, descriptorSlot, requiresTexture);
            if (!textureReady.success) {
                return textureReady;
            }
            for (const auto& plan : drawSet.plans) {
                auto* pipeline = pipelineForFamily(plan.visualFamily);
                if (!pipeline) {
                    continue;
                }

                auto mutablePlan = plan;
                mutablePlan.pushConstants.visualFamily =
                    static_cast<std::uint32_t>(plan.visualFamily);

                const auto verticesWritten = device_.writeBuffer(
                    *uploadBuffer_, mutablePlan.vertices.data(), mutablePlan.verticesSizeBytes());
                if (!verticesWritten.success) {
                    return RendererResult{false, verticesWritten.diagnostics};
                }

                const auto recorded = recordParticleDrawPlan(encoder,
                    ParticleRhiDrawDesc{
                        *frame.colorTexture,
                        *pipeline,
                        *vertexBuffer_,
                        uploadBuffer_.get(),
                        mutablePlan,
                        true,
                        frame.frame.frameSerial,
                        descriptorSets_[descriptorSlot].get(),
                        activeDepthSet,
                        true,
                    });
                if (!recorded.success) {
                    return recorded;
                }
            }
        }

        // --- Trail/Beam path ---
        if (hasTrailBeam) {
            if (!pipelineTrailBeam_ || !vertexBufferTrailBeam_ || !uploadBufferTrailBeam_) {
                return RendererResult{false, "trail/beam RHI renderer is not ready"};
            }

            // Process trail and beam separately
            ParticleVisualFamily trailBeamFamilies[] = {
                ParticleVisualFamily::Trail,
                ParticleVisualFamily::Beam,
            };
            for (auto family : trailBeamFamilies) {
                const auto plan = buildTrailBeamDrawPlan(trailBeamParticles, scene, family);
                if (!plan.success) {
                    // Empty family is OK — skip
                    continue;
                }
                if (plan.verticesSizeBytes() > maxUploadBytesTrailBeam_) {
                    return RendererResult{
                        false, "trail/beam RHI vertex upload exceeds renderer budget"};
                }

                auto* pipeline = pipelineForFamily(family);
                if (!pipeline) {
                    continue;
                }

                const auto verticesWritten = device_.writeBuffer(
                    *uploadBufferTrailBeam_, plan.vertices.data(), plan.verticesSizeBytes());
                if (!verticesWritten.success) {
                    return RendererResult{false, verticesWritten.diagnostics};
                }

                // Record via modified RHI path for trail/beam
                rhi::RenderPassDesc passDesc;
                passDesc.label = "TrailBeam";
                passDesc.colorTargetName = frame.colorTexture->desc().label;
                passDesc.colorAttachments.push_back({
                    "TrailBeamColor",
                    frame.colorTexture->id(),
                    frame.colorTexture->desc().format,
                    rhi::LoadOp::Load,
                    rhi::StoreOp::Store,
                });

                // Upload transfer
                auto transfer = encoder.beginTransferPass({"TrailBeamUpload"});
                if (!transfer) {
                    return RendererResult{false, encoder.lastValidation().diagnostics};
                }
                const auto uploaded = transfer->uploadBuffer({
                    "TrailBeamVertexUpload",
                    uploadBufferTrailBeam_->id(),
                    vertexBufferTrailBeam_->id(),
                    plan.verticesSizeBytes(),
                    rhi::StagingIntent::Upload,
                    true,
                    frame.frame.frameSerial,
                });
                if (!uploaded.success) {
                    transfer->end();
                    return RendererResult{false, uploaded.diagnostics};
                }
                const auto transferEnd = transfer->end();
                if (!transferEnd.success) {
                    return RendererResult{false, transferEnd.diagnostics};
                }

                auto pass = encoder.beginRenderPass(passDesc);
                if (!pass) {
                    return RendererResult{false, encoder.lastValidation().diagnostics};
                }
                const auto pipelineResult =
                    bindRasterPipeline(*pass, *pipeline, frame.colorTarget.extent);
                if (!pipelineResult.success) {
                    pass->end();
                    return RendererResult{false, pipelineResult.diagnostics};
                }
                // TrailBeam.slang has no depth-texture declaration, so its
                // pipeline layout intentionally contains no depth set.
                const auto vb = pass->bindVertexBuffer(0, *vertexBufferTrailBeam_, 0);
                if (!vb.success) {
                    pass->end();
                    return RendererResult{false, vb.diagnostics};
                }
                const auto push =
                    pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                        0,
                        static_cast<std::uint32_t>(plan.pushConstantSizeBytes()),
                        &plan.pushConstants);
                if (!push.success) {
                    pass->end();
                    return RendererResult{false, push.diagnostics};
                }
                for (const auto& cmd : plan.commands) {
                    // Trail/beam particles use non-indexed draw (vertex buffer only)
                    const auto draw = pass->draw(
                        cmd.vertexCount, cmd.instanceCount, cmd.firstVertex, cmd.firstInstance);
                    if (!draw.success) {
                        pass->end();
                        return RendererResult{false, draw.diagnostics};
                    }
                }
                const auto end = pass->end();
                if (!end.success) {
                    return RendererResult{false, end.diagnostics};
                }
            }
        }

        // --- Mesh particle prototype path ---
        if (hasMeshParticles) {
            if (!pipelineMeshParticle_ || !vertexBufferMeshParticle_ || !indexBufferMeshParticle_
                || !uploadBufferMeshParticleVertices_ || !uploadBufferMeshParticleIndices_) {
                return RendererResult{
                    false,
                    meshParticleUnavailableDiagnostics_.empty()
                        ? "mesh particle RHI renderer is not ready"
                        : meshParticleUnavailableDiagnostics_.c_str(),
                };
            }

            core::pmr::Vector<const ParticleInstance*> meshParticleRefs{
                &core::defaultCpuMemoryResource()};
            meshParticleRefs.reserve(meshParticles.size());
            for (const auto& p : meshParticles) {
                meshParticleRefs.push_back(&p);
            }
            const auto meshPlan = buildMeshParticleDrawPlan(meshParticleRefs, scene);
            if (!meshPlan.success) {
                return RendererResult{false, meshPlan.diagnostics};
            }

            core::pmr::Vector<MeshParticleVertex> meshVertices{&core::defaultCpuMemoryResource()};
            core::pmr::Vector<std::uint32_t> meshIndices{&core::defaultCpuMemoryResource()};
            const auto geometryBuilt =
                buildMeshParticleGeometry(meshPlan, meshVertices, meshIndices);
            if (!geometryBuilt.success) {
                return geometryBuilt;
            }

            const auto vertexBytes =
                static_cast<std::uint64_t>(meshVertices.size() * sizeof(MeshParticleVertex));
            const auto indexBytes =
                static_cast<std::uint64_t>(meshIndices.size() * sizeof(std::uint32_t));
            if (vertexBytes > maxUploadBytesMeshParticleVertices_
                || indexBytes > maxUploadBytesMeshParticleIndices_) {
                return RendererResult{false, "mesh particle RHI upload exceeds renderer budget"};
            }

            const auto verticesWritten = device_.writeBuffer(
                *uploadBufferMeshParticleVertices_, meshVertices.data(), vertexBytes);
            if (!verticesWritten.success) {
                return RendererResult{false, verticesWritten.diagnostics};
            }
            const auto indicesWritten = device_.writeBuffer(
                *uploadBufferMeshParticleIndices_, meshIndices.data(), indexBytes);
            if (!indicesWritten.success) {
                return RendererResult{false, indicesWritten.diagnostics};
            }

            auto transfer = encoder.beginTransferPass({"MeshParticleUpload"});
            if (!transfer) {
                return RendererResult{false, encoder.lastValidation().diagnostics};
            }
            const auto vertexUploaded = transfer->uploadBuffer({
                "MeshParticleVertexUpload",
                uploadBufferMeshParticleVertices_->id(),
                vertexBufferMeshParticle_->id(),
                vertexBytes,
                rhi::StagingIntent::Upload,
                true,
                frame.frame.frameSerial,
            });
            if (!vertexUploaded.success) {
                transfer->end();
                return RendererResult{false, vertexUploaded.diagnostics};
            }
            const auto indexUploaded = transfer->uploadBuffer({
                "MeshParticleIndexUpload",
                uploadBufferMeshParticleIndices_->id(),
                indexBufferMeshParticle_->id(),
                indexBytes,
                rhi::StagingIntent::Upload,
                true,
                frame.frame.frameSerial,
            });
            if (!indexUploaded.success) {
                transfer->end();
                return RendererResult{false, indexUploaded.diagnostics};
            }
            const auto transferEnd = transfer->end();
            if (!transferEnd.success) {
                return RendererResult{false, transferEnd.diagnostics};
            }

            rhi::RenderPassDesc passDesc;
            passDesc.label = "MeshParticles";
            passDesc.colorTargetName = frame.colorTexture->desc().label;
            passDesc.colorAttachments.push_back({
                "MeshParticleColor",
                frame.colorTexture->id(),
                frame.colorTexture->desc().format,
                rhi::LoadOp::Load,
                rhi::StoreOp::Store,
            });
            auto pass = encoder.beginRenderPass(passDesc);
            if (!pass) {
                return RendererResult{false, encoder.lastValidation().diagnostics};
            }
            const auto pipelineResult =
                bindRasterPipeline(*pass, *pipelineMeshParticle_, frame.colorTarget.extent);
            if (!pipelineResult.success) {
                pass->end();
                return RendererResult{false, pipelineResult.diagnostics};
            }
            const auto vb = pass->bindVertexBuffer(0, *vertexBufferMeshParticle_, 0);
            if (!vb.success) {
                pass->end();
                return RendererResult{false, vb.diagnostics};
            }
            const auto ib =
                pass->bindIndexBuffer(*indexBufferMeshParticle_, rhi::IndexFormat::Uint32, 0u);
            if (!ib.success) {
                pass->end();
                return RendererResult{false, ib.diagnostics};
            }
            MeshParticlePushConstants pushConstants;
            for (std::uint32_t i = 0; i < 16u; ++i) {
                pushConstants.viewProjection[i] = scene.viewProjection[i];
            }
            pushConstants.visualFamily =
                static_cast<std::uint32_t>(ParticleVisualFamily::MeshParticle);
            const auto push =
                pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                    0,
                    static_cast<std::uint32_t>(sizeof(MeshParticlePushConstants)),
                    &pushConstants);
            if (!push.success) {
                pass->end();
                return RendererResult{false, push.diagnostics};
            }
            const auto draw =
                pass->drawIndexed(static_cast<std::uint32_t>(meshIndices.size()), 1u, 0u, 0, 0u);
            if (!draw.success) {
                pass->end();
                return RendererResult{false, draw.diagnostics};
            }
            const auto end = pass->end();
            if (!end.success) {
                return RendererResult{false, end.diagnostics};
            }
        }

        return RendererResult{true, ""};
    }

    rhi::GpuDevice& device_;
    rhi::ShaderPackageDesc shaderPackage_;
    arp::core::UniquePtr<rhi::PipelineLayout> pipelineLayout_;
    arp::core::UniquePtr<rhi::RenderPipeline> pipelineAlpha_;
    arp::core::UniquePtr<rhi::RenderPipeline> pipelineAdditive_;
    arp::core::UniquePtr<rhi::RenderPipeline> pipelineTrailBeam_;
    arp::core::UniquePtr<rhi::RenderPipeline> pipelineMeshParticle_;
    rhi::ShaderPackageDesc shaderPackageTrailBeam_;
    rhi::ShaderPackageDesc shaderPackageMeshParticle_;
    arp::core::UniquePtr<rhi::PipelineLayout> pipelineLayoutTrailBeam_;
    arp::core::UniquePtr<rhi::PipelineLayout> pipelineLayoutMeshParticle_;
    arp::core::UniquePtr<rhi::DescriptorArena> descriptorArena_;
    std::array<arp::core::UniquePtr<rhi::DescriptorSet>, kParticleDescriptorSetSlots>
        descriptorSets_;
    arp::core::UniquePtr<rhi::GpuSampler> sampler_;
    arp::core::UniquePtr<rhi::GpuSampler> depthSampler_;
    arp::core::UniquePtr<rhi::GpuTexture> fallbackTexture_;
    arp::core::UniquePtr<rhi::GpuTexture> depthFallbackTexture_;
    std::array<arp::core::UniquePtr<rhi::GpuTexture>, kParticleDescriptorSetSlots>
        uploadedTextures_;
    std::array<arp::core::UniquePtr<rhi::GpuTextureView>, kParticleDescriptorSetSlots>
        textureViews_;
    std::array<rhi::GpuResourceId, kParticleDescriptorSetSlots> boundTextures_;
    arp::core::UniquePtr<rhi::GpuBuffer> vertexBuffer_;
    arp::core::UniquePtr<rhi::GpuBuffer> uploadBuffer_;
    arp::core::UniquePtr<rhi::GpuBuffer> vertexBufferTrailBeam_;
    arp::core::UniquePtr<rhi::GpuBuffer> uploadBufferTrailBeam_;
    arp::core::UniquePtr<rhi::GpuBuffer> vertexBufferMeshParticle_;
    arp::core::UniquePtr<rhi::GpuBuffer> indexBufferMeshParticle_;
    arp::core::UniquePtr<rhi::GpuBuffer> uploadBufferMeshParticleVertices_;
    arp::core::UniquePtr<rhi::GpuBuffer> uploadBufferMeshParticleIndices_;
    arp::core::UniquePtr<rhi::DescriptorSet> depthDescriptorSet_;
    arp::core::UniquePtr<rhi::GpuTextureView> depthFallbackView_;
    assets::MeshAsset fallbackMeshParticleAsset_;
    core::pmr::String meshParticleUnavailableDiagnostics_{&core::defaultCpuMemoryResource()};
    std::uint32_t maxParticles_ = 0;
    std::uint64_t maxUploadBytes_ = 0;
    std::uint64_t maxUploadBytesTrailBeam_ = 0;
    std::uint64_t maxUploadBytesMeshParticleVertices_ = 0;
    std::uint64_t maxUploadBytesMeshParticleIndices_ = 0;
};

} // namespace arp::renderer
