#pragma once

#include "reflection/reflection_macros.h"

namespace arp::tests::reflection_codegen {

ACLASS(Ownership = Engine, Lifecycle = Explicit, Construct = Default)

class CodegenNativeBase {
public:
    OBJECT_RTTI_IMPL(CodegenNativeBase);

    AFUNC(Callable)

    void set_value(int value) { value_ = value; }

    AFUNC()

    int value() const { return value_; }

    APROP(Serialize, Runtime)
    int value_ = 0;
};

ACLASS(Ownership = Engine, Lifecycle = Explicit, Construct = Default)

class CodegenNativeDerived final : public CodegenNativeBase {
public:
    OBJECT_RTTI_IMPL(CodegenNativeDerived);

    AFUNC(Callable)

    int doubled() const { return value_ * 2; }
};

ACLASS(Ownership = Value, Lifecycle = Value)

class CodegenConstructible {
public:
    ACONSTRUCT()

    CodegenConstructible(int value) : value_(value) {}

    AFUNC(Static)

    static int add_one(int value) { return value + 1; }

    AFUNC(Name = "add_int")

    int add(int value) { return value + 10; }

    AFUNC(Name = "add_float")

    int add(float value) { return static_cast<int>(value) + 20; }

    AFUNC(Async, Nullable)

    CodegenConstructible* nullable_self() { return nullptr; }

    APROP(Runtime, Doc = "fixture:field-metadata")
    int value_ = 0;
};

} // namespace arp::tests::reflection_codegen
