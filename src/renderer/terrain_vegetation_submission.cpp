#include "renderer/terrain_vegetation_submission.h"

namespace arp::renderer {

TerrainVegetationSubmission::~TerrainVegetationSubmission()
{
    if (state_ == State::Attempted
        || (state_ == State::Accepted && (!completion_ || !completion_->isSignaled())))
        rhi::RhiErrorHandler::get().fatal("VEGETATION-SUBMISSION-DESTROY-BEFORE-COMPLETION");
}

TerrainVegetationSubmission::Result TerrainVegetationSubmission::prepare(
    std::shared_ptr<const TerrainVegetationGpuSnapshot> source, const Desc& desc)
{
    if (state_ != State::Empty)
        return {false, "VEGETATION-SUBMISSION-NOT-EMPTY"};
    if (desc.contractVersion != 1u || !desc.maxCpuBytes || !desc.liveSubmissions)
        return {false, "VEGETATION-SUBMISSION-BUDGET-INVALID"};
    if (!source || !source->belongsTo(device_))
        return {false, "VEGETATION-SUBMISSION-DEVICE-MISMATCH"};
    const auto decision =
        TerrainVegetationGpuUpload::plan(device_.capabilities(), desc.validatorOptIn);
    if (!decision.executable || source->validatorOnly() != decision.validatorOnly)
        return {false, "VEGETATION-SUBMISSION-PROFILE-UNAVAILABLE"};
    try {
        auto quota = core::makeShared<core::CpuMemoryQuota>(desc.maxCpuBytes);
        core::CpuMemoryQuotaScope scope(quota);
        auto lease = core::makeShared<core::AtomicCapacityLease>(desc.liveSubmissions, 1u);
        if (!lease->charged())
            return {false, "VEGETATION-SUBMISSION-CAPACITY"};
        submissionLease_ = std::move(lease);
        snapshot_ = std::move(source);
        state_ = State::Prepared;
        return {true, {}};
    } catch (const std::bad_alloc&) {
        return {false, "VEGETATION-SUBMISSION-CPU-BUDGET"};
    }
}

render_graph::PassSubmissionToken TerrainVegetationSubmission::token() const noexcept
{
    return {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)), 1u};
}

bool TerrainVegetationSubmission::matches(render_graph::PassSubmissionToken value) const noexcept
{
    const auto expected = token();
    return value.ownerId == expected.ownerId && value.generation == expected.generation;
}

const char* TerrainVegetationSubmission::validateSubmission(
    render_graph::PassSubmissionToken value) const noexcept
{
    return matches(value) && (state_ == State::Prepared || state_ == State::Attempted)
        ? nullptr
        : "VEGETATION-SUBMISSION-TOKEN-STALE";
}

const char* TerrainVegetationSubmission::beginUse(
    render_graph::RenderContext& context, render_graph::PassSubmissionToken value) noexcept
{
    if (const auto* diagnostic = validateSubmission(value))
        return diagnostic;
    if (&context.device() != &device_)
        return "VEGETATION-SUBMISSION-CONSUMER-DEVICE-MISMATCH";
    const auto identity = context.commands().recordingIdentity();
    if (!identity)
        return "VEGETATION-SUBMISSION-RECORDING-IDENTITY-UNAVAILABLE";
    if (recordingIdentity_ && recordingIdentity_ != identity)
        return "VEGETATION-SUBMISSION-RECORDING-REPLAY";
    recordingIdentity_ = identity;
    state_ = State::Attempted;
    return nullptr;
}

void TerrainVegetationSubmission::onSubmitted(render_graph::PassSubmissionToken value,
    const std::shared_ptr<const rhi::GpuFence>& completion) noexcept
{
    if (!matches(value) || !completion || (state_ != State::Attempted && state_ != State::Accepted)
        || (state_ == State::Accepted
            && (completion_ != completion || completion_.owner_before(completion)
                || completion.owner_before(completion_))))
        rhi::RhiErrorHandler::get().fatal("VEGETATION-SUBMISSION-COMPLETION-CONFLICT");
    completion_ = completion;
    state_ = State::Accepted;
}

void TerrainVegetationSubmission::release(State state) noexcept
{
    snapshot_.reset();
    completion_.reset();
    submissionLease_.reset();
    state_ = state;
}

bool TerrainVegetationSubmission::poll() noexcept
{
    if (state_ == State::Completed)
        return true;
    if (state_ != State::Accepted || !completion_ || !completion_->isSignaled())
        return false;
    release(State::Completed);
    return true;
}

bool TerrainVegetationSubmission::cancel() noexcept
{
    if (state_ == State::Cancelled || state_ == State::Empty)
        return true;
    if (state_ != State::Prepared)
        return false;
    release(State::Cancelled);
    return true;
}

TerrainVegetationSubmission::Result TerrainVegetationSubmission::drainAndClear(
    const rhi::semantics::ShutdownDrainRequest& request)
{
    if (poll() || cancel())
        return {true, {}};
    const auto report = device_.drainForShutdown(request);
    if (!report.completed())
        return {false, report.diagnosticId};
    release(State::Completed);
    return {true, {}};
}

} // namespace arp::renderer
