#pragma once

#include "editor/workspace/editor_scene_inspector.h"

#include <optional>

namespace arp::editor {

// Exact bounded field observation shared by object and resource navigation.
class EditorSceneReferenceSource final {
public:
    static const char* prepare(const EditorSceneDocument&,
        std::uint64_t objectId,
        std::uint64_t componentId,
        std::string_view path,
        std::optional<SceneInspectorProperty>& destination) noexcept;
};

// Resolves an exact source field to its current target; never retains Scene or
// reflection ownership and never substitutes a name or candidate-menu index.
class EditorSceneReferenceFocus final {
public:
    // nullptr means success. Every rejection, including OOM, preserves output.
    // Uses the same bounded observation profile as the selection Inspector.
    static const char* prepare(const EditorSceneDocument&,
        std::uint64_t objectId,
        std::uint64_t componentId,
        std::string_view path,
        std::uint64_t& target) noexcept;
};
} // namespace arp::editor
