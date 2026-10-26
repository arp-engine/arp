#include "editor/remote/editor_remote_protocol.h"
#include "editor/tests/fixtures/transform_selection_fixture.h"
#include "editor/workspace/editor_scene_transform_transaction.h"

#include <cassert>
#include <cstdio>
#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;
using Result = EditorShell::TransformCommitResult;
using Frame = EditorSceneTransformFrame;

using Fixture = tests::TransformSelectionFixture;

void singlePopupQuarantine(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    assert(f.shell.submitCommand(SelectSceneObjectCommand{f.first.value()}).status
        == EditorResponseStatus::Completed);
    const auto dropdownId = f.shell.id("scene_component_add");
    auto* dropdown =
        dynamic_cast<EditorEnumDropdown*>(f.shell.runtime().findControlObject(dropdownId));
    assert(dropdown);
    ui::UiEventContext context(f.shell.runtime(), dropdownId);
    ui::UiControlInteraction click;
    click.kind = ui::UiControlInteractionKind::Click;
    click.widgetId = dropdownId;
    assert(dropdown->applyInteraction(click, context).ok());
    core::pmr::Vector<ui::WidgetId> popupOptions;
    for (const auto id : f.shell.runtime().canvas().traversal())
        if (f.shell.runtime().canvas().findWidget(id)->desc().name.find(
                "scene_component_add_option_")
            == 0)
            popupOptions.push_back(id);
    assert(!popupOptions.empty());
    auto token = f.prepare();
    assert(token->changeCount() == 1 && !token->committed());
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    const auto before = core::defaultCpuAllocator().stats();
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(f.shell.sceneTransformReadiness(*token) == Result::Ready);
        assert(f.shell.commitSceneTransform(*token) == Result::Committed);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(after.allocationCount == before.allocationCount);
    assert(token->committed() && f.local(f.first).translation.x == 3);
    assert(f.shell.inspectorState() == EditorShell::InspectorState::Pending);
    for (const auto id : popupOptions) {
        const auto* widget = f.shell.runtime().canvas().findWidget(id);
        assert(widget && !widget->visible() && !widget->enabled());
    }
    assert(f.shell.commitSceneTransform(*token) == Result::Consumed);
    token.reset();
    assert(f.shell.retrySceneInspector());
    f.history(true);
    assert(f.local(f.first).translation.x == 2);
    std::puts(
        "PASS single-selection live Inspector popup quarantine and allocation-free history publication");
}

void commitAndHistory(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    f.select(true);
    SetSceneObjectNameCommand rename("first with an existing allocated history name");
    rename.objectId = f.first.value();
    assert(f.shell.submitCommand(std::move(rename)).status == EditorResponseStatus::Completed);
    const auto childStart = f.local(f.child);
    const auto dropdownId = f.shell.id("scene_component_add");
    assert(!f.shell.runtime().canvas().findWidget(dropdownId)->enabled());
    auto token = f.prepare();
    assert(token->changeCount() == 2 && !token->committed());
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    const auto before = core::defaultCpuAllocator().stats();
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(f.shell.sceneTransformReadiness(*token) == Result::Ready);
        assert(f.shell.commitSceneTransform(*token) == Result::Committed);
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == before.allocationCount);
    assert(token->committed() && f.local(f.first).translation.x == 3
        && f.local(f.second).translation.x == 7);
    assert(Frame::same(f.local(f.child), childStart));
    assert(
        f.shell.sceneDocument().scene().findObject(f.child)->world_transform().translation.x == 4);
    assert(f.shell.inspectorState() == EditorShell::InspectorState::Pending);
    assert(f.shell.commitSceneTransform(*token) == Result::Consumed);
    token.reset();
    assert(f.shell.retrySceneInspector());
    f.history(true);
    assert(f.local(f.first).translation.x == 2 && f.local(f.second).translation.x == 6);
    assert(f.shell.sceneViewportObjectSelected(f.second)
        && f.shell.sceneViewportObjectSelected(f.child));
    f.history(false);
    assert(f.local(f.first).translation.x == 3 && f.local(f.second).translation.x == 7);
    f.history(true);
    f.history(true);
    assert(f.shell.sceneDocument().scene().findObject(f.first)->name() == "first");
    f.history(false);
    assert(f.shell.sceneDocument().scene().findObject(f.first)->name()
        == "first with an existing allocated history name");
    f.history(false);
    f.select(true);
    // All pivots use the numeric preparation, one complete Undo/Redo each.
    for (auto pivot : {EditorSceneTransformPivot::Active,
             EditorSceneTransformPivot::Center,
             EditorSceneTransformPivot::Individual}) {
        const auto a = f.local(f.first), b = f.local(f.second);
        TransformSceneSelectionCommand command;
        command.tool = EditorSceneTransformTool::Rotate;
        command.pivot = pivot;
        command.axis = 2;
        command.amount = 0.5;
        assert(f.shell.submitCommand(command).status == EditorResponseStatus::Completed);
        const auto changedA = f.local(f.first), changedB = f.local(f.second);
        f.history(true);
        assert(Frame::same(f.local(f.first), a) && Frame::same(f.local(f.second), b));
        f.history(false);
        assert(Frame::same(f.local(f.first), changedA) && Frame::same(f.local(f.second), changedB));
    }
    std::puts(
        "PASS batch commit allocations=0; hierarchy dedup, three pivots, one-entry exact Undo/Redo and selection preservation");
}

void lateFailures(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    auto token = f.prepare();
    Fixture other(files);
    assert(other.shell.commitSceneTransform(*token) == Result::Stale);
    Result wrong = Result::Ready;
    std::thread thread([&] {
        wrong = f.shell.commitSceneTransform(*token);
    });
    thread.join();
    assert(wrong == Result::WrongThread && !token->committed());
    auto changed = f.local(f.second);
    changed.translation.y = 1;
    f.set(f.second, changed);
    assert(f.shell.commitSceneTransform(*token) == Result::Stale
        && f.local(f.first).translation.x == 2);
    token = f.prepare();
    const auto parentPose = f.local(f.first);
    auto parent = f.shell.sceneDocument().scene().createObject("new parent").id();
    assert(f.shell.sceneDocument().scene().reparent(f.second, parent).ok());
    assert(f.shell.commitSceneTransform(*token) == Result::Stale);
    assert(Frame::same(f.local(f.first), parentPose));
    token = f.prepare();
    f.set(parent, scene::Transform::fromTranslation({1, 0, 0}));
    assert(f.shell.commitSceneTransform(*token) == Result::Stale);
    token = f.prepare();
    assert(f.shell.submitCommand(SelectSceneObjectCommand{f.first.value()}).status
        == EditorResponseStatus::Completed);
    assert(f.shell.commitSceneTransform(*token) == Result::Stale);
    f.select();
    token = f.prepare();
    f.apply();
    assert(f.shell.commitSceneTransform(*token) == Result::Stale);
    token = f.prepare();
    assert(f.shell.submitCommand(NewSceneCommand{true}).status == EditorResponseStatus::Completed);
    assert(f.shell.commitSceneTransform(*token) == Result::Stale);
    std::puts(
        "PASS wrong Shell/thread, changed member/parent/parent world/selection/history and replaced Scene reject before writes");
}

void failuresAndRedo(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    f.apply();
    f.history(true);
    const auto startA = f.local(f.first), startB = f.local(f.second);
    SelectSceneObjectsCommand attemptedSelection;
    attemptedSelection.objectIds = {f.first.value(), f.second.value(), f.child.value()};
    attemptedSelection.primaryObjectId = f.second.value();
    EditorCommandPayload selectionPayload(std::move(attemptedSelection));
    auto selectionQuota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(selectionQuota);
        assert(!f.shell.execute(selectionPayload).success);
    }
    assert(f.shell.sceneDocument().selectedObject() == f.first
        && f.shell.sceneViewportObjectSelected(f.second)
        && !f.shell.sceneViewportObjectSelected(f.child));
    unsigned rejected = 0, accepted = 0;
    for (std::uint64_t limit : {1u, 128u, 1024u, 4096u, 16384u, 65536u, 262144u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(limit);
        const auto baseline = core::defaultCpuAllocator().stats();
        TransformSceneSelectionCommand command;
        command.amount = 1;
        const char* diagnostic = nullptr;
        core::UniquePtr<EditorShell::PreparedSceneTransform> token;
        {
            core::CpuMemoryQuotaScope scope(quota);
            token = f.shell.prepareSceneTransform(command, diagnostic);
        }
        if (token)
            ++accepted;
        else {
            ++rejected;
            assert(diagnostic);
        }
        token.reset();
        const auto current = core::defaultCpuAllocator().stats();
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        assert(Frame::same(f.local(f.first), startA) && Frame::same(f.local(f.second), startB));
    }
    assert(rejected && accepted);
    auto late = f.prepare();
    auto external = f.local(f.second);
    external.translation.y = 4;
    f.set(f.second, external);
    assert(f.shell.commitSceneTransform(*late) == Result::Stale);
    f.set(f.second, startB);
    auto stillReady = f.prepare();
    const auto version = f.shell.stateVersion();
    auto noOp = f.prepare(0);
    assert(f.shell.commitSceneTransform(*noOp) == Result::NoChange);
    assert(f.shell.sceneTransformReadiness(*stillReady) == Result::Ready);
    assert(f.shell.stateVersion() == version);
    f.apply(0);
    assert(f.shell.stateVersion() == version);
    // Preparation failures and no-op did not destroy the existing Redo.
    f.history(false);
    assert(f.local(f.first).translation.x == 3 && f.local(f.second).translation.x == 7);
    // A conflict on the last member cannot partially undo the first.
    auto conflict = f.local(f.second);
    conflict.translation.z = 2;
    f.set(f.second, conflict);
    assert(f.shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Failed);
    assert(f.local(f.first).translation.x == 3 && f.local(f.second).translation.z == 2);
    conflict.translation.z = 0;
    f.set(f.second, conflict);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        const auto result = f.shell.execute(UndoSceneCommand{});
        assert(!result.success && !result.stateChanged);
    }
    assert(f.local(f.first).translation.x == 3 && f.local(f.second).translation.x == 7);
    f.history(true);
    // The Redo branch has the same all-member conflict and allocation gate.
    external = f.local(f.second);
    external.translation.z = 3;
    f.set(f.second, external);
    assert(f.shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Failed);
    assert(f.local(f.first).translation.x == 2);
    external.translation.z = 0;
    f.set(f.second, external);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!f.shell.execute(RedoSceneCommand{}).success);
    }
    assert(f.local(f.first).translation.x == 2 && f.local(f.second).translation.x == 6);
    f.history(false);
    // A changed command clears Redo only after successful publication.
    f.history(true);
    auto token = f.prepare(2);
    assert(f.shell.commitSceneTransform(*token) == Result::Committed
        && token->retiredRedoCount() == 1);
    const auto a = f.local(f.first);
    f.history(false);
    assert(Frame::same(f.local(f.first), a));
    std::printf(
        "PASS preparation quotas rejected=%u accepted=%u, no-op/Redo preservation, all-or-none conflict and history-capacity failure\n",
        rejected,
        accepted);
}

void capacity(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    for (unsigned i = 0; i < 257; ++i) {
        auto token = f.prepare();
        assert(f.shell.commitSceneTransform(*token) == Result::Committed);
    }
    for (unsigned i = 0; i < 256; ++i)
        f.history(true);
    assert(f.local(f.first).translation.x == 3 && f.local(f.second).translation.x == 7);
    const auto version = f.shell.stateVersion();
    f.history(true);
    assert(f.shell.stateVersion() == version && f.local(f.first).translation.x == 3);
    std::puts(
        "PASS explicit 256-entry budget retires only oldest Undo, complete 256 batch undo operations");
}

void replacedHistory(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    f.apply();
    EditorSceneDocument candidate;
    const auto a = candidate.createObject("replacement first");
    const auto b = candidate.createObject("replacement second");
    assert(a == f.first && b == f.second);
    candidate.scene().findObject(a)->set_local_transform(f.local(f.first));
    candidate.scene().findObject(b)->set_local_transform(f.local(f.second));
    assert(f.shell.sceneDocument().replaceWith(std::move(candidate)));
    // Reusing numeric IDs and exact poses cannot authorize old Scene history.
    assert(f.shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Failed);
    assert(f.local(a).translation.x == 3 && f.local(b).translation.x == 7);
    std::puts("PASS batch history refuses replacement Scene with identical IDs and numeric poses");
}

void protocol(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    EditorRemoteProtocol protocol("01234567890123456789012345678901");
    const auto request = [&](std::string_view body, bool mcp = false) {
        network::http::Request request;
        request.set_method(network::http::Method::Post);
        request.set_target(mcp ? "/mcp" : "/api/v1/commands");
        request.add_header("Authorization", "Bearer 01234567890123456789012345678901");
        request.add_header("Accept", "application/json");
        request.add_header("MCP-Protocol-Version", "2025-11-25");
        request.set_content_type(network::http::ContentType::Json);
        request.set_body_text(body);
        return protocol.handle(request, f.shell);
    };
    const auto body =
        R"({"schemaVersion":1,"command":"scene.selection.transform","tool":"move","space":"world","pivot":"center","axis":0,"amount":1,"idempotencyKey":"batch-once"})";
    assert(request(body).status().value() == 200);
    assert(request(body).status().value() == 200 && f.local(f.first).translation.x == 3);
    assert(
        request(
            R"({"schemaVersion":1,"command":"scene.selection.transform","tool":"move","space":"world","pivot":"center","axis":3,"amount":1})")
            .status()
            .value()
        == 400);
    assert(
        request(
            R"({"schemaVersion":1,"command":"scene.selection.transform","tool":"move","space":"world","pivot":"center","axis":0,"amount":1,"objectId":1})")
            .status()
            .value()
        == 400);
    assert(
        request(
            R"({"schemaVersion":1,"command":"scene.selection.transform","tool":"move","space":"world","pivot":"center","axis":0,"amount":1,"expectedStateVersion":0})")
            .body_text()
            .find("stale_state_version")
        != std::string_view::npos);
    assert(
        request(
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.selection.transform","arguments":{"tool":"move","space":"world","pivot":"individual","axis":1,"amount":2}}})",
            true)
            .body_text()
            .find(R"("isError":false)")
        != std::string_view::npos);
    assert(f.local(f.first).translation.y == 2 && f.local(f.second).translation.y == 2);
    std::puts(
        "PASS REST/MCP shared batch command, strict axis/fields, stale version and idempotent replay");
}

void lifecycle(const tests::RecoveryFixture& files)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            Fixture f(files);
            f.select(true);
            f.apply();
            f.history(true);
            f.history(false);
            auto abandoned = f.prepare();
            assert(f.shell.drawCommands().ok());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary batch commit/Undo/Redo/abandon/draw/destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifecycle(files);
    else {
        singlePopupQuarantine(files);
        commitAndHistory(files);
        lateFailures(files);
        failuresAndRedo(files);
        capacity(files);
        replacedHistory(files);
        protocol(files);
    }
}
