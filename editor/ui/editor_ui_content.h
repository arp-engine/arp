#pragma once

#include "renderer/render_frame.h"
#include "resource/resource_manager.h"
#include "ui/ui_document.h"
#include "ui/ui_font.h"

#include <array>

namespace arp::editor {
// Shared authored UI inputs; no native or Web GPU publication is implied.
struct EditorUiContentLimits {
    static constexpr std::size_t maxProfileBytes = 65536;
    static constexpr std::size_t maxDocumentBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t maxAtlasBytes = 16u * 1024u * 1024u;
    static constexpr std::size_t maxFileBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t maxShaderFileBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t maxSourceBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t maxPayloadBytes = 128u * 1024u * 1024u;
    static constexpr std::size_t maxStaticWidgets = 8192;
    static constexpr std::array<const char*, 4> fontTokens{
        "font.editor", "font.editor.semibold", "font.code", "font.code.cjk"};
};

struct EditorUiContent {
    core::pmr::String documentPath, documentYaml;
    resource::ResourceFileOrigin documentOrigin;
    renderer::GuiAtlasTextureDesc baseAtlas{core::pmr::String{&core::defaultCpuMemoryResource()}};
    std::array<ui::UiFontFaceDesc, 4> fonts;

    bool valid(core::pmr::String& diagnostic) const;
    ui::UiArpuiRuntimeLoadDesc document() const;
    std::size_t processedByteCount() const noexcept;
};
} // namespace arp::editor
