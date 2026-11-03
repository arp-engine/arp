#include "assets/image_decoder.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/filesystem_path.h"
#include "editor/ui/editor_atlas_layout.h"
#include "editor/ui/editor_ui_content_preparation.h"
#include "resource/resource_source_io.h"
#include "ui/ui_font_source.h"

#include <algorithm>

namespace arp::editor {
namespace {
using Limits = EditorUiContentLimits;
using Y = core::YamlContract;
} // namespace

bool EditorUiContentPreparation::parse(const resource::ResourceLoadContext& context,
    EditorUiContentMounts mounts,
    const EditorUiContentProfile& profile,
    EditorUiContentBindings& result,
    core::pmr::String& diagnostic)
{
    const resource::ResourceFileOrigin origin{core::io::FilePathScope::MountedResource, mounts.ui};
    auto input =
        resource::readResourceText(profile.path, context, Limits::maxProfileBytes, &origin);
    if (!input.success) {
        diagnostic = input.diagnostics;
        return false;
    }
    try {
        const auto node = Y::parseSingleDocument(input.text);
        Y::onlyKeys(node, {"type", "contractVersion", "document", "atlas", "guiShader", "fonts"});
        if (Y::text(node, "type") != "arp.editor-ui-sources"
            || Y::uint32(node, "contractVersion") != 1
            || Y::text(node, "guiShader") != profile.guiPublication) {
            diagnostic = "EDITOR-UI-SOURCE-PROFILE-CONTRACT";
            return false;
        }
        result.document = Y::text(node, "document");
        result.atlas = Y::text(node, "atlas");
        const auto& fonts = Y::sequence(node, "fonts");
        if (fonts.size() != result.fontPaths.size())
            Y::fail();
        std::array<bool, 4> seen{};
        for (const auto& font : fonts) {
            Y::onlyKeys(font, {"token", "family", "path", "maxBytes"});
            const auto token = Y::text(font, "token");
            const auto found =
                std::find(Limits::fontTokens.begin(), Limits::fontTokens.end(), token);
            if (found == Limits::fontTokens.end())
                Y::fail();
            const auto index = static_cast<std::size_t>(found - Limits::fontTokens.begin());
            if (seen[index])
                Y::fail();
            seen[index] = true;
            result.fontPaths[index] = Y::text(font, "path");
            result.families[index] = Y::text(font, "family");
            const auto maximum = Y::uint64(Y::required(font, "maxBytes"));
            if (!maximum || maximum > Limits::maxFileBytes)
                Y::fail();
            result.fontBudgets[index] = static_cast<std::size_t>(maximum);
            if (result.families[index].empty() || result.families[index].size() > 128
                || result.families[index].find('\0') != core::pmr::String::npos)
                Y::fail();
        }
        core::pmr::Vector<core::pmr::String> paths{
            profile.path, result.document, result.atlas, profile.guiPublication};
        for (const auto& path : result.fontPaths)
            paths.push_back(path);
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (paths[i].size() > 512 || !core::FilePath::isSafeRelativePath(paths[i]))
                Y::fail();
            for (std::size_t j = 0; j < i; ++j)
                if (paths[i] == paths[j]) {
                    // Only an explicitly declared Web font alias may share a file.
                    if (!profile.allowFontAliases || i < 4 || j < 4)
                        Y::fail();
                    const auto a = i - 4, b = j - 4;
                    if (result.families[a] != result.families[b]
                        || result.fontBudgets[a] != result.fontBudgets[b])
                        Y::fail();
                }
        }
        return true;
    } catch (...) {
        diagnostic = "EDITOR-UI-SOURCE-PROFILE-INVALID";
        return false;
    }
}

bool EditorUiContentPreparation::prepare(const resource::ResourceLoadContext& context,
    EditorUiContentMounts mounts,
    const EditorUiContentBindings& binding,
    EditorUiContent& candidate,
    core::pmr::String& diagnostic)
{
    if (!context.requirePreloadedSources) {
        diagnostic = "EDITOR-UI-SOURCES-PRELOADED-REQUIRED";
        return false;
    }
    candidate.documentPath = binding.document;
    candidate.documentOrigin = {core::io::FilePathScope::MountedResource, mounts.ui};
    auto text = resource::readResourceText(
        binding.document, context, Limits::maxDocumentBytes, &candidate.documentOrigin);
    if (!text.success) {
        diagnostic = text.diagnostics;
        return false;
    }
    candidate.documentYaml = std::move(text.text);
    auto document = ui::UiArpuiRuntimeLoader().load(candidate.document());
    if (!document.ok()) {
        diagnostic = document.diagnostic().message;
        return false;
    }
    auto image = resource::readResourceSource(
        binding.atlas, context, Limits::maxAtlasBytes, false, &candidate.documentOrigin);
    if (!image.success) {
        diagnostic = image.diagnostics;
        return false;
    }
    auto decoded = assets::decodeImageToRgba8(image.bytes, "image/png", false);
    if (!decoded.success || decoded.image.width != editor_atlas::kWidth
        || decoded.image.height != editor_atlas::kBaseHeight) {
        diagnostic = "EDITOR-UI-SOURCE-ATLAS-CONTRACT";
        return false;
    }
    candidate.baseAtlas.resourceId = "texture/editor-reference";
    candidate.baseAtlas.extent = {decoded.image.width, decoded.image.height};
    candidate.baseAtlas.rgba8 = std::move(decoded.image.rgba8);
    candidate.baseAtlas.srgb = false;
    const resource::ResourceFileOrigin fontOrigin{
        core::io::FilePathScope::MountedResource, mounts.fonts};
    for (std::size_t i = 0; i < candidate.fonts.size(); ++i) {
        auto& face = candidate.fonts[i];
        face = {};
        face.tokenId = Limits::fontTokens[i];
        face.family = binding.families[i];
        face.sourcePath = std::filesystem::u8path(binding.fontPaths[i]);
        face.maxSourceBytes = binding.fontBudgets[i];
        for (std::size_t j = 0; j < i; ++j)
            if (binding.fontPaths[i] == binding.fontPaths[j])
                face.source = candidate.fonts[j].source;
        if (!face.source) {
            auto bytes = resource::readResourceSource(
                binding.fontPaths[i], context, binding.fontBudgets[i], false, &fontOrigin);
            if (!bytes.success) {
                diagnostic = bytes.diagnostics;
                return false;
            }
            ui::UiFontSourceDesc source;
            source.path = binding.fontPaths[i];
            source.pathScope = fontOrigin.pathScope;
            source.mount = fontOrigin.mount;
            source.maxBytes = binding.fontBudgets[i];
            face.source =
                ui::UiFontSource::create(std::move(source), std::move(bytes.bytes), diagnostic);
            if (!face.source)
                return false;
        }
        // These are the original Editor's authored token styles.
        face.sizePx = 16;
        face.lineHeightPx = i < 2 ? 24 : 22;
        face.weight = i == 1 ? 600 : 400;
        face.renderMode = i == 3 ? "alpha" : "sdf";
        auto loaded = ui::UiFontFaceLoader().load(face);
        if (!loaded.ok()) {
            diagnostic = loaded.diagnostic().message;
            return false;
        }
    }
    return candidate.valid(diagnostic);
}

bool EditorUiContent::valid(core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    if (documentYaml.empty() || !documentOrigin.mount || documentPath.empty()
        || baseAtlas.resourceId != "texture/editor-reference" || baseAtlas.srgb
        || baseAtlas.extent.width != editor_atlas::kWidth
        || baseAtlas.extent.height != editor_atlas::kBaseHeight
        || baseAtlas.rgba8.size()
            != std::size_t(editor_atlas::kWidth) * editor_atlas::kBaseHeight * 4) {
        diagnostic = "EDITOR-UI-SOURCES-INCOMPLETE";
        return false;
    }
    for (std::size_t i = 0; i < fonts.size(); ++i)
        if (fonts[i].tokenId != Limits::fontTokens[i]
            || !ui::validatePreparedUiFontFace(fonts[i], diagnostic)) {
            if (diagnostic.empty())
                diagnostic = "EDITOR-UI-SOURCE-FONT-TOKEN";
            return false;
        }
    return true;
}

ui::UiArpuiRuntimeLoadDesc EditorUiContent::document() const
{
    ui::UiArpuiRuntimeLoadDesc result;
    result.path = std::filesystem::u8path(documentPath);
    result.sourceYaml = documentYaml;
    result.sourceYamlProvided = true;
    result.maxDocumentBytes = Limits::maxDocumentBytes;
    result.maxWidgetCount = Limits::maxStaticWidgets;
    result.sourceContext.requirePreloadedSources = true;
    result.origin = documentOrigin;
    return result;
}

std::size_t EditorUiContent::processedByteCount() const noexcept
{
    auto bytes = documentYaml.size() + baseAtlas.rgba8.size();
    for (std::size_t i = 0; i < fonts.size(); ++i) {
        if (!fonts[i].source)
            continue;
        bool alias = false;
        for (std::size_t j = 0; j < i; ++j)
            alias |= fonts[i].source == fonts[j].source;
        if (!alias)
            bytes += fonts[i].source->bytes().size();
    }
    return bytes;
}

} // namespace arp::editor
