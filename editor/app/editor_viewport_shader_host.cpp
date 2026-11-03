#include "editor/app/editor_viewport_shader_host.h"

#include "editor/app/editor_startup_resource_host.h"

#include <cstdio>

namespace arp::editor {
bool EditorViewportShaderHost::prepare(const std::filesystem::path& root,
    std::optional<std::string_view> baseUrl,
    EditorViewportShaderSources& output,
    core::pmr::String& diagnostic)
{
    if (baseUrl && baseUrl->empty()) {
        diagnostic = "EDITOR-VIEWPORT-SHADER-URL-EMPTY";
        return false;
    }
    try {
        resource::ResourceManager manager;
        const EditorStartupMountDesc mount{
            "editor-viewport-shaders", root, baseUrl, EditorViewportShaderSources::maxFileBytes};
        EditorStartupResourceHost host({&mount, 1});
        resource::ResourceGraphExecutionResult result;
        if (!host.execute(
                manager, EditorViewportShaderSources::request(host.mount(0)), result, diagnostic))
            return false;
        auto candidate = EditorViewportShaderSources::capture(manager);
        if (!candidate.valid()) {
            diagnostic = "EDITOR-VIEWPORT-SHADER-CANDIDATE";
            return false;
        }
        std::printf(
            "editorViewportShaderSource: { profile: NativeViewportSources/1, files: %llu, bytes: %llu, ioRetired: true }\n",
            static_cast<unsigned long long>(result.telemetry.submittedFileReads),
            static_cast<unsigned long long>(result.telemetry.bytesRead));
        output = std::move(candidate);
        diagnostic.clear();
        return true;
    } catch (...) {
        diagnostic = "EDITOR-VIEWPORT-SHADER-ALLOCATION";
        return false;
    }
}
} // namespace arp::editor
