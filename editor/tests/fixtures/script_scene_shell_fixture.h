#pragma once

#include "2d/physics_components.h"
#include "core/io/file_io.h"
#include "editor/tests/fixtures/editor_script_document_fixture.h"
#include "editor/tests/fixtures/scene_persistence_fixture.h"
#include "editor/workspace/editor_scene_script_policy.h"
#include "reflection/instance.h"

namespace arp::editor::testfixtures {

// Shared formal v4 disk/project fixture for Shell commands and native captures.
// Components are restored cold; its user initializers must never execute.

class ScriptSceneShellFixture final {
public:
    ScriptSceneShellFixture()
    {
        files.manifest.physics2d =
            physics2d::WorldConfig{{0, 0}, 32, 32, 8, 32, 64, 32, 16, 4, 1.f / 60};
        ProjectDiagnostic issue;
        check(ProjectManifestCodec::encode(files.manifest, manifest, issue));
        files.write("project.arp.yaml", manifest);
        EditorSceneDocument document(false);
        core::pmr::String diagnostic;
        check(document.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
        check(files.prepare(document, diagnostic), diagnostic);
        document.scene().setName("Shell script root");
        child = document.createObject("Local A");
        const auto* body = document.addComponent("RigidBody2DComponent", document.scene().rootId());
        check(body != nullptr);
        bodyId = body->id();
        scene::Scene::IdentityRestoration transaction(document.scene(), {0, 2});
        const auto catalog = document.scriptCatalog();
        auto& mover = transaction.createScriptComponent(document.scene().rootId(),
            *catalog->modules().find({primaryId, "Mover", 1}),
            {primaryId, "Mover", 1},
            document.scene().components().size());
        scriptId = mover.id();
        mover.scriptInstance().ptr<reflection::instance>()->slot("amount") = reflection::object(7);
        auto& link = transaction.createScriptComponent(child,
            *catalog->modules().find({secondaryId, "PhysicsLink", 1}),
            {secondaryId, "PhysicsLink", 1},
            1);
        linkId = link.id();
        link.scriptInstance().ptr<reflection::instance>()->slot("body") =
            body->nativeReflectionObject();
        transaction.prepare();
        transaction.commit();
        check(EditorSceneScriptCodec::encode(document,
                  EditorSceneScriptPolicy::inputs(files.root, files.manifest),
                  original,
                  diagnostic),
            diagnostic);
        files.write("scene.yaml", original);
    }

    void open(EditorShell& shell)
    {
        check(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        check(shell.projectManifest() != nullptr);
        load(shell);
        check(!shell.sceneDirty());
        check(shell.sceneDocument().scene().name() == "Shell script root");
        auto* body = shell.sceneDocument().scene().findComponent(bodyId);
        auto* link = shell.sceneDocument().scene().findObject(child)->findComponent(linkId);
        check(body && link);
        check(link->scriptInstance().ptr<reflection::instance>()->slot("body")
            == body->nativeReflectionObject());
    }

    static void succeeded(const EditorStateSnapshot& result, bool committed = false)
    {
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded,
            result.scenePersistenceTaskDiagnostic);
        check(result.scenePersistenceTaskCommitted == committed);
    }

    static EditorStateSnapshot load(EditorShell& shell, const char* path = "scene.yaml")
    {
        LoadSceneCommand command(path);
        command.discardChanges = true;
        auto result = tests::ScenePersistenceFixture::finish(shell, shell.submitCommand(command));
        succeeded(result);
        return result;
    }

    static void command(EditorShell& shell, EditorCommandPayload command)
    {
        check(shell.submitCommand(std::move(command)).status == EditorResponseStatus::Completed);
    }

    core::pmr::String read(const char* name)
    {
        const auto disk = core::readTextFile(files.root / name);
        check(disk.success);
        return disk.text;
    }

    int amount(const EditorShell& shell) const
    {
        const auto* script = shell.sceneDocument().scene().findComponent(scriptId);
        check(script != nullptr);
        return script->scriptInstance().ptr<reflection::instance>()->slot("amount").cast<int>();
    }

    Fixture files;
    core::pmr::String manifest, original;
    scene::GameObjectId child;
    scene::ComponentId scriptId, bodyId, linkId;
};

} // namespace arp::editor::testfixtures
