#include "rhi/backends/webgpu/webgpu_original_depth_table_expansion_binding_space.h"

#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"

namespace arp::rhi_webgpu {
namespace {
void require(bool condition)
{
    shader::requireShaderArtifact(condition, "RHI-WEBGPU-TABLE-SPACE-MAP");
}
} // namespace

void WebGpuOriginalDepthTableExpansionBindingSpace::populate(
    const shader::OriginalDepthTableExpansionArtifact& artifact,
    const rhi::ShaderPackageDesc& package)
{
    const bool compute =
        artifact.profile() == shader::OriginalDepthTableExpansionArtifact::Profile::Compute;
    const auto& reflection = artifact.entry(compute ? 0u : 1u).physical.reflection;
    require(reflection.webGpuBindingSpaceContractVersion == kWebGpuBindingSpaceContractVersion
        && reflection.webGpuBindingSpaceStatus == "Available");
    count_ = compute ? 4u : 3u;
    groupCount_ = reflection.webGpuBindingSpaceGroupCount;
    require(reflection.webGpuBindingSpace.size() == count_ && reflection.bindings.size() == count_);
    for (std::uint32_t i = 0u; i < count_; ++i) {
        const bool table = i < artifact.elements().size();
        const auto& logical = package.reflection.descriptorBindings.at(table ? 0u : 1u);
        const auto set = table ? artifact.elements()[i].set : logical.set;
        const auto binding = table ? artifact.elements()[i].binding : logical.binding;
        const core::StringView name = table ? core::StringView(artifact.elements()[i].name)
                                            : core::StringView(artifact.logicalBindings()[1u].name);
        const auto physical = std::find_if(reflection.webGpuBindingSpace.begin(),
            reflection.webGpuBindingSpace.end(),
            [&](const auto& entry) {
                return entry.name == name && entry.set == set && entry.binding == binding;
            });
        require(physical != reflection.webGpuBindingSpace.end());
        const auto declaration = std::find_if(
            reflection.bindings.begin(), reflection.bindings.end(), [&](const auto& entry) {
                return entry.name == name && entry.set == set && entry.binding == binding;
            });
        require(declaration != reflection.bindings.end());
        const auto type = rhi::descriptorTypeFromShaderBinding(physical->physicalType);
        require(type && *type == logical.type && physical->active && physical->capacity == 1u
            && !physical->arrayElement && !physical->requiresSampler
            && physical->loadSemantics.empty() && physical->depthTexture == logical.depthTexture
            && physical->group < groupCount_ && declaration->arrayCount == 1u
            && declaration->depthTexture == logical.depthTexture
            && declaration->type == physical->physicalType
            && declaration->stageMask == static_cast<std::uint32_t>(logical.stages));
        auto& entry = entries_[i];
        entry.logical = logical;
        entry.arrayElement = table ? artifact.elements()[i].arrayElement : 0u;
        entry.physicalSet = set;
        entry.physicalBinding = binding;
        entry.group = physical->group;
        entry.binding = physical->webGpuBinding;
        entry.physicalCardinality = physical->capacity;
        entry.physicalName = physical->name;
        for (std::uint32_t previous = 0u; previous < i; ++previous)
            require(entries_[previous].group != entry.group
                || entries_[previous].binding != entry.binding);
    }
}

template <class Input>
std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace>
WebGpuOriginalDepthTableExpansionBindingSpace::prepareImpl(
    const std::shared_ptr<const Input>& input,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (budget.contractVersion != 1u) {
        diagnostic = "RHI-WEBGPU-TABLE-SPACE-VERSION";
        return {};
    }
    if (!budget.prototypeOptIn) {
        diagnostic = "RHI-WEBGPU-TABLE-SPACE-OPT-IN";
        return {};
    }
    if (const auto* id = Input::validateTableExpansion(input)) {
        diagnostic = id;
        return {};
    }
    if (!quota || !budget.maxCpuMetadataBytes
        || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-TABLE-SPACE-QUOTA";
        return {};
    }
    for (const auto& domain : std::array{input->quota(), input->tableExpansion()->quota()})
        if (core::SharedOwnerProjection::sameControlBlock(domain, quota)) {
            diagnostic = "RHI-WEBGPU-TABLE-SPACE-QUOTA-DOMAIN";
            return {};
        }
    const auto count = input->tableExpansion()->profile()
            == shader::OriginalDepthTableExpansionArtifact::Profile::Compute
        ? 4u
        : 3u;
    if (budget.maxPhysicalBindings < count) {
        diagnostic = "RHI-WEBGPU-TABLE-SPACE-COUNT-BUDGET";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<WebGpuOriginalDepthTableExpansionBindingSpace>(
            ConstructionKey{}, budget, input);
        result->populate(*input->tableExpansion(), input->package());
        return shareOriginal(std::move(result));
    } catch (const shader::ShaderArtifactLoadError& error) {
        diagnostic = error.diagnostic;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-TABLE-SPACE-ALLOCATION";
    }
    return {};
}

std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace>
WebGpuOriginalDepthTableExpansionBindingSpace::prepare(
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    return prepareImpl(input, budget, quota, diagnostic);
}

std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace>
WebGpuOriginalDepthTableExpansionBindingSpace::prepare(
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    return prepareImpl(input, budget, quota, diagnostic);
}
} // namespace arp::rhi_webgpu
