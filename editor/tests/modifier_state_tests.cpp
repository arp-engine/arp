#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_modifier_smoke.h"
#include "editor/workspace/editor_shell.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;

EditorResponse key(EditorShell& shell, EditorInputKey value, bool down = true)
{
    EditorInputCommand input;
    input.kind = down ? EditorInputKind::KeyDown : EditorInputKind::KeyUp;
    input.key = value;
    return shell.submitCommand(input);
}

bool probe(const tests::RecoveryFixture& files)
{
    bool passed = true;
    for (bool control : {true, false}) {
        EditorShell shell;
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        assert(shell.openScriptFile(files.root / "modifiers.arp"));
        const auto left = control ? EditorInputKey::LeftControl : EditorInputKey::LeftShift;
        const auto right = control ? EditorInputKey::RightControl : EditorInputKey::RightShift;
        key(shell, left);
        key(shell, right);
        key(shell, left, false);
        const auto before = shell.stateVersion();
        if (control) {
            EditorInputCommand input;
            input.kind = EditorInputKind::Text;
            input.text = "x";
            shell.submitCommand(input);
            const bool ok = shell.scriptText() == "abc\n" && shell.stateVersion() == before;
            std::printf("control remaining right: textSuppressed=%u versionDelta=%llu\n",
                ok,
                static_cast<unsigned long long>(shell.stateVersion() - before));
            passed &= ok;
        } else {
            key(shell, EditorInputKey::ArrowRight);
            const bool ok = shell.scriptSelectionStart() == 0 && shell.scriptSelectionEnd() == 1;
            std::printf("shift remaining right: selection=%zu..%zu accepted=%u\n",
                shell.scriptSelectionStart(),
                shell.scriptSelectionEnd(),
                ok);
            passed &= ok;
        }
    }
    return passed;
}

void load(EditorShell& shell, const tests::RecoveryFixture& files)
{
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    assert(shell.openScriptFile(files.root / "modifiers.arp"));
}

void text(EditorShell& shell, std::string_view value)
{
    EditorInputCommand input;
    input.kind = EditorInputKind::Text;
    input.text = value;
    assert(shell.submitCommand(input).status == EditorResponseStatus::Completed);
}

void sideSequences(const tests::RecoveryFixture& files)
{
    for (bool control : {true, false}) {
        for (bool releaseLeft : {true, false}) {
            EditorShell shell;
            load(shell, files);
            const auto first = control ? EditorInputKey::LeftControl : EditorInputKey::LeftShift;
            const auto second = control ? EditorInputKey::RightControl : EditorInputKey::RightShift;
            const auto released = releaseLeft ? first : second;
            const auto held = releaseLeft ? second : first;
            const auto version = shell.stateVersion();
            // No-op releases, repeated presses and each distinct physical side
            // have exact receipts even when the aggregate OR does not change.
            key(shell, first, false);
            assert(shell.stateVersion() == version);
            key(shell, first);
            key(shell, first);
            assert(shell.stateVersion() == version + 1);
            key(shell, second);
            key(shell, second);
            assert(shell.stateVersion() == version + 2);
            key(shell, released, false);
            key(shell, released, false);
            assert(shell.stateVersion() == version + 3);
            if (control) {
                text(shell, "rejected while held");
                assert(shell.scriptText() == "abc\n" && shell.stateVersion() == version + 3);
                key(shell, EditorInputKey::A);
                assert(shell.scriptSelectionStart() == 0 && shell.scriptSelectionEnd() == 4);
            } else {
                key(shell, EditorInputKey::ArrowRight);
                key(shell, EditorInputKey::ArrowRight);
                assert(shell.scriptSelectionStart() == 0 && shell.scriptSelectionEnd() == 2);
            }
            const auto beforeRelease = shell.stateVersion();
            {
                const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
                core::CpuMemoryQuotaScope scope(quota);
                key(shell, held, false);
                key(shell, held, false);
            }
            assert(shell.stateVersion() == beforeRelease + 1);
            if (control) {
                text(shell, "x");
                assert(shell.scriptText() == "x");
            } else {
                key(shell, EditorInputKey::ArrowRight);
                assert(shell.scriptSelectionStart() == 2 && shell.scriptSelectionEnd() == 2);
            }
        }
    }
    std::puts(
        "PASS both release orders, same-side repeat, orphan release, exact version and quota1 cleanup");
}

void lifecycleBarriers(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    load(shell, files);
    key(shell, EditorInputKey::LeftControl);
    key(shell, EditorInputKey::RightControl);
    key(shell, EditorInputKey::LeftShift);
    key(shell, EditorInputKey::RightShift);
    {
        EditorInputCommand input;
        input.kind = EditorInputKind::FocusLost;
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(input).status == EditorResponseStatus::Completed);
    }
    key(shell, EditorInputKey::ArrowRight);
    assert(shell.scriptSelectionStart() == 1 && shell.scriptSelectionEnd() == 1);
    text(shell, "x");
    assert(shell.scriptText() == "axbc\n");

    key(shell, EditorInputKey::LeftControl);
    key(shell, EditorInputKey::RightControl);
    shell.activate("workspace_Game");
    const auto releaseVersion = shell.stateVersion();
    key(shell, EditorInputKey::LeftControl, false);
    key(shell, EditorInputKey::LeftControl, false);
    assert(shell.stateVersion() == releaseVersion + 1);
    shell.activate("workspace_Script");
    const auto unchanged = shell.stateVersion();
    text(shell, "blocked");
    assert(shell.stateVersion() == unchanged && shell.scriptText() == "axbc\n");
    key(shell, EditorInputKey::RightControl, false);
    text(shell, "y");
    assert(shell.scriptText() == "axybc\n");

    // Opening the panel explicitly resets old input. Newly held physical sides
    // still receive KeyUp while authoring is blocked; shortcuts remain blocked.
    assert(shell.submitCommand(ActivateControlCommand("script_ui_close")).status
        == EditorResponseStatus::Completed);
    assert(shell.runtime().canvas().findWidget(shell.id("script_ui_modal"))->visible());
    key(shell, EditorInputKey::LeftControl);
    key(shell, EditorInputKey::RightControl);
    key(shell, EditorInputKey::LeftControl, false);
    const auto blocked = shell.stateVersion();
    key(shell, EditorInputKey::A);
    text(shell, "blocked");
    assert(shell.stateVersion() == blocked
        && shell.scriptSelectionStart() == shell.scriptSelectionEnd());
    assert(shell.submitCommand(ActivateControlCommand("script_ui_cancel")).status
        == EditorResponseStatus::Completed);
    text(shell, "blocked");
    assert(shell.scriptText() == "axybc\n");
    key(shell, EditorInputKey::RightControl, false);
    text(shell, "z");
    assert(shell.scriptText() == "axyzbc\n");
    // A release entirely inside the next modal cannot stick after cancellation.
    assert(shell.submitCommand(ActivateControlCommand("script_ui_close")).status
        == EditorResponseStatus::Completed);
    key(shell, EditorInputKey::LeftControl);
    key(shell, EditorInputKey::LeftControl, false);
    assert(shell.submitCommand(ActivateControlCommand("script_ui_cancel")).status
        == EditorResponseStatus::Completed);
    text(shell, "q");
    assert(shell.scriptText() == "axyzqbc\n");
    assert(core::readTextFile(files.root / "modifiers.arp").text == "abc\n");
    std::puts(
        "PASS FocusLost/workspace/modal reset and release do not leak shortcuts or alter disk");
}

void saveUndoRedo(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    load(shell, files);
    text(shell, "x");
    key(shell, EditorInputKey::LeftControl);
    key(shell, EditorInputKey::RightControl);
    key(shell, EditorInputKey::RightControl, false);
    auto version = shell.stateVersion();
    key(shell, EditorInputKey::Z);
    assert(shell.scriptText() == "abc\n" && shell.stateVersion() == version + 1);
    key(shell, EditorInputKey::LeftShift);
    key(shell, EditorInputKey::RightShift);
    key(shell, EditorInputKey::LeftShift, false);
    version = shell.stateVersion();
    key(shell, EditorInputKey::Z);
    assert(shell.scriptText() == "xabc\n" && shell.stateVersion() == version + 1);
    version = shell.stateVersion();
    key(shell, EditorInputKey::S);
    assert(!shell.scriptDirty() && shell.stateVersion() == version + 1);
    assert(core::readTextFile(files.root / "modifiers.arp").text == "xabc\n");
    std::puts(
        "PASS remaining-side Ctrl+Z/Ctrl+Shift+Z/Ctrl+S preserve nested single-version receipts");
}

void nativeRootAdmission(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    core::pmr::String diagnostic;
    assert(!EditorModifierSmoke::run(shell, "modifier_control", diagnostic));
    assert(diagnostic == "EDITOR-MODIFIER-SMOKE-OPEN" && shell.openScriptCount() == 0);
    std::puts(
        "PASS declared fixture root mismatch rejects at open; res:// admission remains intact");
}

void nativePreparation(std::string_view scenario)
{
    EditorShell shell;
    assert(shell
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                std::filesystem::path(ARP_SOURCE_DIR))
            .ok());
    core::pmr::String diagnostic;
    assert(EditorModifierSmoke::run(shell, scenario, diagnostic));
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    tests::RecoveryFixture::write(files.root / "modifiers.arp", "abc\n");
    if (argc == 2 && std::string_view(argv[1]) == "--probe")
        return probe(files) ? 0 : 2;
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    nativeRootAdmission(files);
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        sideSequences(files);
        lifecycleBarriers(files);
        for (const auto scenario : {"modifier_control", "modifier_shift", "modifier_released"})
            nativePreparation(scenario);
        saveUndoRedo(files);
        tests::RecoveryFixture::write(files.root / "modifiers.arp", "abc\n");
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    const bool passed = probe(files);
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary modifier/edit/selection/history/modal/workspace/destruction cycles: core live bytes/count delta=0");
    return passed ? 0 : 2;
}
