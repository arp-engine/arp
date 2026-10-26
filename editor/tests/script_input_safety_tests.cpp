#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_script_input_smoke.h"
#include "editor/workspace/editor_shell.h"

#include <array>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::editor;

bool probe(const tests::RecoveryFixture& files)
{
    tests::RecoveryFixture::write(files.root / "input.arp", "# input\n");
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    assert(shell.openScriptFile(files.root / "input.arp"));
    EditorInputCommand ignored;
    ignored.kind = EditorInputKind::KeyDown;
    const auto ignoredVersion = shell.stateVersion();
    const auto ignoredResult = shell.submitCommand(ignored);
    const bool ignoredOk = ignoredResult.status == EditorResponseStatus::Completed
        && shell.stateVersion() == ignoredVersion;
    std::printf("ignored key versionDelta=%llu\n",
        static_cast<unsigned long long>(shell.stateVersion() - ignoredVersion));

    assert(shell
               .submitCommand(
                   ReplaceScriptTextCommand(shell.activeScriptDocumentId(), "# saved input\n"))
               .status
        == EditorResponseStatus::Completed);
    EditorInputCommand control;
    control.kind = EditorInputKind::KeyDown;
    control.key = EditorInputKey::LeftControl;
    shell.submitCommand(control);
    EditorInputCommand save;
    save.kind = EditorInputKind::KeyDown;
    save.key = EditorInputKey::S;
    const auto saveVersion = shell.stateVersion();
    const auto saved = shell.submitCommand(save);
    const bool saveOk = saved.status == EditorResponseStatus::Completed
        && shell.stateVersion() == saveVersion + 1 && !shell.scriptDirty();
    std::printf("shortcut save versionDelta=%llu\n",
        static_cast<unsigned long long>(shell.stateVersion() - saveVersion));
    control.kind = EditorInputKind::KeyUp;
    shell.submitCommand(control);
    EditorInputCommand text;
    text.kind = EditorInputKind::Text;
    text.text = "x";
    const auto beforeText = shell.scriptText();
    const auto textVersion = shell.stateVersion();
    const auto quota = core::makeShared<core::CpuMemoryQuota>(512);
    bool threw = false;
    EditorResponse typed;
    {
        core::CpuMemoryQuotaScope scope(quota);
        try {
            typed = shell.submitCommand(text);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
    }
    std::printf("text OOM threw=%u changed=%u versionDelta=%llu\n",
        threw,
        shell.scriptText() != beforeText,
        static_cast<unsigned long long>(shell.stateVersion() - textVersion));
    return ignoredOk && saveOk && !threw && typed.status == EditorResponseStatus::Failed
        && shell.scriptText() == beforeText && shell.stateVersion() == textVersion;
}

EditorResponse key(EditorShell& shell, EditorInputKey value, bool down = true)
{
    EditorInputCommand input;
    input.kind = down ? EditorInputKind::KeyDown : EditorInputKind::KeyUp;
    input.key = value;
    return shell.submitCommand(input);
}

void noOpsAndHistory(const tests::RecoveryFixture& files)
{
    tests::RecoveryFixture::write(files.root / "keys.arp", "abc\n");
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    assert(shell.openScriptFile(files.root / "keys.arp"));
    const auto version = shell.stateVersion();
    for (const auto value : {EditorInputKey::Other,
             EditorInputKey::ArrowLeft,
             EditorInputKey::Home,
             EditorInputKey::Backspace}) {
        assert(key(shell, value).status == EditorResponseStatus::Completed);
        assert(shell.stateVersion() == version && !shell.scriptDirty());
    }
    EditorInputCommand empty;
    empty.kind = EditorInputKind::Text;
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(empty).status == EditorResponseStatus::Completed);
    }
    assert(shell.stateVersion() == version);
    assert(key(shell, EditorInputKey::LeftControl).status == EditorResponseStatus::Completed);
    assert(shell.stateVersion() == version + 1);
    key(shell, EditorInputKey::LeftControl);
    assert(shell.stateVersion() == version + 1);
    key(shell, EditorInputKey::Z);
    assert(shell.stateVersion() == version + 1);
    key(shell, EditorInputKey::LeftControl, false);
    assert(shell.stateVersion() == version + 2);
    key(shell, EditorInputKey::LeftControl, false);
    assert(shell.stateVersion() == version + 2);
    EditorInputCommand text;
    text.kind = EditorInputKind::Text;
    text.text = "\xe7\x8c\xab";
    auto before = shell.stateVersion();
    assert(shell.submitCommand(text).status == EditorResponseStatus::Completed);
    assert(shell.scriptText()
            == "\xe7\x8c\xab"
               "abc\n"
        && shell.stateVersion() == before + 1);
    before = shell.stateVersion();
    key(shell, EditorInputKey::Backspace);
    assert(shell.scriptText() == "abc\n" && shell.stateVersion() == before + 1);
    key(shell, EditorInputKey::LeftControl);
    before = shell.stateVersion();
    key(shell, EditorInputKey::Z);
    assert(shell.scriptText()
            == "\xe7\x8c\xab"
               "abc\n"
        && shell.stateVersion() == before + 1);
    before = shell.stateVersion();
    key(shell, EditorInputKey::Y);
    assert(shell.scriptText() == "abc\n" && shell.stateVersion() == before + 1);
    key(shell, EditorInputKey::A);
    before = shell.stateVersion();
    key(shell, EditorInputKey::A);
    assert(shell.scriptSelectionStart() == 0 && shell.scriptSelectionEnd() == 4
        && shell.stateVersion() == before);
    key(shell, EditorInputKey::LeftControl, false);
    std::puts(
        "PASS no-op/empty/modifier/boundary inputs and UTF-8 history have exact version effects");
}

void quotaAndFailure(const tests::RecoveryFixture& files, std::size_t budget)
{
    tests::RecoveryFixture::write(files.root / "quota.arp", "baseline\n");
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    assert(shell.openScriptFile(files.root / "quota.arp"));
    EditorInputCommand text;
    text.kind = EditorInputKind::Text;
    text.text = "edit";
    const auto version = shell.stateVersion();
    EditorResponse result;
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
        core::CpuMemoryQuotaScope scope(quota);
        result = shell.submitCommand(text);
    }
    if (result.status == EditorResponseStatus::Completed) {
        assert(shell.scriptText() == "editbaseline\n" && shell.stateVersion() == version + 1);
        assert(shell.submitCommand(UndoScriptCommand{shell.activeScriptDocumentId()}).status
            == EditorResponseStatus::Completed);
    } else {
        assert(result.diagnostic == EditorDiagnosticCode::CommandPreparationFailed
            && shell.stateVersion() == version);
        assert(shell.scriptText() == "baseline\n" && shell.scriptSelectionStart() == 0
            && shell.scriptSelectionEnd() == 0 && !shell.scriptDirty());
        assert(shell.submitCommand(text).status == EditorResponseStatus::Completed);
        assert(shell.submitCommand(UndoScriptCommand{shell.activeScriptDocumentId()}).status
            == EditorResponseStatus::Completed);
    }
    assert(shell.scriptText() == "baseline\n");
    // A failed nested save must remain a failed input response and must preserve
    // the dirty document; the external writer owns the new disk bytes.
    shell.submitCommand(text);
    tests::RecoveryFixture::write(files.root / "quota.arp", "external\n");
    key(shell, EditorInputKey::LeftControl);
    const auto failedVersion = shell.stateVersion();
    const auto failed = key(shell, EditorInputKey::S);
    assert(failed.status == EditorResponseStatus::Failed
        && failed.diagnostic == EditorDiagnosticCode::ScriptExternalConflict
        && shell.stateVersion() == failedVersion && shell.scriptDirty());
    key(shell, EditorInputKey::LeftControl, false);
    assert(core::readTextFile(files.root / "quota.arp").text == "external\n");
}

void focusBarrier(const tests::RecoveryFixture& files)
{
    tests::RecoveryFixture::write(files.root / "focus.arp", "focus\n");
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    assert(shell.openScriptFile(files.root / "focus.arp"));
    key(shell, EditorInputKey::LeftControl);
    key(shell, EditorInputKey::LeftShift);
    EditorInputCommand focus;
    focus.kind = EditorInputKind::FocusLost;
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(focus).status == EditorResponseStatus::Completed);
    }
    EditorInputCommand text;
    text.kind = EditorInputKind::Text;
    text.text = "x";
    assert(shell.submitCommand(text).status == EditorResponseStatus::Completed
        && shell.scriptText() == "xfocus\n");
    key(shell, EditorInputKey::ArrowLeft);
    assert(shell.scriptSelectionStart() == 0 && shell.scriptSelectionEnd() == 0);
    std::puts(
        "PASS FocusLost clears modifiers without allocation and preserves the next text/selection input");
}

void pointerAndDocumentOwnership(const tests::RecoveryFixture& files)
{
    core::pmr::String contents;
    for (unsigned line = 0; line != 100; ++line)
        contents += "abcdefghij\n";
    tests::RecoveryFixture::write(files.root / "pointer.arp", contents);
    tests::RecoveryFixture::write(files.root / "other.arp", contents);
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    assert(shell.openScriptFile(files.root / "pointer.arp"));
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto rect = layout.findWidget(shell.id("viewport"))->rect;
    EditorInputCommand down;
    down.kind = EditorInputKind::PointerDown;
    down.button = EditorPointerButton::Left;
    down.primaryButton = true;
    down.x = rect.x + 70;
    down.y = rect.y + layout::kScriptEditorHeaderHeight + 6;
    const auto version = shell.stateVersion();
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(512);
        core::CpuMemoryQuotaScope scope(quota);
        const auto failed = shell.submitCommand(down);
        assert(failed.status == EditorResponseStatus::Failed
            && failed.diagnostic == EditorDiagnosticCode::CommandPreparationFailed);
    }
    assert(shell.stateVersion() == version && shell.scriptSelectionStart() == 0
        && shell.scriptSelectionEnd() == 0);
    assert(shell.submitCommand(down).status == EditorResponseStatus::Completed);
    const auto selectionStart = shell.scriptSelectionStart(),
               selectionEnd = shell.scriptSelectionEnd();
    EditorInputCommand move = down;
    move.kind = EditorInputKind::PointerMoved;
    move.y += 80;
    const auto moveVersion = shell.stateVersion();
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(512);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(move).status == EditorResponseStatus::Failed);
    }
    assert(shell.stateVersion() == moveVersion && shell.scriptSelectionStart() == selectionStart
        && shell.scriptSelectionEnd() == selectionEnd);
    EditorInputCommand up = down;
    up.kind = EditorInputKind::PointerUp;
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(up).status == EditorResponseStatus::Completed);
    }
    const auto releaseVersion = shell.stateVersion();
    shell.submitCommand(move);
    assert(shell.stateVersion() == releaseVersion && shell.scriptSelectionStart() == selectionStart
        && shell.scriptSelectionEnd() == selectionEnd);
    EditorInputCommand wheel = down;
    wheel.kind = EditorInputKind::Wheel;
    wheel.wheelDelta = 1;
    const auto wheelVersion = shell.stateVersion();
    shell.submitCommand(wheel);
    assert(shell.stateVersion() == wheelVersion && shell.scriptScrollLine() == 0);
    wheel.wheelDelta = std::numeric_limits<float>::max();
    assert(shell.submitCommand(wheel).status == EditorResponseStatus::Failed
        && shell.stateVersion() == wheelVersion);
    // The old document's scroll capture must never continue on a new active tab.
    down.x = rect.x + rect.width - layout::kScriptScrollbarWidth - 1;
    down.y = rect.y + 50;
    shell.submitCommand(down);
    assert(shell.openScriptFile(files.root / "other.arp"));
    const auto other = shell.activeScriptDocumentId();
    move.x = down.x;
    move.y = rect.y + rect.height - 30;
    shell.submitCommand(move);
    assert(shell.activeScriptDocumentId() == other && shell.scriptScrollLine() == 0);
    platform::WindowInputEvent tail;
    tail.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
    tail.mouseButton = platform::WindowMouseButton::Left;
    tail.x = move.x;
    tail.y = move.y;
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.handlePlatformInput(tail));
    }
    // Focus cancellation also swallows the old primary tail after a workspace
    // switch, so it cannot reach an unrelated UI control.
    shell.submitCommand(down);
    shell.activate("workspace_Game");
    assert(shell.handlePlatformInput(tail));
    std::puts(
        "PASS pointer OOM preserves selection, release is allocation-free, and capture cannot cross documents");
}

void invalidTextAndReceipt(const tests::RecoveryFixture& files)
{
    tests::RecoveryFixture::write(files.root / "validation.arp", "valid\n");
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    assert(shell.openScriptFile(files.root / "validation.arp"));
    const auto version = shell.stateVersion();
    for (const auto bytes : {std::string_view("\xc0\xaf", 2),
             std::string_view("\xed\xa0\x80", 3),
             std::string_view("\xe7\x8c", 2)}) {
        EditorInputCommand text;
        text.kind = EditorInputKind::Text;
        text.text = bytes;
        const auto result = shell.submitCommand(text);
        assert(result.status == EditorResponseStatus::Failed
            && result.diagnostic == EditorDiagnosticCode::InvalidCommandPayload
            && shell.stateVersion() == version && !shell.scriptDirty());
    }
    EditorInputCommand tooLarge;
    tooLarge.kind = EditorInputKind::Text;
    tooLarge.text.assign(ScriptDocumentSession::maxScriptBytes + 1, 'x');
    assert(shell.submitCommand(std::move(tooLarge)).status == EditorResponseStatus::Failed
        && shell.stateVersion() == version);
    EditorInputCommand text;
    text.kind = EditorInputKind::Text;
    text.text = "once";
    EditorCommandEnvelope envelope(std::move(text));
    envelope.idempotencyKey = "input-once";
    envelope.idempotencyFingerprint = "Text:once";
    const auto result = shell.submitCommand(envelope);
    assert(result.status == EditorResponseStatus::Completed && shell.scriptText() == "oncevalid\n"
        && shell.stateVersion() == version + 1);
    assert(shell.submitCommand(envelope).status == EditorResponseStatus::Completed
        && shell.stateVersion() == version + 1 && shell.scriptText() == "oncevalid\n");
    std::puts(
        "PASS invalid UTF-8/capacity rejection and retained input receipt prevent duplicate edits");
}

void nativePreparation(std::string_view scenario)
{
    EditorShell shell;
    assert(shell
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                std::filesystem::path(ARP_SOURCE_DIR))
            .ok());
    core::pmr::String diagnostic;
    assert(EditorScriptInputSmoke::run(shell, scenario, diagnostic));
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
        noOpsAndHistory(files);
        focusBarrier(files);
        pointerAndDocumentOwnership(files);
        invalidTextAndReceipt(files);
        for (const auto scenario :
            {"script_input_edited", "script_input_selected", "script_input_saved"})
            nativePreparation(scenario);
        for (const auto budget : std::array<std::size_t, 4>{1, 512, 30000, 131072})
            quotaAndFailure(files, budget);
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    const bool passed = probe(files);
    std::printf("%s script input state and receipt probe\n", passed ? "PASS" : "FAIL");
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary input/edit/history/failure/focus/destruction cycles: core live bytes/count delta=0");
    return passed ? 0 : 2;
}
