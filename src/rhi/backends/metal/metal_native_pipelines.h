#pragma once

#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_binding_facts.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/metal_direct_binding_validation.h"
#include "rhi/semantics/metal_vertex_stream_lowering.h"
#include "shader/spirv_msl_buffer_memory_lowering.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace arp::rhi {
class BindlessShaderPackage;
}

namespace arp::rhi_metal {
class MetalOriginalPipeline;

struct MetalRhiNativeBindingSlot {
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t metalSlot = UINT32_MAX;
    rhi::DescriptorType type = rhi::DescriptorType::UniformBuffer;
    bool active = false;
};

struct MetalRhiNativeArgumentBufferSlot {
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t argumentBufferSlot = UINT32_MAX;
    std::uint32_t argumentIndex = UINT32_MAX;
    std::uint32_t capacity = 0;
    rhi::DescriptorType type = rhi::DescriptorType::UniformBuffer;
    bool active = false;
};

struct MetalRhiNativeFramebufferFetchSlot {
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t logicalAttachmentIndex = UINT32_MAX;
    std::uint32_t metalColorSlot = UINT32_MAX;
    bool depth = false;
    bool active = false;
};

// Cold facts inside the existing payload, never a public proof issuer. The
// owning pipeline separately retains/authenticates the selected package module.
struct MetalRhiNativeBufferMemberMemory {
    std::uint32_t set = 0u, binding = 0u, slot = UINT32_MAX;
    std::size_t targetType = shader::BufferDeclarationTypeLayouts::absent;
    shader::BufferDeclarationTypeLayouts target;
    shader::SpirvMslBufferMemoryLowering::Facts relation;
    shader::SpirvMslBufferMemoryLowering::TargetElement element;
};

struct MetalRhiNativeStageBufferMemberMemory {
    std::uint32_t version = 0u;
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    core::pmr::Vector<MetalRhiNativeBufferMemberMemory> entries{&core::defaultCpuMemoryResource()};
};

struct MetalRhiNativePipelinePayload {
    std::shared_ptr<void> nativeDevice;
    bool shaderModulePayloadPresent = false;
    bool renderPipelinePayloadPresent = false;
    bool supportsIndirectCommands = false;
    bool computePipelinePayloadPresent = false;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    bool pipelineCacheKeyStable = false;
    bool rasterStateRecorded = false;
    bool hasViewport = false;
    rhi::ViewportState viewport;
    bool hasScissor = false;
    rhi::ScissorState scissor;
    bool depthStencilStateRecorded = false;
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
    bool depthOnlyRenderPipeline = false;
    rhi::ShaderStage preparedGraphicsStages = rhi::ShaderStage::None;
    bool renderPassCompatibilityEnabled = false;
    std::pmr::vector<rhi::TextureFormat> colorFormats{&core::defaultCpuMemoryResource()};
    rhi::TextureFormat depthStencilFormat = rhi::TextureFormat::Unknown;
    bool writesRenderTargetArrayIndex = false;
    bool usesDepthStencilAttachment = false;
    rhi::CompareOp depthCompare = rhi::CompareOp::LessEqual;
    rhi::CullMode cullMode = rhi::CullMode::None;
    rhi::FrontFace frontFace = rhi::FrontFace::CounterClockwise;
    std::uint64_t sampleMask = 0xffffffffffffffffull;
    std::uint32_t sampleCount = 1u;
    bool alphaToCoverageEnabled = false;
    std::uint32_t subpassIndex = 0;
    rhi::PrimitiveTopology primitiveTopology = rhi::PrimitiveTopology::TriangleList;
    // Physical Metal resource index emitted by shader lowering. A logical RHI
    // binding is not necessarily the same slot in the MSL artifact.
    std::uint32_t pushConstantMetalBufferIndex = UINT32_MAX;
    rhi::semantics::MetalVertexStreamMapping vertexStreams;
    MetalRhiVertexInputFacts vertexInputFacts;
    rhi::semantics::MetalPhysicalBindingStages nativeBindingStages;
    core::pmr::Vector<MetalRhiNativeBindingSlot> metalBindingSpace{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiNativeArgumentBufferSlot> metalArgumentBuffer{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiNativeFramebufferFetchSlot> metalFramebufferFetch{
        &core::defaultCpuMemoryResource()};
    std::uint32_t computeBufferFactsVersion = 0u;
    core::pmr::Vector<MetalRhiBufferBindingFact> computeBufferFacts{
        &core::defaultCpuMemoryResource()};
    std::array<MetalRhiStageBufferBindingFacts, 2u> graphicsBufferFacts;
    std::array<MetalRhiStageBufferPointeeFacts, 2u> graphicsBufferPointeeFacts;
    std::array<MetalRhiNativeStageBufferMemberMemory, 2u> graphicsBufferMemberMemory;
    std::uint32_t graphicsTextureFactsVersion = 0u;
    core::pmr::Vector<MetalRhiTextureBindingFact> graphicsTextureFacts{
        &core::defaultCpuMemoryResource()};
    std::uint32_t computeWorkgroupSizeX = 0;
    std::uint32_t computeWorkgroupSizeY = 0;
    std::uint32_t computeWorkgroupSizeZ = 0;
    core::pmr::String metalGpuCountedIndirectContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String metalGpuCountedIndirectStatus{&core::defaultCpuMemoryResource()};
    std::uint32_t metalGpuCountedIndirectCountBufferIndex = UINT32_MAX;
    bool metalGpuCountedIndirectUsesDrawId = false;
    bool metalGpuCountedIndirectMasksOutOfRange = false;
    core::pmr::String metalTessellationContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String metalTessellationStatus{&core::defaultCpuMemoryResource()};
    bool metalTessellationAbiRecorded = false;
    std::uint32_t metalTessellationFactorBufferIndex = UINT32_MAX;
    std::uint32_t metalTessellationControlPointCount = 0;
    core::pmr::String metalTessellationVertexEntryPoint{&core::defaultCpuMemoryResource()};
    std::uint32_t metalTessellationPatchType = UINT32_MAX;
    std::uint32_t metalTessellationFactorFormat = UINT32_MAX;
    std::uint32_t metalTessellationFactorBytesPerPatch = 0;
    std::uint32_t metalTessellationFactorInstanceStrideBytes = 0;
    // depthBias: previously the Metal payload dropped the L2 depthBias state
    // entirely, so a pipeline that enabled depthBias rendered without any bias
    // - a silent native semantics drop the RHI contract forbids. The four
    // fields below mirror VkPipelineRasterizationStateCreateInfo so the
    // command encoder can apply them via setDepthBias:slopeScale:clamp: once
    // the Metal render-command-encoder path is wired.
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    // stencil: previously the Metal payload only recorded a single
    // depthStencilStateRecorded bool and dropped the front/back face ops +
    // read/write masks. Mirror the L2 StencilFaceState so the depth-stencil
    // descriptor and encoder calls can lower them faithfully.
    bool stencilEnabled = false;
    rhi::StencilFaceState stencilFront = {};
    rhi::StencilFaceState stencilBack = {};
    std::uint32_t stencilReadMask = 0xffu;
    std::uint32_t stencilWriteMask = 0xffu;
    std::uint64_t pipelineCacheKey = 0;
    std::shared_ptr<void> shaderLibrary;
    std::shared_ptr<void> fragmentShaderLibrary;
    std::shared_ptr<void> vertexFunction;
    std::shared_ptr<void> fragmentFunction;
    std::shared_ptr<void> computeFunction;
    std::shared_ptr<void> pipelineState;
    std::shared_ptr<void> depthStencilState;
    std::uint32_t primitiveType = 3;
    std::uint32_t patchControlPointCount = 0;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};

private:
    friend class MetalOriginalPipeline;
    // Native command retention must also retain the original artifact/input
    // and its independent creation quota after the public wrapper is dropped.
    std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> originalGraphicsInput_;
    std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> originalComputeInput_;
    std::shared_ptr<core::CpuMemoryQuota> originalCreationQuota_;
    std::uint64_t originalCreationQuotaBytes_ = 0u;
};

struct MetalRhiPipelinePayloadInspection {
    bool backendPayloadPresent = false;
    bool shaderModulePayloadPresent = false;
    bool renderPipelinePayloadPresent = false;
    bool computePipelinePayloadPresent = false;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    bool pipelineCacheKeyStable = false;
    bool rasterStateRecorded = false;
    bool depthStencilStateRecorded = false;
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
    bool depthOnlyRenderPipeline = false;
    rhi::CompareOp depthCompare = rhi::CompareOp::LessEqual;
    rhi::CullMode cullMode = rhi::CullMode::None;
    rhi::FrontFace frontFace = rhi::FrontFace::CounterClockwise;
    std::uint64_t sampleMask = 0xffffffffffffffffull;
    bool alphaToCoverageEnabled = false;
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    bool stencilEnabled = false;
    rhi::StencilFaceState stencilFront = {};
    rhi::StencilFaceState stencilBack = {};
    std::uint32_t stencilReadMask = 0xffu;
    std::uint32_t stencilWriteMask = 0xffu;
    std::uint64_t pipelineCacheKey = 0;
};

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const rhi::semantics::MetalVertexStreamMapping& vertexStreams,
    bool supportsIndirectCommands);

// Backend-private startup construction from the two verified package entries.
std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const rhi::semantics::MetalVertexStreamMapping& vertexStreams,
    std::string_view verifiedVertexMsl,
    std::string_view verifiedFragmentMsl);

// Authenticated original package overload. Requested Native member graphs are
// collected at actual pipeline creation and compared before publication.
std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc&,
    const rhi::PipelineValidationReport&,
    const rhi::semantics::MetalVertexStreamMapping&,
    const std::shared_ptr<const rhi::BindlessShaderPackage>&);

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc& desc, const rhi::PipelineValidationReport& report);
std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    std::string_view verifiedMslSource);

} // namespace arp::rhi_metal
