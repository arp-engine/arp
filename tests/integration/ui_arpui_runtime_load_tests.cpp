#include "ui/ui_document.h"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

void writeFile(const std::filesystem::path& path, const char* content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    assert(output.good());
    output << content;
}

const char* arpuiFixture()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas:
  id: hud-yaml
  width: 320
  height: 180
  dpi: 96
textures:
  - resourceId: ui/button_atlas
    layer: layer_button
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
    - id: start_button
      type: Button
      layer: layer_button
      rect: { x: 12, y: 16, width: 96, height: 24 }
      style: primary
      state: enabled
      event: submit
      text: Start
)yaml";
}

const char* adaptiveLayoutFixture()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas:
  id: adaptive
  width: 320
  height: 180
  dpi: 96
root:
  id: root
  type: Panel
  layer: root
  rect: { x: 0, y: 0, width: 180, height: 120 }
  style: root
  state: normal
  event: none
  layout: { type: verticalStack, paddingLeft: 10, paddingTop: 8, paddingRight: 10, paddingBottom: 8, spacing: 6 }
  children:
    - id: row
      type: Panel
      layer: row
      rect: { x: 0, y: 0, width: -1, height: 32 }
      style: row
      state: normal
      event: none
      layoutRole: stackItem
      layout: { type: horizontalStack, spacing: 4 }
      children:
        - id: checkbox
          type: Checkbox
          layer: checkbox
          rect: { x: 0, y: 0, width: 24, height: 24 }
          style: check
          state: enabled
          event: toggle
          layoutRole: stackItem
        - id: label
          type: Label
          layer: label
          rect: { x: 0, y: 0, width: 80, height: 20 }
          style: label
          state: enabled
          event: none
          text: Adaptive
          layoutRole: stackItem
    - id: apply_button
      type: Button
      layer: button
      rect: { x: 0, y: 0, width: 96, height: 28 }
      style: button
      state: enabled
      event: submit
      text: Apply
      layoutRole: stackItem
)yaml";
}

const char* overflowingAdaptiveLayoutFixture()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas:
  id: adaptive-overflow
  width: 320
  height: 180
  dpi: 96
root:
  id: root
  type: Panel
  layer: root
  rect: { x: 0, y: 0, width: 120, height: 40 }
  style: root
  state: normal
  event: none
  layout: { type: verticalStack, spacing: 8 }
  children:
    - id: first
      type: Button
      layer: first
      rect: { x: 0, y: 0, width: -1, height: 24 }
      style: button
      state: enabled
      event: first
      text: First
      layoutRole: stackItem
    - id: second
      type: Button
      layer: second
      rect: { x: 0, y: 0, width: -1, height: 24 }
      style: button
      state: enabled
      event: second
      text: Second
      layoutRole: stackItem
)yaml";
}

const char* legacyArpuiFixture()
{
    return R"yaml(
type: ArpUiDocument
contractVersion: arpui.v1
canvas:
  id: legacy
  width: 320
  height: 180
  dpi: 96
nodes:
  - id: 1
    parent: 0
    type: Panel
    layer: root
    rect: { x: 0, y: 0, width: 320, height: 180 }
    style: root
    state: normal
    event: none
)yaml";
}

const char* invalidMissingRootFixture()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas:
  id: missing-root
  width: 320
  height: 180
  dpi: 96
nodes: []
)yaml";
}

const char* invalidMissingNodeMetadataFixture()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas:
  id: missing-node-metadata
  width: 320
  height: 180
  dpi: 96
root:
  id: root
  type: Panel
  layer: root
  rect: { x: 0, y: 0, width: 320, height: 180 }
  state: normal
  event: none
)yaml";
}

const char* arpuiIrYamlFixture()
{
    return R"yaml(
type: ArpUiAuthoringIR
contractVersion: arpui.ir.v1
status: Contract Complete
canvas:
  id: adaptive-ir
  width: 320
  height: 180
  dpi: 96
textures: []
nodes:
  - id: 1
    parent: 0
    type: Panel
    layer: root
    rect: { x: 20, y: 12, width: 180, height: 96 }
    style: panel.character
    state: enabled
    event: none
    layout:
      type: verticalStack
      spacing: 6
      paddingLeft: 10
      paddingTop: 8
      paddingRight: 10
      paddingBottom: 8
  - id: 2
    parent: 1
    type: Label
    layer: title
    rect: { x: 0, y: 0, width: -1, height: 24 }
    style: text.character.title
    state: enabled
    event: none
    text: CHARACTER
    layoutRole: stackItem
  - id: 3
    parent: 1
    type: Button
    layer: close
    rect: { x: 0, y: 0, width: 112, height: 32 }
    style: button.secondary
    state: enabled
    event: command.close_character
    text: CLOSE
    layoutRole: stackItem
)yaml";
}

const char* invalidArpuiIrYamlFixture()
{
    return R"yaml(
type: ArpUiAuthoringIR
contractVersion: arpui.ir.v1
canvas:
  id: bad-ir
  width: 320
  height: 180
  dpi: 96
nodes:
  - id: 1
    parent: 0
    type: Label
    layer: missing-rect
    style: text.character.title
    state: enabled
    event: none
    text: BAD
)yaml";
}

const char* rejectedJsonArpuiFixture()
{
    return R"json({"type":"ui.document","contractVersion":"ui.document/v1","canvas":{"width":1,"height":1,"dpi":96},"root":{"type":"Panel","id":"root"}})json";
}

void loads_arpui_into_document_control_tree_and_render_queue()
{
    const auto path = std::filesystem::path("artifacts/ui_arpui_runtime_load/hud.arpui");
    writeFile(path, arpuiFixture());

    arp::ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = path;
    const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
    assert(loaded.ok());
    assert(loaded.document().canvas().contractVersion
        == arp::core::pmr::String("ui.document/v1", &arp::core::defaultCpuMemoryResource()));
    assert(loaded.document().nodes().size() == 2u);
    assert(loaded.document().textures().size() == 1u);
    assert(loaded.runtime().controlCount() == 2u);

    const auto layout = loaded.runtime().canvas().resolveLayout();
    assert(layout.ok());
    const auto extracted = arp::ui::RenderExtractor().extract(loaded.runtime().canvas(), layout);
    assert(extracted.ok());
    assert(!extracted.queue().exposesNativeBackendHandles());
    assert(!extracted.queue().generatesRawStaticVertexDataInSceneHotPath());
}

void legacy_arpui_contract_is_rejected_deterministically()
{
    const auto path = std::filesystem::path("artifacts/ui_arpui_runtime_load/legacy.arpui");
    writeFile(path, legacyArpuiFixture());

    arp::ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = path;
    const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
    assert(!loaded.ok());
    assert(loaded.diagnostic().code == arp::ui::UiDiagnosticCode::RuntimeLoadFailed);
    assert(loaded.diagnostic().message.find("ui.document/v1") != arp::core::pmr::String::npos);
}

void json_arpui_is_rejected_deterministically()
{
    const auto path = std::filesystem::path("artifacts/ui_arpui_runtime_load/legacy_json.arpui");
    writeFile(path, rejectedJsonArpuiFixture());

    arp::ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = path;
    const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
    assert(!loaded.ok());
    assert(loaded.diagnostic().code == arp::ui::UiDiagnosticCode::RuntimeLoadFailed);
    assert(loaded.diagnostic().message.find("YAML only") != arp::core::pmr::String::npos);
}

void missing_root_and_node_metadata_fail_deterministically()
{
    {
        const auto path =
            std::filesystem::path("artifacts/ui_arpui_runtime_load/missing_root.arpui");
        writeFile(path, invalidMissingRootFixture());
        arp::ui::UiArpuiRuntimeLoadDesc desc;
        desc.path = path;
        const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
        assert(!loaded.ok());
        assert(loaded.diagnostic().message.find("root") != arp::core::pmr::String::npos);
    }
    {
        const auto path =
            std::filesystem::path("artifacts/ui_arpui_runtime_load/missing_node_metadata.arpui");
        writeFile(path, invalidMissingNodeMetadataFixture());
        arp::ui::UiArpuiRuntimeLoadDesc desc;
        desc.path = path;
        const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
        assert(!loaded.ok());
        assert(loaded.diagnostic().message.find("style") != arp::core::pmr::String::npos);
    }
}

void adaptive_stack_layout_computes_child_rects_from_container_metadata()
{
    const auto path = std::filesystem::path("artifacts/ui_arpui_runtime_load/adaptive.arpui");
    writeFile(path, adaptiveLayoutFixture());

    arp::ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = path;
    const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
    assert(loaded.ok());

    const auto layout = loaded.runtime().canvas().resolveLayout();
    assert(layout.ok());
    const auto& row = layout.requireWidget(arp::ui::WidgetId::fromValue(3));
    const auto& checkbox = layout.requireWidget(arp::ui::WidgetId::fromValue(4));
    const auto& label = layout.requireWidget(arp::ui::WidgetId::fromValue(5));
    const auto& button = layout.requireWidget(arp::ui::WidgetId::fromValue(6));
    assert(row.rect.x == 10.0f);
    assert(row.rect.y == 8.0f);
    assert(row.rect.width == 160.0f);
    assert(row.rect.height == 32.0f);
    assert(checkbox.rect.x == 10.0f);
    assert(checkbox.rect.y == 8.0f);
    assert(label.rect.x == 38.0f);
    assert(label.rect.y == 8.0f);
    assert(button.rect.x == 10.0f);
    assert(button.rect.y == 46.0f);
}

void adaptive_stack_layout_overflow_fails_deterministically()
{
    const auto path =
        std::filesystem::path("artifacts/ui_arpui_runtime_load/adaptive_overflow.arpui");
    writeFile(path, overflowingAdaptiveLayoutFixture());

    arp::ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = path;
    const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
    assert(loaded.ok());

    const auto layout = loaded.runtime().canvas().resolveLayout();
    assert(!layout.ok());
    assert(layout.diagnostic().code == arp::ui::UiDiagnosticCode::ImpossibleLayoutConstraint);
}

void arpui_ir_yaml_builds_document_yaml_and_loads_runtime()
{
    const auto path =
        std::filesystem::path("artifacts/ui_arpui_runtime_load/adaptive_ir.arpui.ir.yaml");
    writeFile(path, arpuiIrYamlFixture());

    arp::ui::UiArpuiIrBuildDesc desc;
    desc.path = path;
    const auto built = arp::ui::UiArpuiIrBuilder().build(desc);
    assert(built.ok());
    assert(built.documentYaml().find("type: ui.document") != arp::core::pmr::String::npos);
    assert(built.documentYaml().find("contractVersion: ui.document/v1")
        != arp::core::pmr::String::npos);
    assert(built.documentYaml().find("verticalStack") != arp::core::pmr::String::npos);

    const auto loaded = arp::ui::UiArpuiIrRuntimeLoader().load(desc);
    assert(loaded.ok());
    assert(loaded.document().canvas().canvasId
        == arp::core::pmr::String("adaptive-ir", &arp::core::defaultCpuMemoryResource()));
    assert(loaded.runtime().controlCount() == 3u);

    const auto layout = loaded.runtime().canvas().resolveLayout();
    assert(layout.ok());
    const auto& title = layout.requireWidget(arp::ui::WidgetId::fromValue(3));
    const auto& close = layout.requireWidget(arp::ui::WidgetId::fromValue(4));
    assert(title.rect.x == 30.0f);
    assert(title.rect.y == 20.0f);
    assert(title.rect.width == 160.0f);
    assert(close.rect.x == 30.0f);
    assert(close.rect.y == 50.0f);
}

void arpui_ir_yaml_missing_required_fields_fails_deterministically()
{
    const auto path =
        std::filesystem::path("artifacts/ui_arpui_runtime_load/invalid_ir.arpui.ir.yaml");
    writeFile(path, invalidArpuiIrYamlFixture());

    arp::ui::UiArpuiIrBuildDesc desc;
    desc.path = path;
    const auto built = arp::ui::UiArpuiIrBuilder().build(desc);
    assert(!built.ok());
    assert(built.diagnostic().code == arp::ui::UiDiagnosticCode::RuntimeLoadFailed);
    assert(built.diagnostic().message.find("rect") != arp::core::pmr::String::npos);
}

void missing_arpui_file_fails_deterministically()
{
    arp::ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = "artifacts/ui_arpui_runtime_load/missing.arpui";
    const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
    assert(!loaded.ok());
    assert(loaded.diagnostic().code == arp::ui::UiDiagnosticCode::RuntimeLoadFailed);
}

} // namespace

int main()
{
    loads_arpui_into_document_control_tree_and_render_queue();
    legacy_arpui_contract_is_rejected_deterministically();
    json_arpui_is_rejected_deterministically();
    missing_root_and_node_metadata_fail_deterministically();
    adaptive_stack_layout_computes_child_rects_from_container_metadata();
    adaptive_stack_layout_overflow_fails_deterministically();
    arpui_ir_yaml_builds_document_yaml_and_loads_runtime();
    arpui_ir_yaml_missing_required_fields_fails_deterministically();
    missing_arpui_file_fails_deterministically();
    return 0;
}
