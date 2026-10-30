#include "rhi/frontend/artifact_owned_shader_reflection.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/pipeline_bindless_interface_merger.h"
#include "rhi/frontend/pipeline_push_constant_abi_identity.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/gpu_pipeline.h"
#include "shader/graphics_shader_module_artifact.h"
#include "shader/single_entry_shader_artifact.h"

namespace arp::rhi {

ValidationResult validateSingleEntryVertexInput(
    const VertexInputConsumptionContract& input, const shader::SingleEntryShaderArtifact& artifact)
{
    if (input.diagnostic())
        return {false, input.diagnostic()};
    if (artifact.stage() != shader::ShaderStage::Vertex)
        return {false, "RHI-VERTEX-INPUT-ARTIFACT-STAGE"};
    return validateReflectedVertexInput(input, artifact.module().reflection);
}

ValidationResult validateReflectedVertexInput(
    const VertexInputConsumptionContract& input, const shader::ShaderReflection& reflection)
{
    if (input.diagnostic())
        return {false, input.diagnostic()};
    const auto& reflected = reflection.vertexAttributes;
    if (reflected.size() != input.attributes().size())
        return {false, "RHI-VERTEX-INPUT-ARTIFACT-COUNT"};
    for (const auto& attribute : input.attributes()) {
        const auto found = std::find_if(reflected.begin(), reflected.end(), [&](const auto& a) {
            return a.location == attribute.layout.shaderLocation;
        });
        if (found == reflected.end())
            return {false, "RHI-VERTEX-INPUT-ARTIFACT-LOCATION"};
        if (std::find_if(found + 1,
                reflected.end(),
                [&](const auto& a) {
                    return a.location == found->location;
                })
            != reflected.end())
            return {false, "RHI-VERTEX-INPUT-ARTIFACT-DUPLICATE"};
        std::optional<VertexFormat> format;
        switch (found->format) {
        case shader::VertexAttributeFormat::Float:
            format = VertexFormat::Float32x1;
            break;
        case shader::VertexAttributeFormat::Float2:
            format = VertexFormat::Float32x2;
            break;
        case shader::VertexAttributeFormat::Float3:
            format = VertexFormat::Float32x3;
            break;
        case shader::VertexAttributeFormat::Float4:
            format = VertexFormat::Float32x4;
            break;
        default:
            return {false, "RHI-VERTEX-INPUT-ARTIFACT-FORMAT-UNKNOWN"};
        }
        if (*format != attribute.layout.format)
            return {false, "RHI-VERTEX-INPUT-ARTIFACT-FORMAT-MISMATCH"};
    }
    return {true, {}};
}

std::optional<DescriptorType> descriptorTypeFromShaderBinding(shader::BindingType type) noexcept
{
    switch (type) {
    case shader::BindingType::UniformBuffer:
        return DescriptorType::UniformBuffer;
    case shader::BindingType::StorageBuffer:
        return DescriptorType::StorageBuffer;
    case shader::BindingType::ReadOnlyStorageBuffer:
        return DescriptorType::ReadOnlyStorageBuffer;
    case shader::BindingType::SampledTexture:
        return DescriptorType::SampledTexture;
    case shader::BindingType::StorageTexture:
        return DescriptorType::StorageTexture;
    case shader::BindingType::Sampler:
        return DescriptorType::Sampler;
    case shader::BindingType::InputAttachment:
        return DescriptorType::InputAttachment;
    }
    return std::nullopt;
}

namespace {
// Private cold conversion policy. Only original-owner entry points select the
// diagnosed profile; raw metadata/ordinary conversion always require MSL.
enum class ShaderInterfacePolicy {
    CompleteTargets,
    RuntimeLengthAvailableTargets,
    AuthorizedBindless
};

ValidationResult validateShaderInterfacePackage(
    const ShaderPackageDesc& package, ShaderInterfacePolicy policy)
{
    if (policy == ShaderInterfacePolicy::CompleteTargets)
        return validateShaderPackage(package);
    if (policy == ShaderInterfacePolicy::AuthorizedBindless)
        return validateAuthorizedBindlessShaderPackage(package);
    const auto authorization = validateShaderPackageAuthorization(package, false);
    if (!authorization.success)
        return authorization;
    const auto source = validateShaderPackageSourcePaths(package);
    if (!source.success)
        return source;
    if (!package.artifacts.mslPath.empty()
        || !hasShaderPathSuffix(package.artifacts.wgslPath, ".wgsl"))
        return {false, "RHI-ORIGINAL-AVAILABLE-TARGET-INTERFACE-ROLES"};
    const auto auxiliary = validateShaderPackageAuxiliaryPaths(package);
    if (!auxiliary.success)
        return auxiliary;
    return validateShaderPackageInterface(package);
}

ValidationResult populateSingleEntryShaderInterfaceImpl(
    const shader::SingleEntryShaderArtifact& artifact,
    ShaderPackageDesc& output,
    ShaderInterfacePolicy policy)
{
    output = ShaderPackageDesc{};
    const auto& module = artifact.module();
    const auto& reflection = module.reflection;
    ShaderPackageDesc result;
    result.label = module.moduleName;
    result.slangModulePath = artifact.filename(shader::ShaderArtifactRole::Source);
    result.artifacts.spirvPath = artifact.filename(shader::ShaderArtifactRole::Spirv);
    result.artifacts.mslPath = artifact.filename(shader::ShaderArtifactRole::Metal);
    result.artifacts.wgslPath = artifact.filename(shader::ShaderArtifactRole::Wgsl);
    result.artifacts.reflectionPath = artifact.filename(shader::ShaderArtifactRole::Reflection);
    copyArtifactOwnedShaderReflection(result.reflection, reflection);
    for (const auto& binding : reflection.bindings) {
        const auto type = descriptorTypeFromShaderBinding(binding.type);
        if (!type)
            return {false, "RHI-SINGLE-ENTRY-BINDING-TYPE-INVALID"};
        result.reflection.descriptorBindings.push_back({binding.set,
            binding.binding,
            *type,
            static_cast<ShaderStage>(binding.stageMask),
            binding.arrayCount,
            binding.subpassAttachmentIndex,
            binding.depthTexture});
    }
    ShaderEntrypoint entry;
    entry.modulePath = result.slangModulePath;
    entry.entryPoint = artifact.entry();
    const auto stage = shaderStageFromCompilerStage(artifact.stage());
    if (!stage)
        return {false, "RHI-SINGLE-ENTRY-STAGE-INVALID"};
    entry.stage = *stage;
    const auto& exact = reflection.entryPoints.front();
    entry.computeWorkgroupSizeX = exact.computeWorkgroupSizeX;
    entry.computeWorkgroupSizeY = exact.computeWorkgroupSizeY;
    entry.computeWorkgroupSizeZ = exact.computeWorkgroupSizeZ;
    entry.writesRenderTargetArrayIndex = exact.writesRenderTargetArrayIndex;
    result.entrypoints.push_back(std::move(entry));
    result.generatedReflectionHydrated = true;
    const auto valid = validateShaderInterfacePackage(result, policy);
    if (!valid.success)
        return valid;
    output = std::move(result);
    return {true, {}};
}

ValidationResult mergeShaderInterfaceImpl(std::uint32_t version,
    const ShaderPackageDesc& vertex,
    const ShaderPackageDesc& fragment,
    ShaderPackageDesc& output,
    ShaderInterfacePolicy policy)
{
    output = ShaderPackageDesc{};
    if (version != 1u)
        return {false, "RHI-GRAPHICS-MERGE-VERSION"};
    if (vertex.entrypoints.size() != 1u || fragment.entrypoints.size() != 1u
        || vertex.entrypoints.front().stage != ShaderStage::Vertex
        || fragment.entrypoints.front().stage != ShaderStage::Fragment)
        return {false, "RHI-GRAPHICS-MERGE-SELECTED-STAGE"};
    if (vertex.slangModulePath != fragment.slangModulePath || vertex.reflection.sourceSha256.empty()
        || vertex.reflection.sourceSha256 != fragment.reflection.sourceSha256
        || vertex.reflection.reflectionContractVersion
            != fragment.reflection.reflectionContractVersion)
        return {false, "RHI-GRAPHICS-MERGE-SOURCE-IDENTITY"};
    const auto vertexValid = validateShaderInterfacePackage(vertex, policy);
    if (!vertexValid.success)
        return vertexValid;
    const auto fragmentValid = validateShaderInterfacePackage(fragment, policy);
    if (!fragmentValid.success)
        return fragmentValid;
    const auto& a = vertex.reflection;
    const auto& b = fragment.reflection;
    if (a.pushConstantByteCount && b.pushConstantByteCount
        && (a.pushConstantByteCount != b.pushConstantByteCount
            || a.pushConstantContractVersion != b.pushConstantContractVersion
            || a.pushConstantWebGpuBindingGroup != b.pushConstantWebGpuBindingGroup
            || a.pushConstantWebGpuBindingIndex != b.pushConstantWebGpuBindingIndex
            || a.pushConstantMetalBufferIndex != b.pushConstantMetalBufferIndex
            || !ShaderPushConstantAbiIdentity::matchesMembers(
                a.pushConstantMembers, b.pushConstantMembers)))
        return {false, "RHI-GRAPHICS-MERGE-PUSH-ABI"};
    const auto first = a.pushConstantByteCount
        ? parseShaderReflectionPushConstantStages(a.pushConstantStages)
        : std::optional<ShaderStage>(ShaderStage::None);
    const auto second = b.pushConstantByteCount
        ? parseShaderReflectionPushConstantStages(b.pushConstantStages)
        : std::optional<ShaderStage>(ShaderStage::None);
    if (!first || !second
        || ((*first | *second) != ShaderStage::None && (*first | *second) != ShaderStage::Vertex
            && (*first | *second) != ShaderStage::Fragment
            && (*first | *second) != (ShaderStage::Vertex | ShaderStage::Fragment)))
        return {false, "RHI-GRAPHICS-MERGE-PUSH-STAGES"};
    auto result = ShaderPackageStateCopy::copy(vertex);
    if (!a.pushConstantByteCount && b.pushConstantByteCount) {
        auto& merged = result.reflection;
        merged.pushConstantByteCount = b.pushConstantByteCount;
        merged.pushConstantContractVersion = b.pushConstantContractVersion;
        merged.pushConstantWebGpuBindingGroup = b.pushConstantWebGpuBindingGroup;
        merged.pushConstantWebGpuBindingIndex = b.pushConstantWebGpuBindingIndex;
        merged.pushConstantMetalBufferIndex = b.pushConstantMetalBufferIndex;
        merged.pushConstantMembers.resize(b.pushConstantMembers.size());
        merged.pushConstantMembers = b.pushConstantMembers;
    }
    const auto stages = *first | *second;
    if (stages == ShaderStage::Vertex)
        result.reflection.pushConstantStages = "VS";
    else if (stages == ShaderStage::Fragment)
        result.reflection.pushConstantStages = "FS";
    else if (stages == (ShaderStage::Vertex | ShaderStage::Fragment))
        result.reflection.pushConstantStages = "VS|FS";
    for (const auto& binding : b.descriptorBindings) {
        const auto existing = std::find_if(result.reflection.descriptorBindings.begin(),
            result.reflection.descriptorBindings.end(),
            [&](const auto& candidate) {
                return candidate.set == binding.set && candidate.binding == binding.binding;
            });
        if (existing == result.reflection.descriptorBindings.end())
            result.reflection.descriptorBindings.push_back(binding);
        else {
            if (existing->type != binding.type || existing->arrayCount != binding.arrayCount
                || existing->depthTexture != binding.depthTexture
                || existing->subpassAttachmentIndex != binding.subpassAttachmentIndex)
                return {false, "RHI-GRAPHICS-MERGE-LOGICAL-BINDING"};
            existing->stages = existing->stages | binding.stages;
        }
    }
    result.entrypoints.resize(2u);
    result.entrypoints[1] = fragment.entrypoints.front();
    result.generatedReflectionHydrated =
        vertex.generatedReflectionHydrated && fragment.generatedReflectionHydrated;
    const auto valid = validateShaderInterfacePackage(result, policy);
    if (!valid.success)
        return valid;
    output = std::move(result);
    return {true, {}};
}

ValidationResult populateSelectedGraphicsShaderInterfaceImpl(
    const shader::SingleEntryShaderArtifact& vertex,
    const shader::SingleEntryShaderArtifact* fragment,
    ShaderPackageDesc& output,
    ShaderInterfacePolicy policy)
{
    output = ShaderPackageDesc{};
    if (vertex.stage() != shader::ShaderStage::Vertex
        || (fragment && fragment->stage() != shader::ShaderStage::Fragment))
        return {false, "RHI-GRAPHICS-MERGE-SELECTED-STAGE"};
    ShaderPackageDesc first;
    const auto valid = populateSingleEntryShaderInterfaceImpl(vertex, first, policy);
    if (!valid.success)
        return valid;
    if (!fragment) {
        output = std::move(first);
        return {true, {}};
    }
    ShaderPackageDesc second;
    const auto fragmentValid = populateSingleEntryShaderInterfaceImpl(*fragment, second, policy);
    if (!fragmentValid.success)
        return fragmentValid;
    return mergeShaderInterfaceImpl(1u, first, second, output, policy);
}

ValidationResult populateGraphicsModuleShaderInterfaceImpl(
    const shader::GraphicsShaderModuleArtifact& artifact,
    ShaderPackageDesc& output,
    ShaderInterfacePolicy policy)
{
    output = ShaderPackageDesc{};
    ShaderPackageDesc result;
    const auto valid = populateSelectedGraphicsShaderInterfaceImpl(
        *artifact.vertex(), artifact.fragment().get(), result, policy);
    if (!valid.success)
        return valid;
    const auto& combined = artifact.module().reflection;
    const auto& merged = result.reflection;
    if (combined.sourceSha256 != merged.sourceSha256
        || combined.reflectionContractVersion != merged.reflectionContractVersion
        || combined.pushConstantSizeBytes != merged.pushConstantByteCount
        || combined.pushConstantContractVersion != merged.pushConstantContractVersion
        || combined.pushConstantWebGpuBindingGroup != merged.pushConstantWebGpuBindingGroup
        || combined.pushConstantWebGpuBindingIndex != merged.pushConstantWebGpuBindingIndex
        || combined.pushConstantMetalBufferIndex != merged.pushConstantMetalBufferIndex
        || !ShaderPushConstantAbiIdentity::matchesMembers(
            combined.pushConstantMembers, merged.pushConstantMembers))
        return {false, "RHI-GRAPHICS-MODULE-MERGED-ABI"};
    if (combined.pushConstantSizeBytes
        && parseShaderReflectionPushConstantStages(combined.pushConstantStages)
            != parseShaderReflectionPushConstantStages(merged.pushConstantStages))
        return {false, "RHI-GRAPHICS-MODULE-MERGED-PUSH-STAGES"};
    if (combined.bindings.size() != merged.descriptorBindings.size())
        return {false, "RHI-GRAPHICS-MODULE-MERGED-BINDINGS"};
    for (const auto& binding : combined.bindings) {
        const auto type = descriptorTypeFromShaderBinding(binding.type);
        const auto existing = std::find_if(merged.descriptorBindings.begin(),
            merged.descriptorBindings.end(),
            [&](const auto& candidate) {
                return candidate.set == binding.set && candidate.binding == binding.binding;
            });
        if (!type || existing == merged.descriptorBindings.end() || existing->type != *type
            || existing->arrayCount != binding.arrayCount
            || existing->depthTexture != binding.depthTexture
            || existing->subpassAttachmentIndex != binding.subpassAttachmentIndex
            || existing->stages != static_cast<ShaderStage>(binding.stageMask))
            return {false, "RHI-GRAPHICS-MODULE-MERGED-BINDINGS"};
    }
    copyArtifactOwnedShaderReflection(result.reflection, combined);
    result.artifacts.spirvPath = artifact.filename(shader::ShaderArtifactRole::Spirv);
    result.artifacts.mslPath = artifact.filename(shader::ShaderArtifactRole::Metal);
    result.artifacts.wgslPath = artifact.filename(shader::ShaderArtifactRole::Wgsl);
    result.artifacts.reflectionPath = artifact.filename(shader::ShaderArtifactRole::Reflection);
    const auto complete = validateShaderInterfacePackage(result, policy);
    if (!complete.success)
        return complete;
    output = std::move(result);
    return {true, {}};
}

} // namespace

ValidationResult populateSingleEntryShaderInterface(
    const shader::SingleEntryShaderArtifact& artifact, ShaderPackageDesc& output)
{
    return populateSingleEntryShaderInterfaceImpl(
        artifact, output, ShaderInterfacePolicy::CompleteTargets);
}

ValidationResult GraphicsShaderInterfaceMerger::merge(std::uint32_t version,
    const ShaderPackageDesc& vertex,
    const ShaderPackageDesc& fragment,
    ShaderPackageDesc& output)
{
    return mergeShaderInterfaceImpl(
        version, vertex, fragment, output, ShaderInterfacePolicy::CompleteTargets);
}

ValidationResult mergeBindlessGraphicsShaderInterface(std::uint32_t version,
    const ShaderPackageDesc& vertex,
    const ShaderPackageDesc& fragment,
    ShaderPackageDesc& output)
{
    return mergeShaderInterfaceImpl(
        version, vertex, fragment, output, ShaderInterfacePolicy::AuthorizedBindless);
}

ValidationResult populateSelectedGraphicsShaderInterface(
    const shader::SingleEntryShaderArtifact& vertex,
    const shader::SingleEntryShaderArtifact* fragment,
    ShaderPackageDesc& output)
{
    return populateSelectedGraphicsShaderInterfaceImpl(
        vertex, fragment, output, ShaderInterfacePolicy::CompleteTargets);
}

ValidationResult populateGraphicsModuleShaderInterface(
    const shader::GraphicsShaderModuleArtifact& artifact, ShaderPackageDesc& output)
{
    return populateGraphicsModuleShaderInterfaceImpl(
        artifact, output, ShaderInterfacePolicy::CompleteTargets);
}

ValidationResult populateRuntimeLengthShaderInterface(
    const std::shared_ptr<const shader::SingleEntryShaderArtifact>& artifact,
    ShaderPackageDesc& output)
{
    output = {};
    if (const auto* id =
            shader::SingleEntryShaderArtifact::runtimeLengthAvailableTargetDiagnostic(artifact))
        return {false, id};
    return populateSingleEntryShaderInterfaceImpl(
        *artifact, output, ShaderInterfacePolicy::RuntimeLengthAvailableTargets);
}

ValidationResult populateRuntimeLengthShaderInterface(
    const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& artifact,
    ShaderPackageDesc& output)
{
    output = {};
    if (const auto* id =
            shader::GraphicsShaderModuleArtifact::runtimeLengthAvailableTargetDiagnostic(artifact))
        return {false, id};
    return populateGraphicsModuleShaderInterfaceImpl(
        *artifact, output, ShaderInterfacePolicy::RuntimeLengthAvailableTargets);
}

void copyArtifactOwnedShaderReflection(
    ShaderReflectionDesc& destination, const shader::ShaderReflection& source)
{
    ArtifactOwnedShaderReflection::copy(destination, source);
}

} // namespace arp::rhi
