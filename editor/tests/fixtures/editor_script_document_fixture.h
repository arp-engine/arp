#pragma once

#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_script_bindings.h"
#include "tests/fixtures/script_component_fixture.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace arp::editor::testfixtures {
inline constexpr const char* primaryId = "00000000-0000-4000-8000-000000000096";
inline constexpr const char* secondaryId = "00000000-0000-4000-8000-000000000097";

inline void check(bool value, const core::pmr::String& diagnostic = {})
{
    if (!value)
        std::fprintf(stderr, "FAIL: %s\n", diagnostic.c_str());
    assert(value);
}

class Fixture final {
public:
    Fixture()
        : root(std::filesystem::path(ARP_SOURCE_DIR) / "build"
              / ("editor-script-bindings-"
                  + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(root);
        manifest.id = "00000000-0000-4000-8000-000000000094";
        manifest.name = "Document script binding";
        manifest.engineContract = "arp.scene-asset/2";
        for (bool secondary : {false, true}) {
            ProjectScriptModule module;
            module.id = secondary ? "secondary" : "primary";
            module.resourceId = secondary ? secondaryId : primaryId;
            module.source = secondary ? "secondary.arp" : "primary.arp";
            module.schemaVersion = 1;
            module.authoring = ProjectScriptAuthoring{};
            module.authoring->maxSourceBytes = 65536;
            module.authoring->preparationBytes = 16 * 1024 * 1024;
            module.authoring->maxInstances = 32;
            module.authoring->execution = arp::tests::ScriptComponentFixture::budget();
            module.authoring->imports = secondary
                ? core::pmr::Vector<core::pmr::String>{"arp_scene", "arp_scene2d"}
                : core::pmr::Vector<core::pmr::String>{"arp_scene"};
            write(module.source, secondary ? secondarySource : primarySource);
            manifest.scriptModules.push_back(std::move(module));
        }
    }

    ~Fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        check(!error);
    }

    void write(std::string_view name, std::string_view source)
    {
        std::ofstream output(
            root / std::filesystem::u8path(name), std::ios::binary | std::ios::trunc);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        output.close();
        check(bool(output));
    }

    static core::SharedPtr<EditorSceneServiceSession> services(bool physics = true)
    {
        physics2d::WorldConfig config{{0, 0}, 32, 32, 8, 32, 64, 32, 16, 4, 1.f / 60};
        auto world = physics ? core::makeShared<physics2d::PhysicsWorld2D>(config) : nullptr;
        check(!physics || world->valid());
        return core::makeShared<EditorSceneServiceSession>(
            core::makeShared<resource::ResourceManager>(), std::move(world));
    }

    static EditorScriptBindingBudget budget() { return {16 * 1024 * 1024, {4, 1024 * 1024}}; }

    bool prepare(EditorSceneDocument& document, core::pmr::String& diagnostic)
    {
        return document.prepareScriptAuthoring(root, manifest, budget(), diagnostic);
    }

    std::filesystem::path root;
    ProjectManifest manifest;
    // clang-format off
    static constexpr const char* primarySource = R"ARP(from arp_scene import main_scene


class Mover(ScriptComponent):
    amount: int

    def __init__(self):
        main_scene.set_object_name("ConstructorExecuted")


def read_name() -> string:
    return main_scene.object_name


main_scene.set_object_name("InitializerExecuted")
)ARP";
    static constexpr const char* secondarySource = R"ARP(from arp_scene import main_scene
from arp_scene2d import RigidBody2DComponent


class PhysicsLink(ScriptComponent):
    body: RigidBody2DComponent?
)ARP";
    // clang-format on
};

} // namespace arp::editor::testfixtures
