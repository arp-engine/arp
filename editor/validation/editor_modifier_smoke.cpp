#include "editor/validation/editor_modifier_smoke.h"

#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_key_input.h"

#include <cstdio>

namespace arp::editor {
bool EditorModifierSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-MODIFIER-SMOKE-OPEN";
    tests::RecoveryFixture files;
    const auto path = files.root / "modifier_workflow.arp";
    constexpr std::string_view original =
        "# Both modifier sides use native normalization.\n\n\nclass ModifierProbe:\n    pass\n";
    tests::RecoveryFixture::write(path, original);
    if (!shell.openScriptFile(path))
        return false;
    diagnostic = "EDITOR-MODIFIER-SMOKE-CONTROL";
    using Key = platform::WindowKey;
    EditorKeyInput::send(shell, Key::LeftControl);
    EditorKeyInput::send(shell, Key::RightControl);
    EditorKeyInput::send(shell, Key::LeftControl, false);
    const auto heldVersion = shell.stateVersion();
    platform::WindowInputEvent text;
    text.kind = platform::WindowInputEvent::Kind::TextInput;
    text.text = "unexpected";
    shell.handlePlatformInput(text);
    if (shell.scriptText() != original || shell.stateVersion() != heldVersion)
        return false;
    diagnostic = "EDITOR-MODIFIER-SMOKE-SELECTION";
    if (scenario == "modifier_control") {
        EditorKeyInput::send(shell, Key::A);
        if (shell.scriptSelectionStart() != 0 || shell.scriptSelectionEnd() != original.size()
            || shell.stateVersion() != heldVersion + 1 || shell.scriptDirty())
            return false;
        EditorKeyInput::send(shell, Key::RightControl, false);
    } else {
        EditorKeyInput::send(shell, Key::RightControl, false);
        EditorKeyInput::send(shell, Key::LeftShift);
        EditorKeyInput::send(shell, Key::RightShift);
        EditorKeyInput::send(shell, Key::RightShift, false);
        const auto selecting = shell.stateVersion();
        EditorKeyInput::send(shell, Key::End);
        if (shell.scriptSelectionStart() != 0 || shell.scriptSelectionEnd() != original.find('\n')
            || shell.stateVersion() != selecting + 1)
            return false;
        if (scenario == "modifier_released") {
            diagnostic = "EDITOR-MODIFIER-SMOKE-FOCUS-RESET";
            // FocusLost is an explicit reset even under denied allocation.
            const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            {
                core::CpuMemoryQuotaScope scope(quota);
                EditorKeyInput::focusLost(shell);
            }
            EditorKeyInput::send(shell, Key::Home);
            if (shell.scriptSelectionStart() != 0 || shell.scriptSelectionEnd() != 0)
                return false;
            text.text = "# Text resumes after focus reset.\n";
            const auto editing = shell.stateVersion();
            shell.handlePlatformInput(text);
            if (shell.scriptText() != text.text + core::pmr::String(original)
                || shell.stateVersion() != editing + 1 || !shell.scriptDirty())
                return false;
        } else if (scenario != "modifier_shift")
            return false;
    }
    diagnostic = "EDITOR-MODIFIER-SMOKE-DISK";
    if (core::readTextFile(path).text != original)
        return false;
    diagnostic.clear();
    std::printf(
        "PASS controlled native modifier scenario=%.*s: remaining side/selection/reset/version/disk verified\n",
        static_cast<int>(scenario.size()),
        scenario.data());
    return true;
}
} // namespace arp::editor
