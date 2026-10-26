#include "renderer/volumetric_cloud/detail/cloud_prepared_submission.h"

#include "renderer/volumetric_cloud/cloud_prepared_history.h"

namespace arp::renderer::cloud::detail {

CloudResult CloudPreparedSubmission::attach(CloudPreparedHistory& history) noexcept
{
    if (history_ || !history.token().ownerId)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-OBSERVER-ATTACH");
    history_ = &history;
    invalidateFrame();
    return {};
}

void CloudPreparedSubmission::detach() noexcept
{
    invalidateFrame();
    history_ = nullptr;
}

void CloudPreparedSubmission::invalidateFrame() noexcept
{
    ++epoch_;
    pending_ = contributed_ = false;
    historyToken_ = {};
}

CloudResult CloudPreparedSubmission::beginFrame() noexcept
{
    if (!history_ || !history_->raw() || pending_)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-OBSERVER-FRAME");
    invalidateFrame();
    historyToken_ = history_->token();
    pending_ = true;
    return {};
}

render_graph::PassSubmissionToken CloudPreparedSubmission::token() const noexcept
{
    return pending_ ? render_graph::PassSubmissionToken{historyToken_.ownerId, epoch_}
                    : render_graph::PassSubmissionToken{};
}

bool CloudPreparedSubmission::matches(render_graph::PassSubmissionToken value) const noexcept
{
    return history_ && pending_ && value.ownerId == historyToken_.ownerId
        && value.generation == epoch_;
}

CloudResult CloudPreparedSubmission::markContributed(
    render_graph::PassSubmissionToken value) noexcept
{
    if (!matches(value) || contributed_)
        return CloudResult::failure("CLOUD-PREPARED-FEATURE-OBSERVER-CONTRIBUTION");
    contributed_ = true;
    return {};
}

const char* CloudPreparedSubmission::validateSubmission(
    render_graph::PassSubmissionToken value) const noexcept
{
    if (!matches(value) || !contributed_)
        return "CLOUD-STALE-SUBMISSION";
    // Forward only the exact pending controller generation recorded at prepare.
    // A stale external token never reaches or dereferences a detached controller.
    return history_->validateSubmission(historyToken_);
}

void CloudPreparedSubmission::onSubmitted(render_graph::PassSubmissionToken value,
    const std::shared_ptr<const rhi::GpuFence>& completion) noexcept
{
    if (validateSubmission(value))
        return;
    history_->onSubmitted(historyToken_, completion);
    if (!history_->raw())
        invalidateFrame();
}

} // namespace arp::renderer::cloud::detail
