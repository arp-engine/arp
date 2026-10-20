#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace arp::editor {
inline constexpr std::size_t kEditorSceneComponentCount = 29;

struct SceneComponentCatalogEntry {
    std::string_view typeName;
    std::string_view displayName;
};
} // namespace arp::editor
