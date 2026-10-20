#pragma once
#include "rhi/backends/webgpu/webgpu_command_inputs.h"
#include "rhi/backends/webgpu/webgpu_transfer_copy_validation.h"
#include "rhi/frontend/transfer_upload_input.h"

namespace arp::rhi_webgpu {
// Exact source/descriptor/positional metadata binding. No native authority.
class WebGpuOriginalBufferUploadValidation final {
public:
    static const char* validate(const WebGpuGenericCommandProjection& graph,
        const rhi::BufferUploadArguments& arguments,
        std::size_t firstReference) noexcept
    {
        if (const auto* id = graph.validate())
            return id;
        const auto& references = graph.references();
        if (firstReference > references.size() || references.size() - firstReference < 3u)
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-REFERENCE";
        using D = WebGpuCommandReferenceDomain;
        using S = WebGpuCommandReferenceSource;
        const auto& source = references[firstReference];
        const auto& destination = references[firstReference + 1u];
        const auto& upload = references[firstReference + 2u];
        if (source.domain != D::Buffer || destination.domain != D::Buffer
            || upload.domain != D::Upload || source.source != S::Transfer
            || destination.source != S::Transfer || upload.source != S::OriginalBufferUpload
            || source.sourceIndex || destination.sourceIndex
            || source.resource != arguments.source()
            || destination.resource != arguments.destination()
            || upload.resource != arguments.source()
            || upload.sourceIndex >= graph.originalUploads().size())
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-REFERENCE";
        const auto& original = *graph.originalUploads()[upload.sourceIndex];
        if (!arguments.backendOwnedStaging()
            || arguments.stagingIntent() != rhi::StagingIntent::Upload)
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-STAGING-INTENT";
        rhi::BufferCopyDesc copy;
        if (!arguments.copyArguments().write(copy))
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-KIND";
        if (copy.sizeBytes != original.sizeBytes()
            || arguments.frameSerial() != original.frameSerial())
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-SOURCE-FACTS";
        // Graph validation has already proved the exact source capsule/Transfer
        // native payload, allocation owner and readiness join.
        return WebGpuTransferCopyValidation::validate(*graph.transfer(), arguments.copyArguments());
    }
};
} // namespace arp::rhi_webgpu
