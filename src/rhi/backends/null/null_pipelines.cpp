#include "core/cpu_allocator.h"
#include "null_backend_internal.h"
#include "rhi/frontend/pipeline_push_constant_abi_identity.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/frontend/pipeline_validation_report_builder.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/shader_reflection_cache.h"

#include <algorithm>
#include <utility>

namespace arp::rhi::null_backend {

namespace {

bool isSlangShader(const ShaderEntrypoint& shader, ShaderStage expectedStage)
{
    if (shader.sourceLanguage != ShaderSourceLanguage::Slang || shader.stage != expectedStage
        || shader.modulePath.empty() || shader.entryPoint.empty()) {
        return false;
    }
    const std::pmr::string suffix = ".slang";
    if (shader.modulePath.size() < suffix.size()) {
        return false;
    }
    return shader.modulePath.compare(
               shader.modulePath.size() - suffix.size(), suffix.size(), suffix)
        == 0;
}

bool packageContainsEntrypoint(const ShaderPackageDesc& package, const ShaderEntrypoint& shader)
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

DescriptorType descriptorTypeFromShaderBinding(shader::BindingType type)
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
    return DescriptorType::UniformBuffer;
}

ShaderStage shaderStageFromReflection(shader::ShaderStage stage)
{
    switch (stage) {
    case shader::ShaderStage::Vertex:
        return ShaderStage::Vertex;
    case shader::ShaderStage::Fragment:
        return ShaderStage::Fragment;
    case shader::ShaderStage::Compute:
        return ShaderStage::Compute;
    case shader::ShaderStage::TessellationControl:
        return ShaderStage::TessellationControl;
    case shader::ShaderStage::TessellationEvaluation:
        return ShaderStage::TessellationEvaluation;
    }
    return ShaderStage::None;
}

bool nullArtifactReflectionMatchesPackage(
    const ShaderPackageDesc& package, std::pmr::string& diagnostics)
{
    if (package.artifacts.reflectionPath.empty()) {
        diagnostics = "RHI-NULL-REFLECTION-ARTIFACT-PATH-MISSING: shader package is missing "
                      "reflection artifact path";
        return false;
    }
    const auto cached =
        shader::ShaderReflectionCache::loadFromFile(package.artifacts.reflectionPath);
    if (!cached.success) {
        diagnostics = "RHI-NULL-REFLECTION-ARTIFACT-LOAD-FAILED: ";
        diagnostics += cached.diagnostics;
        return false;
    }
    const auto artifactIdentityValidation =
        shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(cached.module,
            package.slangModulePath,
            package.artifacts.spirvPath,
            package.artifacts.mslPath,
            package.artifacts.wgslPath);
    if (!artifactIdentityValidation.success) {
        diagnostics = "RHI-NULL-REFLECTION-ARTIFACT-IDENTITY-INVALID: ";
        diagnostics += artifactIdentityValidation.diagnostics;
        return false;
    }
    const auto memberValidation =
        shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(
            cached.module.reflection);
    if (!memberValidation.success) {
        diagnostics = "RHI-NULL-REFLECTION-MEMBER-INVALID: ";
        diagnostics += memberValidation.diagnostics;
        return false;
    }
    const auto& reflected = cached.module.reflection;
    const auto& packageReflection = package.reflection;
    if (reflected.pushConstantSizeBytes != packageReflection.pushConstantByteCount
        || reflected.pushConstantContractVersion != packageReflection.pushConstantContractVersion
        || reflected.pushConstantStages != packageReflection.pushConstantStages
        || reflected.pushConstantWebGpuBindingGroup
            != packageReflection.pushConstantWebGpuBindingGroup
        || reflected.pushConstantWebGpuBindingIndex
            != packageReflection.pushConstantWebGpuBindingIndex
        || reflected.pushConstantMetalBufferIndex != packageReflection.pushConstantMetalBufferIndex
        || reflected.computeWorkgroupSizeX != packageReflection.computeWorkgroupSizeX
        || reflected.computeWorkgroupSizeY != packageReflection.computeWorkgroupSizeY
        || reflected.computeWorkgroupSizeZ != packageReflection.computeWorkgroupSizeZ
        || reflected.tessellationControlPointCount
            != packageReflection.tessellationControlPointCount
        || reflected.metalGpuCountedIndirectContractVersion
            != packageReflection.metalGpuCountedIndirectContractVersion
        || reflected.metalGpuCountedIndirectStatus
            != packageReflection.metalGpuCountedIndirectStatus
        || reflected.metalGpuCountedIndirectCountBufferIndex
            != packageReflection.metalGpuCountedIndirectCountBufferIndex
        || reflected.metalGpuCountedIndirectUsesDrawId
            != packageReflection.metalGpuCountedIndirectUsesDrawId
        || reflected.metalGpuCountedIndirectMasksOutOfRange
            != packageReflection.metalGpuCountedIndirectMasksOutOfRange
        || reflected.metalTessellationContractVersion
            != packageReflection.metalTessellationContractVersion
        || reflected.metalTessellationStatus != packageReflection.metalTessellationStatus
        || reflected.metalTessellationVertexEntryPoint
            != packageReflection.metalTessellationVertexEntryPoint
        || reflected.metalTessellationFactorBufferIndex
            != packageReflection.metalTessellationFactorBufferIndex
        || reflected.metalTessellationControlPointCount
            != packageReflection.metalTessellationControlPointCount
        || reflected.metalTessellationPatchType != packageReflection.metalTessellationPatchType
        || reflected.metalTessellationFactorFormat
            != packageReflection.metalTessellationFactorFormat
        || reflected.metalTessellationFactorBytesPerPatch
            != packageReflection.metalTessellationFactorBytesPerPatch
        || reflected.metalTessellationFactorInstanceStrideBytes
            != packageReflection.metalTessellationFactorInstanceStrideBytes
        || reflected.pushConstantMembers.size() != packageReflection.pushConstantMembers.size()) {
        diagnostics =
            "RHI-NULL-REFLECTION-ABI-MISMATCH: artifact disagrees with package ABI metadata";
        return false;
    }
    if (!ShaderPushConstantAbiIdentity::matchesMembers(
            reflected.pushConstantMembers, packageReflection.pushConstantMembers)) {
        diagnostics =
            "RHI-NULL-REFLECTION-MEMBER-MISMATCH: artifact disagrees with push-constant members";
        return false;
    }
    if (reflected.bindings.size() != packageReflection.descriptorBindings.size()) {
        diagnostics =
            "RHI-NULL-REFLECTION-DESCRIPTOR-MISMATCH: artifact disagrees with descriptor bindings";
        return false;
    }
    for (const auto& binding : reflected.bindings) {
        const auto found = std::find_if(packageReflection.descriptorBindings.begin(),
            packageReflection.descriptorBindings.end(),
            [&](const auto& candidate) {
                return candidate.set == binding.set && candidate.binding == binding.binding;
            });
        if (found == packageReflection.descriptorBindings.end()
            || found->type != descriptorTypeFromShaderBinding(binding.type)
            || found->arrayCount != binding.arrayCount
            || binding.stageMask != static_cast<std::uint32_t>(found->stages)
            || found->subpassAttachmentIndex != binding.subpassAttachmentIndex
            || found->depthTexture != binding.depthTexture) {
            diagnostics = "RHI-NULL-REFLECTION-DESCRIPTOR-SEMANTICS-MISMATCH: artifact disagrees "
                          "with descriptor semantics";
            return false;
        }
    }
    for (const auto& entrypoint : package.entrypoints) {
        const auto found = std::find_if(
            reflected.entryPoints.begin(), reflected.entryPoints.end(), [&](const auto& candidate) {
                return candidate.name == entrypoint.entryPoint
                    && shaderStageFromReflection(candidate.stage) == entrypoint.stage;
            });
        if (found == reflected.entryPoints.end()
            || found->computeWorkgroupSizeX != entrypoint.computeWorkgroupSizeX
            || found->computeWorkgroupSizeY != entrypoint.computeWorkgroupSizeY
            || found->computeWorkgroupSizeZ != entrypoint.computeWorkgroupSizeZ
            || found->writesRenderTargetArrayIndex != entrypoint.writesRenderTargetArrayIndex) {
            diagnostics = "RHI-NULL-REFLECTION-ENTRYPOINT-MISMATCH: artifact disagrees with "
                          "entrypoint metadata";
            return false;
        }
    }
    return true;
}

std::uint32_t totalPushConstantByteCount(const PipelineLayoutDesc& desc)
{
    std::uint32_t byteCount = 0;
    for (const auto& range : desc.pushConstantRanges) {
        const auto rangeEnd = range.offsetBytes + range.sizeBytes;
        if (rangeEnd > byteCount) {
            byteCount = rangeEnd;
        }
    }
    return byteCount;
}

const DescriptorLayoutDesc* findDescriptorLayout(
    const std::pmr::vector<DescriptorLayoutDesc>& descriptorLayouts,
    const std::pmr::string& layoutName)
{
    for (const auto& layout : descriptorLayouts) {
        if (layout.name == layoutName) {
            return &layout;
        }
    }
    return nullptr;
}

bool descriptorSetBindingMatchesLayout(
    const DescriptorLayoutDesc& layout, const ShaderReflectionBinding& binding)
{
    for (const auto& descriptor : layout.bindings) {
        if (descriptor.binding == binding.binding && descriptor.type == binding.type
            && (descriptor.stages & binding.stages) == binding.stages
            && descriptor.arrayCount == binding.arrayCount) {
            return true;
        }
    }
    return false;
}

bool descriptorSetBindingIsDeclaredByLayout(
    const PipelineLayoutDesc& layout, const ShaderReflectionBinding& binding)
{
    for (const auto& slot : layout.descriptorSets) {
        if (slot.set == binding.set) {
            if (layout.descriptorLayouts.empty()) {
                return true;
            }
            const auto* descriptorLayout =
                findDescriptorLayout(layout.descriptorLayouts, slot.layoutName);
            return descriptorLayout
                && descriptorSetBindingMatchesLayout(*descriptorLayout, binding);
        }
    }
    return false;
}

bool descriptorTableBindingIsDeclaredByLayout(
    const PipelineLayoutDesc& layout, const ShaderReflectionBinding& binding)
{
    for (const auto& slot : layout.descriptorTables) {
        if (slot.set == binding.set && slot.type == binding.type
            && (slot.stages & binding.stages) == binding.stages && binding.binding < slot.capacity
            && binding.arrayCount <= slot.capacity - binding.binding) {
            return true;
        }
    }
    return false;
}

bool descriptorTableBindingExceedsCapacity(
    const PipelineLayoutDesc& layout, const ShaderReflectionBinding& binding)
{
    for (const auto& slot : layout.descriptorTables) {
        if (slot.set == binding.set && slot.type == binding.type
            && (slot.stages & binding.stages) == binding.stages
            && (binding.binding >= slot.capacity
                || binding.arrayCount > slot.capacity - binding.binding)) {
            return true;
        }
    }
    return false;
}

bool usesPipelineStages(const ShaderReflectionBinding& binding, ShaderStage pipelineStages)
{
    return (binding.stages & pipelineStages) != ShaderStage::None;
}

ShaderReflectionDesc filterReflectionForStages(
    const ShaderReflectionDesc& reflection, ShaderStage pipelineStages)
{
    auto result = ShaderReflectionStateCopy::copy(reflection);
    result.descriptorBindings.clear();
    result.pushConstantStages.clear();
    if ((pipelineStages & ShaderStage::Vertex) != ShaderStage::None) {
        result.pushConstantStages += "VS";
    }
    if ((pipelineStages & ShaderStage::Fragment) != ShaderStage::None) {
        if (!result.pushConstantStages.empty()) {
            result.pushConstantStages += "|";
        }
        result.pushConstantStages += "FS";
    }
    if ((pipelineStages & ShaderStage::Compute) != ShaderStage::None) {
        if (!result.pushConstantStages.empty()) {
            result.pushConstantStages += "|";
        }
        result.pushConstantStages += "CS";
    }
    for (const auto& binding : reflection.descriptorBindings) {
        if (usesPipelineStages(binding, pipelineStages)) {
            result.descriptorBindings.push_back(binding);
        }
    }
    return result;
}

bool reflectionBindingsMatchLayout(
    const ShaderReflectionDesc& reflection, const PipelineLayoutDesc& layout)
{
    for (const auto& binding : reflection.descriptorBindings) {
        if (!descriptorSetBindingIsDeclaredByLayout(layout, binding)
            && !descriptorTableBindingIsDeclaredByLayout(layout, binding)) {
            return false;
        }
    }
    return true;
}

bool reflectionTableBindingsFitCapacity(
    const ShaderReflectionDesc& reflection, const PipelineLayoutDesc& layout)
{
    for (const auto& binding : reflection.descriptorBindings) {
        if (descriptorTableBindingExceedsCapacity(layout, binding)) {
            return false;
        }
    }
    return true;
}

PipelineValidationReport makeBaseReport(const PipelineLayout* layout,
    const ShaderPackageDesc* shaderPackage,
    std::uint32_t entrypointCount,
    ShaderStage pipelineStages)
{
    PipelineValidationReport report;
    report.backend = Backend::Null;
    report.entrypointCount = entrypointCount;
    if (layout) {
        report.descriptorLayoutCount =
            static_cast<std::uint32_t>(layout->desc().descriptorLayouts.size());
        report.descriptorSetCount = layout->descriptorSetCount();
        report.descriptorTableCount = layout->descriptorTableCount();
        report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
            && layout->backendFallbacksHiddenFromUpperLayer();
        report.nativeHandleExposedToUpperLayer =
            report.nativeHandleExposedToUpperLayer || layout->nativeHandleExposedToUpperLayer();
    }
    if (shaderPackage) {
        const auto pipelineReflection =
            filterReflectionForStages(shaderPackage->reflection, pipelineStages);
        report.descriptorBindingCount =
            static_cast<std::uint32_t>(pipelineReflection.descriptorBindings.size());
        report.pushConstantBytes = shaderPackage->reflection.pushConstantByteCount;
        report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
            && shaderPackage->backendFallbacksHiddenFromUpperLayer;
        report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
            || shaderPackage->nativeHandleExposedToUpperLayer;
    }
    return report;
}

PipelineValidationReport makeFailure(PipelineValidationReport report, std::pmr::string diagnostics)
{
    report.success = false;
    report.diagnostics = std::move(diagnostics);
    return report;
}

class NullPipelineLayout final : public PipelineLayout {
public:
    explicit NullPipelineLayout(PipelineLayoutDesc desc) : desc_(std::move(desc)) {}

    const PipelineLayoutDesc& desc() const override { return desc_; }

    std::uint32_t descriptorSetCount() const override
    {
        return static_cast<std::uint32_t>(desc_.descriptorSets.size());
    }

    std::uint32_t descriptorTableCount() const override
    {
        return static_cast<std::uint32_t>(desc_.descriptorTables.size());
    }

    std::uint32_t pushConstantByteCount() const override
    {
        return totalPushConstantByteCount(desc_);
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

private:
    PipelineLayoutDesc desc_;
};

class NullRenderPipeline final : public RenderPipeline {
public:
    NullRenderPipeline(RenderPipelineDesc desc,
        PipelineValidationReport validation,
        ShaderReflectionDesc reflection)
        : desc_(std::move(desc)), validation_(std::move(validation)),
          reflection_(std::move(reflection))
    {}

    const PipelineLayout* layout() const override { return desc_.layout; }

    const PipelineValidationReport& validationReport() const override { return validation_; }

    const ShaderReflectionDesc& shaderReflection() const override { return reflection_; }

    ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return ShaderSourceLanguage::Slang;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

    rhi::PrimitiveTopology primitiveTopology() const override { return desc_.topology; }

    bool usesPatchListTessellation() const override
    {
        return desc_.topology == rhi::PrimitiveTopology::PatchList;
    }

private:
    RenderPipelineDesc desc_;
    PipelineValidationReport validation_;
    ShaderReflectionDesc reflection_;
};

class NullComputePipeline final : public ComputePipeline {
public:
    NullComputePipeline(ComputePipelineDesc desc,
        PipelineValidationReport validation,
        ShaderReflectionDesc reflection)
        : desc_(std::move(desc)), validation_(std::move(validation)),
          reflection_(std::move(reflection))
    {}

    const PipelineLayout* layout() const override { return desc_.layout; }

    const PipelineValidationReport& validationReport() const override { return validation_; }

    const ShaderReflectionDesc& shaderReflection() const override { return reflection_; }

    ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return ShaderSourceLanguage::Slang;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

private:
    ComputePipelineDesc desc_;
    PipelineValidationReport validation_;
    ShaderReflectionDesc reflection_;
};

} // namespace

void applyPipelineStateValidation(
    PipelineValidationReport& report, const PipelineStateValidationResult& stateValidation)
{
    PipelineValidationReportBuilder::validatedGraphicsState(report, stateValidation);
}

arp::core::UniquePtr<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc)
{
    const auto validation = validatePipelineLayout(desc);
    if (!validation.success) {
        return nullptr;
    }
    if (totalPushConstantByteCount(desc) > 256u) {
        return nullptr;
    }
    return arp::core::makeUnique<NullPipelineLayout>(arp::rhi::PipelineLayoutStateCopy::copy(desc));
}

PipelineValidationReport validateRenderPipeline(const RenderPipelineDesc& desc)
{
    const auto pipelineStages = ShaderStage::Vertex | ShaderStage::Fragment;
    auto report = makeBaseReport(desc.layout, desc.shaderPackage, 2, pipelineStages);
    if (!desc.layout) {
        return makeFailure(report, "render pipeline requires pipeline layout");
    }
    if (!desc.shaderPackage) {
        return makeFailure(report, "render pipeline requires shader package");
    }
    if (!isSlangShader(desc.vertexShader, ShaderStage::Vertex)) {
        return makeFailure(report, "render pipeline vertex shader must be Slang vertex entrypoint");
    }
    if (!isSlangShader(desc.fragmentShader, ShaderStage::Fragment)) {
        return makeFailure(
            report, "render pipeline fragment shader must be Slang fragment entrypoint");
    }

    const auto packageValidation = validateShaderPackage(*desc.shaderPackage);
    if (!packageValidation.success) {
        return makeFailure(report, packageValidation.diagnostics);
    }
    std::pmr::string artifactDiagnostics;
    if (!nullArtifactReflectionMatchesPackage(*desc.shaderPackage, artifactDiagnostics)) {
        return makeFailure(report, artifactDiagnostics);
    }
    report.shaderPackageValidated = true;

    const auto pipelineReflection =
        filterReflectionForStages(desc.shaderPackage->reflection, pipelineStages);
    const auto reflectionValidation =
        validatePipelineLayoutAgainstShaderReflection(desc.layout->desc(), pipelineReflection);
    if (!reflectionValidation.success) {
        return makeFailure(report, reflectionValidation.diagnostics);
    }

    if (!packageContainsEntrypoint(*desc.shaderPackage, desc.vertexShader)) {
        return makeFailure(report, "shader package does not contain render vertex entrypoint");
    }
    if (!packageContainsEntrypoint(*desc.shaderPackage, desc.fragmentShader)) {
        return makeFailure(report, "shader package does not contain render fragment entrypoint");
    }
    if (desc.shaderPackage->reflection.pushConstantByteCount
        > desc.layout->pushConstantByteCount()) {
        return makeFailure(report, "shader reflection push constant bytes exceed pipeline layout");
    }
    if (!reflectionTableBindingsFitCapacity(pipelineReflection, desc.layout->desc())) {
        return makeFailure(
            report, "shader reflection descriptor table binding exceeds pipeline layout capacity");
    }
    if (!reflectionBindingsMatchLayout(pipelineReflection, desc.layout->desc())) {
        return makeFailure(report,
            "shader reflection descriptor binding does not match pipeline descriptor layout");
    }
    report.reflectionMatched = true;

    const auto stateValidation = validateRenderPipelineState(desc);
    applyPipelineStateValidation(report, stateValidation);
    if (!stateValidation.success) {
        return makeFailure(report, stateValidation.diagnostics);
    }

    report.success = true;
    return report;
}

arp::core::UniquePtr<RenderPipeline> createRenderPipeline(const RenderPipelineDesc& desc)
{
    const auto validation = validateRenderPipeline(desc);
    if (!validation.success) {
        return nullptr;
    }
    return arp::core::makeUnique<NullRenderPipeline>(desc,
        validation,
        filterReflectionForStages(
            desc.shaderPackage->reflection, ShaderStage::Vertex | ShaderStage::Fragment));
}

PipelineValidationReport validateComputePipeline(const ComputePipelineDesc& desc)
{
    const auto pipelineStages = ShaderStage::Compute;
    auto report = makeBaseReport(desc.layout, desc.shaderPackage, 1, pipelineStages);
    if (!desc.layout) {
        return makeFailure(report, "compute pipeline requires pipeline layout");
    }
    if (!desc.shaderPackage) {
        return makeFailure(report, "compute pipeline requires shader package");
    }
    if (!isSlangShader(desc.computeShader, ShaderStage::Compute)) {
        return makeFailure(report, "compute pipeline shader must be Slang compute entrypoint");
    }

    const auto packageValidation = validateShaderPackage(*desc.shaderPackage);
    if (!packageValidation.success) {
        return makeFailure(report, packageValidation.diagnostics);
    }
    std::pmr::string artifactDiagnostics;
    if (!nullArtifactReflectionMatchesPackage(*desc.shaderPackage, artifactDiagnostics)) {
        return makeFailure(report, artifactDiagnostics);
    }
    report.shaderPackageValidated = true;

    const auto pipelineReflection =
        filterReflectionForStages(desc.shaderPackage->reflection, pipelineStages);
    const auto reflectionValidation =
        validatePipelineLayoutAgainstShaderReflection(desc.layout->desc(), pipelineReflection);
    if (!reflectionValidation.success) {
        return makeFailure(report, reflectionValidation.diagnostics);
    }

    if (!packageContainsEntrypoint(*desc.shaderPackage, desc.computeShader)) {
        return makeFailure(report, "shader package does not contain compute entrypoint");
    }
    if (desc.shaderPackage->reflection.pushConstantByteCount
        > desc.layout->pushConstantByteCount()) {
        return makeFailure(report, "shader reflection push constant bytes exceed pipeline layout");
    }
    if (!reflectionTableBindingsFitCapacity(pipelineReflection, desc.layout->desc())) {
        return makeFailure(
            report, "shader reflection descriptor table binding exceeds pipeline layout capacity");
    }
    if (!reflectionBindingsMatchLayout(pipelineReflection, desc.layout->desc())) {
        return makeFailure(report,
            "shader reflection descriptor binding does not match pipeline descriptor layout");
    }
    report.reflectionMatched = true;
    report.success = true;
    return report;
}

arp::core::UniquePtr<ComputePipeline> createComputePipeline(const ComputePipelineDesc& desc)
{
    const auto validation = validateComputePipeline(desc);
    if (!validation.success) {
        return nullptr;
    }
    return arp::core::makeUnique<NullComputePipeline>(desc,
        validation,
        filterReflectionForStages(desc.shaderPackage->reflection, ShaderStage::Compute));
}

} // namespace arp::rhi::null_backend
