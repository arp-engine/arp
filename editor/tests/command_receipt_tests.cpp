#include "editor/command/editor_command.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::editor;

EditorCommandEnvelope request(std::string_view key = {})
{
    EditorCommandEnvelope command{SetSceneTransformSettingsCommand{}};
    command.commandId = 71;
    command.correlationId = 17;
    command.idempotencyKey = key;
    if (!key.empty())
        command.idempotencyFingerprint.assign(256, 'f');
    return command;
}

// Populate responses before quota rejection so the test isolates dispatcher
// allocation after execution, rather than allocation inside a domain executor.
class ReceiptExecutor final : public IEditorCommandExecutor {
public:
    explicit ReceiptExecutor(EditorCommandOutcome initial = {}) : outcome(std::move(initial)) {}

    void prepare(bool dynamic)
    {
        outcome = {};
        if (!dynamic)
            return;
        outcome.hasSnapshot = true;
        outcome.snapshot.scriptText.assign(512, 's');
        outcome.snapshot.sceneExportTask.diagnostic.assign(128, 'd');
        outcome.hasSceneInspector = true;
        outcome.sceneInspector.objectName.assign(128, 'o');
        outcome.sceneInspector.properties.emplace_back();
        outcome.sceneInspector.properties.back().stringValue.assign(128, 'p');
        outcome.hasSceneInspectorSelection = true;
        outcome.sceneInspectorSelection.documentEpoch = 42;
        outcome.sceneInspectorSelection.selectedCount = 2;
        outcome.sceneInspectorSelection.objects.emplace_back();
        outcome.sceneInspectorSelection.objects.back().inspector.objectName.assign(128, 'g');
        outcome.sceneInspectorSelection.fields.emplace_back();
        outcome.sceneInspectorSelection.fields.back().path.assign(128, 'v');
        outcome.hasSceneHierarchy = true;
        outcome.sceneHierarchy.entries.emplace_back();
        outcome.sceneHierarchy.entries.back().name.assign(128, 'h');
        outcome.hasSceneComponentCatalog = true;
        outcome.sceneComponentCatalog.entries.emplace_back();
        outcome.sceneComponentCatalog.entries.back().typeName.assign(128, 'c');
    }

    EditorCommandOutcome execute(const EditorCommandPayload&) override
    {
        ++calls;
        if (rejectAfterExecute)
            quota->setLimitBytes(1);
        return std::move(outcome);
    }

    EditorCommandOutcome outcome;
    core::SharedPtr<core::CpuMemoryQuota> quota;
    unsigned calls = 0;
    bool rejectAfterExecute = false;
};

void preparationFailure()
{
    EditorCommandDispatcher dispatcher(1);
    ReceiptExecutor executor;
    const auto first = request("accepted");
    assert(dispatcher.dispatch(first, executor).status == EditorResponseStatus::Completed);
    const auto second = request("pending");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    EditorResponse rejected;
    bool threw = false;
    {
        core::CpuMemoryQuotaScope scope(quota);
        try {
            rejected = dispatcher.dispatch(second, executor);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
    }
    std::printf("preparation rejection: threw=%u calls=%u version=%llu\n",
        threw,
        executor.calls,
        static_cast<unsigned long long>(dispatcher.stateVersion()));
    assert(!threw && executor.calls == 1 && dispatcher.stateVersion() == 1);
    assert(rejected.status == EditorResponseStatus::Rejected
        && rejected.diagnostic == EditorDiagnosticCode::CommandPreparationFailed);
    assert(dispatcher.dispatch(first, executor).status == EditorResponseStatus::Completed);
    assert(executor.calls == 1 && quota->liveBytes() == 0);
    assert(dispatcher.dispatch(second, executor).status == EditorResponseStatus::Completed);
    assert(executor.calls == 2 && dispatcher.stateVersion() == 2);
    std::puts("PASS preparation rejects before execute, keeps previous FIFO entry, retry succeeds");
}

void checkPayload(const EditorResponse& response)
{
    assert(response.hasSnapshot && response.snapshot.scriptText.size() == 512
        && response.snapshot.scriptText.find_first_not_of('s') == core::pmr::String::npos);
    assert(response.snapshot.sceneExportTask.diagnostic.size() == 128);
    assert(response.hasSceneInspector && response.sceneInspector.objectName.size() == 128
        && response.sceneInspector.properties.size() == 1
        && response.sceneInspector.properties.front().stringValue.size() == 128);
    assert(response.hasSceneHierarchy && response.sceneHierarchy.entries.size() == 1
        && response.sceneHierarchy.entries.front().name.size() == 128);
    assert(response.hasSceneInspectorSelection
        && response.sceneInspectorSelection.documentEpoch == 42
        && response.sceneInspectorSelection.selectedCount == 2
        && response.sceneInspectorSelection.objects.size() == 1
        && response.sceneInspectorSelection.objects.front().inspector.objectName.size() == 128
        && response.sceneInspectorSelection.fields.size() == 1
        && response.sceneInspectorSelection.fields.front().path.size() == 128);
    assert(response.hasSceneComponentCatalog && response.sceneComponentCatalog.entries.size() == 1
        && response.sceneComponentCatalog.entries.front().typeName.size() == 128);
}

void postExecution(bool retained, bool success = true)
{
    auto quota = core::makeShared<core::CpuMemoryQuota>();
    {
        EditorCommandDispatcher dispatcher(2);
        ReceiptExecutor executor;
        executor.prepare(true);
        executor.outcome.success = success;
        executor.outcome.diagnostic =
            success ? EditorDiagnosticCode::None : EditorDiagnosticCode::WorkspaceBusy;
        executor.quota = quota;
        executor.rejectAfterExecute = true;
        auto command = request(retained ? "snapshot" : "");
        EditorResponse receipt;
        bool threw = false;
        {
            core::CpuMemoryQuotaScope scope(quota);
            try {
                receipt = dispatcher.dispatch(command, executor);
            } catch (const std::bad_alloc&) {
                threw = true;
            }
        }
        assert(!threw && executor.calls == 1 && receipt.commandId == 71
            && receipt.correlationId == 17 && receipt.hasStateVersion
            && receipt.stateVersion == (success ? 1u : 0u)
            && dispatcher.stateVersion() == receipt.stateVersion);
        assert(receipt.status
            == (success ? EditorResponseStatus::Completed : EditorResponseStatus::Failed));
        if (!retained) {
            checkPayload(receipt);
            assert(!quota->liveBytes());
        } else {
            assert(!receipt.hasSnapshot && !receipt.hasSceneInspector
                && !receipt.hasSceneInspectorSelection && !receipt.hasSceneHierarchy
                && !receipt.hasSceneComponentCatalog && !receipt.hasSceneTransformSettings
                && receipt.diagnostic == EditorDiagnosticCode::ResponsePayloadUnavailable);
            command.commandId = 72;
            command.correlationId = 18;
            command.hasExpectedStateVersion = true;
            command.expectedStateVersion = 99;
            {
                core::CpuMemoryQuotaScope scope(quota);
                const auto unavailable = dispatcher.dispatch(command, executor);
                assert(unavailable.commandId == 71 && unavailable.correlationId == 18
                    && unavailable.status == receipt.status
                    && unavailable.stateVersion == receipt.stateVersion
                    && !unavailable.hasSnapshot);
            }
            quota->setLimitBytes(0);
            auto replay = dispatcher.dispatch(command, executor);
            checkPayload(replay);
            assert(executor.calls == 1 && replay.commandId == 71 && replay.correlationId == 18
                && replay.stateVersion == receipt.stateVersion
                && replay.diagnostic
                    == (success ? EditorDiagnosticCode::None
                                : EditorDiagnosticCode::WorkspaceBusy));
            replay.snapshot.scriptText.clear();
            checkPayload(dispatcher.dispatch(command, executor));
            // Refuse at different dynamic-copy boundaries. Partial copies must
            // be released without altering the retained result or reexecuting.
            for (const auto budget : {1u, 512u, 1024u, 2048u, 8192u}) {
                const auto copyQuota = core::makeShared<core::CpuMemoryQuota>(budget);
                {
                    core::CpuMemoryQuotaScope scope(copyQuota);
                    const auto copied = dispatcher.dispatch(command, executor);
                    assert(copied.status == receipt.status
                        && copied.stateVersion == receipt.stateVersion);
                    if (copied.diagnostic == EditorDiagnosticCode::ResponsePayloadUnavailable)
                        assert(!copied.hasSnapshot && !copied.hasSceneInspector
                            && !copied.hasSceneInspectorSelection && !copied.hasSceneHierarchy
                            && !copied.hasSceneComponentCatalog);
                    else
                        checkPayload(copied);
                }
                assert(!copyQuota->liveBytes() && executor.calls == 1);
            }
            command.idempotencyFingerprint = "conflict";
            assert(dispatcher.dispatch(command, executor).diagnostic
                == EditorDiagnosticCode::IdempotencyKeyConflict);
            assert(executor.calls == 1);
        }
    }
    assert(!quota->liveBytes());
    std::puts(retained
            ? "PASS retained full result survives receipt OOM, replay OOM and caller mutation"
            : "PASS unkeyed dynamic response moves without dispatcher post-execute allocation");
}

void customResource()
{
    core::CpuMemoryResource resource(
        core::defaultCpuAllocator(), core::CpuAllocationIntent::General, "receipt-custom-domain");
    EditorCommandOutcome custom{true,
        true,
        EditorDiagnosticCode::None,
        false,
        {},
        false,
        {},
        true,
        {12,
            core::pmr::String(256, 'a', &resource),
            core::pmr::Vector<SceneInspectorProperty>(&resource)}};
    ReceiptExecutor executor(std::move(custom));
    executor.quota = core::makeShared<core::CpuMemoryQuota>();
    executor.rejectAfterExecute = true;
    EditorCommandDispatcher dispatcher;
    const auto command = request();
    {
        core::CpuMemoryQuotaScope scope(executor.quota);
        const auto response = dispatcher.dispatch(command, executor);
        assert(response.status == EditorResponseStatus::Completed && response.hasSceneInspector
            && response.sceneInspector.objectName.size() == 256
            && response.sceneInspector.objectName.get_allocator().resource() == &resource);
    }
    assert(!executor.quota->liveBytes());
    std::puts("PASS response move construction preserves a distinct domain PMR resource under OOM");
}

void nestedProjection()
{
    for (bool success : {true, false}) {
        EditorCommandDispatcher dispatcher;
        ReceiptExecutor child;
        child.prepare(true);
        child.outcome.success = success;
        child.outcome.hasSceneTransformSettings = true;
        child.outcome.sceneTransformSettings.tool = EditorSceneTransformTool::Scale;
        child.outcome.diagnostic =
            success ? EditorDiagnosticCode::None : EditorDiagnosticCode::WorkspaceBusy;
        auto response = dispatcher.dispatch(request(), child);
        const auto version = dispatcher.stateVersion();
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope scope(quota);
            auto outcome = std::move(response).intoNestedOutcome();
            assert(outcome.success == success && !outcome.stateChanged);
            ReceiptExecutor parent(std::move(outcome));
            const auto projected = dispatcher.dispatch(request(), parent);
            checkPayload(projected);
            assert(projected.status
                    == (success ? EditorResponseStatus::Completed : EditorResponseStatus::Failed)
                && projected.diagnostic
                    == (success ? EditorDiagnosticCode::None : EditorDiagnosticCode::WorkspaceBusy)
                && projected.stateVersion == version && dispatcher.stateVersion() == version
                && projected.hasSceneTransformSettings
                && projected.sceneTransformSettings.tool == EditorSceneTransformTool::Scale);
        }
        assert(!quota->liveBytes());
    }
    std::puts(
        "PASS nested full payload projection under OOM preserves result without another version");
}

class NestedExecutor final : public IEditorCommandExecutor {
public:
    NestedExecutor(EditorCommandDispatcher& owner, core::SharedPtr<core::CpuMemoryQuota> quota)
        : owner_(owner), quota_(std::move(quota))
    {}

    EditorCommandOutcome execute(const EditorCommandPayload&) override
    {
        const auto nested = owner_.dispatch(request("inner"), child);
        assert(nested.status == EditorResponseStatus::Completed);
        quota_->setLimitBytes(1);
        ++calls;
        return {};
    }

    ReceiptExecutor child;
    unsigned calls = 0;

private:
    EditorCommandDispatcher& owner_;
    core::SharedPtr<core::CpuMemoryQuota> quota_;
};

void nestedPublication()
{
    auto quota = core::makeShared<core::CpuMemoryQuota>();
    {
        EditorCommandDispatcher dispatcher(2);
        ReceiptExecutor seed;
        const auto seeded = request("seed");
        assert(dispatcher.dispatch(seeded, seed).status == EditorResponseStatus::Completed);
        const auto outer = request("outer");
        NestedExecutor executor(dispatcher, quota);
        {
            core::CpuMemoryQuotaScope scope(quota);
            const auto response = dispatcher.dispatch(outer, executor);
            assert(
                response.status == EditorResponseStatus::Completed && response.stateVersion == 3);
        }
        assert(dispatcher.dispatch(outer, executor).stateVersion == 3 && executor.calls == 1);
        assert(dispatcher.dispatch(request("inner"), seed).stateVersion == 2 && seed.calls == 1);
        quota->setLimitBytes(0);
        assert(dispatcher.dispatch(seeded, seed).stateVersion == 4 && seed.calls == 2);
    }
    assert(!quota->liveBytes());
    std::puts(
        "PASS nested different-key publication uses prepared bound and preserves FIFO semantics");
}

void lifecycle()
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        preparationFailure();
        postExecution(false);
        postExecution(true);
        postExecution(true, false);
        customResource();
        nestedProjection();
        nestedPublication();
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary dispatcher prepare/receipt failure/replay/destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifecycle();
    else {
        preparationFailure();
        postExecution(false);
        postExecution(true);
        postExecution(true, false);
        customResource();
        nestedProjection();
        nestedPublication();
    }
}
