#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <future>
#include <thread>

namespace {
using namespace arp;
constexpr std::size_t workerCount = 4;
constexpr unsigned copyRounds = 32;

// Immutable payload; only the independent destruction observation is mutable.
class LifetimeValue final {
public:
    explicit LifetimeValue(std::atomic<unsigned>& destroyed) : destroyed_(destroyed) {}

    ~LifetimeValue() { destroyed_.fetch_add(1, std::memory_order_relaxed); }

    const int marker = 73;

private:
    std::atomic<unsigned>& destroyed_;
};

void separateOwners(bool releaseRootBeforeWorkers)
{
    using Owner = reflection::detail::ObjectRef<LifetimeValue>;
    const auto baseline = core::defaultCpuAllocator().stats();
    std::atomic<unsigned> destroyed{0};
    {
        auto root = Owner::make(destroyed);
        assert(root.unique());
        std::promise<void> admission;
        const auto start = admission.get_future().share();
        std::array<std::thread, workerCount> workers;
        for (auto& worker : workers) {
            worker = std::thread([owner = root, start] {
                start.wait();
                for (unsigned round = 0; round < copyRounds; ++round) {
                    auto copy = owner;
                    Owner assigned;
                    assigned = copy;
                    auto moved = std::move(assigned);
                    assert(assigned.get() == nullptr && moved.identity() == owner.identity());
                    assert(moved->marker == 73 && !moved.unique());
                    moved = moved;
                    copy.swap(moved);
                }
            });
        }
        assert(!root.unique() && destroyed.load() == 0);
        if (releaseRootBeforeWorkers)
            root = Owner{};
        admission.set_value();
        for (auto& worker : workers)
            worker.join();
        if (releaseRootBeforeWorkers)
            assert(root.get() == nullptr && !root.unique() && destroyed.load() == 1);
        else
            assert(root.unique() && root->marker == 73 && destroyed.load() == 0);
    }
    assert(destroyed.load() == 1);
    const auto after = core::defaultCpuAllocator().stats();
    assert(after.liveBytes == baseline.liveBytes);
    assert(after.allocationCount - after.deallocationCount
        == baseline.allocationCount - baseline.deallocationCount);
}

class MetadataValue final {
public:
    int health = 19;
};

void immutableMetadata()
{
    using namespace reflection;
    registration::class_<MetadataValue>("ReflectionLifetimeMetadata")
        .property("health", &MetadataValue::health);
    const auto* metadata = class_of<MetadataValue>();
    // Finish lazy registration and payload adaptation before publishing readers.
    MetadataValue warm;
    object warmReference = object::native_reference(&warm);
    const auto fields = metadata->get_all_properties();
    assert(fields.at("health").cast<const property&>().get_value(warmReference).cast<int>() == 19);
    const auto baseline = core::defaultCpuAllocator().stats();
    {
        std::promise<void> admission;
        const auto start = admission.get_future().share();
        std::array<std::thread, workerCount> workers;
        for (auto& worker : workers) {
            worker = std::thread([metadata, start] {
                MetadataValue local;
                const auto reference = object::native_reference(&local);
                start.wait();
                for (unsigned round = 0; round < copyRounds; ++round) {
                    const auto properties = metadata->get_all_properties();
                    object copy = properties.at("health");
                    object assigned;
                    assigned = copy;
                    object moved = std::move(assigned);
                    const auto& field = moved.cast<const property&>();
                    assert(field.name() == "health" && field.type() == type_of<int>());
                    assert(field.get_value(reference).cast<int>() == 19);
                }
            });
        }
        admission.set_value();
        for (auto& worker : workers)
            worker.join();
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(after.liveBytes == baseline.liveBytes);
    assert(after.allocationCount - after.deallocationCount
        == baseline.allocationCount - baseline.deallocationCount);
}
} // namespace

int main()
{
    separateOwners(false);
    separateOwners(true);
    immutableMetadata();
    std::puts(
        "PASS bounded separate-owner retain/copy/move/release, unique observation, last-owner single destruction, immutable metadata; core live bytes/count delta=0");
}
