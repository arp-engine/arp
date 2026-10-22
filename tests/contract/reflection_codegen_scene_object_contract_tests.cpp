#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scene/scene.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace arp::reflection::generated {
void register_reflection_codegen_scene_objects_reflection();
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
    const auto json = readText(std::filesystem::path(ARP_REFLECTION_CODEGEN_SCENE_OBJECT_META));
    assert(json.find("\"reflectName\": \"Component\"") != arp::core::pmr::String::npos);
    assert(json.find("\"reflectName\": \"GameObject\"") != arp::core::pmr::String::npos);
    assert(json.find("\"reflectName\": \"Scene\"") != arp::core::pmr::String::npos);
    assert(json.find("\"bases\": [\"arp::scene::GameObject\"]") != arp::core::pmr::String::npos);
    assert(json.find("\"ownership\": \"Engine\"") != arp::core::pmr::String::npos);
    assert(json.find("\"lifecycle\": \"Explicit\"") != arp::core::pmr::String::npos);

    arp::reflection::generated::register_reflection_codegen_scene_objects_reflection();
    arp::scene::Scene scene{arp::core::pmr::String("main", &arp::core::defaultCpuMemoryResource()),
        arp::scene::SceneId::fromValue(42u)};
    auto sceneValue = arp::reflection::object::native_reference(&scene);
    assert(sceneValue.call("scene_id_value").cast<std::uint64_t>() == 42u);

    const auto childValue = sceneValue.call("create_object_ref",
        arp::core::pmr::String("child", &arp::core::defaultCpuMemoryResource()));
    assert(!childValue.is_undefined());
    assert(childValue.is_native_reference());
    auto* child = childValue.cast<arp::scene::GameObject*>();
    assert(child != nullptr);
    assert(childValue.call("object_name").cast<arp::core::pmr::String>() == "child");
    assert(sceneValue
               .call("find_object_by_name_ref",
                   arp::core::pmr::String("child", &arp::core::defaultCpuMemoryResource()))
               .cast<arp::scene::GameObject*>()
        == child);
    assert(sceneValue.call("destroy_object_ref", child).cast<bool>());
    assert(!childValue.native_reference_alive());
    assert(childValue.call("object_name").is_undefined());
    assert(sceneValue
            .call("find_object_by_name_ref",
                arp::core::pmr::String("child", &arp::core::defaultCpuMemoryResource()))
            .is_undefined());
    return 0;
}
