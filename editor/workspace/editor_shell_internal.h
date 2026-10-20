#pragma once

#include "editor/workspace/editor_scene_document.h"

#include <string_view>

namespace arp::editor::detail {

bool sceneResourceReady(void* context, std::string_view type, std::string_view id);
resource::ResourceManager* sceneResourceManager(const EditorSceneDocument& document);

} // namespace arp::editor::detail
