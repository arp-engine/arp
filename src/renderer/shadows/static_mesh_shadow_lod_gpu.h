#pragma once

#include "renderer/mesh/immutable_mesh_upload.h"

#include <array>

namespace arp::renderer {
// Prepared immutable buffers only; the caller binds the original vertex bank
// with originalIndexBuffer(). No CPU mesh/resource pointer is retained. Clear or
// destroy only after the caller's normal completion drain, or device-loss path.
class StaticMeshShadowLodGpu final {
public:
    struct Result {
        bool success = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    struct ResidencyBudget {
        ImmutableMeshUpload::Budget upload;
        std::uint64_t sourceGeneration = 0;
    };

    Result prepare(rhi::GpuDevice& device,
        const core::pmr::Vector<std::uint32_t>& originalIndices,
        std::size_t originalVertexCount,
        const assets::MeshAsset& lod1,
        const assets::MeshAsset& lod2,
        std::uint64_t maxResidentBytes)
    {
        if (prepared())
            return {false, "SHADOW-LOD-GPU-ALREADY-PREPARED"};
        if (!maxResidentBytes || originalIndices.size() > UINT32_MAX || originalIndices.size() % 3u)
            return {false, "SHADOW-LOD-GPU-BUDGET-INVALID"};
        std::uint64_t bytes = 0;
        const auto charge = [&](std::size_t count, std::size_t stride) {
            if (count > (maxResidentBytes - bytes) / stride)
                return false;
            bytes += std::uint64_t(count) * stride;
            return true;
        };
        if (!charge(originalIndices.size(), sizeof(std::uint32_t)))
            return {false, "SHADOW-LOD-GPU-BUDGET-INVALID"};
        for (const auto* mesh : {&lod1, &lod2})
            if (!charge(mesh->vertices.size(), sizeof(assets::MeshVertex))
                || !charge(mesh->indices.size(), sizeof(std::uint32_t)))
                return {false, "SHADOW-LOD-GPU-BUDGET-INVALID"};
        for (const auto index : originalIndices)
            if (index >= originalVertexCount)
                return {false, "SHADOW-LOD-GPU-ORIGINAL-INDEX-INVALID"};
        std::array<MeshGpuBuffer, 2> candidates;
        unsigned level = 0;
        for (const auto* mesh : {&lod1, &lod2}) {
            auto uploaded = uploadMeshToRhi(device, *mesh);
            if (!uploaded.success)
                return {false, std::move(uploaded.diagnostics)};
            candidates[level++] = std::move(uploaded.mesh);
        }
        core::UniquePtr<rhi::GpuBuffer> original;
        if (!originalIndices.empty()) {
            original = device.createBuffer(rhi::BufferDesc{"ShadowLodOriginalIndices",
                std::uint64_t(originalIndices.size()) * sizeof(std::uint32_t),
                rhi::ResourceUsage::IndexBuffer | rhi::ResourceUsage::CopyDst,
                rhi::MemoryUsage::CpuToGpu,
                rhi::ResourceState::ShaderRead});
            if (!original)
                return {false, "SHADOW-LOD-GPU-INDEX-CREATE-FAILED"};
            const auto written = device.writeBuffer(*original,
                originalIndices.data(),
                std::uint64_t(originalIndices.size()) * sizeof(std::uint32_t));
            if (!written.success)
                return {false, written.diagnostics};
        }
        candidates_ = std::move(candidates);
        original_ = std::move(original);
        residentBytes_ = bytes;
        ready_ = true;
        return {true, {}};
    }

    // Separate opt-in: the legacy prepare path retains its original semantics.
    // Prepare all banks before any submit; partial accepted work never falls back.
    Result prepareResident(rhi::GpuDevice& device,
        const core::pmr::Vector<std::uint32_t>& originalIndices,
        std::size_t originalVertexCount,
        const assets::MeshAsset& lod1,
        const assets::MeshAsset& lod2,
        const ResidencyBudget& budget)
    {
        if (prepared() || requiresDrain())
            return {false, "SHADOW-LOD-GPU-ALREADY-PREPARED"};
        const auto& policy = budget.upload;
        if (policy.contractVersion != 1 || !budget.sourceGeneration || !policy.destinationBytes
            || !policy.uploadBytes || !policy.metadataBytes || !policy.maxCopyBytes
            || policy.maxCopyBytes % 4 || !policy.maxCopyCommands || originalIndices.empty()
            || originalIndices.size() > UINT32_MAX || originalIndices.size() % 3u)
            return {false, "SHADOW-LOD-GPU-RESIDENCY-BUDGET-INVALID"};
        std::uint64_t payload = 0, metadata = 0, copies = 0;
        const auto charge =
            [](std::uint64_t& used, std::size_t count, std::size_t stride, std::uint64_t limit) {
                if (count > (limit - used) / stride)
                    return false;
                used += std::uint64_t(count) * stride;
                return true;
            };
        const auto stream = [&](std::size_t count, std::size_t stride) {
            if (!charge(payload, count, stride, policy.destinationBytes))
                return false;
            const auto bytes = std::uint64_t(count) * stride;
            copies += bytes / policy.maxCopyBytes + (bytes % policy.maxCopyBytes != 0);
            return copies <= policy.maxCopyCommands && payload <= policy.uploadBytes;
        };
        if (!stream(originalIndices.size(), sizeof(std::uint32_t)))
            return {false, "SHADOW-LOD-GPU-RESIDENCY-BUDGET-INVALID"};
        for (const auto* mesh : {&lod1, &lod2})
            if (!stream(mesh->vertices.size(), sizeof(assets::MeshVertex))
                || !stream(mesh->indices.size(), sizeof(std::uint32_t))
                || !charge(metadata,
                    mesh->submeshes.size(),
                    sizeof(assets::Submesh) + sizeof(SubmeshDrawBounds),
                    policy.metadataBytes)
                || !charge(metadata,
                    mesh->sourceRanges.size(),
                    sizeof(assets::MeshSourceRange),
                    policy.metadataBytes))
                return {false, "SHADOW-LOD-GPU-RESIDENCY-BUDGET-INVALID"};
        for (const auto index : originalIndices)
            if (index >= originalVertexCount)
                return {false, "SHADOW-LOD-GPU-ORIGINAL-INDEX-INVALID"};
        // All ticket ownership is cold and core-allocated. No hot-path container
        // growth, queue wait, native memory query, or backend-specific branch.
        try {
            std::array<core::UniquePtr<ImmutableMeshUpload>, 3> tickets;
            for (auto& ticket : tickets)
                ticket = core::makeUnique<ImmutableMeshUpload>();
            for (unsigned i = 0; i < 2; ++i) {
                const auto result =
                    tickets[i]->prepare(device, i ? lod2 : lod1, budget.sourceGeneration, policy);
                if (!result.success)
                    return {false, result.diagnostics};
            }
            const auto result = tickets[2]->prepareIndexStream(
                device, originalIndices, originalVertexCount, budget.sourceGeneration, policy);
            if (!result.success)
                return {false, result.diagnostics};
            for (unsigned i = 0; i < 2; ++i)
                tickets[i]->installPreparedBindings(candidates_[i]);
            tickets[2]->installPreparedIndexBinding(original_);
            uploads_ = std::move(tickets);
            residentBytes_ = payload;
            metadataBytes_ = metadata;
            copyCommands_ = static_cast<std::uint32_t>(copies);
            residentPrepared_ = true;
            return {true, {}};
        } catch (const core::DiagnosticError& error) {
            return {false, error.what()};
        } catch (const std::bad_alloc&) {
            return {false, "SHADOW-LOD-GPU-RESIDENCY-CPU-BUDGET"};
        }
    }

    Result submitResidency()
    {
        if (!residentPrepared_ || submitAttempted_)
            return {false, "SHADOW-LOD-GPU-RESIDENCY-NOT-PREPARED"};
        submitAttempted_ = true;
        for (auto& ticket : uploads_) {
            const auto result = ticket->submit();
            if (!result.success)
                return {false, result.diagnostics};
        }
        residentSubmitted_ = true;
        return {true, {}};
    }

    bool prepared() const noexcept { return ready_ || residentPrepared_; }

    bool ready() const
    {
        if (ready_)
            return true;
        if (!residentSubmitted_)
            return false;
        for (const auto& ticket : uploads_)
            if (!ticket->ready())
                return false;
        return true;
    }

    bool requiresDrain() const noexcept
    {
        for (const auto& ticket : uploads_)
            if (ticket
                && (ticket->state() == ImmutableMeshUpload::State::AcceptedPending
                    || ticket->state() == ImmutableMeshUpload::State::AcceptedError
                    || ticket->state() == ImmutableMeshUpload::State::Completed))
                return true;
        return false;
    }

    bool reapCompleted()
    {
        if (!residentPrepared_)
            return false;
        for (auto& ticket : uploads_)
            if (ticket->state() == ImmutableMeshUpload::State::AcceptedPending)
                ticket->reapCompleted();
        return ready();
    }

    std::uint64_t retainedUploadBytes() const noexcept
    {
        std::uint64_t bytes = 0;
        for (const auto& ticket : uploads_)
            if (ticket)
                bytes += ticket->retainedUploadBytes();
        return bytes;
    }

    std::uint64_t metadataBytes() const noexcept { return metadataBytes_; }

    std::uint32_t copyCommandCount() const noexcept { return copyCommands_; }

    std::uint64_t residentBytes() const { return residentBytes_; }

    rhi::GpuBuffer* originalIndexBuffer() const { return original_.get(); }

    const MeshGpuBuffer& candidate(unsigned level) const
    {
        if (!prepared() || !level || level > 2)
            throw core::DiagnosticError("SHADOW-LOD-GPU-LEVEL-INVALID");
        return candidates_[level - 1];
    }

    void clear()
    {
        if (requiresDrain())
            throw core::DiagnosticError("SHADOW-LOD-GPU-RESIDENCY-DRAIN-REQUIRED");
        reset();
    }

    bool clearAfterDeviceDrain(const rhi::semantics::ShutdownDrainReport& report)
    {
        if (!report.completed())
            return false;
        for (auto& ticket : uploads_)
            if (ticket)
                ticket->clearAfterDeviceDrain(report);
        reset();
        return true;
    }

    void clearAfterDeviceLoss()
    {
        for (auto& ticket : uploads_)
            if (ticket)
                ticket->clearAfterDeviceLoss();
        reset();
    }

private:
    void reset()
    {
        uploads_ = {};
        candidates_ = {};
        original_.reset();
        residentBytes_ = metadataBytes_ = 0;
        copyCommands_ = 0;
        ready_ = residentPrepared_ = residentSubmitted_ = submitAttempted_ = false;
    }

    std::array<core::UniquePtr<ImmutableMeshUpload>, 3> uploads_;
    std::array<MeshGpuBuffer, 2> candidates_;
    core::UniquePtr<rhi::GpuBuffer> original_;
    std::uint64_t residentBytes_ = 0;
    std::uint64_t metadataBytes_ = 0;
    std::uint32_t copyCommands_ = 0;
    bool ready_ = false, residentPrepared_ = false, residentSubmitted_ = false,
         submitAttempted_ = false;
};
} // namespace arp::renderer
