#include "core/runtime/async_runtime.h"
#include "resource/resource_candidate_preparation.h"
#include "resource/resource_domain.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace arp;
using namespace resource;
constexpr const char* leaf = "165b5ee8-2514-46e7-8472-1bfa17c1e226";
constexpr const char* first = "94d1a54f-2719-41d1-b37c-5b5d266feb52";
constexpr const char* second = "932adff7-c6cb-4aaf-a4c2-a54e1a299f12";

void check(bool ok, const char* diagnostic)
{
    if (!ok)
        throw std::runtime_error(diagnostic);
}

ResourceIdentity identity(const char* id)
{
    return {id, "DomainProbe", "sealed-content-v1"};
}

core::runtime::AsyncRuntimeDesc runtimeDesc()
{
    return {128, 4, 64, 32};
}

ResourceDomainBudget budget()
{
    return {32, 64, 64, 4, {32, 64, 1024 * 1024, 1024 * 1024, 32}};
}

struct Observations {
    core::pmr::Map<core::pmr::String, unsigned> loads{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> destroyed{&core::defaultCpuMemoryResource()};
    bool fail = false;
};

class Probe final : public ResourceBase {
public:
    Probe(const char* id,
        const char* dependency,
        std::filesystem::path path,
        Observations& observations)
        : ResourceBase({id, "DomainProbe", {}, "Contract Complete", {}}), path_(std::move(path)),
          observations_(observations)
    {
        if (dependency)
            addDependency({dependency, "strong"});
    }

    ~Probe() override
    {
        if (loaded_)
            observations_.destroyed.push_back(reference().id);
    }

    bool collectFileReadIntents(ResourceManager&,
        core::pmr::Vector<ResourceFileReadIntent>& intents,
        core::pmr::String&) const override
    {
        if (!path_.empty()) {
            ResourceFileReadIntent intent;
            intent.path = path_.generic_string().c_str();
            intent.pathScope = core::io::FilePathScope::HostFilesystem;
            intent.role = "sealed-input";
            intent.maxBytes = 4096;
            intents.push_back(std::move(intent));
        }
        return true;
    }

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override
    {
        ++observations_.loads[reference().id];
        if (!path_.empty()) {
            check(context.requirePreloadedSources && context.sources
                    && context.sources->find(path_.generic_string().c_str()),
                "source was reopened or absent");
        }
        if (observations_.fail && reference().id == first)
            return {false, ResourceState::Failed, {}, {}, "injected-transient-failure"};
        auto admitted = manager.admitResourceMetadata(*this, context.policy);
        loaded_ = admitted.success;
        return admitted;
    }

private:
    std::filesystem::path path_;
    Observations& observations_;
    bool loaded_ = false;
};

void define(ResourceDomain& domain,
    Observations& observations,
    const char* id,
    const char* dependency = nullptr,
    const std::filesystem::path& path = {})
{
    auto registered = domain.registerResource(
        identity(id), [&, id, dependency, path]() -> core::UniquePtr<ResourceBase> {
            return core::makeUnique<Probe>(id, dependency, path, observations);
        });
    check(registered.success(), "definition rejected");
}

template <class Predicate> void until(ResourceDomain& domain, Predicate done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        check(std::chrono::steady_clock::now() < deadline, "domain dispatch timeout");
        check(domain.dispatch().success(), "dispatch rejected");
        std::this_thread::yield();
    }
}

void sharing(const std::filesystem::path& source)
{
    Observations observations;
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(runtimeDesc());
    ResourceDomain domain(manager, runtime, budget());
    define(domain, observations, leaf, nullptr, source);
    define(domain, observations, first, leaf);
    define(domain, observations, second, leaf);
    ResourceLease a, b, c;
    unsigned completed = 0;
    check(domain
              .prewarmAsync(identity(first),
                  [&](ResourceAcquireResult r) {
                      check(r.result.success(), "prewarm failed");
                      a = std::move(r.lease);
                      ++completed;
                  })
              .success(),
        "prewarm submit");
    check(domain
              .acquireAsync(identity(first),
                  [&](ResourceAcquireResult r) {
                      check(r.result.success(), "joined acquisition failed");
                      b = std::move(r.lease);
                      ++completed;
                  })
              .success(),
        "join submit");
    check(domain
              .acquireAsync(identity(second),
                  [&](ResourceAcquireResult r) {
                      check(r.result.success(), "shared dependency acquisition failed");
                      c = std::move(r.lease);
                      ++completed;
                  })
              .success(),
        "second submit");
    until(domain, [&] {
        return completed == 3;
    });
    check(a.get<Probe>() && a.get<Probe>() == b.get<Probe>(), "canonical resource differs");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    const auto allocationsBefore = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        for (unsigned i = 0; i < 512; ++i)
            check(a.ready() && a.resource() == b.get<Probe>() && c.ready(),
                "lease observation failed under exhausted allocation budget");
    }
    check(core::defaultCpuAllocator().stats().allocationCount == allocationsBefore
            && !quota->liveBytes(),
        "Ready lease observation allocated memory");
    check(observations.loads[leaf] == 1 && observations.loads[first] == 1
            && observations.loads[second] == 1,
        "duplicate resource processing");
    check(runtime.telemetry().fileService.submitted == 1, "duplicate source acquisition");
    check(!manager.unload(leaf).success && manager.find<Probe>(leaf).ready(),
        "live dependency unloaded");
    const auto before = domain.telemetry().loads;
    auto again = domain.acquire(identity(first));
    check(again.lease.get<Probe>() == a.get<Probe>() && domain.telemetry().loads == before,
        "Ready acquisition reloaded resource");
    again.lease.reset();
    a.reset();
    b.reset();
    domain.collect();
    check(manager.stateOf(first) == ResourceState::Unloaded && manager.find<Probe>(leaf).ready(),
        "shared dependency released before final root");
    c.reset();
    domain.collect();
    check(manager.stateOf(second) == ResourceState::Unloaded
            && manager.stateOf(leaf) == ResourceState::Unloaded,
        "last root did not release closure");
    check(observations.destroyed.size() == 3 && observations.destroyed.back() == leaf,
        "dependency destruction order");
    std::cout
        << "PASS prewarm/Ready/in-flight sharing, allocation-free observation, one source read, shared dependency and reverse release\n";
}

void cancellation(const std::filesystem::path& source)
{
    Observations observations;
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(runtimeDesc());
    ResourceDomain domain(manager, runtime, budget());
    define(domain, observations, leaf, nullptr, source);
    ResourceLease retained;
    core::runtime::CancellationSource cancellation;
    unsigned cancelled = 0, ready = 0;
    domain.acquireAsync(
        identity(leaf),
        [&](ResourceAcquireResult r) {
            check(r.result.status == ResourceAcquireStatus::Cancelled, "cancel status");
            ++cancelled;
        },
        cancellation.token());
    domain.acquireAsync(identity(leaf), [&](ResourceAcquireResult r) {
        check(r.lease.ready(), "other subscriber cancelled");
        retained = std::move(r.lease);
        ++ready;
    });
    domain.dispatch();
    cancellation.cancel();
    until(domain, [&] {
        return cancelled == 1 && ready == 1;
    });
    check(observations.loads[leaf] == 1 && runtime.telemetry().fileService.submitted == 1,
        "cancel triggered duplicate loading");
    std::thread worker([lease = std::move(retained)]() mutable {
        check(!lease.ready(), "lease observed from non-owner thread");
        lease.reset();
    });
    worker.join();
    domain.collect();
    check(manager.stateOf(leaf) == ResourceState::Unloaded, "cross-thread final lease release");
    bool unwound = false;
    try {
        domain.acquire(identity(leaf), [] {
            throw std::runtime_error("closed-window");
        });
    } catch (const std::runtime_error&) {
        unwound = true;
    }
    check(unwound, "cold yield did not unwind");
    until(domain, [&] {
        return !domain.telemetry().pendingRequests && !domain.telemetry().inFlightLoads;
    });
    auto retry = domain.acquire(identity(leaf));
    check(retry.lease.ready(), "abandoned load prevented later acquisition");
    std::cout
        << "PASS individual cancellation, owner observation, cross-thread release, cold cancellation\n";
}

void failures(const std::filesystem::path& source)
{
    Observations observations;
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(runtimeDesc());
    ResourceDomain domain(manager, runtime, budget());
    define(domain, observations, leaf, nullptr, source);
    define(domain, observations, first, leaf);
    auto wrong = identity(first);
    wrong.version = "different-content";
    check(domain.acquire(wrong).result.status == ResourceAcquireStatus::IdentityConflict,
        "version conflict accepted");
    observations.fail = true;
    auto failed = domain.acquire(identity(first));
    check(failed.result.status == ResourceAcquireStatus::Failed, "failure published Ready");
    domain.collect();
    check(manager.stateOf(leaf) == ResourceState::Unloaded, "failed root retained dependency");
    auto attempts = observations.loads[first];
    domain.acquire(identity(first));
    check(observations.loads[first] == attempts, "failure silently retried");
    observations.fail = false;
    ResourceLease retried;
    bool done = false;
    check(domain
              .acquireAsync(
                  identity(first),
                  [&](ResourceAcquireResult r) {
                      check(r.lease.ready(), "explicit retry failed");
                      retried = std::move(r.lease);
                      done = true;
                  },
                  {},
                  true)
              .success(),
        "explicit retry submission");
    until(domain, [&] {
        return done;
    });
    ResourceLease escaped = retried;
    check(domain.close().status == ResourceAcquireStatus::Closed, "close did not finish");
    check(!escaped.ready(), "lease survived closed domain as usable pointer");
    std::cout
        << "PASS identity conflict, sticky failure, explicit retry, failed closure cleanup, close invalidation\n";
}

void invalidPlans()
{
    Observations observations;
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(runtimeDesc());
    ResourceDomain domain(manager, runtime, budget());
    define(domain, observations, first, leaf);
    check(domain.acquire(identity(first)).result.status == ResourceAcquireStatus::MissingDefinition,
        "missing dependency accepted");
    define(domain, observations, leaf, first);
    check(domain.acquire(identity(first)).result.status == ResourceAcquireStatus::DependencyCycle,
        "cycle accepted");
    check(domain.telemetry().loads == 0, "invalid dependency plan performed IO");
    std::cout << "PASS missing definition and cycle rejected before acquisition\n";
}

void ownedLifetime()
{
    Observations observations;
    ResourceLease kept;
    {
        ResourceDomain domain(runtimeDesc(), budget());
        define(domain, observations, leaf);
        kept = domain.acquire(identity(leaf)).lease;
        check(kept.ready(), "owned acquisition failed");
    }
    check(kept.ready() && observations.destroyed.empty(),
        "owned immutable resource died before lease");
    kept.reset();
    check(observations.destroyed.size() == 1, "final lease did not dispose owned resource context");
    std::cout
        << "PASS owned CPU resource outlives facade after producers drain, final lease releases context\n";
}

void limitsAndReentrancy()
{
    Observations observations;
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(runtimeDesc());
    auto bounds = budget();
    bounds.maxResources = 2;
    bounds.maxRequests = 1;
    bounds.maxLeases = 1;
    ResourceDomain domain(manager, runtime, bounds);
    define(domain, observations, leaf);
    define(domain, observations, first);
    check(domain.registerResource(identity(second),
                    [&]() -> core::UniquePtr<ResourceBase> {
                        return core::makeUnique<Probe>(
                            second, nullptr, std::filesystem::path{}, observations);
                    })
                .status
            == ResourceAcquireStatus::BudgetExceeded,
        "resource catalog exceeded budget");
    check(domain.prewarmAsync(
                    identity(leaf),
                    [](ResourceAcquireResult) {},
                    {},
                    ResourcePrewarmReadiness::GpuReady)
                .status
            == ResourceAcquireStatus::InvalidRequest,
        "unsupported GPU prewarm claimed readiness");
    ResourceLease kept;
    bool completed = false;
    domain.acquireAsync(identity(leaf), [&](ResourceAcquireResult value) {
        check(domain.acquire(identity(first)).result.status == ResourceAcquireStatus::Busy,
            "synchronous callback reentry accepted");
        kept = std::move(value.lease);
        completed = true;
    });
    check(domain.acquireAsync(identity(leaf), [](ResourceAcquireResult) {}).status
            == ResourceAcquireStatus::BudgetExceeded,
        "request count exceeded budget");
    until(domain, [&] {
        return completed;
    });
    check(domain.acquire(identity(leaf)).result.status == ResourceAcquireStatus::BudgetExceeded,
        "lease count exceeded budget");
    std::thread foreign([&] {
        check(domain.acquire(identity(leaf)).result.status == ResourceAcquireStatus::InvalidRequest,
            "foreign synchronous acquisition accepted");
    });
    foreign.join();
    kept.reset();
    domain.collect();

    struct Deferred final : ResourceLifecycleObserver {
        bool block = true;

        bool beforeResourceUnload(NameView) noexcept override { return !block; }

        bool beforeRetiredResourcesRelease() noexcept override { return !block; }
    } observer;

    manager.addLifecycleObserver(observer);
    auto held = domain.acquire(identity(first));
    held.lease.reset();
    check(domain.collect().status == ResourceAcquireStatus::Busy
            && manager.stateOf(first) == ResourceState::Ready,
        "deferred disposal lost Ready resource");
    observer.block = false;
    check(domain.collect().success() && manager.stateOf(first) == ResourceState::Unloaded,
        "deferred disposal did not resume");
    manager.removeLifecycleObserver(observer);
    std::cout
        << "PASS bounded catalog/requests/leases, unsupported readiness, owner/reentry gates and deferred disposal\n";
}

struct PreparationObservation {
    std::atomic<unsigned> prepared{0};
    unsigned target = 2;
    bool fail = false;
};

class PreparedProbe final : public ResourceBase, public ResourceCandidatePreparation {
public:
    PreparedProbe(const char* id, std::filesystem::path source, PreparationObservation& observation)
        : ResourceBase({id, "PreparedProbe", {}, "Contract Complete", {}}),
          source_(std::move(source)), observation_(observation), owner_(std::this_thread::get_id())
    {}

    bool collectFileReadIntents(ResourceManager&,
        core::pmr::Vector<ResourceFileReadIntent>& intents,
        core::pmr::String&) const override
    {
        ResourceFileReadIntent intent;
        intent.path = source_.generic_string().c_str();
        intent.maxBytes = 4096;
        intent.pathScope = core::io::FilePathScope::HostFilesystem;
        intent.role = "candidate";
        intents.push_back(std::move(intent));
        return true;
    }

    ResourceCandidatePreparationResult prepareResourceCandidate(
        const ResourceCandidatePreparationContext& context) override
    {
        worker = std::this_thread::get_id();
        const auto* bytes = context.sources.find(source_.generic_string().c_str());
        if (!bytes || bytes->bytes.size() > context.maxDecodedBytes)
            return {false, "PREPARED-PROBE-BUDGET"};
        payload_.assign(bytes->bytes.begin(), bytes->bytes.end());
        ++observation_.prepared;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (observation_.prepared.load() < observation_.target) {
            if (context.cancellation.cancelled() || std::chrono::steady_clock::now() >= until)
                return {false, "PREPARED-PROBE-CANCELLED"};
            std::this_thread::yield();
        }
        return {!observation_.fail, observation_.fail ? "PREPARED-PROBE-FAILURE" : nullptr};
    }

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override
    {
        check(owner_ == std::this_thread::get_id(), "candidate published off owner");
        check(worker != owner_ && !payload_.empty(), "candidate was not prepared on worker");
        return manager.admitResourceMetadata(*this, context.policy);
    }

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::ImageDecode;
    }

    std::size_t processedByteCount() const noexcept override { return payload_.size(); }

    std::thread::id worker;

private:
    std::filesystem::path source_;
    PreparationObservation& observation_;
    std::thread::id owner_;
    core::ByteBuffer payload_{&core::defaultCpuMemoryResource()};
};

void background(const std::filesystem::path& source)
{
    PreparationObservation observation;
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(runtimeDesc());
    ResourceDomain domain(manager, runtime, budget());
    auto registerPrepared = [&](const char* id) {
        return domain.registerResource(
            {id, "PreparedProbe", "sealed-v1"}, [&, id]() -> core::UniquePtr<ResourceBase> {
                return core::makeUnique<PreparedProbe>(id, source, observation);
            });
    };
    check(registerPrepared(leaf).success() && registerPrepared(first).success(),
        "prepared registration");
    ResourceLease a, b;
    unsigned done = 0;
    check(domain
              .prewarmAsync({leaf, "PreparedProbe", "sealed-v1"},
                  [&](ResourceAcquireResult r) {
                      check(r.lease.ready(), "prepared prewarm failed");
                      a = std::move(r.lease);
                      ++done;
                  })
              .success(),
        "prepared prewarm submit");
    check(domain
              .acquireAsync({first, "PreparedProbe", "sealed-v1"},
                  [&](ResourceAcquireResult r) {
                      check(r.lease.ready(), "prepared acquire failed");
                      b = std::move(r.lease);
                      ++done;
                  })
              .success(),
        "prepared acquire submit");
    until(domain, [&] {
        return done == 2;
    });
    check(a.get<PreparedProbe>()->worker != b.get<PreparedProbe>()->worker,
        "independent candidates did not prepare concurrently");
    a.reset();
    b.reset();
    domain.collect();
    observation.target = 1;
    observation.fail = true;
    auto failure = domain.acquire({leaf, "PreparedProbe", "sealed-v1"});
    check(!failure.lease.ready() && failure.result.status == ResourceAcquireStatus::Failed,
        "failed worker candidate published Ready");
    check(manager.stateOf(leaf) != ResourceState::Ready, "worker failure mutated registry");
    std::cout
        << "PASS concurrent immutable-source candidate preparation, owner publication and preparation failure\n";
}
} // namespace

int main()
{
    try {
        const auto fixture = std::filesystem::current_path() / "resource-domain-fixture.bin";
        {
            std::ofstream out(fixture, std::ios::binary);
            out << "sealed domain fixture";
        }
        sharing(fixture);
        cancellation(fixture);
        failures(fixture);
        invalidPlans();
        ownedLifetime();
        limitsAndReentrancy();
        background(fixture);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
