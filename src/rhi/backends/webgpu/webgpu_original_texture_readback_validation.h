#pragma once
#include "rhi/backends/webgpu/webgpu_command_inputs.h"

namespace arp::rhi_webgpu {
// Full positional original input/pair binding only. No privileged copy/map
// operation or current pending state proof is issued by this validator.
class WebGpuOriginalTextureReadbackValidation final {
public:
    static const char* validate(const WebGpuGenericCommandProjection& graph,
        const rhi::TextureReadbackOriginalInputArguments& arguments,
        std::size_t firstReference) noexcept
    {
        if (const auto* id = graph.validate())
            return id;
        const auto& input = arguments.originalInput();
        if (!input || !input->hasQuotaBoundSharedOwner(input))
            return "RHI-WEBGPU-ORIGINAL-READBACK-INPUT";
        const auto& references = graph.references();
        if (firstReference > references.size() || references.size() - firstReference < 2u)
            return "RHI-WEBGPU-ORIGINAL-READBACK-REFERENCE";
        using D = WebGpuCommandReferenceDomain;
        using S = WebGpuCommandReferenceSource;
        const auto& texture = references[firstReference];
        const auto& readback = references[firstReference + 1u];
        rhi::ReadbackTextureDesc fields;
        input->scalarFields().writeScalarFields(fields);
        if (texture.domain != D::Texture || readback.domain != D::Readback
            || texture.source != S::OriginalTextureReadback
            || readback.source != S::OriginalTextureReadback
            || texture.sourceIndex != readback.sourceIndex
            || readback.sourceIndex >= graph.originalReadbacks().size()
            || texture.resource != fields.sourceResource
            || readback.resource != fields.destinationReadbackBuffer)
            return "RHI-WEBGPU-ORIGINAL-READBACK-REFERENCE";
        const auto& pair = graph.originalReadbacks()[readback.sourceIndex];
        if (!core::SharedOwnerProjection::sameOwner(input, pair->layout()->inputOwner()))
            return "RHI-WEBGPU-ORIGINAL-READBACK-FOREIGN-INPUT";
        for (std::size_t i = 0u; i < pair->layout()->mipCopies().size(); ++i) {
            const auto& original = pair->layout()->mipCopies()[i];
            rhi::TextureReadbackLayout::Mip output;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            void* device = pair->destination()->nativeResource()->ownerDevice;
#else
            void* device = nullptr;
#endif
            if (const auto* id = WebGpuOriginalTextureReadbackPairProjection::mapMipOriginal(
                    pair, input, i, original, device, output))
                return id;
        }
        return nullptr;
    }
};
} // namespace arp::rhi_webgpu
