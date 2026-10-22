#include "core/memory/cpu_allocator.h"
#include "scene/scene.h"
#include "ui/ui.h"
#include "ui/ui_document.h"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path artifactDir()
{
    auto path = std::filesystem::path("artifacts/scene_ui_document_component");
    std::filesystem::create_directories(path);
    return path;
}

void writeText(const std::filesystem::path& path, const char* text)
{
    std::ofstream file(path, std::ios::binary);
    assert(file.good());
    file << text;
}

const char* validDocumentYaml()
{
    return R"(type: ui.document
contractVersion: ui.document/v1
metadata:
  id: scene-hud
canvas:
  id: scene-hud
  width: 320
  height: 180
  dpi: 96
root:
  id: root
  type: Panel
  layer: root
  rect: { x: 0, y: 0, width: 320, height: 180 }
  style: root
  state: normal
  event: none
  children:
    - id: start
      type: Button
      text: Start
      layer: start
      rect: { x: 16, y: 12, width: 96, height: 24 }
      style: primary
      state: enabled
      event: submit
)";
}

void mounts_document_component_and_produces_runtime_handle()
{
    const auto path = artifactDir() / "scene_hud.arpui";
    writeText(path, validDocumentYaml());

    arp::ui::DocumentComponentDesc desc;
    desc.documentSource = arp::core::toPmrString(path.string());
    desc.resourceId =
        arp::core::pmr::String("ui.scene_hud", &arp::core::defaultCpuMemoryResource());
    desc.layer = 3u;
    desc.inputRoute =
        arp::core::pmr::String("main-pointer", &arp::core::defaultCpuMemoryResource());
    desc.renderRoute = arp::core::pmr::String("hud-rhi", &arp::core::defaultCpuMemoryResource());

    arp::scene::Scene scene("scene-ui-document-component");
    auto& object = scene.createObject("hud");
    auto& component = object.addComponent<arp::ui::DocumentComponent>(desc);

    const auto mounted = arp::ui::DocumentComponentMounter().mount(component);
    assert(mounted.ok());
    assert(component.runtimeHandle().valid());
    assert(component.runtimeRootWidget().valid());
    assert(component.mountedControlCount() == 2u);
    assert(component.mountedDrawRecordCount() >= 1u);
    assert(component.desc().layer == 3u);
    assert(component.desc().resourceId == desc.resourceId);
    assert(component.desc().inputRoute == desc.inputRoute);
    assert(component.desc().renderRoute == desc.renderRoute);
    assert(!component.exposesChildUiNodesAsSceneComponents());
}

void diagnostics_are_deterministic_for_missing_source_invalid_contract_and_route()
{
    arp::ui::DocumentComponentDesc missingSource;
    missingSource.resourceId =
        arp::core::pmr::String("ui.missing", &arp::core::defaultCpuMemoryResource());
    missingSource.inputRoute =
        arp::core::pmr::String("input", &arp::core::defaultCpuMemoryResource());
    missingSource.renderRoute =
        arp::core::pmr::String("render", &arp::core::defaultCpuMemoryResource());
    arp::ui::DocumentComponent missingSourceComponent(missingSource);
    const auto missingSourceMounted =
        arp::ui::DocumentComponentMounter().mount(missingSourceComponent);
    assert(!missingSourceMounted.ok());
    assert(missingSourceMounted.diagnostic().code == arp::ui::UiDiagnosticCode::RuntimeLoadFailed);

    const auto invalidPath = artifactDir() / "invalid.arpui";
    writeText(invalidPath, "type: ArpUiDocument\ncontractVersion: arpui.v1\n");
    arp::ui::DocumentComponentDesc invalidContract;
    invalidContract.documentSource = arp::core::toPmrString(invalidPath.string());
    invalidContract.resourceId =
        arp::core::pmr::String("ui.invalid", &arp::core::defaultCpuMemoryResource());
    invalidContract.inputRoute =
        arp::core::pmr::String("input", &arp::core::defaultCpuMemoryResource());
    invalidContract.renderRoute =
        arp::core::pmr::String("render", &arp::core::defaultCpuMemoryResource());
    arp::ui::DocumentComponent invalidContractComponent(invalidContract);
    const auto invalidMounted = arp::ui::DocumentComponentMounter().mount(invalidContractComponent);
    assert(!invalidMounted.ok());
    assert(invalidMounted.diagnostic().code == arp::ui::UiDiagnosticCode::RuntimeLoadFailed);

    arp::ui::DocumentComponentDesc missingRoute;
    missingRoute.documentSource =
        arp::core::toPmrString((artifactDir() / "scene_hud.arpui").string());
    missingRoute.resourceId =
        arp::core::pmr::String("ui.no_route", &arp::core::defaultCpuMemoryResource());
    missingRoute.inputRoute =
        arp::core::pmr::String("input", &arp::core::defaultCpuMemoryResource());
    arp::ui::DocumentComponent missingRouteComponent(missingRoute);
    const auto missingRouteMounted =
        arp::ui::DocumentComponentMounter().mount(missingRouteComponent);
    assert(!missingRouteMounted.ok());
    assert(missingRouteMounted.diagnostic().code == arp::ui::UiDiagnosticCode::RuntimeLoadFailed);
}

void anchor_components_reference_ids_by_value()
{
    const auto widget = arp::ui::WidgetId::fromValue(42u);
    arp::ui::WorldAnchorComponentDesc worldDesc;
    worldDesc.widget = widget;
    worldDesc.pixelSize = {24.0f, 8.0f};
    worldDesc.viewportOffset = {1.0f, 2.0f};

    arp::ui::WorldAnchorComponent world(worldDesc);
    arp::ui::World2DAnchorComponent world2d(worldDesc);
    arp::ui::ScreenAnchorComponent screen(widget);
    assert(world.desc().widget == widget);
    assert(world2d.desc().widget == widget);
    assert(screen.widget() == widget);
}

} // namespace

int main()
{
    mounts_document_component_and_produces_runtime_handle();
    diagnostics_are_deterministic_for_missing_source_invalid_contract_and_route();
    anchor_components_reference_ids_by_value();
    return 0;
}
