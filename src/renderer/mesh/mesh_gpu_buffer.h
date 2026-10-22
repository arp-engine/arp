#pragma once

#include "assets/asset_pipeline.h"
#include "assets/mesh_source_ranges.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_device.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace arp::renderer {

using assets::MeshAsset;
using assets::MeshVertex;
using assets::Submesh;

struct SubmeshDrawCenter {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct SubmeshDrawBounds {
    SubmeshDrawCenter center;
    float radius = 0.0f;
    bool valid = false;
};

class MeshGpuBuffer {
public:
    MeshGpuBuffer() = default;
    MeshGpuBuffer(const MeshGpuBuffer&) = delete;
    MeshGpuBuffer& operator=(const MeshGpuBuffer&) = delete;
    MeshGpuBuffer(MeshGpuBuffer&&) noexcept = default;
    MeshGpuBuffer& operator=(MeshGpuBuffer&&) noexcept = default;

    bool validForIndexedDraw() const { return vertexBuffer_ && indexBuffer_ && indexCount_ > 0; }

    rhi::GpuBuffer& vertexBuffer() const { return *vertexBuffer_; }

    rhi::GpuBuffer& indexBuffer() const { return *indexBuffer_; }

    size_t vertexCount() const { return vertexCount_; }

    size_t indexCount() const { return indexCount_; }

    size_t submeshCount() const { return submeshes_.size(); }

    const Submesh& submesh(size_t index) const { return submeshes_.at(index); }

    const std::pmr::vector<Submesh>& submeshes() const { return submeshes_; }

    const std::pmr::vector<SubmeshDrawBounds>& submeshBounds() const { return submeshBounds_; }

    const core::pmr::Vector<assets::MeshSourceRange>& sourceRanges() const { return sourceRanges_; }

    std::uint32_t sourceRangesVersion() const { return sourceRangesVersion_; }

    uint64_t backendResourceId() const { return backendResourceId_; }

    bool compatibleWith(const MeshAsset& mesh) const
    {
        const auto vertexBytes =
            static_cast<std::uint64_t>(mesh.vertices.size() * sizeof(MeshVertex));
        const auto indexBytes =
            static_cast<std::uint64_t>(mesh.indices.size() * sizeof(std::uint32_t));
        return validForIndexedDraw() && vertexCount_ == mesh.vertices.size()
            && indexCount_ == mesh.indices.size() && vertexBuffer_->desc().sizeBytes == vertexBytes
            && indexBuffer_->desc().sizeBytes == indexBytes;
    }

    void setBackendResourceId(uint64_t id) { backendResourceId_ = id; }

    bool immutable() const noexcept { return immutable_; }

    void markImmutable() noexcept
    {
        immutable_ = true;
        if (vertexBuffer_)
            vertexBuffer_->sealHostWrites();
        if (indexBuffer_)
            indexBuffer_->sealHostWrites();
    }

    void reset(arp::core::UniquePtr<rhi::GpuBuffer> vertexBuffer,
        arp::core::UniquePtr<rhi::GpuBuffer> indexBuffer,
        size_t vertexCount,
        size_t indexCount,
        std::pmr::vector<Submesh> submeshes,
        std::pmr::vector<SubmeshDrawBounds> submeshBounds = {},
        core::pmr::Vector<assets::MeshSourceRange> sourceRanges = {},
        std::uint32_t sourceRangesVersion = 0u)
    {
        vertexBuffer_ = std::move(vertexBuffer);
        indexBuffer_ = std::move(indexBuffer);
        vertexCount_ = vertexCount;
        indexCount_ = indexCount;
        submeshes_ = std::move(submeshes);
        submeshBounds_ = std::move(submeshBounds);
        sourceRanges_ = std::move(sourceRanges);
        sourceRangesVersion_ = sourceRangesVersion;
        immutable_ = false;
    }

    void updateMetadata(size_t vertexCount,
        size_t indexCount,
        std::pmr::vector<Submesh> submeshes,
        std::pmr::vector<SubmeshDrawBounds> submeshBounds = {},
        core::pmr::Vector<assets::MeshSourceRange> sourceRanges = {},
        std::uint32_t sourceRangesVersion = 0u)
    {
        vertexCount_ = vertexCount;
        indexCount_ = indexCount;
        submeshes_ = std::move(submeshes);
        submeshBounds_ = std::move(submeshBounds);
        sourceRanges_ = std::move(sourceRanges);
        sourceRangesVersion_ = sourceRangesVersion;
    }

private:
    arp::core::UniquePtr<rhi::GpuBuffer> vertexBuffer_;
    arp::core::UniquePtr<rhi::GpuBuffer> indexBuffer_;
    size_t vertexCount_ = 0;
    size_t indexCount_ = 0;
    std::pmr::vector<Submesh> submeshes_{&core::defaultCpuMemoryResource()};
    std::pmr::vector<SubmeshDrawBounds> submeshBounds_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<assets::MeshSourceRange> sourceRanges_{&core::defaultCpuMemoryResource()};
    std::uint32_t sourceRangesVersion_ = 0u;
    uint64_t backendResourceId_ = 0;
    bool immutable_ = false;
};

struct MeshGpuBufferResult {
    bool success = false;
    std::pmr::string diagnostics;
    MeshGpuBuffer mesh;
};

struct MeshGpuBufferUploadResult {
    bool success = false;
    std::pmr::string diagnostics;
    bool reusedExistingBuffers = false;
};

inline std::pmr::vector<SubmeshDrawBounds> calculateSubmeshDrawBounds(const MeshAsset& mesh)
{
    std::pmr::vector<SubmeshDrawBounds> bounds{&core::defaultCpuMemoryResource()};
    bounds.reserve(mesh.submeshes.size());
    for (const auto& submesh : mesh.submeshes) {
        if (submesh.indexCount == 0
            || submesh.firstIndex + submesh.indexCount > mesh.indices.size()) {
            bounds.push_back(SubmeshDrawBounds{});
            continue;
        }

        SubmeshDrawCenter minPoint{};
        SubmeshDrawCenter maxPoint{};
        bool hasPoint = false;
        for (uint32_t offset = 0; offset < submesh.indexCount; ++offset) {
            const auto vertexIndex = mesh.indices[submesh.firstIndex + offset];
            if (vertexIndex >= mesh.vertices.size()) {
                hasPoint = false;
                break;
            }
            const auto& vertex = mesh.vertices[vertexIndex];
            const SubmeshDrawCenter point{
                vertex.position[0], vertex.position[1], vertex.position[2]};
            if (!hasPoint) {
                minPoint = point;
                maxPoint = point;
                hasPoint = true;
                continue;
            }
            minPoint.x = std::min(minPoint.x, point.x);
            minPoint.y = std::min(minPoint.y, point.y);
            minPoint.z = std::min(minPoint.z, point.z);
            maxPoint.x = std::max(maxPoint.x, point.x);
            maxPoint.y = std::max(maxPoint.y, point.y);
            maxPoint.z = std::max(maxPoint.z, point.z);
        }

        if (!hasPoint) {
            bounds.push_back(SubmeshDrawBounds{});
            continue;
        }

        const SubmeshDrawCenter center{
            (minPoint.x + maxPoint.x) * 0.5f,
            (minPoint.y + maxPoint.y) * 0.5f,
            (minPoint.z + maxPoint.z) * 0.5f,
        };
        float radiusSq = 0.0f;
        for (uint32_t offset = 0; offset < submesh.indexCount; ++offset) {
            const auto& vertex = mesh.vertices[mesh.indices[submesh.firstIndex + offset]];
            const auto dx = vertex.position[0] - center.x;
            const auto dy = vertex.position[1] - center.y;
            const auto dz = vertex.position[2] - center.z;
            radiusSq = std::max(radiusSq, dx * dx + dy * dy + dz * dz);
        }
        bounds.push_back(SubmeshDrawBounds{center, std::sqrt(radiusSq), true});
    }
    return bounds;
}

inline MeshGpuBufferResult uploadMeshToRhi(rhi::GpuDevice& device, const MeshAsset& mesh)
{
    try {
        assets::validateMeshSourceRanges(mesh);
    } catch (const core::DiagnosticError& error) {
        return {false, error.what(), {}};
    }
    if (!mesh.validForIndexedDraw()) {
        return MeshGpuBufferResult{false, "mesh is not valid for indexed draw", {}};
    }

    auto vertexBuffer = device.createBuffer(rhi::BufferDesc{
        "MeshVertices",
        static_cast<uint64_t>(mesh.vertices.size() * sizeof(MeshVertex)),
        rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead,
    });
    auto indexBuffer = device.createBuffer(rhi::BufferDesc{
        "MeshIndices",
        static_cast<uint64_t>(mesh.indices.size() * sizeof(uint32_t)),
        rhi::ResourceUsage::IndexBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead,
    });
    if (!vertexBuffer || !indexBuffer) {
        return MeshGpuBufferResult{false, "RHI buffer creation failed", {}};
    }
    const auto vertexWritten = device.writeBuffer(*vertexBuffer,
        mesh.vertices.data(),
        static_cast<uint64_t>(mesh.vertices.size() * sizeof(MeshVertex)));
    if (!vertexWritten.success) {
        return MeshGpuBufferResult{false, vertexWritten.diagnostics, {}};
    }
    const auto indexWritten = device.writeBuffer(*indexBuffer,
        mesh.indices.data(),
        static_cast<uint64_t>(mesh.indices.size() * sizeof(uint32_t)));
    if (!indexWritten.success) {
        return MeshGpuBufferResult{false, indexWritten.diagnostics, {}};
    }

    MeshGpuBuffer uploaded;
    uploaded.reset(std::move(vertexBuffer),
        std::move(indexBuffer),
        mesh.vertices.size(),
        mesh.indices.size(),
        mesh.submeshes,
        calculateSubmeshDrawBounds(mesh),
        mesh.sourceRanges,
        mesh.sourceRangesVersion);
    return MeshGpuBufferResult{true, "", std::move(uploaded)};
}

inline MeshGpuBufferUploadResult uploadOrUpdateMeshToRhi(
    rhi::GpuDevice& device, const MeshAsset& mesh, MeshGpuBuffer& output)
{
    if (output.immutable())
        return {false, "IMMUTABLE-MESH-UPDATE-FORBIDDEN", false};
    try {
        assets::validateMeshSourceRanges(mesh);
    } catch (const core::DiagnosticError& error) {
        return {false, error.what(), false};
    }
    if (!mesh.validForIndexedDraw()) {
        return MeshGpuBufferUploadResult{false, "mesh is not valid for indexed draw", false};
    }

    if (output.compatibleWith(mesh)) {
        const auto vertexWritten = device.writeBuffer(output.vertexBuffer(),
            mesh.vertices.data(),
            static_cast<std::uint64_t>(mesh.vertices.size() * sizeof(MeshVertex)));
        if (!vertexWritten.success) {
            return MeshGpuBufferUploadResult{false, vertexWritten.diagnostics, true};
        }
        // Compatibility means the existing allocations have sufficient and
        // matching capacity. It does not make index order immutable: terrain
        // LOD stitching changes topology while retaining its index count.
        const auto indexWritten = device.writeBuffer(output.indexBuffer(),
            mesh.indices.data(),
            static_cast<std::uint64_t>(mesh.indices.size() * sizeof(std::uint32_t)));
        if (!indexWritten.success) {
            return MeshGpuBufferUploadResult{false, indexWritten.diagnostics, true};
        }
        output.updateMetadata(mesh.vertices.size(),
            mesh.indices.size(),
            mesh.submeshes,
            calculateSubmeshDrawBounds(mesh),
            mesh.sourceRanges,
            mesh.sourceRangesVersion);
        return MeshGpuBufferUploadResult{true, "", true};
    }

    auto uploaded = uploadMeshToRhi(device, mesh);
    if (!uploaded.success) {
        return MeshGpuBufferUploadResult{false, std::move(uploaded.diagnostics), false};
    }
    output = std::move(uploaded.mesh);
    return MeshGpuBufferUploadResult{true, "", false};
}

} // namespace arp::renderer
