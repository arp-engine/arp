#include "editor/ui/editor_ui_assets.h"

#include "assets/image_decoder.h"
#include "core/io/file_io.h"
#include "editor/ui/editor_atlas_layout.h"
#include "editor/workspace/editor_shell.h"
#include "ui/ui_font.h"

#include <initializer_list>

namespace arp::editor {
namespace {
std::filesystem::path firstExistingFont(std::initializer_list<const char*> paths)
{
    for (const auto* path : paths)
        if (std::filesystem::is_regular_file(path))
            return path;
    return {};
}

} // namespace

EditorUiAssets EditorUiAssets::failure(const char* stage, const core::pmr::String& diagnostic)
{
    EditorUiAssets result;
    result.stage_ = stage;
    result.diagnostic_ = diagnostic;
    return result;
}

EditorUiAssets EditorUiAssets::build(EditorShell& shell,
    const std::filesystem::path& root,
    const core::pmr::Vector<renderer::ui::UiDrawCommand>& commands)
{
    using namespace arp;
    auto file = core::readBinaryFile(root / "editor_atlas.png", 16 * 1024 * 1024);
    if (!file.success)
        return failure("ATLAS-FILE", file.diagnostics);
    auto decoded = assets::decodeImageToRgba8(file.bytes, "image/png", false);
    if (!decoded.success)
        return failure("ATLAS-DECODE", decoded.diagnostics);
    if (decoded.image.width != editor::editor_atlas::kWidth
        || decoded.image.height != editor::editor_atlas::kBaseHeight)
        return failure("ATLAS-CONTRACT", "base image extent does not match editor atlas layout");
    renderer::GuiAtlasTextureDesc base;
    base.resourceId = "texture/editor-reference";
    base.extent = {decoded.image.width, decoded.image.height};
    base.rgba8 = std::move(decoded.image.rgba8);
    base.srgb = false;
    core::pmr::Vector<ui::UiFontFaceDesc> fonts;
    ui::UiFontFaceDesc font;
    font.tokenId = "font.editor";
    font.sizePx = 16;
    font.lineHeightPx = 24;
    font.renderMode = "sdf";
    ui::UiFontFaceDesc semibold;
    semibold.tokenId = "font.editor.semibold";
    semibold.sizePx = 16;
    semibold.weight = 600;
    semibold.lineHeightPx = 24;
    semibold.renderMode = "sdf";
    ui::UiFontFaceDesc codeFont;
    codeFont.tokenId = "font.code";
    codeFont.sizePx = 16;
    codeFont.lineHeightPx = 22;
    codeFont.renderMode = "sdf";
    ui::UiFontFaceDesc cjkCodeFont;
    cjkCodeFont.tokenId = "font.code.cjk";
#if defined(__EMSCRIPTEN__)
    font.family = "Roboto";
    font.sourcePath = "/fonts/editor.ttf";
    semibold.family = "Roboto";
    semibold.sourcePath = "/fonts/editor.ttf";
    codeFont.family = "Consolas";
    codeFont.sourcePath = "/fonts/code.ttf";
    cjkCodeFont.family = "DengXian";
    cjkCodeFont.sourcePath = "/fonts/cjk.ttf";
#elif defined(_WIN32)
    font.family = "Segoe UI";
    font.sourcePath =
        firstExistingFont({"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"});
    semibold.family = "Segoe UI Semibold";
    semibold.sourcePath =
        firstExistingFont({"C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/arialbd.ttf"});
    codeFont.family = "Consolas";
    codeFont.sourcePath =
        firstExistingFont({"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf"});
    cjkCodeFont.family = "DengXian";
    cjkCodeFont.sourcePath =
        firstExistingFont({"C:/Windows/Fonts/Deng.ttf", "C:/Windows/Fonts/msyh.ttc"});
#elif defined(__APPLE__)
    font.family = "SF Pro";
    font.sourcePath = firstExistingFont(
        {"/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf"});
    semibold.family = "SF Pro Semibold";
    semibold.sourcePath = firstExistingFont(
        {"/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf"});
    codeFont.family = "Menlo";
    codeFont.sourcePath = firstExistingFont(
        {"/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Supplemental/Courier New.ttf"});
    cjkCodeFont.family = "PingFang SC";
    cjkCodeFont.sourcePath = firstExistingFont({"/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"});
#else
    font.family = "DejaVu Sans";
    font.sourcePath = firstExistingFont({"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"});
    semibold.family = "DejaVu Sans Bold";
    semibold.sourcePath = firstExistingFont({"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf"});
    codeFont.family = "DejaVu Sans Mono";
    codeFont.sourcePath = firstExistingFont({"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf"});
    cjkCodeFont.family = "Noto Sans CJK SC";
    cjkCodeFont.sourcePath =
        firstExistingFont({"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
            "/usr/share/fonts/truetype/arphic/uming.ttc"});
#endif
    if (font.sourcePath.empty() || semibold.sourcePath.empty() || codeFont.sourcePath.empty()
        || cjkCodeFont.sourcePath.empty())
        return failure("FONTS",
            "Required UI fonts were not found. Install the platform sans, monospace and CJK fonts.");
    fonts.push_back(std::move(font));
    fonts.push_back(std::move(semibold));
    // Text runs are laid out against the SDF source size, then scaled to the
    // on-screen font size. Measure at that same source size so token starts
    // and caret stops use the exact rendered monospace advance.
    constexpr std::uint32_t kCodeSdfSampleSize = 48u;
    ui::UiFontRasterizeDesc codeAdvanceDesc;
    codeAdvanceDesc.face = codeFont;
    codeAdvanceDesc.face.sizePx = kCodeSdfSampleSize;
    codeAdvanceDesc.face.lineHeightPx = kCodeSdfSampleSize;
    codeAdvanceDesc.text = "MMMM";
    codeAdvanceDesc.originX = 0.0f;
    codeAdvanceDesc.originY = 0.0f;
    codeAdvanceDesc.maxX = 4096.0f;
    codeAdvanceDesc.maxY = 128.0f;
    const auto measuredCodeAdvance = ui::UiFontTextRasterizer().rasterize(codeAdvanceDesc);
    if (!measuredCodeAdvance.ok())
        return failure("CODE-FONT", measuredCodeAdvance.diagnostic().message);
    shell.setCodeColumnWidth(measuredCodeAdvance.text().advanceX / 4.0f
        * static_cast<float>(codeFont.sizePx) / static_cast<float>(kCodeSdfSampleSize));
    fonts.push_back(std::move(codeFont));
    // Use the static regular face so the requested weight is encoded in
    // the font outline instead of relying on an unapplied variable axis.
    cjkCodeFont.sizePx = 16;
    cjkCodeFont.weight = 400;
    cjkCodeFont.lineHeightPx = 22;
    // Rasterize at display size: downscaled SDF thresholds can erase thin
    // strokes in dense Han glyphs such as 你.
    cjkCodeFont.renderMode = "alpha";
    ui::UiFontRasterizeDesc cjkAdvanceDesc;
    cjkAdvanceDesc.face = cjkCodeFont;
    cjkAdvanceDesc.face.sizePx = kCodeSdfSampleSize;
    cjkAdvanceDesc.face.lineHeightPx = kCodeSdfSampleSize;
    cjkAdvanceDesc.text = "\xE4\xB8\xAD";
    cjkAdvanceDesc.originX = 0.0f;
    cjkAdvanceDesc.originY = 0.0f;
    cjkAdvanceDesc.maxX = 4096.0f;
    cjkAdvanceDesc.maxY = 128.0f;
    const auto measuredCjkAdvance = ui::UiFontTextRasterizer().rasterize(cjkAdvanceDesc);
    if (!measuredCjkAdvance.ok())
        return failure("CODE-CJK-FONT", measuredCjkAdvance.diagnostic().message);
    shell.setCodeCjkColumnWidth(measuredCjkAdvance.text().advanceX
        * static_cast<float>(cjkCodeFont.sizePx) / static_cast<float>(kCodeSdfSampleSize));
    fonts.push_back(std::move(cjkCodeFont));
    auto warm = commands;
    renderer::ui::UiDrawCommand alphabet;
    alphabet.kind = renderer::ui::UiDrawCommandKind::Text;
    alphabet.fontToken = "font.editor";
    alphabet.styleId = "text";
    alphabet.rect = ui::LayoutRect::fromOriginSize(0, 0, 1920, 24);
    alphabet.clipRect = alphabet.rect;
    for (char c = 32; c < 127; ++c)
        alphabet.text += c;
    warm.push_back(std::move(alphabet));
    auto semiboldAlphabet = warm.back();
    semiboldAlphabet.fontToken = "font.editor.semibold";
    warm.push_back(std::move(semiboldAlphabet));
    // Script token text changes on every scroll/edit. Seed its full ASCII
    // glyph set up front so normal scrolling never triggers atlas uploads.
    auto codeAlphabet = warm.back();
    codeAlphabet.fontToken = "font.code";
    codeAlphabet.text.clear();
    for (char c = 32; c < 127; ++c)
        codeAlphabet.text += c;
    warm.push_back(std::move(codeAlphabet));
    renderer::ui::UnifiedUiAtlasRuntimeBuildDesc atlasDesc;
    atlasDesc.baseAtlas = std::move(base);
    atlasDesc.commands = &warm;
    atlasDesc.fontFaces = &fonts;
    atlasDesc.maxWidth = editor::editor_atlas::kWidth;
    atlasDesc.maxHeight = editor::editor_atlas::kGlyphPageHeight;
    atlasDesc.dynamicGlyphBudgetPerFrame = 512;
    atlasDesc.shapingCapability = renderer::ui::TextShapingCapability::BasicUnicodeUtf8;
    auto atlas = renderer::ui::UnifiedUiAtlasRuntime::build(std::move(atlasDesc));
    if (!atlas.ok())
        return failure("ATLAS", atlas.diagnostics());
    if (atlas.atlas().extent.height != editor::editor_atlas::kHeight)
        return failure("ATLAS-CONTRACT",
            "combined image and glyph atlas extent does not match editor atlas layout");
    EditorUiAssets result;
    result.atlas_.emplace(std::move(atlas));
    return result;
}
} // namespace arp::editor
