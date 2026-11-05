#pragma once

#include "core/runtime/cancellation_token.h"
#include "resource/resource_manager.h"

namespace arp::resource {

struct ResourceCandidatePreparationContext {
    const ResourceSourceSet& sources;
    std::size_t maxDecodedBytes;
    core::runtime::CancellationToken cancellation;
};

struct ResourceCandidatePreparationResult {
    bool success = false;
    const char* diagnosticId = nullptr;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

// Explicit opt-in for CPU candidate preparation on WorkService. Implementations
// consume only acquired immutable sources and mutate their private candidate.
// No shared registry mutation, host byte IO, scene/GPU work or owner-bound
// reflection registration is allowed. A private CPU subgraph may be built in
// the candidate using schemas frozen before submission; it must not escape
// before owner publication.
// ResourceBase::load later validates/adopts the prepared candidate on its owner.
// Existing ResourceBase types retain their owner-thread processing contract.
class ARP_API ResourceCandidatePreparation {
public:
    virtual ~ResourceCandidatePreparation() = default;
    virtual ResourceCandidatePreparationResult prepareResourceCandidate(
        const ResourceCandidatePreparationContext&) = 0;
};

} // namespace arp::resource
