#pragma once

#include "reflection/reflection_macros.h"

namespace arp::tests::cpp_plugin {

ASTRUCT(Construct = Default)

struct Point final {
    APROP(Serialize)
    int value = 21;

    AFUNC()

    int doubleValue() const { return value * 2; }

    AFUNC()

    int adjusted() const { return value; }

    AFUNC()

    void set_adjusted(int next) { value = next; }
};

AFUNC(Name = "add")
int add(int lhs, int rhs);

} // namespace arp::tests::cpp_plugin
