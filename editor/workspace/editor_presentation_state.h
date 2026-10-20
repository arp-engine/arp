#pragma once

#include "core/cpu_allocator.h"

#include <cstdint>
#include <string_view>

namespace arp::editor {
class EditorPresentationState final {
public:
    EditorPresentationState();

    const core::pmr::String& selection() const noexcept { return selected_; }

    const core::pmr::String& workspace() const noexcept { return workspace_; }

    const core::pmr::String& scene() const noexcept { return scene_; }

    const core::pmr::String& dock() const noexcept { return dock_; }

    void select(std::string_view value);
    void setWorkspace(std::string_view value);
    void setScene(std::string_view value);
    void setDock(std::string_view value);

    void registerSource(std::uint64_t widgetId, const core::pmr::String& source);
    void unregisterSource(std::uint64_t widgetId);
    const core::pmr::String& sourceFor(std::uint64_t widgetId) const;
    bool toggleDisabledMask(std::string_view source);
    bool isDisabledMask(std::string_view source) const noexcept;
    std::size_t advanceUpdateMode() noexcept;

    bool outputOpen() const noexcept { return outputOpen_; }

    void toggleOutput() noexcept { outputOpen_ = !outputOpen_; }

    void openOutput() noexcept { outputOpen_ = true; }

    bool menuOpen() const noexcept { return menuOpen_; }

    void toggleMenu() noexcept { menuOpen_ = !menuOpen_; }

    void closeMenu() noexcept { menuOpen_ = false; }

private:
    core::pmr::String selected_;
    core::pmr::String workspace_;
    core::pmr::String scene_;
    core::pmr::String dock_;
    core::pmr::Vector<core::pmr::String> disabledMasks_;
    core::pmr::UnorderedMap<std::uint64_t, core::pmr::String> sources_;
    std::size_t updateMode_ = 0;
    bool outputOpen_ = false;
    bool menuOpen_ = false;
};
} // namespace arp::editor
