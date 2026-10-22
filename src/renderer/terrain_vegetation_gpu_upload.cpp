#include "renderer/terrain_vegetation_gpu_upload.h"

#include <algorithm>
#include <cstring>

namespace arp::renderer {
namespace {

bool sameRecords(const TerrainVegetationDrawSnapshot& a, const TerrainVegetationDrawSnapshot& b)
{
    if (a.tiles.size() != b.tiles.size())
        return false;
    for (std::size_t i = 0u; i < a.tiles.size(); ++i) {
        const auto& x = a.tiles[i];
        const auto& y = b.tiles[i];
        if (!(x.coord == y.coord) || x.prototypes.size() != y.prototypes.size())
            return false;
        for (std::size_t j = 0u; j < x.prototypes.size(); ++j) {
            const auto& p = x.prototypes[j];
            const auto& q = y.prototypes[j];
            if (p.prototypeId != q.prototypeId || p.instances.size() != q.instances.size()
                || (!p.instances.empty()
                    && std::memcmp(p.instances.data(),
                        q.instances.data(),
                        p.instances.size() * sizeof(vegetation::InstanceDataGpu))))
                return false;
        }
    }
    return true;
}

} // namespace

TerrainVegetationGpuUpload::Decision TerrainVegetationGpuUpload::plan(
    const rhi::DeviceCapabilities& caps, bool validatorOptIn) noexcept
{
    Decision decision;
    switch (caps.backend) {
    case rhi::Backend::Null:
        decision.validatorOnly = true;
        if (!validatorOptIn) {
            decision.diagnostic = "VEGETATION-GPU-VALIDATOR-OPT-IN-REQUIRED";
            return decision;
        }
        break;
    case rhi::Backend::Vulkan:
    case rhi::Backend::Metal:
    case rhi::Backend::WebGPU:
        break;
    default:
        return decision;
    }
    decision.mode = rhi::CapabilityMode::Degraded;
    decision.diagnostic = "VEGETATION-GPU-CAPABILITY-UNAVAILABLE";
    if (!caps.core.graphics || !caps.core.compute || !caps.features.computeDispatch
        || !caps.memory.deviceLocal || !caps.memory.hostVisible
        || !caps.explicitFeatures.explicitBarriers || !caps.features.storageBufferBinding
        || (caps.computeResources.mode != rhi::CapabilityMode::Native
            && caps.computeResources.mode != rhi::CapabilityMode::Equivalent)
        || !caps.computeResources.maxStorageBufferBindingsPerStage
        || !caps.computeResources.maxStorageBufferBindingSizeBytes)
        return decision;
    decision.mode =
        decision.validatorOnly ? rhi::CapabilityMode::Equivalent : caps.computeResources.mode;
    decision.executable = true;
    decision.maxBufferBytes = caps.computeResources.maxStorageBufferBindingSizeBytes;
    decision.diagnostic = decision.validatorOnly ? "VEGETATION-GPU-VALIDATOR-ONLY"
                                                 : "VEGETATION-GPU-UPLOAD-AVAILABLE";
    return decision;
}

TerrainVegetationGpuUpload::~TerrainVegetationGpuUpload()
{
    if (outstanding() && (!completion_ || !completion_->isSignaled()))
        rhi::RhiErrorHandler::get().fatal("VEGETATION-GPU-DESTROY-BEFORE-COMPLETION");
}

bool TerrainVegetationGpuUpload::outstanding() const noexcept
{
    return state_ == State::AcceptedPending || state_ == State::AcceptedError;
}

TerrainVegetationGpuUpload::Result TerrainVegetationGpuUpload::prepare(rhi::GpuDevice& device,
    std::shared_ptr<const TerrainVegetationDrawSnapshot> source,
    const Desc& desc)
{
    return prepareWithQuota(device, std::move(source), desc, nullptr);
}

TerrainVegetationGpuUpload::Result TerrainVegetationGpuUpload::prepareWithQuota(
    rhi::GpuDevice& device,
    std::shared_ptr<const TerrainVegetationDrawSnapshot> source,
    const Desc& desc,
    std::shared_ptr<core::CpuMemoryQuota> quota)
{
    if (state_ != State::Empty)
        return {false, "VEGETATION-GPU-TICKET-NOT-EMPTY"};
    if (desc.contractVersion != 1u || !desc.maxTiles || !desc.maxInstances || !desc.maxPrototypes
        || !desc.maxCopyBytes || desc.maxCopyBytes % 4u || !desc.maxCopyCommands
        || !desc.maxCpuBytes || !desc.liveGenerations || !desc.liveInstances)
        return {false, "VEGETATION-GPU-BUDGET-INVALID"};
    decision_ = plan(device.capabilities(), desc.validatorOptIn);
    if (!decision_.executable)
        return {false, decision_.diagnostic};
    if (!source || !source->source || source->tiles.size() > desc.maxTiles
        || source->source->instanceCount > desc.maxInstances)
        return {false, "VEGETATION-GPU-SOURCE-INVALID"};
    std::size_t groups = 0u;
    std::uint64_t copies = 0u;
    for (const auto& tile : source->tiles) {
        if (tile.prototypes.size() > desc.maxPrototypes - groups)
            return {false, "VEGETATION-GPU-PROTOTYPE-BUDGET"};
        groups += tile.prototypes.size();
        for (const auto& prototype : tile.prototypes) {
            if (prototype.instances.empty() || prototype.instances.size() > UINT32_MAX
                || prototype.instances.size()
                    > decision_.maxBufferBytes / sizeof(vegetation::InstanceDataGpu))
                return {false, "VEGETATION-GPU-BUFFER-RANGE"};
            const auto bytes =
                std::uint64_t(prototype.instances.size()) * sizeof(vegetation::InstanceDataGpu);
            const auto count = bytes / desc.maxCopyBytes + (bytes % desc.maxCopyBytes != 0u);
            if (count > desc.maxCopyCommands - copies)
                return {false, "VEGETATION-GPU-COPY-BUDGET"};
            copies += count;
        }
    }
    try {
        if (!quota)
            quota = core::makeShared<core::CpuMemoryQuota>(desc.maxCpuBytes);
        core::CpuMemoryQuotaScope scope(quota);
        // Public input structs are not an authority to bypass world/ABI validation.
        TerrainVegetationDrawPreparation verifier;
        const auto verified = verifier.prepare(source->source, {desc.maxTiles, desc.maxInstances});
        if (!verified.success)
            return {false, verified.diagnostics};
        if (!sameRecords(*source, *verifier.snapshot()))
            return {false, "VEGETATION-GPU-RECORDS-MISMATCH"};
        auto candidate = core::makeShared<TerrainVegetationGpuSnapshot>();
        candidate->generationLease_ =
            core::makeShared<core::AtomicCapacityLease>(desc.liveGenerations, 1u);
        if (!candidate->generationLease_->charged())
            return {false, "VEGETATION-GPU-LIVE-GENERATION-BUDGET"};
        if (source->source->instanceCount) {
            candidate->instanceLease_ = core::makeShared<core::AtomicCapacityLease>(
                desc.liveInstances, source->source->instanceCount);
            if (!candidate->instanceLease_->charged())
                return {false, "VEGETATION-GPU-LIVE-INSTANCE-BUDGET"};
        }
        // StandaloneCompletion/1 is independent of the broader NativeSync
        // capability. The existing L2 factory grants an authentic fence or null.
        core::UniquePtr<rhi::GpuFence> completion;
        if (groups) {
            completion = device.createFence();
            if (!completion)
                return {false, "VEGETATION-GPU-COMPLETION-UNAVAILABLE"};
        }
        candidate->source_ = std::move(source);
        candidate->validatorOnly_ = decision_.validatorOnly;
        candidate->device_ = &device;
        candidate->tiles_.reserve(candidate->source_->tiles.size());
        core::pmr::Vector<core::UniquePtr<rhi::GpuBuffer>> uploads{
            &core::defaultCpuMemoryResource()};
        uploads.reserve(groups);
        for (const auto& tile : candidate->source_->tiles) {
            TerrainVegetationGpuSnapshot::Tile output;
            output.coord = tile.coord;
            output.prototypes.reserve(tile.prototypes.size());
            for (const auto& prototype : tile.prototypes) {
                const auto bytes =
                    std::uint64_t(prototype.instances.size()) * sizeof(vegetation::InstanceDataGpu);
                auto destination = device.createBuffer({"TerrainVegetation.WorldInstances",
                    bytes,
                    rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopyDst
                        | rhi::ResourceUsage::CopySrc,
                    rhi::MemoryUsage::GpuOnly,
                    rhi::ResourceState::CopyDst});
                auto upload = device.createBuffer({"TerrainVegetation.WorldUpload",
                    bytes,
                    rhi::ResourceUsage::CopySrc,
                    rhi::MemoryUsage::CpuToGpu,
                    rhi::ResourceState::CopySrc});
                if (!destination || !upload)
                    return {false, "VEGETATION-GPU-BUFFER-CREATE"};
                const auto written = device.writeBuffer(*upload, prototype.instances.data(), bytes);
                if (!written.success)
                    return {false, written.diagnostics};
                upload->sealHostWrites();
                output.prototypes.push_back({prototype.prototypeId,
                    static_cast<std::uint32_t>(prototype.instances.size()),
                    std::move(destination)});
                uploads.push_back(std::move(upload));
            }
            candidate->tiles_.push_back(std::move(output));
        }
        core::UniquePtr<rhi::CommandBuffer> commands;
        if (groups) {
            auto encoder = device.createCommandEncoder({"TerrainVegetation.WorldUpload"});
            if (!encoder)
                return {false, "VEGETATION-GPU-ENCODER-CREATE"};
            auto transfer = encoder->beginTransferPass({"TerrainVegetation.WorldCopy"});
            if (!transfer)
                return {false, "VEGETATION-GPU-TRANSFER-BEGIN"};
            std::size_t group = 0u;
            for (const auto& tile : candidate->tiles_)
                for (const auto& prototype : tile.prototypes) {
                    const auto bytes = prototype.buffer->desc().sizeBytes;
                    for (std::uint64_t offset = 0u; offset < bytes;) {
                        const auto chunk = std::min(desc.maxCopyBytes, bytes - offset);
                        const auto copied = transfer->copyBufferToBuffer(
                            {uploads[group]->id(), prototype.buffer->id(), offset, offset, chunk});
                        if (!copied.success)
                            return {false, copied.diagnostics};
                        offset += chunk;
                    }
                    ++group;
                }
            const auto ended = transfer->end();
            if (!ended.success)
                return {false, ended.diagnostics};
            for (const auto& tile : candidate->tiles_)
                for (const auto& prototype : tile.prototypes) {
                    rhi::ResourceBarrierDesc barrier;
                    barrier.resource = prototype.buffer->id();
                    barrier.before = rhi::ResourceState::CopyDst;
                    barrier.after = rhi::ResourceState::ShaderRead;
                    barrier.sourceStages = rhi::PipelineStage::Transfer;
                    barrier.destinationStages =
                        rhi::PipelineStage::ComputeShader | rhi::PipelineStage::VertexShader;
                    barrier.sourceAccess = rhi::ResourceAccess::TransferWrite;
                    barrier.destinationAccess = rhi::ResourceAccess::ShaderRead;
                    const auto transitioned = encoder->transition(*prototype.buffer, barrier);
                    if (!transitioned.success)
                        return {false, transitioned.diagnostics};
                }
            commands = encoder->finish();
            if (!commands)
                return {false, "VEGETATION-GPU-COMMAND-FINISH"};
        }
        // No fallible operation remains after candidate custody transfers.
        candidate_ = std::move(candidate);
        uploads_ = std::move(uploads);
        commands_ = std::move(commands);
        completion_ = std::move(completion);
        device_ = &device;
        state_ = groups ? State::Prepared : State::Completed;
        return {true, {}};
    } catch (const core::DiagnosticError& error) {
        return {false, error.what()};
    } catch (const std::bad_alloc&) {
        return {false, "VEGETATION-GPU-CPU-BUDGET"};
    }
}

TerrainVegetationGpuUpload::Result TerrainVegetationGpuUpload::submit()
{
    if (state_ != State::Prepared)
        return {false, "VEGETATION-GPU-NOT-PREPARED"};
    state_ = State::AcceptedError;
    const auto submitted = device_->queue().submit(*commands_, completion_.get());
    if (!submitted.hasAcceptedWork())
        state_ = State::Rejected;
    else if (submitted.success)
        state_ = State::AcceptedPending;
    return {submitted.success, submitted.diagnostics};
}

bool TerrainVegetationGpuUpload::poll()
{
    if (state_ == State::Completed)
        return true;
    if (state_ != State::AcceptedPending || !completion_->isSignaled())
        return false;
    for (const auto& tile : candidate_->tiles_)
        for (const auto& prototype : tile.prototypes)
            prototype.buffer->sealWrites();
    commands_.reset();
    completion_.reset();
    uploads_.clear();
    state_ = State::Completed;
    return true;
}

bool TerrainVegetationGpuUpload::cancel()
{
    if (outstanding() && (!completion_ || !completion_->isSignaled()))
        return false;
    reset();
    return true;
}

TerrainVegetationGpuUpload::Result TerrainVegetationGpuUpload::drainAndClear(
    const rhi::semantics::ShutdownDrainRequest& request)
{
    if (outstanding()) {
        const auto report = device_->drainForShutdown(request);
        if (!report.completed())
            return {false, report.diagnosticId};
    }
    reset();
    return {true, {}};
}

void TerrainVegetationGpuUpload::reset() noexcept
{
    commands_.reset();
    completion_.reset();
    uploads_.clear();
    candidate_.reset();
    device_ = nullptr;
    state_ = State::Empty;
    decision_ = {};
}

} // namespace arp::renderer
