#pragma once

#include "core/ownership/lifetime_epoch.h"
#include "ui/ui.h"

#include <array>
#include <cstdint>

namespace arp::editor {

// One primary pointer owns the complete sequence, including a cancelled tail.
// Identity storage is bounded; field metadata is resolved anew at each drop.
class EditorSceneReferenceDrag final {
public:
    static constexpr float thresholdPixels = 5.0f;
    bool captured = false;
    bool suppressRelease = false;
    bool cancelled = false;
    bool dragging = false;
    bool allowed = false;
    bool fileSource = false;
    std::uint64_t objectId = 0, sourceObjectId = 0;
    std::uint64_t documentEpoch = 0, resourceRevision = 0;
    core::LifetimeEpoch::Ticket sceneOwner;
    // Resource ID contract: canonical UUID (36 ASCII bytes) plus terminator.
    static constexpr std::size_t uuidBytes = 36;
    std::array<char, uuidBytes + 1> resourceId{};
    ui::WidgetId sourceWidget, targetWidget;
    float startX = 0, startY = 0, x = 0, y = 0;

    void cancel() noexcept
    {
        cancelled = captured;
        allowed = false;
        targetWidget = {};
    }

    void reset() noexcept { *this = {}; }
};
} // namespace arp::editor
