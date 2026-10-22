#pragma once

#include "renderer/mesh/mesh_gpu_buffer.h"

#include <array>
#include <limits>

namespace arp::renderer {

// ImmutableMeshResidency/1. One cold ticket; no wait/IO or allocation in ready/reap.
// Bindings may be installed before completion, but every consuming pipeline MUST
// gate drawing on ready(). Keep this owner alive through completion or device drain.
class ImmutableMeshUpload final {
public:
    enum class State { Empty, Prepared, Rejected, AcceptedPending, AcceptedError, Completed };

    struct Budget {
        std::uint32_t contractVersion = 0;
        std::uint64_t destinationBytes = 0;
        std::uint64_t uploadBytes = 0;
        std::uint64_t metadataBytes = 0;
        std::uint64_t maxCopyBytes = 0;
        std::uint32_t maxCopyCommands = 0;
        bool readbackSource = false;
    };

    struct Result {
        bool success = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    ImmutableMeshUpload() = default;
    ImmutableMeshUpload(const ImmutableMeshUpload&) = delete;
    ImmutableMeshUpload& operator=(const ImmutableMeshUpload&) = delete;

    Result prepare(rhi::GpuDevice& device,
        const assets::MeshAsset& source,
        std::uint64_t sourceGeneration,
        const Budget& budget)
    {
        if (state_ != State::Empty)
            return {false, "IMMUTABLE-MESH-TICKET-NOT-EMPTY"};
        if (budget.contractVersion != 1 || !sourceGeneration || !budget.destinationBytes
            || !budget.uploadBytes || !budget.metadataBytes || !budget.maxCopyBytes
            || budget.maxCopyBytes % 4 || !budget.maxCopyCommands)
            return {false, "IMMUTABLE-MESH-BUDGET-OR-GENERATION"};
        const auto caps = device.capabilities();
        if (!caps.memory.deviceLocal || !caps.memory.hostVisible
            || !caps.explicitFeatures.explicitBarriers)
            return {false, "IMMUTABLE-MESH-EXECUTION-UNAVAILABLE"};
        if (source.vertices.empty() || source.indices.empty() || source.submeshes.empty()
            || source.indices.size() > UINT32_MAX)
            return {false, "IMMUTABLE-MESH-GEOMETRY-INVALID"};
        std::uint64_t payload = 0, metadata = 0;
        const auto charge =
            [](std::uint64_t& used, std::size_t count, std::size_t stride, std::uint64_t limit) {
                if (count > (limit - used) / stride)
                    return false;
                used += std::uint64_t(count) * stride;
                return true;
            };
        if (!charge(payload,
                source.vertices.size(),
                sizeof(assets::MeshVertex),
                budget.destinationBytes)
            || !charge(
                payload, source.indices.size(), sizeof(std::uint32_t), budget.destinationBytes)
            || payload > budget.uploadBytes
            || !charge(metadata,
                source.submeshes.size(),
                sizeof(assets::Submesh) + sizeof(SubmeshDrawBounds),
                budget.metadataBytes)
            || !charge(metadata,
                source.sourceRanges.size(),
                sizeof(assets::MeshSourceRange),
                budget.metadataBytes))
            return {false, "IMMUTABLE-MESH-PAYLOAD-BUDGET"};
        const auto vertexBytes = std::uint64_t(source.vertices.size()) * sizeof(assets::MeshVertex);
        const auto indexBytes = std::uint64_t(source.indices.size()) * sizeof(std::uint32_t);
        const auto copies = [](std::uint64_t bytes, std::uint64_t chunk) {
            return bytes / chunk + (bytes % chunk != 0);
        };
        if (copies(vertexBytes, budget.maxCopyBytes) > budget.maxCopyCommands
            || copies(indexBytes, budget.maxCopyBytes)
                > budget.maxCopyCommands - copies(vertexBytes, budget.maxCopyBytes))
            return {false, "IMMUTABLE-MESH-COMMAND-BUDGET"};
        for (const auto index : source.indices)
            if (index >= source.vertices.size())
                return {false, "IMMUTABLE-MESH-INDEX-INVALID"};
        for (const auto& section : source.submeshes)
            if (!section.indexCount || section.firstIndex > source.indices.size()
                || section.indexCount > source.indices.size() - section.firstIndex)
                return {false, "IMMUTABLE-MESH-SECTION-INVALID"};
        try {
            assets::validateMeshSourceRanges(source);
            // Only mesh metadata consumes this quota. Backend native allocation
            // alignment and wrappers retain their existing physical/CPU budgets.
            auto quota = core::makeShared<core::CpuMemoryQuota>(budget.metadataBytes);
            core::pmr::Vector<assets::Submesh> sections{&core::defaultCpuMemoryResource()};
            core::pmr::Vector<SubmeshDrawBounds> bounds{&core::defaultCpuMemoryResource()};
            core::pmr::Vector<assets::MeshSourceRange> ranges{&core::defaultCpuMemoryResource()};
            {
                core::CpuMemoryQuotaScope scope(quota);
                sections.assign(source.submeshes.begin(), source.submeshes.end());
                bounds = calculateSubmeshDrawBounds(source);
                ranges.assign(source.sourceRanges.begin(), source.sourceRanges.end());
            }
            const std::array<std::uint64_t, 2> sizes{vertexBytes, indexBytes};
            const std::array<const void*, 2> data{source.vertices.data(), source.indices.data()};
            std::array<core::UniquePtr<rhi::GpuBuffer>, 2> destinations;
            std::array<core::UniquePtr<rhi::GpuBuffer>, 2> uploads;
            for (unsigned i = 0; i < 2; ++i) {
                destinations[i] =
                    device.createBuffer({i ? "ImmutableMesh.Indices" : "ImmutableMesh.Vertices",
                        sizes[i],
                        (i ? rhi::ResourceUsage::IndexBuffer : rhi::ResourceUsage::VertexBuffer)
                            | rhi::ResourceUsage::CopyDst
                            | (budget.readbackSource ? rhi::ResourceUsage::CopySrc
                                                     : rhi::ResourceUsage::None),
                        rhi::MemoryUsage::GpuOnly,
                        rhi::ResourceState::CopyDst});
                uploads[i] = device.createBuffer({"ImmutableMesh.Upload",
                    sizes[i],
                    rhi::ResourceUsage::CopySrc,
                    rhi::MemoryUsage::CpuToGpu,
                    rhi::ResourceState::CopySrc});
                if (!destinations[i] || !uploads[i])
                    return {false, "IMMUTABLE-MESH-BUFFER-CREATE"};
                const auto written = device.writeBuffer(*uploads[i], data[i], sizes[i]);
                if (!written.success)
                    return {false, written.diagnostics};
                uploads[i]->sealHostWrites();
            }
            auto encoder = device.createCommandEncoder({"ImmutableMesh.Upload"});
            if (!encoder)
                return {false, "IMMUTABLE-MESH-ENCODER-CREATE"};
            auto transfer = encoder->beginTransferPass({"ImmutableMesh.Copy"});
            if (!transfer)
                return {false, "IMMUTABLE-MESH-TRANSFER-BEGIN"};
            for (unsigned i = 0; i < 2; ++i)
                for (std::uint64_t offset = 0; offset < sizes[i];) {
                    const auto bytes = std::min(budget.maxCopyBytes, sizes[i] - offset);
                    const auto copied = transfer->copyBufferToBuffer(
                        {uploads[i]->id(), destinations[i]->id(), offset, offset, bytes});
                    if (!copied.success)
                        return {false, copied.diagnostics};
                    offset += bytes;
                }
            const auto ended = transfer->end();
            if (!ended.success)
                return {false, ended.diagnostics};
            for (unsigned i = 0; i < 2; ++i) {
                rhi::ResourceBarrierDesc barrier;
                barrier.resource = destinations[i]->id();
                barrier.before = rhi::ResourceState::CopyDst;
                barrier.after = rhi::ResourceState::ShaderRead;
                barrier.sourceStages = rhi::PipelineStage::Transfer;
                barrier.destinationStages = rhi::PipelineStage::VertexInput;
                barrier.sourceAccess = rhi::ResourceAccess::TransferWrite;
                barrier.destinationAccess =
                    i ? rhi::ResourceAccess::IndexRead : rhi::ResourceAccess::VertexAttributeRead;
                const auto transitioned = encoder->transition(*destinations[i], barrier);
                if (!transitioned.success)
                    return {false, transitioned.diagnostics};
            }
            auto commands = encoder->finish();
            if (!commands)
                return {false, "IMMUTABLE-MESH-COMMAND-FINISH"};
            auto completion = commands->ownedSubmissionFence();
            if (!completion) {
                // NativeSync also requires public binary semaphores. Its broad
                // feature flag does not describe this independent fence factory.
                // The L2 factory must return an authentic completion or null.
                auto fence = device.createFence();
                if (!fence)
                    return {false, "IMMUTABLE-MESH-COMPLETION-UNAVAILABLE"};
                auto owner = core::makeShared<core::UniquePtr<rhi::GpuFence>>(std::move(fence));
                completion = {owner, owner->get()};
            }
            mesh_.reset(std::move(destinations[0]),
                std::move(destinations[1]),
                source.vertices.size(),
                source.indices.size(),
                std::move(sections),
                std::move(bounds),
                std::move(ranges),
                source.sourceRangesVersion);
            mesh_.markImmutable();
            uploads_ = std::move(uploads);
            commands_ = std::move(commands);
            completion_ = std::move(completion);
            device_ = &device;
            sourceGeneration_ = sourceGeneration;
            payloadBytes_ = retainedUploadBytes_ = payload;
            state_ = State::Prepared;
            return {true, {}};
        } catch (const core::DiagnosticError& error) {
            return {false, error.what()};
        } catch (const std::bad_alloc&) {
            return {false, "IMMUTABLE-MESH-CPU-BUDGET"};
        }
    }

    Result submit()
    {
        if (state_ != State::Prepared)
            return {false, "IMMUTABLE-MESH-NOT-PREPARED"};
        // Unknown exceptions after entering submit conservatively retain custody.
        state_ = State::AcceptedError;
        const auto result = device_->queue().submit(*commands_, completion_.get());
        if (!result.hasAcceptedWork()) {
            state_ = State::Rejected;
            return {false, result.diagnostics};
        }
        if (!result.success)
            return {false, result.diagnostics};
        state_ = State::AcceptedPending;
        return {true, {}};
    }

    // Installing bindings is not a completion credential. Caller retains this
    // ticket and gates ALL uses on ready(); a pending upload must defer rendering.
    void installBindings(MeshGpuBuffer& destination)
    {
        if (state_ != State::AcceptedPending || installed_ || destination.validForIndexedDraw())
            throw core::DiagnosticError("IMMUTABLE-MESH-BINDING-STATE");
        destination = std::move(mesh_);
        installed_ = true;
    }

    bool ready() const
    {
        return state_ == State::Completed
            || (state_ == State::AcceptedPending && completion_ && completion_->isSignaled());
    }

    bool reapCompleted()
    {
        if (!ready())
            return false;
        uploads_ = {};
        commands_.reset();
        completion_.reset();
        retainedUploadBytes_ = 0;
        state_ = State::Completed;
        return true;
    }

    State state() const noexcept { return state_; }

    std::uint64_t sourceGeneration() const noexcept { return sourceGeneration_; }

    std::uint64_t payloadBytes() const noexcept { return payloadBytes_; }

    std::uint64_t retainedUploadBytes() const noexcept { return retainedUploadBytes_; }

    // Valid only at the caller's successful device drain / authenticated loss
    // boundary, or for unaccepted work. Never use a frame serial as that proof.
    bool clearAfterDeviceDrain(const rhi::semantics::ShutdownDrainReport& report)
    {
        if (!report.completed())
            return false;
        clearAfterDeviceLoss();
        return true;
    }

    bool cancelUnsubmitted()
    {
        if (state_ == State::AcceptedPending || state_ == State::AcceptedError
            || state_ == State::Completed)
            return false;
        clearAfterDeviceLoss();
        return true;
    }

    void clearAfterDeviceLoss()
    {
        mesh_ = {};
        commands_.reset();
        completion_.reset();
        uploads_ = {};
        device_ = nullptr;
        sourceGeneration_ = payloadBytes_ = retainedUploadBytes_ = 0;
        installed_ = false;
        state_ = State::Empty;
    }

private:
    State state_ = State::Empty;
    rhi::GpuDevice* device_ = nullptr;
    MeshGpuBuffer mesh_;
    std::array<core::UniquePtr<rhi::GpuBuffer>, 2> uploads_;
    core::UniquePtr<rhi::CommandBuffer> commands_;
    std::shared_ptr<rhi::GpuFence> completion_;
    std::uint64_t sourceGeneration_ = 0, payloadBytes_ = 0, retainedUploadBytes_ = 0;
    bool installed_ = false;
};
} // namespace arp::renderer
