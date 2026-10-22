#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"

#include <cassert>
#include <cstdint>

namespace {

class NativeBase {
public:
    OBJECT_RTTI_IMPL(NativeBase);

    void set_value(int value) { value_ = value; }

    int value() const { return value_; }

    int value_ = 0;
};

class NativeDerived final : public NativeBase {
public:
    OBJECT_RTTI_IMPL(NativeDerived);

    void set_derived_value(int value) { derivedValue_ = value; }

    int derived_value() const { return derivedValue_; }

    int derivedValue_ = 0;
};

struct QuotaPayload {
    std::uint64_t words[32]{};
};

void registerTypes()
{
    using namespace arp::reflection;
    registration::class_<NativeBase>("NativeBase")
        .construct<>()
        .method("set_value", &NativeBase::set_value)
        .method("value", &NativeBase::value)
        .property("value_", &NativeBase::value_);
    registration::class_<NativeDerived>("NativeDerived")
        .inherit<NativeBase>()
        .construct<>()
        .method("set_derived_value", &NativeDerived::set_derived_value)
        .method("derived_value", &NativeDerived::derived_value);
}

} // namespace

int main()
{
    using namespace arp::reflection;
    registerTypes();

    NativeDerived value;
    object reference = object::native_reference(&value);
    object copy = reference;

    assert(reference.is_native_reference());
    assert(reference.native_reference_alive());
    assert(reference.class_() == class_of<NativeDerived>());
    assert(reference.typeinfo_() == type_of<NativeDerived*>());
    assert(reference.can_safe_cast<NativeDerived*>());
    assert(reference.can_safe_cast<NativeBase*>());
    assert(reference.cast<NativeBase*>() == static_cast<NativeBase*>(&value));
    assert(reference.cast<const NativeDerived&>().derived_value() == 0);

    reference.call("set_value", 7);
    reference.call("set_derived_value", 11);
    assert(reference["value_"].cast<int>() == 7);
    assert(value.derived_value() == 11);

    reference.invalidate_native_reference();
    assert(!reference.native_reference_alive());
    assert(!copy.native_reference_alive());
    assert(!copy.can_safe_cast<NativeDerived*>());
    assert(copy.cast<NativeDerived*>() == nullptr);
    assert(copy.ptr<NativeDerived>() == nullptr);
    assert(copy.call("set_value", 19).is_undefined());
    assert(copy["value_"].is_undefined());

    // An invalidated control is independent from a new object at the same
    // address; stale references must never resolve to the replacement.
    object replacement = object::native_reference(&value);
    assert(replacement.native_reference_alive());
    assert(!copy.native_reference_alive());
    assert(copy.cast<NativeDerived*>() == nullptr);
    assert(replacement.cast<NativeDerived*>() == &value);
    replacement.invalidate_native_reference();

    // Large reflected values are heap-backed by variant.  Their storage must
    // be charged to the active engine quota and released with the same
    // allocator when the Value is dropped.
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>();
    const auto before = quota->liveBytes();
    {
        arp::core::CpuMemoryQuotaScope scope{quota};
        object payload = QuotaPayload{};
        assert(quota->liveBytes() > before);
        payload = object::undefined();
    }
    assert(quota->liveBytes() == before);
    return 0;
}
