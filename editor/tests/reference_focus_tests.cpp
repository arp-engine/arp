#include "2d/physics_components.h"
#include "editor/tests/fixtures/editor_protocol_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_control_input.h"
#include "editor/validation/editor_reference_focus_smoke.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_reference_focus.h"

#include <cassert>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::editor;
using Choices = EditorSceneObjectReferenceChoices;

// Pending one-ended joints expose authored IDs without creating live joints.
// Type-only body components are explicit CPU schema fixtures, not simulation.
class Fixture final {
public:
    Fixture()
        : world(core::makeShared<physics2d::PhysicsWorld2D>(
              physics2d::WorldConfig{{0, 0}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f}))
    {
        assert(world->valid());
        auto& first = document.scene().createObject("Same name");
        first.addComponent<physics2d::RigidBody2DComponent>(nullptr);
        target = first.id().value();
        auto& second = document.scene().createObject("Same name");
        second.addComponent<physics2d::RigidBody2DComponent>(nullptr);
        alternative = second.id().value();
        auto& source = document.scene().createObject("Reference source");
        object = source.id().value();
        joint = &source.addComponent<physics2d::Joint2DComponent>(world);
        set(target);
    }

    void set(std::uint64_t value)
    {
        auto properties = joint->editorAuthoringProperties();
        properties.cast<physics2d::Joint2DEditorProperties&>().bodyA = value;
        assert(joint->applyEditorAuthoringProperties(properties));
    }

    const char* prepare(std::uint64_t& value) const
    {
        return EditorSceneReferenceFocus::prepare(
            document, object, joint->id().value(), "bodyA", value);
    }

    core::SharedPtr<physics2d::PhysicsWorld2D> world;
    EditorSceneDocument document{false};
    physics2d::Joint2DComponent* joint = nullptr;
    std::uint64_t target = 0, alternative = 0, object = 0;
};

void statesAndCurrentIdentity()
{
    Fixture fixture;
    std::uint64_t output = 77;
    assert(!fixture.prepare(output) && output == fixture.target);
    fixture.set(fixture.alternative);
    assert(!fixture.prepare(output) && output == fixture.alternative);
    const auto reject = [&](std::uint64_t target, std::string_view diagnostic) {
        fixture.set(target);
        output = 77;
        assert(fixture.prepare(output) == diagnostic && output == 77);
    };
    reject(0, "EDITOR-REFERENCE-FOCUS-UNASSIGNED");
    reject(std::numeric_limits<std::uint64_t>::max(), "EDITOR-REFERENCE-FOCUS-TARGET-MISSING");
    reject(fixture.object, "EDITOR-REFERENCE-FOCUS-TARGET-TYPE");
    fixture.set(fixture.target);
    assert(fixture.document.scene()
            .destroyObject(scene::GameObjectId::fromValue(fixture.target))
            .ok());
    output = 77;
    assert(std::string_view(fixture.prepare(output)) == "EDITOR-REFERENCE-FOCUS-TARGET-MISSING"
        && output == 77);
    for (const auto path : {"", "missing", "length"}) {
        assert(EditorSceneReferenceFocus::prepare(
            fixture.document, fixture.object, fixture.joint->id().value(), path, output));
        assert(output == 77);
    }
    assert(EditorSceneReferenceFocus::prepare(
        fixture.document, 0, fixture.joint->id().value(), "bodyA", output));
    assert(
        EditorSceneReferenceFocus::prepare(fixture.document, fixture.object, 0, "bodyA", output));
    assert(output == 77);

    SceneInspectorProperty property;
    property.kind = SceneInspectorPropertyKind::SceneObjectReference;
    property.sceneObjectType = "*";
    property.sceneObjectReferenceValue = fixture.object;
    assert(
        Choices::targetStatus(fixture.document.scene(), property) == Choices::TargetStatus::Valid);
    property.sceneObjectReferenceValue = 0;
    property.resourceOptional = true;
    assert(
        Choices::targetStatus(fixture.document.scene(), property) == Choices::TargetStatus::None);
    property.sceneObjectType.clear();
    assert(Choices::targetStatus(fixture.document.scene(), property)
        == Choices::TargetStatus::InvalidMetadata);
    const auto invalid = Choices::build(fixture.document.scene(), property);
    assert(!invalid.editable && invalid.resourceDiagnostic == "EDITOR-INSPECTOR-REFERENCE-METADATA"
        && invalid.sceneObjectReferenceValue == 0 && invalid.enumOptions.size() == 1);
    std::puts(
        "PASS exact source field, current real target IDs, same names, missing/type/empty/metadata rejection");
}

void allocationAndCapacity()
{
    Fixture fixture;
    for (const auto budget : {1u, 256u, 2048u, 8192u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
        {
            core::CpuMemoryQuotaScope scope(quota);
            std::uint64_t output = 77;
            const auto* diagnostic = fixture.prepare(output);
            if (diagnostic)
                assert(std::string_view(diagnostic) == "EDITOR-REFERENCE-FOCUS-ALLOCATION-FAILED"
                    && output == 77);
            else
                assert(output == fixture.target);
        }
        assert(quota->liveBytes() == 0);
    }
    auto* source =
        fixture.document.scene().findObject(scene::GameObjectId::fromValue(fixture.object));
    for (unsigned index = 0; index < 20; ++index)
        source->addComponent<physics2d::Joint2DComponent>(fixture.world);
    std::uint64_t output = 77;
    assert(std::string_view(fixture.prepare(output)) == "EDITOR-REFERENCE-FOCUS-BUDGET"
        && output == 77);
    std::puts("PASS bounded source observation, OOM preserves output and releases core ownership");
}

void ordinaryCycle()
{
    Fixture fixture;
    std::uint64_t target = 0;
    assert(!fixture.prepare(target) && target == fixture.target);
    fixture.set(fixture.alternative);
    assert(!fixture.prepare(target) && target == fixture.alternative);
}

// Real authoring components; only pending endpoints, never a solver fixture.
class ShellFixture final {
public:
    explicit ShellFixture(const tests::RecoveryFixture& files)
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        shell.activate("workspace_3D");
        auto world = core::makeShared<physics2d::PhysicsWorld2D>(
            physics2d::WorldConfig{{0, 0}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f});
        assert(world->valid());
        shell.sceneDocument().setPhysicsWorld2DProvider([world] {
            return world;
        });
        const auto create = [&](std::string_view type) {
            assert(shell.submitCommand(CreateSceneObjectCommand("Same name")).status
                == EditorResponseStatus::Completed);
            assert(shell.submitCommand(AddSceneComponentCommand(type)).status
                == EditorResponseStatus::Completed);
            return shell.sceneDocument().selectedObject();
        };
        target = create("RigidBody2DComponent");
        alternative = create("RigidBody2DComponent");
        const auto anchor = create("RigidBody2DComponent");
        source = create("Joint2DComponent");
        shell.sceneDocument().scene().findObject(target)->set_local_transform(
            scene::Transform::fromTranslation({8, 1, 0}));
        shell.sceneDocument()
            .scene()
            .findObject(alternative)
            ->set_local_transform(scene::Transform::fromTranslation({-4, 2, 0}));
        for (const auto& component : shell.sceneDocument().scene().findObject(source)->components())
            if (std::string_view(component->scriptTypeName()) == "Joint2DComponent")
                joint = static_cast<physics2d::Joint2DComponent*>(component.get());
        assert(joint);
        command.objectId = source.value();
        command.componentId = joint->id().value();
        command.property = "bodyA";
        command.documentEpoch = shell.submitCommand(GetSceneInspectorSelectionQuery{})
                                    .sceneInspectorSelection.documentEpoch;
        // Serialization requires both endpoints. A distinct fixed endpoint also
        // keeps later bodyA swaps valid under the actual joint contract.
        auto endpoints = joint->editorAuthoringProperties();
        endpoints.cast<physics2d::Joint2DEditorProperties&>().bodyB = anchor.value();
        assert(joint->applyEditorAuthoringProperties(endpoints));
        set(target.value());
        assert(shell.retrySceneInspector());
    }

    void set(std::uint64_t value)
    {
        auto properties = joint->editorAuthoringProperties();
        properties.cast<physics2d::Joint2DEditorProperties&>().bodyA = value;
        assert(joint->applyEditorAuthoringProperties(properties));
    }

    EditorShell shell;
    FocusSceneReferenceCommand command;
    scene::GameObjectId target, alternative, source;
    physics2d::Joint2DComponent* joint = nullptr;
};

bool sameCamera(const EditorSceneViewportCamera& a, const EditorSceneViewportCamera& b)
{
    return a.target().x == b.target().x && a.target().y == b.target().y
        && a.target().z == b.target().z && a.yawDegrees() == b.yawDegrees()
        && a.pitchDegrees() == b.pitchDegrees() && a.distance() == b.distance();
}

void commandAndProtocol(const tests::RecoveryFixture& files)
{
    ShellFixture f(files);
    auto& shell = f.shell;
    SetSceneObjectNameCommand rename("Renamed");
    rename.objectId = f.source.value();
    assert(shell.submitCommand(std::move(rename)).status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    core::pmr::String before, after, diagnostic;
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), before, diagnostic));
    const auto dirty = shell.sceneDirty();
    const auto version = shell.stateVersion();
    const auto original = shell.sceneViewportCamera();
    assert(shell.submitCommand(f.command).status == EditorResponseStatus::Completed);
    assert(shell.sceneViewportCamera().target().x == 8 && shell.stateVersion() == version + 1);
    assert(shell.submitCommand(f.command).stateVersion == version + 1);
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, diagnostic)
        && before == after && shell.sceneDirty() == dirty
        && shell.sceneDocument().selectedObject() == f.source);
    assert(shell.sceneViewportCamera().distance() == original.distance());
    assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(f.source)->name() == "Renamed");
    f.set(f.alternative.value());
    assert(shell.submitCommand(f.command).status == EditorResponseStatus::Completed
        && shell.sceneViewportCamera().target().x == -4);
    auto stale = f.command;
    ++stale.documentEpoch;
    assert(shell.submitCommand(stale).diagnostic == EditorDiagnosticCode::StaleDocumentTarget);
    shell.activate("open_scene");
    assert(shell.submitCommand(f.command).diagnostic == EditorDiagnosticCode::WorkspaceBusy);
    shell.activate("scene_ui_cancel");
    assert(shell.submitCommand(BeginSceneTransformEditCommand{f.source.value()}).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(f.command).diagnostic == EditorDiagnosticCode::WorkspaceBusy);
    (void)shell.submitCommand(EndSceneTransformEditCommand{f.source.value()});
    f.set(f.target.value());
    const auto camera = shell.sceneViewportCamera();
    for (const auto bytes : {1u, 4096u, 16384u, 65536u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        EditorCommandEnvelope envelope(f.command);
        EditorResponse result;
        {
            core::CpuMemoryQuotaScope scope(quota);
            result = shell.submitCommand(std::move(envelope));
        }
        if (result.status != EditorResponseStatus::Completed)
            assert(sameCamera(camera, shell.sceneViewportCamera()));
        else {
            assert(shell.sceneViewportCamera().target().x == 8);
            f.set(f.alternative.value());
            assert(shell.submitCommand(f.command).status == EditorResponseStatus::Completed);
            f.set(f.target.value());
        }
        assert(quota->liveBytes() == 0);
    }
    tests::EditorProtocolFixture protocol(shell);
    char buffer[256];
    std::snprintf(buffer,
        sizeof(buffer),
        "\"objectId\":%llu,\"componentId\":%llu,\"documentEpoch\":%llu,\"property\":\"bodyA\"",
        static_cast<unsigned long long>(f.command.objectId),
        static_cast<unsigned long long>(f.command.componentId),
        static_cast<unsigned long long>(f.command.documentEpoch));
    const core::pmr::String fields(buffer);
    for (bool mcp : {false, true}) {
        const auto accepted = protocol.call("scene.reference.focus", fields, mcp);
        std::fprintf(stderr,
            "reference protocol %s: %.*s\n",
            mcp ? "MCP" : "REST",
            int(accepted.body_text().size()),
            accepted.body_text().data());
        assert(accepted.body_text().find(mcp ? "\"isError\":false" : "\"status\":\"completed\"")
            != std::string_view::npos);
        for (const auto extra : {",\"targetId\":1", ",\"objectId\":1", ",\"unknown\":1"}) {
            const auto rejected = protocol.call("scene.reference.focus", fields + extra, mcp);
            assert(mcp
                    ? rejected.body_text().find("unknown_tool_argument") != std::string_view::npos
                    : rejected.status().value() == 400);
        }
        assert(protocol.call("scene.reference.focus", {}, mcp)
                   .body_text()
                   .find("invalid_or_unsupported_command")
            != std::string_view::npos);
        const auto staleVersion =
            protocol.call("scene.reference.focus", fields + ",\"expectedStateVersion\":0", mcp);
        assert(staleVersion.body_text().find("stale_state_version") != std::string_view::npos);
    }
    assert(protocol.call("scene.reference.focus", fields + ",\"idempotencyKey\":\"reference-1\"")
               .status()
               .value()
        == 200);
    f.set(f.alternative.value());
    assert(protocol.call("scene.reference.focus", fields + ",\"idempotencyKey\":\"reference-1\"")
                .status()
                .value()
            == 200
        && shell.sceneViewportCamera().target().x == 8);
    auto other = fields;
    other.replace(other.find("bodyA"), 5, "bodyB");
    assert(protocol.call("scene.reference.focus", other + ",\"idempotencyKey\":\"reference-1\"")
               .body_text()
               .find("idempotency_key_conflict")
        != std::string_view::npos);
    f.set(0);
    assert(protocol.call("scene.reference.focus", fields)
               .body_text()
               .find("scene_reference_unavailable")
        != std::string_view::npos);
    assert(shell.submitCommand(NewSceneCommand{true}).status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(f.command).diagnostic == EditorDiagnosticCode::StaleDocumentTarget);
    std::puts(
        "PASS reference typed/REST/MCP current source, strict fields, epoch/version, replay fingerprint, no-op/Redo, busy and OOM camera isolation");
}

void uiScenarios(const tests::RecoveryFixture& files)
{
    for (const auto scenario :
        {"focus_valid", "focus_current", "focus_missing", "focus_failed", "focus_retry"}) {
        EditorShell shell;
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        core::pmr::String diagnostic;
        if (!EditorReferenceFocusSmoke::run(shell, scenario, diagnostic)) {
            std::fprintf(stderr, "%s: %s\n", scenario, diagnostic.c_str());
            assert(false);
        }
    }
}

void staleUiIsolation(const tests::RecoveryFixture& files)
{
    ShellFixture f(files);
    SceneInspectorProperty field;
    field.objectId = f.command.objectId;
    field.componentId = f.command.componentId;
    field.name = f.command.property;
    const auto action =
        core::pmr::String("scene_reference_focus_") + scenePropertyControlSource(field);
    const auto oldWidget = EditorControlInput::find(f.shell, action);
    assert(oldWidget.valid() && f.shell.acceptsInspectorInput(action, oldWidget));
    assert(f.shell.retrySceneInspector());
    assert(!f.shell.acceptsInspectorInput(action, oldWidget));
    SelectSceneObjectsCommand selection;
    selection.objectIds = {f.source.value(), f.target.value()};
    selection.primaryObjectId = f.source.value();
    assert(f.shell.submitCommand(std::move(selection)).status == EditorResponseStatus::Completed);
    assert(!f.shell.acceptsInspectorInput(action));
    const auto camera = f.shell.sceneViewportCamera();
    const auto version = f.shell.stateVersion();
    f.shell.activate(action);
    assert(sameCamera(camera, f.shell.sceneViewportCamera()) && f.shell.stateVersion() == version);
    assert(f.shell.submitCommand(SelectSceneObjectCommand{f.source.value()}).status
        == EditorResponseStatus::Completed);
    const auto current = EditorControlInput::find(f.shell, action);
    assert(current.valid() && f.shell.acceptsInspectorInput(action, current));
    f.shell.sceneDocument().setPhysicsWorld2DProvider([] {
        return core::SharedPtr<physics2d::PhysicsWorld2D>{};
    });
    assert(!f.shell.acceptsInspectorInput(action, current));
    f.shell.activate(action);
    assert(sameCamera(camera, f.shell.sceneViewportCamera()));
    std::puts(
        "PASS reference action stale Widget, full multi-selection and native owner ticket isolation");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        tests::RecoveryFixture files;
        ordinaryCycle();
        uiScenarios(files);
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned cycle = 0; cycle < 20; ++cycle) {
            ordinaryCycle();
            uiScenarios(files);
            const auto current = core::defaultCpuAllocator().stats();
            assert(current.liveBytes == baseline.liveBytes
                && current.allocationCount - current.deallocationCount
                    == baseline.allocationCount - baseline.deallocationCount);
        }
        std::puts(
            "PASS 20 ordinary warmed reference-focus preparation/UI/failure/retry cycles: core live bytes/count delta=0");
    } else {
        statesAndCurrentIdentity();
        allocationAndCapacity();
        tests::RecoveryFixture files;
        commandAndProtocol(files);
        staleUiIsolation(files);
        uiScenarios(files);
    }
}
