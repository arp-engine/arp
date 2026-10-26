#pragma once

#include "renderer/render_graph/render_graph.h"
#include "renderer/volumetric_cloud/cloud_contract.h"

namespace arp::renderer::cloud {
class CloudPreparedHistory;

namespace detail {

// Value member of the Feature: its observer address survives resource resize.
// The borrowed controller is detached before destruction. Epochs never reset
// when a replacement controller starts its own history generation at zero.
class CloudPreparedSubmission final : public render_graph::PassSubmissionObserver {
public:
    CloudPreparedSubmission() = default;
    CloudPreparedSubmission(const CloudPreparedSubmission&) = delete;
    CloudPreparedSubmission& operator=(const CloudPreparedSubmission&) = delete;
    CloudResult attach(CloudPreparedHistory&) noexcept;
    void detach() noexcept;
    CloudResult beginFrame() noexcept;
    CloudResult markContributed(render_graph::PassSubmissionToken) noexcept;
    void invalidateFrame() noexcept;
    render_graph::PassSubmissionToken token() const noexcept;

    const char* validateSubmission(render_graph::PassSubmissionToken) const noexcept override;
    void onSubmitted(render_graph::PassSubmissionToken,
        const std::shared_ptr<const rhi::GpuFence>&) noexcept override;

private:
    bool matches(render_graph::PassSubmissionToken) const noexcept;
    CloudPreparedHistory* history_ = nullptr;
    render_graph::PassSubmissionToken historyToken_{};
    std::uint64_t epoch_ = 0u;
    bool pending_ = false, contributed_ = false;
};
} // namespace detail
} // namespace arp::renderer::cloud
