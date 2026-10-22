#pragma once

#include "scriptvm/runtime/semantic_runtime.h"

namespace arp::scriptvm {

// Shared scalar semantics with Value fallback at genuinely dynamic operations.
// Operands remain carriers until consumed; native and cv imports are never
// replaced by a cached payload, and failures retain the shared diagnostic.
class DirectNativeSlotOperations final {
public:
    template <SemanticBinaryOperation Operation>
    static SemanticSlotOperationResult binary(ExecutionSlot lhs, ExecutionSlot rhs)
    {
        SemanticSlotOperationResult result;
        if constexpr (Operation == SemanticBinaryOperation::Add) {
            std::string_view a, b;
            if (lhs.stringView(a) && rhs.stringView(b)) {
                auto joined = ScriptSemanticRuntime::concatenateStrings(a, b);
                result.value = std::move(joined.value);
                result.diagnostic = std::move(joined.diagnostic);
                return result;
            }
        }
        ScriptScalar left, right;
        ScalarOperationResult scalar;
        bool handled = false;
        if (lhs.scalar(left) && rhs.scalar(right)) {
            if constexpr (Operation <= SemanticBinaryOperation::Mod)
                handled = ScriptSemanticRuntime::scalarBinary<Operation>(left, right, scalar);
            else
                handled = ScriptSemanticRuntime::scalarBinary(Operation, left, right, scalar);
        }
        if (handled) {
            result.value = scalar.value;
            result.diagnostic.code = scalar.code;
            if (!scalar.ok())
                result.diagnostic.message = scalar.message;
        } else {
            auto boxed = ScriptSemanticRuntime::binary(Operation, lhs.takeValue(), rhs.takeValue());
            result.value = std::move(boxed.value);
            result.diagnostic = std::move(boxed.diagnostic);
        }
        return result;
    }

    template <SemanticCompareOperation Operation>
    static SemanticSlotOperationResult compare(ExecutionSlot lhs, ExecutionSlot rhs)
    {
        SemanticSlotOperationResult result;
        ScriptScalar left, right;
        bool value = false;
        std::string_view leftString, rightString;
        if (lhs.stringView(leftString) && rhs.stringView(rightString)) {
            result.value = ScriptScalar(
                ScriptSemanticRuntime::compareStrings(Operation, leftString, rightString));
        } else if (lhs.scalar(left) && rhs.scalar(right)
            && ScriptSemanticRuntime::scalarCompare<Operation>(left, right, value)) {
            result.value = ScriptScalar(value);
        } else {
            auto boxed =
                ScriptSemanticRuntime::compare(Operation, lhs.takeValue(), rhs.takeValue());
            result.value = std::move(boxed.value);
            result.diagnostic = std::move(boxed.diagnostic);
        }
        return result;
    }

    static bool integer(const ExecutionSlot& slot, int& value)
    {
        if (const auto* actual = slot.scalarPointer<int>()) {
            value = *actual;
            return true;
        }
        return tryGetInt(slot.boxed(), value);
    }

    static SemanticSlotOperationResult condition(const ExecutionSlot& slot)
    {
        SemanticSlotOperationResult result;
        if (const auto* actual = slot.scalarPointer<bool>())
            result.value = ScriptScalar(*actual);
        else {
            auto boxed =
                ScriptSemanticRuntime::convert(SemanticConversionOperation::Bool, slot.boxed());
            result.value = std::move(boxed.value);
            result.diagnostic = std::move(boxed.diagnostic);
        }
        return result;
    }
};

} // namespace arp::scriptvm
