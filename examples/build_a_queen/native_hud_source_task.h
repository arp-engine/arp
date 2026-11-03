#pragma once

#include "startup_resource_task.h"
#include "ui/ui_atlas_image_resource.h"
#include "ui/ui_font.h"

namespace arp::build_a_queen {

// Immutable CPU inputs; neither FreeType nor the renderer needs the IO runtime
// after this owner is published. Atlas and face come from original UI loaders.
struct NativeHudSources {
    static constexpr const char* metadataFile = "hud.atlas.arpui";
    static constexpr std::size_t maxFileBytes = 2 * 1024 * 1024;
    static constexpr std::size_t maxSourceBytes = 4 * 1024 * 1024;
    static constexpr std::size_t maxDecodedBytes = 4 * 1024 * 1024;
    ui::UiFontFace face;
    core::SharedPtr<const ui::UiAtlasImageResourceData> atlas;
    bool valid() const noexcept;
};

class NativeHudSourceTask final {
public:
    NativeHudSourceTask(core::io::FileService&,
        core::runtime::CompletionQueue&,
        core::PoolHandle,
        core::runtime::CancellationToken = {});
    void advance() noexcept;
    void cancel() noexcept;
    StartupResourcePhase phase() const noexcept;
    std::string_view diagnostic() const noexcept;
    std::uint64_t readCount() const noexcept;
    core::SharedPtr<const NativeHudSources> sources() const noexcept;

private:
    StartupResourceTask task_;
};
} // namespace arp::build_a_queen
