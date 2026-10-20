#pragma once

#include "reflection/reflection_macros.h"

namespace arp::tests::reflection_codegen_constructor_negative {

ACLASS(Ownership = Value, Lifecycle = Value)

class MultipleConstructors {
public:
    ACONSTRUCT()

    MultipleConstructors(int value) : value_(value) {}

    ACONSTRUCT()

    MultipleConstructors(float value) : value_(static_cast<int>(value)) {}

    APROP()
    int value_ = 0;
};

} // namespace arp::tests::reflection_codegen_constructor_negative
