#include "editor/validation/editor_script_inspector_smoke.h"

#include "editor/tests/fixtures/script_inspector_fixture.h"

#include <cstdio>

namespace arp::editor {
bool EditorScriptInspectorSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    using namespace testfixtures;
    using F = ScriptInspectorFixture;
    using YamlContract = core::YamlContract;
    if (scenario.substr(0, 7) == "attach_") {
        if (scenario != "attach_menu" && scenario != "attach_draft" && scenario != "attach_invalid"
            && scenario != "attach_applied" && scenario != "attach_failed"
            && scenario != "attach_retry")
            return false;
        F files;
        files.open(shell);
        F::command(shell, SelectSceneObjectCommand{files.other.value()});
        if (scenario == "attach_menu") {
            F::command(shell, SetInspectorScrollCommand{0.f});
            check(F::scriptMenu(shell).valid());
        } else {
            F::beginAttachment(shell);
            if (scenario == "attach_invalid") {
                F::click(shell, "Attach script");
                check(F::label(shell, "Invalid value: amount"));
            } else {
                F::fillAttachment(shell);
                // The existing native Editor atlas explicitly supports Latin
                // shaping only. UTF-8 archive round-trip stays in data tests;
                // do not pretend this capture validates CJK glyph rendering.
                F::edit(shell, "title", "\"Initial\\nPixel survivor\"");
                if (scenario == "attach_applied") {
                    F::click(shell, "Value", "target");
                    F::choose(shell,
                        "target value",
                        core::pmr::String("Script fields [")
                            + core::formatUnsigned(files.owner.value()) + ":0]");
                    F::click(shell, "Attach script");
                    check(shell.sceneHistoryUsage().undoEntries == 1);
                    check(!F::label(shell, "New ScriptComponent - not attached"));
                }
            }
            F::command(
                shell, SetInspectorScrollCommand{scenario == "attach_applied" ? 680.f : 300.f});
            if (scenario == "attach_failed" || scenario == "attach_retry") {
                bool ready;
                {
                    core::CpuMemoryQuotaScope quota(core::makeShared<core::CpuMemoryQuota>(1));
                    ready = shell.retrySceneInspector();
                }
                check(!ready && shell.inspectorState() == EditorShell::InspectorState::Failed);
                if (scenario == "attach_retry") {
                    check(shell.retrySceneInspector());
                    check(F::label(shell, "New ScriptComponent - not attached"));
                    check(shell.runtime()
                              .findControlObject(
                                  F::control(shell, "amount", ui::UiControlType::TextInput))
                              ->value()
                              .text
                        == "23");
                }
            }
        }
        check(shell.sceneDirty() == (scenario == "attach_applied"));
        std::printf("PASS actual script attachment native scenario=%.*s\n",
            int(scenario.size()),
            scenario.data());
        diagnostic.clear();
        return true;
    }
    if (scenario != "fields_draft" && scenario != "fields_applied" && scenario != "fields_reference"
        && scenario != "fields_invalid" && scenario != "fields_failed"
        && scenario != "fields_retry") {
        diagnostic = "EDITOR-SCRIPT-INSPECTOR-SMOKE-SCENARIO";
        return false;
    }
    F files;
    files.open(shell);
    const auto before = files.state(shell);
    F::edit(shell, "amount", scenario == "fields_invalid" ? "invalid number" : "23");
    F::edit(shell, "title", "\"Edited\\nPixel survivor\"");
    if (scenario == "fields_applied") {
        F::click(shell, "Apply fields");
        check(shell.sceneHistoryUsage().undoEntries == 1 && shell.sceneDirty());
        check(YamlContract::int32(
                  YamlContract::parseSingleDocument(files.state(shell).fields), "amount")
            == 23);
    } else if (scenario == "fields_invalid") {
        F::click(shell, "Apply fields");
        check(shell.sceneHistoryUsage().undoEntries == 0 && !shell.sceneDirty());
        check(F::label(shell, "Invalid value: amount"));
    } else if (scenario == "fields_reference") {
        F::click(shell, "Value", "target");
        F::choose(shell,
            "target value",
            core::pmr::String("Reference target [") + core::formatUnsigned(files.other.value())
                + ":0]");
        F::click(shell, "Apply fields");
        check(shell.sceneHistoryUsage().undoEntries == 1);
    }
    if (scenario == "fields_draft" || scenario == "fields_invalid")
        check(EditorScriptComponentArchive::equal(before, files.state(shell)));
    // A reproducible scroll exposes actual field widgets in the capture;
    // reference mode additionally shows the list and exact target choice.
    F::command(shell, SetInspectorScrollCommand{scenario == "fields_reference" ? 680.f : 320.f});
    if (scenario == "fields_failed" || scenario == "fields_retry") {
        bool ready;
        {
            core::CpuMemoryQuotaScope quota(core::makeShared<core::CpuMemoryQuota>(1));
            ready = shell.retrySceneInspector();
        }
        check(!ready && shell.inspectorState() == EditorShell::InspectorState::Failed);
        check(EditorScriptComponentArchive::equal(before, files.state(shell)));
        if (scenario == "fields_retry") {
            check(shell.retrySceneInspector());
            check(shell.runtime()
                      .findControlObject(F::control(shell, "amount", ui::UiControlType::TextInput))
                      ->value()
                      .text
                == "23");
        }
    }
    check(shell.inspectorState()
        == (scenario == "fields_failed" ? EditorShell::InspectorState::Failed
                                        : EditorShell::InspectorState::Ready));
    std::printf("PASS actual script Inspector native scenario=%.*s history=%llu\n",
        int(scenario.size()),
        scenario.data(),
        static_cast<unsigned long long>(shell.sceneHistoryUsage().undoEntries));
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
