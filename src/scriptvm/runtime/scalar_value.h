#pragma once

#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/value.h"

#include <type_traits>

namespace arp::scriptvm {

// Immutable scalar payload shared by checked semantics and execution slots.
// Reference-bearing values stay in Value and materialize only at explicit edges.
class ScriptScalar final {
public:
    enum class Kind : std::uint8_t { Nil, Bool, Int, Int64, Float };
    ScriptScalar() = default;

    explicit ScriptScalar(bool value) : kind_(Kind::Bool) { payload_.boolean = value; }

    explicit ScriptScalar(int value) : kind_(Kind::Int) { payload_.integer = value; }

    explicit ScriptScalar(std::int64_t value) : kind_(Kind::Int64) { payload_.wide = value; }

    explicit ScriptScalar(double value) : kind_(Kind::Float) { payload_.floating = value; }

    Kind kind() const noexcept { return kind_; }

    bool numeric() const noexcept { return integral() || kind_ == Kind::Float; }

    bool integral() const noexcept { return kind_ == Kind::Int || kind_ == Kind::Int64; }

    std::int64_t integer() const noexcept
    {
        return kind_ == Kind::Int ? payload_.integer : payload_.wide;
    }

    double floating() const noexcept
    {
        return kind_ == Kind::Float ? payload_.floating : double(integer());
    }

    bool boolean() const noexcept { return payload_.boolean; }

    template <class T> const T* ptr() const noexcept
    {
        if constexpr (std::is_same_v<T, int>)
            return kind_ == Kind::Int ? &payload_.integer : nullptr;
        else if constexpr (std::is_same_v<T, std::int64_t>)
            return kind_ == Kind::Int64 ? &payload_.wide : nullptr;
        else if constexpr (std::is_same_v<T, double>)
            return kind_ == Kind::Float ? &payload_.floating : nullptr;
        else if constexpr (std::is_same_v<T, bool>)
            return kind_ == Kind::Bool ? &payload_.boolean : nullptr;
        else
            return nullptr;
    }

    static bool read(const Value& value, ScriptScalar& out)
    {
        // These selectors prove actual owned storage without resolving native
        // controls or registering reflection metadata. A miss must retain the
        // original ordered type queries (including cv and module boundaries).
        if (const auto* p = exactValuePayload<int>(value))
            out = ScriptScalar(*p);
        else if (const auto* p = exactValuePayload<std::int64_t>(value))
            out = ScriptScalar(*p);
        else if (const auto* p = exactValuePayload<double>(value))
            out = ScriptScalar(*p);
        else if (const auto* p = exactValuePayload<float>(value))
            out = ScriptScalar(double(*p));
        else if (const auto* p = exactValuePayload<bool>(value))
            out = ScriptScalar(*p);
        else if (value.is<int>())
            out = ScriptScalar(*value.ptr<int>());
        else if (value.is<std::int64_t>())
            out = ScriptScalar(*value.ptr<std::int64_t>());
        else if (value.is<double>())
            out = ScriptScalar(*value.ptr<double>());
        else if (value.is<float>())
            out = ScriptScalar(double(*value.ptr<float>()));
        else if (value.is<bool>())
            out = ScriptScalar(*value.ptr<bool>());
        else if (isNil(value))
            out = ScriptScalar();
        else
            return false;
        return true;
    }

    // Caller has proved the owning collection cannot escape. Only an exclusive
    // nonnative carrier may change; shared scalar caches keep their normal ABI.
    bool replaceUniqueCarrier(Value& destination) const
    {
        if (kind_ == Kind::Nil || kind_ == Kind::Bool
            || (kind_ == Kind::Int && intValueUsesCache(payload_.integer))
            || !destination.hasUniqueStorage())
            return false;
        switch (kind_) {
        case Kind::Int:
            return replaceOwnedPayload(destination, payload_.integer);
        case Kind::Int64:
            return replaceOwnedPayload(destination, payload_.wide);
        case Kind::Float:
            return replaceOwnedPayload(destination, payload_.floating);
        default:
            return false;
        }
    }

private:
    template <class T> static bool replaceOwnedPayload(Value& destination, T value)
    {
        // The caller has proved exclusive carrier ownership. Exact storage
        // also excludes native references; a selector miss retains the original
        // cv/module/native guard and pointer semantics, without extra allocation.
        const T* actual = checkedOwnedValuePayload<T>(destination);
        if (!actual)
            return false;
        *const_cast<T*>(actual) = value;
        return true;
    }

public:
    Value materialize() const
    {
        switch (kind_) {
        case Kind::Bool:
            return makeBool(payload_.boolean);
        case Kind::Int:
            return makeInt(payload_.integer);
        case Kind::Int64:
            return makeInt64(payload_.wide);
        case Kind::Float:
            return makeFloat(payload_.floating);
        case Kind::Nil:
            return makeNil();
        }
        return makeNil();
    }

private:
    Kind kind_ = Kind::Nil;

    union Payload {
        bool boolean;
        int integer;
        std::int64_t wide;
        double floating;

        Payload() : wide(0) {}
    } payload_;
};

struct ScalarOperationResult {
    ScriptScalar value;
    ScriptDiagnosticCode code = ScriptDiagnosticCode::None;
    const char* message = "";

    bool ok() const noexcept { return code == ScriptDiagnosticCode::None; }
};

} // namespace arp::scriptvm
