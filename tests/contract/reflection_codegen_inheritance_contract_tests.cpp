#include "core/cpu_allocator.h"
#include "fixtures/reflection_codegen_inheritance_fixture.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace arp::reflection::generated {
void register_reflection_codegen_inheritance_reflection();
}

namespace {

arp::core::pmr::String readText(const std::filesystem::path& path)
{
    std::ifstream input(path);
    assert(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return arp::core::toPmrString(buffer.str());
}

} // namespace

int main()
{
    const auto json = readText(std::filesystem::path(ARP_REFLECTION_CODEGEN_INHERITANCE_META));
    assert(json.find("\"ownership\": \"Engine\"") != arp::core::pmr::String::npos);
    assert(json.find("\"lifecycle\": \"Explicit\"") != arp::core::pmr::String::npos);
    assert(json.find("\"bases\": [\"arp::tests::reflection_codegen::CodegenNativeBase\"]")
        != arp::core::pmr::String::npos);
    assert(json.find("\"flags\": [\"Serialize\", \"Runtime\"]") != arp::core::pmr::String::npos);
    assert(json.find("\"doc\": \"fixture:field-metadata\"") != arp::core::pmr::String::npos);
    assert(json.find("\"flags\": [\"Callable\"]") != arp::core::pmr::String::npos);
    assert(json.find("\"constructors\": [{ \"parameters\": [{ \"name\": \"value\"")
        != arp::core::pmr::String::npos);
    assert(json.find("\"static\": true") != arp::core::pmr::String::npos);
    assert(json.find("\"maySuspend\": true") != arp::core::pmr::String::npos);
    assert(json.find("\"nullableReturn\": true") != arp::core::pmr::String::npos);
    assert(json.find("\"returnReferenceKind\": \"pointer\"") != arp::core::pmr::String::npos);
    assert(json.find("\"const\": true") != arp::core::pmr::String::npos);
    assert(json.find("\"reflectName\": \"add_int\"") != arp::core::pmr::String::npos);
    assert(json.find("\"reflectName\": \"add_float\"") != arp::core::pmr::String::npos);
    assert(
        json.find(
            "\"name\": \"value\", \"cppType\": \"int\", \"access\": \"public\", \"source\": \"accessor\", \"getter\": \"value\", \"setter\": \"set_value\"")
        != arp::core::pmr::String::npos);

    arp::reflection::generated::register_reflection_codegen_inheritance_reflection();
    using namespace arp::reflection;
    using namespace arp::tests::reflection_codegen;
    object value = class_of<CodegenNativeDerived>()->new_object();
    assert(!value.is_undefined());
    assert(value.class_()->is_derived_from(class_of<CodegenNativeBase>()));
    assert(value.can_safe_cast<CodegenNativeBase*>());
    value.class_()->set_attr(value, "value", 21);
    assert(value.class_()->get_attr(value, "value").cast<int>() == 21);
    assert(value.class_()->get_field("value").is_property());
    assert(value.class_()->get_field("value").cast<property&>().is_readonly() == false);
    assert(value.call("doubled").cast<int>() == 42);

    using namespace arp::tests::reflection_codegen;
    object constructed = class_of<CodegenConstructible>()->new_object(41);
    assert(!constructed.is_undefined());
    assert(constructed["value_"].cast<int>() == 41);
    assert(constructed.class_()->get_field("value_").cast<property&>().doc()
        == "fixture:field-metadata");
    assert(constructed.call("add_one", 4).cast<int>() == 5);
    assert(constructed.call("add_int", 4).cast<int>() == 14);
    assert(constructed.call("add_float", 4.0f).cast<int>() == 24);
    return 0;
}
