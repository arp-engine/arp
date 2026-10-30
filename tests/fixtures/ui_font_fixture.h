#pragma once

namespace arp::tests {

// Two explicit tokens share one binary. Acquisition must deduplicate the file,
// while both descriptors retain its immutable source owner.
inline const char* uiFontMetadataFixture()
{
    return R"font(type: ui.atlas
contractVersion: ui.atlas/v2
status: Contract Complete
atlasId: test.font.atlas
resourceId: texture/test.font.atlas
baseImageAtlas: { sourcePath: images/atlas.png, width: 32, height: 16, colorSpace: srgb }
fontFaces:
  - tokenId: font.test
    family: Droid Sans
    sourcePath: fonts/test.ttf
    weight: 400
    lineHeightPx: 20
    sizeRangePx: { min: 16 }
    fallbackChain: [font.test]
    supportedScripts: [Latin]
    charset: test.ascii
  - tokenId: font.other
    family: Droid Sans
    sourcePath: fonts/test.ttf
    weight: 400
    lineHeightPx: 20
    sizeRangePx: { min: 16 }
    fallbackChain: [font.test]
    supportedScripts: [Latin]
    charset: test.ascii
glyphPolicy: { mode: hybrid }
pagePolicy: { mode: singleTexture }
slices:
  - sliceId: panel
    rect: { x: 0, y: 0, width: 32, height: 16 }
)font";
}

} // namespace arp::tests
