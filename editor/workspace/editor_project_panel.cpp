#include "editor/workspace/editor_project_panel.h"

#include "editor/project/project_identity.h"
#include "editor/workspace/editor_shell.h"
#include "resource/resource_contract.h"
#include "scene/scene_asset_resource.h"

namespace arp::editor {
namespace {
constexpr std::size_t maxNameBytes = 128;
constexpr std::size_t maxPathBytes = 512;
constexpr std::size_t maxEntrySceneChoices = 64;
constexpr std::uint32_t entryVisibleRows = 6;

bool newManifest(const core::pmr::String& name,
    ProjectTemplateKind kind,
    ProjectManifest& manifest,
    core::pmr::String& diagnostic)
{
    if (!ProjectIdentity::create("EditorProject/1", manifest.id, diagnostic))
        return false;
    manifest.name = name;
    // This binds the implemented authoring format, not an unimplemented Player ABI.
    manifest.engineContract = "arp.scene-asset/2";
    if (kind != ProjectTemplateKind::Empty)
        manifest.entryScene = resource::deriveResourceUuid(manifest.id, "entry-scene/1");
    return true;
}
} // namespace

void EditorProjectPanel::initialize(EditorShell& shell)
{
    shell.visible("project_ui_menu", false);
    shell.visible("project_ui_modal", false);
}

void EditorProjectPanel::show(EditorShell& shell, Mode mode)
{
    removeEntryChoice(shell);
    mode_ = mode;
    menuOpen_ = false;
    pending_.reset();
    status_.clear();
    saving_ = false;
    name_ = mode == Mode::Settings && shell.projectManifest() ? shell.projectManifest()->name
                                                              : core::pmr::String{};
    path_.clear();
    entryScene_ = mode == Mode::Settings && shell.projectManifest()
        ? shell.projectManifest()->entryScene
        : core::pmr::String{};
    if (name_.size() > maxNameBytes) {
        name_.clear();
        status_ =
            "The existing name exceeds the UI limit. Enter a name of at most 128 UTF-8 bytes.";
    }
    templateKind_ = ProjectTemplateKind::Game2D;
    shell.presentationState_.closeMenu();
    shell.visible("menu_popup", false);
    shell.visible("project_ui_menu", false);
    shell.showTerrainBrowser(false);
    modal_.open(shell, "project_ui_modal");
    if (mode == Mode::Settings && shell.projectManifest())
        createEntryChoice(shell);
    shell.text("project_ui_name", name_);
    shell.text("project_ui_path", path_);
    render(shell);
    (void)shell.runtime().requestFocus(shell.id(mode == Mode::Open ? "project_ui_path"
            : mode == Mode::Close                                  ? "project_ui_cancel"
                                                                   : "project_ui_name"));
}

void EditorProjectPanel::hide(EditorShell& shell)
{
    removeEntryChoice(shell);
    mode_ = Mode::None;
    menuOpen_ = false;
    pending_.reset();
    saving_ = false;
    shell.visible("project_ui_modal", false);
    shell.visible("project_ui_menu", false);
    modal_.close(shell);
}

void EditorProjectPanel::render(EditorShell& shell)
{
    const bool confirming = pending_.has_value();
    const bool creating = mode_ == Mode::Create;
    const bool settings = mode_ == Mode::Settings;
    const bool hasName = !confirming && (creating || settings);
    const bool hasPath = !confirming && (creating || mode_ == Mode::Open);
    shell.text("project_ui_title",
        confirming                ? "Unsaved changes"
            : creating            ? "New Project"
            : settings            ? "Project Settings"
            : mode_ == Mode::Open ? "Open Project"
                                  : "Close Project");
    shell.text("project_ui_description",
        confirming     ? "Scene, scripts or project settings have unsaved changes."
            : creating ? "Choose a template and a new project folder."
            : settings
            ? "Save the project name and startup scene. Scene files are saved separately."
            : mode_ == Mode::Open ? "Open the folder containing project.arp.yaml."
                                  : "Close the current project and return to folder browsing.");
    for (const char* id :
        {"project_ui_name", "project_ui_name_label", "project_ui_name_background"})
        shell.visible(id, hasName);
    for (const char* id :
        {"project_ui_path", "project_ui_path_label", "project_ui_path_background"})
        shell.visible(id, hasPath);
    shell.visible("project_ui_entry_label", settings && !confirming);
    if (entryChoice_.valid()) {
        (void)shell.runtime().setVisible(entryChoice_, settings && !confirming);
        (void)shell.runtime().setEnabled(entryChoice_, !saving_);
    }
    for (const char* id :
        {"project_ui_template_label", "project_ui_empty", "project_ui_2d", "project_ui_3d"})
        shell.visible(id, creating && !confirming);
    shell.text("project_ui_empty",
        templateKind_ == ProjectTemplateKind::Empty ? "Empty (selected)" : "Empty");
    shell.text(
        "project_ui_2d", templateKind_ == ProjectTemplateKind::Game2D ? "2D (selected)" : "2D");
    shell.text(
        "project_ui_3d", templateKind_ == ProjectTemplateKind::Game3D ? "3D (selected)" : "3D");
    shell.text("project_ui_path_label",
        creating ? "New folder (512 UTF-8 bytes; must not exist)"
                 : "Project folder (512 UTF-8 bytes)");
    shell.text("project_ui_status",
        confirming ? "Discard and Continue permanently abandons unsaved edits in this workspace."
                   : status_);
    shell.text("project_ui_submit",
        creating                  ? "Create Project"
            : settings            ? "Save Settings"
            : mode_ == Mode::Open ? "Open Project"
                                  : "Close Project");
    shell.visible("project_ui_submit", !confirming);
    shell.visible("project_ui_discard", confirming);
    for (const char* id : {"project_ui_submit",
             "project_ui_name",
             "project_ui_path",
             "project_ui_empty",
             "project_ui_2d",
             "project_ui_3d"})
        (void)shell.runtime().setEnabled(shell.id(id),
            !saving_
                && (std::string_view(id) != "project_ui_submit" || !settings
                    || entryChoice_.valid()));
}

void EditorProjectPanel::removeEntryChoice(EditorShell& shell)
{
    if (!entryChoice_.valid())
        return;
    if (auto* dropdown =
            dynamic_cast<EditorEnumDropdown*>(shell.runtime().findControlObject(entryChoice_)))
        dropdown->closeMenu();
    shell.presentationState_.unregisterSource(entryChoice_.value());
    (void)shell.runtime().removeControl(entryChoice_);
    entryChoice_ = {};
}

void EditorProjectPanel::createEntryChoice(EditorShell& shell)
{
    const auto candidates =
        shell.projectResources().snapshotReferences("SceneAssetResource", maxEntrySceneChoices);
    if (!candidates.success) {
        status_ = "Cannot list startup scenes. " + candidates.diagnostics;
        return;
    }
    core::pmr::Vector<SceneInspectorEnumOption> options;
    options.emplace_back();
    options.back().name = "<None - startup scene not configured>";
    std::uint32_t selected = 0;
    for (const auto& candidate : candidates.entries) {
        if (candidate.state != resource::ResourceState::Ready)
            continue;
        SceneInspectorEnumOption option;
        option.commandValue = candidate.reference.id;
        option.name = candidate.reference.displayName.empty()
            ? candidate.reference.id
            : candidate.reference.displayName + " | " + candidate.reference.id;
        if (candidate.reference.id == entryScene_)
            selected = static_cast<std::uint32_t>(options.size());
        options.push_back(std::move(option));
    }
    if (!entryScene_.empty() && !selected) {
        status_ = "Startup scene is no longer Ready. Reopen the project to resolve its resources.";
        return;
    }
    ui::UiControlDesc desc;
    desc.type = ui::UiControlType::Dropdown;
    desc.widget.name = "project_ui_entry";
    desc.widget.localRect = ui::LayoutRect::fromOriginSize(24, 240, 850, 40);
    desc.widget.styleId = "input";
    desc.widget.fontToken = "font.editor";
    desc.widget.acceptsFocus = true;
    desc.initialValue = ui::UiControlValue::fromIndex(selected);
    for (const auto& option : options)
        desc.optionLabels.push_back(option.name);
    const auto created = shell.runtime().createControl(core::makeUnique<EditorEnumDropdown>(shell,
                                                           "project_ui_entry",
                                                           std::move(options),
                                                           true,
                                                           shell.id("project_ui_modal"),
                                                           entryVisibleRows),
        std::move(desc),
        shell.id("project_ui_panel"));
    if (!created.ok()) {
        status_ = "Cannot create startup scene selector. " + created.diagnostic().message;
        return;
    }
    entryChoice_ = created.widgetId();
    shell.presentationState_.registerSource(entryChoice_.value(), "project_ui_entry");
}

void EditorProjectPanel::commandCompleted(
    std::uint64_t previousVersion, std::uint64_t stateVersion, bool blockedModalInput)
{
    modal_.commandCompleted(previousVersion, stateVersion, blockedModalInput);
}

void EditorProjectPanel::replace(EditorShell& shell, Replacement operation)
{
    const auto result = std::visit(
        [&](const auto& command) {
            return shell.submitCommand(command);
        },
        operation);
    if (result.status == EditorResponseStatus::Completed) {
        hide(shell);
        shell.activate("workspace_3D");
    } else if (result.diagnostic == EditorDiagnosticCode::UnsavedChanges) {
        pending_ = std::move(operation);
        modal_.armConfirmation();
        render(shell);
        (void)shell.runtime().requestFocus(shell.id("project_ui_cancel"));
    } else {
        status_ = result.diagnostic == EditorDiagnosticCode::WorkspaceBusy
            ? "The workspace is busy. Finish pending file operations, terrain work or Play first."
            : "Project operation failed. " + shell.projectDiagnostic_;
        render(shell);
    }
}

void EditorProjectPanel::save(EditorShell& shell)
{
    if (!shell.projectManifest() || !entryChoice_.valid()) {
        status_ = "No project is open.";
        render(shell);
        return;
    }
    if (name_.empty()) {
        status_ = "Enter a project name.";
        render(shell);
        return;
    }
    if (auto* dropdown =
            dynamic_cast<EditorEnumDropdown*>(shell.runtime().findControlObject(entryChoice_)))
        dropdown->closeMenu();
    const auto renamed = shell.submitCommand(SetProjectSettingsCommand{name_, entryScene_});
    const auto saved = renamed.status == EditorResponseStatus::Completed
        ? shell.submitCommand(SaveProjectCommand{})
        : renamed;
    if (saved.status == EditorResponseStatus::Completed) {
        saving_ = bool(shell.projectSaveRequest_);
        status_ = saving_ ? "Saving project settings..." : "Project settings saved.";
    } else
        status_ = "Settings were not saved. " + shell.projectDiagnostic_;
    render(shell);
}

void EditorProjectPanel::synchronize(EditorShell& shell)
{
    if (saving_ && !shell.projectSaveRequest_) {
        saving_ = false;
        status_ = shell.projectDiagnostic_.empty()
            ? "Project settings saved."
            : "Settings were not saved. " + shell.projectDiagnostic_;
        render(shell);
    }
}

bool EditorProjectPanel::edit(
    EditorShell& shell, std::string_view source, const core::pmr::String& value)
{
    if (source != "project_ui_name" && source != "project_ui_path" && source != "project_ui_entry")
        return false;
    if (mode_ == Mode::None || pending_ || saving_)
        return true;
    if (source == "project_ui_entry") {
        if (mode_ == Mode::Settings)
            entryScene_ = value;
        render(shell);
        return true;
    }
    const auto limit = source == "project_ui_name" ? maxNameBytes : maxPathBytes;
    if (!ui::TextInputContract::validate(value, limit).ok()) {
        status_ = source == "project_ui_name" ? "Name must be one line, at most 128 UTF-8 bytes."
                                              : "Path must be one line, at most 512 UTF-8 bytes.";
        // Retain the last accepted draft rather than silently truncating input.
        shell.text(core::pmr::String(source), source == "project_ui_name" ? name_ : path_);
    } else {
        (source == "project_ui_name" ? name_ : path_) = value;
        status_.clear();
    }
    render(shell);
    return true;
}

bool EditorProjectPanel::activate(EditorShell& shell, std::string_view source)
{
    if (source == "menu_Project") {
        if (mode_ != Mode::None)
            return true;
        menuOpen_ = !menuOpen_;
        shell.presentationState_.closeMenu();
        shell.visible("menu_popup", false);
        shell.visible("project_ui_menu", menuOpen_);
        return true;
    }
    if (source.substr(0, 11) != "project_ui_") {
        if (mode_ == Mode::None && menuOpen_) {
            menuOpen_ = false;
            shell.visible("project_ui_menu", false);
        }
        return false;
    }
    if (source == "project_ui_cancel") {
        hide(shell);
        return true;
    }
    if (source == "project_ui_entry") {
        if (mode_ == Mode::Settings && entryChoice_.valid() && !saving_)
            (void)shell.runtime().interact(ui::UiControlInteraction::click(entryChoice_));
        return true;
    }
    if (source == "project_ui_new")
        show(shell, Mode::Create);
    else if (source == "project_ui_open")
        show(shell, Mode::Open);
    else if (source == "project_ui_settings")
        show(shell, Mode::Settings);
    else if (source == "project_ui_close")
        show(shell, Mode::Close);
    else if (source == "project_ui_empty" || source == "project_ui_2d"
        || source == "project_ui_3d") {
        if (mode_ == Mode::Create && !pending_ && !saving_) {
            templateKind_ = source == "project_ui_empty" ? ProjectTemplateKind::Empty
                : source == "project_ui_2d"              ? ProjectTemplateKind::Game2D
                                                         : ProjectTemplateKind::Game3D;
            render(shell);
        }
    } else if (source == "project_ui_discard" && pending_) {
        if (!modal_.confirmationValid(shell.stateVersion())) {
            pending_.reset();
            status_ = "The workspace changed. Review and submit the operation again.";
            render(shell);
        } else {
            auto operation = std::move(*pending_);
            pending_.reset();
            std::visit(
                [](auto& command) {
                    command.discardChanges = true;
                },
                operation);
            replace(shell, std::move(operation));
        }
    } else if (source == "project_ui_submit" && mode_ != Mode::None && !pending_ && !saving_) {
        if (mode_ == Mode::Settings)
            save(shell);
        else if (mode_ == Mode::Close)
            replace(shell, CloseProjectCommand{});
        else if (path_.empty() || (mode_ == Mode::Create && name_.empty())) {
            status_ = "Enter the required project name and folder.";
            render(shell);
        } else if (mode_ == Mode::Open)
            replace(shell, OpenProjectCommand{path_, false});
        else {
            ProjectManifest manifest;
            try {
                core::pmr::String diagnostic;
                if (!newManifest(name_, templateKind_, manifest, diagnostic)) {
                    status_ = diagnostic;
                    render(shell);
                    return true;
                }
            } catch (...) {
                status_ = "Unable to allocate a new project identity. No project was created.";
                render(shell);
                return true;
            }
            replace(shell, CreateProjectCommand{path_, std::move(manifest), false, templateKind_});
        }
    }
    return true;
}
} // namespace arp::editor
