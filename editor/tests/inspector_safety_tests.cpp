#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_shell.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::editor;

void load(EditorShell& shell, const tests::RecoveryFixture& files)
{
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    shell.activate("workspace_3D");
    assert(shell.submitCommand(CreateSceneObjectCommand("Inspector target")).status
        == EditorResponseStatus::Completed);
    assert(shell.inspectorState() == EditorShell::InspectorState::Ready);
}

void allocationFailures()
{
    tests::RecoveryFixture files;
    unsigned rejected = 0;
    for (const std::uint64_t limit : {1u, 4096u, 16384u, 65536u, 262144u}) {
        EditorShell shell;
        load(shell, files);
        const auto object = shell.sceneDocument().selectedObject();
        const auto count = shell.runtime().controlCount();
        core::pmr::String before, diagnostic;
        assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), before, diagnostic));
        auto quota = core::makeShared<core::CpuMemoryQuota>(limit);
        bool ready = false;
        {
            core::CpuMemoryQuotaScope scope(quota);
            ready = shell.retrySceneInspector();
        }
        if (!ready) {
            ++rejected;
            assert(shell.inspectorState() == EditorShell::InspectorState::Failed);
            assert(
                std::string_view(shell.inspectorFailure()) == "EDITOR-INSPECTOR-ALLOCATION-FAILED");
            assert(!shell.runtime().canvas().findWidget(shell.id("inspector_content"))->visible());
            assert(
                shell.runtime().canvas().findWidget(shell.id("scene_inspector_retry"))->visible());
            assert(!shell.acceptsInspectorInput("scene_name"));
            shell.entryChanged("scene_name", "stale input");
            shell.activate("scene_active");
            assert(shell.sceneDocument().scene().findObject(object)->name() == "Inspector target");
            assert(shell.sceneDocument().scene().findObject(object)->active());
        }
        assert(shell.retrySceneInspector());
        assert(shell.runtime().controlCount() == count);
        assert(!shell.runtime().canvas().findWidget(shell.id("scene_inspector_retry"))->visible());
        core::pmr::String after;
        assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, diagnostic));
        assert(before == after && shell.sceneDocument().selectedObject() == object);
        // Successful retry must allow real editing again.
        shell.entryChanged("scene_name", "After retry");
        assert(shell.sceneDocument().scene().findObject(object)->name() == "After retry");
        std::printf(
            "PASS Inspector quota=%llu: failed=%u, cleanup/retry control baseline preserved\n",
            static_cast<unsigned long long>(limit),
            unsigned(!ready));
    }
    assert(rejected >= 2);
}

void retiredWidgetAndScene()
{
    tests::RecoveryFixture files;
    EditorShell shell;
    load(shell, files);
    auto properties =
        shell.sceneDocument().inspectorProperties(shell.sceneDocument().selectedObject());
    const auto source = scenePropertyControlSource(properties.front()) + "_x";
    auto* content = shell.runtime().canvas().findWidget(shell.id("inspector_content"));
    ui::WidgetId retired;
    for (const auto id : content->childIds())
        if (shell.runtime().canvas().findWidget(id)->desc().name == source)
            retired = id;
    assert(retired.valid() && shell.acceptsInspectorInput(source, retired));
    assert(shell.retrySceneInspector());
    assert(!shell.runtime().findControlObject(retired)
        && !shell.acceptsInspectorInput(source, retired));
    // Native document replacement may reuse numeric IDs; the epoch still rejects.
    shell.sceneDocument() = EditorSceneDocument(false);
    assert(!shell.acceptsInspectorInput("scene_name"));
    assert(shell.retrySceneInspector());
    std::puts("PASS Inspector retired Widget IDs and replaced Scene epoch reject stale inputs");
}

void popupFailure()
{
    tests::RecoveryFixture files;
    unsigned rejected = 0;
    for (const std::uint64_t limit : {1u, 16384u, 65536u, 262144u}) {
        EditorShell shell;
        load(shell, files);
        const auto count = shell.runtime().controlCount();
        const auto id = shell.id("scene_component_add");
        auto* dropdown = dynamic_cast<EditorEnumDropdown*>(shell.runtime().findControlObject(id));
        assert(dropdown);
        ui::UiEventContext context(shell.runtime(), id);
        ui::UiControlInteraction click;
        click.kind = ui::UiControlInteractionKind::Click;
        click.widgetId = id;
        auto quota = core::makeShared<core::CpuMemoryQuota>(limit);
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(dropdown->applyInteraction(click, context).ok());
        }
        if (shell.inspectorState() == EditorShell::InspectorState::Failed) {
            ++rejected;
            assert(!shell.acceptsInspectorInput("scene_component_add"));
        }
        assert(shell.retrySceneInspector() && shell.runtime().controlCount() == count);
    }
    assert(rejected);
    std::puts(
        "PASS Inspector popup allocation failure quarantine, source/widget cleanup and retry baseline");
}

void lifetime()
{
    tests::RecoveryFixture files;
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            load(shell, files);
            auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            {
                core::CpuMemoryQuotaScope scope(quota);
                assert(!shell.retrySceneInspector());
            }
            shell.activate("scene_inspector_retry");
            assert(shell.inspectorState() == EditorShell::InspectorState::Ready);
            assert(shell.drawCommands().ok());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary Shell failure/retry/destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime();
    else {
        allocationFailures();
        retiredWidgetAndScene();
        popupFailure();
    }
}
