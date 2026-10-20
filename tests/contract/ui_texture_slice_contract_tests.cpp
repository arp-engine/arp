#include "tools/asset_importer/asset_importer.h"
#include "ui/ui_document.h"

#include <cassert>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::ui::UiDocumentDesc documentWithTexture()
{
    arp::ui::UiDocumentDesc desc;
    desc.canvas.canvasId = text("hud");
    desc.canvas.contractVersion = text("arpui.v1");
    desc.canvas.width = 256.0f;
    desc.canvas.height = 128.0f;
    desc.canvas.hasExplicitSize = true;
    desc.canvas.designDpi = 96.0f;
    desc.canvas.hasExplicitDpi = true;

    arp::ui::UiDocumentNodeDesc root;
    root.nodeId = arp::ui::UiDocumentNodeId::fromValue(1);
    root.controlType = arp::ui::UiControlType::Panel;
    root.layout = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 256.0f, 128.0f);
    root.styleId = text("root");
    root.stateId = text("normal");
    root.eventRole = text("none");
    root.psdLayerBinding.sourceLayerId = text("layer_root");
    desc.nodes.push_back(root);

    arp::ui::UiDocumentTextureEntry texture;
    texture.resourceId = text("ui/button_atlas");
    texture.sourceLayerId = text("layer_button");
    texture.atlasId = text("hud_atlas");
    texture.sourceRect = arp::ui::LayoutRect::fromOriginSize(8.0f, 16.0f, 64.0f, 24.0f);
    texture.pivot = {0.5f, 0.5f};
    texture.nineSlice = {4.0f, 4.0f, 4.0f, 4.0f};
    texture.alphaMode = arp::ui::UiTextureAlphaMode::Straight;
    texture.srgb = true;
    texture.hasExplicitSrgb = true;
    texture.mipIntent = arp::ui::UiTextureMipIntent::None;
    texture.compressionIntent = arp::ui::UiTextureCompressionIntent::UiLossless;
    texture.samplerIntent = arp::ui::UiTextureSamplerIntent::LinearClamp;
    desc.textures.push_back(texture);
    return desc;
}

void texture_slice_metadata_validates_deterministically()
{
    const arp::ui::UiDocument document(documentWithTexture());
    const auto validation = arp::ui::UiDocumentValidator().validate(document);
    assert(validation.ok());
    assert(document.textures().size() == 1u);
    assert(document.textures()[0].resourceId == text("ui/button_atlas"));
    assert(document.textures()[0].nineSlice.left == 4.0f);
    assert(document.stableHash() != 0u);

    auto repeated = documentWithTexture();
    const arp::ui::UiDocument repeatedDocument(repeated);
    assert(document.stableHash() == repeatedDocument.stableHash());

    repeated.textures[0].nineSlice.left = 8.0f;
    const arp::ui::UiDocument changed(repeated);
    assert(document.stableHash() != changed.stableHash());
}

void missing_texture_metadata_fails_deterministically()
{
    arp::ui::UiDocumentValidator validator;

    {
        auto desc = documentWithTexture();
        desc.textures[0].sourceLayerId.clear();
        const auto result = validator.validate(arp::ui::UiDocument(desc));
        assert(!result.ok());
        assert(
            result.diagnostic().code == arp::ui::UiDocumentDiagnosticCode::InvalidTextureMetadata);
    }
    {
        auto desc = documentWithTexture();
        desc.textures[0].alphaMode = arp::ui::UiTextureAlphaMode::Unknown;
        const auto result = validator.validate(arp::ui::UiDocument(desc));
        assert(!result.ok());
        assert(
            result.diagnostic().code == arp::ui::UiDocumentDiagnosticCode::InvalidTextureMetadata);
    }
    {
        auto desc = documentWithTexture();
        desc.textures[0].hasExplicitSrgb = false;
        const auto result = validator.validate(arp::ui::UiDocument(desc));
        assert(!result.ok());
        assert(
            result.diagnostic().code == arp::ui::UiDocumentDiagnosticCode::InvalidTextureMetadata);
    }
}

void importer_manifest_requires_explicit_texture_intent()
{
    const auto imported = arp::tools::asset_importer::importUiFromPsdManifest(
        R"json({"type":"MockPsdLayerTree","canvas":{"width":256,"height":128,"dpi":96},"layers":[{"id":"layer_root","name":"Root","kind":"group"},{"id":"layer_button","name":"Button","kind":"pixel"}]})json",
        R"json({"type":"ArpUiManifest","canvasId":"hud","contractVersion":"arpui.v1","textures":[{"resourceId":"ui/button_atlas","layer":"layer_button","atlas":"hud_atlas","x":8,"y":16,"width":64,"height":24,"pivotX":0.5,"pivotY":0.5,"sliceLeft":4,"sliceTop":4,"sliceRight":4,"sliceBottom":4,"alpha":"straight","srgb":true,"mips":"none","compression":"ui-lossless","sampler":"linear-clamp"}],"nodes":[{"id":1,"type":"Panel","layer":"layer_root","x":0,"y":0,"width":256,"height":128,"style":"root","state":"normal","event":"none"}]})json");
    assert(imported.success);
    assert(imported.document.textures().size() == 1u);
    assert(imported.document.textures()[0].samplerIntent
        == arp::ui::UiTextureSamplerIntent::LinearClamp);

    const auto missingIntent = arp::tools::asset_importer::importUiFromPsdManifest(
        R"json({"type":"MockPsdLayerTree","canvas":{"width":256,"height":128,"dpi":96},"layers":[{"id":"layer_root","name":"Root","kind":"group"},{"id":"layer_button","name":"Button","kind":"pixel"}]})json",
        R"json({"type":"ArpUiManifest","canvasId":"hud","contractVersion":"arpui.v1","textures":[{"resourceId":"ui/button_atlas","layer":"layer_button","atlas":"hud_atlas","x":8,"y":16,"width":64,"height":24}],"nodes":[{"id":1,"type":"Panel","layer":"layer_root","x":0,"y":0,"width":256,"height":128,"style":"root","state":"normal","event":"none"}]})json");
    assert(!missingIntent.success);
    assert(missingIntent.diagnostics.find("texture") != std::pmr::string::npos);
}

} // namespace

int main()
{
    texture_slice_metadata_validates_deterministically();
    missing_texture_metadata_fails_deterministically();
    importer_manifest_requires_explicit_texture_intent();
    return 0;
}
