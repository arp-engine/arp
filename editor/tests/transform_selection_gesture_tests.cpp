#include "editor/remote/editor_remote_protocol.h"
#include "editor/tests/fixtures/transform_selection_fixture.h"
#include "editor/validation/editor_transform_gesture_input.h"
#include "editor/workspace/editor_scene_transaction.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::editor;
using Fixture = tests::TransformSelectionFixture;
using Tool = EditorSceneTransformTool;
using Pivot = EditorSceneTransformPivot;
using Frame = EditorSceneTransformFrame;
using Status = EditorResponseStatus;

std::uint64_t begin(Fixture& f,
    Tool tool = Tool::Move,
    Pivot pivot = Pivot::Center,
    unsigned axis = 0,
    EditorSceneTransformSpace space = EditorSceneTransformSpace::World)
{
    TransformSceneSelectionCommand operation;
    operation.tool = tool;
    operation.pivot = pivot;
    operation.axis = axis;
    operation.space = space;
    assert(f.shell.submitCommand(BeginSceneSelectionTransformEditCommand{operation}).status
        == Status::Completed);
    const auto id = f.shell.activeSceneSelectionTransformEditId();
    assert(id && f.shell.sceneSelectionTransformEditCurrent(id));
    return id;
}

void update(Fixture& f, std::uint64_t id, double amount)
{
    assert(f.shell.submitCommand(UpdateSceneSelectionTransformEditCommand{id, amount}).status
        == Status::Completed);
}

void end(Fixture& f, std::uint64_t id)
{
    assert(f.shell.submitCommand(EndSceneSelectionTransformEditCommand{id}).status
        == Status::Completed);
    assert(!f.shell.activeSceneSelectionTransformEditId());
}

bool close(core::Vec3 a, core::Vec3 b)
{
    return core::distance(a, b) < 0.0001f;
}

void cumulativeAndHistory(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    f.select(true);
    const auto version = f.shell.stateVersion();
    const auto id = begin(f);
    assert(f.shell.stateVersion() == version);
    update(f, id, 0.25);
    update(f, id, 0.5);
    assert(f.local(f.first).translation.x == 2.5f && f.local(f.second).translation.x == 6.5f);
    assert(f.local(f.child).translation.x == 1);
    assert(f.shell.sceneDocument().scene().findObject(f.child)->world_transform().translation.x
        == 3.5f);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    const auto before = core::defaultCpuAllocator().stats();
    {
        core::CpuMemoryQuotaScope scope(quota);
        end(f, id);
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == before.allocationCount);
    assert(
        f.shell.submitCommand(EndSceneSelectionTransformEditCommand{id}).status == Status::Failed);
    f.history(true);
    assert(f.local(f.first).translation.x == 2 && f.local(f.second).translation.x == 6);
    const auto undone = f.shell.stateVersion();
    f.history(true);
    assert(f.shell.stateVersion() == undone); // exactly one entry
    f.history(false);
    assert(f.local(f.first).translation.x == 2.5f && f.local(f.second).translation.x == 6.5f);
    assert(f.shell.sceneViewportObjectSelected(f.child)
        && f.shell.sceneViewportObjectSelected(f.second));
    std::puts(
        "PASS frozen cumulative group/selected descendant, zero-allocation End, one Undo/Redo with preserved selection");
}

void failureAndNoOp(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    f.apply();
    f.history(true);
    const auto start = f.local(f.first);
    auto id = begin(f);
    update(f, id, 1);
    const auto accepted = f.local(f.first);
    assert(f.shell
               .submitCommand(UpdateSceneSelectionTransformEditCommand{
                   id, std::numeric_limits<double>::infinity()})
               .status
        == Status::Failed);
    assert(
        Frame::same(f.local(f.first), accepted) && f.shell.sceneSelectionTransformEditCurrent(id));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(f.shell.submitCommand(UpdateSceneSelectionTransformEditCommand{id, 2}).status
            == Status::Failed);
    }
    assert(
        Frame::same(f.local(f.first), accepted) && f.shell.sceneSelectionTransformEditCurrent(id));
    update(f, id, 2);
    update(f, id, 0);
    end(f, id);
    assert(Frame::same(f.local(f.first), start));
    f.history(false); // return to start preserved old Redo
    assert(f.local(f.first).translation.x == 3);
    id = begin(f);
    end(f, id);
    f.history(true);
    assert(f.local(f.first).translation.x == 2);
    BeginSceneSelectionTransformEditCommand command;
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(f.shell.submitCommand(command).status == Status::Failed);
    }
    assert(!f.shell.activeSceneSelectionTransformEditId());
    std::puts(
        "PASS invalid/OOM Begin and Update, accepted pose retained, continued input, return-to-start preserves Redo");
}

void pivots(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    auto id = begin(f, Tool::Rotate, Pivot::Center, 2);
    update(f, id, 1.5707963267948966);
    assert(close(f.local(f.first).translation, {4, -2, 0})
        && close(f.local(f.second).translation, {4, 2, 0}));
    end(f, id);
    f.history(true);
    id = begin(f, Tool::Rotate, Pivot::Individual, 2);
    update(f, id, 0.5);
    assert(close(f.local(f.first).translation, {2, 0, 0})
        && close(f.local(f.second).translation, {6, 0, 0}));
    end(f, id);
    f.history(true);
    auto a = f.local(f.first), b = f.local(f.second);
    a.scale = {2, 1, 1};
    b.scale = {4, 1, 1};
    f.set(f.first, a);
    f.set(f.second, b);
    id = begin(f, Tool::Scale);
    update(f, id, 1);
    assert(close(f.local(f.first).scale, {3, 1, 1}) && close(f.local(f.second).scale, {6, 1, 1}));
    assert(close(f.local(f.first).translation, {1, 0, 0})
        && close(f.local(f.second).translation, {7, 0, 0}));
    update(f, id, -4);
    assert(f.local(f.first).scale.x == -2 && f.local(f.second).scale.x == -4);
    assert(f.local(f.first).translation.x == 6 && f.local(f.second).translation.x == 2);
    end(f, id);
    f.history(true);
    b.rotation = core::Quat::fromAxisAngle({0, 0, 1}, 0.5f);
    f.set(f.second, b);
    id = begin(f, Tool::Scale);
    assert(f.shell.submitCommand(UpdateSceneSelectionTransformEditCommand{id, 1}).status
        == Status::Failed);
    assert(Frame::same(f.local(f.first), a) && Frame::same(f.local(f.second), b));
    update(f, id, 0);
    end(f, id);
    id = begin(f, Tool::Rotate, Pivot::Individual, 0, EditorSceneTransformSpace::Local);
    update(f, id, 0.5);
    const auto turn = core::Quat::fromAxisAngle({1, 0, 0}, 0.5f);
    assert(std::abs(std::abs(core::dot(f.local(f.second).rotation, b.rotation * turn)) - 1)
        < 0.00001f);
    end(f, id);
    std::puts(
        "PASS Center/Individual rotate, proportional and negative scale, whole-group shear refusal, distinct local axes");
}

void staleAndBusy(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    auto id = begin(f);
    update(f, id, 0.5);
    const auto version = f.shell.stateVersion();
    for (EditorCommandPayload payload : {EditorCommandPayload(UndoSceneCommand{}),
             EditorCommandPayload(RedoSceneCommand{}),
             EditorCommandPayload(SetSceneTransformPivotCommand{Pivot::Individual}),
             EditorCommandPayload(SelectSceneObjectCommand{f.second.value()})}) {
        const auto response = f.shell.submitCommand(std::move(payload));
        assert(response.status == Status::Failed
            && response.diagnostic == EditorDiagnosticCode::WorkspaceBusy);
    }
    assert(f.shell.stateVersion() == version);
    auto external = f.local(f.second);
    external.translation.y = 7;
    f.set(f.second, external);
    assert(f.shell.submitCommand(UpdateSceneSelectionTransformEditCommand{id, 1}).status
        == Status::Failed);
    assert(
        !f.shell.activeSceneSelectionTransformEditId() && Frame::same(f.local(f.second), external));
    assert(f.local(f.first).translation.x == 2.5f); // no reverse writes over independent edits
    // A newer no-op gesture can reuse the old allocation and stateVersion.
    // A prepared replacement must also bind its monotonic gesture ID.
    id = begin(f);
    EditorSceneDocument staleCandidate;
    core::pmr::String staleDiagnostic;
    auto staleReplacement = f.shell.prepareSceneReplacement(staleCandidate, staleDiagnostic);
    assert(staleReplacement);
    end(f, id);
    const auto newer = begin(f);
    assert(newer > id);
    assert(f.shell.sceneReplacementReadiness(*staleReplacement, staleCandidate)
        != EditorShell::SceneReplacementResult::Ready);
    end(f, newer);
    id = begin(f);
    update(f, id, 0.25);
    EditorSceneDocument candidate;
    core::pmr::String diagnostic;
    auto replacement = f.shell.prepareSceneReplacement(candidate, diagnostic);
    assert(replacement && diagnostic.empty());
    assert(f.shell.commitSceneReplacement(*replacement, candidate)
        == EditorShell::SceneReplacementResult::Committed);
    assert(!f.shell.activeSceneSelectionTransformEditId());
    const auto a = f.shell.sceneDocument().createObject("new first");
    const auto b = f.shell.sceneDocument().createObject("new second");
    f.first = a;
    f.second = b;
    f.select();
    const auto next = begin(f);
    assert(next > id);
    assert(
        f.shell.submitCommand(EndSceneSelectionTransformEditCommand{id}).status == Status::Failed);
    assert(f.shell.sceneSelectionTransformEditCurrent(next));
    end(f, next);
    std::puts(
        "PASS Busy writes, independent native pose invalidation, cold replacement retirement, monotonic old-ID refusal");
}

void pointerAndToolbar(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    f.shell.activate("scene_transform_pivot");
    assert(f.shell.sceneTransformPivot() == Pivot::Center);
    core::Vec3 origin;
    Frame frame;
    assert(!f.shell.sceneTransformSelectionView(origin, frame) && close(origin, {4, 0, 0}));
    assert(f.shell.submitCommand(FocusSceneSelectionCommand{}).status == Status::Completed);
    EditorTransformGestureInput input(f.shell, 1280, 720);
    assert(input.linear(false));
    assert(close(f.local(f.first).translation, {2.5f, 0, 0})
        && close(f.local(f.second).translation, {6.5f, 0, 0}));
    assert(!f.shell.activeSceneSelectionTransformEditId());
    f.history(true);
    assert(close(f.local(f.first).translation, {2, 0, 0}));
    assert(input.rotate()); // FocusLost + PointerUp must commit once
    const auto rotated = f.local(f.first);
    assert(!close(rotated.translation, {2, 0, 0}));
    f.history(true);
    assert(close(f.local(f.first).translation, {2, 0, 0}));
    const auto version = f.shell.stateVersion();
    f.history(true);
    assert(f.shell.stateVersion() == version);
    f.history(false);
    assert(Frame::same(f.local(f.first), rotated));
    f.shell.activate("scene_transform_pivot");
    assert(f.shell.sceneTransformPivot() == Pivot::Individual);
    assert(
        !f.shell.sceneTransformSelectionView(origin, frame) && close(origin, rotated.translation));
    f.shell.activate("scene_transform_pivot");
    assert(f.shell.sceneTransformPivot() == Pivot::Active);
    assert(f.shell.drawCommands().ok());
    std::puts(
        "PASS actual pointer pivot hit-test, group Move/Rotate, FocusLost then PointerUp one history, toolbar cycle/display");
}

void contextAndSnap(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    auto id = begin(f);
    update(f, id, 0.25);
    assert(f.shell.submitCommand(RequestEditorExitCommand{}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    assert(f.shell.sceneDocument().scene().reparent(f.second, f.first).ok());
    assert(f.shell.submitCommand(UpdateSceneSelectionTransformEditCommand{id, 1}).status
        == Status::Failed);
    assert(!f.shell.activeSceneSelectionTransformEditId());
    assert(f.local(f.first).translation.x == 2.25f);
    id = begin(f);
    assert(f.shell.sceneDocument().select(f.second));
    assert(
        f.shell.submitCommand(EndSceneSelectionTransformEditCommand{id}).status == Status::Failed);
    assert(!f.shell.activeSceneSelectionTransformEditId());
    assert(f.shell.sceneDocument()
            .scene()
            .reparent(f.second, f.shell.sceneDocument().scene().rootId())
            .ok());
    f.set(f.first, scene::Transform::fromTranslation({2, 0, 0}));
    f.set(f.second, scene::Transform::fromTranslation({6, 0, 0}));
    f.select();
    EditorSceneTransformSnap snap;
    snap.enabled = true;
    snap.moveStep = 0.75;
    assert(f.shell.submitCommand(SetSceneTransformSnapCommand{snap}).status == Status::Completed);
    assert(f.shell.submitCommand(SetSceneTransformPivotCommand{Pivot::Center}).status
        == Status::Completed);
    const auto version = f.shell.stateVersion();
    assert(f.shell.submitCommand(SetSceneTransformPivotCommand{Pivot::Center}).status
        == Status::Completed);
    assert(f.shell.stateVersion() == version);
    assert(f.shell.submitCommand(FocusSceneSelectionCommand{}).status == Status::Completed);
    EditorTransformGestureInput input(f.shell, 1920, 1080);
    assert(input.linear(false));
    assert(close(f.local(f.first).translation, {2.75f, 0, 0})
        && close(f.local(f.second).translation, {6.75f, 0, 0}));
    f.history(true);
    assert(input.linear(false, -1));
    assert(close(f.local(f.first).translation, {1.25f, 0, 0})
        && close(f.local(f.second).translation, {5.25f, 0, 0}));
    std::puts(
        "PASS native parent/selection invalidation, exit Busy, pivot no-op, multi-selection substep snap/negative input at 1080p mapping");
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
        R"({"schemaVersion":1,"command":"scene.transform.pivot.set","pivot":"center","idempotencyKey":"pivot-once"})";
    assert(request(body).status().value() == 200);
    const auto version = f.shell.stateVersion();
    assert(request(body).status().value() == 200 && f.shell.stateVersion() == version);
    assert(request(R"({"schemaVersion":1,"command":"scene.transform.pivot.set","pivot":"Center"})")
               .status()
               .value()
        == 400);
    assert(
        request(
            R"({"schemaVersion":1,"command":"scene.transform.pivot.set","pivot":"center","amount":1})")
            .status()
            .value()
        == 400);
    assert(
        request(
            R"({"schemaVersion":1,"command":"scene.transform.pivot.set","pivot":"center","expectedStateVersion":0})")
            .body_text()
            .find("stale_state_version")
        != std::string_view::npos);
    assert(
        request(
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.transform.pivot.set","arguments":{"pivot":"individual"}}})",
            true)
            .body_text()
            .find(R"("isError":false)")
        != std::string_view::npos);
    assert(f.shell.sceneTransformPivot() == Pivot::Individual);
    const auto id = begin(f);
    assert(f.shell.submitCommand(SetSceneTransformPivotCommand{Pivot::Center}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    end(f, id);
    std::puts(
        "PASS REST/MCP pivot strict fields/enums, stale version, normal idempotent replay, Busy gate");
}

void lifecycle(const tests::RecoveryFixture& files)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            Fixture f(files);
            f.select(true);
            const auto id = begin(f);
            update(f, id, 0.25);
            update(f, id, 0.5);
            end(f, id);
            f.history(true);
            f.history(false);
            assert(f.shell.drawCommands().ok());
            (void)begin(f); // destruction with an abandoned active gesture
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary group Begin/Update/End/Undo/Redo/draw/active destruction cycles core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifecycle(files);
    else {
        cumulativeAndHistory(files);
        failureAndNoOp(files);
        pivots(files);
        staleAndBusy(files);
        pointerAndToolbar(files);
        contextAndSnap(files);
        protocol(files);
    }
}
