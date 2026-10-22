#pragma once
#include "core/ownership/shared_owner_projection.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/bindless_closure_extensions.h"

#include <array>

namespace arp::rhi {
// SnapshotScope/1 describes membership in an original command, not a new
// snapshot generation or publication authority. Each snapshot keeps its own
// issued generation, authorization, budget and fault buffers. The caller must
// authenticate the original Roots before projecting them here.
class OriginalBindlessSnapshotScopeContract final {
public:
    struct Intent {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxSnapshots = 0u, maxVisits = 0u;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !optIn && !maxSnapshots && !maxVisits;
        }
    };

    struct Report {
        const char* diagnostic = nullptr;
        std::uint64_t roots = 0u, snapshots = 0u, visits = 0u;
    };

    struct Decision {
        bool preparationAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-SNAPSHOT-SCOPE-BACKEND";
    };

    static constexpr const char* validateIntent(const Intent& intent) noexcept
    {
        if (intent.empty())
            return nullptr;
        if (intent.contractVersion != 1u)
            return "RHI-BINDLESS-SNAPSHOT-SCOPE-VERSION";
        if (!intent.optIn)
            return "RHI-BINDLESS-SNAPSHOT-SCOPE-OPT-IN";
        return intent.maxSnapshots && intent.maxVisits ? nullptr
                                                       : "RHI-BINDLESS-SNAPSHOT-SCOPE-BUDGET";
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        return semantics::BindlessClosureExtensionLowering::snapshot_scope<Decision>(
            backend, version);
    }

    // A projection may include non-Root entries, represented by nullptr. This
    // allows the original inventory to be inspected without another owner list.
    // All probes, including null entries and duplicate checks, share one bound.
    template <class RootAt>
    static Report inspect(std::size_t count, RootAt&& rootAt, const Intent& intent) noexcept
    {
        Report report;
        const auto reject = [&](const char* id) noexcept {
            report.diagnostic = id;
            return report;
        };
        if (const auto* id = validateIntent(intent))
            return reject(id);
        if (intent.empty())
            return reject("RHI-BINDLESS-SNAPSHOT-SCOPE-INTENT-REQUIRED");
        const auto visit = [&]() noexcept {
            if (report.visits == intent.maxVisits)
                return false;
            ++report.visits;
            return true;
        };
        for (std::size_t i = 0u; i < count; ++i) {
            if (!visit())
                return reject("RHI-BINDLESS-SNAPSHOT-SCOPE-VISIT-CAPACITY");
            const auto* root = rootAt(i);
            if (!root)
                continue;
            ++report.roots;
            const auto& snapshot = root->originalSnapshot();
            if (!snapshot || !snapshot->generation())
                return reject("RHI-BINDLESS-SNAPSHOT-SCOPE-IDENTITY");
            bool duplicate = false;
            for (std::size_t prior = 0u; prior < i; ++prior) {
                if (!visit())
                    return reject("RHI-BINDLESS-SNAPSHOT-SCOPE-VISIT-CAPACITY");
                const auto* other = rootAt(prior);
                if (!other)
                    continue;
                const auto& earlier = other->originalSnapshot();
                const bool same = core::SharedOwnerProjection::sameOwner(snapshot, earlier);
                if (same != (snapshot.get() == earlier.get())
                    || same != (snapshot->generation() == earlier->generation()))
                    return reject("RHI-BINDLESS-SNAPSHOT-SCOPE-IDENTITY");
                if (same) {
                    duplicate = true;
                    continue;
                }
                // Ordinary scene resources may be shared. Snapshot metadata
                // and fault domains must remain independent physical owners.
                const std::array current{snapshot->metadataBuffer(),
                    snapshot->faultLatchBuffer(),
                    snapshot->faultReadbackBuffer()};
                const std::array previous{earlier->metadataBuffer(),
                    earlier->faultLatchBuffer(),
                    earlier->faultReadbackBuffer()};
                for (const auto* buffer : current) {
                    if (!buffer || !buffer->id().isValid())
                        return reject("RHI-BINDLESS-SNAPSHOT-SCOPE-BUFFER");
                    for (const auto* otherBuffer : previous)
                        if (!otherBuffer || buffer == otherBuffer
                            || buffer->id() == otherBuffer->id())
                            return reject("RHI-BINDLESS-SNAPSHOT-SCOPE-BUFFER-ALIAS");
                }
            }
            if (!duplicate) {
                if (report.snapshots == intent.maxSnapshots)
                    return reject("RHI-BINDLESS-SNAPSHOT-SCOPE-CAPACITY");
                ++report.snapshots;
            }
        }
        return report.roots ? report : reject("RHI-BINDLESS-SNAPSHOT-SCOPE-EMPTY");
    }
};
} // namespace arp::rhi
