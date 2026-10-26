#include "rhi/frontend/command_sources.h"
#include "rhi/gpu_command_sources.h"

namespace arp::rhi {
namespace {
template <class T>
const char* inspectSource(const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const GpuDevice& device,
    std::size_t index,
    std::shared_ptr<const T>& output) noexcept
{
    output = {};
    if (const auto* id = OriginalCommandSourceAssembly::validate(inventory, device))
        return id;
    if (index >= inventory->sources().size())
        return "RHI-ORIGINAL-SOURCES-OBSERVATION-INDEX";
    const auto* source = std::get_if<std::shared_ptr<const T>>(&inventory->sources()[index]);
    if (!source)
        return "RHI-ORIGINAL-SOURCES-OBSERVATION-KIND";
    // validateRegisteredLifetime authenticates the actual typed source and
    // original issuer, including each source's canonical shared owner.
    output = *source;
    return nullptr;
}
} // namespace

const char* OriginalCommandSourceAssembly::validate(
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const GpuDevice& device) noexcept
{
    return OriginalCommandResourceSources::validateRegisteredLifetime(inventory, device);
}

bool OriginalCommandSourceAssembly::sealed(const OriginalCommandResourceSources& inventory) noexcept
{
    return inventory.inventorySealed();
}

std::size_t OriginalCommandSourceAssembly::size(
    const OriginalCommandResourceSources& inventory) noexcept
{
    return inventory.sources().size();
}

const std::shared_ptr<core::CpuMemoryQuota>& OriginalCommandSourceAssembly::quota(
    const OriginalCommandResourceSources& inventory) noexcept
{
    return inventory.originalMetadataQuota();
}

const char* OriginalCommandSourceAssembly::cpuOwnerCount(
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const GpuDevice& device,
    std::uint32_t roots,
    std::uint32_t& owners) noexcept
{
    owners = 0u;
    if (const auto* id = validate(inventory, device))
        return id;
    std::uint32_t candidate = 0u;
    if (const auto* id =
            inventory->cpuOwnerCount(roots, inventory->originalBudget().maxGraphOwners, candidate))
        return id;
    owners = candidate;
    return nullptr;
}

const char* OriginalCommandSourceAssembly::inspectBuffer(
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const GpuDevice& device,
    std::size_t index,
    std::shared_ptr<const OriginalCommandBufferLease>& output) noexcept
{
    return inspectSource(inventory, device, index, output);
}

const char* OriginalCommandSourceAssembly::inspectTextureView(
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const GpuDevice& device,
    std::size_t index,
    TextureViewObservation& output) noexcept
{
    output = {};
    std::shared_ptr<const OriginalCommandTextureViewSource> source;
    if (const auto* id = inspectSource(inventory, device, index, source))
        return id;
    output = {source->originalViewLease(), source->originalParentLease()};
    return nullptr;
}

const char* OriginalCommandSourceAssembly::inspectComputePipeline(
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const GpuDevice& device,
    std::size_t index,
    std::shared_ptr<const ComputePipeline>& output) noexcept
{
    output = {};
    std::shared_ptr<const OriginalCommandComputePipelineSource> source;
    if (const auto* id = inspectSource(inventory, device, index, source))
        return id;
    output = source->originalPipeline();
    return nullptr;
}

const char* OriginalCommandSourceAssembly::inspectGraphicsPipeline(
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const GpuDevice& device,
    std::size_t index,
    std::shared_ptr<const RenderPipeline>& output) noexcept
{
    output = {};
    std::shared_ptr<const OriginalCommandGraphicsPipelineSource> source;
    if (const auto* id = inspectSource(inventory, device, index, source))
        return id;
    output = source->originalPipeline();
    return nullptr;
}

const char* OriginalCommandSourceAssembly::descriptorForPipeline(
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const GpuDevice& device,
    std::size_t pipeline,
    bool& present) noexcept
{
    present = false;
    if (const auto* id = validate(inventory, device))
        return id;
    if (pipeline >= inventory->sources().size())
        return "RHI-ORIGINAL-SOURCES-OBSERVATION-INDEX";
    if (!std::holds_alternative<std::shared_ptr<const OriginalCommandComputePipelineSource>>(
            inventory->sources()[pipeline])
        && !std::holds_alternative<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
            inventory->sources()[pipeline]))
        return "RHI-ORIGINAL-SOURCES-OBSERVATION-KIND";
    for (const auto& source : inventory->sources())
        if (const auto* set =
                std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(&source))
            if ((*set)->frozenPipelineSourceIndex() == pipeline) {
                present = true;
                break;
            }
    return nullptr;
}

std::uint32_t OriginalCommandSourceAssembly::finiteDescriptorRetainedOwners() noexcept
{
    return OriginalCommandDescriptorSetSource::finiteExclusiveRetainedOwnerCount;
}

const char* OriginalCommandSourceAssembly::appendDescriptorSet(core::UniquePtr<DescriptorSet>& set,
    const std::shared_ptr<const DescriptorArena>& arena,
    const std::shared_ptr<OriginalCommandResourceSources>& inventory,
    const OriginalCommandDescriptorSetElement* elements,
    std::size_t count,
    const OriginalCommandDescriptorSourceBudget& budget,
    const GpuDevice& device,
    std::size_t& index)
{
    index = SIZE_MAX;
    if (const auto* id = validate(inventory, device))
        return id;
    const char* id = nullptr;
    const auto source = OriginalCommandDescriptorSetSource::prepareOriginal(
        set, arena, inventory, elements, count, budget, quota(*inventory), device, id);
    if (!source)
        return id ? id : "RHI-ORIGINAL-SOURCES-DESCRIPTOR-SOURCE";
    const auto candidate = inventory->sources().size();
    if (const auto* error =
            OriginalCommandResourceSources::appendOriginalTyped(inventory, source, device))
        return error;
    index = candidate;
    return nullptr;
}
} // namespace arp::rhi
