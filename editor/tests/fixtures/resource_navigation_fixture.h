#pragma once

#include "3d/components.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/tests/fixtures/project_package_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor::tests {

// A real explicitly packaged pair; navigation must follow IDs across directories.
class ResourceNavigationFixture final {
public:
    explicit ResourceNavigationFixture(std::filesystem::path directory) : root(std::move(directory))
    {
        std::filesystem::create_directories(root);
        root = std::filesystem::canonical(root);
        const auto path = root / "Assets/Base.arpkg";
        auto package = ProjectPackageFixture::write(path);
        resource::ResourceManager manager;
        assert(resource::InternalAssetPackageReader().load(path, manager).success);
        for (const auto& entry : package.entries)
            if (entry.resourceType == "MaterialResource") {
                materialId = entry.resourceId;
                first = path / std::filesystem::u8path(entry.envelopePath);
            }
        assert(!materialId.empty());
        const auto material = manager.find<resource::MaterialResource>(materialId);
        assert(material.ready());
        resource::AssetResourcePackageBuildDesc desc;
        desc.resourceId = alternateId;
        desc.alias = "material/alternate";
        desc.displayName = "editor-test";
        desc.status = "Prototype Complete";
        desc.envelopePath = "definitions/alternate.arpres.yaml";
        const auto alternate = material.get()->buildPackageEntry(desc);
        assert(alternate.success);
        package.entries.push_back(alternate.entry);
        assert(resource::InternalAssetPackageWriter().write(package, path).success);
        second = path / "definitions/alternate.arpres.yaml";
        RecoveryFixture::write(root / "project.arp.yaml",
            ProjectFixture::document("assetPackages: [Assets/Base.arpkg]\nphysics2d: null\n"));
    }

    scene3d::MeshComponent* prepare(EditorShell& shell) const
    {
        OpenProjectCommand open;
        open.root = core::FilePath::utf8(root);
        open.discardChanges = true;
        assert(shell.submitCommand(std::move(open)).status == EditorResponseStatus::Completed);
        shell.activate("workspace_3D");
        assert(shell.submitCommand(CreateSceneObjectCommand("Resource reference")).status
            == EditorResponseStatus::Completed);
        assert(shell.submitCommand(AddSceneComponentCommand("MeshComponent")).status
            == EditorResponseStatus::Completed);
        auto* mesh = shell.sceneDocument()
                         .scene()
                         .findObject(shell.sceneDocument().selectedObject())
                         ->getComponent<scene3d::MeshComponent>();
        assert(mesh);
        mesh->set_material_resource_id(materialId);
        assert(shell.retrySceneInspector());
        return mesh;
    }

    static LocateSceneResourceCommand command(
        EditorShell& shell, const scene3d::MeshComponent& mesh)
    {
        const auto selection = shell.submitCommand(GetSceneInspectorSelectionQuery{});
        assert(selection.hasSceneInspectorSelection);
        LocateSceneResourceCommand result;
        result.objectId = shell.sceneDocument().selectedObject().value();
        result.componentId = mesh.id().value();
        result.property = "materialResourceId";
        result.documentEpoch = selection.sceneInspectorSelection.documentEpoch;
        result.resourceRevision = selection.sceneInspectorSelection.resourceRevision;
        return result;
    }

    static core::pmr::String source(EditorShell& shell)
    {
        const auto properties =
            shell.sceneDocument().inspectorProperties(shell.sceneDocument().selectedObject());
        for (const auto& property : properties)
            if (property.name == "materialResourceId")
                return core::pmr::String("scene_resource_locate_")
                    + scenePropertyControlSource(property);
        assert(false);
        return {};
    }

    static constexpr auto alternateId = "00000000-0000-4000-8000-000000007777";
    std::filesystem::path root, first, second;
    core::pmr::String materialId;
};
} // namespace arp::editor::tests
