#pragma once
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace arp::resource {
class ResourceManager;
}

namespace arp::scene {
class GameObjectId;
class Scene;
class GameObject;
} // namespace arp::scene

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
    bool serializable = false;
    bool resourceOptional = false;
    bool booleanValue = false;
    int integerValue = 0;
    std::uint64_t unsignedIntegerValue = 0;
    std::uint64_t sceneObjectReferenceValue = 0;
    double numberValue = 0.0f;
    core::Vec2 vector2Value{};
    core::pmr::Vector<core::Vec2> vector2ListValue{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneInspectorObjectLink> sceneObjectLinkListValue{
        &core::defaultCpuMemoryResource()};
    core::Vec3 vector3Value{};
    core::Vec4 color4Value{};
};

bool sceneInspectorMetadataEqual(const SceneInspectorProperty&, const SceneInspectorProperty&);
bool sceneInspectorValuesEqual(const SceneInspectorProperty&, const SceneInspectorProperty&);

struct EditorSceneInspectorSnapshot {
    std::uint64_t objectId = 0;
    core::pmr::String objectName{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneInspectorProperty> properties{&core::defaultCpuMemoryResource()};
};

class EditorSceneDocument;

// Prepares a new presentation value; the original property and Scene stay intact
// on allocation failure. Object IDs always come from explicit reference metadata.
class EditorSceneObjectReferenceChoices final {
public:
    enum class TargetStatus { Valid, None, Unassigned, Missing, WrongType, InvalidMetadata };
    static bool matchesTarget(const scene::GameObject&, std::string_view type) noexcept;
    static TargetStatus targetStatus(const scene::Scene&, const SceneInspectorProperty&) noexcept;
    static SceneInspectorProperty build(const scene::Scene&, const SceneInspectorProperty&);
};

// Authoring IDs remain independent of resource handles/generations. Choices use
// the existing exact-type, bounded snapshot; failures never select a substitute.
class EditorSceneResourceReferenceChoices final {
public:
    static constexpr std::size_t maxEntries = 32u;
    static SceneInspectorProperty build(
        const resource::ResourceManager*, const SceneInspectorProperty&);
    // UUID groups 1..3 and 4..5 form two explicit single-line UI labels.
    // Views refer to the supplied ID; non-UUID input is diagnosed, never sliced.
    static std::array<std::string_view, 2> identifierLines(std::string_view id) noexcept;
};

core::pmr::String scenePropertyControlSource(const SceneInspectorProperty& property);
core::pmr::String sceneInspectorDisplayValue(const SceneInspectorProperty& property);
core::pmr::Vector<SceneInspectorProperty> buildSceneInspectorProperties(
    const EditorSceneDocument& document,
    resource::ResourceManager* resources,
    scene::GameObjectId objectId);
} // namespace arp::editor
