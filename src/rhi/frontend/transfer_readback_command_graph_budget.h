#pragma once
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/memory_budget_ledger.h"

#include <array>

namespace arp::rhi {
// OriginalReadbackCompleteGraphBudget/1. Authored ceilings, never defaults or
// execution authority. The one-readback template has explicit positional intent.
struct TextureReadbackCommandGraphBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCompletedSourceGraphs = 0u, maxProjectedOwners = 0u, maxAllocationRecords = 0u;
    std::uint32_t maxReferences = 0u, maxTextureReferences = 0u, maxReadbackReferences = 0u,
                  maxStateReferences = 0u;
    std::uint32_t maxReferenceUsages = 0u, maxDescriptorRecords = 0u, maxReadbackDescriptors = 0u,
                  maxSourceReferences = 0u;
    std::uint64_t maxCommandTextBytes = 0u, maxPassLabelBytes = 0u, maxCpuMetadataBytes = 0u;
};

class TextureReadbackCommandGraphContract final {
public:
    static constexpr std::uint32_t commandCount = 5u, referenceCount = 3u, descriptorCount = 2u;
    static constexpr std::array<CommandUsageKind, commandCount> commands{
        CommandUsageKind::TextureBarrier,
        CommandUsageKind::BeginTransferPass,
        CommandUsageKind::TextureReadback,
        CommandUsageKind::EndTransferPass,
        CommandUsageKind::Finish};

    static const char* validateBudget(const TextureReadbackCommandGraphBudget& b,
        std::uint64_t requestLabelBytes,
        std::uint64_t requestQuotaBytes) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-GRAPH-BUDGET-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-GRAPH-BUDGET-OPT-IN";
        if (!b.maxCompletedSourceGraphs || !b.maxProjectedOwners || !b.maxAllocationRecords
            || !b.maxReferences || !b.maxTextureReferences || !b.maxReadbackReferences
            || !b.maxStateReferences || !b.maxReferenceUsages || !b.maxDescriptorRecords
            || !b.maxReadbackDescriptors || !b.maxSourceReferences)
            return "RHI-READBACK-GRAPH-BUDGET-COUNT";
        if (b.maxReferences < referenceCount || b.maxTextureReferences < 2u
            || b.maxReadbackReferences < 1u || b.maxStateReferences < 1u
            || b.maxReferenceUsages < referenceCount || b.maxDescriptorRecords < descriptorCount
            || b.maxReadbackDescriptors < 1u || b.maxTextureReferences > b.maxReferences
            || b.maxReadbackReferences > b.maxReferences
            || b.maxStateReferences > b.maxTextureReferences
            || b.maxReferenceUsages < b.maxReferences
            || b.maxReadbackDescriptors > b.maxDescriptorRecords)
            return "RHI-READBACK-GRAPH-BUDGET-CAPACITY";
        if (!b.maxCpuMetadataBytes || b.maxCpuMetadataBytes != requestQuotaBytes)
            return "RHI-READBACK-GRAPH-BUDGET-QUOTA";
        if (!core::MemoryBudgetLedger::canAdd(b.maxPassLabelBytes, requestLabelBytes)
            || b.maxPassLabelBytes + requestLabelBytes > b.maxCommandTextBytes
            || b.maxCommandTextBytes > SIZE_MAX)
            return "RHI-READBACK-GRAPH-BUDGET-LABEL-RANGE";
        return nullptr;
    }
};
} // namespace arp::rhi
