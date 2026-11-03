#pragma once

#include "editor/ui/editor_ui_content.h"
#include "resource/resource_graph_executor.h"
#include "resource/shader_artifact_source.h"
#include "shader/webgl_shader_program_artifact.h"

namespace arp::editor {
struct EditorWebStartupMounts {
    core::PoolHandle ui, fonts, shaders;
};

// The paths and finite admissions form WebEditorStartupSources/1. They refer to
// original compiler publications, never native GUI aliases or guessed files.
struct EditorWebStartupLimits : EditorUiContentLimits {
    static constexpr const char* profile = "web-ui.sources.yaml";
    static constexpr std::array<const char*, 4> programs{"gui_unified.webgl-program.yaml",
        "environment/editor_viewport_environment.webgl-program.yaml",
        "sprite/editor_viewport_sprite.webgl-program.yaml",
        "pbr/editor_viewport_pbr.webgl-program.yaml"};
    static constexpr std::array<const char*, 2> modules{
        "sprite/editor_viewport_sprite.graphics.module.yaml",
        "pbr/editor_viewport_pbr.graphics.module.yaml"};
    static constexpr std::size_t maxFiles = 128;
    static constexpr std::size_t maxShaderMetadataBytes = 32u * 1024u * 1024u;
    static shader::WebGlShaderLoadBudget programBudget();
};

class EditorWebStartupSources final {
public:
    explicit EditorWebStartupSources(resource::ResourceFileOrigin shaderOrigin);
    EditorWebStartupSources(const EditorWebStartupSources&) = delete;
    EditorWebStartupSources& operator=(const EditorWebStartupSources&) = delete;

    const EditorUiContent& ui() const noexcept { return ui_; }

    const shader::ShaderArtifactSource& shaders() const noexcept { return projection_; }

    const shader::ShaderReflection& spriteReflection() const noexcept { return reflections_[0]; }

    const shader::ShaderReflection& pbrReflection() const noexcept { return reflections_[1]; }

    const resource::ResourceSourceSet& files() const noexcept { return files_; }

    std::size_t processedByteCount() const noexcept;

    static resource::ResourceGraphExecutionRequest request(
        EditorWebStartupMounts, core::runtime::CancellationToken = {});
    static core::SharedPtr<const EditorWebStartupSources> capture(const resource::ResourceManager&);

    // Called only on a private candidate by the resource admission path.
    bool prepare(const resource::ResourceLoadContext&,
        EditorWebStartupMounts,
        core::pmr::String& diagnostic);

private:
    EditorUiContent ui_;
    resource::ResourceSourceSet files_;
    resource::ResourceLoadContext context_;
    resource::AcquiredShaderArtifactSource projection_;
    std::array<shader::ShaderReflection, 2> reflections_;
};
} // namespace arp::editor
