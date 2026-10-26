#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/backends/metal/metal_native_runtime.h"

namespace arp::rhi_metal {

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc&,
    const rhi::PipelineValidationReport&,
    const rhi::semantics::MetalVertexStreamMapping&,
    const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>&)
{
    return {};
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc&,
    const rhi::PipelineValidationReport&,
    const std::shared_ptr<const shader::SingleEntryShaderArtifact>&)
{
    return {};
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc&,
    const rhi::PipelineValidationReport&,
    const rhi::semantics::MetalVertexStreamMapping&,
    const std::shared_ptr<const rhi::BindlessShaderPackage>&)
{
    return {};
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc&,
    const rhi::PipelineValidationReport&,
    const rhi::semantics::MetalVertexStreamMapping&,
    std::string_view,
    std::string_view)
{
    // No executable Graphics construction outside Apple. Never return a stub
    // pipeline as evidence for the prepared-native interface.
    return {};
}

namespace {

std::shared_ptr<MetalRhiNativePipelinePayload> makeDiagnosticPayload(
    const rhi::PipelineValidationReport& report, bool renderPipeline)
{
    auto payload = arp::core::makeShared<MetalRhiNativePipelinePayload>();
    payload->nativeDevice = metalRhiNativeDeviceIdentity();
    payload->shaderModulePayloadPresent = true;
    payload->renderPipelinePayloadPresent = renderPipeline;
    payload->computePipelinePayloadPresent = !renderPipeline;
    payload->nativeRuntimeDiagnosticOnly = true;
    payload->pipelineCacheKeyStable = report.pipelineCacheKeyStable;
    payload->pipelineCacheKey = report.pipelineCacheKey;
    payload->diagnostics =
        "Metal RHI native pipelines are diagnostic-only outside Apple Objective-C++ builds";
    return payload;
}

} // namespace

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const rhi::semantics::MetalVertexStreamMapping& vertexStreams,
    bool /*supportsIndirectCommands*/)
{
    auto payload = makeDiagnosticPayload(report, true);
    payload->vertexStreams = vertexStreams;
    payload->rasterStateRecorded = report.rasterStateValidated;
    payload->depthStencilStateRecorded = report.stencilStateValidated;
    payload->depthTestEnabled = desc.depthStencil.depthTestEnabled;
    payload->depthWriteEnabled = desc.depthStencil.depthWriteEnabled;
    payload->depthOnlyRenderPipeline = report.colorTargetCount == 0u;
    payload->colorFormats = report.colorFormats;
    payload->depthStencilFormat = report.depthStencilFormat;
    payload->patchControlPointCount = desc.patchControlPointCount;
    payload->writesRenderTargetArrayIndex = report.selectedFragmentWritesRenderTargetArrayIndex;
    payload->depthCompare = desc.depthStencil.depthCompare;
    payload->cullMode = desc.cullMode;
    payload->frontFace = desc.frontFace;
    payload->pushConstantMetalBufferIndex = desc.shaderPackage
        ? desc.shaderPackage->reflection.pushConstantMetalBufferIndex
        : UINT32_MAX;
    payload->computeWorkgroupSizeX =
        desc.shaderPackage ? desc.shaderPackage->reflection.computeWorkgroupSizeX : 0;
    payload->computeWorkgroupSizeY =
        desc.shaderPackage ? desc.shaderPackage->reflection.computeWorkgroupSizeY : 0;
    payload->computeWorkgroupSizeZ =
        desc.shaderPackage ? desc.shaderPackage->reflection.computeWorkgroupSizeZ : 0;
    if (desc.shaderPackage) {
        const auto& reflection = desc.shaderPackage->reflection;
        payload->metalGpuCountedIndirectContractVersion =
            reflection.metalGpuCountedIndirectContractVersion;
        payload->metalGpuCountedIndirectStatus = reflection.metalGpuCountedIndirectStatus;
        payload->metalGpuCountedIndirectCountBufferIndex =
            reflection.metalGpuCountedIndirectCountBufferIndex;
        payload->metalGpuCountedIndirectUsesDrawId = reflection.metalGpuCountedIndirectUsesDrawId;
        payload->metalGpuCountedIndirectMasksOutOfRange =
            reflection.metalGpuCountedIndirectMasksOutOfRange;
        payload->metalTessellationContractVersion = reflection.metalTessellationContractVersion;
        payload->metalTessellationStatus = reflection.metalTessellationStatus;
        payload->metalTessellationFactorBufferIndex = reflection.metalTessellationFactorBufferIndex;
        payload->metalTessellationControlPointCount = reflection.metalTessellationControlPointCount;
        payload->metalTessellationVertexEntryPoint = reflection.metalTessellationVertexEntryPoint;
        payload->metalTessellationPatchType = reflection.metalTessellationPatchType;
        payload->metalTessellationFactorFormat = reflection.metalTessellationFactorFormat;
        payload->metalTessellationFactorBytesPerPatch =
            reflection.metalTessellationFactorBytesPerPatch;
        payload->metalTessellationFactorInstanceStrideBytes =
            reflection.metalTessellationFactorInstanceStrideBytes;
        payload->metalTessellationAbiRecorded =
            rhi::hasMetalTessellationAbi(reflection, desc.patchControlPointCount);
    }
    // Mirror the L2 depthBias + stencil state on the diagnostic-only stub so
    // parity tests can assert a uniform payload surface across Apple and
    // non-Apple builds. The real Metal encoder calls only fire on Apple
    // builds (.mm); the stub stays diagnostic-only.
    payload->depthBiasEnabled = desc.depthStencil.depthBiasEnabled;
    payload->depthBiasConstantFactor = desc.depthStencil.depthBiasConstantFactor;
    payload->depthBiasClamp = desc.depthStencil.depthBiasClamp;
    payload->depthBiasSlopeFactor = desc.depthStencil.depthBiasSlopeFactor;
    payload->stencilEnabled = desc.depthStencil.stencilEnabled;
    payload->stencilFront = desc.depthStencil.front;
    payload->stencilBack = desc.depthStencil.back;
    payload->stencilReadMask = desc.depthStencil.stencilReadMask;
    payload->stencilWriteMask = desc.depthStencil.stencilWriteMask;
    return payload;
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc& desc, const rhi::PipelineValidationReport& report)
{
    auto payload = makeDiagnosticPayload(report, false);
    payload->pushConstantMetalBufferIndex = desc.shaderPackage
        ? desc.shaderPackage->reflection.pushConstantMetalBufferIndex
        : UINT32_MAX;
    payload->computeWorkgroupSizeX = report.selectedComputeWorkgroupSizeX;
    payload->computeWorkgroupSizeY = report.selectedComputeWorkgroupSizeY;
    payload->computeWorkgroupSizeZ = report.selectedComputeWorkgroupSizeZ;
    return payload;
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    std::string_view verifiedMslSource)
{
    auto payload = createMetalRhiNativeComputePipelinePayload(desc, report);
    if (verifiedMslSource.empty()) {
        payload->diagnostics = "Metal RHI verified MSL bytes are missing";
    } else {
        payload->diagnostics =
            "Metal RHI verified MSL compilation is unavailable outside Apple Objective-C++ builds";
    }
    return payload;
}

} // namespace arp::rhi_metal
