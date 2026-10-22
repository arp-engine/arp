#pragma once

#include "core/memory/cpu_allocator.h"
#include "resource/resource_contract.h"
#include "resource/resource_manager.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

namespace arp::renderer::particles {

inline constexpr const char* kParticleTextureResourceType = "ParticleTextureResource";
inline constexpr const char* kParticleTextureResourceContract = "particle.texture/v1";

struct ParticleTextureUploadIntent {
    arp::core::pmr::String resourceUsage{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String uploadPolicy{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String requiredFormat{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String requiredDescriptorSet{&arp::core::defaultCpuMemoryResource()};
    bool backendOwnedUploadStaging = false;
};

struct ParticleTextureResourceData {
    arp::core::pmr::String textureResourceId{&arp::core::defaultCpuMemoryResource()};
    std::filesystem::path sourcePath;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    bool srgb = true;
    arp::core::pmr::Vector<std::uint8_t> rgba8{&arp::core::defaultCpuMemoryResource()};
    ParticleTextureUploadIntent intent;
    bool exposesNativeBackendHandles = false;

    bool valid() const noexcept
    {
        const auto expectedBytes =
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u;
        return !textureResourceId.empty() && width > 0u && height > 0u
            && expectedBytes == rgba8.size();
    }
};

struct ParticleTextureResourceLoadDesc {
    std::filesystem::path sourceRoot;
    std::filesystem::path sourcePath;
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    bool srgb = true;
    ParticleTextureUploadIntent intent;
};

struct ParticleTextureResource : public arp::resource::ResourceBase {
    ParticleTextureResource() = default;

    explicit ParticleTextureResource(ParticleTextureResourceLoadDesc desc)
        : arp::resource::ResourceBase(arp::resource::ResourceReference{
              desc.resourceId,
              arp::core::pmr::String(
                  kParticleTextureResourceType, &arp::core::defaultCpuMemoryResource()),
              desc.alias,
              desc.status,
          }),
          loadDesc(std::move(desc))
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& resourceManager,
        const arp::resource::ResourceLoadContext& context) override;
    bool collectFileReadIntents(arp::resource::ResourceManager& resourceManager,
        arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
        arp::core::pmr::String& diagnostics) const override;

    arp::resource::ResourceGraphStage processingStage() const noexcept override
    {
        return arp::resource::ResourceGraphStage::ImageDecode;
    }

    std::size_t processedByteCount() const noexcept override
    {
        return data ? data->rgba8.size() : 0u;
    }

    std::shared_ptr<ParticleTextureResourceData> data;
    ParticleTextureResourceLoadDesc loadDesc;
};

struct ParticleTextureResourceLoadResult {
    bool success = false;
    arp::resource::ResourceLoadResult resource;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ParticleTextureResourceLoadResult loadParticleTextureResource(
    arp::resource::ResourceManager& resourceManager, const ParticleTextureResourceLoadDesc& desc);

} // namespace arp::renderer::particles
