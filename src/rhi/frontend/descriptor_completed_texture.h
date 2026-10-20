#pragma once
#include "rhi/gpu_resources.h"
#include "rhi/semantics/descriptor_lowering.h"

namespace arp::rhi {
class OriginalCommandTextureViewSource;
class OriginalCompletedDepthTextureSource;

// Cold descriptor publication only. Shader execution and state transitions
// still require the original inventory, graph, seal and completion journal.
struct OriginalCompletedTextureDescriptorRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxNativeImageViews = 0u, maxNativeWrites = 0u;
    std::uint32_t maxRetainedSourceOwners = 0u;
};

class ARP_API OriginalCompletedTextureDescriptorContract final {
public:
    struct Decision {
        bool validatorOnly = false, publicationAvailable = false;
        bool shaderExecutionAvailable = false;
        const char* diagnostic = "RHI-COMPLETED-TEXTURE-DESCRIPTOR-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::DescriptorLowering::OriginalCompletedTextureDescriptor_lowering<Decision>(
            backend, version);
    }

    static const char* validateIntent(const OriginalCompletedTextureDescriptorRequest&) noexcept;

    // Publication1 consumes queried-allocation upload control3; independent
    // WebGPU publication2 consumes canonical whole-upload control1. Neither
    // profile grants content completion, shader execution or a state change.
    // Publication3 consumes a typed completed depth cell source, never a whole
    // ContentExpectation; it has its own overload and the same execution boundary.
    // Independent Publication4 consumes Source2, one native control4 depth cell.
    // Publication5 adds RGBA16F color to control3 whole-view publication.
    // It retains its version in native storage; Publication1 remains RGBA8.
    // Publication6 independently admits canonical control1 whole RGBA8/RGBA16F
    // 2D/2DArray/single Cube. Publication2 keeps its old RGBA8 2D/array scope.
    static constexpr const char* validateControl(
        std::uint32_t controlVersion, std::uint32_t publicationVersion = 1u) noexcept
    {
        return ((publicationVersion == 1u || publicationVersion == 5u) && controlVersion == 3u)
                || ((publicationVersion == 2u || publicationVersion == 6u) && controlVersion == 1u)
            ? nullptr
            : "RHI-COMPLETED-TEXTURE-DESCRIPTOR-CONTENT";
    }

    // Value-only shape check; never authenticates a Source, content or owner.
    static const char* validateView(
        const TextureDesc&, const TextureViewDesc&, std::uint32_t publicationVersion) noexcept;

    static const char* validateSource(
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentExpectation&) noexcept;
    static const char* validateSource(
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentExpectation&,
        std::uint32_t publicationVersion) noexcept;
    static const char* validateSource(
        const std::shared_ptr<const OriginalCompletedDepthTextureSource>&,
        std::uint32_t publicationVersion) noexcept;
};
} // namespace arp::rhi
