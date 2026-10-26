#pragma once

#include "editor/workspace/editor_scene_document.h"

#include <string_view>

namespace arp::editor {
enum class ScenePropertyValueKind : std::uint8_t;
bool sceneInspectorKindForValueKind(ScenePropertyValueKind, SceneInspectorPropertyKind&);
} // namespace arp::editor

namespace arp::editor::detail {

resource::ResourceManager* sceneResourceManager(const EditorSceneDocument& document);
core::pmr::String singleLine(std::string_view text);
core::pmr::String sceneFileFailureMessage(std::string_view diagnostic);

} // namespace arp::editor::detail
