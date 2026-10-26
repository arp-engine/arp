#include "2d/physics_components.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_control_input.h"
#include "editor/validation/editor_inspector_label_smoke.h"
#include "scene/camera_temporal_component.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;

const ui::Widget& widget(EditorShell& shell, std::string_view source)
{
    const auto* result =
        shell.runtime().canvas().findWidget(EditorControlInput::find(shell, source));
    assert(result);
    return *result;
}

void verifyLayout(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    core::pmr::String diagnostic;
    assert(EditorInspectorLabelSmoke::run(shell, "label_nested", diagnostic));
    auto* owner = shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
    auto& temporal = owner->addComponent<scene::CameraTemporalComponent>();
    temporal.setPreviousProjection(68.f, 1.5f, .1f, 100.f);
    assert(shell.retrySceneInspector());
    const auto properties = shell.submitCommand(GetSceneInspectorQuery{}).sceneInspector.properties;
    bool nested = false, resource = false, runtime = false;
    for (const auto& property : properties) {
        if (property.componentType == "TransformComponent")
            continue;
        const auto source = scenePropertyControlSource(property);
        const auto& title = widget(shell, source + "_label");
        assert(title.desc().text == property.name && title.desc().localRect.width == 288.f);
        assert(!title.desc().acceptsFocus && !title.desc().acceptsPointerInput);
        const auto valueSource = source
            + (property.editable && property.kind == SceneInspectorPropertyKind::Vector3 ? "_x"
                                                                                         : "");
        const auto& value = widget(shell, valueSource);
        assert(
            value.desc().localRect.y >= title.desc().localRect.y + title.desc().localRect.height);
        if (property.editable && property.kind == SceneInspectorPropertyKind::Vector3)
            for (const auto axis : {"_x", "_y", "_z"}) {
                const auto& rect = widget(shell, source + axis).desc().localRect;
                assert(rect.x >= title.desc().localRect.x
                    && rect.x + rect.width
                        <= title.desc().localRect.x + title.desc().localRect.width);
            }
        nested |= property.name == "localBounds.extents";
        resource |= property.name == "spriteResourceId";
        runtime |= !property.editable;
    }
    assert(nested && resource && runtime);
    // Runtime-only fixture components deliberately have no authoring codec.
    assert(owner->removeComponent(temporal.id()));
    assert(shell.retrySceneInspector());
    const auto object = shell.sceneDocument().selectedObject();
    core::pmr::String before, after;
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), before, diagnostic));
    core::pmr::String retiredSource;
    for (const auto& property : properties)
        if (property.name == "localBounds.extents")
            retiredSource = scenePropertyControlSource(property) + "_x";
    const auto retired = EditorControlInput::find(shell, retiredSource);
    assert(EditorInspectorLabelSmoke::run(shell, "label_locked", diagnostic));
    // The previous object's dynamic widgets are retired by selection replacement.
    assert(!shell.acceptsInspectorInput(retiredSource, retired));
    assert(!shell.runtime().canvas().findWidget(retired));
    assert(shell.sceneDocument().scene().findObject(object));
    const auto selected = shell.sceneDocument().selectedObject();
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), before, diagnostic));
    assert(shell.retrySceneInspector());
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, diagnostic)
        && before == after);
    for (const auto& property :
        shell.submitCommand(GetSceneInspectorQuery{}).sceneInspector.properties) {
        if (property.componentType == "TransformComponent")
            continue;
        assert(!property.editable);
        const auto source = scenePropertyControlSource(property);
        assert(widget(shell, source + "_label").desc().text == property.name);
        assert(!widget(shell, source).desc().acceptsPointerInput);
    }
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!shell.retrySceneInspector());
    }
    assert(shell.inspectorState() == EditorShell::InspectorState::Failed);
    assert(shell.retrySceneInspector() && shell.sceneDocument().selectedObject() == selected);
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, diagnostic)
        && before == after);
}

void collection(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    assert(shell.submitCommand(CreateSceneObjectCommand("Collection layout")).status
        == EditorResponseStatus::Completed);
    auto* object = shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
    for (const bool populated : {false, true}) {
        physics2d::ShapeDesc shape;
        if (populated)
            shape.points = {{-1, 0}, {1, 0}};
        auto& collider = object->addComponent<physics2d::Collider2DComponent>(
            nullptr, physics2d::BodyHandle{}, std::move(shape));
        assert(shell.retrySceneInspector());
        bool found = false;
        for (const auto& property :
            shell.submitCommand(GetSceneInspectorQuery{}).sceneInspector.properties) {
            if (property.name != "points")
                continue;
            const auto source = scenePropertyControlSource(property);
            const auto& title = widget(shell, source + "_label");
            const auto& first =
                widget(shell, source + (populated ? "_point_0_label" : "_point_add"));
            assert(first.desc().localRect.y
                >= title.desc().localRect.y + title.desc().localRect.height);
            found = true;
        }
        assert(found && object->removeComponent(collider.id()));
    }
    std::puts(
        "PASS empty/populated collection title owns a separate row; first item/add cannot overlap it");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        verifyLayout(files);
        collection(files);
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary Inspector layout/lock/failure/retry/destruction cycles: core live bytes/count delta=0");
    return 0;
}
