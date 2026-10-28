#include "editor/tests/fixtures/script_inspector_fixture.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;
using namespace arp::editor::testfixtures;
using F = ScriptInspectorFixture;
using Y = core::YamlContract;

void selectEmpty(EditorShell& shell, F& files)
{
    files.open(shell);
    F::command(shell, SelectSceneObjectCommand{files.other.value()});
}

const scene::Component* attached(EditorShell& shell, scene::GameObjectId owner)
{
    for (const auto& component : shell.sceneDocument().scene().findObject(owner)->components())
        if (!component->scriptInstance().is_undefined())
            return component.get();
    return nullptr;
}

EditorResponse confirm(EditorShell& shell)
{
    return shell.submitCommand(ActivateControlCommand(
        F::name(shell, F::control(shell, {}, ui::UiControlType::Button, "Attach script"))));
}

void attachment(F& files)
{
    core::SharedPtr<const EditorScriptComponentCatalog> retained;
    {
        EditorShell shell;
        selectEmpty(shell, files);
        retained = shell.sceneDocument().scriptCatalog();
        const auto baseline = shell.sceneHistoryUsage().undoEntries;
        F::beginAttachment(shell);
        check(!attached(shell, files.other) && !shell.sceneDirty());
        check(confirm(shell).status != EditorResponseStatus::Completed);
        check(F::label(shell, "Invalid value: amount"));
        F::fillAttachment(shell);
        F::click(shell, "Value", "target");
        F::choose(shell,
            "target value",
            core::pmr::String("Script fields [") + core::formatUnsigned(files.owner.value())
                + ":0]");
        F::click(shell, "Component active: true");
        check(!attached(shell, files.other) && !shell.sceneDirty());
        const auto retired =
            F::name(shell, F::control(shell, "amount", ui::UiControlType::TextInput));
        check(confirm(shell).status == EditorResponseStatus::Completed);
        const auto* component = attached(shell, files.other);
        check(component && !component->enabled());
        const auto id = component->id();
        const auto guard = component->nativeReflectionObject();
        const auto expected =
            EditorScriptComponentArchive(shell.sceneDocument(), EditorSceneScriptPolicy::fields())
                .capture(*component);
        check(Y::uint64(Y::required(Y::parseSingleDocument(expected.fields), "counter"))
            == 9007199254740993LL);
        check(expected.references.size() == 1 && expected.references[0].owner == files.owner);
        check(shell.sceneHistoryUsage().undoEntries == baseline + 1);
        check(shell.submitCommand(SetTextValueCommand(retired, "99")).status
            != EditorResponseStatus::Completed);
        F::command(shell, UndoSceneCommand{});
        check(!attached(shell, files.other) && !guard.native_reference_alive());
        F::command(shell, RedoSceneCommand{});
        component = attached(shell, files.other);
        check(component && component->id() == id && component->nativeReflectionObject() != guard);
        SaveSceneCommand save("attached-ui.yaml");
        save.overwriteExisting = true;
        check(!arp::editor::tests::ScenePersistenceFixture::finish(shell, shell.submitCommand(save))
                  .sceneDirty,
            "save attachment");
        ScriptSceneShellFixture::load(shell, "attached-ui.yaml");
        component = attached(shell, files.other);
        check(component && component->id() == id);
        check(EditorScriptComponentArchive::equal(expected,
            EditorScriptComponentArchive(shell.sceneDocument(), EditorSceneScriptPolicy::fields())
                .capture(*component)));
        check(shell.sceneDocument().scene().name() == "Script Inspector project");
    }
    for (const auto& module : retained->modules().entries())
        check(module.module->liveInstances() == 0);
}

void cancelAndFailure(F& files)
{
    EditorShell shell;
    selectEmpty(shell, files);
    F::beginAttachment(shell);
    F::edit(shell, "amount", "invalid");
    const auto old = F::name(shell, F::control(shell, "amount", ui::UiControlType::TextInput));
    F::click(shell, "Cancel");
    check(!attached(shell, files.other) && !shell.sceneDirty());
    check(shell.submitCommand(SetTextValueCommand(old, "9")).status
        != EditorResponseStatus::Completed);
    F::beginAttachment(shell);
    F::fillAttachment(shell);
    bool ready;
    {
        core::CpuMemoryQuotaScope quota(core::makeShared<core::CpuMemoryQuota>(1));
        ready = shell.retrySceneInspector();
    }
    check(!ready && shell.inspectorState() == EditorShell::InspectorState::Failed);
    check(!attached(shell, files.other) && !shell.sceneDirty());
    check(shell.retrySceneInspector());
    check(shell.runtime()
              .findControlObject(F::control(shell, "amount", ui::UiControlType::TextInput))
              ->value()
              .text
        == "23");
    files.files.write("primary.arp", core::pmr::String(F::source) + "\n# changed\n");
    check(confirm(shell).status != EditorResponseStatus::Completed);
    check(!attached(shell, files.other) && !shell.sceneDirty());
    files.files.write("primary.arp", F::source);
    check(confirm(shell).status == EditorResponseStatus::Completed);
    // Same-type duplicate is rejected by the shared typed command, retaining its draft.
    F::beginAttachment(shell);
    F::fillAttachment(shell);
    check(confirm(shell).status != EditorResponseStatus::Completed);
    check(F::label(shell, "Attachment rejected - draft retained"));
    F::click(shell, "Cancel");
    F::command(shell, SelectSceneObjectCommand{files.owner.value()});
    F::beginAttachment(shell);
    const auto stale = F::name(shell, F::control(shell, "amount", ui::UiControlType::TextInput));
    F::command(shell, SelectSceneObjectCommand{files.other.value()});
    check(!F::label(shell, "New ScriptComponent - not attached"));
    check(shell.submitCommand(SetTextValueCommand(stale, "9")).status
        != EditorResponseStatus::Completed);

    F::command(shell, SelectSceneObjectCommand{files.owner.value()});
    F::beginAttachment(shell);
    const auto guarded = F::name(shell, F::control(shell, "amount", ui::UiControlType::TextInput));
    const auto epoch = shell.submitCommand(GetSceneHierarchyQuery{}).sceneHierarchy.documentEpoch;
    F::command(shell, SetSceneObjectViewCommand{files.owner.value(), epoch, false, true});
    check(shell.submitCommand(SetTextValueCommand(guarded, "12")).status
        != EditorResponseStatus::Completed);
    F::command(shell, SetSceneObjectViewCommand{files.owner.value(), epoch, false, false});
    shell.activate("menu_Scene");
    check(confirm(shell).status != EditorResponseStatus::Completed);
    shell.activate("menu_Scene");
    F::command(shell, DestroySceneObjectCommand{});
    F::command(shell, UndoSceneCommand{});
    F::command(shell, SelectSceneObjectCommand{files.owner.value()});
    check(!F::label(shell, "New ScriptComponent - not attached"));
    check(shell.submitCommand(SetTextValueCommand(guarded, "12")).status
        != EditorResponseStatus::Completed);
}

void cycle(F& files)
{
    attachment(files);
    cancelAndFailure(files);
}

void unsupportedCatalog()
{
    // clang-format off
    F files(R"ARP(


class PlainValue:
    value: int


# This has no component/reference archive contract and must remain unavailable.
class UnsupportedFields(ScriptComponent):
    value: PlainValue
)ARP");
    // clang-format on
    EditorShell shell;
    selectEmpty(shell, files);
    ui::UiControlInteraction select;
    select.kind = ui::UiControlInteractionKind::Click;
    select.widgetId = F::scriptMenu(shell, "Unavailable: primary / UnsupportedFields");
    check(shell.runtime().interact(select).ok());
    check(F::label(shell, "ARP-SCRIPT-COMPONENT-INSPECTION-FIELD-TYPE"));
    check(!attached(shell, files.other) && !shell.sceneDirty());
    check(!F::label(shell, "New ScriptComponent - not attached"));
}
} // namespace

int main(int argc, char** argv)
{
    F files;
    cycle(files);
    unsupportedCatalog();
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i) {
            cycle(files);
            const auto now = core::defaultCpuAllocator().stats();
            check(now.liveBytes == baseline.liveBytes);
            check(now.allocationCount - now.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        }
        std::printf(
            "ordinary_script_attachment_ui_cycles=20 live_bytes_delta=0 live_count_delta=0 retired_instances=0 peak_bytes=%llu\n",
            static_cast<unsigned long long>(core::defaultCpuAllocator().stats().peakBytes));
    }
    std::puts(
        "PASS real dynamic Add catalog, unassigned fields, cancel, initial references, failure/retry, exact history and save/reopen");
}
