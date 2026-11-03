#pragma once

#include "editor/ui/editor_ui_sources.h"

#include <optional>

namespace arp::editor {
class EditorUiSourceHost final {
public:
    static bool prepare(const std::filesystem::path& uiRoot,
        const std::filesystem::path& fontRoot,
        const std::filesystem::path& shaderRoot,
        std::optional<std::string_view> baseUrl,
        core::SharedPtr<const EditorUiSources>& output,
        core::pmr::String& diagnostic);
};
} // namespace arp::editor
