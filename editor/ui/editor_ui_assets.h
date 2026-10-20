#pragma once

#include "renderer/ui/ui_atlas_runtime.h"

#include <filesystem>
#include <optional>

namespace arp::editor {
class EditorShell;

class EditorUiAssets final {
public:
    static EditorUiAssets build(EditorShell& shell,
        const std::filesystem::path& root,
        const core::pmr::Vector<renderer::ui::UiDrawCommand>& commands);

    bool ok() const { return atlas_.has_value(); }

    const char* stage() const { return stage_; }

    const core::pmr::String& diagnostic() const { return diagnostic_; }

    renderer::ui::UnifiedUiAtlasRuntime& atlas() { return *atlas_; }

private:
    static EditorUiAssets failure(const char* stage, const core::pmr::String& diagnostic);
    std::optional<renderer::ui::UnifiedUiAtlasRuntime> atlas_;
    const char* stage_ = "ASSETS";
    core::pmr::String diagnostic_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::editor
