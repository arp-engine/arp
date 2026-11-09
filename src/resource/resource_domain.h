#pragma once

#include "core/concurrency/callback.h"
#include "core/container/array_view.h"
#include "core/ownership/lifetime_epoch.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"

namespace arp::resource {

struct ResourceIdentity {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    core::pmr::String type{&core::defaultCpuMemoryResource()};
    core::pmr::String version{&core::defaultCpuMemoryResource()};
    ResourceIdentity() = default;

    ResourceIdentity(NameView sourceId, NameView sourceType, NameView contentVersion)
        : id(sourceId, &core::defaultCpuMemoryResource()),
          type(sourceType, &core::defaultCpuMemoryResource()),
          version(contentVersion, &core::defaultCpuMemoryResource())
    {}

    ResourceIdentity(const ResourceIdentity& other)
        : ResourceIdentity(other.id, other.type, other.version)
    {}

    ResourceIdentity& operator=(const ResourceIdentity& other)
    {
        id = other.id;
        type = other.type;
        version = other.version;
        return *this;
    }

    ResourceIdentity(ResourceIdentity&&) noexcept = default;
    ResourceIdentity& operator=(ResourceIdentity&&) noexcept = default;
};

enum class ResourceAcquireStatus {
    Accepted,
    Ready,
    InvalidRequest,
    IdentityConflict,
    MissingDefinition,
    DependencyCycle,
    BudgetExceeded,
    Failed,
    Cancelled,
    Closed,
    Busy,
};

struct ResourceDomainResult {
    ResourceAcquireStatus status = ResourceAcquireStatus::InvalidRequest;
    const char* diagnosticId = nullptr;

    bool success() const noexcept
    {
        return status == ResourceAcquireStatus::Accepted || status == ResourceAcquireStatus::Ready;
    }
};

// Resource descriptors are constructed without IO or registry mutation.
// Their ResourceBase::dependencies() is the single strong-dependency source.
using ResourceDescriptorFactory = core::SharedCallback<core::UniquePtr<ResourceBase>()>;

struct ResourceRegistration {
    ResourceIdentity identity;
    ResourceDescriptorFactory factory;
};

struct ResourceDomainBudget {
    std::size_t maxResources = 0;
    std::size_t maxRequests = 0;
    std::size_t maxLeases = 0;
    std::size_t maxInFlightLoads = 0;
    ResourceGraphExecutionBudget load;
};

namespace detail {
struct ResourceLeaseState;
}

// Copies retain the same root pin. Destruction can occur on any thread;
// owner-thread collect() performs the actual dependency release and disposal.
// Observation is allocation-free, owner-thread only and guarded against domain
// destruction; the manager's stable ResourceHandle checks its generation.
class ARP_API ResourceLease final {
public:
    ResourceLease() = default;
    bool ready() const noexcept;
    const ResourceIdentity* identity() const noexcept;
    const ResourceBase* resource() const noexcept;

    template <class T> const T* get() const noexcept { return dynamic_cast<const T*>(resource()); }

    void reset() noexcept { state_.reset(); }

private:
    friend class ResourceDomain;

    explicit ResourceLease(core::SharedPtr<detail::ResourceLeaseState> state)
        : state_(std::move(state))
    {}

    core::SharedPtr<detail::ResourceLeaseState> state_;
};

struct ResourceAcquireResult {
    ResourceDomainResult result;
    ResourceLease lease;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

enum class ResourcePrewarmReadiness { CpuReady, GpuReady, PipelineReady };

using ResourceAcquireCompletion = core::Callback<void(ResourceAcquireResult)>;

struct ResourceDomainTelemetry {
    std::uint64_t loads = 0;
    std::uint64_t readyHits = 0;
    std::uint64_t joinedLoads = 0;
    std::uint64_t releasedResources = 0;
    std::size_t pendingRequests = 0;
    std::size_t inFlightLoads = 0;
    std::size_t liveLeases = 0;
};

// Native resource acquisition domain over the existing owner-thread manager.
// Manager/runtime must outlive the domain and drain accepted asynchronous work.
// Explicit manager load/admission remains distinct from repeatable acquire.
class ARP_API ResourceDomain final {
public:
    ResourceDomain(ResourceManager&, core::runtime::AsyncRuntime&, ResourceDomainBudget);
    // Owned CPU context: leases retain immutable resources after the facade is
    // gone. Destruction stops/drains producers; explicit close invalidates views.
    ResourceDomain(core::runtime::AsyncRuntimeDesc, ResourceDomainBudget);
    ~ResourceDomain();
    ResourceDomain(const ResourceDomain&) = delete;
    ResourceDomain& operator=(const ResourceDomain&) = delete;

    ResourceDomainResult registerResource(ResourceIdentity, ResourceDescriptorFactory);
    // Transactional, complete-closure registration. Factories are metadata-only
    // and repeatable; matching existing identities must agree on dependencies.
    ResourceDomainResult registerResources(core::ArrayView<const ResourceRegistration>);
    ResourceDomainResult acquireAsync(const ResourceIdentity&,
        ResourceAcquireCompletion,
        core::runtime::CancellationToken = {},
        bool retryFailed = false);
    // CPU prewarm retains exactly the same resource/closure as acquisition.
    ResourceDomainResult prewarmAsync(const ResourceIdentity&,
        ResourceAcquireCompletion,
        core::runtime::CancellationToken = {},
        ResourcePrewarmReadiness = ResourcePrewarmReadiness::CpuReady);
    // Cold preparation only; waits while dispatching the supplied async runtime.
    ResourceAcquireResult acquire(
        const ResourceIdentity&, const core::SharedCallback<void()>& yield = {});
    // Dispatch the shared runtime, publish acquisitions, release unused roots.
    ResourceDomainResult dispatch();
    // Per-call runtime completion budget; acquisition publication is unchanged.
    // Does not change worker count, resource identity or the shared runtime's
    // configured default; zero is invalid.
    ResourceDomainResult dispatch(std::size_t maxCallbacks);
    // Cold loading pump: drains completions and waits on producer notifications
    // until the time slice ends or all acquisitions complete. Callback execution
    // itself is not preempted. Rendering continues on its independent clock.
    ResourceDomainResult dispatchFor(std::chrono::steady_clock::duration, std::size_t maxCallbacks);
    ResourceDomainResult collect();
    // Derived CPU preparation shares this domain's bounded worker and completion
    // queues. The caller owns task data until completion (including cancellation).
    // Does not create an asset or bypass graph acquisition/publication semantics.
    core::runtime::WorkSubmitResult submitPreparation(core::runtime::WorkItem);
    ResourceDomainResult close();
    ResourceDomainTelemetry telemetry() const;
    ResourceState stateOf(NameView) const;
    const ResourceManager& resources() const;

private:
    ResourceDomainResult dispatchImpl(std::size_t maxCallbacks);
    ResourceDomainResult registerResourcesImpl(
        core::ArrayView<const ResourceRegistration>, bool requireCompleteClosure);
    struct Impl;
    core::SharedPtr<Impl> impl_;
};

} // namespace arp::resource
