#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "resource/resource_manager.h"
#include "rhi/gpu_device.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace arp::editor {

struct EditorSceneViewportTextureCacheBudget {
    std::size_t maxResidentTextures = 0;
    std::uint64_t maxResidentDecodedBytes = 0;
    std::uint64_t maxUploadStagingBytes = 0;
};

enum class EditorSceneViewportTextureColorSpace : std::uint8_t { Srgb, Linear };

enum class EditorSceneViewportTextureCacheDiagnostic : std::uint8_t {
    None,
    InvalidBudget,
    InvalidIdentity,
    OwnerMismatch,
    ResourceChanged,
    InvalidDecodedData,
    ColorSpaceMismatch,
    BudgetExceeded,
    TextureCreateFailed,
    ViewCreateFailed,
    SamplerCreateFailed,
    UploadFailed,
};

struct EditorSceneViewportTextureCacheResult {
    EditorSceneViewportTextureCacheDiagnostic diagnostic =
        EditorSceneViewportTextureCacheDiagnostic::None;
    core::pmr::String detail{&core::defaultCpuMemoryResource()};
    rhi::GpuTexture* texture = nullptr;
    rhi::GpuTextureView* view = nullptr;
    rhi::GpuSampler* sampler = nullptr;
    bool reused = false;

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportTextureCacheDiagnostic::None && texture != nullptr
            && view != nullptr && sampler != nullptr;
    }
};

// Owns uploaded texture bindings for one device and resource manager.
// Destroy after submitted work using these bindings completes and before the device.
class EditorSceneViewportTextureCache final {
public:
    explicit EditorSceneViewportTextureCache(EditorSceneViewportTextureCacheBudget budget);
    EditorSceneViewportTextureCache(const EditorSceneViewportTextureCache&) = delete;
    EditorSceneViewportTextureCache& operator=(const EditorSceneViewportTextureCache&) = delete;

    EditorSceneViewportTextureCacheResult acquire(rhi::GpuDevice& device,
        const resource::ResourceManager& resources,
        std::string_view resourceId,
        resource::ResourceGeneration generation,
        EditorSceneViewportTextureColorSpace colorSpace);
    std::size_t residentTextureCount() const noexcept;
    std::uint64_t residentDecodedBytes() const noexcept;

private:
    struct Entry {
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
        resource::ResourceGeneration generation = 0;
        bool srgb = false;
        std::uint64_t decodedBytes = 0;
        core::UniquePtr<rhi::GpuTexture> texture;
        core::UniquePtr<rhi::GpuTextureView> view;
    };

    EditorSceneViewportTextureCacheBudget budget_;
    const rhi::GpuDevice* ownerDevice_ = nullptr;
    const resource::ResourceManager* ownerResources_ = nullptr;
    core::UniquePtr<rhi::GpuSampler> sampler_;
    core::pmr::Vector<core::UniquePtr<Entry>> entries_{&core::defaultCpuMemoryResource()};
    std::uint64_t residentDecodedBytes_ = 0;
};

ARP_API const char* editorSceneViewportTextureCacheDiagnosticCode(
    EditorSceneViewportTextureCacheDiagnostic diagnostic) noexcept;

} // namespace arp::editor
