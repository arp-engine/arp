#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"

#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_push_constant_abi_identity.h"
#include "rhi/frontend/pipeline_single_entry_input.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/shader_reflection_cache.h"

#include <algorithm>
#include <string>
#include <utility>

namespace arp::rhi_webgpu {

namespace {

// Version 5 adds executable binding-array metadata plus the exact physical
// bind-group count. A logical descriptor array is represented by one WGSL
// binding-array declaration with an explicit capacity; the RHI still writes
// and validates each element independently.
constexpr const char* kContractVersion = kWebGpuBindingSpaceContractVersion;

const WebGpuBindingSpaceLoweringEntry* findEntry(
    const WebGpuBindingSpaceLowering& lowering, std::uint32_t set, std::uint32_t binding)
{
    for (const auto& entry : lowering.entries) {
        if (entry.set == set && entry.binding == binding) {
            return &entry;
        }
    }
    return nullptr;
}

const rhi::ShaderReflectionBinding* findArtifactBinding(
    const WebGpuBindingSpaceLowering& lowering, std::uint32_t set, std::uint32_t binding)
{
    for (const auto& entry : lowering.artifactBindings) {
        if (entry.set == set && entry.binding == binding) {
            return &entry;
        }
    }
    return nullptr;
}

rhi::DescriptorType descriptorTypeFromShaderBinding(shader::BindingType type);

bool descriptorReflectionMatches(
    const shader::ShaderReflection& cached, const rhi::ShaderReflectionDesc& package)
{
    if (cached.bindings.size() != package.descriptorBindings.size()) {
        return false;
    }
    for (const auto& cachedBinding : cached.bindings) {
        const auto found = std::find_if(package.descriptorBindings.begin(),
            package.descriptorBindings.end(),
            [&](const auto& candidate) {
                return candidate.set == cachedBinding.set
                    && candidate.binding == cachedBinding.binding;
            });
        if (found == package.descriptorBindings.end()
            || descriptorTypeFromShaderBinding(cachedBinding.type) != found->type
            || cachedBinding.arrayCount != found->arrayCount
            || cachedBinding.stageMask != static_cast<std::uint32_t>(found->stages)
            || cachedBinding.subpassAttachmentIndex != found->subpassAttachmentIndex
            || cachedBinding.depthTexture != found->depthTexture) {
            return false;
        }
    }
    return true;
}

rhi::DescriptorType descriptorTypeFromShaderBinding(shader::BindingType type)
{
    switch (type) {
    case shader::BindingType::UniformBuffer:
        return rhi::DescriptorType::UniformBuffer;
    case shader::BindingType::StorageBuffer:
        return rhi::DescriptorType::StorageBuffer;
    case shader::BindingType::ReadOnlyStorageBuffer:
        return rhi::DescriptorType::ReadOnlyStorageBuffer;
    case shader::BindingType::SampledTexture:
        return rhi::DescriptorType::SampledTexture;
    case shader::BindingType::StorageTexture:
        return rhi::DescriptorType::StorageTexture;
    case shader::BindingType::Sampler:
        return rhi::DescriptorType::Sampler;
    case shader::BindingType::InputAttachment:
        return rhi::DescriptorType::InputAttachment;
    }
    return rhi::DescriptorType::UniformBuffer;
}

WebGpuBindingSpaceLoweringLoadResult lowerWebGpuBindingSpaceReflection(
    const rhi::ShaderPackageDesc& package, const shader::ShaderReflectionModule& module)
{
    WebGpuBindingSpaceLoweringLoadResult result;
    const auto pushMemberValidation =
        shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(module.reflection);
    if (!pushMemberValidation.success) {
        result.diagnostics = "WebGPU reflection artifact failed push-constant member validation: ";
        result.diagnostics += pushMemberValidation.diagnostics;
        return result;
    }
    if (module.reflection.pushConstantSizeBytes != package.reflection.pushConstantByteCount
        || module.reflection.pushConstantContractVersion
            != package.reflection.pushConstantContractVersion
        || module.reflection.pushConstantStages != package.reflection.pushConstantStages
        || module.reflection.pushConstantWebGpuBindingGroup
            != package.reflection.pushConstantWebGpuBindingGroup
        || module.reflection.pushConstantWebGpuBindingIndex
            != package.reflection.pushConstantWebGpuBindingIndex
        || !rhi::ShaderPushConstantAbiIdentity::matchesMembers(
            module.reflection.pushConstantMembers, package.reflection.pushConstantMembers)
        || !descriptorReflectionMatches(module.reflection, package.reflection)) {
        result.diagnostics =
            "WebGPU reflection artifact disagrees with package reflection metadata";
        return result;
    }
    if (module.reflection.webGpuBindingSpaceStatus != "Available") {
        result.diagnostics = "WebGPU lowering artifact is unavailable for this shader";
        if (!module.reflection.webGpuBindingSpaceUnavailableReason.empty()) {
            result.diagnostics += ": ";
            result.diagnostics += module.reflection.webGpuBindingSpaceUnavailableReason;
        }
        return result;
    }
    if (module.reflection.webGpuBindingSpaceContractVersion != kContractVersion) {
        result.diagnostics = "WebGPU lowering artifact has an unsupported contract version";
        return result;
    }
    std::uint32_t highestPhysicalGroup = 0u;
    bool hasActivePhysicalGroup = false;
    for (const auto& source : module.reflection.webGpuBindingSpace) {
        if (source.active) {
            highestPhysicalGroup = std::max(highestPhysicalGroup, source.group);
            hasActivePhysicalGroup = true;
        }
        if (!source.loadSemantics.empty() && source.loadSemantics != "CurrentFragment") {
            result.diagnostics = "WebGPU lowering artifact has unsupported load semantics";
            return result;
        }
    }

    result.lowering.contractVersion = module.reflection.webGpuBindingSpaceContractVersion;
    result.lowering.physicalGroupCount = module.reflection.webGpuBindingSpaceGroupCount;
    if (module.reflection.pushConstantSizeBytes > 0u) {
        highestPhysicalGroup =
            std::max(highestPhysicalGroup, module.reflection.pushConstantWebGpuBindingGroup);
        hasActivePhysicalGroup = true;
    }
    if ((hasActivePhysicalGroup
            && (result.lowering.physicalGroupCount == 0u
                || highestPhysicalGroup >= result.lowering.physicalGroupCount
                || result.lowering.physicalGroupCount != highestPhysicalGroup + 1u))
        || (!hasActivePhysicalGroup && result.lowering.physicalGroupCount != 0u)) {
        result.diagnostics =
            "WebGPU lowering artifact has invalid physical bind-group count metadata";
        return result;
    }
    result.lowering.pushConstantGroup = module.reflection.pushConstantWebGpuBindingGroup;
    result.lowering.pushConstantBinding = module.reflection.pushConstantWebGpuBindingIndex;
    if (module.reflection.pushConstantSizeBytes > 0u
        && (result.lowering.pushConstantGroup == UINT32_MAX
            || result.lowering.pushConstantBinding == UINT32_MAX)) {
        result.diagnostics =
            "WebGPU lowering artifact is missing a reflected push-constant group/binding";
        return result;
    }
    for (const auto& source : module.reflection.bindings) {
        result.lowering.artifactBindings.push_back({
            source.set,
            source.binding,
            descriptorTypeFromShaderBinding(source.type),
            static_cast<rhi::ShaderStage>(source.stageMask),
            source.arrayCount,
            source.subpassAttachmentIndex,
            source.depthTexture,
        });
    }
    for (const auto& source : module.reflection.webGpuBindingSpace) {
        result.lowering.entries.push_back({
            core::pmr::String(source.name, &core::defaultCpuMemoryResource()),
            source.set,
            source.binding,
            source.group,
            source.webGpuBinding,
            source.active,
            descriptorTypeFromShaderBinding(source.physicalType),
            rhi::semantics::SubpassInputLoadSemantics::CurrentFragment,
            source.loadSemantics == "CurrentFragment",
            source.requiresSampler,
            source.depthTexture,
            source.arrayElement,
            source.capacity,
        });
    }
    result.success = true;
    return result;
}

} // namespace

WebGpuBindingSpaceLoweringLoadResult loadWebGpuBindingSpaceLowering(
    const rhi::ShaderPackageDesc& package)
{
    WebGpuBindingSpaceLoweringLoadResult result;
    if (package.artifacts.reflectionPath.empty()) {
        result.diagnostics = "WebGPU lowering requires a generated reflection artifact path";
        return result;
    }
    const auto cached =
        shader::ShaderReflectionCache::loadFromFile(package.artifacts.reflectionPath);
    if (!cached.success) {
        result.diagnostics = "WebGPU lowering could not load reflection artifact";
        if (!package.label.empty()) {
            result.diagnostics += " for package '";
            result.diagnostics += package.label;
            result.diagnostics += "'";
        }
        result.diagnostics += " (reflectionPath='";
        result.diagnostics += package.artifacts.reflectionPath;
        result.diagnostics += "'): ";
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
        result.diagnostics = "WebGPU reflection artifact identity validation failed: ";
        result.diagnostics += artifactIdentityValidation.diagnostics;
        return result;
    }
    return lowerWebGpuBindingSpaceReflection(package, cached.module);
}

WebGpuBindingSpaceLoweringLoadResult loadOriginalWebGpuBindingSpaceLowering(
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input)
{
    const auto target =
        rhi::OriginalSingleEntryPipelineInput::selectTarget(input, rhi::Backend::WebGPU);
    if (target.diagnostic) {
        WebGpuBindingSpaceLoweringLoadResult result;
        result.diagnostics = target.diagnostic;
        return result;
    }
    return lowerWebGpuBindingSpaceReflection(input->package(), input->artifact()->module());
}

WebGpuBindingSpaceLoweringLoadResult loadOriginalWebGpuGraphicsBindingSpaceLowering(
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input)
{
    const auto target =
        rhi::OriginalGraphicsPipelineInput::selectTarget(input, rhi::Backend::WebGPU);
    if (target.diagnostic) {
        WebGpuBindingSpaceLoweringLoadResult result;
        result.diagnostics = target.diagnostic;
        return result;
    }
    return lowerWebGpuBindingSpaceReflection(input->package(), input->artifact()->module());
}

WebGpuBindingSpaceLoweringLoadResult loadBindlessWebGpuBindingSpaceLowering(
    const rhi::BindlessShaderPackage& package,
    std::string_view entry,
    rhi::ShaderStage stage,
    bool explicitPush)
{
    WebGpuBindingSpaceLoweringLoadResult result;
    core::StringView wgslBytes;
    if (!rhi::selectBindlessEmulatedWgsl(package, entry, stage, wgslBytes, result.diagnostics))
        return result;
    const auto module = std::find_if(
        package.modules().begin(), package.modules().end(), [&](const auto& candidate) {
            return candidate.entry == entry;
        });
    if (module == package.modules().end()) {
        result.diagnostics = "RHI-BINDLESS-WEBGPU-ENTRY-MISMATCH";
        return result;
    }
    const auto& reflection = module->reflection.reflection;
    if (reflection.webGpuBindingSpaceStatus != "Available"
        || reflection.webGpuBindingSpaceContractVersion != kContractVersion
        || (reflection.pushConstantSizeBytes != 0u && !explicitPush)) {
        result.diagnostics = "RHI-BINDLESS-WEBGPU-LOWERING-UNAVAILABLE";
        return result;
    }
    result.lowering.contractVersion = reflection.webGpuBindingSpaceContractVersion;
    result.lowering.physicalGroupCount = reflection.webGpuBindingSpaceGroupCount;
    result.lowering.pushConstantGroup = reflection.pushConstantWebGpuBindingGroup;
    result.lowering.pushConstantBinding = reflection.pushConstantWebGpuBindingIndex;
    std::uint32_t highestGroup = 0;
    bool hasActiveGroup = false;
    for (const auto& source : reflection.bindings) {
        const auto* domain = rhi::semantics::BindlessPipelineInterfaceContract::descriptorLayout(
            package.artifact(), source.set);
        if (!domain) {
            result.diagnostics = "RHI-BINDLESS-WEBGPU-REFLECTION-MISMATCH";
            return result;
        }
        const auto physical = std::find_if(
            domain->bindings.begin(), domain->bindings.end(), [&](const auto& candidate) {
                return candidate.binding == source.binding;
            });
        if (physical == domain->bindings.end()
            || physical->type != descriptorTypeFromShaderBinding(source.type)
            || physical->arrayCount != source.arrayCount
            || (static_cast<std::uint32_t>(physical->stages) & source.stageMask)
                != source.stageMask) {
            result.diagnostics = "RHI-BINDLESS-WEBGPU-REFLECTION-MISMATCH";
            return result;
        }
        result.lowering.artifactBindings.push_back({
            source.set,
            source.binding,
            descriptorTypeFromShaderBinding(source.type),
            static_cast<rhi::ShaderStage>(source.stageMask),
            source.arrayCount,
            source.subpassAttachmentIndex,
            source.depthTexture,
        });
    }
    for (const auto& source : reflection.webGpuBindingSpace) {
        if (!source.active)
            continue;
        const auto logical = std::find_if(
            reflection.bindings.begin(), reflection.bindings.end(), [&](const auto& candidate) {
                return candidate.set == source.set && candidate.binding == source.binding;
            });
        if (logical == reflection.bindings.end() || source.physicalType != logical->type
            || source.arrayElement != 0u || source.capacity != logical->arrayCount
            || source.capacity != 1u || source.group >= result.lowering.physicalGroupCount
            || source.webGpuBinding == UINT32_MAX || !source.loadSemantics.empty()) {
            result.diagnostics = "RHI-BINDLESS-WEBGPU-SCALAR-MAPPING-MISMATCH";
            return result;
        }
        for (const auto& existing : result.lowering.entries) {
            if ((existing.set == source.set && existing.binding == source.binding)
                || (existing.group == source.group
                    && existing.webGpuBinding == source.webGpuBinding)) {
                result.diagnostics = "RHI-BINDLESS-WEBGPU-SCALAR-MAPPING-MISMATCH";
                return result;
            }
        }
        result.lowering.entries.push_back({
            core::pmr::String(source.name, &core::defaultCpuMemoryResource()),
            source.set,
            source.binding,
            source.group,
            source.webGpuBinding,
            true,
            descriptorTypeFromShaderBinding(source.physicalType),
            rhi::semantics::SubpassInputLoadSemantics::CurrentFragment,
            false,
            source.requiresSampler,
            source.depthTexture,
            source.arrayElement,
            source.capacity,
        });
        highestGroup = std::max(highestGroup, source.group);
        hasActiveGroup = true;
    }
    for (const auto& logical : reflection.bindings) {
        if (std::none_of(result.lowering.entries.begin(),
                result.lowering.entries.end(),
                [&](const auto& source) {
                    return source.set == logical.set && source.binding == logical.binding;
                })) {
            result.diagnostics = "RHI-BINDLESS-WEBGPU-SCALAR-MAPPING-MISMATCH";
            return result;
        }
    }
    if (reflection.pushConstantSizeBytes) {
        if (reflection.pushConstantContractVersion != "AtomicFullBlockPush/1"
            || reflection.pushConstantWebGpuBindingGroup == UINT32_MAX
            || reflection.pushConstantWebGpuBindingIndex == UINT32_MAX) {
            result.diagnostics = "RHI-BINDLESS-WEBGPU-PUSH-MAPPING";
            return result;
        }
        highestGroup = std::max(highestGroup, reflection.pushConstantWebGpuBindingGroup);
        hasActiveGroup = true;
    }
    if ((hasActiveGroup && result.lowering.physicalGroupCount != highestGroup + 1u)
        || (!hasActiveGroup && result.lowering.physicalGroupCount != 0u)) {
        result.diagnostics = "RHI-BINDLESS-WEBGPU-GROUP-COUNT-MISMATCH";
        return result;
    }
    result.success = true;
    return result;
}

namespace {
rhi::ValidationResult buildBindlessActiveLayout(const rhi::BindlessShaderArtifact& artifact,
    const rhi::ShaderReflectionDesc& reflection,
    rhi::ShaderStage stages,
    rhi::PipelineLayoutDesc& layout)
{
    for (const auto& reflected : reflection.descriptorBindings) {
        const auto* domain = rhi::semantics::BindlessPipelineInterfaceContract::descriptorLayout(
            artifact, reflected.set);
        if (!domain || reflected.stages == rhi::ShaderStage::None
            || (reflected.stages & stages) != reflected.stages)
            return {false, "RHI-BINDLESS-WEBGPU-ACTIVE-REFLECTION-MISMATCH"};
        const auto binding = std::find_if(
            domain->bindings.begin(), domain->bindings.end(), [&](const auto& candidate) {
                return candidate.binding == reflected.binding;
            });
        if (binding == domain->bindings.end())
            return {false, "RHI-BINDLESS-WEBGPU-ACTIVE-REFLECTION-MISMATCH"};
        auto selected = *binding;
        selected.stages = reflected.stages;
        const auto mapping = std::find_if(
            layout.descriptorSets.begin(), layout.descriptorSets.end(), [&](const auto& set) {
                return set.set == reflected.set;
            });
        if (mapping == layout.descriptorSets.end()) {
            rhi::DescriptorLayoutDesc active;
            active.name = domain->name;
            active.bindings.push_back(std::move(selected));
            layout.descriptorLayouts.push_back(std::move(active));
            layout.descriptorSets.push_back({reflected.set, domain->name});
        } else {
            const auto active = std::find_if(layout.descriptorLayouts.begin(),
                layout.descriptorLayouts.end(),
                [&](const auto& candidate) {
                    return candidate.name == mapping->layoutName;
                });
            active->bindings.push_back(std::move(selected));
        }
    }
    return {true, {}};
}
} // namespace

WebGpuBindlessComputeInterface prepareBindlessWebGpuComputeInterface(
    const rhi::BindlessShaderPackage& package, std::string_view entry)
{
    WebGpuBindlessComputeInterface result;
    const auto lowered =
        loadBindlessWebGpuBindingSpaceLowering(package, entry, rhi::ShaderStage::Compute);
    if (!lowered.success) {
        result.diagnostics = lowered.diagnostics;
        return result;
    }
    result.lowering = lowered.lowering;
    result.shader.label = package.artifact().physicalLayout.name;
    result.shader.generatedReflectionHydrated = true;
    if (!rhi::appendBindlessShaderEntry(
            package, entry, rhi::ShaderStage::Compute, result.shader, result.diagnostics))
        return result;

    const auto active = buildBindlessActiveLayout(
        package.artifact(), result.shader.reflection, rhi::ShaderStage::Compute, result.layout);
    if (!active.success) {
        result.diagnostics = active.diagnostics;
        return result;
    }
    const auto layoutValidation = rhi::validatePipelineLayout(result.layout);
    if (!layoutValidation.success) {
        result.diagnostics = layoutValidation.diagnostics;
        return result;
    }
    const auto reflectionValidation =
        rhi::validatePipelineLayoutAgainstShaderReflection(result.layout, result.shader.reflection);
    if (!reflectionValidation.success) {
        result.diagnostics = reflectionValidation.diagnostics;
        return result;
    }
    const auto loweringValidation = validateWebGpuBindingSpaceLowering(result.layout,
        result.shader.reflection,
        result.lowering,
        result.lowering.pushConstantGroup);
    if (!loweringValidation.success) {
        result.diagnostics = loweringValidation.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

WebGpuBindlessRenderInterface prepareBindlessWebGpuRenderInterface(
    const rhi::BindlessShaderPackage& package,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const std::shared_ptr<const rhi::BindlessShaderPackage>& pushOwner)
{
    WebGpuBindlessRenderInterface result;
    const auto vertex = loadBindlessWebGpuBindingSpaceLowering(
        package, vertexEntry, rhi::ShaderStage::Vertex, bool(pushOwner));
    if (!vertex.success) {
        result.diagnostics = vertex.diagnostics;
        return result;
    }
    const auto fragment = loadBindlessWebGpuBindingSpaceLowering(
        package, fragmentEntry, rhi::ShaderStage::Fragment, bool(pushOwner));
    if (!fragment.success) {
        result.diagnostics = fragment.diagnostics;
        return result;
    }
    result.shader.label = package.artifact().physicalLayout.name;
    result.shader.generatedReflectionHydrated = true;
    if (pushOwner) {
        if (pushOwner.get() != &package) {
            result.diagnostics = "RHI-BINDLESS-WEBGPU-PUSH-PACKAGE-OWNER";
            return result;
        }
        rhi::BindlessGraphicsPipelineEntries selected;
        const auto composed = rhi::BindlessGraphicsPipelineEntries::compose(
            pushOwner, vertexEntry, fragmentEntry, result.shader, result.layout, selected);
        if (!composed.success) {
            result.diagnostics = composed.diagnostics;
            return result;
        }
    } else if (!rhi::appendBindlessShaderEntry(package,
                   vertexEntry,
                   rhi::ShaderStage::Vertex,
                   result.shader,
                   result.diagnostics)
        || !rhi::appendBindlessShaderEntry(
            package, fragmentEntry, rhi::ShaderStage::Fragment, result.shader, result.diagnostics))
        return result;

    result.lowering = fragment.lowering;
    if (pushOwner) {
        // The authenticated selected interface also covers vertex-only push.
        // Its merged slot is authoritative when Fragment declares no block.
        result.lowering.pushConstantGroup = result.shader.reflection.pushConstantWebGpuBindingGroup;
        result.lowering.pushConstantBinding =
            result.shader.reflection.pushConstantWebGpuBindingIndex;
    }
    result.lowering.physicalGroupCount =
        std::max(vertex.lowering.physicalGroupCount, fragment.lowering.physicalGroupCount);
    for (const auto& entry : vertex.lowering.entries) {
        const auto existing = std::find_if(result.lowering.entries.begin(),
            result.lowering.entries.end(),
            [&](const auto& candidate) {
                return candidate.set == entry.set && candidate.binding == entry.binding;
            });
        if (existing != result.lowering.entries.end()) {
            if (existing->group != entry.group || existing->webGpuBinding != entry.webGpuBinding
                || existing->physicalType != entry.physicalType
                || existing->capacity != entry.capacity
                || existing->arrayElement != entry.arrayElement
                || existing->requiresSampler != entry.requiresSampler
                || existing->depthTexture != entry.depthTexture
                || existing->hasLoadSemantics != entry.hasLoadSemantics
                || existing->loadSemantics != entry.loadSemantics) {
                result.diagnostics = "RHI-BINDLESS-WEBGPU-RENDER-MAPPING-MISMATCH";
                return result;
            }
        } else {
            if (std::any_of(result.lowering.entries.begin(),
                    result.lowering.entries.end(),
                    [&](const auto& candidate) {
                        return candidate.group == entry.group
                            && candidate.webGpuBinding == entry.webGpuBinding;
                    })) {
                result.diagnostics = "RHI-BINDLESS-WEBGPU-RENDER-MAPPING-MISMATCH";
                return result;
            }
            result.lowering.entries.push_back(entry);
        }
    }
    for (const auto& binding : vertex.lowering.artifactBindings) {
        const auto existing = std::find_if(result.lowering.artifactBindings.begin(),
            result.lowering.artifactBindings.end(),
            [&](const auto& candidate) {
                return candidate.set == binding.set && candidate.binding == binding.binding;
            });
        if (existing == result.lowering.artifactBindings.end()) {
            result.lowering.artifactBindings.push_back(binding);
        } else if (existing->type != binding.type || existing->arrayCount != binding.arrayCount
            || existing->subpassAttachmentIndex != binding.subpassAttachmentIndex
            || existing->depthTexture != binding.depthTexture) {
            result.diagnostics = "RHI-BINDLESS-WEBGPU-RENDER-REFLECTION-MISMATCH";
            return result;
        } else {
            existing->stages = existing->stages | binding.stages;
        }
    }
    auto activeLayout = rhi::PipelineLayoutDesc{};
    const auto active = buildBindlessActiveLayout(package.artifact(),
        result.shader.reflection,
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        activeLayout);
    if (!active.success) {
        result.diagnostics = active.diagnostics;
        return result;
    }
    activeLayout.pushConstantRanges = result.layout.pushConstantRanges;
    result.layout = std::move(activeLayout);
    const auto layoutValidation = rhi::validatePipelineLayout(result.layout);
    if (!layoutValidation.success) {
        result.diagnostics = layoutValidation.diagnostics;
        return result;
    }
    const auto reflectionValidation =
        rhi::validatePipelineLayoutAgainstShaderReflection(result.layout, result.shader.reflection);
    if (!reflectionValidation.success) {
        result.diagnostics = reflectionValidation.diagnostics;
        return result;
    }
    const auto loweringValidation = validateWebGpuBindingSpaceLowering(result.layout,
        result.shader.reflection,
        result.lowering,
        result.lowering.pushConstantGroup);
    if (!loweringValidation.success) {
        result.diagnostics = loweringValidation.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

rhi::ValidationResult validateWebGpuBindingSpaceLowering(const rhi::PipelineLayoutDesc& layout,
    const rhi::ShaderReflectionDesc& logicalReflection,
    const WebGpuBindingSpaceLowering& lowering,
    std::uint32_t pushConstantGroup)
{
    if (lowering.contractVersion != kContractVersion) {
        return {false, "WebGPU lowering requires WebGpuBindingSpace/5"};
    }
    if (lowering.physicalGroupCount == 0u
        && (!logicalReflection.descriptorBindings.empty() || !layout.descriptorSets.empty()
            || !layout.descriptorTables.empty() || !layout.pushConstantRanges.empty())) {
        return {false, "WebGPU lowering is missing physical bind-group count metadata"};
    }
    if (!layout.pushConstantRanges.empty()
        && (pushConstantGroup == UINT32_MAX || pushConstantGroup >= lowering.physicalGroupCount)) {
        return {false, "WebGPU lowering push-constant group is outside artifact capacity"};
    }
    for (const auto& physical : lowering.entries) {
        if (physical.active && physical.group >= lowering.physicalGroupCount) {
            return {false, "WebGPU lowering entry is outside artifact bind-group capacity"};
        }
    }
    for (const auto& logical : logicalReflection.descriptorBindings) {
        const auto* artifact = findArtifactBinding(lowering, logical.set, logical.binding);
        if (!artifact) {
            return {false, "WebGPU reflection artifact is missing a logical descriptor binding"};
        }
        if (artifact->type != logical.type || artifact->arrayCount != logical.arrayCount
            || artifact->subpassAttachmentIndex != logical.subpassAttachmentIndex
            || artifact->depthTexture != logical.depthTexture) {
            core::pmr::String diagnostics("WebGPU reflection artifact disagrees with shader "
                                          "package descriptor metadata (set=",
                &core::defaultCpuMemoryResource());
            diagnostics += std::to_string(logical.set).c_str();
            diagnostics += ", binding=";
            diagnostics += std::to_string(logical.binding).c_str();
            diagnostics += ", logicalType=";
            diagnostics += std::to_string(static_cast<std::uint32_t>(logical.type)).c_str();
            diagnostics += ", artifactType=";
            diagnostics += std::to_string(static_cast<std::uint32_t>(artifact->type)).c_str();
            diagnostics += ", logicalDepth=";
            diagnostics += logical.depthTexture ? "1" : "0";
            diagnostics += ", artifactDepth=";
            diagnostics += artifact->depthTexture ? "1" : "0";
            diagnostics += ')';
            return {false, std::move(diagnostics)};
        }
        const auto* physical = findEntry(lowering, logical.set, logical.binding);
        if (!physical || !physical->active) {
            core::pmr::String diagnostics(
                "active WebGPU logical descriptor has no physical WGSL lowering (set=",
                &core::defaultCpuMemoryResource());
            diagnostics += std::to_string(logical.set).c_str();
            diagnostics += ", binding=";
            diagnostics += std::to_string(logical.binding).c_str();
            diagnostics += ')';
            return {false, std::move(diagnostics)};
        }
        const auto expectedPhysicalType = logical.type == rhi::DescriptorType::InputAttachment
            ? rhi::DescriptorType::SampledTexture
            : logical.type;
        if (physical->physicalType != expectedPhysicalType) {
            return {false,
                "WebGPU lowering physical descriptor type does not match the code-level contract"};
        }
        if (physical->arrayElement != 0u || physical->capacity != logical.arrayCount
            || physical->capacity == 0u) {
            return {false,
                "WebGpuBindingSpace/5 descriptor-array capacity metadata disagrees with logical "
                "reflection"};
        }
        if (logical.type == rhi::DescriptorType::InputAttachment
            && (!physical->hasLoadSemantics
                || physical->loadSemantics
                    != rhi::semantics::SubpassInputLoadSemantics::CurrentFragment
                || physical->requiresSampler || physical->depthTexture != logical.depthTexture)) {
            return {false,
                "WebGPU InputAttachment lowering is missing current-fragment textureLoad "
                "semantics"};
        }
        for (const auto& other : lowering.entries) {
            if (&other != physical && other.active && other.group == physical->group
                && other.webGpuBinding == physical->webGpuBinding) {
                return {
                    false, "WebGPU lowering maps multiple logical descriptors to one WGSL slot"};
            }
        }
    }

    // The generated artifact's logical binding table includes declarations
    // that may be optimized out of a selected entrypoint. Such declarations
    // are valid only when the same artifact explicitly records an inactive
    // physical entry. An artifact binding with no lowering entry, or with an
    // active lowering entry, must still match the package's logical RHI
    // reflection before native bind-group creation.
    for (const auto& artifact : lowering.artifactBindings) {
        const auto* physical = findEntry(lowering, artifact.set, artifact.binding);
        if (!physical) {
            core::pmr::String diagnostics(
                "WebGPU reflection artifact contains an unmapped logical descriptor binding (set=",
                &core::defaultCpuMemoryResource());
            diagnostics += std::to_string(artifact.set).c_str();
            diagnostics += ", binding=";
            diagnostics += std::to_string(artifact.binding).c_str();
            diagnostics += ')';
            return {false, std::move(diagnostics)};
        }
        if (!physical->active) {
            continue;
        }
        bool matchesLogical = false;
        for (const auto& logical : logicalReflection.descriptorBindings) {
            if (logical.set == artifact.set && logical.binding == artifact.binding) {
                matchesLogical = true;
                break;
            }
        }
        if (!matchesLogical) {
            core::pmr::String diagnostics("WebGPU reflection artifact contains an extra active "
                                          "logical descriptor binding (set=",
                &core::defaultCpuMemoryResource());
            diagnostics += std::to_string(artifact.set).c_str();
            diagnostics += ", binding=";
            diagnostics += std::to_string(artifact.binding).c_str();
            diagnostics += ")";
            return {false, std::move(diagnostics)};
        }
    }
    for (const auto& physical : lowering.entries) {
        if (!physical.active) {
            continue;
        }
        bool matchesLogical = false;
        for (const auto& logical : logicalReflection.descriptorBindings) {
            if (logical.set == physical.set && logical.binding == physical.binding) {
                matchesLogical = true;
                break;
            }
        }
        if (!matchesLogical) {
            core::pmr::String diagnostics(
                "WebGPU lowering contains an extra active physical descriptor binding (set=",
                &core::defaultCpuMemoryResource());
            diagnostics += std::to_string(physical.set).c_str();
            diagnostics += ", binding=";
            diagnostics += std::to_string(physical.binding).c_str();
            diagnostics += ")";
            return {false, std::move(diagnostics)};
        }
    }

    // The artifact owns the physical group count.  Keep this exact rather
    // than treating it as an upper bound: a count with a trailing unused group
    // cannot be represented by the native bind-group layout path without
    // manufacturing an unproven empty group layout.
    std::uint32_t highestPhysicalGroup = 0u;
    bool hasPhysicalGroup = false;
    for (const auto& physical : lowering.entries) {
        if (physical.active) {
            highestPhysicalGroup = std::max(highestPhysicalGroup, physical.group);
            hasPhysicalGroup = true;
        }
    }
    if (!layout.pushConstantRanges.empty()) {
        if (pushConstantGroup == UINT32_MAX) {
            return {false, "WebGPU lowering push-constant group is missing"};
        }
        highestPhysicalGroup = std::max(highestPhysicalGroup, pushConstantGroup);
        hasPhysicalGroup = true;
    }
    if ((hasPhysicalGroup && lowering.physicalGroupCount != highestPhysicalGroup + 1u)
        || (!hasPhysicalGroup && lowering.physicalGroupCount != 0u)) {
        return {false, "WebGPU lowering physical bind-group count is not exact"};
    }

    core::pmr::Vector<std::pair<std::uint32_t, std::uint32_t>> occupiedGroups{
        &core::defaultCpuMemoryResource()};
    for (const auto& slot : layout.descriptorSets) {
        const auto* descriptorLayout = rhi::findPipelineDescriptorLayout(layout, slot.layoutName);
        if (!descriptorLayout) {
            return {
                false, "WebGPU lowering pipeline layout references an unknown descriptor layout"};
        }
        bool groupKnown = false;
        std::uint32_t group = 0;
        for (const auto& descriptor : descriptorLayout->bindings) {
            const auto* physical = findEntry(lowering, slot.set, descriptor.binding);
            if (!physical || !physical->active) {
                continue;
            }
            if (descriptor.type == rhi::DescriptorType::InputAttachment
                && physical->depthTexture != descriptor.depthTexture) {
                return {false,
                    "WebGpuBindingSpace/5 depth input metadata disagrees with descriptor layout"};
            }
            if (!groupKnown) {
                group = physical->group;
                groupKnown = true;
            } else if (group != physical->group) {
                return {false, "one L2 descriptor set maps to multiple WebGPU groups"};
            }
        }
        if (groupKnown && !layout.pushConstantRanges.empty() && group == pushConstantGroup) {
            return {false, "WebGPU descriptor group collides with push-constant lowering group"};
        }
        if (groupKnown && group >= lowering.physicalGroupCount) {
            return {false, "WebGPU descriptor group exceeds artifact bind-group capacity"};
        }
        if (groupKnown) {
            // WebGpuBindingSpace/5 explicitly permits multiple logical sets
            // to share a physical group. The physical (group,binding) check
            // above is the invariant that keeps the merged bind group valid.
            for (const auto& occupied : occupiedGroups) {
                if (occupied.first == group && occupied.second == slot.set) {
                    return {false,
                        "WebGpuBindingSpace/5 contains duplicate logical descriptor-set metadata"};
                }
            }
            occupiedGroups.push_back({group, slot.set});
        }
    }
    for (const auto& table : layout.descriptorTables) {
        const rhi::ShaderReflectionBinding* reflected = nullptr;
        for (const auto& candidate : logicalReflection.descriptorBindings) {
            if (candidate.set == table.set && candidate.type == table.type
                && candidate.arrayCount == table.capacity
                && candidate.depthTexture == table.depthTexture
                && (table.stages & candidate.stages) == candidate.stages
                && candidate.arrayCount != 0u) {
                if (reflected != nullptr) {
                    return {
                        false, "WebGPU descriptor table matches multiple reflected array bindings"};
                }
                reflected = &candidate;
            }
        }
        if (!reflected) {
            return {false, "WebGPU descriptor table has no matching reflected array binding"};
        }
        const auto* physical = findEntry(lowering, reflected->set, reflected->binding);
        if (!physical || !physical->active || physical->arrayElement != 0u
            || physical->capacity != table.capacity || physical->depthTexture != table.depthTexture
            || physical->group >= lowering.physicalGroupCount) {
            return {false, "WebGPU descriptor table lowering metadata is missing or mismatched"};
        }
        if (!layout.pushConstantRanges.empty() && physical->group == pushConstantGroup) {
            return {false, "WebGPU descriptor table collides with push-constant lowering group"};
        }
        for (const auto& occupied : occupiedGroups) {
            if (occupied.first == physical->group && occupied.second == table.set) {
                return {false,
                    "WebGPU descriptor table duplicates a logical descriptor-set metadata entry"};
            }
        }
        occupiedGroups.push_back({physical->group, table.set});
    }
    return {true, {}};
}

} // namespace arp::rhi_webgpu
