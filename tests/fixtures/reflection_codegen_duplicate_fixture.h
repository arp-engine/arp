#pragma once

#include "reflection/reflection_macros.h"

namespace arp::tests::reflection_codegen_negative {

ACLASS(Name = "DuplicateType")

class First {
public:
    AFUNC()

    int value() const { return 1; }
};

ACLASS(Name = "DuplicateType")

class Second {
public:
    AFUNC()

    int value() const { return 2; }
};

} // namespace arp::tests::reflection_codegen_negative
