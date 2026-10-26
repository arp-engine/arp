#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_script_modal_smoke.h"
#include "editor/workspace/editor_shell.h"

#include <array>
#include <cstdio>
#include <new>

namespace {
using namespace arp;
using namespace arp::editor;

bool probe(const tests::RecoveryFixture& files)
{
    bool passed = true;
    for (const auto budget : std::array<std::size_t, 5>{1, 128, 512, 4096, 131072}) {
        tests::RecoveryFixture::write(files.root / "modal.arp", "# disk\n");
        EditorShell shell;
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        assert(shell.openScriptFile(files.root / "modal.arp"));
        const auto document = shell.activeScriptDocumentId();
        assert(shell.submitCommand(ReplaceScriptTextCommand(document, "# local\n")).status
            == EditorResponseStatus::Completed);
        EditorCommandEnvelope command(ActivateControlCommand("script_ui_reload"));
        const auto version = shell.stateVersion();
        const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
        bool threw = false;
        EditorResponse response;
        {
            core::CpuMemoryQuotaScope scope(quota);
            try {
                response = shell.submitCommand(std::move(command));
            } catch (const std::bad_alloc&) {
                threw = true;
            }
        }
        const bool visible =
            shell.runtime().canvas().findWidget(shell.id("script_ui_modal"))->visible();
        const bool background =
            shell.runtime().canvas().findWidget(shell.id("viewport"))->enabled();
        const bool keyboard = shell.scriptEditorCapturesKeyboard();
        const bool coherent = visible ? (!background && !keyboard) : (background && keyboard);
        std::printf(
            "modal budget=%zu threw=%u visible=%u background=%u keyboard=%u versionDelta=%llu\n",
            budget,
            threw,
            visible,
            background,
            keyboard,
            static_cast<unsigned long long>(shell.stateVersion() - version));
        passed &= !threw && coherent && shell.activeScriptDocumentId() == document
            && shell.scriptText() == "# local\n" && shell.scriptDirty();
    }
    return passed;
}

EditorResponse action(EditorShell& shell, std::string_view source)
{
    return shell.submitCommand(ActivateControlCommand(source));
}

bool modal(EditorShell& shell)
{
    return shell.runtime().canvas().findWidget(shell.id("script_ui_modal"))->visible();
}

void initialize(EditorShell& shell, const tests::RecoveryFixture& files)
{
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    tests::RecoveryFixture::write(files.root / "entry.arp", "# disk\n");
    assert(shell.openScriptFile(files.root / "entry.arp"));
    assert(
        shell.submitCommand(ReplaceScriptTextCommand(shell.activeScriptDocumentId(), "# local\n"))
            .status
        == EditorResponseStatus::Completed);
}

EditorCommandPayload request(EditorShell& shell, unsigned entry)
{
    if (entry == 0)
        return ActivateControlCommand("script_ui_reload");
    EditorInputCommand event;
    event.kind = EditorInputKind::KeyDown;
    event.key = EditorInputKey::LeftControl;
    if (entry == 1) {
        assert(shell.submitCommand(event).status == EditorResponseStatus::Completed);
        event.key = EditorInputKey::W;
        return event;
    }
    const auto drawing = shell.drawCommands();
    assert(drawing.ok());
    for (const auto& command : drawing.commands()) {
        if (command.widgetId != shell.id("scene_tabs")
            || command.kind != renderer::ui::UiDrawCommandKind::Text || command.text != "x")
            continue;
        event.kind = EditorInputKind::PointerDown;
        event.primaryButton = true;
        event.button = EditorPointerButton::Left;
        event.x = command.rect.x + command.rect.width * 0.5f;
        event.y = command.rect.y + command.rect.height * 0.5f;
        return event;
    }
    assert(false && "Missing actual tab close glyph");
    return event;
}

void entryCases(const tests::RecoveryFixture& files, bool full)
{
    for (unsigned entry = 0; entry != 3; ++entry) {
        for (const auto budget : std::array<std::size_t, 6>{1, 128, 512, 4096, 30000, 131072}) {
            if (!full && budget != 1 && budget != 131072)
                continue;
            EditorShell shell;
            initialize(shell, files);
            const auto id = shell.activeScriptDocumentId();
            auto payload = request(shell, entry);
            EditorCommandEnvelope command(std::move(payload));
            const auto version = shell.stateVersion();
            const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
            EditorResponse response;
            {
                core::CpuMemoryQuotaScope scope(quota);
                response = shell.submitCommand(std::move(command));
            }
            assert(shell.scriptText() == "# local\n" && shell.scriptDirty()
                && shell.activeScriptDocumentId() == id);
            if (!modal(shell)) {
                assert(response.status == EditorResponseStatus::Failed
                    && response.diagnostic == EditorDiagnosticCode::CommandPreparationFailed
                    && shell.stateVersion() == version && shell.scriptEditorCapturesKeyboard());
            } else {
                assert(response.status == EditorResponseStatus::Completed
                    && shell.stateVersion() == version + 1
                    && !shell.scriptEditorCapturesKeyboard());
                EditorCommandEnvelope cancel(ActivateControlCommand("script_ui_cancel"));
                const auto cancelling = shell.stateVersion();
                {
                    const auto minimal = core::makeShared<core::CpuMemoryQuota>(1);
                    core::CpuMemoryQuotaScope scope(minimal);
                    assert(shell.submitCommand(std::move(cancel)).status
                        == EditorResponseStatus::Completed);
                }
                assert(!modal(shell) && shell.stateVersion() == cancelling + 1);
            }
            // Retry the exact same intent after restoring memory, then cancel.
            payload = request(shell, entry);
            const auto retrying = shell.stateVersion();
            assert(
                shell.submitCommand(std::move(payload)).status == EditorResponseStatus::Completed);
            assert(modal(shell) && shell.stateVersion() == retrying + 1);
            assert(action(shell, "script_ui_cancel").status == EditorResponseStatus::Completed);
            assert(!modal(shell) && shell.scriptText() == "# local\n");
            assert(shell.submitCommand(UndoScriptCommand{id}).status
                == EditorResponseStatus::Completed);
            assert(shell.scriptText() == "# disk\n" && !shell.scriptDirty());
            assert(shell.submitCommand(RedoScriptCommand{id}).status
                == EditorResponseStatus::Completed);
            assert(core::readTextFile(files.root / "entry.arp").text == "# disk\n");
        }
    }
    std::puts(
        "PASS reload/Ctrl+W/rendered-tab quota rejection, exact version, no half-open modal, cancel under OOM and retry/history/disk");
}

void confirmationCases(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    initialize(shell, files);
    const auto document = shell.activeScriptDocumentId();
    EditorCommandEnvelope once(ActivateControlCommand("script_ui_reload"));
    once.idempotencyKey = "modal-open-once";
    once.idempotencyFingerprint = "activate:script_ui_reload";
    const auto opened = shell.submitCommand(once);
    assert(opened.status == EditorResponseStatus::Completed);
    const auto repeated = shell.stateVersion();
    assert(action(shell, "script_ui_reload").status == EditorResponseStatus::Completed
        && shell.stateVersion() == repeated);
    assert(action(shell, "script_ui_cancel").status == EditorResponseStatus::Completed);
    const auto cancelled = shell.stateVersion();
    const auto replay = shell.submitCommand(once);
    assert(replay.status == EditorResponseStatus::Completed
        && replay.stateVersion == opened.stateVersion && shell.stateVersion() == cancelled
        && !modal(shell));
    assert(action(shell, "script_ui_reload").status == EditorResponseStatus::Completed);
    assert(
        shell.submitCommand(ReplaceScriptTextCommand(document, "# changed behind modal\n")).status
        == EditorResponseStatus::Completed);
    const auto staleVersion = shell.stateVersion();
    assert(
        action(shell, "script_ui_confirm").diagnostic == EditorDiagnosticCode::StaleDocumentTarget);
    assert(modal(shell) && shell.stateVersion() == staleVersion
        && shell.runtime().canvas().findWidget(shell.id("script_ui_stale"))->visible());
    assert(action(shell, "script_ui_cancel").status == EditorResponseStatus::Completed);
    assert(action(shell, "script_ui_reload").status == EditorResponseStatus::Completed);
    EditorCommandEnvelope failing(ActivateControlCommand("script_ui_confirm"));
    const auto beforeFailure = shell.stateVersion();
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        const auto failed = shell.submitCommand(std::move(failing));
        assert(failed.status == EditorResponseStatus::Failed
            && failed.diagnostic == EditorDiagnosticCode::CommandPreparationFailed);
    }
    assert(modal(shell) && shell.stateVersion() == beforeFailure
        && shell.scriptText() == "# changed behind modal\n"
        && shell.runtime().canvas().findWidget(shell.id("script_ui_failed"))->visible());
    assert(action(shell, "script_ui_confirm").status == EditorResponseStatus::Failed);
    assert(action(shell, "script_ui_cancel").status == EditorResponseStatus::Completed);
    tests::RecoveryFixture::write(files.root / "entry.arp", "# new disk\r\n");
    assert(action(shell, "script_ui_reload").status == EditorResponseStatus::Completed);
    const auto reloading = shell.stateVersion();
    assert(action(shell, "script_ui_confirm").status == EditorResponseStatus::Completed);
    assert(!modal(shell) && shell.stateVersion() == reloading + 1
        && shell.scriptText() == "# new disk\n" && !shell.scriptDirty());
    // Clean close commits through the nested command exactly once as well.
    const auto closing = shell.stateVersion();
    assert(action(shell, "script_ui_close").status == EditorResponseStatus::Completed);
    assert(shell.stateVersion() == closing + 1 && shell.openScriptCount() == 0);
    assert(action(shell, "script_ui_confirm").status == EditorResponseStatus::Failed);
    std::puts(
        "PASS stale/failed confirmation is isolated; explicit cancel/reopen restores real reload/clean close, one version per commit");
}

void nativePreparation()
{
    for (const auto scenario :
        {"script_modal_open", "script_modal_failed", "script_modal_retry", "script_modal_closed"}) {
        EditorShell shell;
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    std::filesystem::path(ARP_SOURCE_DIR))
                .ok());
        core::pmr::String diagnostic;
        assert(EditorScriptModalSmoke::run(shell, scenario, diagnostic));
    }
}

void confirmationQuotaCases(const tests::RecoveryFixture& files)
{
    for (const auto source : {"script_ui_close", "script_ui_reload"}) {
        for (const auto budget : std::array<std::size_t, 4>{1, 512, 30000, 131072}) {
            EditorShell shell;
            initialize(shell, files);
            assert(action(shell, source).status == EditorResponseStatus::Completed);
            EditorCommandEnvelope confirm(ActivateControlCommand("script_ui_confirm"));
            const auto version = shell.stateVersion();
            EditorResponse response;
            {
                const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
                core::CpuMemoryQuotaScope scope(quota);
                response = shell.submitCommand(std::move(confirm));
            }
            if (response.status == EditorResponseStatus::Completed) {
                assert(!modal(shell) && shell.stateVersion() == version + 1);
                if (std::string_view(source) == "script_ui_close")
                    assert(shell.openScriptCount() == 0);
                else
                    assert(shell.scriptText() == "# disk\n" && !shell.scriptDirty());
            } else {
                assert(modal(shell) && shell.stateVersion() == version
                    && shell.scriptText() == "# local\n" && shell.scriptDirty());
                assert(action(shell, "script_ui_cancel").status == EditorResponseStatus::Completed);
            }
            assert(core::readTextFile(files.root / "entry.arp").text == "# disk\n");
        }
    }
    std::puts(
        "PASS confirmation quotas distinguish rejection from actual nested commit and retire modal without another allocation");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--probe")
        return probe(files) ? 0 : 2;
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        entryCases(files, cycle == 0);
        confirmationCases(files);
        if (cycle == 0) {
            confirmationQuotaCases(files);
            nativePreparation();
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (cycle == 0)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary modal failure/cancel/retry/history/destruction cycles: core live bytes/count delta=0");
    return 0;
}
