#pragma once

#include "editor/command/editor_command.h"
#include "editor/workspace/editor_modal_session.h"
#include "ui/ui.h"

#include <optional>

namespace arp::editor {
class EditorShell;

// Presentation drafts and modal lifetime only; workspace commands own all IO.
class EditorProjectPanel final {
public:
    bool activate(EditorShell&, std::string_view source);
    bool edit(EditorShell&, std::string_view source, const core::pmr::String& value);
    void initialize(EditorShell&);
    void synchronize(EditorShell&);
    void commandCompleted(
        std::uint64_t previousVersion, std::uint64_t stateVersion, bool blockedModalInput);

    bool active() const noexcept { return mode_ != Mode::None || menuOpen_; }

    bool modalActive() const noexcept { return mode_ != Mode::None; }

private:
    enum class Mode { None, Create, Open, Settings, Close };
    using Replacement = std::variant<CreateProjectCommand, OpenProjectCommand, CloseProjectCommand>;
    void show(EditorShell&, Mode);
    void hide(EditorShell&);
    void render(EditorShell&);
    void replace(EditorShell&, Replacement);
    void save(EditorShell&);
    void createEntryChoice(EditorShell&);
    void removeEntryChoice(EditorShell&);

    Mode mode_ = Mode::None;
    ProjectTemplateKind templateKind_ = ProjectTemplateKind::Game2D;
    bool menuOpen_ = false;
    bool saving_ = false;
    EditorModalSession modal_;
    std::optional<Replacement> pending_;
    core::pmr::String name_, path_, status_, entryScene_;
    ui::WidgetId entryChoice_;
};
} // namespace arp::editor
