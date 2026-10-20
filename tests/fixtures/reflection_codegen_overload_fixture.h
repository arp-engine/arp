#pragma once

#include "reflection/reflection_macros.h"

namespace arp::tests::reflection_codegen_overload_negative {

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class Overloaded {
public:
    AFUNC(Name = "same")

    int value(int input) { return input; }

    AFUNC(Name = "same")

    int value(float input) { return static_cast<int>(input); }
};

} // namespace arp::tests::reflection_codegen_overload_negative
