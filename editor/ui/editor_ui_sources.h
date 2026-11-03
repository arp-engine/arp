#pragma once

#include "editor/ui/editor_ui_content.h"
#include "renderer/shader_package_reflection.h"
#include "resource/resource_graph_executor.h"
#include "ui/ui_document.h"
#include "ui/ui_font.h"

#include <array>

namespace arp::editor {

struct EditorUiSourceMounts {
    core::PoolHandle ui, fonts, shaders;
};

// NativeEditorUiSources/1. These are explicit startup admissions, not inferred
// resource semantics or claims about the parser's whole-process memory peak.
struct EditorUiSourceLimits : EditorUiContentLimits {
    static constexpr const char* profile = "windows-ui.sources.yaml";
    static constexpr const char* guiPublication = "gui_overlay.artifact-set.yaml";
};

struct EditorUiSources final : EditorUiContent {
    renderer::ShaderPackagePreparationDesc guiShader;

    bool valid(core::pmr::String& diagnostic) const;
    static resource::ResourceGraphExecutionRequest request(
        EditorUiSourceMounts, core::runtime::CancellationToken = {});
    static core::SharedPtr<const EditorUiSources> capture(const resource::ResourceManager&);
};
} // namespace arp::editor
