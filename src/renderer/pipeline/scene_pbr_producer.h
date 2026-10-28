#pragma once

#include "renderer/pipeline/scene_opaque_producer.h"
#include "renderer/pipeline/scene_pbr_texture_publication.h"

namespace arp::renderer {

struct ScenePbrVertex {
    core::Vec4 position, normal, tangent, uv, tint;
};

static_assert(sizeof(ScenePbrVertex) == 80u);
static_assert(offsetof(ScenePbrVertex, tangent) == 32u);
static_assert(offsetof(ScenePbrVertex, tint) == 64u);

struct ScenePbrMaterialConstants {
    core::Vec4 baseColor, emissive, factors, flags;
};

static_assert(sizeof(ScenePbrMaterialConstants) == 64u);
static_assert(offsetof(ScenePbrMaterialConstants, factors) == 32u);

struct ScenePbrFrameConstants {
    core::Mat4 viewProjection;
    BasicForwardLightingConstants lighting;
    core::Vec4 cameraPosition;
};

static_assert(sizeof(ScenePbrFrameConstants) == 128u);
static_assert(offsetof(ScenePbrFrameConstants, cameraPosition) == 112u);

struct ScenePbrGeometryBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxItems = 0u, maxExpandedVertices = 0u, maxSections = 0u;
    std::uint64_t maxCpuBytes = 0u;
    std::shared_ptr<core::CpuMemoryQuota> quota;
};

class ARP_API ScenePbrGeometry final {
public:
    struct Section {
        std::uint32_t firstVertex = 0u, vertexCount = 0u;
        std::size_t itemIndex = 0u;
        std::uint32_t materialSlot = 0u;
        ScenePbrMaterialConstants constants;
        std::shared_ptr<const resource::MaterialResource> material;
    };

    static const char* material(const resource::MaterialResource&,
        const resource::ShaderPackageResource&,
        std::string_view shaderId,
        ScenePbrMaterialConstants&);
    static const char* create(const scene3d::Scene3DRenderQueue&,
        core::ArrayView<SceneOpaqueMeshBinding>,
        const resource::ShaderPackageResource&,
        std::string_view shaderId,
        const ScenePbrGeometryBudget&,
        std::shared_ptr<const ScenePbrGeometry>&);
    const char* validateCurrent(
        const scene3d::Scene3DRenderQueue&, core::ArrayView<SceneOpaqueMeshBinding>) const noexcept;

    core::ArrayView<ScenePbrVertex> vertices() const noexcept { return vertices_; }

    core::ArrayView<Section> sections() const noexcept { return sections_; }

private:
    struct Snapshot {
        scene3d::Scene3DRenderItem item;
        BasicWorldMeshProfile profile;
        std::shared_ptr<const assets::MeshAsset> mesh;
        std::uint64_t content = 0u;
        core::pmr::Vector<std::shared_ptr<const resource::MaterialResource>> materials;
        core::pmr::Vector<std::uint64_t> materialContent;
    };

    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::pmr::Vector<ScenePbrVertex> vertices_;
    core::pmr::Vector<Section> sections_;
    core::pmr::Vector<Snapshot> snapshots_;
};

// Order is base-color, normal, metallic-roughness, occlusion, emissive.
// Explicit resource-ID to original GPU publication bindings, supplied cold by
// the caller's loader/uploader. No guessed paths or substitute images.
struct ScenePbrTextureBinding {
    core::pmr::String resourceId;
    std::shared_ptr<const rhi::OriginalCommandTextureViewSource> view;
    std::shared_ptr<const ScenePbrTexturePublication> publication;
};

struct ScenePbrMaterialBinding {
    std::array<ScenePbrTextureBinding, 5u> textures;
    std::shared_ptr<const rhi::OriginalCommandSamplerLease> sampler;
};

class ARP_API ScenePbrFrame final {
    friend class ScenePbrProducer;

    class Key {
        friend class ScenePbrProducer;
        Key() = default;
    };

public:
    ScenePbrFrame(Key,
        std::shared_ptr<const rhi::OriginalCommandBufferLease> geometry,
        std::shared_ptr<const rhi::OriginalCommandBufferLease> constants,
        ScenePbrFrameConstants values)
        : geometry_(std::move(geometry)), constants_(std::move(constants)), values_(values)
    {}

    const auto& geometry() const noexcept { return geometry_; }

    const auto& constants() const noexcept { return constants_; }

    const auto& values() const noexcept { return values_; }

private:
    std::shared_ptr<const rhi::OriginalCommandBufferLease> geometry_, constants_;
    ScenePbrFrameConstants values_;
};

struct ScenePbrGraphContext {
    render_graph::OriginalFrameGraphExecution& execution;
    const ScenePbrFrame& frame;
    render_graph::TextureHandle color, depth;
    std::size_t pipeline = SIZE_MAX;
    core::ArrayView<std::size_t> sectionDescriptors;
    rhi::Color clear;
};

struct ScenePbrView {
    enum class Kind { Unspecified, Perspective, Orthographic };
    Kind kind = Kind::Unspecified;
    // Perspective: world camera position. Orthographic: surface-to-eye direction.
    core::Vec3 positionOrDirection;
};

class ARP_API ScenePbrProducer final {
public:
    ScenePbrProducer() = default;
    ScenePbrProducer(const ScenePbrProducer&) = delete;
    ScenePbrProducer& operator=(const ScenePbrProducer&) = delete;
    static const char* viewConstants(const ScenePbrView&, core::Vec4&) noexcept;
    static const char* frameConstants(const core::Mat4&,
        const BasicForwardLighting&,
        const ScenePbrView&,
        const ScenePbrGeometry&,
        ScenePbrFrameConstants&) noexcept;
    // Availability alone never grants sampled content; every binding requires
    // its exact Ready-decoded/upload-publication join.
    static const char* textureExecutionDiagnostic(
        rhi::Backend, std::uint64_t maxTextures, std::uint64_t maxPackedBytes) noexcept;
    static const char* validatePipeline(const rhi::RenderPipelineDesc&);
    static const char* validateMaterialBinding(
        const resource::MaterialResource&, const ScenePbrMaterialBinding&);
    const char* create(rhi::GpuDevice&,
        const std::shared_ptr<const ScenePbrGeometry>&,
        core::ArrayView<ScenePbrMaterialBinding>,
        const SceneOpaqueProducerBudget&);
    const char* prepareFrame(const core::Mat4&,
        const BasicForwardLighting&,
        const ScenePbrView&,
        std::shared_ptr<const ScenePbrFrame>&) const;
    const char* prepareFrame(const resource::ResourceManager&,
        const core::Mat4&,
        const BasicForwardLighting&,
        const ScenePbrView&,
        std::shared_ptr<const ScenePbrFrame>&) const;
    const char* validate(const ScenePbrGraphContext&) const;
    const char* contribute(const ScenePbrGraphContext&) const;

    const auto& geometry() const noexcept { return geometry_; }

    const auto& geometryBuffer() const noexcept { return geometryBuffer_; }

    const auto& materialBuffers() const noexcept { return materialBuffers_; }

    const auto& materialBindings() const noexcept { return bindings_; }

private:
    const char* prepareValidatedFrame(const core::Mat4&,
        const BasicForwardLighting&,
        const ScenePbrView&,
        std::shared_ptr<const ScenePbrFrame>&) const;
    rhi::GpuDevice* device_ = nullptr;
    rhi::OriginalDeviceLifetime::Ticket lifetime_;
    std::thread::id owner_;
    SceneOpaqueProducerBudget budget_;
    std::shared_ptr<const ScenePbrGeometry> geometry_;
    std::shared_ptr<const rhi::OriginalCommandBufferLease> geometryBuffer_;
    core::pmr::Vector<std::shared_ptr<const rhi::OriginalCommandBufferLease>> materialBuffers_;
    core::pmr::Vector<ScenePbrMaterialBinding> bindings_;
};
} // namespace arp::renderer
