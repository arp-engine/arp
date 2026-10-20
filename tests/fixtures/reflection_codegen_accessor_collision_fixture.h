#pragma once

#include "reflection/reflection_macros.h"

namespace arp::tests::reflection_codegen_accessor_collision {

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class AccessorCollision {
public:
    AFUNC()

    int value() const { return value_; }

    AFUNC()

    void set_value(int value) { value_ = value; }

    AFUNC(Name = "value")

    void conflicting_export() {}

private:
    int value_ = 0;
};

} // namespace arp::tests::reflection_codegen_accessor_collision
