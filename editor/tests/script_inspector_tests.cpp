#include "editor/tests/fixtures/script_inspector_fixture.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;
using namespace arp::editor::testfixtures;
using Files = ScriptInspectorFixture;
using Y = core::YamlContract;

EditorResponse action(EditorShell& shell, const char* caption)
{
    return shell.submitCommand(ActivateControlCommand(
        Files::name(shell, Files::control(shell, {}, ui::UiControlType::Button, caption))));
}

void roundTrip(Files& files)
{
    core::SharedPtr<const EditorScriptComponentCatalog> retained;
    reflection::object native, receiver;
    {
        EditorShell shell;
        files.open(shell);
        retained = shell.sceneDocument().scriptCatalog();
        native = files.component(shell).nativeReflectionObject();
        receiver = files.component(shell).scriptInstance();
        const auto original = files.state(shell);
        Files::edit(shell, "amount", "19");
        Files::edit(shell, "counter", "9223372036854775807");
        Files::edit(shell, "speed", "0.12345678901234566");
        Files::edit(shell, "title", "\"中文\\nEdited line\"");
        Files::choose(shell, "visible", "false");
        Files::choose(shell, "enabled", "true");
        check(EditorScriptComponentArchive::equal(original, files.state(shell))
            && !shell.sceneDirty() && shell.sceneHistoryUsage().undoEntries == 0);
        check(action(shell, "Apply fields").status == EditorResponseStatus::Completed);
        const auto after = files.state(shell);
        const auto fields = Y::parseSingleDocument(after.fields);
        check(Y::int32(fields, "amount") == 19 && Y::uint64(fields["counter"]) == INT64_MAX
            && Y::number(fields, "speed") == 0.12345678901234566 && !Y::boolean(fields, "visible")
            && Y::text(fields, "title") == "中文\nEdited line" && Y::boolean(fields, "enabled")
            && files.component(shell).enabled());
        check(shell.sceneHistoryUsage().undoEntries == 1 && shell.sceneDirty()
            && files.component(shell).nativeReflectionObject() == native
            && files.component(shell).scriptInstance() == receiver);
        const auto version = shell.stateVersion();
        check(action(shell, "Apply fields").status == EditorResponseStatus::Completed
            && shell.sceneHistoryUsage().undoEntries == 1 && shell.stateVersion() == version);
        Files::command(shell, UndoSceneCommand{});
        check(EditorScriptComponentArchive::equal(files.state(shell), original)
            && !shell.sceneDirty());
        Files::command(shell, RedoSceneCommand{});
        check(EditorScriptComponentArchive::equal(files.state(shell), after));
        SaveSceneCommand save("inspector.yaml");
        save.overwriteExisting = true;
        ScriptSceneShellFixture::succeeded(
            arp::editor::tests::ScenePersistenceFixture::finish(shell, shell.submitCommand(save)),
            true);
        ScriptSceneShellFixture::load(shell, "inspector.yaml");
        check(!shell.sceneDirty() && !native.native_reference_alive()
            && !receiver.ptr<reflection::instance>()->alive());
        Files::command(shell, SelectSceneObjectCommand{files.owner.value()});
        check(EditorScriptComponentArchive::equal(files.state(shell), after)
            && shell.sceneDocument().scene().name() == "Script Inspector project");
    }
    for (const auto& entry : retained->modules().entries())
        check(entry.module->liveInstances() == 0);
}

void collectionsAndReferences(Files& files)
{
    EditorShell shell;
    files.open(shell);
    Files::click(shell, "Add item (unassigned)", "tags");
    check(action(shell, "Apply fields").status != EditorResponseStatus::Completed
        && shell.sceneHistoryUsage().undoEntries == 0 && !shell.sceneDirty());
    Files::edit(shell, "tags [2]", "\"\"");
    Files::click(shell, "Remove item", "tags [0]");
    Files::click(shell, "Value", "target");
    // A required child remains unassigned instead of picking the first object.
    check(action(shell, "Apply fields").status != EditorResponseStatus::Completed);
    const auto targetName =
        core::pmr::String("Reference target [") + core::formatUnsigned(files.other.value()) + ":0]";
    Files::choose(shell, "target value", targetName);
    check(action(shell, "Apply fields").status == EditorResponseStatus::Completed);
    auto fields = Y::parseSingleDocument(files.state(shell).fields);
    check(Y::sequence(fields, "tags").size() == 2 && Y::text(fields["tags"][0]).empty()
        && Y::text(fields["tags"][1]).empty()
        && Y::uint64(fields["target"]["owner"]) == files.other.value()
        && Y::uint64(fields["target"]["component"]) == 0);
    Files::click(shell, "None", "target");
    Files::click(shell, "Add item (unassigned)", "links");
    check(action(shell, "Apply fields").status != EditorResponseStatus::Completed);
    const auto transform =
        shell.sceneDocument().scene().findObject(files.other)->componentIds().front();
    const auto nativeName = core::pmr::String("Reference target [")
        + core::formatUnsigned(files.other.value()) + ":" + core::formatUnsigned(transform.value())
        + "]";
    Files::choose(shell, "links [0]", nativeName);
    check(action(shell, "Apply fields").status == EditorResponseStatus::Completed);
    fields = Y::parseSingleDocument(files.state(shell).fields);
    check(fields["target"].is_null() && fields["links"].size() == 1
        && Y::uint64(fields["links"][0]["component"]) == transform.value());
    Files::click(shell, "Remove item", "links [0]");
    check(action(shell, "Apply fields").status == EditorResponseStatus::Completed);
    check(Y::parseSingleDocument(files.state(shell).fields)["links"].empty());
    Files::click(shell, "Add item (unassigned)", "choices");
    Files::click(shell, "Create empty list", "choices [0]");
    check(action(shell, "Apply fields").status == EditorResponseStatus::Completed);
    fields = Y::parseSingleDocument(files.state(shell).fields);
    check(fields["choices"].size() == 1 && fields["choices"][0].is_sequence()
        && fields["choices"][0].empty());
    Files::click(shell, "Add item (unassigned)", "choices [0]");
    Files::click(shell, "None", "choices [0] [0]");
    check(action(shell, "Apply fields").status == EditorResponseStatus::Completed);
    check(Y::parseSingleDocument(files.state(shell).fields)["choices"][0][0].is_null());
    Files::click(shell, "Value", "choices [0] [0]");
    check(action(shell, "Apply fields").status != EditorResponseStatus::Completed);
    Files::edit(shell, "choices [0] [0] value", "\"\"");
    check(action(shell, "Apply fields").status == EditorResponseStatus::Completed);
    check(Y::text(Y::parseSingleDocument(files.state(shell).fields)["choices"][0][0]).empty());
}

void failuresAndConflicts(Files& files)
{
    EditorShell shell;
    files.open(shell);
    const auto before = files.state(shell);
    const auto input = Files::control(shell, "amount", ui::UiControlType::TextInput);
    const auto retiredSource = Files::name(shell, input);
    Files::edit(shell, "amount", "wrong");
    check(action(shell, "Apply fields").status != EditorResponseStatus::Completed);
    check(Files::label(shell, "Invalid value: amount"));
    check(EditorScriptComponentArchive::equal(before, files.state(shell)) && !shell.sceneDirty());
    check(shell.runtime().findControlObject(input)->value().text == "wrong");
    check(shell.retrySceneInspector() && Files::label(shell, "Invalid value: amount"));
    check(action(shell, "Reset draft").status == EditorResponseStatus::Completed);
    check(!shell.runtime().findControlObject(input));
    check(shell.submitCommand(SetTextValueCommand(retiredSource, "99")).status
        != EditorResponseStatus::Completed);
    Files::edit(shell, "amount", "31");
    auto fields = Y::parseSingleDocument(before.fields);
    fields["amount"] = 11;
    SetSceneScriptFieldsCommand external(primaryId, "Fields", 1, Y::serialize(fields), true);
    external.objectId = files.owner.value();
    external.componentId = files.componentId.value();
    Files::command(shell, external);
    check(action(shell, "Apply fields").status != EditorResponseStatus::Completed
        && shell.sceneHistoryUsage().undoEntries == 1);
    check(shell.runtime()
              .findControlObject(Files::control(shell, "amount", ui::UiControlType::TextInput))
              ->value()
              .text
        == "31");
    check(action(shell, "Reset draft").status == EditorResponseStatus::Completed);
    check(shell.runtime()
              .findControlObject(Files::control(shell, "amount", ui::UiControlType::TextInput))
              ->value()
              .text
        == "11");
    Files::edit(shell, "amount", "15");
    files.files.write("primary.arp", core::pmr::String(Files::source) + "\n# changed\n");
    check(action(shell, "Apply fields").status != EditorResponseStatus::Completed);
    files.files.write("primary.arp", Files::source);
    const auto apply =
        Files::name(shell, Files::control(shell, {}, ui::UiControlType::Button, "Apply fields"));
    EditorCommandPayload command = ActivateControlCommand(apply);
    {
        core::CpuMemoryQuotaScope quota(core::makeShared<core::CpuMemoryQuota>(1));
        check(!shell.execute(command).success);
    }
    check(Y::int32(Y::parseSingleDocument(files.state(shell).fields), "amount") == 11);
    check(shell.retrySceneInspector());
    check(action(shell, "Apply fields").status == EditorResponseStatus::Completed);
    check(Y::int32(Y::parseSingleDocument(files.state(shell).fields), "amount") == 15);
    const auto oldSource =
        Files::name(shell, Files::control(shell, "amount", ui::UiControlType::TextInput));
    Files::command(shell, SelectSceneObjectCommand{files.other.value()});
    check(shell.submitCommand(SetTextValueCommand(oldSource, "123")).status
        != EditorResponseStatus::Completed);
    Files::command(shell, SelectSceneObjectCommand{files.owner.value()});
    check(Y::int32(Y::parseSingleDocument(files.state(shell).fields), "amount") == 15);
    // Explicit presentation failure, cleanup and retry leave the live fields intact.
    bool ready;
    {
        core::CpuMemoryQuotaScope quota(core::makeShared<core::CpuMemoryQuota>(1));
        ready = shell.retrySceneInspector();
    }
    check(!ready && shell.inspectorState() == EditorShell::InspectorState::Failed);
    check(shell.retrySceneInspector()
        && shell.inspectorState() == EditorShell::InspectorState::Ready);
    check(Y::int32(Y::parseSingleDocument(files.state(shell).fields), "amount") == 15);
}

void inputGates(Files& files)
{
    EditorShell shell;
    files.open(shell);
    const auto before = files.state(shell);
    auto textSource =
        Files::name(shell, Files::control(shell, "amount", ui::UiControlType::TextInput));
    auto applySource =
        Files::name(shell, Files::control(shell, {}, ui::UiControlType::Button, "Apply fields"));
    const auto epoch = shell.submitCommand(GetSceneHierarchyQuery{}).sceneHierarchy.documentEpoch;
    Files::command(shell, SetSceneObjectViewCommand{files.owner.value(), epoch, false, true});
    check(shell.submitCommand(SetTextValueCommand(textSource, "18")).status
        != EditorResponseStatus::Completed);
    check(shell.submitCommand(ActivateControlCommand(applySource)).status
        != EditorResponseStatus::Completed);
    Files::command(shell, SetSceneObjectViewCommand{files.owner.value(), epoch, false, false});
    textSource = Files::name(shell, Files::control(shell, "amount", ui::UiControlType::TextInput));
    // Modal and group selection reject the same source through typed ingress.
    shell.activate("menu_Scene");
    check(shell.submitCommand(SetTextValueCommand(textSource, "18")).status
        != EditorResponseStatus::Completed);
    shell.activate("menu_Scene");
    SelectSceneObjectsCommand selection;
    selection.primaryObjectId = files.owner.value();
    selection.objectIds = {files.owner.value(), files.other.value()};
    Files::command(shell, selection);
    check(shell.submitCommand(SetTextValueCommand(textSource, "18")).status
        != EditorResponseStatus::Completed);
    Files::command(shell, SelectSceneObjectCommand{files.owner.value()});
    check(EditorScriptComponentArchive::equal(before, files.state(shell))
        && shell.sceneHistoryUsage().undoEntries == 0);
    textSource = Files::name(shell, Files::control(shell, "amount", ui::UiControlType::TextInput));
    // Deleting and restoring the same numeric component ID creates a new guard.
    RemoveSceneComponentCommand remove;
    remove.objectId = files.owner.value();
    remove.componentId = files.componentId.value();
    Files::command(shell, remove);
    Files::command(shell, UndoSceneCommand{});
    check(shell.submitCommand(SetTextValueCommand(textSource, "18")).status
        != EditorResponseStatus::Completed);
    textSource = Files::name(shell, Files::control(shell, "amount", ui::UiControlType::TextInput));
    ScriptSceneShellFixture::load(shell);
    check(shell.submitCommand(SetTextValueCommand(textSource, "18")).status
        != EditorResponseStatus::Completed);
}

void draftAllocationFailure(Files& files)
{
    EditorShell shell;
    files.open(shell);
    Files::edit(shell, "amount", "17");
    const auto textSource =
        Files::name(shell, Files::control(shell, "amount", ui::UiControlType::TextInput));
    EditorCommandPayload edit = SetTextValueCommand(textSource, "123456789012345678901234567890");
    {
        core::CpuMemoryQuotaScope quota(core::makeShared<core::CpuMemoryQuota>(1));
        check(!shell.execute(edit).success);
    }
    check(shell.inspectorState() == EditorShell::InspectorState::Failed && !shell.sceneDirty());
    check(shell.retrySceneInspector());
    check(shell.runtime()
              .findControlObject(Files::control(shell, "amount", ui::UiControlType::TextInput))
              ->value()
              .text
        == "17");
    check(action(shell, "Apply fields").status == EditorResponseStatus::Completed);
    check(Y::int32(Y::parseSingleDocument(files.state(shell).fields), "amount") == 17);
}

void cycle(Files& files)
{
    roundTrip(files);
    collectionsAndReferences(files);
    failuresAndConflicts(files);
    inputGates(files);
    draftAllocationFailure(files);
}
} // namespace

int main(int argc, char** argv)
{
    Files files;
    cycle(files);
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
            "ordinary_script_inspector_cycles=20 live_bytes_delta=0 live_count_delta=0 retired_instances=0 peak_bytes=%llu\n",
            static_cast<unsigned long long>(core::defaultCpuAllocator().stats().peakBytes));
    }
    std::puts(
        "PASS real script field widgets, exact scalars, collections/references, draft/Apply/Reset, conflicts, history and save/reopen");
}
