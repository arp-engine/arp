#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/descriptor_general_binding_validation.h"

// region original_command_descriptor_dependencies
namespace arp::rhi {
OriginalCommandDescriptorDependencies::PipelineOwner
OriginalCommandDescriptorDependencies::capturePipeline(
    const OriginalCommandResourceSources& sources, std::size_t index) noexcept
{
    if (index >= sources.sources().size())
        return {};
    return std::visit(
        [](const auto& source) -> PipelineOwner {
            using T = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<T,
                              std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>
                || std::is_same_v<T, std::shared_ptr<const OriginalCommandComputePipelineSource>>)
                return source;
            else
                return {};
        },
        sources.sources()[index]);
}

const char* OriginalCommandDescriptorDependencies::validatePipeline(
    const OriginalCommandResourceSources& sources,
    std::size_t index,
    const PipelineOwner& pipeline,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
{
    const auto canonical = capturePipeline(sources, index);
    return std::visit(
        [&](const auto& source) -> const char* {
            using T = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return "RHI-ORIGINAL-FINITE-SOURCE-PIPELINE-POSITION";
            else {
                const auto* original = std::get_if<T>(&canonical);
                if (!original || !core::SharedOwnerProjection::sameOwner(source, *original))
                    return "RHI-ORIGINAL-FINITE-SOURCE-PIPELINE-POSITION";
                using S = std::remove_const_t<typename T::element_type>;
                if (const auto* id = S::validateOriginal(source))
                    return id;
                return core::SharedOwnerProjection::sameOwner(
                           quota, source->originalMetadataQuota())
                    ? nullptr
                    : "RHI-ORIGINAL-DESCRIPTOR-SOURCE-RESOURCE-QUOTA";
            }
        },
        pipeline);
}

const char* OriginalCommandDescriptorDependencies::validateFiniteContent(
    const DescriptorSet& object,
    const PipelineOwner& pipeline,
    std::uint32_t set,
    const OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    std::uint64_t revision,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    std::uint32_t sourceVersion) noexcept
{
    return std::visit(
        [&](const auto& source) -> const char* {
            using T = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return "RHI-ORIGINAL-FINITE-SOURCE-PIPELINE-POSITION";
            else {
                if (!source || !quota)
                    return "RHI-ORIGINAL-FINITE-SOURCE-QUOTA";
                if (sourceVersion == 4u)
                    return object.diagnoseOriginalFiniteTargetMemorySource(
                        {1u, true, revision, quota->limitBytes()},
                        source->originalPipeline(),
                        set,
                        writes,
                        count,
                        quota);
                if (sourceVersion != 3u)
                    return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-VERSION";
                return object.diagnoseOriginalFiniteSource(
                    {1u, true, revision, quota->limitBytes()},
                    source->originalPipeline(),
                    set,
                    writes,
                    count,
                    quota);
            }
        },
        pipeline);
}

const char* OriginalCommandDescriptorDependencies::validateFullBoundView(
    const OriginalCommandDescriptorSetElement& element,
    const DescriptorBindingDesc& binding,
    std::uint64_t bufferBytes) noexcept
{
    return OriginalCommandTypedSourceContract::validateFullBoundView(element, binding, bufferBytes);
}

const char* OriginalCommandDescriptorDependencies::validateGeneralFootprint(
    const OriginalCommandDescriptorSetElement& element,
    const DescriptorBindingDesc& binding,
    const Owner& owner) noexcept
{
    return OriginalGeneralDescriptorBindingContract::validateFootprint(binding, element, owner, 2u);
}

const char* OriginalCommandDescriptorDependencies::validateCompletedDepth(
    const OriginalCommandDescriptorSetElement& element,
    const DescriptorBindingDesc& binding,
    const Owner& owner,
    const std::shared_ptr<const OriginalCompletedDepthTextureSource>& depth,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const OriginalCompletedDepthReadInputContract::Budget& budget) noexcept
{
    const bool required = binding.type == DescriptorType::SampledTexture && binding.depthTexture;
    if (!required)
        return depth ? "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-NONDEPTH" : nullptr;
    if (const auto* id = validateGeneralFootprint(element, binding, owner))
        return id;
    const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&owner);
    if (!view)
        return "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-VIEW";
    const auto input = OriginalCompletedDepthReadInputContract::inspect(
        {depth, element.write.resourceState, element.stages, element.access}, *view, budget);
    if (input.diagnostic)
        return input.diagnostic;
    return quota && core::SharedOwnerProjection::sameOwner(quota, depth->originalMetadataQuota())
        ? nullptr
        : "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-QUOTA";
}

const char* OriginalCommandDescriptorDependencies::validateGeneralContent(
    const DescriptorSet& object,
    const PipelineOwner& pipeline,
    const std::shared_ptr<const OriginalCommandResourceSources>& resources,
    std::uint32_t set,
    const OriginalCommandDescriptorSetFrozenElement* elements,
    std::size_t count,
    std::uint64_t revision,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
{
    return std::visit(
        [&](const auto& source) -> const char* {
            using T = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return "RHI-ORIGINAL-FINITE-SOURCE-PIPELINE-POSITION";
            else {
                if (!source || !quota)
                    return "RHI-ORIGINAL-GENERAL-SOURCE-QUOTA";
                return object.diagnoseOriginalGeneralSource(
                    {1u, true, revision, quota->limitBytes()},
                    source->originalPipeline(),
                    resources,
                    set,
                    elements,
                    count,
                    quota);
            }
        },
        pipeline);
}

const char* OriginalCommandDescriptorDependencies::validateInventoryOwner(
    const std::shared_ptr<const OriginalCommandResourceSources>& sources,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
{
    // This boundary intentionally inspects owner/quota only: recursively
    // validating a parent that owns this descriptor would recurse forever.
    if (!sources || !sources->hasQuotaBoundSharedOwner(sources))
        return "RHI-ORIGINAL-SOURCES-OWNER";
    return core::SharedOwnerProjection::sameOwner(quota, sources->originalMetadataQuota())
        ? nullptr
        : "RHI-ORIGINAL-DESCRIPTOR-SOURCE-RESOURCE-QUOTA";
}

const char* OriginalCommandDescriptorDependencies::validateInventory(
    const std::shared_ptr<const OriginalCommandResourceSources>& sources,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const GpuDevice& device) noexcept
{
    if (const auto* id =
            OriginalCommandResourceSources::validateRegisteredLifetime(sources, device))
        return id;
    return validateInventoryOwner(sources, quota);
}

const char* OriginalCommandDescriptorDependencies::validate(const DescriptorLayoutDesc& definition,
    const OriginalCommandDescriptorSetElement* elements,
    std::size_t count,
    const std::shared_ptr<const OriginalCommandResourceSources>& sources,
    const GpuDevice& device,
    std::uint32_t bindlessSamplerArraysVersion)
{
    return OriginalCommandTypedSourceContract::validateSetElements(
        definition, elements, count, sources, device, bindlessSamplerArraysVersion);
}

const char* OriginalCommandDescriptorDependencies::validate(const DescriptorTableDesc& definition,
    const OriginalCommandDescriptorTableElement* elements,
    std::size_t count,
    const std::shared_ptr<const OriginalCommandResourceSources>& sources,
    const GpuDevice& device)
{
    return OriginalCommandTypedSourceContract::validateTableElements(
        definition, elements, count, sources, device);
}

std::optional<OriginalCommandDescriptorDependencies::Owner>
OriginalCommandDescriptorDependencies::capture(
    const OriginalCommandResourceSources& sources, std::size_t index) noexcept
{
    if (index >= sources.sources().size())
        return {};
    return std::visit(
        [](const auto& lease) -> std::optional<Owner> {
            using T = std::decay_t<decltype(lease)>;
            if constexpr (std::is_same_v<T, std::shared_ptr<const OriginalCommandBufferLease>>
                || std::is_same_v<T, std::shared_ptr<const OriginalCommandTextureViewSource>>
                || std::is_same_v<T, std::shared_ptr<const OriginalCommandSamplerLease>>)
                return lease;
            else
                return {};
        },
        sources.sources()[index]);
}

bool OriginalCommandDescriptorDependencies::matches(const OriginalCommandResourceSources& sources,
    std::size_t index,
    const Owner& dependency) noexcept
{
    const auto canonical = capture(sources, index);
    if (!canonical)
        return false;
    return std::visit(
        [&](const auto& owner) noexcept {
            const auto* original = std::get_if<std::decay_t<decltype(owner)>>(&*canonical);
            return original && core::SharedOwnerProjection::sameOwner(owner, *original);
        },
        dependency);
}

const char* OriginalCommandDescriptorDependencies::validateOwner(const Owner& dependency,
    std::uint64_t generation,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
{
    return std::visit(
        [&](const auto& lease) -> const char* {
            using T = std::remove_const_t<typename std::decay_t<decltype(lease)>::element_type>;
            if (const auto* id = T::validateOriginal(lease))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(quota, lease->originalMetadataQuota()))
                return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-RESOURCE-QUOTA";
            if (const auto* id =
                    lease->originalObject()->originalResourceRegistration().diagnostic())
                return id;
            if (lease->originalObject()->originalResourceRegistration().generation() != generation)
                return "RHI-ORIGINAL-TYPED-DESCRIPTOR-GENERATION";
            if constexpr (std::is_same_v<T, OriginalCommandTextureViewSource>) {
                const auto* parent = lease->originalParentLease()->originalObject();
                if (const auto* id = parent->originalResourceRegistration().diagnostic())
                    return id;
                if (!lease->originalObject()->originalResourceRegistration().matchesParent(
                        parent->originalResourceRegistration()))
                    return "RHI-ORIGINAL-SOURCES-REGISTERED-PARENT";
            }
            return nullptr;
        },
        dependency);
}

const char* OriginalCommandDescriptorDependencies::validateRegistered(
    const Owner& dependency, const GpuDevice& device) noexcept
{
    return std::visit(
        [&](const auto& lease) -> const char* {
            if (const auto* id =
                    device.validateOriginalResourceRegistration(*lease->originalObject()))
                return id;
            using T = std::remove_const_t<typename std::decay_t<decltype(lease)>::element_type>;
            if constexpr (std::is_same_v<T, OriginalCommandTextureViewSource>)
                return device.validateOriginalResourceRegistration(
                    *lease->originalParentLease()->originalObject());
            else
                return nullptr;
        },
        dependency);
}

template <class D>
static const char* validateOriginalDescriptorClosure(const D& object,
    const std::shared_ptr<const DescriptorArena>& arena,
    const GpuDevice& device) noexcept
{
    if (!arena || !arena->sharedOwnerMatches(arena))
        return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-ARENA-OWNER";
    if (const auto* id = device.validateOriginalResourceRegistration(*arena))
        return id;
    if (const auto* id = device.validateOriginalResourceRegistration(object))
        return id;
    if (!object.originalResourceRegistration().matchesParent(arena->originalResourceRegistration()))
        return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-PARENT";
    return object.allocationEpochTicket().sameCurrentEpoch(arena->allocationEpochTicket())
        ? nullptr
        : "RHI-ORIGINAL-DESCRIPTOR-SOURCE-EPOCH";
}

const char* OriginalCommandDescriptorDependencies::validateClosure(const DescriptorSet& object,
    const std::shared_ptr<const DescriptorArena>& arena,
    const GpuDevice& device) noexcept
{
    return validateOriginalDescriptorClosure(object, arena, device);
}

const char* OriginalCommandDescriptorDependencies::validateClosure(const DescriptorTable& object,
    const std::shared_ptr<const DescriptorArena>& arena,
    const GpuDevice& device) noexcept
{
    return validateOriginalDescriptorClosure(object, arena, device);
}
} // namespace arp::rhi

// endregion original_command_descriptor_dependencies
