#pragma once
#include "rhi/frontend/bindless_graphics_pass_sequence.h"
#include "rhi/frontend/bindless_produced_geometry.h"
#include "rhi/frontend/bindless_root_execution.h"
#include "rhi/frontend/bindless_root_initialized_shader.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"

namespace arp::rhi {
// Callback-free Native execution uses the original Root, graph, journal and
// result. These budgets cannot be adapted from WebGPU callback capacities.
struct OriginalBindlessRootFenceExecutionInputs {
    std::shared_ptr<const OriginalCommandBindlessRootSource> root;
    std::shared_ptr<const OriginalCommandResourceSources> inventory;
    std::shared_ptr<OriginalCommandStateTransaction> transaction;
    std::shared_ptr<const OriginalCommandBufferReadbackBinding> readback;
    std::shared_ptr<core::CpuMemoryQuota> closureQuota, submissionQuota, viewQuota;
    // Execution9 composes two authenticated entries over one snapshot/journal.
    std::shared_ptr<const OriginalCommandBindlessRootSource> nextRoot;
    std::shared_ptr<core::AtomicCapacityBudget> attachmentCapacity;
    std::shared_ptr<const OriginalCommandBindlessRootSource> finalRoot;
    const std::shared_ptr<const OriginalInitializedGeometrySource>* initializedShader = nullptr;
    std::size_t initializedShaderCount = 0u;
    OriginalBindlessRootReadbackSequence::Input readbacks;
    OriginalBindlessRootSequenceContract::Input roots;
    OriginalBindlessProducedGeometryInputs producedGeometry;
    OriginalBindlessProducedPassInputs producedPasses;
};

struct OriginalBindlessRootFenceExecutionBudget {
    BindlessRequirements::OriginalExecution intent;
    OriginalBindlessRootNativeClosureBudget closure;
    OriginalCommandNativeLifecycleBudget lifecycle;
    OriginalCommandLifecycleCapacityRequest capacity;
    std::uint64_t maxCpuViewBytes = 0u, maxDiagnosticBytes = 0u;
    OriginalBindlessMixedEntryFlowContract::Budget mixedEntries;
    bool requireWorkloadRetention = false;

    const char* validate() const noexcept
    {
        if (intent.contractVersion < 6u
            || (intent.contractVersion > 9u && intent.contractVersion != 11u
                && intent.contractVersion != 15u))
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-VERSION";
        if (const auto* id = OriginalBindlessRootNativeExecutionContract::validateIntent(intent))
            return id;
        const bool mixed = intent.contractVersion == 11u;
        if (mixed) {
            if (const auto* id =
                    OriginalBindlessMixedEntryFlowContract::validateIntent(mixedEntries))
                return id;
        } else if (mixedEntries.contractVersion || mixedEntries.prototypeOptIn
            || mixedEntries.maxDeclarationVisits)
            return "RHI-BINDLESS-ROOT-EXECUTION-MIXED-SCHEMA";
        if (const auto* id = OriginalBindlessRootNativeClosureContract::validateBudget(closure))
            return id;
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateIntent(lifecycle))
            return id;
        const bool storage = intent.contractVersion == 9u;
        const bool buffer = intent.contractVersion == 7u;
        const bool color = intent.contractVersion == 8u;
        const bool produced = intent.contractVersion == 15u;
        if (closure.contractVersion
                != (produced              ? 21u
                        : mixed           ? 14u
                        : storage         ? 11u
                        : buffer || color ? 1u
                                          : 9u)
            || lifecycle.contractVersion
                != (produced      ? 21u
                        : mixed   ? 17u
                        : storage ? 15u
                        : color   ? 8u
                        : buffer  ? 6u
                                  : 14u))
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-PROFILE";
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateCapacityRequest(
                capacity, lifecycle))
            return id;
        if (capacity.contractVersion
            != (produced      ? 11u
                    : mixed   ? 9u
                    : storage ? 8u
                    : color   ? 4u
                    : buffer  ? 2u
                              : 7u))
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-CAPACITY";
        if (produced && !requireWorkloadRetention)
            return "RHI-BINDLESS-PRODUCED-EXECUTION-RETENTION-REQUIRED";
        if (!maxCpuViewBytes || !maxDiagnosticBytes || maxDiagnosticBytes >= maxCpuViewBytes)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-VIEW-BUDGET";
        return nullptr;
    }
};
} // namespace arp::rhi
