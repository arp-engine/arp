#pragma once

#include "renderer/mesh/mesh_gpu_buffer.h"

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

    Result prepare(rhi::GpuDevice& device,
        const core::pmr::Vector<std::uint32_t>& originalIndices,
        std::size_t originalVertexCount,
        const assets::MeshAsset& lod1,
        const assets::MeshAsset& lod2,
        std::uint64_t maxResidentBytes)
    {
        if (ready_)
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

    bool ready() const { return ready_; }

    std::uint64_t residentBytes() const { return residentBytes_; }

    rhi::GpuBuffer* originalIndexBuffer() const { return original_.get(); }

    const MeshGpuBuffer& candidate(unsigned level) const
    {
        if (!ready_ || !level || level > 2)
            throw core::DiagnosticError("SHADOW-LOD-GPU-LEVEL-INVALID");
        return candidates_[level - 1];
    }

    void clear()
    {
        candidates_ = {};
        original_.reset();
        residentBytes_ = 0;
        ready_ = false;
    }

private:
    std::array<MeshGpuBuffer, 2> candidates_;
    core::UniquePtr<rhi::GpuBuffer> original_;
    std::uint64_t residentBytes_ = 0;
    bool ready_ = false;
};
} // namespace arp::renderer
