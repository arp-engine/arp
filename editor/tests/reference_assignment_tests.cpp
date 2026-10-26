#include "2d/physics_components.h"
#include "editor/tests/fixtures/resource_navigation_fixture.h"
#include "editor/workspace/editor_scene_reference_assignment.h"
#include "scene/camera_temporal_component.h"

#include <cstdio>
#include <limits>
#include <new>

namespace {
using namespace arp;
using namespace arp::editor;
using Assignment = EditorSceneReferenceAssignment;

// Real authored endpoints in a Passive document. Bodies and a joint keep the
// serialized references valid; preparation itself never steps the solver.
class ObjectFixture final {
public:
    ObjectFixture()
        : world(core::makeShared<physics2d::PhysicsWorld2D>(
              physics2d::WorldConfig{{0, 0}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f}))
    {
        assert(world->valid());
        const auto body = [&] {
            auto& object = document.scene().createObject("Same name");
            physics2d::BodyDesc desc;
            desc.type = physics2d::BodyType::Dynamic;
            auto& component = object.addComponent<physics2d::RigidBody2DComponent>(world, desc);
            assert(component.initializeForOwner(object.world_transform(), object.id()));
            return object.id().value();
        };
        target = body();
        alternative = body();
        const auto anchor = body();
        auto& source = document.scene().createObject("Reference owner");
        joint = &source.addComponent<physics2d::Joint2DComponent>(world);
        auto properties = joint->editorAuthoringProperties();
        auto& value = properties.cast<physics2d::Joint2DEditorProperties&>();
        value.bodyA = target;
        value.bodyB = anchor;
        assert(joint->applyEditorAuthoringProperties(properties));
        assert(document.select(source.id()));
        intent.objectId = source.id().value();
        intent.componentId = joint->id().value();
        intent.property = "bodyA";
        intent.sourceObjectId = alternative;
        intent.documentEpoch = context.documentEpoch = 3;
        intent.resourceRevision = context.resourceRevision = 7;
        context.selectedObjectId = intent.objectId;
        context.selectedCount = 1;
        context.sceneOwner = document.ownerTicket();
    }

    const char* prepare(Assignment::Prepared& output) const
    {
        return Assignment::prepare(document, resources, locations, context, intent, output);
    }

    core::SharedPtr<physics2d::PhysicsWorld2D> world;
    resource::ResourceManager resources;
    ProjectResourceLocations locations;
    EditorSceneDocument document{false};
    physics2d::Joint2DComponent* joint = nullptr;
    std::uint64_t target = 0, alternative = 0;
    Assignment::Intent intent;
    Assignment::Context context;
};

class ResourceFixture final {
public:
    explicit ResourceFixture(const tests::ResourceNavigationFixture& files)
    {
        assert(resource::InternalAssetPackageReader()
                .load(files.root / "Assets/Base.arpkg", *resources)
                .success);
        assert(!ProjectResourceLocations::capture(
            files.root, {"Assets/Base.arpkg"}, resources, locations));
        document.setResourceManagerProvider([owner = resources] {
            return owner.get();
        });
        const auto object = document.createObject("Mesh reference owner");
        mesh = static_cast<scene3d::MeshComponent*>(document.addComponent("MeshComponent"));
        assert(mesh);
        mesh->set_material_resource_id(files.materialId);
        intent.sourceKind = Assignment::SourceKind::Resource;
        intent.objectId = object.value();
        intent.componentId = mesh->id().value();
        intent.property = "materialResourceId";
        intent.sourceResourceId = tests::ResourceNavigationFixture::alternateId;
        intent.documentEpoch = context.documentEpoch = 4;
        intent.resourceRevision = context.resourceRevision = 8;
        context.selectedObjectId = object.value();
        context.selectedCount = 1;
        context.sceneOwner = document.ownerTicket();
    }

    const char* prepare(Assignment::Prepared& output) const
    {
        return Assignment::prepare(document, *resources, locations, context, intent, output);
    }

    core::SharedPtr<resource::ResourceManager> resources =
        core::makeShared<resource::ResourceManager>();
    ProjectResourceLocations locations;
    EditorSceneDocument document{false};
    scene3d::MeshComponent* mesh = nullptr;
    Assignment::Intent intent;
    Assignment::Context context;
};

core::pmr::String serialized(const EditorSceneDocument& document)
{
    core::pmr::String yaml, diagnostic;
    assert(EditorSceneAuthoringCodec::encode(document, yaml, diagnostic));
    return yaml;
}

Assignment::Prepared sentinel()
{
    Assignment::Prepared output;
    output.kind = SceneInspectorPropertyKind::Number;
    output.objectId = 999;
    output.resourceId = "unchanged owned output identity";
    output.changed = true;
    return output;
}

void assertSentinel(const Assignment::Prepared& output)
{
    assert(output.kind == SceneInspectorPropertyKind::Number && output.objectId == 999
        && output.resourceId == "unchanged owned output identity" && output.changed);
}

template <class Fixture> void rejected(Fixture& fixture, const char* reason = nullptr)
{
    auto output = sentinel();
    const auto* diagnostic = fixture.prepare(output);
    assert(diagnostic && (!reason || std::string_view(diagnostic) == reason));
    assertSentinel(output);
}

void objectContracts()
{
    ObjectFixture fixture;
    const auto before = serialized(fixture.document);
    Assignment::Prepared output;
    assert(!fixture.prepare(output));
    assert(output.kind == SceneInspectorPropertyKind::SceneObjectReference
        && output.objectId == fixture.alternative && output.resourceId.empty() && output.changed);
    fixture.intent.sourceObjectId = fixture.target;
    assert(!fixture.prepare(output) && !output.changed && output.objectId == fixture.target);
    for (const auto id :
        {std::uint64_t(0), fixture.intent.objectId, std::numeric_limits<std::uint64_t>::max()}) {
        fixture.intent.sourceObjectId = id;
        rejected(fixture);
    }
    fixture.intent.sourceObjectId = fixture.alternative;
    fixture.intent.sourceResourceId = tests::ResourceNavigationFixture::alternateId;
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-SOURCE");
    fixture.intent.sourceResourceId.clear();
    fixture.intent.sourceKind = static_cast<Assignment::SourceKind>(255);
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-SOURCE");
    fixture.intent.sourceKind = Assignment::SourceKind::Resource;
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-SOURCE");
    fixture.intent.sourceKind = Assignment::SourceKind::Object;
    for (const auto path : {"", "missing", "length"}) {
        fixture.intent.property = path;
        rejected(fixture);
    }
    fixture.intent.property.assign(EditorSceneDocument::maxPropertyNameBytes + 1, 'x');
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-PATH");
    fixture.intent.property = "bodyA";
    ++fixture.intent.componentId;
    rejected(fixture);
    --fixture.intent.componentId;
    auto& runtime = fixture.document.scene()
                        .findObject(scene::GameObjectId::fromValue(fixture.intent.objectId))
                        ->addComponent<scene::CameraTemporalComponent>();
    const auto jointId = fixture.intent.componentId;
    fixture.intent.componentId = runtime.id().value();
    fixture.intent.property = "previousFovDegrees_";
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-READONLY");
    assert(fixture.document.removeComponent(fixture.document.selectedObject(), runtime.id()));
    fixture.intent.componentId = jointId;
    fixture.intent.property = "bodyA";
    assert(serialized(fixture.document) == before
        && fixture.document.selectedObject().value() == fixture.intent.objectId);
    assert(fixture.document.scene()
            .destroyObject(scene::GameObjectId::fromValue(fixture.alternative))
            .ok());
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-OBJECT");
    std::puts(
        "PASS actual typed object field, same-name distinct IDs, no-op, missing/type/source/path rejection; preparation preserves authored bytes and selection");
}

void identityAndBudget()
{
    ObjectFixture fixture;
    for (const auto epoch : {std::uint64_t(0), fixture.context.documentEpoch + 1}) {
        fixture.intent.documentEpoch = epoch;
        rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-STALE");
    }
    fixture.intent.documentEpoch = fixture.context.documentEpoch;
    for (const auto revision : {std::uint64_t(0), fixture.context.resourceRevision + 1}) {
        fixture.intent.resourceRevision = revision;
        rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-STALE");
    }
    fixture.intent.resourceRevision = fixture.context.resourceRevision;
    for (const auto count : {0u, 2u}) {
        fixture.context.selectedCount = count;
        rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-SELECTION");
    }
    fixture.context.selectedCount = 1;
    ++fixture.context.selectedObjectId;
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-SELECTION");
    fixture.context.selectedObjectId = fixture.intent.objectId;
    assert(fixture.document.select(scene::GameObjectId::fromValue(fixture.target)));
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-SELECTION");
    assert(fixture.document.select(scene::GameObjectId::fromValue(fixture.intent.objectId)));
    fixture.document.setResourceManagerProvider({});
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-STALE");
    fixture.context.sceneOwner = fixture.document.ownerTicket();
    fixture.document = EditorSceneDocument(false);
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-STALE");

    ObjectFixture capacity;
    auto* source = capacity.document.scene().findObject(
        scene::GameObjectId::fromValue(capacity.intent.objectId));
    for (unsigned index = 0; index < 20; ++index)
        source->addComponent<physics2d::Joint2DComponent>(capacity.world);
    rejected(capacity, "EDITOR-REFERENCE-FOCUS-BUDGET");
    std::puts(
        "PASS zero/stale epochs, exact single selection, retained owner/provider/replacement ticket and bounded metadata observation");
}

void resourceContracts(const tests::ResourceNavigationFixture& files)
{
    ResourceFixture fixture(files);
    const auto before = serialized(fixture.document);
    Assignment::Prepared output;
    assert(!fixture.prepare(output));
    assert(output.kind == SceneInspectorPropertyKind::ResourceReference && !output.objectId
        && output.resourceId == tests::ResourceNavigationFixture::alternateId && output.changed);
    fixture.intent.sourceResourceId = files.materialId;
    assert(!fixture.prepare(output) && !output.changed && output.resourceId == files.materialId);
    for (const auto id : {"",
             "invalid",
             tests::ProjectPackageFixture::shaderId,
             "00000000-0000-4000-8000-000000009999"}) {
        fixture.intent.sourceResourceId = id;
        rejected(fixture);
    }
    fixture.intent.sourceResourceId = tests::ResourceNavigationFixture::alternateId;
    fixture.intent.sourceObjectId = fixture.intent.objectId;
    rejected(fixture, "EDITOR-REFERENCE-ASSIGNMENT-SOURCE");
    fixture.intent.sourceObjectId = 0;
    const auto bytes = tests::RecoveryFixture::read(files.second);
    tests::RecoveryFixture::write(files.second, bytes + "\n# changed source\n");
    rejected(fixture, "EDITOR-RESOURCE-LOCATION-CONTENT-CHANGED");
    tests::RecoveryFixture::write(files.second, bytes);
    assert(serialized(fixture.document) == before
        && fixture.document.selectedObject().value() == fixture.intent.objectId);
    const auto original =
        fixture.resources->find<resource::MaterialResource>(fixture.intent.sourceResourceId);
    assert(original.ready());
    auto updated = core::makeUnique<resource::MaterialResource>(*original.get());
    // Real payload-only change: keep metadata and envelope bytes unchanged.
    const auto requestedRed = original.get()->baseColorR == 0.f ? 1.f : 0.f;
    updated->baseColorR = requestedRed;
    const auto oldGeneration = original.generation();
    const auto reloaded = fixture.resources->reloadResource(std::move(updated));
    assert(reloaded.success && !reloaded.unchangedContent && original.stale()
        && fixture.resources->generationOf(fixture.intent.sourceResourceId) != oldGeneration);
    const auto current =
        fixture.resources->find<resource::MaterialResource>(fixture.intent.sourceResourceId);
    assert(current.ready() && current.get()->baseColorR == requestedRed);
    rejected(fixture, "EDITOR-RESOURCE-LOCATION-METADATA-CHANGED");
    assert(fixture.resources->unload(fixture.intent.sourceResourceId).success);
    rejected(fixture, "EDITOR-RESOURCE-LOCATION-METADATA-CHANGED");
    std::puts(
        "PASS explicit resource IDs, exact type, current value/no-op, changed definition and unloaded owner; no Scene mutation");
}

template <class Fixture> void allocationFailure(Fixture& fixture)
{
    unsigned failures = 0, successes = 0;
    for (const auto bytes : {1u, 256u, 2048u, 8192u, 65536u, 262144u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            auto output = sentinel();
            core::CpuMemoryQuotaScope scope(quota);
            if (fixture.prepare(output)) {
                ++failures;
                assertSentinel(output);
            } else {
                ++successes;
                assert(output.changed);
            }
        }
        assert(quota->liveBytes() == 0);
    }
    assert(failures && successes);
}

void ordinaryCycle(const tests::ResourceNavigationFixture& files)
{
    ObjectFixture object;
    ResourceFixture resource(files);
    Assignment::Prepared output;
    assert(!object.prepare(output) && output.objectId == object.alternative);
    assert(!resource.prepare(output)
        && output.resourceId == tests::ResourceNavigationFixture::alternateId);
}

SetSceneComponentPropertyCommand propertyCommand(
    const EditorShell& shell, const scene3d::MeshComponent& mesh, std::string_view resourceId)
{
    SetSceneComponentPropertyCommand command("materialResourceId");
    command.objectId = shell.sceneDocument().selectedObject().value();
    command.componentId = mesh.id().value();
    command.valueKind = ScenePropertyValueKind::ResourceReference;
    command.stringValue = resourceId;
    return command;
}

void propertyHistory(const tests::RecoveryFixture& root,
    const tests::ResourceNavigationFixture& files,
    bool undoAttempt)
{
    unsigned failures = 0, successes = 0;
    for (const auto bytes : {1u, 256u, 4096u, 16384u, 65536u, 262144u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            EditorShell shell;
            assert(shell
                    .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                        root.root)
                    .ok());
            auto* mesh = files.prepare(shell);
            const auto command =
                propertyCommand(shell, *mesh, tests::ResourceNavigationFixture::alternateId);
            if (undoAttempt)
                assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
            else {
                SetSceneObjectNameCommand rename("Renamed");
                rename.objectId = command.objectId;
                assert(shell.submitCommand(rename).status == EditorResponseStatus::Completed);
                assert(shell.submitCommand(UndoSceneCommand{}).status
                    == EditorResponseStatus::Completed);
                const auto version = shell.stateVersion();
                // No-op must retain the existing Redo branch and version.
                assert(shell.submitCommand(propertyCommand(shell, *mesh, files.materialId))
                           .stateVersion
                    == version);
            }
            const auto before = serialized(shell.sceneDocument());
            const auto dirty = shell.sceneDirty();
            const auto version = shell.stateVersion();
            bool completed = false;
            try {
                core::CpuMemoryQuotaScope scope(quota);
                const auto response = undoAttempt ? shell.submitCommand(UndoSceneCommand{})
                                                  : shell.submitCommand(command);
                completed = response.status == EditorResponseStatus::Completed;
            } catch (const std::bad_alloc&) {
                // Existing property APIs can throw; this substage guarantees
                // preparation/history ownership, not arbitrary setter rollback.
            }
            if (!completed) {
                ++failures;
                assert(serialized(shell.sceneDocument()) == before && shell.sceneDirty() == dirty
                    && shell.stateVersion() == version);
                if (undoAttempt) {
                    // A thrown history operation must not leave history mode
                    // enabled and suppress the next independent edit's entry.
                    assert(
                        shell.submitCommand(propertyCommand(shell, *mesh, files.materialId)).status
                        == EditorResponseStatus::Completed);
                    assert(shell.submitCommand(UndoSceneCommand{}).status
                        == EditorResponseStatus::Completed);
                    assert(mesh->material_resource_id()
                        == tests::ResourceNavigationFixture::alternateId);
                } else {
                    assert(shell.submitCommand(RedoSceneCommand{}).status
                        == EditorResponseStatus::Completed);
                    assert(shell.sceneDocument()
                               .scene()
                               .findObject(shell.sceneDocument().selectedObject())
                               ->name()
                        == "Renamed");
                }
            } else {
                ++successes;
                assert(shell.stateVersion() == version + 1);
                if (undoAttempt) {
                    assert(mesh->material_resource_id() == files.materialId);
                    assert(shell.submitCommand(RedoSceneCommand{}).status
                        == EditorResponseStatus::Completed);
                } else {
                    assert(mesh->material_resource_id()
                        == tests::ResourceNavigationFixture::alternateId);
                    assert(shell.submitCommand(UndoSceneCommand{}).status
                        == EditorResponseStatus::Completed);
                    assert(mesh->material_resource_id() == files.materialId);
                    assert(shell.submitCommand(RedoSceneCommand{}).status
                        == EditorResponseStatus::Completed);
                    const auto redone = shell.stateVersion();
                    assert(shell.submitCommand(RedoSceneCommand{}).stateVersion == redone);
                }
            }
            assert(shell.sceneDocument().selectedObject().value() == command.objectId);
        }
        assert(quota->liveBytes() == 0);
    }
    assert(failures && successes);
    std::puts(undoAttempt
            ? "PASS reference Undo cold destination history, exception mode reset and quota ownership"
            : "PASS reference history prepared before setter, failed/no-op Redo preserved and one-field Undo/Redo");
}

void historyOrdinaryCycle(
    const tests::RecoveryFixture& root, const tests::ResourceNavigationFixture& files)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root.root)
            .ok());
    auto* mesh = files.prepare(shell);
    auto failure = core::makeShared<bool>(false);
    const auto provider = shell.sceneDocument().resourceManagerProvider();
    shell.sceneDocument().setResourceManagerProvider([failure, provider] {
        if (*failure)
            throw std::bad_alloc();
        return provider();
    });
    assert(shell.retrySceneInspector());
    const auto command =
        propertyCommand(shell, *mesh, tests::ResourceNavigationFixture::alternateId);
    assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
    const auto version = shell.stateVersion();
    *failure = true;
    bool threw = false;
    try {
        (void)shell.submitCommand(UndoSceneCommand{});
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    *failure = false;
    assert(threw && shell.stateVersion() == version
        && mesh->material_resource_id() == tests::ResourceNavigationFixture::alternateId);
    assert(shell.submitCommand(propertyCommand(shell, *mesh, files.materialId)).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(mesh->material_resource_id() == tests::ResourceNavigationFixture::alternateId);
    assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(mesh->material_resource_id() == files.materialId);
}

void smoke(const tests::RecoveryFixture& root, const tests::ResourceNavigationFixture& files)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        ordinaryCycle(files);
        historyOrdinaryCycle(root, files);
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary object/resource/history failure/retry/destroy cycles; core live bytes/count delta=0");
}

// Explicit payload regression also registered independently with CTest. Exit 2
// preserves the original probe's fail-closed behavior if the defect returns.
int probeMaterialReload(const tests::ResourceNavigationFixture& files)
{
    ResourceFixture fixture(files);
    const auto original =
        fixture.resources->find<resource::MaterialResource>(fixture.intent.sourceResourceId);
    assert(original.ready());
    const auto oldGeneration = original.generation();
    const auto oldRed = original.get()->baseColorR;
    auto updated = core::makeUnique<resource::MaterialResource>(*original.get());
    const auto requestedRed = oldRed == 0.f ? 1.f : 0.f;
    updated->baseColorR = requestedRed;
    const auto result = fixture.resources->reloadResource(std::move(updated));
    const auto current =
        fixture.resources->find<resource::MaterialResource>(fixture.intent.sourceResourceId);
    std::printf(
        "MaterialResource payload reload: success=%d unchanged=%d generation=%llu->%llu red=%g requested=%g actual=%g diagnostic=%s\n",
        result.success,
        result.unchangedContent,
        (unsigned long long)oldGeneration,
        (unsigned long long)current.generation(),
        oldRed,
        requestedRed,
        current.ready() ? current.get()->baseColorR : -1.f,
        result.diagnostics.c_str());
    if (result.success && current.ready() && current.generation() != oldGeneration
        && current.get()->baseColorR == requestedRed)
        return 0;
    std::puts("FAIL MaterialResource payload reload dropped the requested color change");
    return 2;
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture root;
    tests::ResourceNavigationFixture files(root.root / "project");
    if (argc == 2 && std::string_view(argv[1]) == "--probe-material-reload")
        return probeMaterialReload(files);
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke(root, files);
    else {
        objectContracts();
        identityAndBudget();
        resourceContracts(files);
        ObjectFixture object;
        ResourceFixture resource(files);
        allocationFailure(object);
        allocationFailure(resource);
        propertyHistory(root, files, false);
        propertyHistory(root, files, true);
        historyOrdinaryCycle(root, files);
        std::puts(
            "PASS allocation failure preserves all prepared output fields and releases quota ownership");
    }
}
