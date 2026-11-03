#include "editor/app/editor_ui_source_host.h"

#include "editor/app/editor_startup_resource_host.h"

#include <cstdio>

namespace arp::editor {
bool EditorUiSourceHost::prepare(const std::filesystem::path& uiRoot,
    const std::filesystem::path& fontRoot,
    const std::filesystem::path& shaderRoot,
    std::optional<std::string_view> baseUrl,
    core::SharedPtr<const EditorUiSources>& output,
    core::pmr::String& diagnostic)
{
    if (baseUrl && baseUrl->empty()) {
        diagnostic = "EDITOR-UI-SOURCE-URL-EMPTY";
        return false;
    }
    try {
        resource::ResourceManager manager;
        const std::array<EditorStartupMountDesc, 3> mounts{
            {{"editor-ui", uiRoot, baseUrl, EditorUiSourceLimits::maxFileBytes},
                {"editor-fonts", fontRoot, baseUrl, EditorUiSourceLimits::maxFileBytes},
                {"editor-gui", shaderRoot, baseUrl, EditorUiSourceLimits::maxFileBytes}}};
        EditorStartupResourceHost host({mounts.data(), mounts.size()});
        resource::ResourceGraphExecutionResult result;
        if (!host.execute(manager,
                EditorUiSources::request({host.mount(0), host.mount(1), host.mount(2)}),
                result,
                diagnostic))
            return false;
        auto candidate = EditorUiSources::capture(manager);
        if (!candidate) {
            diagnostic = "EDITOR-UI-SOURCE-CANDIDATE";
            return false;
        }
        if (!candidate->valid(diagnostic))
            return false;
        std::printf(
            "editorUiSource: { profile: NativeEditorUiSources/1, files: %llu, bytes: %llu, ioRetired: true }\n",
            static_cast<unsigned long long>(result.telemetry.submittedFileReads),
            static_cast<unsigned long long>(result.telemetry.bytesRead));
        output = std::move(candidate);
        diagnostic.clear();
        return true;
    } catch (...) {
        diagnostic = "EDITOR-UI-SOURCE-PREPARATION";
        return false;
    }
}
} // namespace arp::editor
