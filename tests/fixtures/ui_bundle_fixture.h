#pragma once

#include "core/io/memory_file_backend.h"
#include "renderer/ui/ui_startup_bundle_resource.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/ui_atlas_pixels.h"
#include "tests/fixtures/ui_font_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::tests {

inline const char* uiBundleThemeFixture()
{
    return R"bundleTheme(type: ui.theme
contractVersion: ui.theme/v1
status: Contract Complete
themeId: bundle.theme
fontTokens:
  - tokenId: font.test
    family: Droid Sans
    sourcePath: fonts/test.ttf
    sizePx: 16
    weight: 400
    lineHeightPx: 20
colorTokens:
  - tokenId: color.panel
    rgba: [0.25, 0.5, 0.75, 1.0]
styleBindings:
  - styleId: body
    baseToken: color.panel
    fontToken: font.test
atlasReferences:
  - resourceId: texture/test.font.atlas
    metadataPath: meta/atlas.arpui
)bundleTheme";
}

inline const char* uiBundleDocumentFixture()
{
    return R"bundleDocument(type: ui.document
contractVersion: ui.document/v1
canvas: { id: bundle, width: 320, height: 180, dpi: 96 }
textures:
  - resourceId: 33333333-3333-4333-8333-333333333333
    layer: image
    atlas: test.font.atlas
    rect: { x: 0, y: 0, width: 32, height: 16 }
    pivotX: 0.5
    pivotY: 0.5
    sliceLeft: 0
    sliceTop: 0
    sliceRight: 0
    sliceBottom: 0
    alpha: straight
    srgb: true
    mips: none
    compression: ui-lossless
    sampler: linear-clamp
root:
  id: root
  type: Panel
  layer: root
  rect: { x: 0, y: 0, width: 320, height: 180 }
  style: body
  state: enabled
  event: none
  children:
    - id: label
      type: Label
      layer: text
      rect: { x: 0, y: 0, width: 100, height: 20 }
      style: body
      state: enabled
      event: none
      text: AV
    - id: image
      type: Image
      layer: image
      rect: { x: 0, y: 24, width: 32, height: 16 }
      style: body
      state: enabled
      event: none
      resourceRole: texture
      resourceId: 33333333-3333-4333-8333-333333333333
)bundleDocument";
}

// Formal resources and their explicit graph edges are shared by memory/HTTP
// tests. No native registry entries or GPU payloads are fabricated by the fixture.
class UiBundleFixture final {
public:
    static constexpr const char* atlasId = "11111111-1111-4111-8111-111111111111";
    static constexpr const char* imageId = "22222222-2222-4222-8222-222222222222";
    static constexpr const char* textureId = "33333333-3333-4333-8333-333333333333";
    static constexpr const char* fontId = "44444444-4444-4444-8444-444444444444";
    static constexpr const char* themeId = "55555555-5555-4555-8555-555555555555";
    static constexpr const char* documentId = "66666666-6666-4666-8666-666666666666";
    static constexpr const char* bundleId = "77777777-7777-4777-8777-777777777777";
    static constexpr std::size_t sourceBudget = 2u * 1024u * 1024u;
    static constexpr std::size_t textBudget = 16u * 1024u;

    explicit UiBundleFixture(resource::ResourceFileOrigin origin) : origin_(origin) {}

    ui::UiAtlasMetadataResourceLoadDesc atlas() const
    {
        ui::UiAtlasMetadataResourceLoadDesc desc;
        desc.resourceId = atlasId;
        desc.alias = "ui/atlas";
        desc.status = "Contract Complete";
        desc.atlasMetadataPath = "meta/atlas.arpui";
        desc.expectedAtlasResourceId = "texture/test.font.atlas";
        desc.maxMetadataBytes = textBudget;
        desc.origin = origin_;
        return desc;
    }

    ui::UiAtlasImageResourceLoadDesc image() const
    {
        ui::UiAtlasImageResourceLoadDesc desc;
        desc.resourceId = imageId;
        desc.alias = "ui/image";
        desc.status = "Contract Complete";
        desc.atlasMetadataAlias = "ui/atlas";
        desc.maxEncodedBytes = desc.maxDecodedBytes = textBudget;
        desc.origin = origin_;
        return desc;
    }

    renderer::ui::UiAtlasTextureResourceLoadDesc texture() const
    {
        renderer::ui::UiAtlasTextureResourceLoadDesc desc;
        desc.resourceId = textureId;
        desc.alias = "ui/texture";
        desc.status = "Prototype Complete";
        desc.atlasImageAlias = "ui/image";
        desc.intent.resourceUsage = "ui.atlas.sampled";
        desc.intent.uploadPolicy = "renderer-owned-staging";
        desc.intent.requiredFormat = "Rgba8Unorm";
        desc.intent.requiredDescriptorSet = "UiAtlasSet";
        return desc;
    }

    ui::UiFontSetResourceLoadDesc fonts() const
    {
        ui::UiFontSetResourceLoadDesc desc;
        desc.resourceId = fontId;
        desc.alias = "ui/fonts";
        desc.status = "Contract Complete";
        desc.atlasMetadataAlias = "ui/atlas";
        desc.maxFaces = 2;
        desc.maxFontBytes = desc.maxTotalSourceBytes = sourceBudget;
        desc.origin = origin_;
        return desc;
    }

    ui::UiThemeResourceLoadDesc theme() const
    {
        ui::UiThemeResourceLoadDesc desc;
        desc.resourceId = themeId;
        desc.alias = "ui/theme";
        desc.status = "Contract Complete";
        desc.themePath = "theme.arpui";
        desc.atlasMetadataPath = "meta/atlas.arpui";
        desc.atlasMetadataAlias = "ui/atlas";
        desc.maxThemeBytes = textBudget;
        desc.origin = desc.atlasMetadataOrigin = origin_;
        return desc;
    }

    ui::UiDocumentResourceLoadDesc document() const
    {
        ui::UiDocumentResourceLoadDesc desc;
        desc.resourceId = documentId;
        desc.alias = "ui/document";
        desc.status = "Contract Complete";
        desc.documentPath = "hud.arpui";
        desc.documentContract = "ui.document/v1";
        desc.maxDocumentBytes = textBudget;
        desc.origin = origin_;
        return desc;
    }

    renderer::ui::UiStartupBundleResourceLoadDesc bundle() const
    {
        renderer::ui::UiStartupBundleResourceLoadDesc desc;
        desc.resourceId = bundleId;
        desc.alias = "ui/bundle";
        desc.status = "Contract Complete";
        desc.themeAlias = "ui/theme";
        desc.atlasMetadataAlias = "ui/atlas";
        desc.atlasImageAlias = "ui/image";
        desc.atlasTextureAlias = "ui/texture";
        desc.fontSetAlias = "ui/fonts";
        desc.documentAliases.push_back("ui/document");
        // Two faces share one <=2MiB source; two 32x16 RGBA payloads are retained.
        desc.budget = {1, 2, sourceBudget + 2 * 32 * 16 * 4};
        return desc;
    }

    resource::ResourceSourceSet sources(const core::pmr::Vector<std::uint8_t>& font,
        core::StringView themeText = uiBundleThemeFixture(),
        core::StringView documentText = uiBundleDocumentFixture(),
        core::StringView atlasText = uiFontMetadataFixture()) const
    {
        resource::ResourceSourceSet result;
        const auto add = [&](const char* path, core::StringView bytes) {
            resource::ResourceSourceBuffer source;
            source.path = path;
            source.pathScope = origin_.pathScope;
            source.mount = origin_.mount;
            source.bytes.assign(bytes.begin(), bytes.end());
            assert(result.add(std::move(source)));
        };
        add("meta/atlas.arpui", atlasText);
        add("theme.arpui", themeText);
        add("hud.arpui", documentText);
        add("images/atlas.png",
            {reinterpret_cast<const char*>(uiAtlasPngBytes), sizeof(uiAtlasPngBytes)});
        add("fonts/test.ttf", {reinterpret_cast<const char*>(font.data()), font.size()});
        return result;
    }

    void loadChildren(
        resource::ResourceManager& manager, const resource::ResourceSourceSet& sources) const
    {
        resource::ResourceLoadContext context;
        context.sources = &sources;
        context.requirePreloadedSources = true;
        context.maxDecodedBytes = sourceBudget;
        const auto admit = [&](core::UniquePtr<resource::ResourceBase> child) {
            const auto loaded = manager.loadResource(std::move(child), context);
            if (!loaded.success)
                std::fprintf(stderr, "Bundle child: %s\n", loaded.diagnostics.c_str());
            assert(loaded.success);
        };
        admit(core::makeUnique<ui::UiAtlasMetadataResource>(atlas()));
        admit(core::makeUnique<ui::UiAtlasImageResource>(image()));
        admit(core::makeUnique<renderer::ui::UiAtlasTextureResource>(texture()));
        admit(core::makeUnique<ui::UiFontSetResource>(fonts()));
        admit(core::makeUnique<ui::UiThemeResource>(theme()));
        admit(core::makeUnique<ui::UiDocumentResource>(document()));
    }

    resource::ResourceGraphExecutionRequest graph() const
    {
        resource::ResourceGraphExecutionRequest request;
        request.graph.rootResourceId = bundleId;
        const auto add = [&](core::UniquePtr<resource::ResourceBase> resource,
                             core::pmr::Vector<resource::ResourceDependency> dependencies) {
            resource::ResourceGraphNode node;
            node.resource = std::move(resource);
            node.dependencies = std::move(dependencies);
            request.graph.nodes.push_back(std::move(node));
        };
        // Deliberately reverse the authoring order: the formal graph owns scheduling.
        add(core::makeUnique<renderer::ui::UiStartupBundleResource>(bundle()),
            {{themeId, "ui.theme"},
                {atlasId, "ui.atlas.metadata"},
                {imageId, "ui.atlas.image"},
                {textureId, "ui.atlas.texture"},
                {fontId, "ui.font-set"},
                {documentId, "ui.document"}});
        add(core::makeUnique<ui::UiDocumentResource>(document()), {});
        add(core::makeUnique<ui::UiThemeResource>(theme()), {{atlasId, "ui.atlas.metadata"}});
        add(core::makeUnique<ui::UiFontSetResource>(fonts()),
            {{atlasId, "ui.atlas.metadata.fonts"}});
        add(core::makeUnique<renderer::ui::UiAtlasTextureResource>(texture()),
            {{imageId, "ui.atlas.image"}});
        add(core::makeUnique<ui::UiAtlasImageResource>(image()), {{atlasId, "ui.atlas.metadata"}});
        add(core::makeUnique<ui::UiAtlasMetadataResource>(atlas()), {});
        request.budget = {7, 5, sourceBudget + 4 * textBudget, sourceBudget, 8};
        return request;
    }

    static core::SharedPtr<core::io::MemoryFileBackend> memory(
        const core::pmr::Vector<std::uint8_t>& font)
    {
        auto backend =
            core::makeShared<core::io::MemoryFileBackend>(core::io::MemoryFileBackendDesc{
                8, sourceBudget + 4 * textBudget, 1, sourceBudget, 128});
        for (const auto* path : {"meta", "fonts", "images"})
            assert(backend->registerDirectory(path) == core::io::FileIoStatus::Success);
        const auto add = [&](const char* path, const std::uint8_t* bytes, std::size_t size) {
            assert(backend->registerFile(path, bytes, size) == core::io::FileIoStatus::Success);
        };
        for (const auto& entry : {std::pair{"meta/atlas.arpui", uiFontMetadataFixture()},
                 std::pair{"theme.arpui", uiBundleThemeFixture()},
                 std::pair{"hud.arpui", uiBundleDocumentFixture()}})
            add(entry.first,
                reinterpret_cast<const std::uint8_t*>(entry.second),
                std::string_view(entry.second).size());
        add("images/atlas.png", uiAtlasPngBytes, sizeof(uiAtlasPngBytes));
        add("fonts/test.ttf", font.data(), font.size());
        return backend;
    }

private:
    resource::ResourceFileOrigin origin_;
};

} // namespace arp::tests
