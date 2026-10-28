#pragma once

#include "core/concurrency/callback.h"
#include "core/encoding/yaml_contract.h"
#include "editor/workspace/editor_scene_inspector.h"

namespace arp::editor {
using EditorSceneResourceValidator = bool (*)(
    void* context, std::string_view resourceType, std::string_view resourceId);

// Common native property wire contract. Callers provide the identity mapping
// and current formal schema; decoding never mutates a document or component.
class EditorScenePropertyCodec final {
public:
    using ObjectResolver = core::SharedCallback<std::uint64_t(std::uint64_t)>;
    static bool encode(const SceneInspectorProperty&,
        core::YamlContract::Node&,
        std::uint64_t sceneObjectLocalId = 0);
    static bool decode(const SceneInspectorProperty&,
        const core::YamlContract::Node&,
        SceneInspectorProperty&,
        EditorSceneResourceValidator,
        void*,
        const ObjectResolver&);
};
} // namespace arp::editor
