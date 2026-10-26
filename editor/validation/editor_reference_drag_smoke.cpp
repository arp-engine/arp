#include "editor/validation/editor_reference_drag_smoke.h"

#include "editor/tests/fixtures/resource_navigation_fixture.h"
#include "editor/validation/editor_reference_drag_input.h"

#include <chrono>
#include <cstdio>

namespace arp::editor {
bool EditorReferenceDragSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-REFERENCE-DRAG-SMOKE";
    if (scenario != "drag_hover" && scenario != "drag_assigned" && scenario != "drag_rejected"
        && scenario != "drag_cancelled")
        return false;
    const auto root = shell.projectRoot() / "build"
        / ("reference-drag-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const tests::ResourceNavigationFixture files(root);
    auto* mesh = files.prepare(shell);
    mesh->set_material_resource_id(tests::ResourceNavigationFixture::alternateId);
    if (!shell.retrySceneInspector())
        return false;
    // Locate the alternate envelope, then restore the target value. The source
    // is admitted by the production package reverse map, never by its filename.
    const auto locate = tests::ResourceNavigationFixture::command(shell, *mesh);
    if (shell.submitCommand(locate).status != EditorResponseStatus::Completed)
        return false;
    mesh->set_material_resource_id(files.materialId);
    if (!shell.retrySceneInspector())
        return false;
    ui::WidgetId source;
    for (const auto* slot : layout::kFileSlots) {
        const auto id = shell.id(slot);
        const auto* widget = shell.runtime().canvas().findWidget(id);
        if (widget && widget->visible()
            && widget->desc().text == core::FilePath::utf8(files.second.filename()))
            source = id;
    }
    if (!source.valid())
        return false;
    const auto selected = shell.sceneDocument().selectedObject();
    auto field = EditorReferenceDragInput::field(
        shell, scenario == "drag_rejected" ? "resourceId" : "materialResourceId");
    const auto destination = EditorReferenceDragInput::center(shell, field);
    EditorReferenceDragInput pointer(shell);
    if (!pointer.down(EditorReferenceDragInput::center(shell, source)) || !pointer.move(destination)
        || shell.sceneDocument().selectedObject() != selected)
        return false;
    const bool allowed = scenario != "drag_rejected";
    if (!EditorReferenceDragInput::feedback(
            shell, allowed ? "Release to assign reference" : "Cannot assign here"))
        return false;
    if (scenario == "drag_cancelled") {
        if (!pointer.send(
                platform::WindowInputEvent::Kind::KeyDown, {}, platform::WindowKey::Escape))
            return false;
    }
    if (scenario != "drag_hover" && !pointer.up(destination))
        return false;
    const auto expected = scenario == "drag_assigned"
        ? tests::ResourceNavigationFixture::alternateId
        : files.materialId.c_str();
    if (mesh->material_resource_id() != expected
        || shell.sceneDocument().selectedObject() != selected)
        return false;
    if (scenario == "drag_rejected"
        && !EditorReferenceDragInput::feedback(shell, "Reference edit failed; value kept"))
        return false;
    std::printf("PASS native reference drag %.*s, shared ingress and exact material=%s\n",
        int(scenario.size()),
        scenario.data(),
        mesh->material_resource_id().c_str());
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
