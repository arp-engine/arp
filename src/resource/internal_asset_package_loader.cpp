#include "resource/internal_asset_package_loader.h"

#include "core/io/filesystem_path.h"
#include "resource/asset_package_closure.h"
#include "resource/asset_package_payload.h"
#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::resource {
namespace {
using core::io::FileIoStatus;

class PackageLoad final {
public:
    ResourceManager* manager = nullptr;
    core::io::FileService* files = nullptr;
    AssetPackageLoadRequest request;
    AssetPackageLoadCompletion completion;
    AssetPackageLoadResult result;
    ResourceSourceSet sources;
    core::pmr::Vector<ResourceFileReadIntent> reads{&core::defaultCpuMemoryResource()};
    core::io::FileReadRequest locationCheck;
    std::size_t readIndex = 0;
    bool terminal = false;

    void fail(FileIoStatus status, const char* diagnostic) noexcept
    {
        result.resource.success = false;
        result.resource.state = ResourceState::Failed;
        result.fileStatus = status;
        result.diagnosticId = diagnostic;
        terminal = true;
    }

    static bool admission(void* context) noexcept
    {
        auto& job = *static_cast<PackageLoad*>(context);
        if (job.request.cancellation.cancelled()) {
            job.fail(FileIoStatus::Cancelled, "PACKAGE-IO-CANCELLED");
            return false;
        }
        try {
            const auto checked = job.files->validateRead(job.locationCheck);
            if (checked.status != FileIoStatus::Success) {
                job.fail(checked.status,
                    checked.diagnosticId ? checked.diagnosticId : "PACKAGE-IO-LOCATION-INVALID");
                return false;
            }
        } catch (...) {
            job.fail(FileIoStatus::BudgetExceeded, "PACKAGE-IO-LOCATION-ALLOCATION");
            return false;
        }
        return true;
    }

    AssetPackageSourceContext context()
    {
        AssetPackageSourceContext context;
        context.origin = request.origin;
        context.resources.policy = request.policy;
        context.resources.sources = &sources;
        context.resources.requirePreloadedSources = true;
        context.resources.maxDecodedBytes = request.budget.maxDecodedBytes;
        context.resources.commitGate = {admission, this};
        return context;
    }

    static void finish(PackageLoad* pointer) noexcept
    {
        core::UniquePtr<PackageLoad> job{pointer,
            core::CpuAllocatedDeleter<PackageLoad>{
                pointer, &core::destroyCpuAllocatedUnique<PackageLoad>}};
        job->result.telemetry.completions = 1;
        job->completion.complete(job->completion.userData, std::move(job->result));
    }

    template <class Function> static void dispatch(PackageLoad* job, Function&& function) noexcept
    {
        try {
            function();
        } catch (const std::bad_alloc&) {
            job->fail(FileIoStatus::BudgetExceeded, "PACKAGE-IO-ALLOCATION");
        } catch (...) {
            job->fail(FileIoStatus::BackendUnavailable, "PACKAGE-IO-EXECUTION");
        }
        if (job->terminal)
            finish(job);
    }

    void nextRead()
    {
        if (!admission(this))
            return;
        if (result.telemetry.submittedReads >= request.budget.maxFileReads
            || result.telemetry.bytesRead >= request.budget.maxTotalBytes) {
            fail(FileIoStatus::BudgetExceeded, "PACKAGE-IO-READ-BUDGET");
            return;
        }
        const auto& intent = reads[readIndex];
        core::io::FileReadRequest read;
        read.contractVersion = 1;
        read.path = intent.path;
        read.pathScope = intent.pathScope;
        read.mount = intent.mount;
        read.expectedVersion = intent.expectedVersion;
        read.maxVersionBytes = intent.maxVersionBytes;
        read.maxBytes = std::min<std::uint64_t>(
            intent.maxBytes, request.budget.maxTotalBytes - result.telemetry.bytesRead);
        read.cancellation = request.cancellation;
        const auto status = files->read(read, {completeRead, this});
        if (status != FileIoStatus::Accepted)
            fail(status, "PACKAGE-IO-READ-SUBMISSION");
        else
            ++result.telemetry.submittedReads;
    }

    void plan()
    {
        if (!admission(this))
            return;
        ++result.telemetry.plans;
        auto planned = InternalAssetPackageReader().planSourceReads(
            request.directory, context(), request.budget, request.codecs);
        if (!planned.success) {
            result.resource.diagnostics = std::move(planned.diagnostics);
            fail(FileIoStatus::InvalidRequest, "PACKAGE-IO-PLAN");
            return;
        }
        reads = std::move(planned.reads);
        for (auto& intent : reads) {
            if (intent.pathScope != request.origin.pathScope
                || !(intent.mount == request.origin.mount)) {
                fail(FileIoStatus::InvalidRequest, "PACKAGE-IO-PLAN-ORIGIN");
                return;
            }
            intent.maxBytes =
                std::min(intent.maxBytes ? intent.maxBytes : request.budget.maxFileBytes,
                    request.budget.maxFileBytes);
        }
        const auto pending = pendingResourceReads(reads, sources, request.budget.maxFileBytes);
        if (pending.status != ResourceSourcePlanStatus::Valid) {
            fail(FileIoStatus::InvalidRequest, "PACKAGE-IO-PLAN-CONFLICT");
            return;
        }
        if (reads.size() > request.budget.maxFileReads - result.telemetry.submittedReads) {
            fail(FileIoStatus::BudgetExceeded, "PACKAGE-IO-READ-COUNT");
            return;
        }
        if (!reads.empty()) {
            readIndex = 0;
            nextRead();
            return;
        }
        if (!admission(this))
            return;
        result.resource = InternalAssetPackageReader().loadSources(
            request.directory, *manager, context(), request.codecs);
        result.telemetry.committed = result.resource.success;
        if (!result.resource.success && !result.diagnosticId)
            result.diagnosticId = "PACKAGE-IO-PREPARATION-OR-ADMISSION";
        terminal = true;
    }

    static void start(void* context) noexcept
    {
        auto* job = static_cast<PackageLoad*>(context);
        dispatch(job, [&] {
            job->plan();
        });
    }

    static void completeRead(void* context, core::io::FileReadResult read) noexcept
    {
        auto* job = static_cast<PackageLoad*>(context);
        dispatch(job, [&] {
            if (!admission(job))
                return;
            if (read.status != FileIoStatus::Success) {
                job->result.resource.diagnostics = std::move(read.diagnostics);
                job->fail(
                    read.status, read.diagnosticId ? read.diagnosticId : "PACKAGE-IO-READ-FAILED");
                return;
            }
            if (read.bytes.size()
                    > job->request.budget.maxTotalBytes - job->result.telemetry.bytesRead
                || read.bytes.size() > job->reads[job->readIndex].maxBytes) {
                job->fail(FileIoStatus::BudgetExceeded, "PACKAGE-IO-SOURCE-BYTES");
                return;
            }
            const auto& intent = job->reads[job->readIndex];
            ResourceSourceBuffer source;
            source.path = intent.path;
            source.role = intent.role;
            source.pathScope = intent.pathScope;
            source.mount = intent.mount;
            source.version = read.version;
            source.bytes = std::move(read.bytes);
            job->result.telemetry.bytesRead += source.bytes.size();
            if (!job->sources.add(std::move(source))) {
                job->fail(FileIoStatus::InvalidRequest, "PACKAGE-IO-SOURCE-AMBIGUOUS");
                return;
            }
            if (++job->readIndex < job->reads.size())
                job->nextRead();
            else
                job->plan();
        });
    }
};
} // namespace

InternalAssetPackageLoader::InternalAssetPackageLoader(ResourceManager& manager,
    core::io::FileService& files,
    core::runtime::CompletionQueue& completions) noexcept
    : manager_(manager), files_(files), completions_(completions)
{}

core::io::FileIoStatus InternalAssetPackageLoader::submit(
    AssetPackageLoadRequest request, AssetPackageLoadCompletion completion) noexcept
{
    if (!completion.complete)
        return FileIoStatus::InvalidRequest;
    if (completions_.closed())
        return FileIoStatus::ServiceClosed;
    for (auto value : {request.budget.maxMetadataBytes,
             request.budget.maxResources,
             request.budget.maxFileReads,
             request.budget.maxFileBytes,
             request.budget.maxTotalBytes,
             request.budget.maxDecodedBytes})
        if (!value || value == SIZE_MAX)
            return FileIoStatus::InvalidRequest;
    if (request.budget.maxMetadataBytes > AssetPackageClosureService::maxMetadataBytes
        || request.budget.maxMetadataBytes > request.budget.maxFileBytes
        || request.budget.maxResources > AssetPackageClosureService::maxResources
        || (request.codecs && !request.codecs->frozen())
        || (request.origin.pathScope == core::io::FilePathScope::MountedResource
            && files_.capability().kind != core::io::FileBackendKind::MountedFileBackend))
        return FileIoStatus::InvalidRequest;
    try {
        std::filesystem::path manifest;
        if (!resolveResourceSourcePath(
                request.directory, "manifest.arpkg.yaml", request.origin, manifest))
            return FileIoStatus::InvalidRequest;
        auto job = core::makeUnique<PackageLoad>();
        job->request = std::move(request);
        job->manager = &manager_;
        job->files = &files_;
        job->completion = completion;
        job->locationCheck.contractVersion = 1;
        job->locationCheck.path = core::FilePath::utf8(manifest);
        job->locationCheck.pathScope = job->request.origin.pathScope;
        job->locationCheck.mount = job->request.origin.mount;
        job->locationCheck.maxBytes = job->request.budget.maxMetadataBytes;
        auto reservation = completions_.reserve();
        if (!reservation.valid())
            return reservation.status() == core::runtime::CompletionStatus::QueueFull
                ? FileIoStatus::QueueFull
                : FileIoStatus::ServiceClosed;
        const auto posted =
            completions_.postReserved(std::move(reservation), {PackageLoad::start, job.get()});
        if (!posted.accepted())
            return FileIoStatus::ServiceClosed;
        job.release();
        return FileIoStatus::Accepted;
    } catch (const std::bad_alloc&) {
        return FileIoStatus::BudgetExceeded;
    } catch (...) {
        return FileIoStatus::InvalidRequest;
    }
}

} // namespace arp::resource
