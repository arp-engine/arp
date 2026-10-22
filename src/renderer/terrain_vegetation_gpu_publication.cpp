#include "renderer/terrain_vegetation_gpu_publication.h"

namespace arp::renderer {

TerrainVegetationGpuPublication::TerrainVegetationGpuPublication(
    rhi::GpuDevice& device, const TerrainVegetationGpuUpload::Desc& desc)
    : device_(device), desc_(desc)
{}

TerrainVegetationGpuPublication::Result TerrainVegetationGpuPublication::stage(
    std::shared_ptr<const TerrainVegetationSnapshot> source)
{
    ready_ = false;
    staged_.reset();
    // Reclaim only on the cold side of the transaction, before native binding.
    displaced_.reset();
    if (!source || (published_ && published_->source()->source->equivalent(*source))) {
        if (source)
            staged_ = published_;
        ready_ = true;
        return {Status::Ready, {}};
    }
    try {
        if (upload_) {
            if (upload_->state() == TerrainVegetationGpuUpload::State::Rejected
                || upload_->state() == TerrainVegetationGpuUpload::State::Prepared) {
                upload_.reset();
                uploadingSource_.reset();
                return {Status::Failed, "VEGETATION-GPU-PUBLICATION-SUBMIT-FAILED"};
            }
            if (upload_->state() == TerrainVegetationGpuUpload::State::AcceptedError) {
                if (upload_->cancel()) {
                    upload_.reset();
                    uploadingSource_.reset();
                }
                return {Status::Failed, "VEGETATION-GPU-PUBLICATION-ACCEPTED-ERROR"};
            }
            const bool completed = upload_->poll();
            if (uploadingSource_->equivalent(*source)) {
                if (!completed)
                    return {Status::Pending, "VEGETATION-GPU-PUBLICATION-PENDING"};
                staged_ = upload_->snapshot();
                ready_ = true;
                return {Status::Ready, {}};
            }
            if (!completed)
                return {Status::Pending, "VEGETATION-GPU-PUBLICATION-BUSY"};
            upload_.reset();
            uploadingSource_.reset();
        }
        if (!desc_.maxCpuBytes)
            return {Status::Failed, "VEGETATION-GPU-BUDGET-INVALID"};
        auto quota = core::makeShared<core::CpuMemoryQuota>(desc_.maxCpuBytes);
        core::CpuMemoryQuotaScope scope(quota);
        TerrainVegetationDrawPreparation preparation;
        const auto prepared = preparation.prepare(source, {desc_.maxTiles, desc_.maxInstances});
        if (!prepared.success)
            return {Status::Failed, prepared.diagnostics};
        auto upload = core::makeUnique<TerrainVegetationGpuUpload>();
        const auto gpu = upload->prepareWithQuota(device_, preparation.snapshot(), desc_, quota);
        if (!gpu.success)
            return {Status::Failed, gpu.diagnostics};
        uploadingSource_ = std::move(source);
        upload_ = std::move(upload);
        if (upload_->state() == TerrainVegetationGpuUpload::State::Prepared) {
            const auto submitted = upload_->submit();
            if (!submitted.success) {
                if (upload_->cancel()) {
                    upload_.reset();
                    uploadingSource_.reset();
                }
                return {Status::Failed, submitted.diagnostics};
            }
        }
        if (!upload_->poll())
            return {Status::Pending, "VEGETATION-GPU-PUBLICATION-PENDING"};
        staged_ = upload_->snapshot();
        ready_ = true;
        return {Status::Ready, {}};
    } catch (const core::DiagnosticError& error) {
        return {Status::Failed, error.what()};
    } catch (const std::bad_alloc&) {
        return {Status::Failed, "VEGETATION-GPU-PUBLICATION-CPU-BUDGET"};
    }
}

std::shared_ptr<const TerrainVegetationSnapshot>
TerrainVegetationGpuPublication::preparedSource() const noexcept
{
    return ready_ && staged_ ? staged_->source()->source : nullptr;
}

void TerrainVegetationGpuPublication::publishPrepared() noexcept
{
    if (!ready_)
        rhi::RhiErrorHandler::get().fatal("VEGETATION-GPU-PUBLICATION-NOT-PREPARED");
    published_.swap(staged_);
    displaced_.swap(staged_);
    ready_ = false;
}

TerrainVegetationGpuUpload::Result TerrainVegetationGpuPublication::drainAndClear(
    const rhi::semantics::ShutdownDrainRequest& request)
{
    ready_ = false;
    if (upload_) {
        // Exact completed fences already authorize release, including accepted
        // errors. Only genuinely outstanding work needs a bounded device drain.
        if (!upload_->cancel()) {
            auto result = upload_->drainAndClear(request);
            if (!result.success)
                return result;
        }
        upload_.reset();
    }
    uploadingSource_.reset();
    staged_.reset();
    displaced_.reset();
    published_.reset();
    return {true, {}};
}

} // namespace arp::renderer
