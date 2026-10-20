#pragma once

namespace arp::tests {
inline const char* uiDocumentFixture()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas:
  id: hud-renderer
  width: 320
  height: 180
  dpi: 96
textures:
  - resourceId: 22222222-2222-4222-8222-222222222222
    layer: layer_icon
    atlas: hud_atlas
    rect: { x: 8, y: 16, width: 64, height: 24 }
    pivotX: 0.5
    pivotY: 0.5
    sliceLeft: 4
    sliceTop: 4
    sliceRight: 4
    sliceBottom: 4
    alpha: straight
    srgb: true
    mips: none
    compression: ui-lossless
    sampler: linear-clamp
root:
  id: root
  type: Panel
  layer: layer_root
  rect: { x: 0, y: 0, width: 320, height: 180 }
  style: root
  state: normal
  event: none
  children:
    - id: score_label
      type: Label
      layer: layer_label
      rect: { x: 12, y: 16, width: 128, height: 24 }
      style: body
      state: enabled
      event: none
      text: Score
    - id: score_icon
      type: Image
      layer: layer_icon
      rect: { x: 148, y: 16, width: 32, height: 32 }
      style: icon
      state: enabled
      event: none
      resourceRole: texture
      resourceId: 22222222-2222-4222-8222-222222222222
)yaml";
}

} // namespace arp::tests
