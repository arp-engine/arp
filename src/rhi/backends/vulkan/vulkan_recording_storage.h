#pragma once

#include "rhi/frontend/command_profiling_budget.h"
#include "rhi/frontend/command_recording_budget.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/frontend/query_recording_storage.h"

#include <limits>
#include <string_view>

namespace arp::rhi_vulkan {

// Backend-private lowering of original opcode/reference ceilings. No native
// authority, resource graph, state journal or additional command vocabulary.
struct VulkanRhiRecordingStorageLimits {
    std::size_t bufferBarriers = 0u, imageBarriers = 0u, memoryBarriers = 0u;
    std::size_t resourceReferences = 0u;

    struct OriginalQueries {
        std::shared_ptr<const rhi::OriginalCommandInputs> inputs;
        rhi::CommandRetentionBudget budget;
        std::shared_ptr<core::CpuMemoryQuota> recordingQuota;
        bool internalProfilingRequested = false;
        std::uint32_t recordingContractVersion = 0u;
        bool recordingOptIn = false;
        rhi::CommandRecordingBudget commandBudget;
        std::optional<rhi::CommandProfilingBudget> profilingBudget;
    };

    std::optional<OriginalQueries> originalQueries;
    // Retain the exact original closure in the existing native owner. Storage
    // preparation alone still grants no original-stream execution authority.
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> originalProgram;

    static constexpr const char* unsupportedDebugLabelDiagnostic() noexcept
    {
        return "RHI-VULKAN-COLD-DEBUG-LABEL-STORAGE-UNAVAILABLE";
    }

    static constexpr const char* unsupportedStagingPoolDiagnostic() noexcept
    {
        return "RHI-VULKAN-COLD-STAGING-POOL-STORAGE-UNAVAILABLE";
    }

    static constexpr std::size_t diagnosticCapacity() noexcept
    {
        std::size_t result = 0u;
        for (const std::string_view id : {"RHI-VULKAN-COLD-RESOURCE-REFERENCE-CAPACITY",
                 "RHI-VULKAN-COLD-BUFFER-BARRIER-CAPACITY",
                 "RHI-VULKAN-COLD-IMAGE-BARRIER-CAPACITY",
                 "RHI-VULKAN-COLD-MEMORY-BARRIER-CAPACITY",
                 "RHI-COMMAND-PROFILING-END-DEVICE-LOST",
                 "RHI-VULKAN-COMMAND-END-ENTRYPOINT",
                 "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS",
                 "RHI-VULKAN-COLD-BARRIER-STORAGE-KIND",
                 "RHI-VULKAN-COLD-RESOURCE-PAYLOAD-MISSING",
                 unsupportedDebugLabelDiagnostic(),
                 "RHI-ORDINARY-TEXTURE-UPLOAD-TRANSITION-DESTINATION",
                 "RHI-ORDINARY-TEXTURE-UPLOAD-INITIAL-TRANSITION",
                 "RHI-STAGING-JOURNAL-ENTRY-CAPACITY",
                 unsupportedStagingPoolDiagnostic(),
                 "RHI-VULKAN-COLD-QUERY-STORAGE-UNAVAILABLE"})
            if (id.size() > result)
                result = id.size();
        return result;
    }

    const char* diagnostic() const noexcept
    {
        const bool mixedTiming =
            originalProgram && originalQueries && originalQueries->recordingContractVersion == 2u;
        if (originalProgram) {
            if (const auto* id =
                    rhi::OriginalCommandStreamProgram::validateOriginal(originalProgram))
                return id;
            if (originalQueries && !mixedTiming)
                return "RHI-VULKAN-COLD-PROGRAM-QUERY-JOIN-UNAVAILABLE";
            if (mixedTiming
                && (!originalQueries->recordingOptIn
                    || originalProgram->originalInputs() != originalQueries->inputs
                    || originalProgram->originalBudget().contractVersion != 5u
                    || originalQueries->inputs->originalBudget().contractVersion != 11u
                    || !originalQueries->inputs->originalBudget().passTiming
                    || originalQueries->internalProfilingRequested
                    || originalQueries->profilingBudget))
                return "RHI-MIXED-PASS-TIMING-PROGRAM-JOIN";
            if (!originalProgram->recordingBudget())
                return "RHI-VULKAN-COLD-PASS-RECORDING-BUDGET-REQUIRED";
            const char* id = nullptr;
            const auto expected = project(*originalProgram->recordingBudget(), id);
            if (!expected)
                return id;
            if (bufferBarriers != expected->bufferBarriers
                || imageBarriers != expected->imageBarriers
                || memoryBarriers != expected->memoryBarriers
                || resourceReferences != expected->resourceReferences)
                return "RHI-VULKAN-COLD-PROGRAM-STORAGE-JOIN";
        }
        if (originalQueries) {
            const auto& query = *originalQueries;
            if (!mixedTiming && (query.recordingContractVersion || query.recordingOptIn)) {
                const auto recording = rhi::semantics::QueryLowering::originalRecording(
                    rhi::Backend::Vulkan, query.recordingContractVersion, query.recordingOptIn);
                if (recording.diagnostic)
                    return recording.diagnostic;
                if (const auto* id =
                        rhi::CommandRecordingBudgetContract::validateIntent(query.commandBudget))
                    return id;
            }
            if (query.internalProfilingRequested && !query.profilingBudget)
                return "RHI-VULKAN-COLD-PROFILING-BUDGET-UNAVAILABLE";
            if (!query.internalProfilingRequested && query.profilingBudget)
                return "RHI-COMMAND-PROFILING-OPT-IN";
            if (query.profilingBudget) {
                if (const auto* id =
                        rhi::CommandProfilingContract::validateIntent(*query.profilingBudget))
                    return id;
                // The native publisher includes the whole interval and every
                // named pair from a referenced pool, never a truncated prefix.
                std::array<std::uint64_t, rhi::GpuTimingSample::kMaxScopes> ids{};
                std::size_t count = 1u;
                if (const auto* id = rhi::OriginalQueryRecordingStorageContract::inspect(
                        query.inputs, query.budget, query.recordingQuota)
                        .diagnostic)
                    return id;
                const auto& inputs = *query.inputs;
                const auto& sources =
                    inputs.originalScalars()->originalUsage()->originalSources()->sources();
                for (std::size_t i = 0u; i < inputs.descriptors().size(); ++i) {
                    const auto* binding = std::get_if<rhi::OriginalCommandQueryBinding>(
                        &inputs.descriptors()[i].arguments);
                    if (!binding)
                        continue;
                    bool first = true;
                    for (std::size_t j = 0u; j < i; ++j) {
                        const auto* previous = std::get_if<rhi::OriginalCommandQueryBinding>(
                            &inputs.descriptors()[j].arguments);
                        first =
                            first && (!previous || previous->sourceIndex != binding->sourceIndex);
                    }
                    if (!first)
                        continue;
                    const auto& pool =
                        std::get<std::shared_ptr<const rhi::OriginalCommandQueryPoolSource>>(
                            sources[binding->sourceIndex]);
                    for (const auto scopeId : pool->originalInput()->scopeIds()) {
                        if (count >= query.profilingBudget->maxPublishedScopes)
                            return "RHI-COMMAND-PROFILING-PUBLICATION-CAPACITY";
                        for (std::size_t j = 0u; j < count; ++j)
                            if (ids[j] == scopeId)
                                return "RHI-COMMAND-PROFILING-SCOPE-IDENTITY";
                        ids[count++] = scopeId;
                    }
                }
            }
            const auto inspection = mixedTiming
                ? rhi::OriginalQueryRecordingStorageContract::inspectPassTiming(
                      query.inputs, query.budget, query.recordingQuota)
                : rhi::OriginalQueryRecordingStorageContract::inspect(
                      query.inputs, query.budget, query.recordingQuota);
            if (const auto* id = inspection.diagnostic)
                return id;
        }
        constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
        return bufferBarriers > maximum || imageBarriers > maximum || memoryBarriers > maximum
            ? "RHI-VULKAN-COLD-RECORDING-STORAGE-RANGE"
            : nullptr;
    }

    static std::optional<VulkanRhiRecordingStorageLimits> project(
        const rhi::CommandRecordingBudget& budget, const char*& diagnostic) noexcept
    {
        diagnostic = rhi::CommandRecordingBudgetContract::validateIntent(budget);
        if (diagnostic)
            return {};
        const auto count = [&](rhi::CommandUsageKind kind) noexcept {
            return budget.limits.commands[static_cast<std::size_t>(kind)];
        };
        using K = rhi::CommandUsageKind;
        const auto buffer = count(K::BufferBarrier), bufferMemory = count(K::BufferMemoryBarrier);
        const auto memory = count(K::TextureMemoryBarrier), alias = count(K::AliasBarrier);
        if (!core::canAddUnsigned(buffer, bufferMemory) || !core::canAddUnsigned(memory, alias)) {
            diagnostic = "RHI-VULKAN-COLD-BARRIER-STORAGE-OVERFLOW";
            return {};
        }
        const auto image = count(K::TextureBarrier);
        constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
        if (buffer + bufferMemory > maximum || image > maximum || memory + alias > maximum
            || budget.limits.resourceReferences > SIZE_MAX) {
            diagnostic = "RHI-VULKAN-COLD-RECORDING-STORAGE-RANGE";
            return {};
        }
        return VulkanRhiRecordingStorageLimits{static_cast<std::size_t>(buffer + bufferMemory),
            static_cast<std::size_t>(image),
            static_cast<std::size_t>(memory + alias),
            static_cast<std::size_t>(budget.limits.resourceReferences)};
    }
};

} // namespace arp::rhi_vulkan
