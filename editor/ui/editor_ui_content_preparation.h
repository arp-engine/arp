#pragma once

#include "editor/ui/editor_ui_content.h"

namespace arp::editor {
// Editor-owned preparation boundary. The native and Web source resources supply
// explicit profile semantics; this never probes the filesystem or a font list.
struct EditorUiContentProfile {
    const char* path;
    const char* guiPublication;
    bool allowFontAliases = false;
};

struct EditorUiContentMounts {
    core::PoolHandle ui, fonts;
};

struct EditorUiContentBindings {
    core::pmr::String document, atlas;
    std::array<core::pmr::String, 4> fontPaths, families;
    std::array<std::size_t, 4> fontBudgets{};
};

class EditorUiContentPreparation final {
public:
    static bool parse(const resource::ResourceLoadContext&,
        EditorUiContentMounts,
        const EditorUiContentProfile&,
        EditorUiContentBindings&,
        core::pmr::String& diagnostic);
    // Caller prepares a private candidate and publishes it only on success.
    static bool prepare(const resource::ResourceLoadContext&,
        EditorUiContentMounts,
        const EditorUiContentBindings&,
        EditorUiContent&,
        core::pmr::String& diagnostic);
};
} // namespace arp::editor
