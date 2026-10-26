#include "editor/validation/editor_resource_navigation_smoke.h"

#include "editor/tests/fixtures/resource_navigation_fixture.h"
#include "editor/validation/editor_control_input.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace arp::editor {
bool EditorResourceNavigationSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-RESOURCE-NAVIGATION-SMOKE";
    if (scenario != "nav_valid" && scenario != "nav_current" && scenario != "nav_missing"
        && scenario != "nav_unmapped" && scenario != "nav_failed" && scenario != "nav_retry")
        return false;
    const auto root = shell.projectRoot() / "build"
        / ("resource-navigation-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const tests::ResourceNavigationFixture workspace(root);
    auto* mesh = workspace.prepare(shell);
    if (scenario == "nav_unmapped") {
        mesh->set_material_resource_id("00000000-0000-4000-8000-000000009999");
        if (!shell.retrySceneInspector())
            return false;
    }
    const auto source = tests::ResourceNavigationFixture::source(shell);
    const auto selected = shell.sceneDocument().selectedObject();
    const auto dirty = shell.sceneDirty();
    const auto camera = shell.sceneViewportCamera();
    const auto version = shell.stateVersion();
    if (scenario == "nav_current")
        mesh->set_material_resource_id(tests::ResourceNavigationFixture::alternateId);
    if (scenario == "nav_missing")
        std::filesystem::remove(workspace.first);
    if (scenario != "nav_unmapped" && !EditorControlInput::click(shell, source)) {
        diagnostic = "EDITOR-RESOURCE-NAVIGATION-SMOKE-CLICK";
        return false;
    }
    if (scenario == "nav_failed" || scenario == "nav_retry") {
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope scope(quota);
            if (shell.retryFileBrowserPresentation())
                return false;
        }
        if (!shell.filesPresentationFailed() || quota->liveBytes())
            return false;
        EditorInputCommand wheel;
        wheel.kind = EditorInputKind::Wheel;
        wheel.x = 100;
        wheel.y = 800;
        wheel.wheelDelta = -1;
        (void)shell.submitCommand(wheel);
        for (const auto* row : layout::kFileSlots)
            if (shell.runtime().canvas().findWidget(shell.id(row))->visible())
                return false;
        if (scenario == "nav_retry" && !EditorControlInput::click(shell, "file_navigation_retry"))
            return false;
    }
    const bool navigated = scenario != "nav_missing" && scenario != "nav_unmapped";
    const auto expected = scenario == "nav_current" ? workspace.second : workspace.first;
    if ((navigated ? shell.selectedFilePath() != expected : !shell.selectedFilePath().empty())
        || shell.sceneDocument().selectedObject() != selected || shell.sceneDirty() != dirty
        || shell.sceneViewportCamera().target().x != camera.target().x
        || shell.filesPresentationFailed() != (scenario == "nav_failed")
        || (scenario != "nav_retry"
            && shell.stateVersion()
                != version + unsigned(navigated) + unsigned(scenario == "nav_failed"))) {
        diagnostic = "EDITOR-RESOURCE-NAVIGATION-SMOKE-STATE";
        return false;
    }
    const auto draws = shell.drawCommands();
    if (!draws.ok())
        return false;
    const auto* action =
        shell.runtime().canvas().findWidget(EditorControlInput::find(shell, source));
    if (!action
        || action->desc().text
            != (scenario == "nav_unmapped" ? "No definition mapping" : "Locate definition")
        || (scenario == "nav_unmapped"
            && (action->desc().acceptsFocus || action->desc().acceptsPointerInput)))
        return false;
    const auto drawn = [&](ui::WidgetId id) {
        return std::any_of(draws.commands().begin(), draws.commands().end(), [&](const auto& draw) {
            return draw.widgetId == id;
        });
    };
    if (!drawn(action->id()))
        return false;
    if (scenario == "nav_missing") {
        if (!drawn(shell.id("file_definition_unavailable"))
            || !drawn(shell.id("file_definition_retry_hint")))
            return false;
    }
    if (scenario == "nav_failed") {
        if (!drawn(shell.id("file_navigation_failed")) || !drawn(shell.id("file_navigation_retry")))
            return false;
    } else if (navigated) {
        bool found = false;
        for (const auto* row : layout::kFileSlots) {
            const auto* widget = shell.runtime().canvas().findWidget(shell.id(row));
            if (widget && widget->desc().text == core::FilePath::utf8(expected.filename())
                && drawn(widget->id()))
                found = true;
        }
        if (!found)
            return false;
    }
    std::printf(
        "PASS resource navigation %.*s exact definition selected=%d, Scene/Inspector/dirty/camera retained\n",
        int(scenario.size()),
        scenario.data(),
        int(navigated));
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
