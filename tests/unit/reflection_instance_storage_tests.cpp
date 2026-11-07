#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"

#include <cassert>
#include <cstdio>
#include <new>

namespace {
using namespace arp;

void storageContract()
{
    auto* cls = reflection::db::instance().register_class(
        "InstanceStorageContract", reflection::instance_t, 0, 0, nullptr);
    auto& memory = core::defaultCpuMemoryResource();
    core::pmr::Vector<core::pmr::String> names{&memory};
    names.emplace_back("long_declared_field_name");
    names.emplace_back("another_field");
    names.emplace_back("long_declared_field_name");
    const reflection::object number{42};
    (void)reflection::object::undefined();
    const auto baseline = core::defaultCpuAllocator().stats().liveBytes;
    {
        reflection::instance original{cls, names};
        assert(original.slots().size() == 2);
        assert(original.__getitem__("another_field").is_undefined());
        original.__setitem__("long_declared_field_name", number);
        auto* retained = &original.slot("long_declared_field_name");
        for (int i = 0; i < 200; ++i) {
            auto name = core::pmr::String("dynamic_field_", &memory);
            name += core::pmr::String(std::to_string(i), &memory);
            original.__setitem__(name, number);
        }
        assert(retained == &original.slot("long_declared_field_name"));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (int i = 0; i < 100; ++i)
            assert(original.__getitem__("long_declared_field_name").cast<int>() == 42);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);

        reflection::instance copied{original};
        assert(copied.slots().size() == original.slots().size());
        copied.__setitem__("another_field", number);
        assert(original.__getitem__("another_field").is_undefined());
        *copied.__getitem__("long_declared_field_name").ptr<int>() = 43;
        assert(original.__getitem__("long_declared_field_name").cast<int>() == 43);
        reflection::instance moved{std::move(original)};
        assert(&moved.slot("long_declared_field_name") == retained);
        original.__setitem__("reused_moved_source", number);
        assert(original.slots().size() == 1);
        reflection::instance assigned{cls};
        assigned = copied;
        assigned = std::move(moved);
        assert(&assigned.slot("long_declared_field_name") == retained);
        assigned.invalidate();
        assert(!assigned.alive() && assigned.slots().empty());
        assert(assigned.__getitem__("long_declared_field_name").is_undefined());
    }
    assert(core::defaultCpuAllocator().stats().liveBytes == baseline);
}

void quotaContract()
{
    auto& memory = core::defaultCpuMemoryResource();
    core::pmr::Vector<core::pmr::String> fields{&memory};
    fields.emplace_back("quota_field");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    bool rejected = false;
    {
        core::CpuMemoryQuotaScope binding(quota);
        try {
            reflection::instance instance{nullptr, fields};
        } catch (const std::bad_alloc&) {
            rejected = true;
        }
    }
    assert(rejected && quota->liveBytes() == 0);
}

void sharedLayoutContract()
{
    auto& memory = core::defaultCpuMemoryResource();
    core::pmr::Vector<core::pmr::String> names{&memory};
    names.emplace_back("z_declared");
    names.emplace_back("a_declared");
    names.emplace_back("a_declared");
    auto layout = core::makeShared<reflection::InstanceFieldLayout>(names);
    names.clear();
    reflection::instance first{nullptr, layout};
    reflection::instance second{nullptr, layout};
    const auto a = layout->find("a_declared");
    const auto z = layout->find("z_declared");
    assert(first.findDeclaredSlot(layout.get(), a) == &first.slot("a_declared"));
    assert(first.findDeclaredSlot(layout.get(), z) == &first.slot("z_declared"));
    assert(first.findDeclaredSlot(layout.get(), layout->names().size()) == nullptr);
    auto equivalent = core::makeShared<reflection::InstanceFieldLayout>(layout->names());
    assert(first.findDeclaredSlot(equivalent.get(), a) == nullptr);
    assert(first.findDeclaredSlot(nullptr, a) == nullptr);
    assert(first.fieldLayoutAllocation() == second.fieldLayoutAllocation());
    assert(first.slots().size() == 2);
    assert(&first.slot("a_declared") != &second.slot("a_declared"));
    auto* retained = &first.slot("z_declared");
    layout.reset();
    first.__setitem__("m_dynamic", reflection::object{3});
    first.__setitem__("0_dynamic", reflection::object{4});
    core::pmr::String previous{&memory};
    for (const auto& item : first.slots()) {
        assert(previous.empty() || previous < item.first);
        assert(&first.slot(item.first) == &item.second);
        const auto found = first.slots().find(item.first);
        assert(found != first.slots().end());
        assert(&found->second == &item.second);
        auto next = found;
        ++next;
        if (next != first.slots().end())
            assert(item.first < next->first);
        previous = item.first;
    }
    assert(first.slots().find("absent") == first.slots().end());
    assert(retained == &first.slot("z_declared"));
    assert(second.slots().size() == 2);
    second.invalidate();
    assert(second.findDeclaredSlot(equivalent.get(), a) == nullptr);
}
} // namespace

int main()
{
    reflection::registerBuiltinReflectionTypes();
    storageContract();
    quotaContract();
    sharedLayoutContract();
    std::puts(
        "PASS instance fields, stable references, copy/move, nil, invalidation, allocation-free lookup, quota rollback and zero retained bytes");
}
