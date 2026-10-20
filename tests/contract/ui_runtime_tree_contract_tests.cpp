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

const char* treeDocument()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas:
  id: tree
  width: 320
  height: 180
  dpi: 96
root:
  id: root
  type: Panel
  layer: root
  rect: { x: 0, y: 0, width: 320, height: 180 }
  style: root
  state: enabled
  event: none
  children:
    - id: play_button
      type: Button
      layer: play
      rect: { x: 10, y: 10, width: 80, height: 24 }
      style: button.primary
      state: enabled
      event: command.play
      text: Play
    - id: options_panel
      type: Panel
      layer: options
      rect: { x: 10, y: 40, width: 120, height: 72 }
      style: panel.options
      state: enabled
      event: none
      children:
        - id: volume_slider
          type: Slider
          layer: volume
          rect: { x: 8, y: 8, width: 96, height: 20 }
          style: slider.volume
          state: enabled
          event: command.volume
)yaml";
}

const char* duplicateIdDocument()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas:
  id: duplicate
  width: 320
  height: 180
  dpi: 96
root:
  id: root
  type: Panel
  layer: root
  rect: { x: 0, y: 0, width: 320, height: 180 }
  style: root
  state: enabled
  event: none
  children:
    - id: duplicate
      type: Button
      layer: first
      rect: { x: 0, y: 0, width: 80, height: 24 }
      style: button
      state: enabled
      event: first
    - id: duplicate
      type: Button
      layer: second
      rect: { x: 0, y: 32, width: 80, height: 24 }
      style: button
      state: enabled
      event: second
)yaml";
}

void document_runtime_tree_preserves_order_and_source_id_lookup()
{
    const auto path = std::filesystem::path("artifacts/ui_runtime_tree/tree.arpui");
    writeFile(path, treeDocument());

    arp::ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = path;
    const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
    assert(loaded.ok());
    assert(loaded.runtime().controlCount() == 4u);

    const auto traversal = loaded.runtime().canvas().traversal();
    assert(traversal.size() == 5u);
    assert(traversal[0] == loaded.runtime().rootId());
    assert(traversal[1] == loaded.widgetIdForSourceNodeId("root"));
    assert(traversal[2] == loaded.widgetIdForSourceNodeId("play_button"));
    assert(traversal[3] == loaded.widgetIdForSourceNodeId("options_panel"));
    assert(traversal[4] == loaded.widgetIdForSourceNodeId("volume_slider"));

    assert(loaded.runtime().canvas().findWidget(loaded.widgetIdForSourceNodeId("play_button"))
        != nullptr);
    assert(!loaded.widgetIdForSourceNodeId("missing").valid());
}

void duplicate_source_ids_fail_deterministically()
{
    const auto path = std::filesystem::path("artifacts/ui_runtime_tree/duplicate.arpui");
    writeFile(path, duplicateIdDocument());

    arp::ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = path;
    const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(desc);
    assert(!loaded.ok());
    assert(loaded.diagnostic().code == arp::ui::UiDiagnosticCode::RuntimeLoadFailed);
    assert(loaded.diagnostic().message.find("unique") != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    document_runtime_tree_preserves_order_and_source_id_lookup();
    duplicate_source_ids_fail_deterministically();
    return 0;
}
