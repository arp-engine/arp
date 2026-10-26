#pragma once

#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_scene_transform_transaction.h"

#include <cassert>

namespace arp::editor::tests {

// Ordinary selected hierarchy shared by transaction and gesture contracts.
class TransformSelectionFixture final {
public:
    explicit TransformSelectionFixture(const tests::RecoveryFixture& files)
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        shell.activate("workspace_3D");
        first = shell.sceneDocument().createObject("first");
        second = shell.sceneDocument().createObject("second");
        child = shell.sceneDocument().createObject("child");
        set(first, scene::Transform::fromTranslation({2, 0, 0}));
        set(second, scene::Transform::fromTranslation({6, 0, 0}));
        set(child, scene::Transform::fromTranslation({1, 0, 0}));
        assert(shell.sceneDocument().scene().reparent(child, first).ok());
        select();
    }

    void select(bool withChild = false)
    {
        SelectSceneObjectsCommand selection;
        selection.objectIds = {first.value(), second.value()};
        if (withChild)
            selection.objectIds.push_back(child.value());
        selection.primaryObjectId = first.value();
        assert(shell.submitCommand(std::move(selection)).status == EditorResponseStatus::Completed);
    }

    scene::Transform local(scene::GameObjectId id) const
    {
        return shell.sceneDocument().scene().findObject(id)->local_transform();
    }

    void set(scene::GameObjectId id, const scene::Transform& pose)
    {
        shell.sceneDocument().scene().findObject(id)->set_local_transform(pose);
    }

    core::UniquePtr<EditorShell::PreparedSceneTransform> prepare(double amount = 1)
    {
        TransformSceneSelectionCommand command;
        command.amount = amount;
        const char* diagnostic = nullptr;
        auto token = shell.prepareSceneTransform(command, diagnostic);
        assert(token && !diagnostic);
        return token;
    }

    void apply(double amount = 1)
    {
        TransformSceneSelectionCommand command;
        command.amount = amount;
        assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
    }

    void history(bool undo)
    {
        const auto response = undo ? shell.submitCommand(UndoSceneCommand{})
                                   : shell.submitCommand(RedoSceneCommand{});
        assert(response.status == EditorResponseStatus::Completed);
    }

    EditorShell shell;
    scene::GameObjectId first, second, child;
};

} // namespace arp::editor::tests
