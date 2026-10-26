#include "editor/validation/editor_script_input_smoke.h"

#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_key_input.h"

#include <cstdio>

namespace arp::editor {
bool EditorScriptInputSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-SCRIPT-INPUT-SMOKE-OPEN";
    tests::RecoveryFixture files;
    const auto path = files.root / "input_workflow.arp";
    constexpr std::string_view original =
        "# Native input fixture (not executed).\n\n\nclass InputProbe:\n    pass\n";
    tests::RecoveryFixture::write(path, original);
    if (!shell.openScriptFile(path))
        return false;
    using Key = platform::WindowKey;
    EditorKeyInput::send(shell, Key::LeftControl);
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        EditorKeyInput::focusLost(shell);
    }
    platform::WindowInputEvent typed;
    typed.kind = platform::WindowInputEvent::Kind::TextInput;
    typed.text = "# Typed through native input\n";
    const auto before = shell.stateVersion();
    shell.handlePlatformInput(typed);
    const core::pmr::String expected = typed.text + core::pmr::String(original);
    diagnostic = "EDITOR-SCRIPT-INPUT-SMOKE-TEXT";
    if (shell.scriptText() != expected || shell.stateVersion() != before + 1
        || !shell.scriptDirty())
        return false;
    typed.text = "x";
    const auto failedVersion = shell.stateVersion();
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(512);
        core::CpuMemoryQuotaScope scope(quota);
        shell.handlePlatformInput(typed);
    }
    diagnostic = "EDITOR-SCRIPT-INPUT-SMOKE-FAILED-TEXT";
    if (shell.scriptText() != expected || shell.stateVersion() != failedVersion)
        return false;
    if (scenario == "script_input_selected") {
        EditorKeyInput::send(shell, Key::LeftControl);
        const auto selecting = shell.stateVersion();
        EditorKeyInput::send(shell, Key::A);
        EditorKeyInput::send(shell, Key::A);
        diagnostic = "EDITOR-SCRIPT-INPUT-SMOKE-SELECTION";
        if (shell.stateVersion() != selecting + 1 || shell.scriptSelectionStart() != 0
            || shell.scriptSelectionEnd() != expected.size())
            return false;
        EditorKeyInput::send(shell, Key::LeftControl, false);
    } else if (scenario == "script_input_saved") {
        EditorKeyInput::send(shell, Key::LeftControl);
        const auto saving = shell.stateVersion();
        EditorKeyInput::send(shell, Key::S);
        diagnostic = "EDITOR-SCRIPT-INPUT-SMOKE-SAVE";
        if (shell.stateVersion() != saving + 1 || shell.scriptDirty()
            || core::readTextFile(path).text != expected)
            return false;
        EditorKeyInput::send(shell, Key::LeftControl, false);
    } else if (scenario != "script_input_edited")
        return false;
    diagnostic.clear();
    std::printf(
        "PASS controlled native script input scenario=%.*s: text/failure/focus/selection/save state and versions verified\n",
        static_cast<int>(scenario.size()),
        scenario.data());
    return true;
}
} // namespace arp::editor
