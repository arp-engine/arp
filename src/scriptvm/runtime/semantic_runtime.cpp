#include "scriptvm/runtime/semantic_runtime.h"

#include "reflection/reflection.h"
#include "scriptvm/runtime/execution_slot.h"
#include "scriptvm/runtime/module_registry.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace arp::scriptvm {

namespace {

SemanticOperationResult failure(ScriptDiagnosticCode code, const char* message)
{
    SemanticOperationResult result;
    result.diagnostic.code = code;
    result.diagnostic.message = message;
    return result;
}

bool isNumeric(const Value& value)
{
    return value.is<int>() || value.is<std::int64_t>() || value.is<float>() || value.is<double>();
}

bool normalizeNumeric(Value& lhs, Value& rhs)
{
    if (!isNumeric(lhs) || !isNumeric(rhs))
        return true;
    if (lhs.is<float>() || lhs.is<double>() || rhs.is<float>() || rhs.is<double>()) {
        double left = 0.0;
        double right = 0.0;
        if (!tryGetFloat(lhs, left) || !tryGetFloat(rhs, right))
            return false;
        if (!lhs.is<double>())
            lhs = makeFloat(left);
        if (!rhs.is<double>())
            rhs = makeFloat(right);
    } else if (lhs.is<std::int64_t>() || rhs.is<std::int64_t>()) {
        std::int64_t left = 0;
        std::int64_t right = 0;
        if (!tryGetInt64(lhs, left) || !tryGetInt64(rhs, right))
            return false;
        if (!lhs.is<std::int64_t>())
            lhs = makeInt64(left);
        if (!rhs.is<std::int64_t>())
            rhs = makeInt64(right);
    }
    return true;
}

bool checkedMultiply(std::int64_t lhs, std::int64_t rhs, std::int64_t& result)
{
    if (lhs == 0 || rhs == 0) {
        result = 0;
        return true;
    }
    if (lhs == -1 && rhs == std::numeric_limits<std::int64_t>::min())
        return false;
    if (rhs == -1 && lhs == std::numeric_limits<std::int64_t>::min())
        return false;
    if (lhs > 0) {
        if (rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() / rhs)
            return false;
        if (rhs < 0 && rhs < std::numeric_limits<std::int64_t>::min() / lhs)
            return false;
    } else if (rhs > 0) {
        if (lhs < std::numeric_limits<std::int64_t>::min() / rhs)
            return false;
    } else if (lhs < std::numeric_limits<std::int64_t>::max() / rhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

bool checkedAdd(std::int64_t lhs, std::int64_t rhs, std::int64_t& result)
{
    if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs)
        || (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs))
        return false;
    result = lhs + rhs;
    return true;
}

bool checkedSubtract(std::int64_t lhs, std::int64_t rhs, std::int64_t& result)
{
    if ((rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs)
        || (rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs))
        return false;
    result = lhs - rhs;
    return true;
}

bool checkedPower(std::int64_t base, std::int64_t exponent, std::int64_t& result)
{
    result = 1;
    std::int64_t factor = base;
    while (exponent > 0) {
        if ((exponent & 1) != 0) {
            if (!checkedMultiply(result, factor, result))
                return false;
        }
        exponent >>= 1;
        if (exponent != 0 && !checkedMultiply(factor, factor, factor))
            return false;
    }
    return true;
}

bool checkedFloorDivide(std::int64_t lhs, std::int64_t rhs, std::int64_t& result)
{
    if (rhs == 0 || (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1))
        return false;
    result = lhs / rhs;
    const auto remainder = lhs % rhs;
    if (remainder != 0 && ((remainder < 0) != (rhs < 0)))
        --result;
    return true;
}

} // namespace

bool ScriptSemanticRuntime::checkedAddInt64(
    std::int64_t lhs, std::int64_t rhs, std::int64_t& result)
{
    return checkedAdd(lhs, rhs, result);
}

bool ScriptSemanticRuntime::checkedSubtractInt64(
    std::int64_t lhs, std::int64_t rhs, std::int64_t& result)
{
    return checkedSubtract(lhs, rhs, result);
}

bool ScriptSemanticRuntime::scalarBinary(SemanticBinaryOperation operation,
    const ScriptScalar& lhs,
    const ScriptScalar& rhs,
    ScalarOperationResult& result)
{
    result = {};
    if (!lhs.numeric() || !rhs.numeric())
        return false;
    auto fail = [&result](ScriptDiagnosticCode code, const char* message) {
        result.code = code;
        result.message = message;
        return true;
    };
    const bool bitwise = operation >= SemanticBinaryOperation::BitAnd
        && operation <= SemanticBinaryOperation::ShiftRight;
    if (bitwise && (!lhs.integral() || !rhs.integral()))
        return fail(ScriptDiagnosticCode::BadCast, "bitwise operand not integer");
    if ((operation == SemanticBinaryOperation::Div || operation == SemanticBinaryOperation::Mod
            || operation == SemanticBinaryOperation::FloorDiv)
        && (rhs.integral() ? rhs.integer() == 0 : rhs.floating() == 0.0))
        return fail(ScriptDiagnosticCode::DivisionByZero, "division or modulo by zero");
    if (!lhs.integral() || !rhs.integral()) {
        const double a = lhs.floating(), b = rhs.floating();
        switch (operation) {
        case SemanticBinaryOperation::Add:
            result = floatBinary<SemanticBinaryOperation::Add>(a, b);
            return true;
        case SemanticBinaryOperation::Sub:
            result = floatBinary<SemanticBinaryOperation::Sub>(a, b);
            return true;
        case SemanticBinaryOperation::Mul:
            result = floatBinary<SemanticBinaryOperation::Mul>(a, b);
            return true;
        case SemanticBinaryOperation::Div:
            result = floatBinary<SemanticBinaryOperation::Div>(a, b);
            return true;
        case SemanticBinaryOperation::Mod:
            result = floatBinary<SemanticBinaryOperation::Mod>(a, b);
            return true;
        case SemanticBinaryOperation::Pow:
            result = floatBinary<SemanticBinaryOperation::Pow>(a, b);
            return true;
        case SemanticBinaryOperation::FloorDiv:
            result = floatBinary<SemanticBinaryOperation::FloorDiv>(a, b);
            return true;
        default:
            return false;
        }
    }

    if (lhs.kind() == ScriptScalar::Kind::Int && rhs.kind() == ScriptScalar::Kind::Int) {
        const int a = static_cast<int>(lhs.integer()), b = static_cast<int>(rhs.integer());
        switch (operation) {
        case SemanticBinaryOperation::Add:
            result = intBinary<SemanticBinaryOperation::Add>(a, b);
            return true;
        case SemanticBinaryOperation::Sub:
            result = intBinary<SemanticBinaryOperation::Sub>(a, b);
            return true;
        case SemanticBinaryOperation::Mul:
            result = intBinary<SemanticBinaryOperation::Mul>(a, b);
            return true;
        case SemanticBinaryOperation::Div:
            result = intBinary<SemanticBinaryOperation::Div>(a, b);
            return true;
        case SemanticBinaryOperation::Mod:
            result = intBinary<SemanticBinaryOperation::Mod>(a, b);
            return true;
        case SemanticBinaryOperation::BitAnd:
            result = intBinary<SemanticBinaryOperation::BitAnd>(a, b);
            return true;
        case SemanticBinaryOperation::BitOr:
            result = intBinary<SemanticBinaryOperation::BitOr>(a, b);
            return true;
        case SemanticBinaryOperation::BitXor:
            result = intBinary<SemanticBinaryOperation::BitXor>(a, b);
            return true;
        case SemanticBinaryOperation::BitClear:
            result = intBinary<SemanticBinaryOperation::BitClear>(a, b);
            return true;
        default:
            break;
        }
    }
    const auto a = lhs.integer(), b = rhs.integer();
    const bool narrow =
        lhs.kind() == ScriptScalar::Kind::Int && rhs.kind() == ScriptScalar::Kind::Int;
    std::int64_t value = 0;
    bool valid = true;
    switch (operation) {
    case SemanticBinaryOperation::Add:
        valid = checkedAdd(a, b, value);
        break;
    case SemanticBinaryOperation::Sub:
        valid = checkedSubtract(a, b, value);
        break;
    case SemanticBinaryOperation::Mul:
        valid = checkedMultiply(a, b, value);
        break;
    case SemanticBinaryOperation::Div:
        valid = !(a == std::numeric_limits<std::int64_t>::min() && b == -1);
        if (valid)
            value = a / b;
        break;
    case SemanticBinaryOperation::Mod:
        value = (a == std::numeric_limits<std::int64_t>::min() && b == -1) ? 0 : a % b;
        break;
    case SemanticBinaryOperation::Pow:
        if (b < 0) {
            result.value = ScriptScalar(std::pow(double(a), double(b)));
            return true;
        }
        valid = checkedPower(a, b, value);
        break;
    case SemanticBinaryOperation::FloorDiv:
        valid = checkedFloorDivide(a, b, value);
        break;
    case SemanticBinaryOperation::BitAnd:
        value = a & b;
        break;
    case SemanticBinaryOperation::BitOr:
        value = a | b;
        break;
    case SemanticBinaryOperation::BitXor:
        value = a ^ b;
        break;
    case SemanticBinaryOperation::BitClear:
        value = a & ~b;
        break;
    case SemanticBinaryOperation::ShiftLeft:
    case SemanticBinaryOperation::ShiftRight: {
        const int width = narrow ? std::numeric_limits<unsigned int>::digits
                                 : std::numeric_limits<std::uint64_t>::digits;
        if (b < 0 || b >= width)
            return fail(ScriptDiagnosticCode::IntegerOverflow, "shift count exceeds scalar width");
        if (operation == SemanticBinaryOperation::ShiftLeft) {
            value = a;
            for (std::int64_t bit = 0; bit < b && valid; ++bit)
                valid = checkedMultiply(value, 2, value);
        } else {
            std::uint64_t bits = static_cast<std::uint64_t>(a);
            bits >>= static_cast<unsigned>(b);
            if (a < 0 && b != 0)
                bits |= (~std::uint64_t(0)) << (64 - static_cast<unsigned>(b));
            std::memcpy(&value, &bits, sizeof(value));
        }
        break;
    }
    }
    if (!valid
        || (narrow
            && (value < std::numeric_limits<int>::min()
                || value > std::numeric_limits<int>::max())))
        return fail(ScriptDiagnosticCode::IntegerOverflow, "integer arithmetic overflow");
    result.value = narrow ? ScriptScalar(static_cast<int>(value)) : ScriptScalar(value);
    return true;
}

namespace {
template <class T> bool compareScalarPayload(SemanticCompareOperation operation, T lhs, T rhs)
{
    switch (operation) {
    case SemanticCompareOperation::Equal:
        return ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::Equal>(lhs, rhs);
    case SemanticCompareOperation::NotEqual:
        return ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::NotEqual>(lhs, rhs);
    case SemanticCompareOperation::Less:
        return ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::Less>(lhs, rhs);
    case SemanticCompareOperation::LessEqual:
        return ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::LessEqual>(lhs, rhs);
    case SemanticCompareOperation::Greater:
        return ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::Greater>(lhs, rhs);
    case SemanticCompareOperation::GreaterEqual:
        return ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::GreaterEqual>(
            lhs, rhs);
    }
    return false;
}
} // namespace

bool ScriptSemanticRuntime::scalarCompare(SemanticCompareOperation operation,
    const ScriptScalar& lhs,
    const ScriptScalar& rhs,
    bool& result)
{
    switch (operation) {
    case SemanticCompareOperation::Equal:
        return scalarCompare<SemanticCompareOperation::Equal>(lhs, rhs, result);
    case SemanticCompareOperation::NotEqual:
        return scalarCompare<SemanticCompareOperation::NotEqual>(lhs, rhs, result);
    case SemanticCompareOperation::Less:
        return scalarCompare<SemanticCompareOperation::Less>(lhs, rhs, result);
    case SemanticCompareOperation::LessEqual:
        return scalarCompare<SemanticCompareOperation::LessEqual>(lhs, rhs, result);
    case SemanticCompareOperation::Greater:
        return scalarCompare<SemanticCompareOperation::Greater>(lhs, rhs, result);
    case SemanticCompareOperation::GreaterEqual:
        return scalarCompare<SemanticCompareOperation::GreaterEqual>(lhs, rhs, result);
    }
    return false;
}

SemanticOperationResult ScriptSemanticRuntime::normalizeArgument(
    Value argument, RuntimeType expected, bool nullable, const core::pmr::String& className)
{
    auto success = [](Value value) {
        SemanticOperationResult result;
        result.value = std::move(value);
        return result;
    };
    if (isNil(argument)) {
        if (nullable || expected == RuntimeType::Any)
            return success(std::move(argument));
    }
    switch (expected) {
    case RuntimeType::Any:
        return success(std::move(argument));
    case RuntimeType::Int:
        if (argument.is<int>())
            return success(std::move(argument));
        break;
    case RuntimeType::Int64:
        if (argument.is<std::int64_t>())
            return success(std::move(argument));
        if (argument.is<int>())
            return success(makeInt64(*argument.ptr<int>()));
        break;
    case RuntimeType::Float:
        if (argument.is<double>())
            return success(std::move(argument));
        if (argument.is<int>())
            return success(makeFloat(*argument.ptr<int>()));
        if (argument.is<std::int64_t>())
            return success(makeFloat(*argument.ptr<std::int64_t>()));
        break;
    case RuntimeType::Bool:
        if (argument.is<bool>())
            return success(std::move(argument));
        break;
    case RuntimeType::String:
        if (argument.is<core::pmr::String>())
            return success(std::move(argument));
        break;
    case RuntimeType::Bytes:
        if (argument.is<ScriptBytes>())
            return success(std::move(argument));
        break;
    case RuntimeType::Object:
        if (argument.is_list() || argument.is_dict() || argument.is_object()
            || argument.is_function() || argument.is_class() || argument.is_property())
            return success(std::move(argument));
        break;
    case RuntimeType::List:
        if (argument.is_list())
            return success(std::move(argument));
        break;
    case RuntimeType::Dict:
        if (argument.is_dict())
            return success(std::move(argument));
        break;
    case RuntimeType::Function:
        if (argument.is_function() || argument.is<ScriptClosure>() || argument.is<HostCallable>()
            || argument.is<ScriptMethod>() || argument.is<ReflectedMethod>())
            return success(std::move(argument));
        break;
    case RuntimeType::Class:
        if (argument.is_object() && argument.class_() != nullptr
            && (className.empty() || argument.class_()->name() == className))
            return success(std::move(argument));
        break;
    }
    return failure(
        ScriptDiagnosticCode::TypeMismatch, "argument does not match the function parameter type");
}

bool ScriptSemanticRuntime::compareStrings(
    SemanticCompareOperation operation, std::string_view lhs, std::string_view rhs)
{
    return compareScalarPayload(operation, lhs, rhs);
}

SemanticOperationResult ScriptSemanticRuntime::concatenateStrings(
    std::string_view lhs, std::string_view rhs)
{
    core::pmr::String concatenated{&core::defaultCpuMemoryResource()};
    concatenated.reserve(lhs.size() + rhs.size());
    concatenated.append(lhs.data(), lhs.size());
    concatenated.append(rhs.data(), rhs.size());
    SemanticOperationResult result;
    result.value = makeString(std::move(concatenated));
    return result;
}

ScriptSemanticRuntime::UniqueStringAccess ScriptSemanticRuntime::UniqueStringAccess::borrow(
    ExecutionSlot& local)
{
    auto* value = local.uniqueBoxed();
    return UniqueStringAccess(
        value ? checkedOwnedValuePayload<core::pmr::String>(*value) : nullptr);
}

bool ScriptSemanticRuntime::UniqueStringAccess::append(std::string_view suffix) const
{
    assert(text_ != nullptr);
    auto& text = *text_;
    if (suffix.size() > text.max_size() - text.size())
        return false;
    // basic_string::append has the strong exception guarantee. Its PMR resource
    // still enforces the current quota, including allocation during growth.
    text.append(suffix.data(), suffix.size());
    return true;
}

bool ScriptSemanticRuntime::UniqueStringAccess::assign(
    std::string_view prefix, std::string_view suffix) const
{
    assert(text_ != nullptr);
    auto& text = *text_;
    if (prefix.size() > text.max_size() || suffix.size() > text.max_size() - prefix.size())
        return false;
    const auto size = prefix.size() + suffix.size();
    // Reserve before changing bytes: growth failure preserves the previous value.
    // Independent sources and sufficient capacity make both writes nonallocating.
    if (size > text.capacity())
        text.reserve(size);
    text.assign(prefix.data(), prefix.size());
    text.append(suffix.data(), suffix.size());
    return true;
}

bool ScriptSemanticRuntime::appendUniqueString(ExecutionSlot& local, std::string_view suffix)
{
    auto access = UniqueStringAccess::borrow(local);
    return access && access.append(suffix);
}

bool ScriptSemanticRuntime::assignUniqueString(
    ExecutionSlot& local, std::string_view prefix, std::string_view suffix)
{
    auto access = UniqueStringAccess::borrow(local);
    return access && access.assign(prefix, suffix);
}

SemanticOperationResult ScriptSemanticRuntime::binary(
    SemanticBinaryOperation operation, Value lhs, Value rhs)
{
    ScriptScalar left, right;
    ScalarOperationResult scalar;
    if (ScriptScalar::read(lhs, left) && ScriptScalar::read(rhs, right)
        && scalarBinary(operation, left, right, scalar)) {
        if (!scalar.ok())
            return failure(scalar.code, scalar.message);
        SemanticOperationResult result;
        result.value = scalar.value.materialize();
        return result;
    }
    if ((operation == SemanticBinaryOperation::Pow
            || operation == SemanticBinaryOperation::FloorDiv)
        && (!isNumeric(lhs) || !isNumeric(rhs)))
        return failure(
            ScriptDiagnosticCode::BadCast, "power and floor division require numeric operands");
    if (operation == SemanticBinaryOperation::Add && lhs.is<core::pmr::String>()
        && rhs.is<core::pmr::String>()) {
        return concatenateStrings(*lhs.ptr<core::pmr::String>(), *rhs.ptr<core::pmr::String>());
    }
    Value value = Value::undefined();
    switch (operation) {
    case SemanticBinaryOperation::Add:
        value = lhs + rhs;
        break;
    case SemanticBinaryOperation::Sub:
        value = lhs - rhs;
        break;
    case SemanticBinaryOperation::Mul:
        value = lhs * rhs;
        break;
    case SemanticBinaryOperation::Div:
        value = lhs / rhs;
        break;
    case SemanticBinaryOperation::Mod:
        value = lhs % rhs;
        break;
    case SemanticBinaryOperation::BitAnd:
        value = lhs & rhs;
        break;
    case SemanticBinaryOperation::BitOr:
        value = lhs | rhs;
        break;
    case SemanticBinaryOperation::BitXor:
        value = lhs ^ rhs;
        break;
    case SemanticBinaryOperation::BitClear:
        value = lhs.call("__andnot__", rhs);
        break;
    case SemanticBinaryOperation::ShiftLeft:
        value = lhs.call("__shl__", rhs);
        break;
    case SemanticBinaryOperation::ShiftRight:
        value = lhs.call("__shr__", rhs);
        break;
    case SemanticBinaryOperation::Pow:
    case SemanticBinaryOperation::FloorDiv:
        return failure(ScriptDiagnosticCode::BadCast, "numeric scalar operation unavailable");
    }
    if (value.is_undefined())
        return failure(ScriptDiagnosticCode::BadCast, "semantic binary operation unavailable");
    SemanticOperationResult result;
    result.value = std::move(value);
    return result;
}

SemanticOperationResult ScriptSemanticRuntime::compare(
    SemanticCompareOperation operation, Value lhs, Value rhs)
{
    ScriptScalar left, right;
    bool value = false;
    if (ScriptScalar::read(lhs, left) && ScriptScalar::read(rhs, right)
        && scalarCompare(operation, left, right, value)) {
        SemanticOperationResult result;
        result.value = makeBool(value);
        return result;
    }
    if (isNil(lhs) || isNil(rhs)) {
        const bool equal = isNil(lhs) && isNil(rhs);
        const bool result = operation == SemanticCompareOperation::Equal ? equal
            : operation == SemanticCompareOperation::NotEqual            ? !equal
                                                                         : false;
        SemanticOperationResult success;
        success.value = makeBool(result);
        return success;
    }
    if (!normalizeNumeric(lhs, rhs))
        return failure(ScriptDiagnosticCode::BadCast, "numeric comparison conversion failed");
    bool result = false;
    // Numeric ordering must use the semantic scalar payload directly. Going
    // through reflection::object::operator< makes comparison depend on the
    // reflected backend's overload lookup and can diverge on Wasm.
    if (lhs.is<int>() && rhs.is<int>()) {
        const auto left = *lhs.ptr<int>();
        const auto right = *rhs.ptr<int>();
        switch (operation) {
        case SemanticCompareOperation::Equal:
            result = left == right;
            break;
        case SemanticCompareOperation::NotEqual:
            result = left != right;
            break;
        case SemanticCompareOperation::Less:
            result = left < right;
            break;
        case SemanticCompareOperation::LessEqual:
            result = left <= right;
            break;
        case SemanticCompareOperation::Greater:
            result = left > right;
            break;
        case SemanticCompareOperation::GreaterEqual:
            result = left >= right;
            break;
        }
        SemanticOperationResult success;
        success.value = makeBool(result);
        return success;
    }
    if (lhs.is<std::int64_t>() && rhs.is<std::int64_t>()) {
        const auto left = *lhs.ptr<std::int64_t>();
        const auto right = *rhs.ptr<std::int64_t>();
        switch (operation) {
        case SemanticCompareOperation::Equal:
            result = left == right;
            break;
        case SemanticCompareOperation::NotEqual:
            result = left != right;
            break;
        case SemanticCompareOperation::Less:
            result = left < right;
            break;
        case SemanticCompareOperation::LessEqual:
            result = left <= right;
            break;
        case SemanticCompareOperation::Greater:
            result = left > right;
            break;
        case SemanticCompareOperation::GreaterEqual:
            result = left >= right;
            break;
        }
        SemanticOperationResult success;
        success.value = makeBool(result);
        return success;
    }
    if (lhs.is<double>() && rhs.is<double>()) {
        const auto left = *lhs.ptr<double>();
        const auto right = *rhs.ptr<double>();
        switch (operation) {
        case SemanticCompareOperation::Equal:
            result = left == right;
            break;
        case SemanticCompareOperation::NotEqual:
            result = left != right;
            break;
        case SemanticCompareOperation::Less:
            result = left < right;
            break;
        case SemanticCompareOperation::LessEqual:
            result = left <= right;
            break;
        case SemanticCompareOperation::Greater:
            result = left > right;
            break;
        case SemanticCompareOperation::GreaterEqual:
            result = left >= right;
            break;
        }
        SemanticOperationResult success;
        success.value = makeBool(result);
        return success;
    }
    if (lhs.is<core::pmr::String>() && rhs.is<core::pmr::String>()) {
        const auto& left = *lhs.ptr<core::pmr::String>();
        const auto& right = *rhs.ptr<core::pmr::String>();
        result = compareStrings(operation, left, right);
        SemanticOperationResult success;
        success.value = makeBool(result);
        return success;
    }
    if (lhs.is<bool>() && rhs.is<bool>()) {
        const auto left = *lhs.ptr<bool>();
        const auto right = *rhs.ptr<bool>();
        switch (operation) {
        case SemanticCompareOperation::Equal:
            result = left == right;
            break;
        case SemanticCompareOperation::NotEqual:
            result = left != right;
            break;
        case SemanticCompareOperation::Less:
            result = left < right;
            break;
        case SemanticCompareOperation::LessEqual:
            result = left <= right;
            break;
        case SemanticCompareOperation::Greater:
            result = left > right;
            break;
        case SemanticCompareOperation::GreaterEqual:
            result = left >= right;
            break;
        }
        SemanticOperationResult success;
        success.value = makeBool(result);
        return success;
    }
    switch (operation) {
    case SemanticCompareOperation::Equal:
        result = lhs == rhs;
        break;
    case SemanticCompareOperation::NotEqual:
        result = lhs != rhs;
        break;
    case SemanticCompareOperation::Less:
        result = lhs < rhs;
        break;
    case SemanticCompareOperation::LessEqual:
        result = lhs <= rhs;
        break;
    case SemanticCompareOperation::Greater:
        result = lhs > rhs;
        break;
    case SemanticCompareOperation::GreaterEqual:
        result = lhs >= rhs;
        break;
    }
    SemanticOperationResult success;
    success.value = makeBool(result);
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::negate(Value value)
{
    if (value.is<int>()) {
        SemanticOperationResult success;
        success.value = makeInt(-value.cast<int>());
        return success;
    }
    if (value.is<std::int64_t>()) {
        SemanticOperationResult success;
        success.value = makeInt64(-value.cast<std::int64_t>());
        return success;
    }
    if (value.is<float>()) {
        SemanticOperationResult success;
        success.value = Value{-value.cast<float>()};
        return success;
    }
    if (value.is<double>()) {
        SemanticOperationResult success;
        success.value = makeFloat(-value.cast<double>());
        return success;
    }
    Value result = -value;
    if (result.is_undefined())
        return failure(ScriptDiagnosticCode::BadCast, "semantic negation unavailable");
    SemanticOperationResult success;
    success.value = std::move(result);
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::logicalNot(Value value)
{
    bool boolean = false;
    if (!tryGetBool(value, boolean))
        return failure(ScriptDiagnosticCode::BadCast, "expected bool operand");
    SemanticOperationResult success;
    success.value = makeBool(!boolean);
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::convert(
    SemanticConversionOperation operation, Value value)
{
    switch (operation) {
    case SemanticConversionOperation::Int: {
        int result = 0;
        if (!tryGetInt(value, result))
            return failure(ScriptDiagnosticCode::BadCast, "int conversion failed");
        SemanticOperationResult success;
        success.value = makeInt(result);
        return success;
    }
    case SemanticConversionOperation::Int64: {
        std::int64_t result = 0;
        if (!tryGetInt64(value, result))
            return failure(ScriptDiagnosticCode::BadCast, "int64 conversion failed");
        SemanticOperationResult success;
        success.value = makeInt64(result);
        return success;
    }
    case SemanticConversionOperation::Float: {
        double result = 0.0;
        if (!tryGetFloat(value, result))
            return failure(ScriptDiagnosticCode::BadCast, "float conversion failed");
        SemanticOperationResult success;
        success.value = makeFloat(result);
        return success;
    }
    case SemanticConversionOperation::String: {
        core::pmr::String result{&core::defaultCpuMemoryResource()};
        if (!tryGetString(value, result))
            return failure(ScriptDiagnosticCode::BadCast, "string conversion failed");
        SemanticOperationResult success;
        success.value = makeString(std::move(result));
        return success;
    }
    case SemanticConversionOperation::Bool: {
        bool result = false;
        if (isNil(value))
            result = false;
        else if (!tryGetBool(value, result))
            return failure(ScriptDiagnosticCode::BadCast, "bool conversion failed");
        SemanticOperationResult success;
        success.value = makeBool(result);
        return success;
    }
    case SemanticConversionOperation::Bytes:
        if (value.is<ScriptBytes>()) {
            SemanticOperationResult success;
            success.value = std::move(value);
            return success;
        }
        if (value.is<core::pmr::String>()) {
            SemanticOperationResult success;
            success.value = makeBytes(*value.ptr<core::pmr::String>());
            return success;
        }
        return failure(ScriptDiagnosticCode::BadCast, "bytes conversion requires string or bytes");
    }
    return failure(ScriptDiagnosticCode::BadCast, "conversion is unavailable");
}

ScalarOperationResult ScriptSemanticRuntime::lengthScalar(const ExecutionSlot& value)
{
    int result = -1;
    std::string_view text;
    if (value.stringView(text))
        result = static_cast<int>(text.size());
    else if (value.is<arp::reflection::hlist>())
        result = static_cast<int>(value.ptr<arp::reflection::hlist>()->size());
    else if (value.is<arp::reflection::hdict>())
        result = static_cast<int>(value.ptr<arp::reflection::hdict>()->size());
    else if (value.is<ScriptBytes>())
        result = static_cast<int>(value.ptr<ScriptBytes>()->data.size());
    ScalarOperationResult success;
    if (result < 0) {
        success.code = ScriptDiagnosticCode::BadCast;
        success.message = "length operand is invalid";
    } else
        success.value = ScriptScalar(result);
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::length(Value value)
{
    const auto scalar = lengthScalar(ExecutionSlot(std::move(value)));
    if (!scalar.ok())
        return failure(scalar.code, scalar.message);
    SemanticOperationResult success;
    success.value = scalar.value.materialize();
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::append(
    Value list, Value value, std::size_t maxContainerValues)
{
    if (!list.is<arp::reflection::hlist>())
        return failure(ScriptDiagnosticCode::BadCast, "append() requires a list");
    auto* values = list.ptr<arp::reflection::hlist>();
    if (values->size() >= maxContainerValues) {
        return failure(
            ScriptDiagnosticCode::ResourceLimitExceeded, "append exceeds the container limit");
    }
    values->push_back(std::move(value));
    SemanticOperationResult success;
    success.value = std::move(list);
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::erase(Value dictionary, Value key)
{
    if (!dictionary.is<arp::reflection::hdict>())
        return failure(ScriptDiagnosticCode::BadCast, "delete() requires a dict");
    auto& values = *dictionary.ptr<arp::reflection::hdict>();
    const auto found = values.find(key);
    if (found != values.end())
        values.erase(found);
    SemanticOperationResult success;
    success.value = makeNil();
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::clear(Value container)
{
    if (container.is<arp::reflection::hlist>()) {
        container.ptr<arp::reflection::hlist>()->clear();
    } else if (container.is<arp::reflection::hdict>()) {
        container.ptr<arp::reflection::hdict>()->clear();
    } else {
        return failure(ScriptDiagnosticCode::BadCast, "clear() requires list or dict");
    }
    SemanticOperationResult success;
    success.value = makeNil();
    return success;
}

namespace {

class ActualIntegerPayload final {
public:
    static const int* qualify(const Value& value)
    {
        const auto* pointer = exactValuePayload<int>(value);
        assert(!pointer || (value.is<int>() && !value.is_native_reference()));
        return pointer;
    }

    static const int* checkedPointer(const Value& value)
    {
        if (const auto* pointer = qualify(value))
            return pointer;
        if (!value.is<int>() || value.is_native_reference())
            return nullptr;
        return readPointer(value);
    }

    static int read(const Value& value) { return *readPointer(value); }

private:
    static const int* readPointer(const Value& value)
    {
        // The caller has validated the actual carrier in this instruction;
        // reading it cannot invoke a host callback or invalidate that proof.
        // Reuse the existing direct variant access rather than re-entering
        // object::ptr's native-control lookup after a nonnative check.
        assert(value.is<int>() && !value.is_native_reference());
        using SharedVariant = arp::reflection::detail::ObjectRef<arp::reflection::variant>;
        return value.cast<const SharedVariant&>()->cast<int>();
    }
};

// One construction contract for boxed AOT stacks and inline Interpreter stacks.
// Each popped operand crosses the Value ABI exactly once, without a scratch vector.
class ContainerConstruction final {
    static Value consume(Value& value) { return std::move(value); }

    static Value consume(ExecutionSlot& slot) { return slot.takeValue(); }

    template <bool CacheBorrowed>
    static Value consume(ExecutionSlot& slot, const IntegerValueCacheBorrow* cache)
    {
        if constexpr (CacheBorrowed) {
            if (const auto* scalar = slot.inlineScalar())
                if (const auto* integer = scalar->ptr<int>();
                    integer && intValueUsesCache(*integer))
                    return cache->makeCached(*integer);
        }
        return consume(slot);
    }

    template <class T> static T* ownedCollection(Value& value)
    {
        return checkedOwnedValuePayload<T>(value);
    }

public:
    template <class Operands>
    static SemanticOperationResult list(
        Operands& operands, int count, std::size_t maxContainerValues)
    {
        if (count < 0 || static_cast<std::size_t>(count) > maxContainerValues)
            return failure(
                ScriptDiagnosticCode::ResourceLimitExceeded, "list exceeds the container limit");
        if (operands.size() < static_cast<std::size_t>(count))
            return failure(
                ScriptDiagnosticCode::StackUnderflow, "list construction stack underflow");
        arp::reflection::hlist list(
            (arp::reflection::hlist::allocator_type(&core::defaultCpuMemoryResource())));
        list.reserve(static_cast<std::size_t>(count));
        for (int index = count - 1; index >= 0; --index) {
            list.emplace_back(consume(operands.back()));
            operands.pop_back();
        }
        std::reverse(list.begin(), list.end());
        SemanticOperationResult success;
        success.value = Value{std::move(list)};
        return success;
    }

    static bool reuseList(
        ExecutionSlot& local, ExecutionSlotBuffer& operands, int count, std::size_t limit)
    {
        if (count < 0 || static_cast<std::size_t>(count) > limit
            || operands.size() < static_cast<std::size_t>(count))
            return false;
        auto* value = local.uniqueBoxed();
        if (!value)
            return false;
        auto* collection = ownedCollection<arp::reflection::hlist>(*value);
        if (!collection)
            return false;
        auto& list = *collection;
        list.resize(static_cast<std::size_t>(count));
        commitList(list, operands, count);
        return true;
    }

    template <bool CacheBorrowed = false>
    static void commitList(arp::reflection::hlist& list,
        ExecutionSlotBuffer& operands,
        int count,
        const IntegerValueCacheBorrow* cache = nullptr)
    {
        for (int index = count - 1; index >= 0; --index) {
            const auto* scalar = operands.back().inlineScalar();
            if (!scalar || !scalar->replaceUniqueCarrier(list[static_cast<std::size_t>(index)]))
                list[static_cast<std::size_t>(index)] =
                    consume<CacheBorrowed>(operands.back(), cache);
            operands.pop_back();
        }
    }

    static bool reuseDictionary(ExecutionSlot& local,
        ExecutionSlotBuffer& operands,
        int pairCount,
        std::size_t limit,
        PrivateDictionaryWriteScratch& scratch)
    {
        if (!prepareDictionary(local, operands, pairCount, limit, scratch))
            return false;
        commitDictionary(
            operands, scratch, operands.size() - static_cast<std::size_t>(pairCount) * 2u);
        scratch.clear();
        return true;
    }

    static bool prepareDictionary(ExecutionSlot& local,
        ExecutionSlotBuffer& operands,
        int pairCount,
        std::size_t limit,
        PrivateDictionaryWriteScratch& scratch)
    {
        scratch.clear();
        (void)arp::reflection::db::instance();
        constexpr auto unassigned = std::numeric_limits<std::uint32_t>::max();
        if (pairCount < 0 || static_cast<std::size_t>(pairCount) > limit
            || operands.size() < static_cast<std::size_t>(pairCount) * 2u
            || operands.size() > unassigned
            || !arp::reflection::type_of<int>()->class_()->hasBuiltinOrdering())
            return false;
        auto* value = local.uniqueBoxed();
        if (!value)
            return false;
        auto* collection = ownedCollection<arp::reflection::hdict>(*value);
        if (!collection)
            return false;
        auto& dictionary = *collection;
        if (dictionary.size() != static_cast<std::size_t>(pairCount))
            return false;
        for (const auto& [key, mapped] : dictionary)
            if (!ActualIntegerPayload::checkedPointer(key))
                return false;
        const auto start = operands.size() - static_cast<std::size_t>(pairCount) * 2u;
        scratch.reserve(static_cast<std::size_t>(pairCount));
        // Stock ordering and exact actual integer keys permit a transient sorted
        // projection of the original tree nodes. Verify monotonic payloads too:
        // cached keys can have mutable host aliases, even in an exclusive map.
        for (auto& [key, mapped] : dictionary) {
            const int payload = ActualIntegerPayload::read(key);
            if (!scratch.empty() && scratch.back().key >= payload) {
                scratch.clear();
                return false;
            }
            scratch.push_back({&mapped, unassigned, payload});
        }
        for (int pair = 0; pair < pairCount; ++pair) {
            const auto keyOffset = start + static_cast<std::size_t>(pair) * 2u;
            auto& keySlot = operands[keyOffset];
            // Only the stock integer cache can materialize keys here without
            // changing invocation-quota order. Boxed, native or other keys replay.
            const auto* scalar = keySlot.inlineScalar();
            const auto* integer = scalar ? scalar->ptr<int>() : nullptr;
            if (!integer || !intValueUsesCache(*integer)) {
                scratch.clear();
                return false;
            }
            // Materialize the same cached carrier and read its actual payload,
            // rather than trusting the LoadInt operand or a declared key type.
            const Value& materialized = keySlot.boxed();
            const auto* pointer = ActualIntegerPayload::checkedPointer(materialized);
            if (!pointer) {
                scratch.clear();
                return false;
            }
            const int payload = *pointer;
            const auto found = std::lower_bound(
                scratch.begin(), scratch.end(), payload, [](const auto& target, int key) {
                    return target.key < key;
                });
            if (found == scratch.end() || found->key != payload || found->operand != unassigned) {
                scratch.clear();
                return false;
            }
            found->operand = static_cast<std::uint32_t>(keyOffset + 1u);
        }
        std::sort(scratch.begin(), scratch.end(), [](const auto& left, const auto& right) {
            return std::less<Value*>()(left.destination, right.destination);
        });
        return true;
    }

    template <bool CacheBorrowed = false>
    static void commitDictionary(ExecutionSlotBuffer& operands,
        const PrivateDictionaryWriteScratch& scratch,
        std::size_t start,
        const IntegerValueCacheBorrow* cache = nullptr)
    {
        // Equal cardinality plus distinct existing keys proves an unchanged key
        // set. Keep the real tree and normal Value aliases, replacing only values.
        for (const auto& target : scratch) {
            auto& source = operands[target.operand];
            const auto* scalar = source.inlineScalar();
            if (!scalar || !scalar->replaceUniqueCarrier(*target.destination))
                *target.destination = consume<CacheBorrowed>(source, cache);
        }
        operands.resize(start);
    }

    template <class Operands>
    static SemanticOperationResult dict(
        Operands& operands, int pairCount, std::size_t maxContainerValues)
    {
        if (pairCount < 0 || static_cast<std::size_t>(pairCount) > maxContainerValues)
            return failure(
                ScriptDiagnosticCode::ResourceLimitExceeded, "dict exceeds the container limit");
        const auto required = static_cast<std::size_t>(pairCount) * 2u;
        if (operands.size() < required)
            return failure(
                ScriptDiagnosticCode::StackUnderflow, "dict construction stack underflow");
        arp::reflection::hdict dict(
            (arp::reflection::hdict::allocator_type(&core::defaultCpuMemoryResource())));
        for (int index = 0; index < pairCount; ++index) {
            Value value = consume(operands.back());
            operands.pop_back();
            Value key = consume(operands.back());
            operands.pop_back();
            // Keep reverse consumption/duplicate-key precedence, avoiding the
            // otherwise allocated undefined Value from map::operator[].
            dict.insert_or_assign(std::move(key), std::move(value));
        }
        SemanticOperationResult success;
        success.value = Value{std::move(dict)};
        return success;
    }
};
} // namespace

ScriptSemanticRuntime::PrivateConstructionBorrow::~PrivateConstructionBorrow()
{
    if (targets_)
        targets_->clear();
}

bool ScriptSemanticRuntime::PrivateConstructionBorrow::bind(ExecutionSlot& local,
    ExecutionSlotBuffer& operands,
    int count,
    std::size_t limit,
    bool dictionary,
    PrivateDictionaryWriteScratch& scratch)
{
    assert(!list_ && !targets_);
    if (core::currentCpuMemoryQuota() || count < 0 || static_cast<std::size_t>(count) > limit)
        return false;
    const auto required = static_cast<std::size_t>(count) * (dictionary ? 2u : 1u);
    if (operands.size() < required)
        return false;
    auto* owner = local.uniqueBoxed();
    if (!owner)
        return false;
    // Releasing an old mapped native/object payload could invoke host code.
    // Exact nonnative integers have no such destructor or structure mutation.
    const auto integer = [](const Value& value) {
        return checkedOwnedValuePayload<int>(value) != nullptr;
    };
    if (dictionary) {
        const auto* values = checkedOwnedValuePayload<arp::reflection::hdict>(*owner);
        if (!values || !std::all_of(values->begin(), values->end(), [&](const auto& item) {
                return integer(item.second);
            }))
            return false;
        if (!ContainerConstruction::prepareDictionary(local, operands, count, limit, scratch))
            return false;
        targets_ = &scratch;
    } else {
        const auto* values = checkedOwnedValuePayload<arp::reflection::hlist>(*owner);
        if (!values || values->size() != static_cast<std::size_t>(count)
            || !std::all_of(values->begin(), values->end(), integer))
            return false;
        list_ = const_cast<arp::reflection::hlist*>(values);
    }
    count_ = count;
    start_ = operands.size() - required;
    integers_ = IntegerValueCacheBorrow::current();
    return true;
}

bool ScriptSemanticRuntime::PrivateConstructionBorrow::commit(ExecutionSlotBuffer& operands) const
{
    assert(list_ || targets_);
    const auto required = static_cast<std::size_t>(count_) * (targets_ ? 2u : 1u);
    if (operands.size() != start_ + required)
        return false;
    for (int i = 0; i != count_; ++i) {
        const auto source =
            start_ + static_cast<std::size_t>(i) * (targets_ ? 2u : 1u) + (targets_ ? 1u : 0u);
        if (!operands[source].scalarPointer<int>())
            return false;
    }
    // Actual key carriers and payloads remain observable. The closed interval
    // fixes only their relation to real mapped nodes, not scalar contents.
    if (targets_)
        for (const auto& target : *targets_) {
            const auto* key = ActualIntegerPayload::checkedPointer(
                operands[target.operand - 1u].boxed(integers_));
            if (!key || *key != target.key)
                return false;
        }
    if (targets_)
        ContainerConstruction::commitDictionary<true>(operands, *targets_, start_, &integers_);
    else
        ContainerConstruction::commitList<true>(*list_, operands, count_, &integers_);
    return true;
}

bool ScriptSemanticRuntime::reuseUniqueList(
    ExecutionSlot& local, ExecutionSlotBuffer& operands, int count, std::size_t limit)
{
    return ContainerConstruction::reuseList(local, operands, count, limit);
}

bool ScriptSemanticRuntime::reuseUniqueDictionary(ExecutionSlot& local,
    ExecutionSlotBuffer& operands,
    int pairCount,
    std::size_t limit,
    PrivateDictionaryWriteScratch& scratch)
{
    return ContainerConstruction::reuseDictionary(local, operands, pairCount, limit, scratch);
}

SemanticOperationResult ScriptSemanticRuntime::makeList(
    core::pmr::Vector<Value>& operands, int count, std::size_t limit)
{
    return ContainerConstruction::list(operands, count, limit);
}

SemanticOperationResult ScriptSemanticRuntime::makeDict(
    core::pmr::Vector<Value>& operands, int count, std::size_t limit)
{
    return ContainerConstruction::dict(operands, count, limit);
}

SemanticOperationResult ScriptSemanticRuntime::makeList(
    ExecutionSlotBuffer& operands, int count, std::size_t limit)
{
    return ContainerConstruction::list(operands, count, limit);
}

SemanticOperationResult ScriptSemanticRuntime::makeDict(
    ExecutionSlotBuffer& operands, int count, std::size_t limit)
{
    return ContainerConstruction::dict(operands, count, limit);
}

SemanticOperationResult ScriptSemanticRuntime::makeRange(
    int start, int end, std::size_t maxContainerValues)
{
    const auto count = static_cast<std::int64_t>(end) - static_cast<std::int64_t>(start);
    if (count > static_cast<std::int64_t>(maxContainerValues))
        return failure(
            ScriptDiagnosticCode::ResourceLimitExceeded, "range exceeds the container limit");
    arp::reflection::hlist range(
        (arp::reflection::hlist::allocator_type(&core::defaultCpuMemoryResource())));
    if (count > 0) {
        range.reserve(static_cast<std::size_t>(count));
        IntegerValueBuilder::appendReservedRange(range, start, end);
    }
    SemanticOperationResult success;
    success.value = Value{std::move(range)};
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::makeRangeFromStack(
    core::pmr::Vector<Value>& operands, int boundCount, std::size_t maxContainerValues)
{
    if (boundCount != 1 && boundCount != 2) {
        return failure(ScriptDiagnosticCode::InvalidOpcode, "range expects one or two arguments");
    }
    const auto popBound = [&operands](int& output) -> bool {
        if (operands.empty())
            return false;
        Value value = std::move(operands.back());
        operands.pop_back();
        return tryGetInt(value, output);
    };
    int end = 0;
    if (!popBound(end))
        return failure(ScriptDiagnosticCode::BadCast, "expected int operand");
    int start = 0;
    if (boundCount == 2 && !popBound(start))
        return failure(ScriptDiagnosticCode::BadCast, "expected int operand");
    return makeRange(start, end, maxContainerValues);
}

SemanticOperationResult ScriptSemanticRuntime::makeReflectedInstance(
    core::pmr::Vector<Value>& operands,
    const core::pmr::String& className,
    int argumentCount,
    std::size_t maxCallArguments)
{
    if (argumentCount < 0 || static_cast<std::size_t>(argumentCount) > maxCallArguments)
        return failure(
            ScriptDiagnosticCode::ResourceLimitExceeded, "constructor exceeds the argument limit");
    auto* classInfo = arp::reflection::db::instance().find_class(className);
    if (classInfo == nullptr) {
        auto diagnostic = failure(ScriptDiagnosticCode::UndeclaredName, "unknown class '");
        diagnostic.diagnostic.message += className;
        diagnostic.diagnostic.message += "'";
        return diagnostic;
    }
    if (operands.size() < static_cast<std::size_t>(argumentCount))
        return failure(
            ScriptDiagnosticCode::StackUnderflow, "stack underflow on class constructor args");
    Value constructor = classInfo->get_field("__new__");
    if (!constructor.is<arp::reflection::function>()) {
        auto diagnostic = failure(
            ScriptDiagnosticCode::HostError, "reflected class has no __new__ constructor: ");
        diagnostic.diagnostic.message += className;
        return diagnostic;
    }
    core::pmr::Vector<Value> arguments{&core::defaultCpuMemoryResource()};
    arguments.reserve(static_cast<std::size_t>(argumentCount));
    for (int index = 0; index < argumentCount; ++index) {
        arguments.push_back(std::move(operands.back()));
        operands.pop_back();
    }
    std::reverse(arguments.begin(), arguments.end());
    try {
        Value instance =
            constructor.cast<arp::reflection::function&>().call_v(Value::undefined(), arguments);
        if (instance.is_undefined()) {
            auto diagnostic =
                failure(ScriptDiagnosticCode::HostError, "reflected class constructor failed: ");
            diagnostic.diagnostic.message += className;
            return diagnostic;
        }
        SemanticOperationResult success;
        success.value = std::move(instance);
        return success;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        return failure(ScriptDiagnosticCode::HostError, "host callable threw");
    }
}

SemanticOperationResult ScriptSemanticRuntime::makeScriptInstance(
    const core::pmr::String& className, const core::pmr::Vector<core::pmr::String>& fields)
{
    auto* classInfo = arp::reflection::db::instance().find_class(className);
    if (classInfo == nullptr) {
        classInfo = arp::reflection::db::instance().register_class(
            className, arp::reflection::instance_t, 0, 0, nullptr);
    }
    if (classInfo == nullptr)
        return failure(ScriptDiagnosticCode::HostError, "script class registration failed");
    arp::reflection::instance instance{classInfo};
    for (const auto& field : fields)
        instance.slot(field) = makeNil();
    SemanticOperationResult success;
    success.value = Value{instance};
    return success;
}

namespace {
// One collection traversal contract shared by boxed/native ABI and slot ABI.
// The integer factory chooses representation, never collection semantics.
class CollectionIteration final {
public:
    template <class Result, class IntegerFactory>
    static ScriptDiagnosticCode advance(
        Result& result, const Value& source, int index, IntegerFactory integer)
    {
        if (const auto* actual = checkedValuePayload<arp::reflection::hlist>(source)) {
            const auto& list = *actual;
            if (index >= 0 && index < static_cast<int>(list.size())) {
                result.key = integer(index);
                result.value = list[static_cast<std::size_t>(index)];
                result.hasMore = true;
            }
            return ScriptDiagnosticCode::None;
        }
        if (const auto* actual = checkedValuePayload<arp::reflection::hdict>(source)) {
            const auto& dict = *actual;
            if (index >= 0 && index < static_cast<int>(dict.size())) {
                auto item = dict.begin();
                std::advance(item, index);
                result.key = item->first;
                result.value = item->second;
                result.hasMore = true;
            }
            return ScriptDiagnosticCode::None;
        }
        if (const auto* actual = checkedValuePayload<ScriptBytes>(source)) {
            const auto& bytes = actual->data;
            if (index >= 0 && index < static_cast<int>(bytes.size())) {
                result.key = integer(index);
                result.value = integer(static_cast<int>(bytes[static_cast<std::size_t>(index)]));
                result.hasMore = true;
            }
            return ScriptDiagnosticCode::None;
        }
        return ScriptDiagnosticCode::BadCast;
    }

    template <class Result, class IntegerFactory>
    static Result next(const Value& source, int index, IntegerFactory integer)
    {
        Result result;
        result.diagnostic.code = advance(result, source, index, integer);
        if (result.diagnostic.code != ScriptDiagnosticCode::None)
            result.diagnostic.message = "range over non-iterable";
        return result;
    }
};
} // namespace

SemanticRangeNextResult ScriptSemanticRuntime::rangeNextCollection(Value source, int index)
{
    return CollectionIteration::next<SemanticRangeNextResult>(source, index, [](int value) {
        return makeInt(value);
    });
}

SemanticSlotRangeNextResult ScriptSemanticRuntime::rangeNextCollectionSlots(
    const Value& source, int index)
{
    return CollectionIteration::next<SemanticSlotRangeNextResult>(source, index, [](int value) {
        return ScriptScalar(value);
    });
}

SemanticCollectionStep ScriptSemanticRuntime::rangeNextCollectionStep(
    const Value& source, int index)
{
    SemanticCollectionStep result;
    result.code = CollectionIteration::advance(result, source, index, [](int value) {
        return ScriptScalar(value);
    });
    return result;
}

namespace {
// Representation adapters share one indexing algorithm across VM and Value ABI.
// Only exact int payloads bypass conversion; dictionary keys always use hdict.
class CollectionIndexAccess final {
    static bool integerIndex(const Value& index, int& value) { return tryGetInt(index, value); }

    static bool integerIndex(const ExecutionSlot& index, int& value)
    {
        if (const auto* integer = index.scalarPointer<int>()) {
            value = *integer;
            return true;
        }
        return tryGetInt(index.boxed(), value);
    }

    static const Value& key(const Value& index) { return index; }

    static const Value& key(const ExecutionSlot& index) { return index.boxed(); }

public:
    template <class Result, class Index, class IntegerFactory>
    static Result load(const Value& base, const Index& index, IntegerFactory integer)
    {
        Result result;
        if (const auto* actual = checkedValuePayload<arp::reflection::hlist>(base)) {
            const auto& list = *actual;
            int slot = 0;
            if (!integerIndex(index, slot) || slot < 0 || slot >= static_cast<int>(list.size())) {
                result.diagnostic.code = ScriptDiagnosticCode::IndexError;
                result.diagnostic.message = "list index out of range";
            } else
                result.value = list[static_cast<std::size_t>(slot)];
        } else if (const auto* actual = checkedValuePayload<arp::reflection::hdict>(base)) {
            const auto& dict = *actual;
            const auto found = dict.find(key(index));
            result.value = found == dict.end() ? makeNil() : found->second;
        } else if (const auto* actual = checkedValuePayload<ScriptBytes>(base)) {
            const auto& bytes = actual->data;
            int slot = 0;
            if (!integerIndex(index, slot) || slot < 0 || slot >= static_cast<int>(bytes.size())) {
                result.diagnostic.code = ScriptDiagnosticCode::IndexError;
                result.diagnostic.message = "bytes index out of range";
            } else
                result.value = integer(static_cast<int>(bytes[static_cast<std::size_t>(slot)]));
        } else {
            result.diagnostic.code = ScriptDiagnosticCode::NotIndexable;
            result.diagnostic.message = "value is not indexable";
        }
        return result;
    }
};
} // namespace

SemanticOperationResult ScriptSemanticRuntime::loadIndex(Value base, Value index)
{
    return CollectionIndexAccess::load<SemanticOperationResult>(base, index, [](int value) {
        return makeInt(value);
    });
}

SemanticSlotOperationResult ScriptSemanticRuntime::loadIndexSlots(
    const Value& base, const ExecutionSlot& index)
{
    return CollectionIndexAccess::load<SemanticSlotOperationResult>(base, index, [](int value) {
        return ScriptScalar(value);
    });
}

bool ScriptSemanticRuntime::IntegerIndexBorrow::bind(const ExecutionSlot& base)
{
    list_ = nullptr;
    dictionary_ = nullptr;
    cache_.clear();
    integers_ = {};
    if (!base.is<arp::reflection::hlist>() && !base.is<arp::reflection::hdict>())
        return false;
    const auto& carrier = base.boxed();
    if (carrier.is_native_reference())
        return false;
    if (carrier.is<arp::reflection::hlist>()) {
        list_ = carrier.ptr<arp::reflection::hlist>();
        if (list_)
            integers_ = IntegerValueCacheBorrow::current();
        return list_ != nullptr;
    }
    if (!carrier.is<arp::reflection::hdict>()
        || !arp::reflection::type_of<int>()->class_()->hasBuiltinOrdering())
        return false;
    auto* dictionary = carrier.ptr<arp::reflection::hdict>();
    if (!dictionary || !std::all_of(dictionary->begin(), dictionary->end(), [](const auto& item) {
            return ActualIntegerPayload::qualify(item.first) != nullptr;
        }))
        return false;
    dictionary_ = dictionary;
    integers_ = IntegerValueCacheBorrow::current();
    return true;
}

Value* ScriptSemanticRuntime::IntegerIndexBorrow::find(const Value& key, int payload) const
{
    assert(dictionary_ != nullptr);
    const Value* element;
    if (!cache_.find(payload, element)) {
        const auto found = dictionary_->find(key);
        element = found == dictionary_->end() ? nullptr : &found->second;
        cache_.insert(payload, element);
    }
    // Addresses originate in this mutable dictionary. Only its lookup relation
    // is borrowed as immutable; replacing mapped Value does not invalidate it.
    return const_cast<Value*>(element);
}

const Value* ScriptSemanticRuntime::IntegerIndexBorrow::read(const ExecutionSlot& index) const
{
    const int* raw = index.scalarPointer<int>();
    if (!raw || (!index.inlineScalar() && index.boxed().is_native_reference()))
        return nullptr;
    if (list_)
        return *raw >= 0 && static_cast<std::size_t>(*raw) < list_->size()
            ? &(*list_)[static_cast<std::size_t>(*raw)]
            : nullptr;
    if (!dictionary_)
        return nullptr;
    const Value* key = index.inlineScalar() ? integers_.find(*raw) : &index.boxed();
    const int* payload = key ? ActualIntegerPayload::checkedPointer(*key) : nullptr;
    return payload ? find(*key, *payload) : nullptr;
}

bool ScriptSemanticRuntime::IntegerIndexBorrow::readInteger(
    const ExecutionSlot& index, int& value) const
{
    const auto* element = read(index);
    const int* payload = element ? ActualIntegerPayload::checkedPointer(*element) : nullptr;
    if (!payload)
        return false;
    value = *payload;
    return true;
}

Value* ScriptSemanticRuntime::IntegerIndexBorrow::destination(const ExecutionSlot& index) const
{
    const int* raw = index.scalarPointer<int>();
    if (!raw)
        return nullptr;
    const Value* key = index.inlineScalar() ? integers_.find(*raw) : &index.boxed();
    const int* actual = key ? ActualIntegerPayload::checkedPointer(*key) : nullptr;
    if (!actual)
        return nullptr;
    const int payload = *actual;
    if (list_)
        return payload >= 0 && static_cast<std::size_t>(payload) < list_->size()
            ? &(*list_)[static_cast<std::size_t>(payload)]
            : nullptr;
    return dictionary_ ? find(*key, payload) : nullptr;
}

bool ScriptSemanticRuntime::IntegerIndexUpdate::bind(
    const IntegerIndexBorrow& borrow, const ExecutionSlot& index, int& source)
{
    destination_ = nullptr;
    source_ = nullptr;
    const Value* element;
    Value* destination;
    if (!borrow.resolve(index, element, destination))
        return false;
    // Both replaced and read carriers must be nonnative integers. In particular
    // a host-modified cached list index can select a different write element;
    // releasing an arbitrary object there must never occur inside this borrow.
    const auto* actual = ActualIntegerPayload::qualify(*element);
    if (!actual || (destination != element && !ActualIntegerPayload::qualify(*destination)))
        return false;
    source = *actual;
    destination_ = destination;
    source_ = element;
    return true;
}

bool ScriptSemanticRuntime::IntegerIndexBorrow::resolve(
    const ExecutionSlot& index, const Value*& source, Value*& destination) const
{
    const int* raw = index.scalarPointer<int>();
    if (!raw)
        return false;
    const Value* key = index.inlineScalar() ? integers_.find(*raw) : &index.boxed();
    if (!key)
        return false;
    const auto* actual = ActualIntegerPayload::qualify(*key);
    if (!actual)
        return false;
    const int payload = *actual;
    if (list_) {
        if (*raw < 0 || static_cast<std::size_t>(*raw) >= list_->size() || payload < 0
            || static_cast<std::size_t>(payload) >= list_->size())
            return false;
        source = &(*list_)[static_cast<std::size_t>(*raw)];
        destination = &(*list_)[static_cast<std::size_t>(payload)];
    } else if (dictionary_)
        source = destination = find(*key, payload);
    else
        return false;
    return source != nullptr && destination != nullptr;
}

bool ScriptSemanticRuntime::IntegerIndexUpdate::readSource(int& value) const
{
    if (!source_)
        return false;
    const auto* actual = ActualIntegerPayload::qualify(*source_);
    if (!actual)
        return false;
    value = *actual;
    return true;
}

bool ScriptSemanticRuntime::IntegerIndexUpdate::bind(
    const ExecutionSlot& base, const ExecutionSlot& index, int& source)
{
    destination_ = nullptr;
    source_ = nullptr;
    const int* rawIndex = index.scalarPointer<int>();
    if (!rawIndex)
        return false;
    // LoadIndex reads an inline list index directly, whereas StoreIndex boxes
    // it and may observe a host-modified cached carrier. Imported boxed indices
    // keep their own identity. Do not materialize a cold cache during a guard.
    const Value* key = index.inlineScalar() ? findCachedIntValue(*rawIndex) : &index.boxed();
    if (!key || !key->is<int>() || key->is_native_reference())
        return false;
    const int writeIndex = ActualIntegerPayload::read(*key);
    Value* destination;
    if (base.is<arp::reflection::hlist>()) {
        const auto& carrier = base.boxed();
        if (carrier.is_native_reference())
            return false;
        auto* list = carrier.ptr<arp::reflection::hlist>();
        if (!list || *rawIndex < 0 || static_cast<std::size_t>(*rawIndex) >= list->size()
            || writeIndex < 0 || static_cast<std::size_t>(writeIndex) >= list->size())
            return false;
        const auto& element = (*list)[static_cast<std::size_t>(*rawIndex)];
        if (!element.is<int>() || element.is_native_reference())
            return false;
        source = ActualIntegerPayload::read(element);
        destination = &(*list)[static_cast<std::size_t>(writeIndex)];
    } else if (base.is<arp::reflection::hdict>()) {
        const auto& carrier = base.boxed();
        if (carrier.is_native_reference()
            || !arp::reflection::type_of<int>()->class_()->hasBuiltinOrdering())
            return false;
        auto* dictionary = carrier.ptr<arp::reflection::hdict>();
        if (!dictionary
            || !std::all_of(dictionary->begin(), dictionary->end(), [](const auto& item) {
                   return item.first.template is<int>() && !item.first.is_native_reference();
               }))
            return false;
        // All comparator payloads are actual nonnative integers with stock
        // ordering. Repeating this lookup cannot invoke custom host callbacks.
        const auto found = dictionary->find(*key);
        if (found == dictionary->end() || !found->second.is<int>()
            || found->second.is_native_reference())
            return false;
        source = ActualIntegerPayload::read(found->second);
        destination = &found->second;
    } else
        return false;
    destination_ = destination;
    return true;
}

void ScriptSemanticRuntime::IntegerIndexUpdate::commit(int value) const
{
    assert(destination_ != nullptr);
    // Never mutate a shared/cached scalar payload in place. The original store
    // replaces the destination carrier with makeInt's actual cached result.
    *destination_ = makeInt(value);
}

SemanticOperationResult ScriptSemanticRuntime::storeIndex(Value base, Value index, Value value)
{
    if (base.is<arp::reflection::hlist>()) {
        int slot = 0;
        auto* list = base.ptr<arp::reflection::hlist>();
        if (!tryGetInt(index, slot) || slot < 0 || slot >= static_cast<int>(list->size()))
            return failure(ScriptDiagnosticCode::IndexError, "list index out of range");
        (*list)[static_cast<std::size_t>(slot)] = std::move(value);
    } else if (base.is<arp::reflection::hdict>()) {
        (*base.ptr<arp::reflection::hdict>())[std::move(index)] = std::move(value);
    } else if (base.is<ScriptBytes>()) {
        return failure(ScriptDiagnosticCode::ImmutableAssign, "bytes values are immutable");
    } else {
        return failure(ScriptDiagnosticCode::NotIndexable, "value is not indexable");
    }
    SemanticOperationResult success;
    success.value = makeNil();
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::findField(
    Value base, const core::pmr::String& fieldName)
{
    try {
        Value result = Value::undefined();
        if (auto* actual = checkedValuePayload<arp::reflection::instance>(base)) {
            if (!actual->alive())
                return failure(
                    ScriptDiagnosticCode::HostError, "ARP-SCRIPT-COMPONENT-STALE-INSTANCE");
            result = actual->__getitem__(fieldName);
        } else if (isReflectedTypeBinding(base)) {
            const auto* classInfo = asReflectedTypeBinding(base);
            if (classInfo != nullptr)
                result = classInfo->get_field(fieldName);
        } else if (base.is<arp::reflection::enumeration>()) {
            result = makeInt(base.cast<arp::reflection::enumeration&>().get_enum_value(fieldName));
        } else if (isModuleObject(base)) {
            const auto exports = asModuleObject(base);
            if (exports != nullptr) {
                const auto found = exports->symbols.find(fieldName);
                if (found != exports->symbols.end())
                    result = found->second;
            }
        } else if (base.class_() != nullptr) {
            result = base.class_()->get_attr(base, fieldName);
        }
        SemanticOperationResult success;
        success.value = std::move(result);
        return success;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        return failure(ScriptDiagnosticCode::HostError, "field getter threw");
    }
}

SemanticOperationResult ScriptSemanticRuntime::loadField(
    Value base, const core::pmr::String& fieldName)
{
    auto result = findField(std::move(base), fieldName);
    if (result.ok() && result.value.is_undefined()) {
        result = failure(ScriptDiagnosticCode::NoMember, "field lookup failed: ");
        result.diagnostic.message += fieldName;
    }
    return result;
}

SemanticOperationResult ScriptSemanticRuntime::loadModuleExport(
    Value module, const core::pmr::String& exportName)
{
    const auto exports = asModuleObject(module);
    if (!exports)
        return failure(ScriptDiagnosticCode::HostError, "LoadModuleExport: value is not a module");
    const auto found = exports->symbols.find(exportName);
    if (found == exports->symbols.end()) {
        SemanticOperationResult result =
            failure(ScriptDiagnosticCode::HostError, "module export not found: ");
        result.diagnostic.message += exportName;
        return result;
    }
    SemanticOperationResult success;
    success.value = found->second;
    return success;
}

SemanticOperationResult ScriptSemanticRuntime::loadModule(
    ModuleRegistry& registry, const core::pmr::String& moduleName)
{
#if defined(ARP_SCRIPT_AOT_ONLY_RUNTIME)
    (void)registry;
    (void)moduleName;
    return failure(ScriptDiagnosticCode::AotModuleUnavailable,
        "strict DirectNative runtime does not load dynamic source modules");
#else
    try {
        const auto exports = registry.loadModule(moduleName);
        if (!exports) {
            SemanticOperationResult result =
                failure(ScriptDiagnosticCode::HostError, "module not found: ");
            result.diagnostic.message += moduleName;
            if (!registry.lastDiagnostics().empty()) {
                result.diagnostic.message += ": ";
                result.diagnostic.message += registry.lastDiagnostics();
            }
            return result;
        }
        SemanticOperationResult success;
        success.value = Value{ModuleObject(exports)};
        return success;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        return failure(ScriptDiagnosticCode::HostError, "module loader threw");
    }
#endif
}

SemanticOperationResult ScriptSemanticRuntime::storeField(
    Value base, const core::pmr::String& fieldName, Value value)
{
    try {
        if (base.is<arp::reflection::instance>()) {
            if (!base.ptr<arp::reflection::instance>()->alive())
                return failure(
                    ScriptDiagnosticCode::HostError, "ARP-SCRIPT-COMPONENT-STALE-INSTANCE");
            base.ptr<arp::reflection::instance>()->__setitem__(fieldName, value);
        } else if (base.class_() != nullptr) {
            const auto member = base.class_()->get_field(fieldName);
            if (member.is_undefined()) {
                auto diagnostic = failure(ScriptDiagnosticCode::NoMember, "field store failed: ");
                diagnostic.diagnostic.message += fieldName;
                return diagnostic;
            }
            if (!member.is_property()) {
                auto diagnostic =
                    failure(ScriptDiagnosticCode::InvalidAssignment, "field is not assignable: ");
                diagnostic.diagnostic.message += fieldName;
                return diagnostic;
            }
            if (member.cast<arp::reflection::property&>().is_readonly()) {
                auto diagnostic =
                    failure(ScriptDiagnosticCode::ImmutableAssign, "field is readonly: ");
                diagnostic.diagnostic.message += fieldName;
                return diagnostic;
            }
            base.class_()->set_attr(base, fieldName, value);
        } else {
            return failure(ScriptDiagnosticCode::NoMember, "field store failed");
        }
        SemanticOperationResult success;
        success.value = makeNil();
        return success;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        return failure(ScriptDiagnosticCode::HostError, "field setter threw");
    }
}

SemanticOperationResult ScriptSemanticRuntime::dereference(const ScriptPointer& pointer)
{
    switch (pointer.kind) {
    case ScriptPointerKind::Cell:
        if (!pointer.cell)
            return failure(ScriptDiagnosticCode::NilDeref, "nil pointer dereference");
        {
            SemanticOperationResult result;
            result.value = pointer.cell->value;
            return result;
        }
    case ScriptPointerKind::Index:
        return loadIndex(pointer.base, pointer.index);
    case ScriptPointerKind::Field:
        return loadField(pointer.base, pointer.field);
    }
    return failure(ScriptDiagnosticCode::NilDeref, "invalid pointer kind");
}

SemanticOperationResult ScriptSemanticRuntime::storePointer(ScriptPointer& pointer, Value value)
{
    switch (pointer.kind) {
    case ScriptPointerKind::Cell:
        if (!pointer.cell)
            return failure(ScriptDiagnosticCode::NilDeref, "nil pointer dereference");
        pointer.cell->value = std::move(value);
        break;
    case ScriptPointerKind::Index:
        return storeIndex(pointer.base, pointer.index, std::move(value));
    case ScriptPointerKind::Field:
        return storeField(pointer.base, pointer.field, std::move(value));
    }
    SemanticOperationResult result;
    result.value = makeNil();
    return result;
}

} // namespace arp::scriptvm
