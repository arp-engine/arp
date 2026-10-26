#pragma once

#include "renderer/volumetric_cloud/cloud_prepared_resources.h"

namespace arp::renderer::cloud {

// Both descriptions must name the same device, selection and resource request.
// State, pipeline, resource, initialization, history and Source quotas remain
// independently authored. The feature never creates an inventory or executor.
struct CloudPreparedFeatureCreateContext {
    const CloudPreparedPipelineCreateContext& pipelines;
    const CloudPreparedResourceCreateContext& resources;
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCpuStateBytes = 0u;
    std::shared_ptr<core::CpuMemoryQuota> stateQuota;
};

// Attachments and typed positions belong to the caller's existing original
// execution. All cloud buffers, plan and history are taken from this feature's
// pending immutable generation; repeated contributions refuse before mutation.
struct CloudPreparedFeatureGraphContext {
    render_graph::OriginalFrameGraphExecution& execution;
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    // Feature epoch, distinct from the replaceable controller's generation.
    render_graph::PassSubmissionToken frameToken;
    render_graph::TextureHandle color, depth, output;
    std::array<std::size_t, kCloudPipelineCount> pipelines{SIZE_MAX, SIZE_MAX, SIZE_MAX};
    std::array<std::size_t, kCloudPipelineCount> descriptors{SIZE_MAX, SIZE_MAX, SIZE_MAX};
};

} // namespace arp::renderer::cloud
