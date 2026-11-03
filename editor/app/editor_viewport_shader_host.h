#pragma once

#include "editor/viewport/editor_viewport_shader_sources.h"

#include <optional>

namespace arp::editor {
// Native cold startup only. Browser hosts must pump their existing event loop.
class EditorViewportShaderHost final {
public:
    static bool prepare(const std::filesystem::path& root,
        std::optional<std::string_view> baseUrl,
        EditorViewportShaderSources& output,
        core::pmr::String& diagnostic);
};
} // namespace arp::editor
