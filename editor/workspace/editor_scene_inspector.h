#pragma once
#include "core/cpu_allocator.h"
#include "core/math.h"

#include <cstdint>

namespace arp::resource {
class ResourceManager;
}

namespace arp::scene {
class GameObjectId;
}

namespace arp::editor {
inline constexpr std::size_t kMaxSceneInspectorVector2ListElements = 256u;
inline constexpr std::size_t kMaxSceneInspectorObjectLinks = 64u;

struct SceneInspectorObjectLink {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    std::uint64_t targetObjectId = 0;
};

enum class SceneInspectorPropertyKind : std::uint8_t {
    Unsupported,
    Boolean,
    Integer,
    UnsignedInteger,
    Number,
    Enumeration,
    Vector2,
    Vector2List,
    SceneObjectLinkList,
    Vector3,
    Color4,
    String,
    ResourceReference,
    SceneObjectReference,
};

struct SceneInspectorEnumOption {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    core::pmr::String commandValue{&core::defaultCpuMemoryResource()};
    int value = 0;
};

struct SceneInspectorProperty {
    std::uint64_t objectId = 0;
    std::uint64_t componentId = 0;
    core::pmr::String componentType{&core::defaultCpuMemoryResource()};
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    core::pmr::String typeName{&core::defaultCpuMemoryResource()};
    core::pmr::String resourceType{&core::defaultCpuMemoryResource()};
    core::pmr::String sceneObjectType{&core::defaultCpuMemoryResource()};
    core::pmr::String resourceDiagnostic{&core::defaultCpuMemoryResource()};
    core::pmr::String stringValue{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneInspectorEnumOption> enumOptions{&core::defaultCpuMemoryResource()};
    SceneInspectorPropertyKind kind = SceneInspectorPropertyKind::Unsupported;
    bool editable = false;
    bool resourceOptional = false;
    bool booleanValue = false;
    int integerValue = 0;
    std::uint32_t unsignedIntegerValue = 0;
    std::uint64_t sceneObjectReferenceValue = 0;
    float numberValue = 0.0f;
    core::Vec2 vector2Value{};
    core::pmr::Vector<core::Vec2> vector2ListValue{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneInspectorObjectLink> sceneObjectLinkListValue{
        &core::defaultCpuMemoryResource()};
    core::Vec3 vector3Value{};
    core::Vec4 color4Value{};
};

struct EditorSceneInspectorSnapshot {
    std::uint64_t objectId = 0;
    core::pmr::String objectName{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneInspectorProperty> properties{&core::defaultCpuMemoryResource()};
};

class EditorSceneDocument;

core::pmr::String scenePropertyControlSource(const SceneInspectorProperty& property);
core::pmr::String sceneInspectorDisplayValue(const SceneInspectorProperty& property);
core::pmr::Vector<SceneInspectorProperty> buildSceneInspectorProperties(
    const EditorSceneDocument& document,
    resource::ResourceManager* resources,
    scene::GameObjectId objectId);
} // namespace arp::editor
