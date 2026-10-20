#include "editor/workspace/editor_presentation_state.h"

#include <algorithm>

namespace arp::editor {
EditorPresentationState::EditorPresentationState()
    : selected_("ReflectionProbe2", &core::defaultCpuMemoryResource()),
      workspace_("Game", &core::defaultCpuMemoryResource()),
      scene_("level", &core::defaultCpuMemoryResource()),
      dock_("Inspector", &core::defaultCpuMemoryResource()),
      disabledMasks_(&core::defaultCpuMemoryResource()), sources_(&core::defaultCpuMemoryResource())
{}

void EditorPresentationState::select(std::string_view value)
{
    selected_.assign(value.data(), value.size());
}

void EditorPresentationState::setWorkspace(std::string_view value)
{
    workspace_.assign(value.data(), value.size());
}

void EditorPresentationState::setScene(std::string_view value)
{
    scene_.assign(value.data(), value.size());
}

void EditorPresentationState::setDock(std::string_view value)
{
    dock_.assign(value.data(), value.size());
}

void EditorPresentationState::registerSource(
    std::uint64_t widgetId, const core::pmr::String& source)
{
    sources_.emplace(widgetId, source);
}

void EditorPresentationState::unregisterSource(std::uint64_t widgetId)
{
    sources_.erase(widgetId);
}

const core::pmr::String& EditorPresentationState::sourceFor(std::uint64_t widgetId) const
{
    return sources_.at(widgetId);
}

bool EditorPresentationState::toggleDisabledMask(std::string_view source)
{
    const auto found = std::find_if(
        disabledMasks_.begin(), disabledMasks_.end(), [source](const core::pmr::String& value) {
            return std::string_view(value.data(), value.size()) == source;
        });
    if (found != disabledMasks_.end()) {
        disabledMasks_.erase(found);
        return false;
    }
    core::pmr::String mask(&core::defaultCpuMemoryResource());
    mask.assign(source.data(), source.size());
    disabledMasks_.push_back(std::move(mask));
    return true;
}

bool EditorPresentationState::isDisabledMask(std::string_view source) const noexcept
{
    return std::find_if(disabledMasks_.begin(),
               disabledMasks_.end(),
               [source](const core::pmr::String& value) {
                   return std::string_view(value.data(), value.size()) == source;
               })
        != disabledMasks_.end();
}

std::size_t EditorPresentationState::advanceUpdateMode() noexcept
{
    updateMode_ = (updateMode_ + 1) % 3;
    return updateMode_;
}
} // namespace arp::editor
