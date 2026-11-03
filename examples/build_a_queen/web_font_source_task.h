#pragma once

#include "startup_resource_task.h"
#include "ui/ui_bitmap_font_resource.h"

#include <array>

namespace arp::build_a_queen {

// Immutable decoded CPU owner. Its atlas handle belongs to this same manager;
// the renderer retains this owner after all startup IO has retired.
class WebFontResources final {
public:
    static constexpr const char* atlasId = "00000000-0000-4000-8000-000000000311";
    static constexpr const char* fontId = "00000000-0000-4000-8000-000000000312";
    static constexpr std::array<const char*, 3> inputs{"font.atlas.yaml", "font.png", "font.yaml"};
    static constexpr std::size_t maxFileBytes = 1024 * 1024;
    static constexpr std::size_t maxSourceBytes = inputs.size() * maxFileBytes;
    static constexpr std::size_t maxDecodedBytes = 4 * 1024 * 1024;

    static core::SharedPtr<const WebFontResources> prepare(
        const resource::ResourceLoadContext&, resource::ResourceFileOrigin, core::pmr::String&);
    const ui::UiBitmapFontResource* font() const noexcept;

private:
    resource::ResourceManager resources_;
};

using WebFontSourcePhase = StartupResourcePhase;

class WebFontSourceTask final {
public:
    WebFontSourceTask(core::io::FileService&,
        core::runtime::CompletionQueue&,
        core::PoolHandle,
        core::runtime::CancellationToken = {});
    void advance() noexcept;
    void cancel() noexcept;
    WebFontSourcePhase phase() const noexcept;
    std::string_view diagnostic() const noexcept;
    std::uint64_t readCount() const noexcept;
    core::SharedPtr<const WebFontResources> resources() const noexcept;

private:
    StartupResourceTask task_;
};
} // namespace arp::build_a_queen
