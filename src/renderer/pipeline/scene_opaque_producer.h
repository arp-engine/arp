#pragma once

#include "3d/scene_3d_render_queue.h"
#include "renderer/pipeline/basic_forward_contract.h"
#include "renderer/render_graph/original_frame_graph_execution.h"

#include <thread>

namespace arp::renderer {

struct SceneOpaqueVertex {
    core::Vec4 position, normal, albedo;
};

static_assert(sizeof(SceneOpaqueVertex) == 48u);

struct SceneOpaqueFrameConstants {
    core::Mat4 viewProjection;
    BasicForwardLightingConstants lighting;
};

static_assert(sizeof(SceneOpaqueFrameConstants) == 112u);
static_assert(offsetof(SceneOpaqueFrameConstants, lighting) == 64u);

// All visible items require an explicit binding, including terrain-produced
// MeshAssets. Material order is the mesh's declared section material-slot ABI.
struct SceneOpaqueMeshBinding {
    std::size_t itemIndex = SIZE_MAX;
    std::shared_ptr<const assets::MeshAsset> mesh;
    BasicWorldMeshProfile profile;
    core::ArrayView<std::shared_ptr<const resource::MaterialResource>> materials;
};

struct SceneOpaqueGeometryBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxItems = 0u, maxExpandedVertices = 0u;
    std::uint64_t maxCpuBytes = 0u;
    std::shared_ptr<core::CpuMemoryQuota> quota;
};

class ARP_API SceneOpaqueGeometry final {
public:
    static const char* create(const scene3d::Scene3DRenderQueue&,
        core::ArrayView<SceneOpaqueMeshBinding>,
        const resource::ShaderPackageResource&,
        std::string_view shaderId,
        const SceneOpaqueGeometryBudget&,
        std::shared_ptr<const SceneOpaqueGeometry>&);
    const char* validateCurrent(
        const scene3d::Scene3DRenderQueue&, core::ArrayView<SceneOpaqueMeshBinding>) const noexcept;

    core::ArrayView<SceneOpaqueVertex> vertices() const noexcept { return vertices_; }

private:
    struct Snapshot {
        scene3d::Scene3DRenderItem item;
        BasicWorldMeshProfile profile;
        std::shared_ptr<const assets::MeshAsset> mesh;
        core::pmr::Vector<std::shared_ptr<const resource::MaterialResource>> materials;
    };

    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::pmr::Vector<SceneOpaqueVertex> vertices_;
    core::pmr::Vector<Snapshot> snapshots_;
};

struct SceneOpaqueProducerBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCpuBytes = 0u, maxGpuBytes = 0u;
    std::array<char, 64u> shaderSourceSha256{};
    std::shared_ptr<core::CpuMemoryQuota> quota, sourceQuota;
    std::shared_ptr<core::AtomicCapacityBudget> retainedGpuBytes;
};

class SceneOpaqueProducer;

class ARP_API SceneOpaqueFrame final {
    friend class SceneOpaqueProducer;

    class Key {
        friend class SceneOpaqueProducer;
        Key() = default;
    };

public:
    SceneOpaqueFrame(Key,
        std::shared_ptr<const rhi::OriginalCommandBufferLease> geometry,
        std::shared_ptr<const rhi::OriginalCommandBufferLease> constants,
        SceneOpaqueFrameConstants values)
        : geometry_(std::move(geometry)), constants_(std::move(constants)), values_(values)
    {}

    const auto& geometry() const noexcept { return geometry_; }

    const auto& constants() const noexcept { return constants_; }

    const auto& values() const noexcept { return values_; }

private:
    std::shared_ptr<const rhi::OriginalCommandBufferLease> geometry_, constants_;
    SceneOpaqueFrameConstants values_;
};

struct SceneOpaqueGraphContext {
    render_graph::OriginalFrameGraphExecution& execution;
    const SceneOpaqueFrame& frame;
    render_graph::TextureHandle color, depth;
    std::size_t pipeline = SIZE_MAX, descriptors = SIZE_MAX;
    rhi::Color clear;
};

// Cold geometry and immutable frame owners. The caller assembles them into its
// existing Sources and freezes descriptors; this producer owns no inventory,
// command encoder, executor or native completion domain.
class ARP_API SceneOpaqueProducer final {
public:
    // IEEE 754 binary16 maximum finite value for the declared RGBA16F target.
    static constexpr float maxLinearColor = 65504.f;
    SceneOpaqueProducer() = default;
    SceneOpaqueProducer(const SceneOpaqueProducer&) = delete;
    SceneOpaqueProducer& operator=(const SceneOpaqueProducer&) = delete;
    static const char* validatePipeline(const rhi::RenderPipelineDesc&);
    const char* create(rhi::GpuDevice&,
        const std::shared_ptr<const SceneOpaqueGeometry>&,
        const SceneOpaqueProducerBudget&);
    const char* prepareFrame(const core::Mat4&,
        const BasicForwardLighting&,
        std::shared_ptr<const SceneOpaqueFrame>&) const;
    const char* contribute(const SceneOpaqueGraphContext&) const;
    const char* validate(const SceneOpaqueGraphContext&) const;

    const auto& geometryBuffer() const noexcept { return geometryBuffer_; }

    const auto& geometry() const noexcept { return geometry_; }

private:
    const char* buffer(const void*,
        std::uint64_t,
        rhi::ResourceUsage,
        std::shared_ptr<const rhi::OriginalCommandBufferLease>&) const;
    rhi::GpuDevice* device_ = nullptr;
    rhi::OriginalDeviceLifetime::Ticket lifetime_;
    std::thread::id owner_;
    SceneOpaqueProducerBudget budget_;
    std::shared_ptr<const SceneOpaqueGeometry> geometry_;
    std::shared_ptr<const rhi::OriginalCommandBufferLease> geometryBuffer_;
};
} // namespace arp::renderer
