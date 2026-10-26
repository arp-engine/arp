#include "core/text/string_utils.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_control_input.h"
#include "editor/validation/editor_inspector_picker_smoke.h"
#include "editor/workspace/editor_enum_popup_layout.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::editor;
constexpr const char* scenarios[] = {"picker_first",
    "picker_last",
    "picker_repaired",
    "picker_invalid",
    "picker_objects",
    "picker_object_repaired"};

void layoutContract()
{
    using L = EditorEnumPopupLayout;
    const auto bounds = ui::LayoutRect::fromOriginSize(0, 0, 100, 200);
    auto anchor = ui::LayoutRect::fromOriginSize(70, 40, 20, 10);
    const auto below = L::prepare(bounds, anchor, 80, 40, 20, 32, 6);
    assert(below.ok() && below.rows == 6 && below.rect.x == 60 && below.rect.y == 50
        && below.rect.height == 120);
    anchor.y = 170;
    const auto above = L::prepare(bounds, anchor, 80, 40, 20, 32, 6);
    assert(above.ok() && above.rows == 6 && above.rect.y == 50);
    anchor.y = 90;
    anchor.height = 20;
    const auto partial = L::prepare(bounds, anchor, -10, 150, 20, 32, 6);
    assert(partial.ok() && partial.rows == 4 && partial.rect.x == 0 && partial.rect.width == 100
        && partial.rect.y == 110 && partial.rect.height == 80);
    assert(L::prepare(bounds, anchor, 0, 40, 20, 2, 6).rows == 2);
    const auto tight = ui::LayoutRect::fromOriginSize(0, 0, 100, 30);
    assert(
        !L::prepare(tight, ui::LayoutRect::fromOriginSize(0, 10, 100, 10), 0, 100, 24, 32, 6).ok());
    for (const float invalid : {0.f,
             -1.f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN()}) {
        assert(!L::prepare(bounds, anchor, 0, 40, invalid, 32, 6).ok());
        assert(!L::prepare(bounds, anchor, 0, invalid, 20, 32, 6).ok());
        auto broken = bounds;
        broken.height = invalid;
        assert(!L::prepare(broken, anchor, 0, 40, 20, 32, 6).ok());
    }
    anchor.x = -1;
    assert(!L::prepare(bounds, anchor, 0, 40, 20, 32, 6).ok());
    anchor.x = 70;
    assert(!L::prepare(bounds, anchor, 0, 40, 20, 0, 6).ok());
    assert(!L::prepare(bounds, anchor, 0, 40, 20, 32, 0).ok());
    assert(!L::prepare(bounds, anchor, std::numeric_limits<float>::infinity(), 40, 20, 32, 6).ok());
    std::puts(
        "PASS popup whole-row placement, upward/downward, horizontal bounds and invalid/no-space inputs");
}

void run(const arp::editor::tests::RecoveryFixture& files, std::string_view scenario)
{
    arp::editor::EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    arp::core::pmr::String diagnostic;
    const auto ready = arp::editor::EditorInspectorPickerSmoke::run(shell, scenario, diagnostic);
    if (!ready)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(ready);
    if (scenario == "picker_repaired") {
        const auto snapshot = shell.submitCommand(GetSceneInspectorQuery{});
        const auto property = std::find_if(snapshot.sceneInspector.properties.begin(),
            snapshot.sceneInspector.properties.end(),
            [](const auto& value) {
                return value.name == "spriteResourceId";
            });
        assert(property != snapshot.sceneInspector.properties.end());
        const auto chosen = property->stringValue;
        SetSceneComponentPropertyCommand set("spriteResourceId");
        set.objectId = property->objectId;
        set.componentId = property->componentId;
        set.valueKind = ScenePropertyValueKind::ResourceReference;
        set.stringValue = "00000000-0000-4000-8000-000000000003";
        assert(shell.submitCommand(std::move(set)).status == EditorResponseStatus::Completed);
        assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
        const auto restored = shell.submitCommand(GetSceneInspectorQuery{});
        const auto actual = std::find_if(restored.sceneInspector.properties.begin(),
            restored.sceneInspector.properties.end(),
            [](const auto& value) {
                return value.name == "spriteResourceId";
            });
        assert(actual != restored.sceneInspector.properties.end() && actual->stringValue == chosen);
        assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
    }
}

void replacementAndReopen(const arp::editor::tests::RecoveryFixture& files)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    core::pmr::String diagnostic;
    assert(EditorInspectorPickerSmoke::run(shell, "picker_first", diagnostic));
    const auto snapshot = shell.submitCommand(GetSceneInspectorQuery{});
    const auto property = std::find_if(snapshot.sceneInspector.properties.begin(),
        snapshot.sceneInspector.properties.end(),
        [](const auto& value) {
            return value.name == "spriteResourceId";
        });
    const auto source = scenePropertyControlSource(*property);
    assert(EditorControlInput::click(shell, source));
    assert(!EditorControlInput::find(shell, source + "_popup").valid());
    assert(EditorControlInput::click(shell, source));
    const auto retired = EditorControlInput::find(shell, source + "_option_0");
    assert(retired.valid());
    assert(shell.submitCommand(CreateSceneObjectCommand("New selection")).status
        == EditorResponseStatus::Completed);
    for (const auto id : shell.runtime().canvas().traversal())
        assert(!core::startsWith(
                   shell.runtime().canvas().findWidget(id)->desc().name, source + "_option_")
            && shell.runtime().canvas().findWidget(id)->desc().name != source + "_popup");
    assert(!shell.runtime().interact(ui::UiControlInteraction::click(retired)).ok());
    assert(shell.submitCommand(SelectSceneObjectCommand{property->objectId}).status
        == EditorResponseStatus::Completed);
    assert(EditorControlInput::click(shell, source));
    assert(shell.submitCommand(NewSceneCommand{true}).status == EditorResponseStatus::Completed);
    assert(!EditorControlInput::find(shell, source + "_popup").valid());
    assert(
        shell.inspectorState() == EditorShell::InspectorState::Ready && shell.drawCommands().ok());
    std::puts(
        "PASS popup close/reopen, selection and Scene replacement retire all row labels and stale input");
}

void popupAllocationFailures(const arp::editor::tests::RecoveryFixture& files)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    core::pmr::String diagnostic;
    assert(EditorInspectorPickerSmoke::run(shell, "picker_first", diagnostic));
    const auto snapshot = shell.submitCommand(GetSceneInspectorQuery{});
    const auto property = std::find_if(snapshot.sceneInspector.properties.begin(),
        snapshot.sceneInspector.properties.end(),
        [](const auto& value) {
            return value.name == "spriteResourceId";
        });
    assert(property != snapshot.sceneInspector.properties.end());
    const auto source = scenePropertyControlSource(*property);
    assert(EditorControlInput::click(shell, source));
    const auto count = shell.runtime().controlCount();
    unsigned refused = 0;
    for (const std::uint64_t budget : {1u, 16384u, 65536u, 262144u}) {
        const auto id = EditorControlInput::find(shell, source);
        auto* dropdown = dynamic_cast<EditorEnumDropdown*>(shell.runtime().findControlObject(id));
        assert(dropdown);
        ui::UiEventContext context(shell.runtime(), id);
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(dropdown->applyInteraction(ui::UiControlInteraction::click(id), context).ok());
        }
        refused += shell.inspectorState() == EditorShell::InspectorState::Failed;
        assert(shell.retrySceneInspector() && shell.runtime().controlCount() == count);
        assert(shell.stateVersion() == snapshot.stateVersion);
    }
    assert(refused);
    std::puts(
        "PASS bounded reference popup allocation failure cleanup/retry and unchanged Scene version");
}
} // namespace

int main(int argc, char** argv)
{
    arp::editor::tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        for (const auto scenario : scenarios)
            run(files, scenario);
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned cycle = 0; cycle < 20; ++cycle) {
            run(files, scenarios[cycle % std::size(scenarios)]);
            const auto current = core::defaultCpuAllocator().stats();
            assert(current.liveBytes == baseline.liveBytes
                && current.allocationCount - current.deallocationCount
                    == baseline.allocationCount - baseline.deallocationCount);
        }
        std::puts(
            "PASS 20 ordinary warmed popup open/scroll/selection/destruction cycles: core live bytes/count delta=0");
    } else {
        layoutContract();
        for (const auto scenario : scenarios)
            run(files, scenario);
        popupAllocationFailures(files);
        replacementAndReopen(files);
    }
}
