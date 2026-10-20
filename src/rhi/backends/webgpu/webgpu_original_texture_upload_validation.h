#pragma once
#include "rhi/backends/webgpu/webgpu_command_inputs.h"

namespace arp::rhi_webgpu {
// Full positional original input binding only, without privileged native writes.
class WebGpuOriginalTextureUploadValidation final {
public:
    static const char* validate(const WebGpuGenericCommandProjection& graph,
        const rhi::TextureUploadOriginalInputArguments& arguments,
        std::size_t firstReference) noexcept
    {
        if (const auto* id = graph.validate())
            return id;
        const auto& input = arguments.originalInput();
        if (!input || !input->hasQuotaBoundSharedOwner(input))
            return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-INPUT";
        const auto& references = graph.references();
        if (firstReference > references.size() || references.size() - firstReference < 2u)
            return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-REFERENCE";
        using D = WebGpuCommandReferenceDomain;
        using S = WebGpuCommandReferenceSource;
        const auto& texture = references[firstReference];
        const auto& upload = references[firstReference + 1u];
        rhi::UploadTextureDesc fields;
        input->scalarFields().writeScalarFields(fields);
        if (texture.domain != D::Texture || upload.domain != D::Upload
            || texture.source != S::OriginalTextureUpload
            || upload.source != S::OriginalTextureUpload
            || texture.sourceIndex != upload.sourceIndex
            || upload.sourceIndex >= graph.originalTextureUploads().size()
            || texture.resource != fields.destinationTexture
            || upload.resource != fields.destinationTexture)
            return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-REFERENCE";
        const auto& source = graph.originalTextureUploads()[upload.sourceIndex];
        for (std::size_t i = 0u; i < source->packing()->mipLayouts().size(); ++i) {
            rhi::semantics::TextureUploadPackedCopy mapped;
            if (const auto* id = WebGpuOriginalTextureUploadSource::mapPackedCopyOriginal(
                    source, input, i, mapped))
                return id;
        }
        return nullptr;
    }
};
} // namespace arp::rhi_webgpu
