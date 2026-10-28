#include "editor/tests/fixtures/script_scene_shell_fixture.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;
using namespace arp::editor::testfixtures;
using Files = ScriptSceneShellFixture;

RemoveSceneComponentCommand remove(scene::GameObjectId owner, scene::ComponentId component)
{
    RemoveSceneComponentCommand command;
    command.objectId = owner.value();
    command.componentId = component.value();
    return command;
}

void rejected(EditorShell& shell, EditorCommandPayload command)
{
    const auto version = shell.stateVersion();
    const auto history = shell.sceneHistoryUsage();
    const auto response = shell.submitCommand(std::move(command));
    check(response.status != EditorResponseStatus::Completed);
    check(shell.stateVersion() == version
        && shell.sceneHistoryUsage().undoEntries == history.undoEntries
        && shell.sceneHistoryUsage().redoEntries == history.redoEntries);
}

void componentRoundTrip(Files& files)
{
    core::SharedPtr<const EditorScriptComponentCatalog> catalog;
    reflection::object retiredReceiver, retiredNative;
    {
        EditorShell shell;
        files.open(shell);
        catalog = shell.sceneDocument().scriptCatalog();
        auto& scene = shell.sceneDocument().scene();
        const auto root = scene.rootId();
        const auto selection = shell.sceneDocument().selectedObject();
        const auto* original = scene.findComponent(files.scriptId);
        retiredReceiver = original->scriptInstance();
        retiredNative = original->nativeReflectionObject();
        check(shell.sceneHistoryUsage().undoEntries == 0);
        Files::command(shell, remove(root, files.scriptId));
        check(!scene.findComponent(files.scriptId) && shell.sceneDirty()
            && shell.sceneDocument().selectedObject() == selection
            && shell.sceneHistoryUsage().undoEntries == 1);
        check(!retiredReceiver.ptr<reflection::instance>()->alive()
            && !retiredNative.native_reference_alive());
        Files::command(shell, UndoSceneCommand{});
        check(files.amount(shell) == 7 && !shell.sceneDirty()
            && scene.componentIds()[2] == files.scriptId
            && !retiredReceiver.ptr<reflection::instance>()->alive()
            && !retiredNative.native_reference_alive());
        const auto restored = scene.findComponent(files.scriptId)->scriptInstance();
        check(restored != retiredReceiver);
        Files::command(shell, RedoSceneCommand{});
        check(
            !scene.findComponent(files.scriptId) && !restored.ptr<reflection::instance>()->alive());
        Files::command(shell, UndoSceneCommand{});
        SaveSceneCommand save("presence.yaml");
        // Ordinary lifecycle cycles deliberately reuse this exact fixture file.
        save.overwriteExisting = true;
        Files::succeeded(arp::editor::tests::ScenePersistenceFixture::finish(
                             shell, shell.submitCommand(std::move(save))),
            true);
        Files::load(shell, "presence.yaml");
        check(files.amount(shell) == 7 && !shell.sceneDirty());
        check(shell.sceneDocument().scene().name() == "Shell script root");
    }
    for (const auto& entry : catalog->modules().entries())
        check(entry.module->liveInstances() == 0);
    check(!retiredReceiver.ptr<reflection::instance>()->alive());
}

void subtreeRoundTrip(Files& files)
{
    EditorShell shell;
    files.open(shell);
    auto& scene = shell.sceneDocument().scene();
    const auto oldObject = scene.findObject(files.child)->nativeReflectionObject();
    const auto receiver =
        scene.findObject(files.child)->findComponent(files.linkId)->scriptInstance();
    Files::command(shell, SelectSceneObjectCommand{files.child.value()});
    const auto* childBody = shell.sceneDocument().addComponent("RigidBody2DComponent", files.child);
    check(childBody != nullptr);
    scene::ComponentId incoming;
    {
        scene::Scene::IdentityRestoration transaction(scene, {0, 1});
        const auto catalog = shell.sceneDocument().scriptCatalog();
        auto& link = transaction.createScriptComponent(scene.rootId(),
            *catalog->modules().find({secondaryId, "PhysicsLink", 1}),
            {secondaryId, "PhysicsLink", 1},
            scene.components().size());
        incoming = link.id();
        link.scriptInstance().ptr<reflection::instance>()->slot("body") =
            childBody->nativeReflectionObject();
        transaction.prepare();
        transaction.commit();
    }
    rejected(shell, DestroySceneObjectCommand{});
    check(scene.findObject(files.child) && receiver.ptr<reflection::instance>()->alive());
    check(shell.sceneDocument().removeComponent(scene.rootId(), incoming));
    Files::command(shell, DestroySceneObjectCommand{});
    check(!scene.findObject(files.child) && !oldObject.native_reference_alive()
        && !receiver.ptr<reflection::instance>()->alive()
        && shell.sceneDocument().selectedObject() == scene.rootId());
    Files::command(shell, UndoSceneCommand{});
    auto* object = scene.findObject(files.child);
    check(object && shell.sceneDocument().selectedObject() == files.child
        && object->componentIds()[1] == files.linkId);
    check(object->findComponent(files.linkId)
              ->scriptInstance()
              .ptr<reflection::instance>()
              ->slot("body")
        == scene.findComponent(files.bodyId)->nativeReflectionObject());
    check(!oldObject.native_reference_alive() && !receiver.ptr<reflection::instance>()->alive());
    // Exact incoming script references continue to protect native dependencies.
    rejected(shell, remove(scene.rootId(), files.bodyId));
    Files::command(shell, RedoSceneCommand{});
    check(!scene.findObject(files.child));
    Files::command(shell, UndoSceneCommand{});
    check(scene.findObject(files.child) && files.amount(shell) == 7);
}

void conflictsAndFailures(Files& files)
{
    EditorShell shell;
    files.open(shell);
    auto& scene = shell.sceneDocument().scene();
    const auto command = remove(scene.rootId(), files.scriptId);
    files.files.write("primary.arp", core::pmr::String(Fixture::primarySource) + "\n# changed\n");
    rejected(shell, command);
    check(files.amount(shell) == 7 && !shell.sceneDirty());
    files.files.write("primary.arp", Fixture::primarySource);
    Files::command(shell, command);
    check(std::filesystem::remove(files.files.root / "primary.arp"));
    rejected(shell, UndoSceneCommand{});
    check(!scene.findComponent(files.scriptId));
    files.files.write("primary.arp", Fixture::primarySource);
    Files::command(shell, UndoSceneCommand{});
    auto* receiver =
        scene.findComponent(files.scriptId)->scriptInstance().ptr<reflection::instance>();
    receiver->slot("amount") = reflection::object(9);
    rejected(shell, RedoSceneCommand{});
    check(files.amount(shell) == 9);
    receiver->slot("amount") = reflection::object(7);
    // Preparation failure leaves the live receiver and both history branches intact.
    const auto undoCount = shell.sceneHistoryUsage().undoEntries;
    const auto redoCount = shell.sceneHistoryUsage().redoEntries;
    EditorCommandPayload payload = command;
    {
        core::CpuMemoryQuotaScope scope(core::makeShared<core::CpuMemoryQuota>(1));
        check(!shell.execute(payload).success);
    }
    check(files.amount(shell) == 7 && shell.sceneHistoryUsage().undoEntries == undoCount
        && shell.sceneHistoryUsage().redoEntries == redoCount);
    Files::command(shell, RedoSceneCommand{});
    check(!scene.findComponent(files.scriptId));
    Files::command(shell, UndoSceneCommand{});
    check(files.amount(shell) == 7 && !shell.sceneDirty());
}

void fieldsRoundTrip(Files& files)
{
    EditorShell shell;
    files.open(shell);
    auto& scene = shell.sceneDocument().scene();
    SetSceneScriptFieldsCommand edit(primaryId, "Mover", 1, "amount: 19\n", true);
    edit.objectId = scene.rootId().value();
    edit.componentId = files.scriptId.value();
    const auto receiver = scene.findComponent(files.scriptId)->scriptInstance();
    const auto native = scene.findComponent(files.scriptId)->nativeReflectionObject();
    const auto selection = shell.sceneDocument().selectedObject();
    const auto version = shell.stateVersion();
    Files::command(shell, edit);
    check(files.amount(shell) == 19 && shell.sceneDirty() && shell.stateVersion() == version + 1
        && shell.sceneHistoryUsage().undoEntries == 1);
    Files::command(shell, UndoSceneCommand{});
    check(files.amount(shell) == 7 && !shell.sceneDirty());
    const auto noOpVersion = shell.stateVersion();
    auto noOp = edit;
    noOp.fields = "amount: 7\n";
    Files::command(shell, noOp);
    check(shell.stateVersion() == noOpVersion && shell.sceneHistoryUsage().redoEntries == 1);
    auto bad = edit;
    bad.fields = "amount: wrong\n";
    rejected(shell, bad);
    bad.fields = "[\n";
    rejected(shell, bad);
    bad.fields = "---\namount: 1\n---\namount: 2\n";
    rejected(shell, bad);
    bad = edit;
    bad.schemaVersion = 2;
    rejected(shell, bad);
    bad = edit;
    bad.resourceId = secondaryId;
    rejected(shell, bad);
    files.files.write("primary.arp", core::pmr::String(Fixture::primarySource) + "\n# conflict\n");
    rejected(shell, edit);
    rejected(shell, RedoSceneCommand{});
    files.files.write("primary.arp", Fixture::primarySource);
    {
        EditorCommandPayload payload = edit;
        core::CpuMemoryQuotaScope scope(core::makeShared<core::CpuMemoryQuota>(1));
        check(!shell.execute(payload).success);
    }
    check(files.amount(shell) == 7 && !shell.sceneDirty()
        && shell.sceneHistoryUsage().redoEntries == 1);
    Files::command(shell, RedoSceneCommand{});
    check(files.amount(shell) == 19);
    // External edits invalidate the expected field value without consuming history.
    receiver.ptr<reflection::instance>()->slot("amount") = reflection::object(20);
    rejected(shell, UndoSceneCommand{});
    receiver.ptr<reflection::instance>()->slot("amount") = reflection::object(19);
    check(scene.findComponent(files.scriptId)->scriptInstance() == receiver
        && scene.findComponent(files.scriptId)->nativeReflectionObject() == native
        && shell.sceneDocument().selectedObject() == selection);

    SetSceneScriptFieldsCommand clear(secondaryId, "PhysicsLink", 1, "body: null\n", true);
    clear.objectId = files.child.value();
    clear.componentId = files.linkId.value();
    auto* link = scene.findObject(files.child)->findComponent(files.linkId);
    const auto linkReceiver = link->scriptInstance();
    const auto oldBody = scene.findComponent(files.bodyId)->nativeReflectionObject();
    Files::command(shell, clear);
    check(linkReceiver.ptr<reflection::instance>()->slot("body").is_undefined());
    Files::command(shell, remove(scene.rootId(), files.bodyId));
    check(!oldBody.native_reference_alive());
    Files::command(shell, UndoSceneCommand{});
    Files::command(shell, UndoSceneCommand{});
    check(link->scriptInstance() == linkReceiver
        && linkReceiver.ptr<reflection::instance>()->slot("body")
            == scene.findComponent(files.bodyId)->nativeReflectionObject()
        && !oldBody.native_reference_alive());
    SaveSceneCommand save("fields.yaml");
    save.overwriteExisting = true;
    Files::succeeded(arp::editor::tests::ScenePersistenceFixture::finish(
                         shell, shell.submitCommand(std::move(save))),
        true);
    Files::load(shell, "fields.yaml");
    check(files.amount(shell) == 19 && !shell.sceneDirty()
        && !receiver.ptr<reflection::instance>()->alive()
        && !linkReceiver.ptr<reflection::instance>()->alive());
}

void attachmentRoundTrip(Files& files)
{
    core::SharedPtr<const EditorScriptComponentCatalog> retained;
    reflection::object receiver, native;
    {
        EditorShell shell;
        files.open(shell);
        auto& scene = shell.sceneDocument().scene();
        retained = shell.sceneDocument().scriptCatalog();
        auto* module = retained->modules().find({primaryId, "Mover", 1});
        check(module != nullptr);
        const auto instances = module->liveInstances();
        const auto selection = shell.sceneDocument().selectedObject();
        AddSceneScriptComponentCommand add(primaryId, "Mover", 1, "amount: 31\n", false);
        add.objectId = files.child.value();
        auto bad = add;
        bad.objectId = scene.rootId().value();
        // Duplicate detection is a late candidate failure. Its lease is retired
        // without touching the existing receiver or publishing history.
        rejected(shell, bad);
        check(module->liveInstances() == instances && files.amount(shell) == 7);
        bad = add;
        bad.fields = "{}\n";
        rejected(shell, bad);
        bad.fields = "[\n";
        rejected(shell, bad);
        bad = add;
        bad.schemaVersion = 2;
        rejected(shell, bad);
        bad = add;
        bad.resourceId = secondaryId;
        rejected(shell, bad);
        files.files.write(
            "primary.arp", core::pmr::String(Fixture::primarySource) + "\n# changed\n");
        rejected(shell, add);
        files.files.write("primary.arp", Fixture::primarySource);
        {
            EditorCommandPayload payload = add;
            core::CpuMemoryQuotaScope quota(core::makeShared<core::CpuMemoryQuota>(1));
            check(!shell.execute(payload).success);
        }
        check(module->liveInstances() == instances);
        Files::command(shell, add);
        auto* child = scene.findObject(files.child);
        const auto id = child->componentIds().back();
        auto* component = child->findComponent(id);
        receiver = component->scriptInstance();
        native = component->nativeReflectionObject();
        check(receiver.ptr<reflection::instance>()->slot("amount").cast<int>() == 31
            && !component->enabled() && shell.sceneDirty() && child->components().size() == 3
            && module->liveInstances() == instances + 1 && scene.name() == "Shell script root"
            && shell.sceneDocument().selectedObject() == selection);
        Files::command(shell, UndoSceneCommand{});
        check(!child->findComponent(id) && !receiver.ptr<reflection::instance>()->alive()
            && !native.native_reference_alive() && !shell.sceneDirty());
        files.files.write(
            "primary.arp", core::pmr::String(Fixture::primarySource) + "\n# changed\n");
        rejected(shell, RedoSceneCommand{});
        files.files.write("primary.arp", Fixture::primarySource);
        Files::command(shell, RedoSceneCommand{});
        component = child->findComponent(id);
        check(component && component->scriptInstance() != receiver
            && !native.native_reference_alive()
            && component->scriptInstance().ptr<reflection::instance>()->slot("amount").cast<int>()
                == 31
            && !component->enabled() && child->componentIds().back() == id);
        core::pmr::String linkFields("body: {owner: ");
        linkFields += std::to_string(scene.rootId().value());
        linkFields += ", component: ";
        linkFields += std::to_string(files.bodyId.value());
        linkFields += "}\n";
        AddSceneScriptComponentCommand link(secondaryId, "PhysicsLink", 1, linkFields, true);
        link.objectId = scene.rootId().value();
        Files::command(shell, link);
        const auto linkId = scene.componentIds().back();
        const auto linkReceiver = scene.findComponent(linkId)->scriptInstance();
        check(linkReceiver.ptr<reflection::instance>()->slot("body")
            == scene.findComponent(files.bodyId)->nativeReflectionObject());
        Files::command(shell, UndoSceneCommand{});
        check(!scene.findComponent(linkId) && !linkReceiver.ptr<reflection::instance>()->alive());
        Files::command(shell, RedoSceneCommand{});
        SaveSceneCommand save("attached-fields.yaml");
        save.overwriteExisting = true;
        Files::succeeded(
            arp::editor::tests::ScenePersistenceFixture::finish(shell, shell.submitCommand(save)),
            true);
        Files::load(shell, "attached-fields.yaml");
        component = shell.sceneDocument().scene().findObject(files.child)->findComponent(id);
        check(component
            && component->scriptInstance().ptr<reflection::instance>()->slot("amount").cast<int>()
                == 31
            && !component->enabled() && !shell.sceneDirty() && files.amount(shell) == 7);
        check(!receiver.ptr<reflection::instance>()->alive() && !native.native_reference_alive());
    }
    for (const auto& entry : retained->modules().entries())
        check(entry.module->liveInstances() == 0);
}

void cycle(Files& files)
{
    componentRoundTrip(files);
    subtreeRoundTrip(files);
    conflictsAndFailures(files);
    fieldsRoundTrip(files);
    attachmentRoundTrip(files);
}
} // namespace

int main(int argc, char** argv)
{
    Files files;
    cycle(files);
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned round = 0; round < 20; ++round) {
            cycle(files);
            const auto current = core::defaultCpuAllocator().stats();
            check(current.liveBytes == baseline.liveBytes);
            check(current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        }
        std::printf("ordinary_script_presence_cycles=20 live_bytes_delta=0 live_count_delta=0 "
                    "retired_instances=0 peak_bytes=%llu\n",
            static_cast<unsigned long long>(core::defaultCpuAllocator().stats().peakBytes));
    }
    std::puts("PASS actual v4 component/subtree deletion, exact Undo/Redo, retired handles, "
              "source conflicts, failures and save/reopen");
}
