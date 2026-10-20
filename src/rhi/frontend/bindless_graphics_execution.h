#pragma once
#include "rhi/frontend/bindless_graphics_pass_preparation.h"
#include "rhi/frontend/bindless_root_initialized_shader.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"
#include "rhi/frontend/bindless_scene_content.h"
#include "rhi/frontend/command_buffer_readback.h"
#include "rhi/frontend/command_geometry_content.h"
#include "rhi/semantics/bindless_graphics_execution_lowering.h"

namespace arp::rhi {
struct OriginalBindlessGraphicsExecutionIntent {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
};

struct OriginalBindlessGraphicsExecutionInputs {
    std::shared_ptr<const OriginalCommandBindlessRootSource> root;
    std::shared_ptr<const OriginalCommandResourceSources> inventory;
    std::shared_ptr<OriginalCommandStateTransaction> transaction;
    std::shared_ptr<const OriginalCommandBufferReadbackBinding> readback;
    const std::shared_ptr<const OriginalInitializedGeometrySource>* geometry = nullptr;
    std::size_t geometryCount = 0u;
    std::shared_ptr<core::CpuMemoryQuota> closureQuota, passQuota, submissionQuota, viewQuota;
    std::shared_ptr<core::AtomicCapacityBudget> passCapacity;
    const std::shared_ptr<const OriginalInitializedGeometrySource>* initializedShader = nullptr;
    std::size_t initializedShaderCount = 0u;
    OriginalBindlessRootReadbackSequence::Input readbacks;
    OriginalBindlessSceneContentInputs scene;
};

struct OriginalBindlessGraphicsExecutionBudget {
    OriginalBindlessGraphicsExecutionIntent intent;
    OriginalBindlessRootNativeClosureBudget closure;
    OriginalBindlessGraphicsPassBudget pass;
    OriginalCommandNativeLifecycleBudget lifecycle;
    OriginalCommandLifecycleCapacityRequest capacity;
    OriginalIndexedDrawFetchContract::Budget fetch;
    std::uint64_t maxCpuViewBytes = 0u;
    std::uint64_t maxDiagnosticBytes = 0u;
    bool requireWorkloadRetention = false;
};

class OriginalBindlessGraphicsExecutionContract final {
public:
    static constexpr const char* validateIntent(
        OriginalBindlessGraphicsExecutionIntent intent) noexcept
    {
        if (intent.contractVersion != 1u && intent.contractVersion != 2u)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-VERSION";
        return intent.prototypeOptIn ? nullptr : "RHI-BINDLESS-GRAPHICS-EXECUTION-OPT-IN";
    }

    static const char* validateBudget(const OriginalBindlessGraphicsExecutionBudget& b) noexcept
    {
        if (const auto* id = validateIntent(b.intent))
            return id;
        if (const auto* id = OriginalBindlessRootNativeClosureContract::validateBudget(b.closure))
            return id;
        if (const auto* id = OriginalBindlessGraphicsPassContract::validateBudget(b.pass))
            return id;
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateIntent(b.lifecycle))
            return id;
        const bool depth = b.intent.contractVersion == 2u;
        if (b.lifecycle.contractVersion != (depth ? 19u : 9u))
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-LIFECYCLE";
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateCapacityRequest(
                b.capacity, b.lifecycle))
            return id;
        if (b.capacity.contractVersion != (depth ? 10u : 5u)
            || (depth ? b.closure.contractVersion != 16u
                      : (b.closure.contractVersion != 1u && b.closure.contractVersion != 3u)))
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-PROFILE";
        if (b.closure.contractVersion == 3u) {
            if (const auto* id = b.fetch.diagnostic())
                return id;
            if (b.fetch.contractVersion != 2u)
                return "RHI-BINDLESS-GRAPHICS-EXECUTION-FETCH";
        } else if (b.fetch.contractVersion || b.fetch.prototypeOptIn || b.fetch.maxCommands
            || b.fetch.maxDraws || b.fetch.maxStreams || b.fetch.maxBindingScans
            || b.fetch.maxAnalyzedIndices)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-FETCH";
        if (!b.maxCpuViewBytes || !b.maxDiagnosticBytes
            || b.maxDiagnosticBytes >= b.maxCpuViewBytes)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-VIEW-BUDGET";
        return nullptr;
    }
};

// Only a view of the existing command/custody. No second state machine, journal,
// completion receipt, result store or submission pool. Poll never waits.
class ARP_API OriginalBindlessGraphicsExecution : public core::QuotaBoundSharedObjectIdentity {
public:
    virtual ~OriginalBindlessGraphicsExecution() = default;
    using Intent = OriginalBindlessGraphicsExecutionIntent;

    static const char* record(const std::shared_ptr<const OriginalBindlessGraphicsExecution>& self,
        Intent intent) noexcept
    {
        if (const auto* id = validateOwner(self, intent))
            return id;
        return self->recordOriginal();
    }

    static const char* submit(const std::shared_ptr<const OriginalBindlessGraphicsExecution>& self,
        Intent intent) noexcept
    {
        if (const auto* id = validateOwner(self, intent))
            return id;
        return self->submitOriginal();
    }

    virtual const char* cancel() const noexcept = 0;
    virtual bool pollCompletion() const noexcept = 0;
    virtual bool recordingProven() const noexcept = 0;
    virtual bool submissionCompleted() const noexcept = 0;
    // A physically completed fence does not by itself prove journal commit or
    // Result publication. Inspect this after poll before consuming the Result.
    virtual const char* completionDiagnostic() const noexcept = 0;

protected:
    virtual std::uint32_t executionContractVersion() const noexcept { return 1u; }

    virtual const char* recordOriginal() const noexcept = 0;
    virtual const char* submitOriginal() const noexcept = 0;

private:
    static const char* validateOwner(
        const std::shared_ptr<const OriginalBindlessGraphicsExecution>& self,
        Intent intent) noexcept
    {
        if (const auto* id = OriginalBindlessGraphicsExecutionContract::validateIntent(intent))
            return id;
        if (self && self->hasQuotaBoundSharedOwner(self)
            && intent.contractVersion != self->executionContractVersion())
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-INTENT-PROFILE";
        return self && self->hasQuotaBoundSharedOwner(self)
            ? nullptr
            : "RHI-BINDLESS-GRAPHICS-EXECUTION-OWNER";
    }
};
} // namespace arp::rhi
