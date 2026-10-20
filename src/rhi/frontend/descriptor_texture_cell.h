#pragma once
#include "rhi/gpu_resources.h"
#include "rhi/semantics/descriptor_lowering.h"

namespace arp::rhi {
class OriginalCommandTextureViewSource;
struct DescriptorBindingDesc;
struct DescriptorBindingWrite;

// Cold immutable descriptor metadata, independently opted in. A range receipt
// authenticates control/resource identity and range only: its captured content
// state and revision grant no initialization, mutation or completion authority.
// Those remain exclusively with the original content plan and journal.
// /3 (Native control6) and /4 (Emulated control7) publish a read-only full
// mip-chain identity for ProducedImageRead/1; they grant no completed contents.
struct OriginalTextureCellDescriptorRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxNativeImageViews = 0u, maxNativeWrites = 0u;
    std::uint32_t maxRetainedSourceOwners = 0u;
};

class ARP_API OriginalTextureCellDescriptorContract final {
public:
    struct Decision {
        bool validatorOnly = false, publicationAvailable = false;
        bool shaderExecutionAvailable = false;
        const char* diagnostic = "RHI-TEXTURE-CELL-DESCRIPTOR-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        return semantics::DescriptorLowering::OriginalTextureCellDescriptor_lowering<Decision>(
            backend, version);
    }

    static constexpr std::uint32_t controlVersion(std::uint32_t version) noexcept
    {
        return version == 1u || version == 3u ? 6u : version == 2u || version == 4u ? 7u : 0u;
    }

    static constexpr bool isProducedImageRead(std::uint32_t version) noexcept
    {
        return version == 3u || version == 4u;
    }

    static const char* validateIntent(const OriginalTextureCellDescriptorRequest&) noexcept;
    static const char* validateSource(
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentRangeExpectation&,
        std::uint32_t version) noexcept;
    static const char* validateBinding(const DescriptorBindingDesc&,
        const DescriptorBindingWrite&,
        std::uint32_t version = 1u) noexcept;
};
} // namespace arp::rhi
