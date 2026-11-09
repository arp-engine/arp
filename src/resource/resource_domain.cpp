#include "resource/resource_domain.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <thread>

namespace arp::resource {
namespace detail {
struct ResourceLeaseState {
    ~ResourceLeaseState()
    {
        if (released)
            released->store(true, std::memory_order_release);
    }

    ResourceIdentity identity;
    ResourceHandle<ResourceBase> handle;
    core::LifetimeEpoch::Ticket lifetime;
    std::thread::id owner;
    core::SharedPtr<void> keepAlive;
    core::SharedPtr<std::atomic<bool>> released;
};
} // namespace detail

const ResourceBase* ResourceLease::resource() const noexcept
{
    if (!state_ || state_->owner != std::this_thread::get_id() || !state_->lifetime.isCurrent())
        return nullptr;
    // Handles observe the manager's stable record address and generation.
    // Looking up the UUID here would allocate a key inside this noexcept path.
    return state_->handle.get();
}

bool ResourceLease::ready() const noexcept
{
    return resource() != nullptr;
}

const ResourceIdentity* ResourceLease::identity() const noexcept
{
    return state_ ? &state_->identity : nullptr;
}

namespace {
ResourceDomainResult result(ResourceAcquireStatus status, const char* diagnostic = nullptr)
{
    return {status, diagnostic};
}

bool same(const ResourceIdentity& a, const ResourceIdentity& b)
{
    return a.id == b.id && a.type == b.type && a.version == b.version;
}

bool sameDependencies(
    const core::pmr::Vector<ResourceDependency>& a, const core::pmr::Vector<ResourceDependency>& b)
{
    if (a.size() != b.size())
        return false;
    for (const auto& dependency : a)
        if (std::none_of(b.begin(), b.end(), [&](const auto& other) {
                return dependency.id == other.id && dependency.usage == other.usage;
            }))
            return false;
    return true;
}
} // namespace

struct ResourceDomain::Impl : ResourceLifecycleObserver {
    enum class State { Dormant, Loading, Ready, Failed };
    struct Job;
    struct Entry;

    struct Edge {
        Entry* parent;
        Entry* child;
        Edge* next = nullptr;
    };

    struct Entry {
        ResourceIdentity identity;
        ResourceDescriptorFactory factory;
        core::pmr::Vector<ResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
        State state = State::Dormant;
        std::size_t roots = 0, parents = 0, pending = 0;
        bool holdsDependencies = false;
        ResourceGeneration generation = 0;
        core::UniquePtr<Job> job;
        const char* failure = nullptr;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Edge> edges{&core::defaultCpuMemoryResource()};
        Edge* incoming = nullptr;
        std::size_t unresolved = 0;
        bool connected = false;
        Entry* readyNext = nullptr;
        Entry* dirtyNext = nullptr;
        bool readyQueued = false;
        bool dirtyQueued = false;
    };

    // Entries have stable addresses and queues are bounded by maxResources.
    // Completion and release propagation never allocate queue nodes.
    template <Entry* Entry::* Next, bool Entry::* Queued> struct Queue {
        Entry* head = nullptr;
        Entry* tail = nullptr;

        void push(Entry& entry) noexcept
        {
            if (entry.*Queued)
                return;
            entry.*Queued = true;
            entry.*Next = nullptr;
            if (tail)
                tail->*Next = &entry;
            else
                head = &entry;
            tail = &entry;
        }

        Entry* pop() noexcept
        {
            auto* entry = head;
            if (entry) {
                head = entry->*Next;
                if (!head)
                    tail = nullptr;
                entry->*Next = nullptr;
                entry->*Queued = false;
            }
            return entry;
        }
    };

    Queue<&Entry::readyNext, &Entry::readyQueued> ready;
    Queue<&Entry::dirtyNext, &Entry::dirtyQueued> dirty;

    struct Job {
        core::SharedPtr<Impl> owner;
        Entry* entry = nullptr;
        core::runtime::CancellationSource cancellation;
    };

    struct Request {
        Entry* root = nullptr;
        Entry* failure = nullptr;
        core::pmr::Vector<Entry*> closure{&core::defaultCpuMemoryResource()};
        ResourceAcquireCompletion complete;
        core::runtime::CancellationToken cancellation;
    };

    core::SharedPtr<ResourceManager> ownedManager;
    core::SharedPtr<core::runtime::AsyncRuntime> ownedRuntime;
    core::WeakPtr<Impl> self;
    ResourceManager& manager;
    core::runtime::AsyncRuntime& runtime;
    const ResourceDomainBudget budget;
    const std::thread::id owner = std::this_thread::get_id();
    core::LifetimeEpoch lifetime;
    core::pmr::Map<core::pmr::String, core::UniquePtr<Entry>, std::less<>> entries{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<Request>> requests{&core::defaultCpuMemoryResource()};

    struct Pin {
        Entry* root;
        core::WeakPtr<detail::ResourceLeaseState> state;
    };

    core::pmr::Vector<Pin> leases{&core::defaultCpuMemoryResource()};
    core::SharedPtr<std::atomic<bool>> leasesReleased = core::makeShared<std::atomic<bool>>(false);
    ResourceDomainTelemetry stats;
    Entry* unloading = nullptr;
    bool closed = false, pumping = false;

    Impl(ResourceManager& m,
        core::runtime::AsyncRuntime& r,
        ResourceDomainBudget b,
        core::SharedPtr<ResourceManager> managerOwner = {},
        core::SharedPtr<core::runtime::AsyncRuntime> runtimeOwner = {})
        : ownedManager(std::move(managerOwner)), ownedRuntime(std::move(runtimeOwner)), manager(m),
          runtime(r), budget(b)
    {
        if (!manager.isOwnerThread() || !runtime.valid() || !b.maxResources || !b.maxRequests
            || !b.maxLeases || !b.maxInFlightLoads || !b.load.maxNodes || !b.load.maxFileReads
            || !b.load.maxSourceBytes || !b.load.maxDecodedBytes || !b.load.completionDispatchBudget
            || b.maxResources == SIZE_MAX || b.maxRequests == SIZE_MAX || b.maxLeases == SIZE_MAX)
            throw std::invalid_argument("RESOURCE-DOMAIN-CONFIGURATION");
    }

    ~Impl()
    {
        try {
            collect();
        } catch (...) {
        }
        manager.removeLifecycleObserver(*this);
    }

    bool beforeResourceUnload(NameView id) noexcept override
    {
        const auto found = entries.find(id);
        if (found == entries.end())
            return true;
        const auto& entry = *found->second;
        return unloading == &entry && !entry.roots && !entry.parents && !entry.pending;
    }

    bool beforeRetiredResourcesRelease() noexcept override { return leases.empty(); }

    bool isOwner() const noexcept { return owner == std::this_thread::get_id(); }

    using VisitColors = core::pmr::Map<Entry*, unsigned>;

    ResourceDomainResult closure(Entry& root,
        core::pmr::Vector<Entry*>& output,
        const decltype(entries)* staged = nullptr,
        VisitColors* visited = nullptr)
    {
        struct Frame {
            Entry* entry;
            std::size_t next = 0;
        };

        VisitColors local{&core::defaultCpuMemoryResource()};
        auto& colors = visited ? *visited : local;
        if (colors[&root] == 2)
            return result(ResourceAcquireStatus::Accepted);
        core::pmr::Vector<Frame> stack{&core::defaultCpuMemoryResource()};
        stack.push_back({&root});
        colors[&root] = 1;
        while (!stack.empty()) {
            auto& frame = stack.back();
            if (frame.next == frame.entry->dependencies.size()) {
                colors[frame.entry] = 2;
                output.push_back(frame.entry);
                stack.pop_back();
                continue;
            }
            const auto& dependency = frame.entry->dependencies[frame.next++];
            Entry* child = nullptr;
            if (staged) {
                const auto found = staged->find(dependency.id);
                if (found != staged->end())
                    child = found->second.get();
            }
            if (!child) {
                const auto found = entries.find(dependency.id);
                if (found != entries.end())
                    child = found->second.get();
            }
            if (!child)
                return result(
                    ResourceAcquireStatus::MissingDefinition, "RESOURCE-DOMAIN-DEPENDENCY-MISSING");
            if (colors[child] == 1)
                return result(
                    ResourceAcquireStatus::DependencyCycle, "RESOURCE-DOMAIN-DEPENDENCY-CYCLE");
            if (colors[child] == 0) {
                colors[child] = 1;
                stack.push_back({child});
            }
        }
        return result(ResourceAcquireStatus::Accepted);
    }

    void releaseDependencies(Entry& entry) noexcept
    {
        if (!entry.holdsDependencies)
            return;
        for (const auto& edge : entry.edges) {
            --edge.child->parents;
            dirty.push(*edge.child);
        }
        entry.holdsDependencies = false;
    }

    void enqueueReady(Entry& entry) noexcept
    {
        if (entry.state == State::Dormant && entry.pending && !entry.unresolved)
            ready.push(entry);
    }

    void connect(Entry& entry)
    {
        if (entry.connected)
            return;
        entry.edges.reserve(entry.dependencies.size());
        for (const auto& dependency : entry.dependencies) {
            auto* child = entries.at(dependency.id).get();
            entry.edges.push_back({&entry, child});
        }
        // All allocations precede linking. Once connected, edges never move.
        for (auto& edge : entry.edges) {
            edge.next = edge.child->incoming;
            edge.child->incoming = &edge;
            entry.unresolved += edge.child->state != State::Ready;
        }
        entry.connected = true;
    }

    void setState(Entry& entry, State state) noexcept
    {
        const bool wasReady = entry.state == State::Ready;
        entry.state = state;
        if (wasReady != (state == State::Ready))
            for (auto* edge = entry.incoming; edge; edge = edge->next) {
                if (wasReady)
                    ++edge->parent->unresolved;
                else
                    --edge->parent->unresolved;
                enqueueReady(*edge->parent);
            }
        enqueueReady(entry);
        dirty.push(entry);
        if (state == State::Failed)
            for (auto& request : requests)
                if (!request->failure
                    && std::find(request->closure.begin(), request->closure.end(), &entry)
                        != request->closure.end())
                    request->failure = &entry;
    }

    static void loaded(void* context, ResourceGraphExecutionResult completed) noexcept
    {
        auto* job = static_cast<Job*>(context);
        auto self = job->owner;
        auto& entry = *job->entry;
        --self->stats.inFlightLoads;
        const bool cancelled = completed.fileStatus == core::io::FileIoStatus::Cancelled;
        self->setState(entry,
            cancelled                        ? State::Dormant
                : completed.resource.success ? State::Ready
                                             : State::Failed);
        entry.failure =
            completed.diagnosticId ? completed.diagnosticId : "RESOURCE-DOMAIN-LOAD-FAILED";
        entry.diagnostics = std::move(completed.resource.diagnostics);
        if (entry.state == State::Ready) {
            entry.generation = self->manager.generationOf(entry.identity.id);
        } else
            self->releaseDependencies(entry);
        entry.job.reset();
        if (self->closed) {
            try {
                self->collect();
            } catch (...) {
            }
        }
    }

    void startLoads(const core::SharedPtr<Impl>& self)
    {
        if (closed)
            return;
        while (ready.head && stats.inFlightLoads < budget.maxInFlightLoads) {
            auto& entry = *ready.pop();
            const auto& id = entry.identity.id;
            if (entry.state != State::Dormant || !entry.pending || entry.unresolved)
                continue;
            try {
                auto descriptor = entry.factory();
                if (!descriptor || descriptor->reference().id != id
                    || descriptor->reference().type != entry.identity.type
                    || !sameDependencies(descriptor->dependencies(), entry.dependencies)) {
                    setState(entry, State::Failed);
                    entry.failure = "RESOURCE-DOMAIN-FACTORY-CONFLICT";
                    continue;
                }
                auto job = core::makeUnique<Job>();
                job->owner = self;
                job->entry = &entry;
                for (const auto& edge : entry.edges)
                    ++edge.child->parents;
                entry.holdsDependencies = true;
                entry.job = std::move(job);
                entry.state = State::Loading;
                ++stats.inFlightLoads;
                const auto submitted = manager.loadAsyncResource(runtime,
                    std::move(descriptor),
                    budget.load,
                    {loaded, entry.job.get()},
                    {},
                    entry.job->cancellation.token());
                if (submitted == ResourceGraphSubmitStatus::Accepted)
                    ++stats.loads;
                else {
                    --stats.inFlightLoads;
                    entry.job.reset();
                    releaseDependencies(entry);
                    setState(entry, State::Failed);
                    entry.failure = "RESOURCE-DOMAIN-LOAD-SUBMISSION";
                }
            } catch (...) {
                if (entry.job) {
                    --stats.inFlightLoads;
                    entry.job.reset();
                    releaseDependencies(entry);
                }
                setState(entry, State::Failed);
                entry.failure = "RESOURCE-DOMAIN-FACTORY-ALLOCATION";
            }
        }
    }

    void publish()
    {
        std::size_t delivered = 0;
        for (std::size_t i = 0; i < requests.size() && delivered < budget.maxRequests;) {
            auto& request = *requests[i];
            ResourceAcquireResult completion;
            bool terminal = true;
            if (closed)
                completion.result = result(ResourceAcquireStatus::Closed, "RESOURCE-DOMAIN-CLOSED");
            else if (request.cancellation.cancelled())
                completion.result =
                    result(ResourceAcquireStatus::Cancelled, "RESOURCE-DOMAIN-CANCELLED");
            else {
                Entry* failure = request.failure;
                if (failure) {
                    completion.result = result(ResourceAcquireStatus::Failed, failure->failure);
                    try {
                        completion.diagnostics = failure->diagnostics;
                    } catch (...) {
                    }
                } else if (request.root->state == State::Ready) {
                    if (leases.size() == budget.maxLeases)
                        completion.result = result(
                            ResourceAcquireStatus::BudgetExceeded, "RESOURCE-DOMAIN-LEASE-BUDGET");
                    else
                        try {
                            auto lease = core::makeShared<detail::ResourceLeaseState>();
                            lease->identity = request.root->identity;
                            lease->handle = manager.find<ResourceBase>(request.root->identity.id);
                            lease->lifetime = lifetime.ticket();
                            lease->owner = owner;
                            lease->released = leasesReleased;
                            if (ownedManager)
                                lease->keepAlive = self.lock();
                            leases.push_back({request.root, lease});
                            ++request.root->roots;
                            completion.lease = ResourceLease(std::move(lease));
                            completion.result = result(ResourceAcquireStatus::Ready);
                        } catch (...) {
                            completion.result = result(ResourceAcquireStatus::BudgetExceeded,
                                "RESOURCE-DOMAIN-LEASE-ALLOCATION");
                        }
                } else
                    terminal = false;
            }
            if (!terminal) {
                ++i;
                continue;
            }
            auto released = std::move(requests[i]);
            requests.erase(requests.begin() + static_cast<std::ptrdiff_t>(i));
            for (auto* entry : released->closure) {
                --entry->pending;
                dirty.push(*entry);
            }
            ++delivered;
            released->complete(std::move(completion));
        }
    }

    bool collect()
    {
        if (leasesReleased->exchange(false, std::memory_order_acq_rel)) {
            for (auto it = leases.begin(); it != leases.end();) {
                if (it->state.expired()) {
                    --it->root->roots;
                    dirty.push(*it->root);
                    it = leases.erase(it);
                } else
                    ++it;
            }
        }
        // An owned manager cannot be mutated outside this domain. Borrowed
        // managers retain the external-generation check on explicit dispatch.
        if (!ownedManager)
            for (auto& [id, owned] : entries) {
                auto& entry = *owned;
                if (entry.state == State::Ready
                    && (manager.stateOf(id) != ResourceState::Ready
                        || manager.generationOf(id) != entry.generation)) {
                    setState(entry, State::Failed);
                    entry.failure = "RESOURCE-DOMAIN-EXTERNAL-GENERATION";
                    releaseDependencies(entry);
                }
            }
        decltype(dirty) deferred;
        while (auto* candidate = dirty.pop()) {
            auto& entry = *candidate;
            if (entry.roots || entry.parents || entry.pending)
                continue;
            if (entry.state == State::Loading) {
                entry.job->cancellation.cancel();
                continue;
            }
            if (entry.state != State::Ready)
                continue;
            unloading = &entry;
            auto disposed = manager.unload(entry.identity.id);
            unloading = nullptr;
            if (!disposed.success) {
                deferred.push(entry);
                continue;
            }
            setState(entry, State::Dormant);
            releaseDependencies(entry);
            ++stats.releasedResources;
        }
        dirty = deferred;
        return !dirty.head;
    }
};

ResourceDomain::ResourceDomain(
    ResourceManager& manager, core::runtime::AsyncRuntime& runtime, ResourceDomainBudget budget)
    : impl_(core::makeShared<Impl>(manager, runtime, budget))
{
    impl_->self = impl_;
    manager.addLifecycleObserver(*impl_);
}

ResourceDomain::ResourceDomain(
    core::runtime::AsyncRuntimeDesc runtimeDesc, ResourceDomainBudget budget)
    : impl_([&] {
          auto manager = core::makeShared<ResourceManager>();
          auto runtime = core::makeShared<core::runtime::AsyncRuntime>(std::move(runtimeDesc));
          return core::makeShared<Impl>(*manager, *runtime, budget, manager, runtime);
      }())
{
    impl_->self = impl_;
    impl_->manager.addLifecycleObserver(*impl_);
}

ResourceDomain::~ResourceDomain()
{
    if (impl_->ownedManager) {
        impl_->closed = true;
        for (auto& [id, entry] : impl_->entries)
            if (entry->job)
                entry->job->cancellation.cancel();
        if (!impl_->pumping)
            impl_->publish();
    } else
        close();
    while (impl_->stats.inFlightLoads && !impl_->runtime.completionQueue().closed()) {
        impl_->runtime.dispatch();
        std::this_thread::yield();
    }
    impl_->collect();
    if (impl_->ownedRuntime)
        impl_->runtime.shutdown(core::runtime::AsyncRuntimeShutdownPolicy::CancelPending);
}

ResourceDomainResult ResourceDomain::registerResource(
    ResourceIdentity identity, ResourceDescriptorFactory factory)
{
    ResourceRegistration registration{std::move(identity), std::move(factory)};
    return registerResourcesImpl({&registration, 1}, false);
}

core::runtime::WorkSubmitResult ResourceDomain::submitPreparation(core::runtime::WorkItem item)
{
    if (!impl_->manager.isOwnerThread() || impl_->closed)
        return {core::runtime::WorkServiceStatus::ServiceClosed, {}};
    return impl_->runtime.workService().submit(std::move(item));
}

ResourceDomainResult ResourceDomain::registerResources(
    core::ArrayView<const ResourceRegistration> registrations)
{
    return registerResourcesImpl(registrations, true);
}

ResourceDomainResult ResourceDomain::registerResourcesImpl(
    core::ArrayView<const ResourceRegistration> registrations, bool requireCompleteClosure)
{
    auto& self = *impl_;
    if (!self.isOwner())
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-OWNER");
    if (self.closed)
        return result(ResourceAcquireStatus::Closed, "RESOURCE-DOMAIN-CLOSED");
    if (registrations.empty() || registrations.size() > self.budget.maxResources)
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-REGISTER-BATCH");
    try {
        decltype(self.entries) staged{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<core::StringView> seen{&core::defaultCpuMemoryResource()};
        for (const auto& registration : registrations) {
            const auto& identity = registration.identity;
            if (!isResourceUuid(identity.id) || identity.type.empty() || identity.version.empty()
                || !registration.factory)
                return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-IDENTITY");
            if (std::find(seen.begin(), seen.end(), identity.id) != seen.end())
                return result(
                    ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-REGISTER-DUPLICATE");
            seen.push_back(identity.id);
            const auto existing = self.entries.find(identity.id);
            if (existing != self.entries.end()) {
                if (!same(existing->second->identity, identity))
                    return result(ResourceAcquireStatus::IdentityConflict,
                        "RESOURCE-DOMAIN-IDENTITY-CONFLICT");
                if (!requireCompleteClosure)
                    continue;
            } else {
                if (staged.size() == self.budget.maxResources - self.entries.size())
                    return result(
                        ResourceAcquireStatus::BudgetExceeded, "RESOURCE-DOMAIN-RESOURCE-BUDGET");
                const auto* admitted = self.manager.recordSnapshot(identity.id);
                if (admitted && admitted->state != ResourceState::Unloaded)
                    return result(ResourceAcquireStatus::IdentityConflict,
                        "RESOURCE-DOMAIN-EXTERNAL-RESOURCE");
            }
            auto descriptor = registration.factory();
            if (!descriptor || descriptor->reference().id != identity.id
                || descriptor->reference().type != identity.type)
                return result(
                    ResourceAcquireStatus::IdentityConflict, "RESOURCE-DOMAIN-FACTORY-CONFLICT");
            const auto dependencies = descriptor->dependencies();
            for (auto current = dependencies.begin(); current != dependencies.end(); ++current) {
                const auto& dependency = *current;
                if (!isResourceUuid(dependency.id) || dependency.usage.empty())
                    return result(
                        ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-DEPENDENCY-ID");
                if (std::any_of(dependencies.begin(), current, [&](const auto& other) {
                        return other.id == dependency.id && other.usage == dependency.usage;
                    }))
                    return result(ResourceAcquireStatus::InvalidRequest,
                        "RESOURCE-DOMAIN-DEPENDENCY-DUPLICATE");
            }
            if (existing != self.entries.end()) {
                if (!sameDependencies(existing->second->dependencies, dependencies))
                    return result(ResourceAcquireStatus::IdentityConflict,
                        "RESOURCE-DOMAIN-FACTORY-CONFLICT");
                continue;
            }
            auto entry = core::makeUnique<Impl::Entry>();
            entry->identity = identity;
            entry->factory = registration.factory;
            entry->dependencies = dependencies;
            staged.emplace(identity.id, std::move(entry));
        }
        if (requireCompleteClosure) {
            Impl::VisitColors visited{&core::defaultCpuMemoryResource()};
            core::pmr::Vector<Impl::Entry*> closure{&core::defaultCpuMemoryResource()};
            for (const auto& registration : registrations) {
                const auto candidate = staged.find(registration.identity.id);
                auto* root = candidate != staged.end()
                    ? candidate->second.get()
                    : self.entries.find(registration.identity.id)->second.get();
                closure.clear();
                const auto planned = self.closure(*root, closure, &staged, &visited);
                if (!planned.success())
                    return planned;
            }
        }
        // Both maps use the core allocator. Node transfer performs no allocation;
        // publication cannot fail halfway after all validation/allocation above.
        self.entries.merge(staged);
        return result(ResourceAcquireStatus::Accepted);
    } catch (...) {
        return result(ResourceAcquireStatus::BudgetExceeded, "RESOURCE-DOMAIN-REGISTER-ALLOCATION");
    }
}

ResourceDomainResult ResourceDomain::acquireAsync(const ResourceIdentity& identity,
    ResourceAcquireCompletion completion,
    core::runtime::CancellationToken cancellation,
    bool retryFailed)
{
    auto& self = *impl_;
    if (!self.isOwner())
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-OWNER");
    if (self.closed)
        return result(ResourceAcquireStatus::Closed, "RESOURCE-DOMAIN-CLOSED");
    if (!completion)
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-COMPLETION");
    const auto found = self.entries.find(identity.id);
    if (found == self.entries.end())
        return result(ResourceAcquireStatus::MissingDefinition, "RESOURCE-DOMAIN-RESOURCE-MISSING");
    if (!same(found->second->identity, identity))
        return result(ResourceAcquireStatus::IdentityConflict, "RESOURCE-DOMAIN-IDENTITY-CONFLICT");
    if (self.requests.size() == self.budget.maxRequests)
        return result(ResourceAcquireStatus::BudgetExceeded, "RESOURCE-DOMAIN-REQUEST-BUDGET");
    try {
        auto request = core::makeUnique<Impl::Request>();
        request->root = found->second.get();
        const auto planned = self.closure(*request->root, request->closure);
        if (!planned.success())
            return planned;
        for (auto* entry : request->closure)
            self.connect(*entry);
        for (auto* entry : request->closure) {
            if (entry->state == Impl::State::Ready
                && (self.manager.stateOf(entry->identity.id) != ResourceState::Ready
                    || self.manager.generationOf(entry->identity.id) != entry->generation))
                return result(
                    ResourceAcquireStatus::IdentityConflict, "RESOURCE-DOMAIN-EXTERNAL-GENERATION");
            if (retryFailed && entry->state == Impl::State::Failed && entry->pending == 0) {
                entry->state = Impl::State::Dormant;
                entry->failure = nullptr;
                entry->diagnostics.clear();
            }
        }
        for (auto* entry : request->closure)
            if (entry->state == Impl::State::Failed) {
                request->failure = entry;
                break;
            }
        if (request->root->state == Impl::State::Ready)
            ++self.stats.readyHits;
        else if (request->root->pending)
            ++self.stats.joinedLoads;
        request->complete = std::move(completion);
        request->cancellation = std::move(cancellation);
        self.requests.push_back(std::move(request));
        for (auto* entry : self.requests.back()->closure) {
            ++entry->pending;
            self.enqueueReady(*entry);
        }
        return result(ResourceAcquireStatus::Accepted);
    } catch (...) {
        return result(ResourceAcquireStatus::BudgetExceeded, "RESOURCE-DOMAIN-REQUEST-ALLOCATION");
    }
}

ResourceDomainResult ResourceDomain::prewarmAsync(const ResourceIdentity& identity,
    ResourceAcquireCompletion completion,
    core::runtime::CancellationToken cancellation,
    ResourcePrewarmReadiness readiness)
{
    if (readiness != ResourcePrewarmReadiness::CpuReady)
        return result(
            ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-READINESS-PROVIDER-REQUIRED");
    return acquireAsync(identity, std::move(completion), std::move(cancellation));
}

ResourceAcquireResult ResourceDomain::acquire(
    const ResourceIdentity& identity, const core::SharedCallback<void()>& yield)
{
    if (!impl_->isOwner()) {
        ResourceAcquireResult refused;
        refused.result = result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-OWNER");
        return refused;
    }
    if (impl_->pumping) {
        ResourceAcquireResult refused;
        refused.result = result(ResourceAcquireStatus::Busy, "RESOURCE-DOMAIN-SYNC-REENTRANT");
        return refused;
    }

    struct Waiting {
        ResourceAcquireResult completed;
        bool done = false;
    };

    auto waiting = core::makeShared<Waiting>();
    core::runtime::CancellationSource cancellation;
    waiting->completed.result = acquireAsync(
        identity,
        [waiting](ResourceAcquireResult value) {
            waiting->completed = std::move(value);
            waiting->done = true;
        },
        cancellation.token());
    if (!waiting->completed.result.success())
        return std::move(waiting->completed);
    try {
        while (!waiting->done) {
            dispatch();
            if (yield)
                yield();
            else
                std::this_thread::yield();
        }
    } catch (...) {
        cancellation.cancel();
        throw;
    }
    return std::move(waiting->completed);
}

ResourceDomainResult ResourceDomain::dispatch()
{
    return dispatchImpl(0);
}

ResourceDomainResult ResourceDomain::dispatch(std::size_t maxCallbacks)
{
    if (!maxCallbacks)
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-DISPATCH-BUDGET");
    return dispatchImpl(maxCallbacks);
}

ResourceDomainResult ResourceDomain::dispatchFor(
    std::chrono::steady_clock::duration duration, std::size_t maxCallbacks)
{
    if (!impl_->isOwner())
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-OWNER");
    if (duration <= std::chrono::steady_clock::duration::zero() || !maxCallbacks)
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-DISPATCH-BUDGET");
    const auto deadline = std::chrono::steady_clock::now() + duration;
    do {
        const auto dispatched = dispatch(maxCallbacks);
        if (!dispatched.success() || impl_->closed || impl_->requests.empty())
            return dispatched;
        if (std::chrono::steady_clock::now() >= deadline)
            break;
        if (impl_->ready.head && impl_->stats.inFlightLoads < impl_->budget.maxInFlightLoads)
            continue;
        impl_->runtime.completionQueue().waitUntil(deadline);
    } while (std::chrono::steady_clock::now() < deadline);
    return result(ResourceAcquireStatus::Accepted);
}

ResourceDomainResult ResourceDomain::dispatchImpl(std::size_t maxCallbacks)
{
    auto self = impl_;
    if (!self->isOwner())
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-OWNER");
    if (self->pumping)
        return result(ResourceAcquireStatus::Busy, "RESOURCE-DOMAIN-REENTRANT-DISPATCH");
    self->pumping = true;
    try {
        if (maxCallbacks)
            self->runtime.dispatch(maxCallbacks);
        else
            self->runtime.dispatch();
        self->collect();
        self->publish();
        self->startLoads(self);
        self->collect();
        self->pumping = false;
        return result(ResourceAcquireStatus::Accepted);
    } catch (...) {
        self->pumping = false;
        throw;
    }
}

ResourceDomainResult ResourceDomain::collect()
{
    if (!impl_->isOwner())
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-OWNER");
    return impl_->collect()
        ? result(ResourceAcquireStatus::Accepted)
        : result(ResourceAcquireStatus::Busy, "RESOURCE-DOMAIN-DISPOSAL-DEFERRED");
}

ResourceDomainResult ResourceDomain::close()
{
    auto self = impl_;
    if (!self->isOwner())
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-OWNER");
    if (!self->closed) {
        self->closed = true;
        self->lifetime.advance();
        self->leases.clear();
        for (auto& [id, entry] : self->entries) {
            entry->roots = 0;
            self->dirty.push(*entry);
            if (entry->job)
                entry->job->cancellation.cancel();
        }
    }
    if (!self->pumping)
        self->publish();
    const bool disposed = self->collect();
    return self->stats.inFlightLoads || !self->requests.empty() || !disposed
        ? result(ResourceAcquireStatus::Busy, "RESOURCE-DOMAIN-DRAINING")
        : result(ResourceAcquireStatus::Closed);
}

ResourceState ResourceDomain::stateOf(NameView id) const
{
    if (!impl_->isOwner())
        throw std::logic_error("RESOURCE-DOMAIN-OWNER");
    const auto found =
        impl_->entries.find(core::pmr::String(id, &core::defaultCpuMemoryResource()));
    if (found == impl_->entries.end())
        return ResourceState::Unloaded;
    switch (found->second->state) {
    case Impl::State::Dormant:
        return ResourceState::Declared;
    case Impl::State::Loading:
        return ResourceState::Loading;
    case Impl::State::Ready:
        return ResourceState::Ready;
    case Impl::State::Failed:
        return ResourceState::Failed;
    }
    return ResourceState::Failed;
}

ResourceDomainTelemetry ResourceDomain::telemetry() const
{
    if (!impl_->isOwner())
        throw std::logic_error("RESOURCE-DOMAIN-OWNER");
    auto value = impl_->stats;
    value.pendingRequests = impl_->requests.size();
    value.liveLeases = impl_->leases.size();
    return value;
}

const ResourceManager& ResourceDomain::resources() const
{
    return impl_->manager;
}

} // namespace arp::resource
