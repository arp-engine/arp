#pragma once

#include "resource/prefab_asset_resources.h"
#include "rhi/frontend/original_texture_upload_device.h"

namespace arp::renderer {

// A renderer-owned immutable join. ResourceManager's unique object is an
// identity token only; its borrowed address is never dereferenced after capture.
class ARP_API ScenePbrTexturePublication final {
    class Key {
        friend class ScenePbrTexturePublication;
        Key() = default;
    };

public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxReferenceBytes = 0u, maxPixelBytes = 0u, maxCpuBytes = 0u;
        std::shared_ptr<core::CpuMemoryQuota> quota;
    };

    explicit ScenePbrTexturePublication(Key) {}

    ScenePbrTexturePublication(const ScenePbrTexturePublication&) = delete;
    ScenePbrTexturePublication& operator=(const ScenePbrTexturePublication&) = delete;

    static const char* create(const resource::ResourceManager&,
        std::string_view resourceId,
        const std::shared_ptr<const rhi::OriginalTextureUploadPublication>&,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>&,
        const Budget&,
        std::shared_ptr<const ScenePbrTexturePublication>&);
    const char* validate() const noexcept;
    const char* validateCurrent(const resource::ResourceManager&) const;

    const auto& resourceId() const noexcept { return id_; }

    const auto& view() const noexcept { return view_; }

    const auto& upload() const noexcept { return upload_; }

private:
    const char* compare(const resource::TextureResource&) const noexcept;
    Budget budget_;
    std::thread::id owner_;
    core::pmr::String id_, sourceId_, sourcePath_, usage_;
    const resource::TextureResource* identity_ = nullptr;
    resource::ResourceGeneration generation_ = 0u;
    std::uint64_t sourceGeneration_ = 0u;
    bool srgb_ = false, alpha_ = false;
    std::shared_ptr<const rhi::OriginalTextureUploadPublication> upload_;
    std::shared_ptr<const rhi::OriginalCommandTextureViewSource> view_;
};

} // namespace arp::renderer
