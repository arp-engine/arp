#pragma once

namespace arp::tests {

inline const char* uiMetadataAtlasFixture()
{
    return R"atlas(type: ui.atlas
contractVersion: ui.atlas/v2
status: Contract Complete
atlasId: test.atlas
resourceId: texture/test.atlas
baseImageAtlas:
  sourcePath: images/atlas.png
  width: 32
  height: 16
  colorSpace: srgb
fontFaces:
  - tokenId: font.test
    family: Test
    sourcePath: fonts/test.ttf
    weight: 400
    lineHeightPx: 16
    fallbackChain: [font.test]
    supportedScripts: [Latin]
    charset: test.ascii
glyphPolicy: { mode: hybrid }
pagePolicy: { mode: singleTexture }
slices:
  - sliceId: panel
    rect: { x: 0, y: 0, width: 32, height: 16 }
)atlas";
}

inline const char* uiMetadataThemeFixture()
{
    return R"theme(type: ui.theme
contractVersion: ui.theme/v1
status: Contract Complete
themeId: test.theme
fontTokens:
  - tokenId: font.test
    family: Test
    sourcePath: fonts/test.ttf
    sizePx: 16
    weight: 400
    lineHeightPx: 16
colorTokens:
  - tokenId: color.panel
    rgba: [0.25, 0.5, 0.75, 1.0]
styleBindings:
  - styleId: style.panel
    baseToken: color.panel
    fontToken: font.test
atlasReferences:
  - resourceId: texture/test.atlas
    metadataPath: authoring/reference.arpui
)theme";
}

} // namespace arp::tests
