#pragma once

#include "editor/command/editor_command.h"
#include "editor/workspace/editor_modal_session.h"

#include <array>
#include <optional>

namespace arp::editor {
class EditorShell;

// Scene document presentation; serialization and IO belong to workspace commands.
class EditorScenePanel final {
public:
    void initialize(EditorShell&);
    static bool handles(std::string_view source) noexcept;
    EditorCommandOutcome activate(EditorShell&, std::string_view source);
    EditorCommandOutcome requestSave(EditorShell&, bool saveAs);
    EditorCommandOutcome edit(EditorShell&, const core::pmr::String& value);
    void synchronize(EditorShell&);

    bool active() const noexcept { return mode_ != Mode::None; }

    void commandCompleted(std::uint64_t previous, std::uint64_t current, bool blockedInput);

private:
    enum class Mode { None, New, Open, SaveAs, Duplicate, Export };
    enum class TaskKind { None, Persistence, Export };
    using Operation = std::variant<NewSceneCommand,
        LoadSceneCommand,
        SaveSceneCommand,
        DuplicateSceneCommand,
        ExportSceneCommand>;

    enum Index {
        Root,
        Title,
        Description,
        DescriptionDetail,
        PathLabel,
        PathBackground,
        Path,
        Status,
        Detail,
        Submit,
        Confirm,
        Cancel,
        Failed,
        Retry,
        Notice,
        NoticeRetry,
        Output,
        Menu,
        Count
    };

    bool validBindings(EditorShell&) const noexcept;
    bool setText(EditorShell&, Index, std::string_view);
    bool renderDraft(EditorShell&,
        Mode,
        std::string_view path,
        std::string_view status,
        std::string_view detail,
        bool confirming,
        bool overwrite);
    bool render(EditorShell&);
    EditorCommandOutcome show(EditorShell&, Mode);
    EditorCommandOutcome hide(EditorShell&);
    void closePresentation(EditorShell&) noexcept;
    void failureNotice(EditorShell&) noexcept;
    void clearFailure(EditorShell&) noexcept;
    bool retryDisplay(EditorShell&) noexcept;
    EditorResponse submit(EditorShell&, Operation);

    Mode mode_ = Mode::None;
    EditorModalSession modal_;
    std::optional<Operation> pending_;
    core::pmr::String path_, status_, detail_;
    TaskKind taskKind_ = TaskKind::None;
    std::uint64_t taskId_ = 0;
    std::array<ui::WidgetId, Count> ids_{};
    bool presentationFailed_ = false;
    bool feedbackPending_ = false;
    bool refreshFilesPending_ = false;
};
} // namespace arp::editor
