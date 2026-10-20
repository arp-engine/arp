#include "fixtures/reflection_codegen_fixture.h"
#include "reflection/reflection.h"

#include <cassert>

namespace arp::reflection::generated {
void register_reflection_codegen_fixture_reflection();
}

int main()
{
    using namespace arp::reflection;
    using namespace arp::tests::reflection_codegen;

    generated::register_reflection_codegen_fixture_reflection();

    object created = class_of<CodegenTransform>()->new_object();
    assert(!created.is_undefined());
    assert(created["x"].cast<float>() == 0.0f);

    created.set("x", object(2.0f));
    assert(created["x"].cast<float>() == 2.0f);

    object translated = created.call("translate", 3.0f);
    assert(translated.cast<float>() == 5.0f);
    assert(created["x"].cast<float>() == 5.0f);

    assert(class_of<DefaultNamedData>()->name()
        == arp::core::pmr::String("DefaultNamedData", &arp::core::defaultCpuMemoryResource()));
    object defaultNamed = class_of<DefaultNamedData>()->new_object();
    assert(!defaultNamed.is_undefined());
    defaultNamed.set("value", object(42));
    assert(defaultNamed["value"].cast<int>() == 42);

    CodegenRttiDerived derived;
    CodegenRttiBase* base = &derived;
    object reflectedBase(base);
    auto* reflectedDerived = reflectedBase.cast<CodegenRttiDerived*>();
    assert(reflectedDerived == &derived);
    assert(reflectedDerived->derivedValue == 2);

    auto axisEnum = class_of<CodegenAxis>()->get_field("CodegenAxis").cast<enumeration&>();
    assert(axisEnum.get_enum_value("X") == static_cast<int>(CodegenAxis::X));
    assert(axisEnum.get_enum_value("Z") == static_cast<int>(CodegenAxis::Z));
    return 0;
}
