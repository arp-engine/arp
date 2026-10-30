#include "core/io/filesystem_path.h"
#include "core/text/string_utils.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>
#include <cstdio>
#include <new>
#include <system_error>

namespace arp::editor {
using namespace layout;

namespace {
const char* fileIconResource(const std::filesystem::path& path, bool directory)
{
    if (directory)
        return "icon-folder";
    auto extension = core::FilePath::utf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) {
        return core::toLowerAscii(c);
    });
    if (extension == ".arp" || extension == ".gd" || extension == ".gdc" || extension == ".cs"
        || extension == ".py")
        return "icon-workspace-script";
    if (extension == ".tscn" || extension == ".scn" || extension == ".scene")
        return "icon-scene-tab-level";
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".webp"
        || extension == ".bmp" || extension == ".tga" || extension == ".svg")
        return "icon-grid";
    if (extension == ".wav" || extension == ".ogg" || extension == ".mp3" || extension == ".flac")
        return "icon-sound";
    if (extension == ".mp4" || extension == ".webm" || extension == ".ogv")
        return "icon-run-movie";
    if (extension == ".shader" || extension == ".gdshader" || extension == ".glsl")
        return "icon-tool-script";
    if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb"
        || extension == ".blend")
        return "icon-cube";
    return "icon-file";
}

} // namespace

bool EditorShell::updateFilePathText()
{
    const auto relative =
        projectBrowser_.currentDirectory().lexically_relative(projectBrowser_.root());
    core::pmr::String value("res://", &core::defaultCpuMemoryResource());
    if (!relative.empty() && relative != ".") {
        value += core::FilePath::utf8(relative);
        if (value.back() != '/')
            value += '/';
    }
    if (value.size() > 64) {
        const auto tail = value.substr(value.size() - 58);
        value = "res://.../";
        value += tail;
    }
    auto result = runtime().setValue(
        id("files_path"), ui::UiControlValue::fromText(value), ui::UiNotifyPolicy::Silent);
    if (!result.ok())
        diagnostic_ = result.diagnostic();
    return result.ok();
}

void EditorShell::refreshFileSystem()
{
    setResourceNavigationRejected(false);
    if (projectBrowser_.sourceSnapshot()) {
        (void)retryFileBrowserPresentation();
        return;
    }
    if (!projectBrowser_.refresh()) {
        feedback("Filesystem error: cannot read "
            + core::FilePath::utf8(projectBrowser_.currentDirectory()));
        updateFilePathText();
        updateFileSlots();
        return;
    }
    (void)retryFileBrowserPresentation();
}

bool EditorShell::initializeFileBrowserPresentation()
{
    filePresentationWidgetCount_ = 0;
    const auto retain = [&](const core::pmr::String& name) {
        const auto widget = id(name);
        if (runtime().canvas().findWidget(widget))
            filePresentationWidgets_[filePresentationWidgetCount_++] = widget;
    };
    retain("files_path");
    retain("file_search");
    retain("file_search_hint");
    for (std::size_t index = 0; index < kFileSlotCount; ++index) {
        retain(kFileSlots[index]);
        if (index < 7) {
            retain(core::pmr::String("folder_icon_") + kFolderSlotNames[index]);
            retain(core::pmr::String("folder_arrow_") + kFolderSlotNames[index]);
        } else {
            char name[32];
            std::snprintf(name, sizeof(name), "asset_icon_%zu", index - 7);
            retain(name);
        }
    }
    resourceNavigationNotice_ = {
        id("file_definition_unavailable"), id("file_definition_retry_hint")};
    for (const auto widget : resourceNavigationNotice_)
        if (!runtime().canvas().findWidget(widget))
            return false;
    setResourceNavigationRejected(false);
    filePresentationFailure_ = id("file_navigation_failed");
    filePresentationRetry_ = id("file_navigation_retry");
    return runtime().canvas().findWidget(filePresentationFailure_)
        && runtime().canvas().findWidget(filePresentationRetry_);
}

void EditorShell::setResourceNavigationRejected(bool rejected) noexcept
{
    for (const auto id : resourceNavigationNotice_)
        if (auto* widget = runtime().canvas().findWidget(id))
            widget->setVisible(rejected);
}

void EditorShell::setFileBrowserPresentationFailed(bool failed) noexcept
{
    if (failed)
        setResourceNavigationRejected(false);
    filesPresentationFailed_ = failed;
    if (failed)
        fileDraggingScrollbar_ = false;
    for (std::size_t index = 0; index < filePresentationWidgetCount_; ++index)
        if (auto* widget = runtime().canvas().findWidget(filePresentationWidgets_[index]))
            widget->setVisible(!failed);
    if (auto* widget = runtime().canvas().findWidget(filePresentationFailure_))
        widget->setVisible(failed);
    if (auto* widget = runtime().canvas().findWidget(filePresentationRetry_))
        widget->setVisible(failed);
}

bool EditorShell::retryFileBrowserPresentation() noexcept
{
    try {
        const auto search = runtime().setValue(id("file_search"),
            ui::UiControlValue::fromText(projectBrowser_.search()),
            ui::UiNotifyPolicy::Silent);
        if (search.ok() && updateFilePathText()) {
            setFileBrowserPresentationFailed(false);
            visible("file_search_hint", projectBrowser_.search().empty());
            updateFileSlots();
            return true;
        }
    } catch (const std::bad_alloc&) {
        // Cached integer Widget identities make isolation allocation-free.
    }
    setFileBrowserPresentationFailed(true);
    return false;
}

void EditorShell::updateFileSlots()
{
    if (filesPresentationFailed_)
        return;
    const auto& entries = projectBrowser_.entries();
    const auto& visibleEntries = projectBrowser_.visibleEntryIndices();
    const std::size_t maxOffset =
        visibleEntries.size() > kFileSlotCount ? visibleEntries.size() - kFileSlotCount : 0;
    fileScrollOffset_ = std::min(fileScrollOffset_, maxOffset);
    for (std::size_t slot = 0; slot < kFileSlotCount; ++slot) {
        const core::pmr::String source(kFileSlots[slot], &core::defaultCpuMemoryResource());
        auto* row = runtime().canvas().findWidget(id(source));
        if (!row)
            continue;
        const std::size_t shown = fileScrollOffset_ + slot;
        const bool populated = shown < visibleEntries.size();
        row->setVisible(populated);
        core::pmr::String iconSource("asset_icon_", &core::defaultCpuMemoryResource());
        if (slot < 7) {
            iconSource = "folder_icon_";
            iconSource += kFolderSlotNames[slot];
        } else {
            char iconName[24];
            std::snprintf(iconName, sizeof(iconName), "asset_icon_%zu", slot - 7);
            iconSource = iconName;
        }
        auto* icon = runtime().canvas().findWidget(id(iconSource));
        if (icon)
            icon->setVisible(populated);
        auto* arrow = slot < 7
            ? runtime().canvas().findWidget(
                  id(core::pmr::String("folder_arrow_", &core::defaultCpuMemoryResource())
                      + kFolderSlotNames[slot]))
            : nullptr;
        if (arrow)
            arrow->setVisible(false);
        if (!populated)
            continue;
        const auto entryIndex = visibleEntries[shown];
        if (entryIndex >= entries.size())
            continue;
        const auto& entry = entries[entryIndex];
        row->setText(entry.name);
        row->setEnabled(true);
        if (icon)
            icon->setTextureResourceId(core::pmr::String(
                fileIconResource(entry.path, entry.directory), &core::defaultCpuMemoryResource()));
        if (slot < 7) {
            if (arrow) {
                arrow->setVisible(entry.directory);
                arrow->setTextureResourceId(
                    core::pmr::String("icon-chevron-right", &core::defaultCpuMemoryResource()));
            }
        }
    }
}

bool EditorShell::navigateTo(const std::filesystem::path& path)
{
    if (projectBrowser_.sourceSnapshot()) {
        auto relative = core::FilePath::utf8(path.lexically_relative(projectBrowser_.root()));
        if (relative == ".")
            relative.clear();
        return submitCommand(
                   NavigateProjectSourceCommand{
                       ProjectBrowserModel::SourceNavigation::Navigate, std::move(relative)})
                   .status
            == EditorResponseStatus::Completed;
    }
    const auto result = projectBrowser_.navigate(path);
    if (result == ProjectBrowserModel::NavigationResult::Rejected) {
        feedback("Filesystem error: path is outside the project or unavailable");
        return false;
    }
    if (result == ProjectBrowserModel::NavigationResult::Unchanged)
        return true;
    selectedFilePath_.clear();
    fileScrollOffset_ = 0;
    lastClickedScriptPath_.clear();
    refreshFileSystem();
    return true;
}

} // namespace arp::editor
