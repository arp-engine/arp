#include "resource/resource_domain.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <thread>

namespace arp::resource {
namespace detail {
struct ResourceLeaseState {
    ResourceIdentity identity;
    ResourceHandle<ResourceBase> handle;
    core::LifetimeEpoch::Ticket lifetime;
    std::thread::id owner;
    core::SharedPtr<void> keepAlive;
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
    };

    struct Job {
        core::SharedPtr<Impl> owner;
        Entry* entry = nullptr;
        core::runtime::CancellationSource cancellation;
    };

    struct Request {
        Entry* root = nullptr;
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

    ResourceDomainResult closure(
        Entry& root, core::pmr::Vector<Entry*>& output, const decltype(entries)* staged = nullptr)
    {
        struct Frame {
            Entry* entry;
            std::size_t next = 0;
        };

        core::pmr::Map<Entry*, unsigned> colors{&core::defaultCpuMemoryResource()};
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
        for (const auto& dependency : entry.dependencies)
            --entries.find(dependency.id)->second->parents;
        entry.holdsDependencies = false;
    }

    static void loaded(void* context, ResourceGraphExecutionResult completed) noexcept
    {
        auto* job = static_cast<Job*>(context);
        auto self = job->owner;
        auto& entry = *job->entry;
        --self->stats.inFlightLoads;
        const bool cancelled = completed.fileStatus == core::io::FileIoStatus::Cancelled;
        entry.state = cancelled          ? State::Dormant
            : completed.resource.success ? State::Ready
                                         : State::Failed;
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
        for (auto& [id, owned] : entries) {
            auto& entry = *owned;
            if (stats.inFlightLoads == budget.maxInFlightLoads)
                break;
            if (entry.state != State::Dormant || !entry.pending)
                continue;
            bool ready = true;
            for (const auto& dependency : entry.dependencies)
                ready &= entries.at(dependency.id)->state == State::Ready;
            if (!ready)
                continue;
            try {
                auto descriptor = entry.factory();
                if (!descriptor || descriptor->reference().id != id
                    || descriptor->reference().type != entry.identity.type
                    || !sameDependencies(descriptor->dependencies(), entry.dependencies)) {
                    entry.state = State::Failed;
                    entry.failure = "RESOURCE-DOMAIN-FACTORY-CONFLICT";
                    continue;
                }
                auto job = core::makeUnique<Job>();
                job->owner = self;
                job->entry = &entry;
                for (const auto& dependency : entry.dependencies)
                    ++entries.at(dependency.id)->parents;
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
                    entry.state = State::Failed;
                    entry.failure = "RESOURCE-DOMAIN-LOAD-SUBMISSION";
                }
            } catch (...) {
                entry.state = State::Failed;
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
                Entry* failure = nullptr;
                for (auto* entry : request.closure)
                    if (entry->state == State::Failed) {
                        failure = entry;
                        break;
                    }
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
            for (auto* entry : released->closure)
                --entry->pending;
            ++delivered;
            released->complete(std::move(completion));
        }
    }

    bool collect()
    {
        for (auto it = leases.begin(); it != leases.end();) {
            if (it->state.expired()) {
                --it->root->roots;
                it = leases.erase(it);
            } else
                ++it;
        }
        bool deferred = false, changed = true;
        while (changed) {
            changed = false;
            for (auto& [id, owned] : entries) {
                auto& entry = *owned;
                if (entry.state == State::Ready
                    && (manager.stateOf(id) != ResourceState::Ready
                        || manager.generationOf(id) != entry.generation)) {
                    entry.state = State::Failed;
                    entry.failure = "RESOURCE-DOMAIN-EXTERNAL-GENERATION";
                    releaseDependencies(entry);
                    continue;
                }
                if (entry.roots || entry.parents || entry.pending)
                    continue;
                if (entry.state == State::Loading) {
                    entry.job->cancellation.cancel();
                    continue;
                }
                if (entry.state != State::Ready)
                    continue;
                unloading = &entry;
                auto disposed = manager.unload(id);
                unloading = nullptr;
                if (!disposed.success) {
                    deferred = true;
                    continue;
                }
                entry.state = State::Dormant;
                releaseDependencies(entry);
                ++stats.releasedResources;
                changed = true;
            }
        }
        return !deferred;
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
        if (requireCompleteClosure)
            for (const auto& registration : registrations) {
                const auto candidate = staged.find(registration.identity.id);
                auto* root = candidate != staged.end()
                    ? candidate->second.get()
                    : self.entries.find(registration.identity.id)->second.get();
                core::pmr::Vector<Impl::Entry*> closure{&core::defaultCpuMemoryResource()};
                const auto planned = self.closure(*root, closure, &staged);
                if (!planned.success())
                    return planned;
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
        if (request->root->state == Impl::State::Ready)
            ++self.stats.readyHits;
        else if (request->root->pending)
            ++self.stats.joinedLoads;
        request->complete = std::move(completion);
        request->cancellation = std::move(cancellation);
        self.requests.push_back(std::move(request));
        for (auto* entry : self.requests.back()->closure)
            ++entry->pending;
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
    auto self = impl_;
    if (!self->isOwner())
        return result(ResourceAcquireStatus::InvalidRequest, "RESOURCE-DOMAIN-OWNER");
    if (self->pumping)
        return result(ResourceAcquireStatus::Busy, "RESOURCE-DOMAIN-REENTRANT-DISPATCH");
    self->pumping = true;
    try {
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
