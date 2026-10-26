#include "editor/workspace/editor_scene_transform_panel.h"

#include "core/text/number_format.h"
#include "editor/workspace/editor_shell.h"

#include <charconv>
#include <new>

namespace arp::editor {
namespace {
constexpr const char* sources[] = {"transform_ui_modal",
    "transform_ui_tool",
    "transform_ui_space",
    "transform_ui_pivot",
    "transform_ui_snap",
    "transform_ui_move",
    "transform_ui_rotate",
    "transform_ui_scale",
    "transform_ui_apply",
    "transform_ui_cancel",
    "transform_ui_invalid",
    "transform_ui_failed",
    "scene_transform_settings"};

bool positiveNumber(std::string_view text, double& destination) noexcept
{
    if (text.empty())
        return false;
    double value = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value)
        || value <= 0)
        return false;
    destination = value;
    return true;
}
} // namespace

bool EditorSceneTransformPanel::initialize(EditorShell& shell)
{
    for (std::size_t i = 0; i != Count; ++i)
        ids_[i] = shell.id(sources[i]);
    if (!validBindings(shell))
        return false;
    modal_ = EditorModalSession{};
    active_ = false;
    shell.runtime().canvas().findWidget(ids_[Root])->setVisible(false);
    errors(shell, false, false);
    return true;
}

bool EditorSceneTransformPanel::validBindings(EditorShell& shell) const noexcept
{
    for (std::size_t i = 0; i != Count; ++i) {
        const auto* widget = shell.runtime().canvas().findWidget(ids_[i]);
        if (!widget)
            return false;
        const auto type = i >= Move && i <= Scale ? ui::UiControlType::TextInput
            : i == Invalid || i == Failed         ? ui::UiControlType::Label
                                                  : ui::UiControlType::Button;
        if (widget->desc().controlType != type
            || (type == ui::UiControlType::TextInput
                && !shell.runtime().findControlObject(ids_[i])))
            return false;
        if (i != Root && i != Launcher
            && !shell.runtime().canvas().isInSubtree(ids_[i], ids_[Root]))
            return false;
    }
    return true;
}

bool EditorSceneTransformPanel::handles(std::string_view source) noexcept
{
    return source == sources[Launcher] || source.substr(0, 13) == "transform_ui_";
}

bool EditorSceneTransformPanel::textSource(std::string_view source) noexcept
{
    return source == sources[Move] || source == sources[Rotate] || source == sources[Scale];
}

bool EditorSceneTransformPanel::interactable(EditorShell& shell, Index index) const
{
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* widget = layout.findWidget(ids_[index]);
    return layout.ok() && widget && widget->visibleInHierarchy && widget->enabledInHierarchy;
}

void EditorSceneTransformPanel::captions(
    EditorShell& shell, const EditorSceneTransformSettings& value)
{
    const std::string_view text[] = {EditorSceneTransformSettings::toolName(value.tool),
        EditorSceneTransformSettings::spaceName(value.space),
        EditorSceneTransformSettings::pivotName(value.pivot),
        value.snap.enabled ? "On" : "Off"};
    const auto prepare = [&](std::size_t i) {
        return core::pmr::String(text[i],
            shell.runtime()
                .canvas()
                .findWidget(ids_[Tool + i])
                ->desc()
                .text.get_allocator()
                .resource());
    };
    std::array<core::pmr::String, 4> prepared = {prepare(0), prepare(1), prepare(2), prepare(3)};
    for (std::size_t i = 0; i != prepared.size(); ++i)
        shell.runtime().canvas().findWidget(ids_[Tool + i])->setText(std::move(prepared[i]));
}

void EditorSceneTransformPanel::errors(EditorShell& shell, bool invalid, bool failed) noexcept
{
    if (auto* widget = shell.runtime().canvas().findWidget(ids_[Invalid]))
        widget->setVisible(invalid);
    if (auto* widget = shell.runtime().canvas().findWidget(ids_[Failed]))
        widget->setVisible(failed);
}

EditorCommandOutcome EditorSceneTransformPanel::open(EditorShell& shell)
{
    const auto gate = shell.sceneTransformSettingsGate();
    if (!gate.success)
        return gate;
    if (!validBindings(shell) || !interactable(shell, Launcher))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const auto current = shell.sceneTransformSettings();
    const double steps[] = {
        current.snap.moveStep, current.snap.rotateStepRadians, current.snap.scaleStep};
    std::array<core::pmr::String, 3> values;
    for (std::size_t i = 0; i != values.size(); ++i)
        if (!core::appendFloating(values[i], steps[i]))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    // The draft may be prepared while hidden; live preferences remain untouched.
    for (std::size_t i = 0; i != values.size(); ++i)
        if (!shell.runtime()
                .setValue(ids_[Move + i], ui::UiControlValue::fromText(std::move(values[i])))
                .ok())
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    captions(shell, current);
    errors(shell, false, false);
    if (!modal_.open(shell, sources[Root]))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    draft_ = current;
    active_ = true;
    return {true, true, EditorDiagnosticCode::None};
}

bool EditorSceneTransformPanel::readDraft(
    EditorShell& shell, EditorSceneTransformSettings& value) const noexcept
{
    value = draft_;
    double* steps[] = {&value.snap.moveStep, &value.snap.rotateStepRadians, &value.snap.scaleStep};
    for (std::size_t i = 0; i != 3; ++i) {
        const auto* control = shell.runtime().findControlObject(ids_[Move + i]);
        if (!control || control->value().kind != ui::UiControlValueKind::Text
            || !positiveNumber(control->value().text, *steps[i]))
            return false;
    }
    return value.valid();
}

void EditorSceneTransformPanel::closePresentation(EditorShell& shell) noexcept
{
    modal_.closePresentation(shell);
    active_ = false;
}

EditorCommandOutcome EditorSceneTransformPanel::apply(EditorShell& shell)
{
    EditorSceneTransformSettings value;
    if (!readDraft(shell, value)) {
        errors(shell, true, false);
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    const auto gate = shell.sceneTransformSettingsGate(this);
    if (!gate.success) {
        errors(shell, false, true);
        return gate;
    }
    // Input callbacks and any pending runtime work must finish before publication.
    if (!shell.runtime().cancelInput().ok()) {
        errors(shell, false, true);
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    const auto result = shell.setSceneTransformSettings(value, this);
    if (!result.success) {
        errors(shell, false, true);
        return result;
    }
    closePresentation(shell);
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorSceneTransformPanel::activate(
    EditorShell& shell, std::string_view source)
{
    try {
        if (source == sources[Launcher])
            return open(shell);
        if (!active_ || !validBindings(shell))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        std::size_t index = Root;
        while (index != Count && source != sources[index])
            ++index;
        if (index == Count || !interactable(shell, static_cast<Index>(index)))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        if (index == Apply)
            return apply(shell);
        if (index == Cancel) {
            if (!shell.runtime().cancelInput().ok())
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            closePresentation(shell);
            return {true, true, EditorDiagnosticCode::None};
        }
        if (index == Root)
            return {true, false, EditorDiagnosticCode::None};
        auto candidate = draft_;
        if (index == Tool)
            candidate.tool = candidate.tool == EditorSceneTransformTool::Select
                ? EditorSceneTransformTool::Move
                : candidate.tool == EditorSceneTransformTool::Move
                ? EditorSceneTransformTool::Rotate
                : candidate.tool == EditorSceneTransformTool::Rotate
                ? EditorSceneTransformTool::Scale
                : EditorSceneTransformTool::Select;
        else if (index == Space)
            candidate.space = candidate.space == EditorSceneTransformSpace::World
                ? EditorSceneTransformSpace::Local
                : EditorSceneTransformSpace::World;
        else if (index == Pivot)
            candidate.pivot = candidate.pivot == EditorSceneTransformPivot::Active
                ? EditorSceneTransformPivot::Center
                : candidate.pivot == EditorSceneTransformPivot::Center
                ? EditorSceneTransformPivot::Individual
                : EditorSceneTransformPivot::Active;
        else if (index == Snap)
            candidate.snap.enabled = !candidate.snap.enabled;
        else
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        captions(shell, candidate);
        draft_ = candidate;
        errors(shell, false, false);
        return {true, true, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        errors(shell, false, active_);
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
}

EditorCommandOutcome EditorSceneTransformPanel::edit(
    EditorShell& shell, std::string_view source, const core::pmr::String& value)
{
    try {
        if (!active_ || !validBindings(shell))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        const auto index = source == sources[Move] ? Move
            : source == sources[Rotate]            ? Rotate
                                                   : Scale;
        if (!textSource(source) || !interactable(shell, index)
            || value.find_first_of("\r\n") != value.npos || value.find('\0') != value.npos
            || !shell.runtime().setValue(ids_[index], ui::UiControlValue::fromText(value)).ok())
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        errors(shell, false, false);
        return {true, true, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        errors(shell, false, active_);
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
}
} // namespace arp::editor
