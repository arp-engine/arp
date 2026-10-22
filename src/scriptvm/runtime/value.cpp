#include "scriptvm/runtime/value.h"

#include "core/math/checked_integer_cast.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/interpreter/frame.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <exception>
#include <limits>

namespace {

struct IntValueCache {
    static constexpr std::size_t kEntryCount =
        static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u;

    // The VM value cache is thread-local by contract. Avoid an atomic load on
    // every integer literal and arithmetic result in the interpreter.
    std::array<arp::scriptvm::Value*, kEntryCount> entries{};
    arp::core::pmr::Vector<arp::core::UniquePtr<arp::scriptvm::Value>> owners{
        &arp::core::defaultCpuMemoryResource()};

    arp::scriptvm::Value& get(std::size_t index, int value)
    {
        auto* cached = entries[index];
        if (cached == nullptr) {
            auto owner = arp::core::makeUnique<arp::scriptvm::Value>(value);
            cached = owner.get();
            owners.push_back(std::move(owner));
            entries[index] = cached;
        }
        return *cached;
    }

    arp::scriptvm::Value make(int value)
    {
        assert(arp::scriptvm::intValueUsesCache(value));
        const auto index = static_cast<std::size_t>(value);
        // Warm reads copy the real carrier. Cold cache objects remain owned
        // by the thread and retain the original uncharged allocation order.
        if (const auto* cached = entries[index])
            return *cached;
        arp::core::CpuMemoryQuotaScope cacheScope(nullptr);
        return get(index, value);
    }
};

IntValueCache& intValueCache()
{
    thread_local IntValueCache cache;
    return cache;
}

} // namespace

namespace arp::scriptvm {

namespace {
template <class T, class Visitor> bool visitNativeIntegerType(const Value& value, Visitor& visitor)
{
    if (const auto* actual = checkedValuePayload<T>(value)) {
        visitor(*actual);
        return true;
    }
    return false;
}

// Reflected engine APIs preserve their exact C++ integer widths. Decode those
// explicitly, while script int/int64 retain their existing fast paths.
template <class Visitor> bool visitNativeInteger(const Value& value, Visitor visitor)
{
    return visitNativeIntegerType<std::uint64_t>(value, visitor)
        || visitNativeIntegerType<std::uint32_t>(value, visitor)
        || visitNativeIntegerType<std::uint16_t>(value, visitor)
        || visitNativeIntegerType<std::uint8_t>(value, visitor)
        || visitNativeIntegerType<std::int16_t>(value, visitor)
        || visitNativeIntegerType<std::int8_t>(value, visitor);
}
} // namespace

template <class T> const T* ExecutionSlot::boxedScalarPointer() const
{
    if (kind_ != Kind::Boxed)
        return nullptr;
    return checkedValuePayload<T>(storage_.value);
}

template const int* ExecutionSlot::boxedScalarPointer<int>() const;
template const std::int64_t* ExecutionSlot::boxedScalarPointer<std::int64_t>() const;
template const double* ExecutionSlot::boxedScalarPointer<double>() const;
template const bool* ExecutionSlot::boxedScalarPointer<bool>() const;

Value makeInt(int v)
{
    if (intValueUsesCache(v)) {
        return intValueCache().make(v);
    }
    return Value{v};
}

void IntegerValueBuilder::appendReservedRange(
    arp::reflection::hlist& destination, int start, int end)
{
    if (start >= end)
        return;
    assert(static_cast<std::uint64_t>(static_cast<std::int64_t>(end) - start)
        <= destination.capacity() - destination.size());
    // Range bounds determine the representation intervals once. Every element
    // is still constructed in ascending order, reading its current cache Value
    // or taking the original owned allocation path. Cache binding occurs at
    // the first cached element, after the caller's list reserve.
    std::int64_t value = start;
    const auto negativeEnd = std::min<std::int64_t>(end, 0);
    for (; value < negativeEnd; ++value)
        destination.emplace_back(Value{static_cast<int>(value)});
    const auto cachedEnd = std::min<std::int64_t>(end, IntValueCache::kEntryCount);
    if (value < cachedEnd) {
        auto& cache = intValueCache();
        for (; value < cachedEnd; ++value)
            destination.emplace_back(cache.make(static_cast<int>(value)));
    }
    for (; value < end; ++value)
        destination.emplace_back(Value{static_cast<int>(value)});
}

const Value* findCachedIntValue(int v) noexcept
{
    return intValueUsesCache(v) ? intValueCache().entries[static_cast<std::size_t>(v)] : nullptr;
}

IntegerValueCacheBorrow IntegerValueCacheBorrow::current() noexcept
{
    return IntegerValueCacheBorrow(intValueCache().entries.data());
}

Value makeBool(bool v)
{
    static const std::array<Value, 2> values{
        Value{false},
        Value{true},
    };
    return values[v ? 1u : 0u];
}

Value makeString(core::pmr::String s)
{
    return Value{std::move(s)};
}

Value makeString(std::string_view s)
{
    return Value{core::pmr::String(s.data(), s.size(), &core::defaultCpuMemoryResource())};
}

Value makeBytes(core::pmr::Vector<std::uint8_t> bytes)
{
    return Value{ScriptBytes{std::move(bytes)}};
}

Value makeBytes(std::string_view s)
{
    core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
    bytes.reserve(s.size());
    for (const unsigned char value : s)
        bytes.push_back(value);
    return makeBytes(std::move(bytes));
}

bool tryGetInt(const Value& v, int& out)
{
    if (const auto* actual = checkedValuePayload<int>(v)) {
        out = *actual;
        return true;
    }
    bool converted = false;
    if (visitNativeInteger(v, [&](auto number) {
            converted = core::checkedIntegerCast(number, out);
        }))
        return converted;
    try {
        out = v.cast<int>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool tryGetInt64(const Value& v, std::int64_t& out)
{
    if (const auto* actual = checkedValuePayload<std::int64_t>(v)) {
        out = *actual;
        return true;
    }
    bool converted = false;
    if (visitNativeInteger(v, [&](auto number) {
            converted = core::checkedIntegerCast(number, out);
        }))
        return converted;
    try {
        out = v.cast<std::int64_t>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool tryGetFloat(const Value& v, double& out)
{
    if (const auto* actual = checkedValuePayload<double>(v)) {
        out = *actual;
        return true;
    }
    if (const auto* actual = checkedValuePayload<float>(v)) {
        out = static_cast<double>(*actual);
        return true;
    }
    if (visitNativeInteger(v, [&](auto number) {
            out = static_cast<double>(number);
        }))
        return true;
    try {
        out = v.cast<double>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool tryGetBool(const Value& v, bool& out)
{
    if (const auto* actual = checkedValuePayload<bool>(v)) {
        out = *actual;
        return true;
    }
    if (visitNativeInteger(v, [&](auto number) {
            out = number != 0;
        }))
        return true;
    try {
        out = v.cast<bool>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool tryGetString(const Value& v, core::pmr::String& out)
{
    try {
        out = v.cast<core::pmr::String>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool tryGetBytes(const Value& v, core::pmr::Vector<std::uint8_t>& out)
{
    const auto* bytes = asScriptBytes(v);
    if (bytes == nullptr)
        return false;
    out = bytes->data;
    return true;
}

const ScriptBytes* asScriptBytes(const Value& v)
{
    return checkedValuePayload<ScriptBytes>(v);
}

bool isNil(const Value& v)
{
    // Keep the common comparison path free of reflected type lookups.
    return v.is<void>() || v.is<std::nullptr_t>();
}

void registerScriptObjectReflection()
{
    (void)arp::reflection::registration::class_<ScriptAwaitable>("ArpScriptAwaitable")
        .method("ready",
            [](const ScriptAwaitable& value) {
                return value.isReady();
            })
        .method("has_operation",
            [](const ScriptAwaitable& value) {
                return value.hasOperation();
            })
        .method("operation", [](const ScriptAwaitable& value) {
            return value.operationObject();
        });

    (void)arp::reflection::registration::class_<ScriptBytes>("ArpScriptBytes");

    (void)arp::reflection::registration::class_<ScriptGenerator>("ArpScriptGenerator");
    if (const auto* bridge = generatorBridgeV2();
        bridge != nullptr && bridge->registerReflection != nullptr)
        bridge->registerReflection();

    // ScriptCell -- registered so object can carry it by value.
    (void)arp::reflection::registration::class_<ScriptCell>("ArpScriptCell");

    // ScriptClosure -- carries funcIndex + captured cells.
    (void)arp::reflection::registration::class_<ScriptClosure>("ArpScriptClosure");

    // ScriptPointer -- carries a shared_ptr<ScriptCell>.
    (void)arp::reflection::registration::class_<ScriptPointer>("ArpScriptPointer");

    // ScriptMethod -- carries a function index and its bound receiver.
    (void)arp::reflection::registration::class_<ScriptMethod>("ArpScriptMethod");

    (void)arp::reflection::registration::class_<ReflectedMethod>("ArpScriptReflectedMethod");
}

bool isScriptAwaitable(const Value& value)
{
    return exactValuePayload<ScriptAwaitable>(value) != nullptr || value.is<ScriptAwaitable>();
}

const ScriptAwaitable* asScriptAwaitable(const Value& value)
{
    return checkedValuePayload<ScriptAwaitable>(value);
}

bool isScriptGenerator(const Value& value)
{
    if (exactValuePayload<ScriptGenerator>(value) != nullptr)
        return true;
    if (hasExactOwnedCollectionPayload(value)) {
        assert(!value.is<ScriptGenerator>());
        return false;
    }
    return value.is<ScriptGenerator>();
}

ScriptGenerator* asScriptGenerator(const Value& value)
{
    return checkedValuePayload<ScriptGenerator>(value);
}

} // namespace arp::scriptvm
