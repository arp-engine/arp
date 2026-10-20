#include "core/cpu_allocator.h"
#include "core/number_format.h"
#include "core/stable_hash.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"
#include "rhi/backends/metal/metal_native_features.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_original_pipeline.h"
#include "rhi/backends/metal/metal_zero_base_indirect_consumer_pipeline.h"
#include "rhi/backends/metal/metal_zero_base_indirect_pipeline.h"
#include "rhi/frontend/bindless_scene_content.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/pipeline_push_constant_abi_identity.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "rhi/semantics/bindless_scene_raster.h"
#include "rhi/semantics/metal_direct_binding_validation.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"
#include "rhi/semantics/original_synchronous_pipeline_creation_lowering.h"
#include "rhi/semantics/push_constant_validation.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/shader_reflection_cache.h"
#include "shader/single_entry_shader_artifact.h"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

namespace arp::rhi_metal {

namespace {

constexpr const char* kMetalBindingSpaceContractVersion = "MetalBindingSpace/1";

struct MetalBindingSpaceLoadResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiNativeBindingSlot> slots{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiNativeArgumentBufferSlot> argumentBuffers{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiNativeFramebufferFetchSlot> framebufferFetch{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<shader::ShaderEntryPoint> reflectedEntryPoints{
        &core::defaultCpuMemoryResource()};
};

rhi::semantics::MetalVertexStreamMapping lowerVertexStreams(
    const rhi::RenderPipelineDesc& desc, const MetalBindingSpaceLoadResult& bindings)
{
    std::uint32_t reserved = 0u;
    // Invalid physical indices set the reserved high bit; the semantic
    // lowering diagnoses them before any native vertex descriptor is built.
    const auto reserve = [&](std::uint32_t index) {
        reserved |= 1u << (index < 31u ? index : 31u);
    };
    if (!bindings.success || !desc.shaderPackage) {
        return {};
    }
    const auto& reflection = desc.shaderPackage->reflection;
    if (reflection.pushConstantByteCount != 0u)
        reserve(reflection.pushConstantMetalBufferIndex);
    for (const auto& binding : bindings.slots) {
        if (binding.active
            && (binding.type == rhi::DescriptorType::UniformBuffer
                || binding.type == rhi::DescriptorType::StorageBuffer
                || binding.type == rhi::DescriptorType::ReadOnlyStorageBuffer))
            reserve(binding.metalSlot);
    }
    for (const auto& binding : bindings.argumentBuffers)
        if (binding.active)
            reserve(binding.argumentBufferSlot);
    if (reflection.metalGpuCountedIndirectStatus == "Available")
        reserve(reflection.metalGpuCountedIndirectCountBufferIndex);
    if (reflection.metalTessellationStatus == "Available")
        reserve(reflection.metalTessellationFactorBufferIndex);
    return rhi::semantics::lowerMetalVertexStreams(desc.vertexBuffers, reserved);
}

rhi::DescriptorType metalDescriptorType(shader::BindingType type)
{
    return rhi::descriptorTypeFromShaderBinding(type).value_or(rhi::DescriptorType::UniformBuffer);
}

const shader::ShaderBinding* findCachedBinding(
    const shader::ShaderReflection& reflection, std::uint32_t set, std::uint32_t binding)
{
    for (const auto& candidate : reflection.bindings) {
        if (candidate.set == set && candidate.binding == binding) {
            return &candidate;
        }
    }
    return nullptr;
}

const shader::ShaderMetalBindingSpaceBinding* findMetalBinding(
    const shader::ShaderReflection& reflection, std::uint32_t set, std::uint32_t binding)
{
    for (const auto& candidate : reflection.metalBindingSpace) {
        if (candidate.set == set && candidate.binding == binding) {
            return &candidate;
        }
    }
    return nullptr;
}

const rhi::ShaderReflectionBinding* findPackageBinding(
    const rhi::ShaderReflectionDesc& reflection, std::uint32_t set, std::uint32_t binding)
{
    for (const auto& candidate : reflection.descriptorBindings) {
        if (candidate.set == set && candidate.binding == binding) {
            return &candidate;
        }
    }
    return nullptr;
}

bool descriptorReflectionMatches(
    const shader::ShaderReflection& cached, const rhi::ShaderReflectionDesc& package)
{
    if (cached.bindings.size() != package.descriptorBindings.size()) {
        return false;
    }
    for (const auto& cachedBinding : cached.bindings) {
        const auto* packageBinding =
            findPackageBinding(package, cachedBinding.set, cachedBinding.binding);
        if (!packageBinding || metalDescriptorType(cachedBinding.type) != packageBinding->type
            || cachedBinding.arrayCount != packageBinding->arrayCount
            || cachedBinding.stageMask != static_cast<std::uint32_t>(packageBinding->stages)
            || cachedBinding.subpassAttachmentIndex != packageBinding->subpassAttachmentIndex
            || cachedBinding.depthTexture != packageBinding->depthTexture) {
            return false;
        }
    }
    return true;
}

bool sameMetalNamespace(rhi::DescriptorType lhs, rhi::DescriptorType rhs)
{
    const auto isBuffer = [](rhi::DescriptorType type) {
        return type == rhi::DescriptorType::UniformBuffer
            || type == rhi::DescriptorType::StorageBuffer
            || type == rhi::DescriptorType::ReadOnlyStorageBuffer;
    };
    const auto isTexture = [](rhi::DescriptorType type) {
        return type == rhi::DescriptorType::SampledTexture
            || type == rhi::DescriptorType::StorageTexture
            || type == rhi::DescriptorType::InputAttachment;
    };
    return (isBuffer(lhs) && isBuffer(rhs)) || (isTexture(lhs) && isTexture(rhs))
        || (lhs == rhi::DescriptorType::Sampler && rhs == rhi::DescriptorType::Sampler);
}

MetalBindingSpaceLoadResult loadMetalBindingSpace(const rhi::ShaderPackageDesc& package,
    const shader::ShaderReflection* verifiedReflection = nullptr,
    bool retainEntryPoints = true)
{
    MetalBindingSpaceLoadResult result;
    shader::ShaderReflectionCacheLoadResult cached;
    if (verifiedReflection) {
        const auto reflectionValidation =
            shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(
                *verifiedReflection);
        if (!reflectionValidation.success) {
            result.diagnostics = "Metal RHI verified package reflection is invalid: ";
            result.diagnostics += reflectionValidation.diagnostics;
            return result;
        }
    } else {
        if (package.artifacts.reflectionPath.empty()) {
            result.diagnostics = "Metal RHI requires a generated reflection artifact path";
            return result;
        }
        cached = shader::ShaderReflectionCache::loadFromFile(package.artifacts.reflectionPath);
        if (!cached.success) {
            result.diagnostics = "Metal RHI could not load reflection artifact: ";
            result.diagnostics += cached.diagnostics;
            return result;
        }
        const auto artifactIdentityValidation =
            shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(cached.module,
                package.slangModulePath,
                package.artifacts.spirvPath,
                package.artifacts.mslPath,
                package.artifacts.wgslPath);
        if (!artifactIdentityValidation.success) {
            result.diagnostics = "Metal RHI reflection artifact identity validation failed: ";
            result.diagnostics += artifactIdentityValidation.diagnostics;
            return result;
        }
    }
    const auto& cachedReflection =
        verifiedReflection ? *verifiedReflection : cached.module.reflection;
    const auto pushMemberValidation =
        shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(cachedReflection);
    if (!pushMemberValidation.success) {
        result.diagnostics =
            "Metal RHI reflection artifact failed push-constant member validation: ";
        result.diagnostics += pushMemberValidation.diagnostics;
        return result;
    }
    const auto& packageReflection = package.reflection;
    if (cachedReflection.pushConstantSizeBytes != packageReflection.pushConstantByteCount
        || cachedReflection.pushConstantContractVersion
            != packageReflection.pushConstantContractVersion
        || cachedReflection.pushConstantStages != packageReflection.pushConstantStages
        || cachedReflection.pushConstantMetalBufferIndex
            != packageReflection.pushConstantMetalBufferIndex
        || !rhi::ShaderPushConstantAbiIdentity::matchesMembers(
            cachedReflection.pushConstantMembers, packageReflection.pushConstantMembers)
        || !descriptorReflectionMatches(cachedReflection, packageReflection)) {
        result.diagnostics =
            "Metal RHI reflection artifact disagrees with package reflection metadata";
        return result;
    }
    if (cachedReflection.metalGpuCountedIndirectContractVersion
            != packageReflection.metalGpuCountedIndirectContractVersion
        || cachedReflection.metalGpuCountedIndirectStatus
            != packageReflection.metalGpuCountedIndirectStatus
        || cachedReflection.metalGpuCountedIndirectCountBufferIndex
            != packageReflection.metalGpuCountedIndirectCountBufferIndex
        || cachedReflection.metalGpuCountedIndirectUsesDrawId
            != packageReflection.metalGpuCountedIndirectUsesDrawId
        || cachedReflection.metalGpuCountedIndirectMasksOutOfRange
            != packageReflection.metalGpuCountedIndirectMasksOutOfRange
        || cachedReflection.metalTessellationContractVersion
            != packageReflection.metalTessellationContractVersion
        || cachedReflection.metalTessellationStatus != packageReflection.metalTessellationStatus
        || cachedReflection.metalTessellationVertexEntryPoint
            != packageReflection.metalTessellationVertexEntryPoint
        || cachedReflection.metalTessellationFactorBufferIndex
            != packageReflection.metalTessellationFactorBufferIndex
        || cachedReflection.metalTessellationControlPointCount
            != packageReflection.metalTessellationControlPointCount
        || cachedReflection.metalTessellationPatchType
            != packageReflection.metalTessellationPatchType
        || cachedReflection.metalTessellationFactorFormat
            != packageReflection.metalTessellationFactorFormat
        || cachedReflection.metalTessellationFactorBytesPerPatch
            != packageReflection.metalTessellationFactorBytesPerPatch
        || cachedReflection.metalTessellationFactorInstanceStrideBytes
            != packageReflection.metalTessellationFactorInstanceStrideBytes) {
        result.diagnostics =
            "Metal RHI reflection artifact disagrees with package shader ABI metadata";
        return result;
    }
    const auto validAbiStatus = [](const std::pmr::string& version,
                                    const std::pmr::string& status,
                                    std::string_view expectedVersion) {
        return (version.empty() && (status.empty() || status == "Unavailable"))
            || (version == expectedVersion && status == "Available");
    };
    if (!validAbiStatus(cachedReflection.metalGpuCountedIndirectContractVersion,
            cachedReflection.metalGpuCountedIndirectStatus,
            rhi::kMetalGpuCountedIndirectContractVersion)
        || !validAbiStatus(cachedReflection.metalTessellationContractVersion,
            cachedReflection.metalTessellationStatus,
            rhi::kMetalTessellationContractVersion)) {
        result.diagnostics = "Metal RHI reflection artifact has an invalid shader ABI status";
        return result;
    }
    if (retainEntryPoints)
        result.reflectedEntryPoints = cachedReflection.entryPoints;
    if (cachedReflection.metalBindingSpaceStatus != "Available"
        || cachedReflection.metalBindingSpaceContractVersion != kMetalBindingSpaceContractVersion) {
        result.diagnostics = "Metal RHI reflection artifact has no MetalBindingSpace/1 contract";
        if (!cachedReflection.metalBindingSpaceUnavailableReason.empty()) {
            result.diagnostics += ": ";
            result.diagnostics += cachedReflection.metalBindingSpaceUnavailableReason;
        }
        return result;
    }

    const auto& argumentReflection = cachedReflection.metalArgumentBuffer;
    if (cachedReflection.metalArgumentBufferStatus == "Available"
        && cachedReflection.metalArgumentBufferContractVersion != "MetalArgumentBuffer/1") {
        result.diagnostics =
            "Metal RHI reflection artifact has an unknown argument-buffer contract";
        return result;
    }
    const auto& fetchReflection = cachedReflection.metalFramebufferFetch;
    if (cachedReflection.metalFramebufferFetchStatus == "Available"
        && cachedReflection.metalFramebufferFetchContractVersion != "MetalFramebufferFetch/1") {
        result.diagnostics =
            "Metal RHI reflection artifact has an unknown framebuffer-fetch contract";
        return result;
    }

    for (const auto& logical : package.reflection.descriptorBindings) {
        const auto* cachedLogical =
            findCachedBinding(cachedReflection, logical.set, logical.binding);
        const auto* physical = findMetalBinding(cachedReflection, logical.set, logical.binding);
        const auto argumentPhysical = std::find_if(
            argumentReflection.begin(), argumentReflection.end(), [&](const auto& candidate) {
                return candidate.set == logical.set && candidate.binding == logical.binding;
            });
        const bool hasArgumentPhysical =
            argumentPhysical != argumentReflection.end() && argumentPhysical->active;
        const bool hasDirectPhysical = physical && physical->active;
        const bool inputAttachment = logical.type == rhi::DescriptorType::InputAttachment;
        if (!cachedLogical || metalDescriptorType(cachedLogical->type) != logical.type
            || cachedLogical->arrayCount != logical.arrayCount
            || cachedLogical->depthTexture != logical.depthTexture
            || (!inputAttachment && !hasDirectPhysical && !hasArgumentPhysical)
            || (!inputAttachment && hasDirectPhysical
                && (physical->metalSlot == UINT32_MAX
                    || !rhi::semantics::validMetalDirectBindingSlot(
                        logical.type, physical->metalSlot)
                    || metalDescriptorType(physical->physicalType) != logical.type))
            || (!inputAttachment && hasArgumentPhysical
                && (argumentPhysical->argumentBufferSlot == UINT32_MAX
                    || argumentPhysical->argumentIndex == UINT32_MAX
                    || argumentPhysical->capacity == 0u
                    || metalDescriptorType(argumentPhysical->physicalType) != logical.type))) {
            result.diagnostics =
                "Metal RHI reflection artifact disagrees with logical descriptor metadata";
            return result;
        }
        if (logical.type == rhi::DescriptorType::InputAttachment) {
            const auto fetch = std::find_if(
                fetchReflection.begin(), fetchReflection.end(), [&](const auto& candidate) {
                    return candidate.set == logical.set && candidate.binding == logical.binding
                        && candidate.active
                        && candidate.logicalAttachmentIndex == logical.subpassAttachmentIndex
                        && candidate.depth == logical.depthTexture;
                });
            if (fetch == fetchReflection.end()) {
                result.diagnostics =
                    "Metal RHI InputAttachment has no executable framebuffer-fetch lowering";
                return result;
            }
        }
    }

    for (const auto& physical : cachedReflection.metalBindingSpace) {
        if (!physical.active) {
            continue;
        }
        if (!findCachedBinding(cachedReflection, physical.set, physical.binding)) {
            result.diagnostics =
                "Metal RHI reflection artifact contains an unmapped physical binding";
            return result;
        }
        if (!findPackageBinding(package.reflection, physical.set, physical.binding)) {
            result.diagnostics =
                "Metal RHI package is missing an active reflected descriptor binding";
            return result;
        }
        const auto type = metalDescriptorType(physical.physicalType);
        if (!rhi::semantics::validMetalDirectBindingSlot(type, physical.metalSlot)) {
            result.diagnostics = "Metal RHI reflection artifact uses an out-of-range resource slot";
            return result;
        }
        if (cachedReflection.pushConstantMetalBufferIndex != UINT32_MAX
            && sameMetalNamespace(type, rhi::DescriptorType::UniformBuffer)
            && physical.metalSlot == cachedReflection.pushConstantMetalBufferIndex) {
            result.diagnostics = "Metal RHI direct binding aliases the push-constant buffer slot";
            return result;
        }
        for (const auto& existing : result.slots) {
            if (existing.set == physical.set && existing.binding == physical.binding) {
                result.diagnostics = "Metal RHI reflection artifact duplicates a logical binding";
                return result;
            }
            if (existing.metalSlot == physical.metalSlot
                && sameMetalNamespace(existing.type, type)) {
                result.diagnostics = "Metal RHI reflection artifact aliases a Metal resource slot";
                return result;
            }
        }
        result.slots.push_back({
            physical.set,
            physical.binding,
            physical.metalSlot,
            type,
            true,
        });
    }

    for (const auto& physical : argumentReflection) {
        if (!physical.active)
            continue;
        if (physical.argumentBufferSlot == UINT32_MAX || physical.argumentIndex == UINT32_MAX
            || physical.capacity == 0u || physical.argumentBufferSlot >= 31u) {
            result.diagnostics =
                "Metal RHI reflection artifact contains an invalid argument-buffer slot";
            return result;
        }
        if (!findCachedBinding(cachedReflection, physical.set, physical.binding)
            || !findPackageBinding(package.reflection, physical.set, physical.binding)) {
            result.diagnostics =
                "Metal RHI argument-buffer metadata contains an unmapped logical binding";
            return result;
        }
        for (const auto& existing : result.argumentBuffers) {
            if (existing.set == physical.set && existing.binding == physical.binding) {
                result.diagnostics =
                    "Metal RHI reflection artifact duplicates an argument-buffer binding";
                return result;
            }
            if (existing.argumentBufferSlot == physical.argumentBufferSlot) {
                result.diagnostics = "Metal RHI reflection artifact aliases argument-buffer slots";
                return result;
            }
        }
        for (const auto& existing : result.slots) {
            if (sameMetalNamespace(existing.type, rhi::DescriptorType::UniformBuffer)
                && existing.metalSlot == physical.argumentBufferSlot) {
                result.diagnostics =
                    "Metal RHI argument-buffer slot aliases a direct Metal buffer slot";
                return result;
            }
        }
        if (cachedReflection.pushConstantMetalBufferIndex != UINT32_MAX
            && physical.argumentBufferSlot == cachedReflection.pushConstantMetalBufferIndex) {
            result.diagnostics =
                "Metal RHI argument-buffer slot aliases the push-constant buffer slot";
            return result;
        }
        result.argumentBuffers.push_back({
            physical.set,
            physical.binding,
            physical.argumentBufferSlot,
            physical.argumentIndex,
            physical.capacity,
            metalDescriptorType(physical.physicalType),
            true,
        });
    }
    const auto validateReservedBufferSlot = [&](std::uint32_t slot, const char* contractName) {
        if (slot >= 31u) {
            result.diagnostics = "Metal RHI ";
            result.diagnostics += contractName;
            result.diagnostics += " uses an out-of-range buffer slot";
            return false;
        }
        if (cachedReflection.pushConstantMetalBufferIndex == slot) {
            result.diagnostics = "Metal RHI ";
            result.diagnostics += contractName;
            result.diagnostics += " aliases the push-constant buffer slot";
            return false;
        }
        for (const auto& existing : result.slots) {
            if (existing.metalSlot == slot
                && sameMetalNamespace(existing.type, rhi::DescriptorType::UniformBuffer)) {
                result.diagnostics = "Metal RHI ";
                result.diagnostics += contractName;
                result.diagnostics += " aliases a direct buffer binding";
                return false;
            }
        }
        for (const auto& existing : result.argumentBuffers) {
            if (existing.argumentBufferSlot == slot) {
                result.diagnostics = "Metal RHI ";
                result.diagnostics += contractName;
                result.diagnostics += " aliases an argument-buffer slot";
                return false;
            }
        }
        return true;
    };
    if (cachedReflection.metalGpuCountedIndirectStatus == "Available") {
        if (!rhi::hasMetalGpuCountedIndirectMaskAbi(packageReflection)) {
            result.diagnostics = "Metal RHI GPU-counted indirect reflection ABI is invalid";
            return result;
        }
        if (!validateReservedBufferSlot(cachedReflection.metalGpuCountedIndirectCountBufferIndex,
                "GPU-counted indirect ABI")) {
            return result;
        }
    }
    if (cachedReflection.metalTessellationStatus == "Available") {
        if (!rhi::hasMetalTessellationAbi(
                packageReflection, cachedReflection.metalTessellationControlPointCount)) {
            result.diagnostics = "Metal RHI tessellation reflection ABI is invalid";
            return result;
        }
        if (!validateReservedBufferSlot(
                cachedReflection.metalTessellationFactorBufferIndex, "tessellation ABI")) {
            return result;
        }
    }

    for (const auto& physical : fetchReflection) {
        if (!physical.active)
            continue;
        if (physical.logicalAttachmentIndex == UINT32_MAX
            || (!physical.depth
                && (physical.metalColorSlot == UINT32_MAX || physical.metalColorSlot >= 8u))
            || (physical.depth && physical.metalColorSlot != UINT32_MAX)) {
            result.diagnostics =
                "Metal RHI reflection artifact contains an invalid framebuffer-fetch slot";
            return result;
        }
        const auto* logical = findCachedBinding(cachedReflection, physical.set, physical.binding);
        const auto* packageBinding =
            findPackageBinding(package.reflection, physical.set, physical.binding);
        if (!logical || !packageBinding
            || metalDescriptorType(logical->type) != rhi::DescriptorType::InputAttachment
            || packageBinding->type != rhi::DescriptorType::InputAttachment
            || logical->subpassAttachmentIndex != physical.logicalAttachmentIndex
            || packageBinding->subpassAttachmentIndex != physical.logicalAttachmentIndex
            || logical->depthTexture != physical.depth
            || packageBinding->depthTexture != physical.depth
            || (!physical.depth && physical.metalColorSlot != physical.logicalAttachmentIndex)) {
            result.diagnostics =
                "Metal RHI framebuffer-fetch metadata disagrees with logical attachment contract";
            return result;
        }
        result.framebufferFetch.push_back({
            physical.set,
            physical.binding,
            physical.logicalAttachmentIndex,
            physical.metalColorSlot,
            physical.depth,
            true,
        });
    }
    result.success = true;
    return result;
}

bool packageContainsEntrypoint(
    const rhi::ShaderPackageDesc& package, const rhi::ShaderEntrypoint& shader)
{
    for (const auto& entrypoint : package.entrypoints) {
        if (entrypoint.modulePath == shader.modulePath && entrypoint.entryPoint == shader.entryPoint
            && entrypoint.stage == shader.stage
            && entrypoint.sourceLanguage == shader.sourceLanguage) {
            return true;
        }
    }
    return false;
}

rhi::PipelineValidationReport makePipelineReport(const rhi::PipelineLayout* layout,
    const rhi::ShaderPackageDesc* shaderPackage,
    std::uint32_t entrypointCount)
{
    rhi::PipelineValidationReport report;
    report.backend = rhi::Backend::Metal;
    report.entrypointCount = entrypointCount;
    if (layout) {
        report.descriptorLayoutCount =
            static_cast<std::uint32_t>(layout->desc().descriptorLayouts.size());
        report.descriptorSetCount = layout->descriptorSetCount();
        report.descriptorTableCount = layout->descriptorTableCount();
        report.nativeHandleExposedToUpperLayer = layout->nativeHandleExposedToUpperLayer();
    }
    if (shaderPackage) {
        report.descriptorBindingCount =
            static_cast<std::uint32_t>(shaderPackage->reflection.descriptorBindings.size());
        report.pushConstantBytes = shaderPackage->reflection.pushConstantByteCount;
        report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
            || shaderPackage->nativeHandleExposedToUpperLayer;
    }
    return report;
}

rhi::PipelineValidationReport failPipeline(
    rhi::PipelineValidationReport report, std::pmr::string diagnostics)
{
    report.diagnostics = std::move(diagnostics);
    return report;
}

void copyStateReport(
    rhi::PipelineValidationReport& report, const rhi::PipelineStateValidationResult& state)
{
    report.colorTargetCount = state.colorTargetCount;
    report.colorFormats = state.colorFormats;
    report.depthStencilFormat = state.depthStencilFormat;
    report.sampleCount = state.sampleCount;
    report.blendStateValidated = state.blendStateValidated;
    report.colorWriteMaskValidated = state.colorWriteMaskValidated;
    report.stencilStateValidated = state.stencilStateValidated;
    report.depthBiasStateValidated = state.depthBiasStateValidated;
    report.rasterStateValidated = state.rasterStateValidated;
    report.topologyValidated = state.topologyValidated;
    report.msaaStateValidated = state.msaaStateValidated;
    report.resolveStateValidated = state.resolveStateValidated;
    report.viewportScissorStateValidated = state.viewportScissorStateValidated;
    report.depthOnlyStateValidated = state.depthOnlyStateValidated;
    report.unsupportedStateDiagnosed = state.unsupportedStateDiagnosed;
    report.pipelineCacheKey = state.pipelineCacheKey;
    report.pipelineCacheKeyStable = state.success;
}

const shader::ShaderEntryPoint* findReflectedEntrypoint(
    const MetalBindingSpaceLoadResult& reflection, const rhi::ShaderEntrypoint& selected)
{
    for (const auto& entrypoint : reflection.reflectedEntryPoints) {
        if (entrypoint.name == selected.entryPoint
            && ((selected.stage == rhi::ShaderStage::Vertex
                    && entrypoint.stage == shader::ShaderStage::Vertex)
                || (selected.stage == rhi::ShaderStage::Fragment
                    && entrypoint.stage == shader::ShaderStage::Fragment)
                || (selected.stage == rhi::ShaderStage::Compute
                    && entrypoint.stage == shader::ShaderStage::Compute))) {
            return &entrypoint;
        }
    }
    return nullptr;
}

rhi::ValidationResult validateMetalDirectDescriptorLowering(const rhi::PipelineLayoutDesc& layout,
    const rhi::ShaderReflectionDesc& reflection,
    const MetalBindingSpaceLoadResult& metalBindingSpace,
    const shader::ShaderReflection* verifiedInactiveReflection = nullptr)
{
    const auto reflectionValidation =
        rhi::validatePipelineLayoutAgainstShaderReflection(layout, reflection);
    if (!reflectionValidation.success) {
        return reflectionValidation;
    }
    if (!metalBindingSpace.success) {
        return {false, metalBindingSpace.diagnostics};
    }
    if (!layout.descriptorTables.empty()) {
        if (metalBindingSpace.argumentBuffers.empty()) {
            return {
                false, "Metal descriptor table lowering requires MetalArgumentBuffer/1 reflection"};
        }
        for (const auto& slot : layout.descriptorTables) {
            std::uint32_t matchingBindings = 0;
            for (const auto& binding : reflection.descriptorBindings) {
                if (binding.set != slot.set || binding.type != slot.type
                    || binding.arrayCount != slot.capacity
                    || (slot.stages & binding.stages) != binding.stages) {
                    continue;
                }
                const auto mapped = std::find_if(metalBindingSpace.argumentBuffers.begin(),
                    metalBindingSpace.argumentBuffers.end(),
                    [&](const auto& physical) {
                        return physical.set == binding.set && physical.binding == binding.binding
                            && physical.type == binding.type && physical.capacity == slot.capacity
                            && physical.active;
                    });
                if (mapped != metalBindingSpace.argumentBuffers.end()) {
                    ++matchingBindings;
                }
            }
            if (matchingBindings == 0u) {
                return {
                    false, "Metal descriptor table is missing executable argument-buffer mapping"};
            }
            if (matchingBindings != 1u) {
                return {false, "Metal descriptor table has ambiguous argument-buffer mapping"};
            }
        }
    }
    for (const auto& binding : reflection.descriptorBindings) {
        if (binding.type != rhi::DescriptorType::InputAttachment)
            continue;
        const auto mapped = std::find_if(metalBindingSpace.framebufferFetch.begin(),
            metalBindingSpace.framebufferFetch.end(),
            [&](const auto& physical) {
                return physical.set == binding.set && physical.binding == binding.binding
                    && physical.active
                    && physical.logicalAttachmentIndex == binding.subpassAttachmentIndex
                    && physical.depth == binding.depthTexture;
            });
        if (mapped == metalBindingSpace.framebufferFetch.end()) {
            return {false, "Metal InputAttachment requires MetalFramebufferFetch/1 reflection"};
        }
    }
    for (const auto& slot : layout.descriptorSets) {
        const auto* descriptorLayout = rhi::findPipelineDescriptorLayout(layout, slot.layoutName);
        if (!descriptorLayout) {
            return {
                false, "Metal direct descriptor lowering references an unknown descriptor layout"};
        }
        for (const auto& binding : descriptorLayout->bindings) {
            if (verifiedInactiveReflection
                && rhi::semantics::BindlessPipelineInterfaceContract::explicitMetalInactiveBinding(
                    *verifiedInactiveReflection, slot.set, binding.binding))
                continue;
            if (binding.type == rhi::DescriptorType::InputAttachment) {
                continue;
            }
            if (binding.tableBacked) {
                return {
                    false, "Metal direct descriptor lowering cannot consume table-backed bindings"};
            }
            if (binding.arrayCount > 1u) {
                const auto mappedCount = std::count_if(metalBindingSpace.argumentBuffers.begin(),
                    metalBindingSpace.argumentBuffers.end(),
                    [&](const MetalRhiNativeArgumentBufferSlot& physical) {
                        return physical.set == slot.set && physical.binding == binding.binding
                            && physical.active && physical.type == binding.type
                            && physical.capacity == binding.arrayCount;
                    });
                if (mappedCount != 1u) {
                    return {false,
                        "Metal descriptor array requires one executable argument-buffer mapping"};
                }
                continue;
            }
            const auto mapped = std::find_if(metalBindingSpace.slots.begin(),
                metalBindingSpace.slots.end(),
                [&](const MetalRhiNativeBindingSlot& physical) {
                    return physical.set == slot.set && physical.binding == binding.binding
                        && physical.active && physical.type == binding.type;
                });
            if (mapped == metalBindingSpace.slots.end()) {
                std::pmr::string diagnostics{
                    "Metal direct descriptor lowering is missing a physical Metal binding: set=",
                    &core::defaultCpuMemoryResource()};
                diagnostics += std::to_string(slot.set).c_str();
                diagnostics += " binding=";
                diagnostics += std::to_string(binding.binding).c_str();
                return {false, std::move(diagnostics)};
            }
        }
    }
    return {true, {}};
}

rhi::ValidationResult validateMetalOptionalLowerings(
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    const rhi::PipelineLayoutDesc& layout,
    const rhi::ShaderReflectionDesc& reflection)
{
    bool requiresArgumentBuffers = !layout.descriptorTables.empty();
    for (const auto& descriptorLayout : layout.descriptorLayouts) {
        for (const auto& binding : descriptorLayout.bindings) {
            // Metal's direct set path is only scalar. Arrays use the same
            // argument-buffer ABI as descriptor tables and therefore require
            // the identical capability gate.
            requiresArgumentBuffers = requiresArgumentBuffers || binding.arrayCount > 1u;
        }
    }
    if (requiresArgumentBuffers
        && !rhi::semantics::hasExecutableNativeFeature(
            capabilityContract, rhi::semantics::NativeBackendFeature::ArgumentBuffers)) {
        return {false, "RHI-METAL-DESCRIPTOR-TABLE-CAPABILITY-NOT-AUTHORIZED"};
    }
    for (const auto& binding : reflection.descriptorBindings) {
        if (binding.type != rhi::DescriptorType::InputAttachment) {
            continue;
        }
        const auto feature = binding.depthTexture
            ? rhi::semantics::NativeBackendFeature::FramebufferFetchDepth
            : rhi::semantics::NativeBackendFeature::FramebufferFetch;
        if (!rhi::semantics::hasExecutableNativeFeature(capabilityContract, feature)) {
            return {false, "RHI-METAL-FRAMEBUFFER-FETCH-CAPABILITY-NOT-AUTHORIZED"};
        }
    }
    return {true, {}};
}

const rhi::ShaderEntrypoint* findMetalComputeEntrypoint(
    const rhi::ShaderPackageDesc& package, const rhi::ShaderEntrypoint& selected)
{
    for (const auto& entrypoint : package.entrypoints) {
        if (entrypoint.modulePath == selected.modulePath
            && entrypoint.entryPoint == selected.entryPoint
            && entrypoint.stage == rhi::ShaderStage::Compute) {
            return &entrypoint;
        }
    }
    return nullptr;
}

class MetalRhiRenderPipeline final : public rhi::RenderPipeline {
public:
    MetalRhiRenderPipeline(rhi::RenderPipelineDesc desc,
        rhi::PipelineValidationReport report,
        std::shared_ptr<MetalRhiNativePipelinePayload> nativePayload,
        rhi::semantics::DirectObjectTelemetryLease telemetryLease,
        rhi::semantics::DirectObjectBudgetLease budgetLease,
        core::UniquePtr<rhi::PipelineLayout> ownedLayout = {},
        core::UniquePtr<rhi::ShaderPackageDesc> ownedInterface = {},
        std::shared_ptr<const rhi::BindlessShaderPackage> retainedPackage = {},
        std::shared_ptr<const rhi::BindlessAuthorization> authorization = {},
        rhi::BindlessGraphicsPipelineEntries entries = {},
        std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> originalInput = {})
        : ownedLayout_(std::move(ownedLayout)), ownedInterface_(std::move(ownedInterface)),
          retainedPackage_(std::move(retainedPackage)), authorization_(std::move(authorization)),
          desc_(std::move(desc)), report_(std::move(report)),
          reflection_(desc_.shaderPackage
                  ? rhi::ShaderReflectionStateCopy::copy(desc_.shaderPackage->reflection)
                  : rhi::ShaderReflectionDesc{}),
          nativePayload_(std::move(nativePayload)), telemetryLease_(std::move(telemetryLease)),
          budgetLease_(std::move(budgetLease)), graphicsEntries_(entries),
          originalInput_(std::move(originalInput))
    {}

    const rhi::PipelineLayout* layout() const override { return desc_.layout; }

    rhi::PrimitiveTopology primitiveTopology() const override { return desc_.topology; }

    const core::pmr::Vector<rhi::VertexBufferLayoutDesc>* bindlessVertexBuffers(
        std::uint32_t version) const noexcept override
    {
        return version == 1u && authorization_ ? &desc_.vertexBuffers : nullptr;
    }

    const rhi::PipelineValidationReport& validationReport() const override { return report_; }

    const rhi::ShaderReflectionDesc& shaderReflection() const override { return reflection_; }

    rhi::ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return rhi::ShaderSourceLanguage::Slang;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

    bool usesPatchListTessellation() const override
    {
        return desc_.topology == rhi::PrimitiveTopology::PatchList;
    }

    std::shared_ptr<const rhi::BindlessAuthorization>
    bindlessAuthorization() const noexcept override
    {
        return authorization_;
    }

    rhi::ShaderStage bindlessShaderStages() const noexcept override
    {
        return authorization_ && nativePayload_ ? nativePayload_->preparedGraphicsStages
                                                : rhi::ShaderStage::None;
    }

    std::shared_ptr<MetalRhiNativePipelinePayload> nativePayload() const { return nativePayload_; }

    MetalRhiSelectedGraphicsEntries selectedEntries() const noexcept
    {
        return {desc_.vertexShader.entryPoint, desc_.fragmentShader.entryPoint};
    }

    const rhi::BindlessPackageModule* bindlessShaderModule(
        rhi::ShaderStage stage, std::uint32_t version) const noexcept override
    {
        return authorization_ ? graphicsEntries_.module(stage, version) : nullptr;
    }

    const char* bindlessOrdinaryBufferRequirement(rhi::ShaderStage stage,
        std::uint32_t set,
        std::uint32_t binding,
        rhi::ShaderBufferMemoryRequirement& output,
        std::uint32_t version) const noexcept override
    {
        output = {};
        if ((version != 1u && version != 2u) || !authorization_)
            return "RHI-BINDLESS-ORDINARY-RANGE-VERSION";
        rhi::ShaderBufferMemoryRequirement candidate;
        if (const auto* id =
                graphicsEntries_.bufferRequirement(stage, set, binding, false, candidate, version))
            return id;
        const auto* module = graphicsEntries_.module(stage, 1u);
        const auto* mapped = findMetalBinding(module->reflection.reflection, set, binding);
        if (!mapped || !mapped->active || mapped->metalSlot == UINT32_MAX || !nativePayload_
            || !nativePayload_->nativeRuntimeBacked || nativePayload_->nativeRuntimeDiagnosticOnly
            || !nativePayload_->renderPipelinePayloadPresent)
            return "RHI-METAL-ORDINARY-RANGE-MAPPING";
        const auto& facts =
            nativePayload_->graphicsBufferFacts[stage == rhi::ShaderStage::Vertex ? 0u : 1u];
        if (facts.stage != stage || facts.version != 1u)
            return "RHI-METAL-ORDINARY-RANGE-FACTS";
        const MetalRhiBufferBindingFact* found = nullptr;
        for (const auto& fact : facts.entries)
            if (fact.slot == mapped->metalSlot) {
                if (found)
                    return "RHI-METAL-ORDINARY-RANGE-DUPLICATE";
                found = &fact;
            }
        if (!found || !found->alignment || !found->requiredBytes)
            return "RHI-METAL-ORDINARY-RANGE-FACTS";
        candidate.minimumBytes = found->requiredBytes;
        candidate.alignmentBytes = found->alignment;
        candidate.evidence = rhi::ShaderBufferMemoryRequirement::Evidence::DriverQuery;
        candidate.targetRuntimeArrayOffsetBytes = candidate.targetRuntimeArrayStrideBytes = 0u;
        if (candidate.hasRuntimeArray && module->nativeMemory.version()) {
            const auto& members =
                nativePayload_
                    ->graphicsBufferMemberMemory[stage == rhi::ShaderStage::Vertex ? 0u : 1u];
            if (members.version != 1u || members.stage != stage)
                return "RHI-METAL-ORDINARY-RANGE-MEMBER-FACTS";
            const MetalRhiNativeBufferMemberMemory* selected = nullptr;
            for (const auto& record : members.entries)
                if (record.set == set && record.binding == binding) {
                    if (selected)
                        return "RHI-METAL-ORDINARY-RANGE-DUPLICATE";
                    selected = &record;
                }
            if (!selected || selected->slot != mapped->metalSlot
                || !selected->relation.hasRuntimeArray || candidate.sourceRuntimeArrayOffsetBytes
                || selected->relation.sourceRuntimeStrideBytes
                    != candidate.sourceRuntimeArrayStrideBytes
                || selected->relation.targetRuntimeStrideBytes != selected->element.strideBytes
                || !selected->element.strideBytes
                || selected->element.alignmentBytes != found->alignment
                || selected->element.strideBytes != found->requiredBytes)
                return "RHI-METAL-ORDINARY-RANGE-MEMBER-FACTS";
            candidate.targetRuntimeArrayStrideBytes = selected->element.strideBytes;
        }
        output = candidate;
        return nullptr;
    }

    const auto& originalInput() const noexcept { return originalInput_; }

    const char* bindlessOrdinaryBufferMemberMemory(rhi::ShaderStage stage,
        std::uint32_t set,
        std::uint32_t binding,
        rhi::ShaderBufferMemberMemory& output,
        std::uint32_t version) const noexcept override
    {
        output = {};
        if (version != 1u && version != 2u)
            return "RHI-BINDLESS-MEMBER-MEMORY-VERSION";
        if (!authorization_)
            return "RHI-METAL-MEMBER-MEMORY-ORIGINAL-PIPELINE";
        const auto* module = graphicsEntries_.module(stage, 1u);
        const shader::NativeBufferMemoryDeclarations* source = nullptr;
        if (const auto* id = rhi::BindlessShaderPackage::originalNativeBufferMemory(
                retainedPackage_, module, set, binding, source))
            return id;
        rhi::ShaderBufferMemoryRequirement range;
        if (const auto* id = bindlessOrdinaryBufferRequirement(stage, set, binding, range, version))
            return id;
        const auto* physical = findMetalBinding(module->reflection.reflection, set, binding);
        const auto& memory =
            nativePayload_->graphicsBufferMemberMemory[stage == rhi::ShaderStage::Vertex ? 0u : 1u];
        if (!physical || memory.version != 1u || memory.stage != stage)
            return "RHI-METAL-MEMBER-MEMORY-FACTS";
        const MetalRhiNativeBufferMemberMemory* selected = nullptr;
        for (const auto& record : memory.entries)
            if (record.set == set && record.binding == binding) {
                if (selected)
                    return "RHI-METAL-MEMBER-MEMORY-DUPLICATE";
                selected = &record;
            }
        const auto* original = source->lookup(set, binding);
        if (!selected || !original || selected->slot != physical->metalSlot
            || selected->targetType >= selected->target.nodes().size()
            || !selected->relation.comparedPairs
            || selected->relation.matrixColumns != original->matrices.size()
            || selected->relation.arrayWrappers != original->wrappers.size()
            || selected->relation.occupiedBytes != range.sourceOccupiedBytes
            || source->sourceTypes().nodes()[original->sourceType].occupiedExtentBytes
                != range.sourceOccupiedBytes)
            return "RHI-METAL-MEMBER-MEMORY-FACTS";
        if (range.hasRuntimeArray) {
            if (version != 2u || original->relationVersion != 2u
                || original->rootForm
                    != shader::SpirvMslBufferMemoryLowering::RootForm::RuntimeElement
                || !selected->relation.hasRuntimeArray || range.sourceOccupiedBytes
                || range.sourceRuntimeArrayOffsetBytes || range.targetRuntimeArrayOffsetBytes
                || selected->relation.sourceRuntimeStrideBytes
                    != range.sourceRuntimeArrayStrideBytes
                || selected->relation.targetRuntimeStrideBytes
                    != range.targetRuntimeArrayStrideBytes
                || selected->element.strideBytes != range.targetRuntimeArrayStrideBytes
                || !selected->element.strideBytes
                || selected->target.nodes()[selected->targetType].occupiedExtentBytes
                    > selected->element.strideBytes)
                return "RHI-METAL-MEMBER-MEMORY-RUNTIME-FACTS";
        } else if (selected->relation.hasRuntimeArray
            || selected->target.nodes()[selected->targetType].occupiedExtentBytes
                != range.sourceOccupiedBytes)
            return "RHI-METAL-MEMBER-MEMORY-FACTS";
        output = {range.sourceOccupiedBytes,
            range.minimumBytes,
            rhi::ShaderBufferMemberMemory::Evidence::ValidatedTargetMembers};
        if (version == 2u) {
            output.sourceMinimumBytes = range.sourceMinimumBytes;
            output.hasRuntimeArray = range.hasRuntimeArray;
            output.sourceRuntimeArrayOffsetBytes = range.sourceRuntimeArrayOffsetBytes;
            output.sourceRuntimeArrayStrideBytes = range.sourceRuntimeArrayStrideBytes;
            output.targetRuntimeArrayOffsetBytes = range.targetRuntimeArrayOffsetBytes;
            output.targetRuntimeArrayStrideBytes = range.targetRuntimeArrayStrideBytes;
        }
        return nullptr;
    }

    auto originalGraphicsPassState() const noexcept
    {
        return rhi::OriginalBindlessGraphicsPassContract::capturePipeline(desc_);
    }

private:
    core::UniquePtr<rhi::PipelineLayout> ownedLayout_;
    core::UniquePtr<rhi::ShaderPackageDesc> ownedInterface_;
    std::shared_ptr<const rhi::BindlessShaderPackage> retainedPackage_;
    std::shared_ptr<const rhi::BindlessAuthorization> authorization_;
    rhi::RenderPipelineDesc desc_;
    rhi::PipelineValidationReport report_;
    rhi::ShaderReflectionDesc reflection_;
    std::shared_ptr<MetalRhiNativePipelinePayload> nativePayload_;
    rhi::semantics::DirectObjectTelemetryLease telemetryLease_;
    rhi::semantics::DirectObjectBudgetLease budgetLease_;
    const rhi::BindlessGraphicsPipelineEntries graphicsEntries_;
    std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> originalInput_;
};

class MetalRhiComputePipeline final : public rhi::ComputePipeline {
public:
    MetalRhiComputePipeline(rhi::ComputePipelineDesc desc,
        rhi::PipelineValidationReport report,
        std::shared_ptr<MetalRhiNativePipelinePayload> nativePayload,
        rhi::semantics::DirectObjectTelemetryLease telemetryLease,
        rhi::semantics::DirectObjectBudgetLease budgetLease,
        std::shared_ptr<const rhi::BindlessAuthorization> authorization = {},
        core::UniquePtr<rhi::PipelineLayout> ownedLayout = {},
        core::UniquePtr<rhi::ShaderPackageDesc> ownedInterface = {},
        std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> originalInput = {})
        : ownedLayout_(std::move(ownedLayout)), ownedInterface_(std::move(ownedInterface)),
          desc_(std::move(desc)), report_(std::move(report)),
          reflection_(desc_.shaderPackage
                  ? rhi::ShaderReflectionStateCopy::copy(desc_.shaderPackage->reflection)
                  : rhi::ShaderReflectionDesc{}),
          nativePayload_(std::move(nativePayload)), telemetryLease_(std::move(telemetryLease)),
          budgetLease_(std::move(budgetLease)), authorization_(std::move(authorization)),
          originalInput_(std::move(originalInput))
    {
        if (ownedLayout_)
            desc_.layout = ownedLayout_.get();
    }

    const rhi::PipelineLayout* layout() const override { return desc_.layout; }

    const rhi::PipelineValidationReport& validationReport() const override { return report_; }

    const rhi::ShaderReflectionDesc& shaderReflection() const override { return reflection_; }

    rhi::ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return rhi::ShaderSourceLanguage::Slang;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

    std::shared_ptr<const rhi::BindlessAuthorization>
    bindlessAuthorization() const noexcept override
    {
        return authorization_;
    }

    std::shared_ptr<MetalRhiNativePipelinePayload> nativePayload() const { return nativePayload_; }

    const auto& originalInput() const noexcept { return originalInput_; }

private:
    core::UniquePtr<rhi::PipelineLayout> ownedLayout_;
    core::UniquePtr<rhi::ShaderPackageDesc> ownedInterface_;
    rhi::ComputePipelineDesc desc_;
    rhi::PipelineValidationReport report_;
    rhi::ShaderReflectionDesc reflection_;
    std::shared_ptr<MetalRhiNativePipelinePayload> nativePayload_;
    rhi::semantics::DirectObjectTelemetryLease telemetryLease_;
    rhi::semantics::DirectObjectBudgetLease budgetLease_;
    std::shared_ptr<const rhi::BindlessAuthorization> authorization_;
    std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> originalInput_;
};

} // namespace

static rhi::PipelineValidationReport validateMetalRhiRenderPipelineImpl(
    const rhi::RenderPipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    const shader::ShaderReflection* verifiedPackageReflection)
{
    auto report = makePipelineReport(desc.layout,
        desc.shaderPackage,
        desc.topology == rhi::PrimitiveTopology::PatchList ? 4u : 2u);
    if (!desc.layout) {
        return failPipeline(report, "Metal RHI render pipeline requires layout");
    }
    if (!desc.shaderPackage) {
        return failPipeline(report, "Metal RHI render pipeline requires shader package");
    }
    const auto packageValidation = rhi::validateShaderPackage(*desc.shaderPackage);
    if (!packageValidation.success) {
        return failPipeline(report, packageValidation.diagnostics);
    }
    report.shaderPackageValidated = true;
    const auto metalBindingSpace =
        loadMetalBindingSpace(*desc.shaderPackage, verifiedPackageReflection);
    if (!metalBindingSpace.success) {
        return failPipeline(report, metalBindingSpace.diagnostics.c_str());
    }
    const auto& reflection = desc.shaderPackage->reflection;
    if (reflection.metalGpuCountedIndirectStatus == "Available"
        && !rhi::hasMetalGpuCountedIndirectMaskAbi(reflection)) {
        return failPipeline(report, "RHI-METAL-GPU-COUNTED-INDIRECT-REFLECTION-ABI-INVALID");
    }
    if (desc.topology == rhi::PrimitiveTopology::PatchList
        && !rhi::hasMetalTessellationAbi(reflection, desc.patchControlPointCount)) {
        return failPipeline(report, "RHI-METAL-PATCH-LIST-TESSELLATION-ABI-NOT-LOWERED");
    }
    const auto* reflectedFragment = findReflectedEntrypoint(metalBindingSpace, desc.fragmentShader);
    if (!reflectedFragment) {
        return failPipeline(report, "RHI-METAL-FRAGMENT-ENTRYPOINT-MISSING-FROM-REFLECTION");
    }
    report.selectedFragmentWritesRenderTargetArrayIndex =
        reflectedFragment->writesRenderTargetArrayIndex;
    const auto descriptorLowering = validateMetalDirectDescriptorLowering(
        desc.layout->desc(), desc.shaderPackage->reflection, metalBindingSpace);
    if (!descriptorLowering.success) {
        return failPipeline(report, descriptorLowering.diagnostics);
    }
    const auto vertexStreams = lowerVertexStreams(desc, metalBindingSpace);
    if (!vertexStreams.success) {
        return failPipeline(report, vertexStreams.diagnostics);
    }
    const auto optionalLowerings = validateMetalOptionalLowerings(
        capabilityContract, desc.layout->desc(), desc.shaderPackage->reflection);
    if (!optionalLowerings.success) {
        return failPipeline(report, optionalLowerings.diagnostics);
    }
    if (!packageContainsEntrypoint(*desc.shaderPackage, desc.vertexShader)
        || !packageContainsEntrypoint(*desc.shaderPackage, desc.fragmentShader)) {
        return failPipeline(report, "Metal RHI shader package is missing render entrypoint");
    }
    if (desc.topology == rhi::PrimitiveTopology::PatchList) {
        if (!packageContainsEntrypoint(*desc.shaderPackage, desc.tessellationControlShader)
            || !packageContainsEntrypoint(*desc.shaderPackage, desc.tessellationEvaluationShader)) {
            return failPipeline(
                report, "Metal RHI shader package is missing tessellation entrypoint");
        }
        const auto generatedVertex = std::find_if(metalBindingSpace.reflectedEntryPoints.begin(),
            metalBindingSpace.reflectedEntryPoints.end(),
            [&](const auto& entrypoint) {
                return entrypoint.name == reflection.metalTessellationVertexEntryPoint
                    && entrypoint.stage == shader::ShaderStage::Vertex;
            });
        if (generatedVertex == metalBindingSpace.reflectedEntryPoints.end()) {
            return failPipeline(
                report, "Metal RHI tessellation ABI generated vertex entrypoint is missing");
        }
    }
    const auto pushPlan =
        rhi::semantics::buildPushConstantLoweringPlan(rhi::Backend::Metal, capabilityContract);
    if (desc.shaderPackage->reflection.pushConstantByteCount > 0u
        && !pushPlan.hasExecutablePayload) {
        return failPipeline(report,
            rhi::semantics::PushConstantDiagCode::kNoExecPayload
                + std::pmr::string(
                    "Metal RHI render pipeline uses push constants but the selected ")
                + "Metal lowering has no executable payload");
    }
    if (desc.shaderPackage->reflection.pushConstantByteCount > 0u
        && (desc.shaderPackage->reflection.pushConstantMetalBufferIndex == UINT32_MAX
            || desc.shaderPackage->reflection.pushConstantMetalBufferIndex >= 31u)) {
        return failPipeline(report,
            rhi::semantics::PushConstantDiagCode::kReflectionContract
                + std::pmr::string(
                    "Metal RHI render pipeline is missing reflected push-constant buffer slot"));
    }
    if (desc.shaderPackage->reflection.pushConstantByteCount
        > desc.layout->pushConstantByteCount()) {
        return failPipeline(report,
            rhi::semantics::PushConstantDiagCode::kSizeMismatch
                + std::pmr::string("Metal RHI push constants exceed pipeline layout"));
    }
    const auto state = rhi::validateRenderPipelineState(desc);
    copyStateReport(report, state);
    if (!state.success) {
        return failPipeline(report, state.diagnostics);
    }
    if (!rhi::semantics::sampleMaskHasFullCoverage(
            desc.multisample.sampleCount, desc.multisample.sampleMask)) {
        return failPipeline(report, "RHI-METAL-SAMPLE-MASK-SHADER-LOWERING-REQUIRED");
    }
    if (desc.renderPassCompatibility.enabled()) {
        const auto& subpass = desc.renderPassCompatibility.subpasses[desc.subpassIndex];
        for (std::uint32_t targetIndex = 0; targetIndex < subpass.colorAttachmentIndices.size();
            ++targetIndex) {
            if (subpass.colorAttachmentIndices[targetIndex] != targetIndex) {
                return failPipeline(
                    report, "RHI-METAL-PIPELINE-COLOR-ATTACHMENT-MAPPING-NOT-LOWERED");
            }
        }
        for (const auto& binding : desc.shaderPackage->reflection.descriptorBindings) {
            if (binding.type != rhi::DescriptorType::InputAttachment) {
                continue;
            }
            if (binding.depthTexture) {
                if (!subpass.readDepthInputAttachment) {
                    return failPipeline(
                        report, "RHI-METAL-PIPELINE-DEPTH-FETCH-NOT-IN-CURRENT-SUBPASS");
                }
                continue;
            }
            const auto found = std::find(subpass.inputAttachmentIndices.begin(),
                subpass.inputAttachmentIndices.end(),
                binding.subpassAttachmentIndex);
            if (found == subpass.inputAttachmentIndices.end()) {
                return failPipeline(report, "RHI-METAL-PIPELINE-FETCH-NOT-IN-CURRENT-SUBPASS");
            }
        }
    } else {
        for (const auto& binding : desc.shaderPackage->reflection.descriptorBindings) {
            if (binding.type == rhi::DescriptorType::InputAttachment) {
                return failPipeline(
                    report, "RHI-METAL-INPUT-ATTACHMENT-REQUIRES-SUBPASS-COMPATIBILITY");
            }
        }
    }
    report.reflectionMatched = true;
    report.success = true;
    return report;
}

rhi::PipelineValidationReport validateMetalRhiRenderPipeline(const rhi::RenderPipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
{
    return validateMetalRhiRenderPipelineImpl(desc, capabilityContract, nullptr);
}

static rhi::PipelineValidationReport validateMetalRhiComputePipelineImpl(
    const rhi::ComputePipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    bool authorizedBindless,
    const shader::ShaderReflection* verifiedPackageReflection)
{
    auto report = makePipelineReport(desc.layout, desc.shaderPackage, 1);
    if (!desc.layout) {
        return failPipeline(report, "Metal RHI compute pipeline requires layout");
    }
    if (!desc.shaderPackage) {
        return failPipeline(report, "Metal RHI compute pipeline requires shader package");
    }
    const auto packageValidation = authorizedBindless
        ? rhi::validateAuthorizedBindlessShaderPackage(*desc.shaderPackage)
        : rhi::validateShaderPackage(*desc.shaderPackage);
    if (!packageValidation.success) {
        return failPipeline(report, packageValidation.diagnostics);
    }
    report.shaderPackageValidated = true;
    const auto metalBindingSpace =
        loadMetalBindingSpace(*desc.shaderPackage, verifiedPackageReflection);
    const auto descriptorLowering = validateMetalDirectDescriptorLowering(desc.layout->desc(),
        desc.shaderPackage->reflection,
        metalBindingSpace,
        authorizedBindless ? verifiedPackageReflection : nullptr);
    if (!descriptorLowering.success) {
        return failPipeline(report, descriptorLowering.diagnostics);
    }
    const auto optionalLowerings = validateMetalOptionalLowerings(
        capabilityContract, desc.layout->desc(), desc.shaderPackage->reflection);
    if (!optionalLowerings.success) {
        return failPipeline(report, optionalLowerings.diagnostics);
    }
    if (!packageContainsEntrypoint(*desc.shaderPackage, desc.computeShader)) {
        return failPipeline(report, "Metal RHI shader package is missing compute entrypoint");
    }
    const auto* computeEntrypoint =
        findMetalComputeEntrypoint(*desc.shaderPackage, desc.computeShader);
    const auto* reflectedCompute = findReflectedEntrypoint(metalBindingSpace, desc.computeShader);
    if (!computeEntrypoint || !reflectedCompute || reflectedCompute->computeWorkgroupSizeX == 0
        || reflectedCompute->computeWorkgroupSizeY == 0
        || reflectedCompute->computeWorkgroupSizeZ == 0) {
        return failPipeline(report,
            "[CS-E011] Metal RHI compute pipeline is missing reflected workgroup dimensions");
    }
    report.selectedComputeWorkgroupSizeX = reflectedCompute->computeWorkgroupSizeX;
    report.selectedComputeWorkgroupSizeY = reflectedCompute->computeWorkgroupSizeY;
    report.selectedComputeWorkgroupSizeZ = reflectedCompute->computeWorkgroupSizeZ;
    const auto pushPlan =
        rhi::semantics::buildPushConstantLoweringPlan(rhi::Backend::Metal, capabilityContract);
    if (desc.shaderPackage->reflection.pushConstantByteCount > 0u
        && !pushPlan.hasExecutablePayload) {
        return failPipeline(report,
            rhi::semantics::PushConstantDiagCode::kNoExecPayload
                + std::pmr::string(
                    "Metal RHI compute pipeline uses push constants but the selected ")
                + "Metal lowering has no executable payload");
    }
    if (desc.shaderPackage->reflection.pushConstantByteCount > 0u
        && (desc.shaderPackage->reflection.pushConstantMetalBufferIndex == UINT32_MAX
            || desc.shaderPackage->reflection.pushConstantMetalBufferIndex >= 31u)) {
        return failPipeline(report,
            rhi::semantics::PushConstantDiagCode::kReflectionContract
                + std::pmr::string(
                    "Metal RHI compute pipeline is missing reflected push-constant buffer slot"));
    }
    if (desc.shaderPackage->reflection.pushConstantByteCount
        > desc.layout->pushConstantByteCount()) {
        return failPipeline(report,
            rhi::semantics::PushConstantDiagCode::kSizeMismatch
                + std::pmr::string("Metal RHI push constants exceed pipeline layout"));
    }
    report.reflectionMatched = true;
    report.pipelineCacheKey = rhi::stableShaderPackageHash(*desc.shaderPackage);
    report.pipelineCacheKeyStable = true;
    report.success = true;
    return report;
}

rhi::PipelineValidationReport validateMetalRhiComputePipeline(const rhi::ComputePipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
{
    return validateMetalRhiComputePipelineImpl(desc, capabilityContract, false, nullptr);
}

static rhi::PipelineValidationReport validateMetalRhiAuthorizedBindlessComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    const shader::ShaderReflection& verifiedPackageReflection)
{
    return validateMetalRhiComputePipelineImpl(
        desc, capabilityContract, true, &verifiedPackageReflection);
}

namespace {
template <class Desc, class Input>
const char* originalPipelineDefinitionDiagnostic(
    const Desc& desc, const std::shared_ptr<const Input>& input)
{
    if (const auto* id = Input::validateOriginal(input))
        return id;
    if (!desc.layout || !desc.shaderPackage)
        return "RHI-ORIGINAL-SYNC-PIPELINE-DEFINITION";
    if (const auto* id = Input::packageDiagnostic(input, *desc.shaderPackage))
        return id;
    const auto selected = Input::selectTarget(input, rhi::Backend::Metal);
    if (selected.diagnostic)
        return selected.diagnostic;
    const auto valid = rhi::validatePipelineLayoutAgainstShaderReflection(
        desc.layout->desc(), input->package().reflection);
    return valid.success ? nullptr : "RHI-ORIGINAL-SYNC-PIPELINE-LAYOUT-ABI";
}

template <class Input>
const char* originalPipelineCreationDiagnostic(const std::shared_ptr<const Input>& input,
    const rhi::semantics::NativeBackendCapabilityContract& caps,
    MetalOriginalPipeline::Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    std::size_t targetBytes)
{
    if (const auto* id = rhi::OriginalSynchronousPipelineCreationContract::validateBudget(budget))
        return id;
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return "RHI-ORIGINAL-SYNC-PIPELINE-QUOTA";
    if (quota.get() == input->quota().get() || quota.get() == input->artifact()->quota().get()
        || core::SharedOwnerProjection::sameControlBlock(quota, input->quota())
        || core::SharedOwnerProjection::sameControlBlock(quota, input->artifact()->quota()))
        return "RHI-ORIGINAL-SYNC-PIPELINE-QUOTA-DOMAIN";
    if (!targetBytes || targetBytes > budget.maxTargetBytes)
        return "RHI-ORIGINAL-SYNC-PIPELINE-TARGET-BUDGET";
    const auto decision =
        rhi::semantics::OriginalSynchronousPipelineCreationLowering::decide(rhi::Backend::Metal,
            {bool(metalRhiNativeDeviceIdentity()), caps.queriedFromNativeDevice, true});
    return decision.diagnostic;
}

bool originalPipelineNative(
    const std::shared_ptr<MetalRhiNativePipelinePayload>& native, bool graphics) noexcept
{
    return native && native->nativeRuntimeBacked && !native->nativeRuntimeDiagnosticOnly
        && !native->nativeHandleExposedToUpperLayer && native->nativeDevice
        && native->nativeDevice == metalRhiNativeDeviceIdentity() && native->shaderLibrary
        && native->shaderModulePayloadPresent && native->pipelineState
        && (graphics ? native->renderPipelinePayloadPresent && native->vertexFunction
                    && native->fragmentFunction && !native->computePipelinePayloadPresent
                     : native->computePipelinePayloadPresent && native->computeFunction
                    && !native->renderPipelinePayloadPresent);
}
} // namespace

rhi::PipelineValidationReport MetalOriginalPipeline::validate(const rhi::RenderPipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
    const rhi::semantics::NativeBackendCapabilityContract& caps)
{
    auto report = makePipelineReport(desc.layout, desc.shaderPackage, 2);
    if (caps.backend != rhi::Backend::Metal)
        return failPipeline(report, "RHI-ORIGINAL-SYNC-PIPELINE-CAPABILITY-BACKEND");
    if (const auto* id = originalPipelineDefinitionDiagnostic(desc, input))
        return failPipeline(report, id);
    const auto& entries = input->package().entrypoints;
    if (entries.size() != 2
        || !rhi::PipelineDefinitionIdentity::matches(desc.vertexShader, entries[0])
        || !rhi::PipelineDefinitionIdentity::matches(desc.fragmentShader, entries[1])
        || desc.topology == rhi::PrimitiveTopology::PatchList)
        return failPipeline(report, "RHI-ORIGINAL-SYNC-PIPELINE-GRAPHICS-ENTRY");
    const auto lowered = rhi::semantics::lowerRenderPipelineState(caps, desc);
    if (!lowered.success)
        return failPipeline(report, lowered.diagnostics);
    return validateMetalRhiRenderPipelineImpl(desc, caps, &input->artifact()->module().reflection);
}

rhi::PipelineValidationReport MetalOriginalPipeline::validate(const rhi::ComputePipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input,
    const rhi::semantics::NativeBackendCapabilityContract& caps)
{
    auto report = makePipelineReport(desc.layout, desc.shaderPackage, 1);
    if (caps.backend != rhi::Backend::Metal)
        return failPipeline(report, "RHI-ORIGINAL-SYNC-PIPELINE-CAPABILITY-BACKEND");
    if (const auto* id = originalPipelineDefinitionDiagnostic(desc, input))
        return failPipeline(report, id);
    const auto& entries = input->package().entrypoints;
    if (input->artifact()->stage() != shader::ShaderStage::Compute || entries.size() != 1
        || !rhi::PipelineDefinitionIdentity::matches(desc.computeShader, entries[0]))
        return failPipeline(report, "RHI-ORIGINAL-SYNC-PIPELINE-COMPUTE-ENTRY");
    const auto lowered = rhi::semantics::lowerComputePipelineState(caps, desc);
    if (!lowered.success)
        return failPipeline(report, lowered.diagnostics);
    // Ordinary original input is not bindless authorization.
    return validateMetalRhiComputePipelineImpl(
        desc, caps, false, &input->artifact()->module().reflection);
}

core::UniquePtr<rhi::RenderPipeline> MetalOriginalPipeline::create(
    const rhi::RenderPipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
    const rhi::semantics::NativeBackendCapabilityContract& caps,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& id)
{
    id = rhi::OriginalSynchronousPipelineCreationContract::validateBudget(budget);
    if (!id)
        id = rhi::OriginalGraphicsPipelineInput::validateOriginal(input);
    if (id)
        return {};
    const auto selected =
        rhi::OriginalGraphicsPipelineInput::selectTarget(input, rhi::Backend::Metal);
    if ((id = selected.diagnostic))
        return {};
    if (selected.sourceCount != 1 || !selected.combinedModule || !selected.sources[0]) {
        id = "RHI-ORIGINAL-SYNC-PIPELINE-TARGET";
        return {};
    }
    const auto& bytes = *selected.sources[0];
    if ((id = originalPipelineCreationDiagnostic(input, caps, budget, quota, bytes.size())))
        return {};
    const auto began = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::nanoseconds(budget.coldCreationTimeoutNanoseconds);
    if (timeout > std::chrono::steady_clock::time_point::max() - began) {
        id = "RHI-ORIGINAL-SYNC-PIPELINE-BUDGET";
        return {};
    }
    const auto deadline = began + timeout;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto report = validate(desc, input, caps);
        if (!report.success) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-VALIDATION";
            return {};
        }
        auto layout =
            createMetalRhiPipelineLayout(rhi::PipelineLayoutStateCopy::copy(desc.layout->desc()));
        auto interface = core::makeUnique<rhi::ShaderPackageDesc>(
            rhi::ShaderPackageStateCopy::copy(input->package()));
        auto state = rhi::RenderPipelineStateCopy::copy(desc);
        state.layout = layout.get();
        state.shaderPackage = interface.get();
        const auto mappings =
            loadMetalBindingSpace(*interface, &input->artifact()->module().reflection);
        const auto streams = lowerVertexStreams(state, mappings);
        if (!layout || !mappings.success || !streams.success) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-LOWERING";
            return {};
        }
        const std::string_view msl(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        auto native = createMetalRhiNativeRenderPipelinePayload(state, report, streams, msl, msl);
        if (!originalPipelineNative(native, true)) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-NATIVE-PAYLOAD";
            return {};
        }
        native->originalGraphicsInput_ = input;
        native->originalCreationQuota_ = quota;
        native->originalCreationQuotaBytes_ = budget.maxCpuMetadataBytes;
        if (std::chrono::steady_clock::now() >= deadline) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-COLD-DEADLINE";
            return {};
        }
        native->metalBindingSpace = mappings.slots;
        native->metalArgumentBuffer = mappings.argumentBuffers;
        native->metalFramebufferFetch = mappings.framebufferFetch;
        auto result = core::makeUnique<MetalRhiRenderPipeline>(std::move(state),
            std::move(report),
            std::move(native),
            rhi::semantics::DirectObjectTelemetryLease{},
            rhi::semantics::DirectObjectBudgetLease{},
            std::move(layout),
            std::move(interface),
            std::shared_ptr<const rhi::BindlessShaderPackage>{},
            std::shared_ptr<const rhi::BindlessAuthorization>{},
            rhi::BindlessGraphicsPipelineEntries{},
            input);
        if (std::chrono::steady_clock::now() >= deadline) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-COLD-DEADLINE";
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        id = "RHI-ORIGINAL-SYNC-PIPELINE-CPU-ALLOCATION";
        return {};
    }
}

core::UniquePtr<rhi::ComputePipeline> MetalOriginalPipeline::create(
    const rhi::ComputePipelineDesc& desc,
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input,
    const rhi::semantics::NativeBackendCapabilityContract& caps,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& id)
{
    id = rhi::OriginalSynchronousPipelineCreationContract::validateBudget(budget);
    if (!id)
        id = rhi::OriginalSingleEntryPipelineInput::validateOriginal(input);
    if (id)
        return {};
    const auto selected =
        rhi::OriginalSingleEntryPipelineInput::selectTarget(input, rhi::Backend::Metal);
    if ((id = selected.diagnostic))
        return {};
    if (!selected.bytes) {
        id = "RHI-ORIGINAL-SYNC-PIPELINE-TARGET";
        return {};
    }
    const auto& bytes = *selected.bytes;
    if ((id = originalPipelineCreationDiagnostic(input, caps, budget, quota, bytes.size())))
        return {};
    const auto began = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::nanoseconds(budget.coldCreationTimeoutNanoseconds);
    if (timeout > std::chrono::steady_clock::time_point::max() - began) {
        id = "RHI-ORIGINAL-SYNC-PIPELINE-BUDGET";
        return {};
    }
    const auto deadline = began + timeout;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto report = validate(desc, input, caps);
        if (!report.success) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-VALIDATION";
            return {};
        }
        auto layout =
            createMetalRhiPipelineLayout(rhi::PipelineLayoutStateCopy::copy(desc.layout->desc()));
        auto interface = core::makeUnique<rhi::ShaderPackageDesc>(
            rhi::ShaderPackageStateCopy::copy(input->package()));
        auto state = rhi::ComputePipelineStateCopy::copy(desc);
        state.layout = layout.get();
        state.shaderPackage = interface.get();
        const auto mappings =
            loadMetalBindingSpace(*interface, &input->artifact()->module().reflection);
        if (!layout || !mappings.success) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-LOWERING";
            return {};
        }
        const std::string_view msl(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        auto native = createMetalRhiNativeComputePipelinePayload(state, report, msl);
        if (!originalPipelineNative(native, false)) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-NATIVE-PAYLOAD";
            return {};
        }
        native->originalComputeInput_ = input;
        native->originalCreationQuota_ = quota;
        native->originalCreationQuotaBytes_ = budget.maxCpuMetadataBytes;
        if (std::chrono::steady_clock::now() >= deadline) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-COLD-DEADLINE";
            return {};
        }
        native->metalBindingSpace = mappings.slots;
        native->metalArgumentBuffer = mappings.argumentBuffers;
        native->metalFramebufferFetch = mappings.framebufferFetch;
        auto result = core::makeUnique<MetalRhiComputePipeline>(std::move(state),
            std::move(report),
            std::move(native),
            rhi::semantics::DirectObjectTelemetryLease{},
            rhi::semantics::DirectObjectBudgetLease{},
            std::shared_ptr<const rhi::BindlessAuthorization>{},
            std::move(layout),
            std::move(interface),
            input);
        if (std::chrono::steady_clock::now() >= deadline) {
            id = "RHI-ORIGINAL-SYNC-PIPELINE-COLD-DEADLINE";
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        id = "RHI-ORIGINAL-SYNC-PIPELINE-CPU-ALLOCATION";
        return {};
    }
}

const char* MetalOriginalPipeline::diagnostic(const MetalRhiNativePipelinePayload& native) noexcept
{
    if (bool(native.originalGraphicsInput_) == bool(native.originalComputeInput_))
        return "RHI-ORIGINAL-SYNC-PIPELINE-CUSTODY";
    if (!native.originalCreationQuota_
        || native.originalCreationQuota_->limitBytes() != native.originalCreationQuotaBytes_)
        return "RHI-ORIGINAL-SYNC-PIPELINE-QUOTA";
    const auto* id = native.originalGraphicsInput_
        ? rhi::OriginalGraphicsPipelineInput::validateOriginal(native.originalGraphicsInput_)
        : rhi::OriginalSingleEntryPipelineInput::validateOriginal(native.originalComputeInput_);
    if (id)
        return id;
    return native.nativeRuntimeBacked && !native.nativeRuntimeDiagnosticOnly && native.nativeDevice
            && native.nativeDevice == metalRhiNativeDeviceIdentity() && native.pipelineState
            && native.shaderModulePayloadPresent
        ? nullptr
        : "RHI-ORIGINAL-SYNC-PIPELINE-NATIVE-PAYLOAD";
}

std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> MetalOriginalPipeline::input(
    const rhi::RenderPipeline& pipeline) noexcept
{
    const auto* metal = dynamic_cast<const MetalRhiRenderPipeline*>(&pipeline);
    return metal ? metal->originalInput() : nullptr;
}

std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> MetalOriginalPipeline::input(
    const rhi::ComputePipeline& pipeline) noexcept
{
    const auto* metal = dynamic_cast<const MetalRhiComputePipeline*>(&pipeline);
    return metal ? metal->originalInput() : nullptr;
}

const char* MetalRhiZeroBaseIndirectPipeline::capture(const MetalRhiZeroBaseIndirectSource& source,
    const MetalRhiNativePipelinePayload& native,
    Facts& output) noexcept
{
    output = {};
    if (const auto* id = source.validate())
        return id;
    RoleContract::Facts roles;
    if (const auto* id = RoleContract::verify(
            *source.package(), source.content(), RoleContract::selectedEntry, roles))
        return id;
    if (roles.execution != rhi::BindlessExecution::Emulated)
        return "RHI-METAL-ZERO-BASE-PIPELINE-SCALAR-PACKAGE-REQUIRED";
    if (!native.nativeRuntimeBacked || native.nativeRuntimeDiagnosticOnly
        || native.nativeHandleExposedToUpperLayer || !native.computePipelinePayloadPresent
        || !native.shaderModulePayloadPresent || native.renderPipelinePayloadPresent
        || !native.nativeDevice || !native.computeFunction || !native.shaderLibrary
        || !native.pipelineState || native.vertexFunction || native.fragmentFunction
        || native.fragmentShaderLibrary || native.depthStencilState)
        return "RHI-METAL-ZERO-BASE-PIPELINE-NATIVE-PAYLOAD-MISSING";
    if (!core::SharedOwnerProjection::sameOwner(
            native.nativeDevice, source.index()->provider()->native()->nativeDevice))
        return "RHI-METAL-ZERO-BASE-PIPELINE-DEVICE-MISMATCH";
    if (native.computeWorkgroupSizeX != 1u || native.computeWorkgroupSizeY != 1u
        || native.computeWorkgroupSizeZ != 1u)
        return "RHI-METAL-ZERO-BASE-PIPELINE-WORKGROUP";
    if (native.metalBindingSpace.size() != RoleContract::bindingCount
        || !native.metalArgumentBuffer.empty() || !native.metalFramebufferFetch.empty()
        || native.computeBufferFacts.size() != RoleContract::bindingCount)
        return "RHI-METAL-ZERO-BASE-PIPELINE-MAPPING";
    Facts captured{};
    // Specified MTLBindingAccessReadOnly=0, ReadWrite=1, MTLDataTypeUInt=33
    // and Struct=1. Exact retained Slang uint4 uniform has a 16-byte struct ABI.
    // Slang lowers StructuredBuffer<uint> to device uint*, with a ReadWrite
    // native pointer ABI even for L2 read-only roles. The compiler-owned closed
    // algorithm and sealed source/metadata bindings retain their read-only
    // semantics; this ABI fact never permits a writable descriptor or dispatch.
    constexpr std::uint32_t readOnlyAccess = 0u, readWriteAccess = 1u, uintType = 33u,
                            structType = 1u;
    for (std::size_t i = 0u; i < roles.bindings.size(); ++i) {
        const auto& role = roles.bindings[i];
        const MetalRhiNativeBindingSlot* mapped = nullptr;
        for (const auto& candidate : native.metalBindingSpace)
            if (candidate.set == role.set && candidate.binding == role.binding) {
                if (mapped)
                    return "RHI-METAL-ZERO-BASE-PIPELINE-MAPPING";
                mapped = &candidate;
            }
        if (!mapped || !mapped->active || mapped->type != role.type
            || native.nativeBindingStages.stages(mapped->type, mapped->metalSlot)
                != rhi::ShaderStage::Compute)
            return "RHI-METAL-ZERO-BASE-PIPELINE-MAPPING";
        for (std::size_t previous = 0u; previous < i; ++previous)
            if (captured[previous].slot == mapped->metalSlot)
                return "RHI-METAL-ZERO-BASE-PIPELINE-MAPPING";
        const bool uniform = role.type == rhi::DescriptorType::UniformBuffer;
        const MetalRhiBufferBindingFact* fact = nullptr;
        if (const auto* id = MetalRhiScalarBufferAbiValidator::validate(native.computeBufferFacts,
                native.computeBufferFactsVersion,
                mapped->metalSlot,
                role.requiredBytes,
                uniform ? readOnlyAccess : readWriteAccess,
                uniform ? structType : uintType,
                uniform ? sizeof(rhi::ZeroBaseIndirectProducerParams) : sizeof(std::uint32_t),
                fact,
                "RHI-METAL-ZERO-BASE-PIPELINE-ABI-DOMAIN"))
            return id;
        if (fact->alignment
            != (uniform ? sizeof(rhi::ZeroBaseIndirectProducerParams) : sizeof(std::uint32_t)))
            return "RHI-METAL-ZERO-BASE-PIPELINE-ABI-DOMAIN";
        captured[i] = *fact;
    }
    output = captured;
    return nullptr;
}

const char* MetalRhiZeroBaseIndirectPipeline::validate() const noexcept
{
    if (!source_ || !source_->sharedOwnerMatches(source_) || !native_ || !interface_ || !layout_)
        return "RHI-METAL-ZERO-BASE-PIPELINE-OWNER-MISMATCH";
    if (budget_.contractVersion != 1u || !budget_.prototypeOptIn || !budget_.maxChildOwners)
        return "RHI-METAL-ZERO-BASE-PIPELINE-BUDGET";
    Facts current;
    if (const auto* id = capture(*source_, *native_, current))
        return id;
    for (std::size_t i = 0u; i < facts_.size(); ++i)
        if (!current[i].matches(facts_[i]))
            return "RHI-METAL-ZERO-BASE-PIPELINE-ABI-CAPTURE-MISMATCH";
    if (owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-ZERO-BASE-PIPELINE-CHILD-OWNER-BUDGET";
    return owners_.matches([&](auto&& visit) {
        projectOwners(visit);
    })
        ? nullptr
        : "RHI-METAL-ZERO-BASE-PIPELINE-OWNER-MISMATCH";
}

std::shared_ptr<const MetalRhiZeroBaseIndirectPipeline> MetalRhiZeroBaseIndirectPipeline::prepare(
    std::shared_ptr<const MetalRhiZeroBaseIndirectSource> source,
    const rhi::semantics::NativeBackendCapabilityContract& capability,
    const MetalRhiOrdinaryBindingBudget& cpuBudget,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](core::StringView id) -> std::shared_ptr<const MetalRhiZeroBaseIndirectPipeline> {
        diagnostic.assign(id.data(), id.size());
        return {};
    };
    if (!source || !source->sharedOwnerMatches(source))
        return fail("RHI-METAL-ZERO-BASE-PIPELINE-OWNER-MISMATCH");
    if (const auto* id = source->validate())
        return fail(id);
    if (const auto* id = cpuBudget.validate(source->quota()))
        return fail(id);
    if (budget.contractVersion != 1u || !budget.prototypeOptIn || !budget.maxChildOwners)
        return fail("RHI-METAL-ZERO-BASE-PIPELINE-BUDGET");
    // One source root plus its projection and exactly five Compute owners:
    // native payload/device/library/function/pipeline state. No Render owners.
    if (1u + source->childOwnerCount() + MetalRhiPreparedFunctionOwnership::computeOwnerCount
        > budget.maxChildOwners)
        return fail("RHI-METAL-ZERO-BASE-PIPELINE-CHILD-OWNER-BUDGET");
    if (capability.backend != rhi::Backend::Metal)
        return fail("RHI-METAL-ZERO-BASE-PIPELINE-BACKEND");
    if (source->package()->artifact().execution != rhi::BindlessExecution::Emulated)
        return fail("RHI-METAL-ZERO-BASE-PIPELINE-SCALAR-PACKAGE-REQUIRED");
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        auto interface = core::makeUnique<rhi::ShaderPackageDesc>();
        const rhi::BindlessPackageModule* module = nullptr;
        if (!rhi::appendBindlessShaderEntry(*source->package(),
                RoleContract::selectedEntry,
                rhi::ShaderStage::Compute,
                *interface,
                diagnostic,
                &module))
            return {};
        rhi::PipelineLayoutDesc authored;
        const auto& artifact = source->package()->artifact();
        authored.descriptorLayouts.push_back(artifact.physicalLayout);
        authored.descriptorSets.push_back({artifact.set, artifact.physicalLayout.name});
        auto layout = createMetalRhiPipelineLayout(authored);
        if (!layout)
            return fail("RHI-METAL-ZERO-BASE-PIPELINE-LAYOUT");
        rhi::ComputePipelineDesc desc;
        desc.label = "CanonicalZeroBaseIndirectProducer";
        desc.layout = layout.get();
        desc.shaderPackage = interface.get();
        desc.computeShader = interface->entrypoints.front();
        const auto& reflection = module->reflection.reflection;
        const auto report =
            validateMetalRhiAuthorizedBindlessComputePipeline(desc, capability, reflection);
        if (!report.success)
            return fail(report.diagnostics);
        const auto mapped = loadMetalBindingSpace(*interface, &reflection);
        if (!mapped.success)
            return fail(mapped.diagnostics);
        const auto& msl = source->package()->files()[module->metalFile].bytes;
        auto native = createMetalRhiNativeComputePipelinePayload(
            desc, report, core::StringView(reinterpret_cast<const char*>(msl.data()), msl.size()));
        if (!native || !native->nativeRuntimeBacked || native->nativeRuntimeDiagnosticOnly)
            return fail("RHI-METAL-ZERO-BASE-PIPELINE-NATIVE-PAYLOAD-MISSING");
        native->metalBindingSpace = mapped.slots;
        native->metalArgumentBuffer = mapped.argumentBuffers;
        native->metalFramebufferFetch = mapped.framebufferFetch;
        Facts facts;
        if (const auto* id = capture(*source, *native, facts)) {
            diagnostic = id;
            for (const auto& observed : native->computeBufferFacts) {
                diagnostic += " [slot=";
                core::appendUnsigned(diagnostic, observed.slot);
                diagnostic += " alignment=";
                core::appendUnsigned(diagnostic, observed.alignment);
                diagnostic += " bytes=";
                core::appendUnsigned(diagnostic, observed.requiredBytes);
                diagnostic += " access=";
                core::appendUnsigned(diagnostic, observed.access);
                diagnostic += " element=";
                core::appendUnsigned(diagnostic, observed.elementType);
                diagnostic += ']';
            }
            return {};
        }
        auto result = core::makeShared<MetalRhiZeroBaseIndirectPipeline>(ConstructionKey{},
            source,
            std::move(interface),
            std::move(layout),
            std::move(native),
            facts,
            budget);
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ZERO-BASE-PIPELINE-ALLOCATION-FAILED");
    }
}

std::shared_ptr<const MetalRhiIndirectProducerPipeline> MetalRhiIndirectProducerPipeline::prepare(
    std::shared_ptr<const shader::SingleEntryShaderArtifact> artifact,
    const rhi::PipelineLayoutDesc& authored,
    const rhi::semantics::NativeBackendCapabilityContract& capability,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](core::StringView id) -> std::shared_ptr<const MetalRhiIndirectProducerPipeline> {
        diagnostic.assign(id.data(), id.size());
        return {};
    };
    if (const auto* id = budget.validate(quota))
        return fail(id);
    if (!artifact || artifact->stage() != shader::ShaderStage::Compute)
        return fail("RHI-METAL-INDIRECT-PRODUCER-ARTIFACT-STAGE");
    if (artifact->quota() != quota)
        return fail("RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    if (capability.backend != rhi::Backend::Metal)
        return fail("RHI-METAL-INDIRECT-PRODUCER-BACKEND");
    const auto& binary = artifact->bytes(shader::ShaderArtifactRole::Spirv);
    shader::ComputeIndirectProducerProof::Facts proof;
    if (const auto* id = shader::ComputeIndirectProducerProof::verify(
            binary.data(), binary.size(), artifact->entry(), proof))
        return fail(id);
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto interface = core::makeUnique<rhi::ShaderPackageDesc>();
        const auto converted = rhi::populateSingleEntryShaderInterface(*artifact, *interface);
        if (!converted.success)
            return fail(converted.diagnostics);
        rhi::GpuIndirectProducerFootprint footprint;
        footprint.set = proof.set;
        footprint.binding = proof.binding;
        footprint.words = proof.fixedWords;
        auto contract = core::makeUnique<rhi::GpuIndirectProducerContract>(
            authored, interface->reflection, footprint);
        if (const auto* id = contract->diagnostic())
            return fail(id);
        auto layout = createMetalRhiPipelineLayout(authored);
        if (!layout)
            return fail("RHI-METAL-INDIRECT-PRODUCER-LAYOUT");
        rhi::ComputePipelineDesc desc;
        desc.label = "PreparedIndirectProducer";
        desc.layout = layout.get();
        desc.shaderPackage = interface.get();
        desc.computeShader = interface->entrypoints.front();
        const auto& reflection = artifact->module().reflection;
        const auto report =
            validateMetalRhiComputePipelineImpl(desc, capability, false, &reflection);
        if (!report.success)
            return fail(report.diagnostics);
        if (const auto* id = shader::ComputeIndirectProducerProof::validateDispatch(
                report.selectedComputeWorkgroupSizeX,
                report.selectedComputeWorkgroupSizeY,
                report.selectedComputeWorkgroupSizeZ))
            return fail(id);
        const auto mapped = loadMetalBindingSpace(*interface, &reflection);
        if (!mapped.success)
            return fail(mapped.diagnostics);
        if (mapped.slots.size() != 1u || !mapped.argumentBuffers.empty()
            || !mapped.framebufferFetch.empty())
            return fail("RHI-METAL-INDIRECT-PRODUCER-MAPPING");
        const auto& mapping = mapped.slots.front();
        if (!mapping.active || mapping.set != proof.set || mapping.binding != proof.binding
            || mapping.type != rhi::DescriptorType::StorageBuffer)
            return fail("RHI-METAL-INDIRECT-PRODUCER-MAPPING");
        const auto& msl = artifact->bytes(shader::ShaderArtifactRole::Metal);
        auto native = createMetalRhiNativeComputePipelinePayload(
            desc, report, core::StringView(reinterpret_cast<const char*>(msl.data()), msl.size()));
        if (!native || !native->nativeRuntimeBacked || native->nativeRuntimeDiagnosticOnly)
            return fail("RHI-METAL-INDIRECT-PRODUCER-NATIVE-PAYLOAD-MISSING");
        native->metalBindingSpace = mapped.slots;
        const MetalRhiBufferBindingFact* fact = nullptr;
        if (const auto* id =
                MetalRhiWritableUint32BufferAbiValidator::validate(native->computeBufferFacts,
                    native->computeBufferFactsVersion,
                    mapping.metalSlot,
                    rhi::GpuIndirectProducerContract::footprintBytes,
                    fact))
            return fail(id);
        auto result = core::makeShared<MetalRhiIndirectProducerPipeline>(ConstructionKey{},
            std::move(artifact),
            std::move(interface),
            std::move(layout),
            std::move(contract),
            proof,
            std::move(native),
            *fact,
            quota);
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-INDIRECT-PRODUCER-ALLOCATION-FAILED");
    }
}

const char* MetalRhiIndirectProducerPipeline::validate() const noexcept
{
    if (!native_ || !native_->nativeRuntimeBacked || native_->nativeRuntimeDiagnosticOnly
        || !native_->computePipelinePayloadPresent || !native_->shaderModulePayloadPresent
        || native_->renderPipelinePayloadPresent || native_->nativeHandleExposedToUpperLayer
        || !native_->nativeDevice || !native_->shaderLibrary || !native_->computeFunction
        || !native_->pipelineState)
        return "RHI-METAL-INDIRECT-PRODUCER-NATIVE-PAYLOAD-MISSING";
    if (!contract_ || contract_->diagnostic() || !artifact_ || artifact_->quota() != quota_
        || proof_.contractVersion != 1u
        || proof_.byteCount != rhi::GpuIndirectProducerContract::footprintBytes || proof_.byteOffset
        || contract_->footprint().words != proof_.fixedWords)
        return "RHI-METAL-INDIRECT-PRODUCER-PROOF-MISMATCH";
    if (const auto* id =
            shader::ComputeIndirectProducerProof::validateDispatch(native_->computeWorkgroupSizeX,
                native_->computeWorkgroupSizeY,
                native_->computeWorkgroupSizeZ))
        return id;
    if (native_->metalBindingSpace.size() != 1u || !native_->metalArgumentBuffer.empty()
        || !native_->metalFramebufferFetch.empty())
        return "RHI-METAL-INDIRECT-PRODUCER-MAPPING";
    const auto& mapping = native_->metalBindingSpace.front();
    if (!mapping.active || mapping.set != proof_.set || mapping.binding != proof_.binding
        || mapping.metalSlot != fact_.slot || mapping.type != rhi::DescriptorType::StorageBuffer
        || native_->nativeBindingStages.stages(mapping.type, mapping.metalSlot)
            != rhi::ShaderStage::Compute)
        return "RHI-METAL-INDIRECT-PRODUCER-MAPPING";
    if (native_->computeBufferFacts.size() != 1u)
        return "RHI-METAL-INDIRECT-PRODUCER-ABI-COUNT";
    const MetalRhiBufferBindingFact* current = nullptr;
    if (const auto* id =
            MetalRhiWritableUint32BufferAbiValidator::validate(native_->computeBufferFacts,
                native_->computeBufferFactsVersion,
                fact_.slot,
                rhi::GpuIndirectProducerContract::footprintBytes,
                current))
        return id;
    if (current->alignment != fact_.alignment || current->requiredBytes != fact_.requiredBytes
        || current->access != fact_.access || current->elementType != fact_.elementType)
        return "RHI-METAL-INDIRECT-PRODUCER-ABI-CAPTURE-MISMATCH";
    return owners_.matches([&](auto&& visit) {
        projectOwners(visit);
    })
        ? nullptr
        : "RHI-METAL-INDIRECT-PRODUCER-OWNER-MISMATCH";
}

static bool appendMetalInactivePipelineMappings(const rhi::BindlessShaderArtifact& artifact,
    const shader::ShaderReflection& reflection,
    rhi::ShaderStage stage,
    core::pmr::Vector<MetalRhiNativeBindingSlot>& direct,
    const core::pmr::Vector<MetalRhiNativeArgumentBufferSlot>& arrays)
{
    const auto append = [&](const rhi::DescriptorLayoutDesc& layout, std::uint32_t set) {
        for (const auto& logical : layout.bindings) {
            const auto activeDirect =
                std::find_if(direct.begin(), direct.end(), [&](const auto& map) {
                    return map.set == set && map.binding == logical.binding && map.active;
                });
            const auto activeArray =
                std::find_if(arrays.begin(), arrays.end(), [&](const auto& map) {
                    return map.set == set && map.binding == logical.binding && map.active;
                });
            if (activeDirect != direct.end() || activeArray != arrays.end())
                continue;
            const auto inactiveDirect = std::find_if(reflection.metalBindingSpace.begin(),
                reflection.metalBindingSpace.end(),
                [&](const auto& map) {
                    return map.set == set && map.binding == logical.binding && !map.active;
                });
            const auto inactiveArray = std::find_if(reflection.metalArgumentBuffer.begin(),
                reflection.metalArgumentBuffer.end(),
                [&](const auto& map) {
                    return map.set == set && map.binding == logical.binding && !map.active;
                });
            if (inactiveDirect == reflection.metalBindingSpace.end()
                || ((logical.stages & stage) != rhi::ShaderStage::None
                    && inactiveArray == reflection.metalArgumentBuffer.end()))
                return false;
            // An authored inactive stage may have no argument-buffer interface.
            // Its explicit compiler inactive binding still proves absence.
            direct.push_back({set, logical.binding, UINT32_MAX, logical.type, false});
        }
        return true;
    };
    if (!append(artifact.physicalLayout, artifact.set))
        return false;
    for (const auto& domain : artifact.additionalDomains)
        if (!append(domain.layout, domain.set))
            return false;
    return true;
}

namespace {
rhi::ValidationResult lowerMetalRhiPreparedGraphicsStageReflection(
    const rhi::ShaderPackageDesc& interface,
    const shader::ShaderReflection& reflection,
    std::string_view entry,
    rhi::ShaderStage stage,
    MetalRhiPreparedGraphicsStageLayout& output)
{
    output = {};
    if ((stage != rhi::ShaderStage::Vertex && stage != rhi::ShaderStage::Fragment)
        || reflection.entryPoints.size() != 1u || reflection.entryPoints.front().name != entry
        || reflection.entryPoints.front().stage
            != (stage == rhi::ShaderStage::Vertex ? shader::ShaderStage::Vertex
                                                  : shader::ShaderStage::Fragment))
        return {false, "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH"};
    const auto mapping = loadMetalBindingSpace(interface, &reflection, false);
    if (!mapping.success) {
        rhi::ValidationResult failure;
        failure.diagnostics = mapping.diagnostics;
        return failure;
    }
    MetalRhiPreparedGraphicsStageLayout result;
    result.stage = stage;
    result.entry.assign(entry.data(), entry.size());
    result.direct = mapping.slots;
    result.arrays = mapping.argumentBuffers;
    result.pushSlot = reflection.pushConstantMetalBufferIndex;
    output = std::move(result);
    return {true, {}};
}
} // namespace

rhi::ValidationResult lowerMetalRhiPreparedGraphicsStage(
    const shader::SingleEntryShaderArtifact& artifact,
    std::string_view entry,
    rhi::ShaderStage stage,
    MetalRhiPreparedGraphicsStageLayout& output)
{
    output = {};
    if ((stage != rhi::ShaderStage::Vertex && stage != rhi::ShaderStage::Fragment)
        || artifact.entry() != entry
        || artifact.stage()
            != (stage == rhi::ShaderStage::Vertex ? shader::ShaderStage::Vertex
                                                  : shader::ShaderStage::Fragment))
        return {false, "RHI-METAL-ORDINARY-GRAPHICS-ENTRY-STAGE-MISMATCH"};
    try {
        rhi::ShaderPackageDesc interface;
        const auto converted = rhi::populateSingleEntryShaderInterface(artifact, interface);
        if (!converted.success)
            return converted;
        const auto& reflection = artifact.module().reflection;
        if (reflection.metalGpuCountedIndirectStatus == "Available"
            || reflection.metalTessellationStatus == "Available"
            || !reflection.metalFramebufferFetch.empty())
            return {false, "RHI-METAL-ORDINARY-GRAPHICS-SHADER-ABI-UNSUPPORTED"};
        // Inactive records are explicit per-entry target facts. Reject duplicates
        // before copying them; never create an inactive record from visibility.
        for (std::size_t i = 0u; i < reflection.metalBindingSpace.size(); ++i)
            for (std::size_t j = 0u; j < i; ++j)
                if (reflection.metalBindingSpace[i].set == reflection.metalBindingSpace[j].set
                    && reflection.metalBindingSpace[i].binding
                        == reflection.metalBindingSpace[j].binding)
                    return {false, "RHI-METAL-ORDINARY-GRAPHICS-MAPPING-DUPLICATE"};
        for (std::size_t i = 0u; i < reflection.metalArgumentBuffer.size(); ++i)
            for (std::size_t j = 0u; j < i; ++j)
                if (reflection.metalArgumentBuffer[i].set == reflection.metalArgumentBuffer[j].set
                    && reflection.metalArgumentBuffer[i].binding
                        == reflection.metalArgumentBuffer[j].binding)
                    return {false, "RHI-METAL-ORDINARY-GRAPHICS-MAPPING-DUPLICATE"};
        MetalRhiPreparedGraphicsStageLayout result;
        const auto mapped = lowerMetalRhiPreparedGraphicsStageReflection(
            interface, reflection, entry, stage, result);
        if (!mapped.success)
            return mapped;
        for (const auto& map : reflection.metalBindingSpace) {
            if (map.active)
                continue;
            result.direct.push_back(
                {map.set, map.binding, UINT32_MAX, metalDescriptorType(map.physicalType), false});
        }
        output = std::move(result);
        return {true, {}};
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return {false, "RHI-METAL-ORDINARY-GRAPHICS-LOWERING-ALLOCATION-FAILED"};
    }
}

const char* MetalRhiZeroBaseIndirectConsumerPipeline::capture(
    const MetalRhiZeroBaseIndirectSource& source,
    const rhi::BindlessShaderPackage& package,
    const Stages& stages,
    const rhi::VertexInputConsumptionContract& input,
    const MetalRhiNativePipelinePayload& native,
    Facts& output) noexcept
{
    output = {};
    if (const auto* id = source.validate())
        return id;
    Contract::Facts roles;
    if (const auto* id = Contract::verify(package, source.content(), roles))
        return id;
    if (roles.execution != rhi::BindlessExecution::Emulated)
        return "RHI-METAL-CLOSED-CONSUMER-SCALAR-PACKAGE-REQUIRED";
    if (!native.nativeRuntimeBacked || native.nativeRuntimeDiagnosticOnly
        || native.nativeHandleExposedToUpperLayer || !native.shaderModulePayloadPresent
        || !native.renderPipelinePayloadPresent || native.computePipelinePayloadPresent
        || !native.nativeDevice || !native.shaderLibrary || !native.fragmentShaderLibrary
        || !native.vertexFunction || !native.fragmentFunction || !native.pipelineState
        || !native.depthStencilState || native.computeFunction || !native.metalBindingSpace.empty()
        || !native.metalArgumentBuffer.empty() || !native.metalFramebufferFetch.empty())
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-PAYLOAD-MISSING";
    if (!core::SharedOwnerProjection::sameOwner(
            native.nativeDevice, source.index()->provider()->native()->nativeDevice))
        return "RHI-METAL-CLOSED-CONSUMER-DEVICE-MISMATCH";
    if (const auto* id = native.vertexInputFacts.validate(input, native.vertexStreams))
        return id;
    if (native.graphicsTextureFactsVersion != 1u
        || native.graphicsTextureFacts.size() != Contract::capacity)
        return "RHI-METAL-CLOSED-CONSUMER-TEXTURE-ABI";
    Facts captured;
    // Specified SDK MTLBindingAccessReadWrite=1; MTLDataTypeUInt=33,
    // UInt4=36, SDK TextureType2D=2, MTLDataTypeFloat=3. Slang device uint4*
    // ABI does not change the proven closed algorithm's readonly L2 mapping.
    constexpr std::uint32_t readWrite = 1u, uintType = 33u, uint4Type = 36u, texture2D = 2u,
                            floatType = 3u;
    for (std::size_t stageIndex = 0u; stageIndex < stages.size(); ++stageIndex) {
        const auto stage = stageIndex ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex;
        const auto entry = stageIndex ? Contract::fragmentEntry : Contract::vertexEntry;
        const auto& mapping = stages[stageIndex];
        const auto& reflection = package.modules()[stageIndex].reflection.reflection;
        if (mapping.stage != stage || mapping.entry != entry || !mapping.arrays.empty()
            || mapping.pushSlot != UINT32_MAX
            || mapping.direct.size() != (stageIndex ? roles.bindingCount - 1u : 1u))
            return "RHI-METAL-CLOSED-CONSUMER-STAGE-MAPPING";
        const auto& bufferFacts = native.graphicsBufferFacts[stageIndex];
        if (bufferFacts.stage != stage || bufferFacts.version != 1u)
            return "RHI-METAL-CLOSED-CONSUMER-BUFFER-ABI";
        if (bufferFacts.entries.size() != 2u)
            return "RHI-METAL-CLOSED-CONSUMER-EXTRA-BUFFER";
        if (!stageIndex) {
            if (native.vertexInputFacts.streams.size() != 1u)
                return "RHI-METAL-CLOSED-CONSUMER-VERTEX-INTERFACE";
            const auto& stream = native.vertexInputFacts.streams.front();
            const auto* authored = input.findStream(stream.slot);
            const MetalRhiBufferBindingFact* queried = nullptr;
            // Actual stage-in buffer is a readonly struct. Its required range
            // is the authored fetch footprint, not an inferred uint4 ABI.
            if (!authored)
                return "RHI-METAL-CLOSED-CONSUMER-VERTEX-INTERFACE";
            if (const auto* id = MetalRhiScalarBufferAbiValidator::validate(bufferFacts.entries,
                    bufferFacts.version,
                    stream.physicalSlot,
                    authored->fetchBytes,
                    0u,
                    1u,
                    authored->fetchBytes,
                    queried,
                    "RHI-METAL-CLOSED-CONSUMER-VERTEX-BUFFER-ABI"))
                return id;
            captured.vertexStreamBuffer = *queried;
        }
        for (const auto& fact : bufferFacts.entries) {
            bool admitted = false;
            for (const auto& map : mapping.direct)
                if (map.type != rhi::DescriptorType::SampledTexture && map.metalSlot == fact.slot)
                    admitted = true;
            if (!stageIndex)
                for (const auto& stream : native.vertexInputFacts.streams)
                    if (stream.physicalSlot == fact.slot)
                        admitted = true;
            if (!admitted)
                return "RHI-METAL-CLOSED-CONSUMER-EXTRA-BUFFER";
        }
        for (std::size_t i = 0u; i < roles.bindingCount; ++i) {
            const auto& role = roles.bindings[i];
            if (role.stages != stage)
                continue;
            const MetalRhiNativeBindingSlot* map = nullptr;
            for (const auto& candidate : mapping.direct)
                if (candidate.set == role.set && candidate.binding == role.binding) {
                    if (map)
                        return "RHI-METAL-CLOSED-CONSUMER-STAGE-MAPPING";
                    map = &candidate;
                }
            const auto* reflected = findMetalBinding(reflection, role.set, role.binding);
            if (!map || !map->active || map->type != role.type || !reflected || !reflected->active
                || reflected->metalSlot != map->metalSlot
                || metalDescriptorType(reflected->physicalType) != map->type
                || (native.nativeBindingStages.stages(map->type, map->metalSlot) & stage)
                    == rhi::ShaderStage::None)
                return "RHI-METAL-CLOSED-CONSUMER-STAGE-MAPPING";
            if (i < 3u) {
                if (!stageIndex)
                    for (const auto& stream : native.vertexInputFacts.streams)
                        if (stream.physicalSlot == map->metalSlot)
                            return "RHI-METAL-CLOSED-CONSUMER-VERTEX-RESOURCE-ALIAS";
                const MetalRhiBufferBindingFact* fact = nullptr;
                const auto minimum =
                    i == 0u ? sizeof(rhi::IndirectInstanceBase) : sizeof(std::uint32_t);
                if (const auto* id = MetalRhiScalarBufferAbiValidator::validate(bufferFacts.entries,
                        bufferFacts.version,
                        map->metalSlot,
                        role.requiredBytes,
                        readWrite,
                        i == 0u ? uint4Type : uintType,
                        minimum,
                        fact,
                        "RHI-METAL-CLOSED-CONSUMER-BUFFER-ABI"))
                    return id;
                // Alignment is an authoritative query fact. bufferAlignment
                // for device uint4* can be4 despite its16-byte element extent.
                // Offset0 passes the shared queried range/alignment validator;
                // exact captured alignment is checked on every observation.
                captured.buffers[i] = *fact;
            } else {
                const MetalRhiTextureBindingFact* fact = nullptr;
                for (const auto& candidate : native.graphicsTextureFacts)
                    if (candidate.stage == stage && candidate.slot == map->metalSlot
                        && candidate.argumentIndex == UINT32_MAX) {
                        if (fact)
                            return "RHI-METAL-CLOSED-CONSUMER-TEXTURE-ABI";
                        fact = &candidate;
                    }
                if (!fact || fact->textureType != texture2D || fact->scalarDataType != floatType
                    || fact->depth)
                    return "RHI-METAL-CLOSED-CONSUMER-TEXTURE-ABI";
                captured.textures[i - 3u] = *fact;
            }
        }
    }
    output = captured;
    return nullptr;
}

const char* MetalRhiZeroBaseIndirectConsumerPipeline::validate() const noexcept
{
    if (!source_ || !source_->sharedOwnerMatches(source_) || !package_
        || !package_->sharedOwnerMatches(package_) || !interface_ || !layout_ || !native_)
        return "RHI-METAL-CLOSED-CONSUMER-OWNER-MISMATCH";
    if (package_->quota() != source_->quota() || source_->package()->quota() != source_->quota())
        return "RHI-METAL-CLOSED-CONSUMER-QUOTA-MISMATCH";
    if (budget_.contractVersion != 1u || !budget_.prototypeOptIn || !budget_.maxChildOwners)
        return "RHI-METAL-CLOSED-CONSUMER-BUDGET";
    Facts current;
    if (!input_ || !input_->sharedOwnerMatches(input_))
        return "RHI-METAL-CLOSED-CONSUMER-VERTEX-INPUT-ORIGINAL-OWNER";
    if (const auto* id = capture(*source_, *package_, stages_, *input_, *native_, current))
        return id;
    for (std::size_t i = 0u; i < facts_.buffers.size(); ++i)
        if (!current.buffers[i].matches(facts_.buffers[i]))
            return "RHI-METAL-CLOSED-CONSUMER-ABI-CAPTURE-MISMATCH";
    if (!current.vertexStreamBuffer.matches(facts_.vertexStreamBuffer))
        return "RHI-METAL-CLOSED-CONSUMER-ABI-CAPTURE-MISMATCH";
    for (std::size_t i = 0u; i < facts_.textures.size(); ++i) {
        const auto& a = current.textures[i];
        const auto& b = facts_.textures[i];
        if (!a.matches(b))
            return "RHI-METAL-CLOSED-CONSUMER-ABI-CAPTURE-MISMATCH";
    }
    if (native_->vertexStreams.slots != streams_.slots
        || native_->vertexStreams.success != streams_.success
        || !(native_->nativeBindingStages == stageFacts_)
        || !(native_->vertexInputFacts == vertexFacts_) || !state_.matches(*native_))
        return "RHI-METAL-CLOSED-CONSUMER-STATE-CAPTURE-MISMATCH";
    if (owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-CLOSED-CONSUMER-CHILD-OWNER-BUDGET";
    return owners_.matches([&](auto&& visit) {
        projectOwners(visit);
    })
        ? nullptr
        : "RHI-METAL-CLOSED-CONSUMER-OWNER-MISMATCH";
}

std::shared_ptr<const MetalRhiZeroBaseIndirectConsumerPipeline>
MetalRhiZeroBaseIndirectConsumerPipeline::prepare(
    std::shared_ptr<const MetalRhiZeroBaseIndirectSource> source,
    std::shared_ptr<const rhi::BindlessShaderPackage> package,
    const rhi::RenderPipelineDesc& state,
    const rhi::semantics::NativeBackendCapabilityContract& capability,
    const MetalRhiOrdinaryBindingBudget& cpuBudget,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail = [&](core::StringView id)
        -> std::shared_ptr<const MetalRhiZeroBaseIndirectConsumerPipeline> {
        diagnostic.assign(id.data(), id.size());
        return {};
    };
    if (!source || !source->sharedOwnerMatches(source) || !package
        || !package->sharedOwnerMatches(package))
        return fail("RHI-METAL-CLOSED-CONSUMER-OWNER-MISMATCH");
    if (const auto* id = source->validate())
        return fail(id);
    if (const auto* id = cpuBudget.validate(source->quota()))
        return fail(id);
    if (package->quota() != source->quota() || source->package()->quota() != source->quota())
        return fail("RHI-METAL-CLOSED-CONSUMER-QUOTA-MISMATCH");
    if (budget.contractVersion != 1u || !budget.prototypeOptIn || !budget.maxChildOwners)
        return fail("RHI-METAL-CLOSED-CONSUMER-BUDGET");
    if (3u + source->childOwnerCount() + MetalRhiPreparedFunctionOwnership::renderOwnerCount
        > budget.maxChildOwners)
        return fail("RHI-METAL-CLOSED-CONSUMER-CHILD-OWNER-BUDGET");
    if (capability.backend != rhi::Backend::Metal)
        return fail("RHI-METAL-CLOSED-CONSUMER-BACKEND");
    Contract::Facts roles;
    if (const auto* id = Contract::verify(*package, source->content(), roles))
        return fail(id);
    if (roles.execution != rhi::BindlessExecution::Emulated)
        return fail("RHI-METAL-CLOSED-CONSUMER-SCALAR-PACKAGE-REQUIRED");
    const auto authored = rhi::validateBindlessAuthoredRenderState(state, true);
    if (!authored.success)
        return fail(authored.diagnostics);
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        auto interface = core::makeUnique<rhi::ShaderPackageDesc>();
        std::array<const rhi::BindlessPackageModule*, 2u> modules{};
        if (!rhi::appendBindlessShaderEntry(*package,
                Contract::vertexEntry,
                rhi::ShaderStage::Vertex,
                *interface,
                diagnostic,
                &modules[0])
            || !rhi::appendBindlessShaderEntry(*package,
                Contract::fragmentEntry,
                rhi::ShaderStage::Fragment,
                *interface,
                diagnostic,
                &modules[1]))
            return {};
        const auto selected = rhi::validateAuthorizedBindlessShaderPackage(*interface);
        if (!selected.success)
            return fail(selected.diagnostics);
        rhi::PipelineLayoutDesc layoutDesc;
        layoutDesc.descriptorLayouts.push_back(package->artifact().physicalLayout);
        layoutDesc.descriptorSets.push_back(
            {package->artifact().set, package->artifact().physicalLayout.name});
        const auto reflected =
            rhi::validatePipelineLayoutAgainstShaderReflection(layoutDesc, interface->reflection);
        if (!reflected.success)
            return fail(reflected.diagnostics);
        const auto optional =
            validateMetalOptionalLowerings(capability, layoutDesc, interface->reflection);
        if (!optional.success)
            return fail(optional.diagnostics);
        auto layout = createMetalRhiPipelineLayout(layoutDesc);
        if (!layout)
            return fail("RHI-METAL-CLOSED-CONSUMER-LAYOUT");
        auto desc = rhi::RenderPipelineStateCopy::copy(state);
        desc.layout = layout.get();
        desc.shaderPackage = interface.get();
        desc.vertexShader = interface->entrypoints[0];
        desc.fragmentShader = interface->entrypoints[1];
        rhi::VertexInputConsumptionContract input(desc.vertexBuffers);
        if (const auto* id = input.diagnostic())
            return fail(id);
        if (input.attributes().size() != 1u || input.streams().size() != 1u
            || !input.findAttribute(0u)
            || input.findAttribute(0u)->layout.format != rhi::VertexFormat::Float32x2)
            return fail("RHI-METAL-CLOSED-CONSUMER-VERTEX-INTERFACE");
        const auto validated = rhi::validateRenderPipelineState(desc);
        if (!validated.success)
            return fail(validated.diagnostics);
        Stages stages;
        for (std::size_t i = 0u; i < stages.size(); ++i) {
            rhi::ShaderPackageDesc selectedInterface;
            if (!rhi::appendBindlessShaderEntry(*package,
                    i ? Contract::fragmentEntry : Contract::vertexEntry,
                    i ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex,
                    selectedInterface,
                    diagnostic))
                return {};
            const auto lowered = lowerMetalRhiPreparedGraphicsStageReflection(selectedInterface,
                modules[i]->reflection.reflection,
                i ? Contract::fragmentEntry : Contract::vertexEntry,
                i ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex,
                stages[i]);
            if (!lowered.success)
                return fail(lowered.diagnostics);
        }
        MetalBindingSpaceLoadResult vertexMapping;
        vertexMapping.success = true;
        vertexMapping.slots = stages[0].direct;
        vertexMapping.argumentBuffers = stages[0].arrays;
        const auto streams = lowerVertexStreams(desc, vertexMapping);
        if (!streams.success)
            return fail(streams.diagnostics);
        auto report = makePipelineReport(layout.get(), interface.get(), 2u);
        copyStateReport(report, validated);
        report.selectedFragmentWritesRenderTargetArrayIndex =
            modules[1]->reflection.reflection.entryPoints.front().writesRenderTargetArrayIndex;
        rhi::ShaderStage selectedStages;
        const auto selection = validateMetalRhiGraphicsStageSelection(desc, report, selectedStages);
        if (!selection.success)
            return fail(selection.diagnostics);
        report.shaderPackageValidated = true;
        report.reflectionMatched = true;
        report.success = true;
        const auto msl = [&](std::size_t i) -> core::StringView {
            const auto& bytes = package->files()[modules[i]->metalFile].bytes;
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        };
        auto native =
            createMetalRhiNativeRenderPipelinePayload(desc, report, streams, msl(0u), msl(1u));
        if (!native || !native->nativeRuntimeBacked || native->nativeRuntimeDiagnosticOnly)
            return fail(native && !native->diagnostics.empty()
                    ? core::StringView(native->diagnostics)
                    : core::StringView("RHI-METAL-CLOSED-CONSUMER-NATIVE-PAYLOAD-MISSING"));
        Facts facts;
        if (const auto* id = capture(*source, *package, stages, input, *native, facts)) {
            diagnostic = id;
            for (const auto& stage : native->graphicsBufferFacts)
                for (const auto& fact : stage.entries) {
                    diagnostic += " [slot=";
                    core::appendUnsigned(diagnostic, fact.slot);
                    diagnostic += " alignment=";
                    core::appendUnsigned(diagnostic, fact.alignment);
                    diagnostic += " bytes=";
                    core::appendUnsigned(diagnostic, fact.requiredBytes);
                    diagnostic += " access=";
                    core::appendUnsigned(diagnostic, fact.access);
                    diagnostic += " element=";
                    core::appendUnsigned(diagnostic, fact.elementType);
                    diagnostic += ']';
                }
            return {};
        }
        auto result = core::makeShared<MetalRhiZeroBaseIndirectConsumerPipeline>(ConstructionKey{},
            source,
            package,
            std::move(interface),
            std::move(layout),
            std::move(stages),
            std::move(input),
            std::move(native),
            facts,
            budget);
        if (const auto* id = result->validate())
            return fail(id);
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-CLOSED-CONSUMER-ALLOCATION-FAILED");
    }
}

std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> MetalRhiOrdinaryGraphicsProvider::create(
    std::shared_ptr<const shader::SingleEntryShaderArtifact> vertex,
    std::shared_ptr<const shader::SingleEntryShaderArtifact> fragment,
    rhi::RenderPipelineDesc&& authored,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    const MetalRhiOrdinaryBindingBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](core::StringView message) -> std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> {
        diagnostic.assign(message.data(), message.size());
        return {};
    };
    if (const auto* id = budget.validate(quota))
        return fail(id);
    if (!vertex || vertex->stage() != shader::ShaderStage::Vertex
        || (fragment && fragment->stage() != shader::ShaderStage::Fragment))
        return fail("RHI-METAL-ORDINARY-GRAPHICS-ARTIFACT-STAGE-MISMATCH");
    if (vertex->quota() != quota || (fragment && fragment->quota() != quota))
        return fail("RHI-METAL-ORDINARY-GRAPHICS-ARTIFACT-QUOTA-MISMATCH");
    if (fragment
        && (vertex->module().reflection.sourceSha256 != fragment->module().reflection.sourceSha256
            || vertex->filename(shader::ShaderArtifactRole::Source)
                != fragment->filename(shader::ShaderArtifactRole::Source)))
        return fail("RHI-METAL-ORDINARY-GRAPHICS-SOURCE-IDENTITY-MISMATCH");
    if (capabilityContract.backend != rhi::Backend::Metal)
        return fail("RHI-METAL-ORDINARY-GRAPHICS-BACKEND-MISMATCH");
    if (!authored.layout)
        return fail("RHI-METAL-ORDINARY-GRAPHICS-LAYOUT-MISSING");
    if (authored.label.get_allocator().resource() != &core::defaultCpuMemoryResource()
        || authored.colorTargets.get_allocator().resource() != &core::defaultCpuMemoryResource()
        || authored.vertexBuffers.get_allocator().resource() != &core::defaultCpuMemoryResource())
        return fail("RHI-METAL-ORDINARY-GRAPHICS-STATE-ALLOCATOR-MISMATCH");
    for (const auto& stream : authored.vertexBuffers)
        if (stream.attributes.get_allocator().resource() != &core::defaultCpuMemoryResource())
            return fail("RHI-METAL-ORDINARY-GRAPHICS-STATE-ALLOCATOR-MISMATCH");
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto desc = std::move(authored);
        rhi::VertexInputConsumptionContract vertexInput(desc.vertexBuffers);
        if (vertexInput.diagnostic())
            return fail(vertexInput.diagnostic());
        const auto vertexInputValid = rhi::validateSingleEntryVertexInput(vertexInput, *vertex);
        if (!vertexInputValid.success)
            return fail(vertexInputValid.diagnostics);
        const auto* layout = desc.layout;
        desc.layout = nullptr;
        const auto authoredState =
            rhi::validateBindlessAuthoredRenderState(desc, static_cast<bool>(fragment));
        if (!authoredState.success)
            return fail(authoredState.diagnostics);
        desc.layout = layout;
        const auto validLayout = rhi::validatePipelineLayout(layout->desc());
        if (!validLayout.success)
            return fail(validLayout.diagnostics);
        if (!layout->desc().descriptorTables.empty())
            return fail("RHI-METAL-ORDINARY-GRAPHICS-TABLE-ABI-UNSUPPORTED");
        auto interface = core::makeUnique<rhi::ShaderPackageDesc>();
        const auto converted =
            rhi::populateSelectedGraphicsShaderInterface(*vertex, fragment.get(), *interface);
        if (!converted.success) {
            if (converted.diagnostics == "RHI-GRAPHICS-MERGE-PUSH-ABI"
                || converted.diagnostics == "RHI-GRAPHICS-MERGE-PUSH-STAGES")
                return fail("RHI-METAL-ORDINARY-GRAPHICS-PUSH-ABI-MISMATCH");
            if (converted.diagnostics == "RHI-GRAPHICS-MERGE-LOGICAL-BINDING")
                return fail("RHI-METAL-ORDINARY-GRAPHICS-LOGICAL-BINDING-MISMATCH");
            return fail(converted.diagnostics);
        }
        const auto packageValid = rhi::validateShaderPackage(*interface);
        if (!packageValid.success)
            return fail(packageValid.diagnostics);
        const auto layoutValid = rhi::validatePipelineLayoutAgainstShaderReflection(
            layout->desc(), interface->reflection);
        if (!layoutValid.success)
            return fail(layoutValid.diagnostics);
        const auto optional = validateMetalOptionalLowerings(
            capabilityContract, layout->desc(), interface->reflection);
        if (!optional.success)
            return fail(optional.diagnostics);
        if (interface->reflection.pushConstantByteCount
            && !rhi::semantics::buildPushConstantLoweringPlan(
                rhi::Backend::Metal, capabilityContract)
                .hasExecutablePayload)
            return fail("RHI-METAL-ORDINARY-GRAPHICS-PUSH-EXECUTION-UNAVAILABLE");
        desc.shaderPackage = interface.get();
        desc.vertexShader = interface->entrypoints[0];
        if (fragment)
            desc.fragmentShader = interface->entrypoints[1];
        const auto state = rhi::validateRenderPipelineState(desc);
        if (!state.success)
            return fail(state.diagnostics);
        std::array<std::shared_ptr<const shader::SingleEntryShaderArtifact>, 2> artifacts{
            std::move(vertex), std::move(fragment)};
        std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2> stages;
        for (std::size_t i = 0u; i < artifacts.size(); ++i) {
            if (!artifacts[i])
                continue;
            auto stage = core::makeShared<MetalRhiPreparedGraphicsStageLayout>();
            const auto lowered = lowerMetalRhiPreparedGraphicsStage(*artifacts[i],
                artifacts[i]->entry(),
                i ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex,
                *stage);
            if (!lowered.success)
                return fail(lowered.diagnostics);
            stages[i] = std::move(stage);
        }
        rhi::ShaderPackageDesc vertexInterface;
        const auto vertexValid =
            rhi::populateSingleEntryShaderInterface(*artifacts[0], vertexInterface);
        if (!vertexValid.success)
            return fail(vertexValid.diagnostics);
        MetalBindingSpaceLoadResult vertexMapping;
        vertexMapping.success = true;
        vertexMapping.slots = stages[0]->direct;
        vertexMapping.argumentBuffers = stages[0]->arrays;
        desc.shaderPackage = &vertexInterface;
        const auto streams = lowerVertexStreams(desc, vertexMapping);
        desc.shaderPackage = interface.get();
        if (!streams.success)
            return fail(streams.diagnostics);
        if (!describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform)
            return fail("RHI-METAL-ORDINARY-GRAPHICS-NATIVE-RUNTIME-UNAVAILABLE");
        if (std::any_of(stages.begin(), stages.end(), [](const auto& stage) {
                return stage && !stage->arrays.empty();
            })) {
            const auto features = probeMetalRhiNativeFeatures();
            if (!features.argumentBuffers || !features.argumentBuffersTier2)
                return fail("RHI-METAL-ORDINARY-GRAPHICS-ARGUMENT-CAPABILITY-UNAVAILABLE");
        }
        auto report = makePipelineReport(layout, interface.get(), artifacts[1] ? 2u : 1u);
        copyStateReport(report, state);
        report.selectedFragmentWritesRenderTargetArrayIndex = artifacts[1]
            && artifacts[1]->module().reflection.entryPoints.front().writesRenderTargetArrayIndex;
        rhi::ShaderStage selectedStages;
        const auto selection = validateMetalRhiGraphicsStageSelection(desc, report, selectedStages);
        if (!selection.success)
            return fail(selection.diagnostics);
        report.shaderPackageValidated = true;
        report.reflectionMatched = true;
        report.success = true;
        core::StableHash64 hash(report.pipelineCacheKey);
        for (const auto& artifact : artifacts)
            if (artifact)
                hash.appendBytes(artifact->identity().data(), artifact->identity().size());
        report.pipelineCacheKey = hash.value();
        report.pipelineCacheKeyStable = true;
        const auto source = [&](std::size_t index) -> core::StringView {
            if (!artifacts[index])
                return {};
            const auto& bytes = artifacts[index]->bytes(shader::ShaderArtifactRole::Metal);
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        };
        auto setLayouts = core::makeUnique<rhi::DescriptorSetLayoutContract>(layout->desc());
        if (!setLayouts->valid())
            return fail("RHI-DESCRIPTOR-LAYOUT-CONTRACT-INVALID");
        auto native = createMetalRhiNativeRenderPipelinePayload(
            desc, report, streams, source(0u), source(1u));
        if (!native || !native->nativeRuntimeBacked || !native->renderPipelinePayloadPresent
            || !native->vertexFunction || (artifacts[1] && !native->fragmentFunction))
            return fail(native && !native->diagnostics.empty()
                    ? core::StringView(native->diagnostics)
                    : core::StringView("RHI-METAL-ORDINARY-GRAPHICS-NATIVE-PIPELINE-MISSING"));
        return core::makeShared<MetalRhiOrdinaryGraphicsProvider>(ConstructionKey{},
            std::move(artifacts),
            std::move(stages),
            std::move(interface),
            std::move(native),
            layout->desc(),
            std::move(setLayouts),
            std::move(vertexInput),
            streams,
            std::move(quota));
    } catch (const std::bad_alloc&) {
        return fail("RHI-METAL-ORDINARY-GRAPHICS-PROVIDER-ALLOCATION-FAILED");
    }
}

rhi::ValidationResult lowerMetalRhiPreparedGraphicsStage(
    const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
    std::string_view entry,
    rhi::ShaderStage stage,
    MetalRhiPreparedGraphicsStageLayout& output)
{
    output = {};
    rhi::ShaderPackageDesc interface;
    const rhi::BindlessPackageModule* module = nullptr;
    const auto composed = rhi::BindlessGraphicsPipelineEntries::composeStage(
        package, entry, stage, interface, module);
    if (!composed.success)
        return composed;
    if (package->artifact().execution != rhi::BindlessExecution::Native)
        return {false, "RHI-METAL-PREPARED-GRAPHICS-MODE-UNSUPPORTED"};
    const auto& reflection = module->reflection.reflection;
    MetalRhiPreparedGraphicsStageLayout result;
    const auto mapped =
        lowerMetalRhiPreparedGraphicsStageReflection(interface, reflection, entry, stage, result);
    if (!mapped.success)
        return mapped;
    if (!appendMetalInactivePipelineMappings(
            package->artifact(), reflection, stage, result.direct, result.arrays))
        return {false, "RHI-METAL-PREPARED-GRAPHICS-INACTIVE-MAPPING-MISSING"};
    output = std::move(result);
    return {true, {}};
}

rhi::ValidationResult lowerMetalRhiPreparedGraphicsStage(const rhi::BindlessShaderPackage& package,
    std::string_view entry,
    rhi::ShaderStage stage,
    MetalRhiPreparedGraphicsStageLayout& output)
{
    output = {};
    if (stage != rhi::ShaderStage::Vertex && stage != rhi::ShaderStage::Fragment)
        return {false, "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH"};
    if (package.artifact().execution != rhi::BindlessExecution::Native)
        return {false, "RHI-METAL-PREPARED-GRAPHICS-MODE-UNSUPPORTED"};
    rhi::ShaderPackageDesc interface;
    core::pmr::String diagnostic;
    const rhi::BindlessPackageModule* module = nullptr;
    if (!rhi::appendBindlessShaderEntry(package, entry, stage, interface, diagnostic, &module))
        return {false, diagnostic};
    const auto& reflection = module->reflection.reflection;
    // MetalBindingSpace/1 maps are module-local. Multi-entry reflection cannot
    // establish the required function-specific mapping and must fail closed.
    if (reflection.entryPoints.size() != 1u || reflection.entryPoints.front().name != entry)
        return {false, "RHI-METAL-PREPARED-GRAPHICS-ENTRY-AMBIGUOUS"};
    MetalRhiPreparedGraphicsStageLayout result;
    const auto mapped =
        lowerMetalRhiPreparedGraphicsStageReflection(interface, reflection, entry, stage, result);
    if (!mapped.success)
        return mapped;
    if (!appendMetalInactivePipelineMappings(
            package.artifact(), reflection, stage, result.direct, result.arrays))
        return {false, "RHI-METAL-PREPARED-GRAPHICS-INACTIVE-MAPPING-MISSING"};
    output = std::move(result);
    return {true, {}};
}

namespace {
core::UniquePtr<rhi::RenderPipeline> buildRetainedMetalGraphicsPipeline(
    std::shared_ptr<const rhi::BindlessShaderPackage> package,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry,
    rhi::semantics::DirectObjectBudgetLease budgetLease,
    core::pmr::String& diagnostic,
    const rhi::BindlessGraphicsPushRequest* push = nullptr)
{
    diagnostic.clear();
    const auto fail = [&](const char* id) -> core::UniquePtr<rhi::RenderPipeline> {
        diagnostic = id;
        return {};
    };
    if (push)
        if (const auto* id = push->diagnostic())
            return fail(id);
    if (!package)
        return fail("RHI-METAL-BINDLESS-GRAPHICS-PACKAGE-MISSING");
    if (!package->sharedOwnerMatches(package))
        return fail("RHI-METAL-BINDLESS-GRAPHICS-PACKAGE-OWNER-MISMATCH");
    // Sealed authorization has already consumed the independent device image
    // opt-in; unowned retained pipeline factories keep their buffer-only scope.
    const auto admission = validateMetalBindlessGraphicsPackage(*package, bool(authorization));
    if (!admission.success) {
        diagnostic = admission.diagnostics;
        return {};
    }
    if (capabilityContract.backend != rhi::Backend::Metal)
        return fail("RHI-METAL-BINDLESS-GRAPHICS-BACKEND-MISMATCH");
    const bool hasFragment = !fragmentEntry.empty();
    const auto authored = rhi::validateBindlessAuthoredRenderState(state, hasFragment);
    if (!authored.success) {
        diagnostic = authored.diagnostics;
        return {};
    }
    const auto scene =
        rhi::OriginalBindlessSceneContentContract::selectEntries(vertexEntry, fragmentEntry);
    if (scene.diagnostic)
        return fail(scene.diagnostic);
    if (scene.recognized)
        if (const auto* id =
                rhi::semantics::BindlessSceneRasterContract::inspect(rhi::Backend::Metal, state))
            return fail(id);
    auto interface = core::makeUnique<rhi::ShaderPackageDesc>();
    interface->label = package->artifact().physicalLayout.name;
    interface->generatedReflectionHydrated = true;
    std::array<const rhi::BindlessPackageModule*, 2> modules{};
    rhi::PipelineLayoutDesc layoutDesc;
    if (push) {
        rhi::BindlessGraphicsPipelineEntries selected;
        const auto composed = rhi::BindlessGraphicsPipelineEntries::compose(
            package, vertexEntry, fragmentEntry, *interface, layoutDesc, selected);
        if (!composed.success) {
            diagnostic = composed.diagnostics;
            return {};
        }
        modules = {selected.module(rhi::ShaderStage::Vertex, 1u),
            selected.module(rhi::ShaderStage::Fragment, 1u)};
        const auto bytes = interface->reflection.pushConstantByteCount;
        if (!bytes || bytes % 4u || bytes > push->maxBytes
            || bytes > capabilityContract.maxPushConstantSizeBytes)
            return fail("RHI-ORIGINAL-PUSH-RECORDING-SIZE-LIMIT");
    } else if (!rhi::appendBindlessShaderEntry(*package,
                   vertexEntry,
                   rhi::ShaderStage::Vertex,
                   *interface,
                   diagnostic,
                   &modules[0])
        || (hasFragment
            && !rhi::appendBindlessShaderEntry(*package,
                fragmentEntry,
                rhi::ShaderStage::Fragment,
                *interface,
                diagnostic,
                &modules[1])))
        return {};
    const auto selectedPackage = rhi::validateAuthorizedBindlessShaderPackage(*interface);
    if (!selectedPackage.success) {
        diagnostic = selectedPackage.diagnostics;
        return {};
    }
    // Sealed bindless packages currently forbid push constants. Check the
    // selected view as well; do not reconstruct an unproven cross-entry ABI.
    if (!push && interface->reflection.pushConstantByteCount)
        return fail("RHI-METAL-BINDLESS-GRAPHICS-PUSH-ABI-UNSUPPORTED");
    if (!push)
        layoutDesc = rhi::bindlessPipelineLayout(package->artifact());
    const auto reflection =
        rhi::validatePipelineLayoutAgainstShaderReflection(layoutDesc, interface->reflection);
    if (!reflection.success) {
        diagnostic = reflection.diagnostics;
        return {};
    }
    const auto optional =
        validateMetalOptionalLowerings(capabilityContract, layoutDesc, interface->reflection);
    if (!optional.success) {
        diagnostic = optional.diagnostics;
        return {};
    }
    if (!describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform)
        return fail("RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE");
    const auto nativeFeatures = probeMetalRhiNativeFeatures();
    if (!nativeFeatures.argumentBuffers || !nativeFeatures.argumentBuffersTier2)
        return fail("RHI-METAL-BINDLESS-GRAPHICS-ARGUMENT-CAPABILITY-UNAVAILABLE");
    const auto depthDeclaration = validateMetalBindlessOrdinaryDepthPipelinePrerequisites(
        package->artifact(), nativeFeatures);
    if (!depthDeclaration.success) {
        diagnostic = depthDeclaration.diagnostics;
        return {};
    }
    auto layout = createMetalRhiPipelineLayout(layoutDesc);
    if (!layout)
        return fail("RHI-METAL-BINDLESS-GRAPHICS-LAYOUT-INVALID");
    auto desc = state;
    if (desc.label.empty())
        desc.label = interface->label;
    desc.layout = layout.get();
    desc.shaderPackage = interface.get();
    desc.vertexShader = interface->entrypoints[0];
    if (hasFragment)
        desc.fragmentShader = interface->entrypoints[1];
    rhi::BindlessGraphicsPipelineEntries entries;
    if (const auto* id = rhi::BindlessGraphicsPipelineEntries::capture(*package, desc, entries))
        return fail(id);
    const auto validatedState = rhi::validateRenderPipelineState(desc);
    if (!validatedState.success) {
        diagnostic = validatedState.diagnostics;
        return {};
    }
    if (!rhi::semantics::sampleMaskHasFullCoverage(
            desc.multisample.sampleCount, desc.multisample.sampleMask))
        return fail("RHI-METAL-SAMPLE-MASK-SHADER-LOWERING-REQUIRED");
    auto report = makePipelineReport(layout.get(), interface.get(), hasFragment ? 2u : 1u);
    copyStateReport(report, validatedState);
    report.selectedFragmentWritesRenderTargetArrayIndex = hasFragment
        && modules[1]->reflection.reflection.entryPoints.front().writesRenderTargetArrayIndex;
    rhi::ShaderStage selectedStages;
    const auto selection = validateMetalRhiGraphicsStageSelection(desc, report, selectedStages);
    if (!selection.success) {
        diagnostic = selection.diagnostics;
        return {};
    }
    // Lower each selected entry independently. Combining native maps would
    // alias legitimate Vertex and Fragment slots across separate functions.
    std::array<MetalRhiPreparedGraphicsStageLayout, 2> stages;
    for (unsigned i = 0u; i < (hasFragment ? 2u : 1u); ++i) {
        const auto name = i ? fragmentEntry : vertexEntry;
        const auto stage = i ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex;
        const auto lowered = push
            ? lowerMetalRhiPreparedGraphicsStage(package, name, stage, stages[i])
            : lowerMetalRhiPreparedGraphicsStage(*package, name, stage, stages[i]);
        if (!lowered.success) {
            diagnostic = lowered.diagnostics;
            return {};
        }
    }
    MetalBindingSpaceLoadResult vertexMapping;
    vertexMapping.success = true;
    vertexMapping.slots = stages[0].direct;
    vertexMapping.argumentBuffers = stages[0].arrays;
    const auto streams = lowerVertexStreams(desc, vertexMapping);
    if (!streams.success) {
        diagnostic = streams.diagnostics;
        return {};
    }
    report.shaderPackageValidated = true;
    report.reflectionMatched = true;
    report.success = true;
    auto payload = createMetalRhiNativeRenderPipelinePayload(desc, report, streams, package);
    if (!payload || !payload->nativeRuntimeBacked || !payload->renderPipelinePayloadPresent) {
        diagnostic = payload && !payload->diagnostics.empty()
            ? payload->diagnostics
            : core::pmr::String("RHI-METAL-BINDLESS-GRAPHICS-NATIVE-PAYLOAD-MISSING");
        return {};
    }
    return core::makeUnique<MetalRhiRenderPipeline>(std::move(desc),
        std::move(report),
        std::move(payload),
        telemetry ? telemetry->acquire(rhi::semantics::DirectObjectKind::RenderPipeline)
                  : rhi::semantics::DirectObjectTelemetryLease{},
        std::move(budgetLease),
        std::move(layout),
        std::move(interface),
        std::move(package),
        std::move(authorization),
        entries);
}
} // namespace

core::UniquePtr<rhi::RenderPipeline> createMetalRhiRetainedGraphicsPipeline(
    std::shared_ptr<const rhi::BindlessShaderPackage> package,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    core::pmr::String& diagnostic)
{
    return buildRetainedMetalGraphicsPipeline(
        std::move(package), state, vertexEntry, fragmentEntry, contract, {}, {}, {}, diagnostic);
}

core::UniquePtr<rhi::RenderPipeline> createMetalRhiAuthorizedBindlessRenderPipeline(
    const rhi::BindlessCrossExtension& issuer,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry,
    rhi::semantics::DirectObjectBudgetLease budgetLease,
    core::pmr::String& diagnostic,
    const rhi::BindlessGraphicsPushRequest* push)
{
    const auto& caps = issuer.capabilities();
    if (push && (push->diagnostic() || caps.originalGraphicsPushContractVersion != 1u)) {
        diagnostic = push->diagnostic() ? push->diagnostic()
                                        : "RHI-METAL-BINDLESS-GRAPHICS-PUSH-UNAVAILABLE";
        return {};
    }
    if (!metalBindlessGraphicsExecutionAvailable(caps)) {
        diagnostic = "RHI-METAL-BINDLESS-GRAPHICS-EXECUTION-UNAVAILABLE";
        return {};
    }
    if (!authorization || !authorization->issuedBy(&issuer, authorization)
        || !authorization->plan().executable
        || authorization->plan().execution != rhi::BindlessExecution::Native
        || authorization->plan().deviceIdentity != caps.deviceIdentity
        || authorization->plan().shaderPackageIdentity
            != authorization->package().artifact().shaderPackageIdentity) {
        diagnostic = "RHI-METAL-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
        return {};
    }
    auto package = authorization->originalPackageOwner();
    return buildRetainedMetalGraphicsPipeline(std::move(package),
        state,
        vertexEntry,
        fragmentEntry,
        contract,
        std::move(authorization),
        std::move(telemetry),
        std::move(budgetLease),
        diagnostic,
        push);
}

arp::core::UniquePtr<rhi::RenderPipeline> createMetalRhiRenderPipeline(
    const rhi::RenderPipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry,
    rhi::semantics::DirectObjectBudgetLease budgetLease)
{
    auto report = validateMetalRhiRenderPipeline(desc, capabilityContract);
    if (!report.success) {
        return nullptr;
    }
    const auto metalBindingSpace = loadMetalBindingSpace(*desc.shaderPackage);
    const auto vertexStreams = lowerVertexStreams(desc, metalBindingSpace);
    if (!vertexStreams.success)
        return nullptr;
    const bool supportsIndirectCommands =
        rhi::semantics::hasExecutableNativeFeature(
            capabilityContract, rhi::semantics::NativeBackendFeature::IndirectCommandBuffers)
        && std::none_of(
            metalBindingSpace.slots.begin(), metalBindingSpace.slots.end(), [](const auto& slot) {
                return slot.active
                    && !rhi::semantics::metalIndirectCommandSupportsDirectBinding(slot.type);
            });
    auto payload = createMetalRhiNativeRenderPipelinePayload(
        desc, report, vertexStreams, supportsIndirectCommands);
    const auto runtime = describeMetalRhiNativeRuntimeBoundary();
    if (!payload || !metalBindingSpace.success
        || (runtime.runtimeAvailableOnThisPlatform && !payload->nativeRuntimeBacked)) {
        if (payload && !payload->diagnostics.empty()) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, payload->diagnostics.c_str());
        }
        return nullptr;
    }
    payload->metalBindingSpace = metalBindingSpace.slots;
    payload->metalArgumentBuffer = metalBindingSpace.argumentBuffers;
    payload->metalFramebufferFetch = metalBindingSpace.framebufferFetch;
    return arp::core::makeUnique<MetalRhiRenderPipeline>(desc,
        report,
        std::move(payload),
        telemetry ? telemetry->acquire(rhi::semantics::DirectObjectKind::RenderPipeline)
                  : rhi::semantics::DirectObjectTelemetryLease{},
        std::move(budgetLease));
}

arp::core::UniquePtr<rhi::ComputePipeline> createMetalRhiComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry,
    rhi::semantics::DirectObjectBudgetLease budgetLease)
{
    auto report = validateMetalRhiComputePipeline(desc, capabilityContract);
    if (!report.success) {
        return nullptr;
    }
    auto payload = createMetalRhiNativeComputePipelinePayload(desc, report);
    const auto metalBindingSpace = loadMetalBindingSpace(*desc.shaderPackage);
    const auto runtime = describeMetalRhiNativeRuntimeBoundary();
    if (!payload || !metalBindingSpace.success
        || (runtime.runtimeAvailableOnThisPlatform && !payload->nativeRuntimeBacked)) {
        return nullptr;
    }
    payload->metalBindingSpace = metalBindingSpace.slots;
    payload->metalArgumentBuffer = metalBindingSpace.argumentBuffers;
    payload->metalFramebufferFetch = metalBindingSpace.framebufferFetch;
    return arp::core::makeUnique<MetalRhiComputePipeline>(desc,
        report,
        std::move(payload),
        telemetry ? telemetry->acquire(rhi::semantics::DirectObjectKind::ComputePipeline)
                  : rhi::semantics::DirectObjectTelemetryLease{},
        std::move(budgetLease));
}

arp::core::UniquePtr<rhi::ComputePipeline> createMetalRhiAuthorizedBindlessComputePipeline(
    const rhi::BindlessCrossExtension& issuer,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    std::string_view entry,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry,
    rhi::semantics::DirectObjectBudgetLease budgetLease,
    core::pmr::String* failureDiagnostic)
{
    if (failureDiagnostic)
        failureDiagnostic->clear();
    const auto fail = [&](std::string_view id,
                          std::string_view detail = {}) -> core::UniquePtr<rhi::ComputePipeline> {
        if (failureDiagnostic) {
            failureDiagnostic->assign(id.data(), id.size());
            if (!detail.empty()) {
                failureDiagnostic->append(": ");
                failureDiagnostic->append(detail.data(), detail.size());
            }
        }
        return {};
    };
    if (!authorization || !authorization->issuedBy(&issuer, authorization)
        || authorization->plan().execution != rhi::BindlessExecution::Native
        || (!authorization->plan().executable
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                authorization->plan()))
        || authorization->plan().shaderPackageIdentity
            != authorization->package().artifact().shaderPackageIdentity) {
        return fail("RHI-METAL-BINDLESS-PIPELINE-AUTHORIZATION-INVALID");
    }
    const auto& package = authorization->package();
    const auto& artifact = package.artifact();
    if (artifact.execution != rhi::BindlessExecution::Native)
        return fail("RHI-METAL-BINDLESS-PIPELINE-MODE-MISMATCH");

    const rhi::BindlessPackageModule* module = nullptr;
    for (const auto& candidate : package.modules()) {
        if (candidate.entry == entry) {
            module = &candidate;
            break;
        }
    }
    if (!module || module->metalFile >= package.files().size())
        return fail("RHI-METAL-BINDLESS-PIPELINE-ENTRY-MISSING");

    rhi::ShaderPackageDesc interface;
    interface.label = artifact.physicalLayout.name;
    interface.generatedReflectionHydrated = true;
    std::pmr::string diagnostic;
    if (!rhi::appendBindlessShaderEntry(
            package, entry, rhi::ShaderStage::Compute, interface, diagnostic))
        return fail("RHI-METAL-BINDLESS-PIPELINE-INTERFACE-INVALID", diagnostic);

    const auto layoutDesc = rhi::bindlessPipelineLayout(artifact);
    auto layout = createMetalRhiPipelineLayout(layoutDesc);
    if (!layout)
        return fail("RHI-METAL-BINDLESS-PIPELINE-LAYOUT-INVALID");
    const auto reflectionValidation =
        rhi::validatePipelineLayoutAgainstShaderReflection(layoutDesc, interface.reflection);
    if (!reflectionValidation.success)
        return fail(
            "RHI-METAL-BINDLESS-PIPELINE-REFLECTION-INVALID", reflectionValidation.diagnostics);

    rhi::ComputePipelineDesc desc;
    desc.label = interface.label;
    desc.layout = layout.get();
    desc.shaderPackage = &interface;
    desc.computeShader = interface.entrypoints.front();
    const auto& verifiedReflection = module->reflection.reflection;
    auto report = validateMetalRhiAuthorizedBindlessComputePipeline(
        desc, capabilityContract, verifiedReflection);
    if (!report.success)
        return fail("RHI-METAL-BINDLESS-PIPELINE-VALIDATION-FAILED", report.diagnostics);

    const auto& msl = package.files()[module->metalFile].bytes;
    const std::string_view mslSource(reinterpret_cast<const char*>(msl.data()), msl.size());
    auto payload = createMetalRhiNativeComputePipelinePayload(desc, report, mslSource);
    const auto metalBindingSpace = loadMetalBindingSpace(interface, &verifiedReflection);
    const auto runtime = describeMetalRhiNativeRuntimeBoundary();
    if (!payload || !metalBindingSpace.success
        || (runtime.runtimeAvailableOnThisPlatform && !payload->nativeRuntimeBacked)) {
        return fail("RHI-METAL-BINDLESS-PIPELINE-NATIVE-PAYLOAD-MISSING",
            payload ? std::string_view(payload->diagnostics) : std::string_view{});
    }
    payload->metalBindingSpace = metalBindingSpace.slots;
    payload->metalArgumentBuffer = metalBindingSpace.argumentBuffers;
    payload->metalFramebufferFetch = metalBindingSpace.framebufferFetch;
    // Preserve explicit per-entry inactivity from the sealed compiler package.
    // The snapshot still contains the full domain, including other stages.
    if (!appendMetalInactivePipelineMappings(artifact,
            verifiedReflection,
            rhi::ShaderStage::Compute,
            payload->metalBindingSpace,
            payload->metalArgumentBuffer))
        return fail("RHI-METAL-BINDLESS-PIPELINE-INACTIVE-MAPPING-MISSING");
    return arp::core::makeUnique<MetalRhiComputePipeline>(desc,
        report,
        std::move(payload),
        telemetry ? telemetry->acquire(rhi::semantics::DirectObjectKind::ComputePipeline)
                  : rhi::semantics::DirectObjectTelemetryLease{},
        std::move(budgetLease),
        std::move(authorization),
        std::move(layout));
}

std::shared_ptr<MetalRhiNativePipelinePayload> metalRhiNativePayloadFor(
    const rhi::RenderPipeline& pipeline)
{
    const auto* metalPipeline = dynamic_cast<const MetalRhiRenderPipeline*>(&pipeline);
    return metalPipeline ? metalPipeline->nativePayload() : nullptr;
}

MetalRhiSelectedGraphicsEntries metalRhiSelectedGraphicsEntriesFor(
    const rhi::RenderPipeline& pipeline) noexcept
{
    const auto* metalPipeline = dynamic_cast<const MetalRhiRenderPipeline*>(&pipeline);
    return metalPipeline ? metalPipeline->selectedEntries() : MetalRhiSelectedGraphicsEntries{};
}

std::optional<rhi::OriginalBindlessGraphicsPassContract::PipelineState>
metalRhiOriginalGraphicsPassPipelineState(const rhi::RenderPipeline& pipeline) noexcept
{
    const auto* concrete = dynamic_cast<const MetalRhiRenderPipeline*>(&pipeline);
    return concrete ? std::make_optional(concrete->originalGraphicsPassState()) : std::nullopt;
}

std::shared_ptr<MetalRhiNativePipelinePayload> metalRhiNativePayloadFor(
    const rhi::ComputePipeline& pipeline)
{
    const auto* metalPipeline = dynamic_cast<const MetalRhiComputePipeline*>(&pipeline);
    return metalPipeline ? metalPipeline->nativePayload() : nullptr;
}

namespace {

MetalRhiPipelinePayloadInspection inspectPipelinePayload(
    const std::shared_ptr<MetalRhiNativePipelinePayload>& payload)
{
    MetalRhiPipelinePayloadInspection inspection;
    if (!payload) {
        return inspection;
    }
    inspection.backendPayloadPresent = true;
    inspection.shaderModulePayloadPresent = payload->shaderModulePayloadPresent;
    inspection.renderPipelinePayloadPresent = payload->renderPipelinePayloadPresent;
    inspection.computePipelinePayloadPresent = payload->computePipelinePayloadPresent;
    inspection.nativeRuntimeBacked = payload->nativeRuntimeBacked;
    inspection.nativeRuntimeDiagnosticOnly = payload->nativeRuntimeDiagnosticOnly;
    inspection.nativeHandleExposedToUpperLayer = payload->nativeHandleExposedToUpperLayer;
    inspection.pipelineCacheKeyStable = payload->pipelineCacheKeyStable;
    inspection.rasterStateRecorded = payload->rasterStateRecorded;
    inspection.depthStencilStateRecorded = payload->depthStencilStateRecorded;
    inspection.depthTestEnabled = payload->depthTestEnabled;
    inspection.depthWriteEnabled = payload->depthWriteEnabled;
    inspection.depthCompare = payload->depthCompare;
    inspection.cullMode = payload->cullMode;
    inspection.frontFace = payload->frontFace;
    inspection.sampleMask = payload->sampleMask;
    inspection.alphaToCoverageEnabled = payload->alphaToCoverageEnabled;
    inspection.pipelineCacheKey = payload->pipelineCacheKey;
    return inspection;
}

} // namespace

MetalRhiPipelinePayloadInspection inspectMetalRhiPipelinePayload(
    const rhi::RenderPipeline& pipeline)
{
    return inspectPipelinePayload(metalRhiNativePayloadFor(pipeline));
}

MetalRhiPipelinePayloadInspection inspectMetalRhiPipelinePayload(
    const rhi::ComputePipeline& pipeline)
{
    return inspectPipelinePayload(metalRhiNativePayloadFor(pipeline));
}

} // namespace arp::rhi_metal
