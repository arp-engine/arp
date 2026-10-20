#pragma once

#include "3d/bounds.h"
#include "core/cpu_allocator.h"
#include "core/math.h"
#include "reflection/reflection_macros.h"
#include "renderer/runtime_scene.h"
#include "scene/component.h"
#include "scene/render_extract.h"

#include <cstdint>
#include <utility>

namespace animation {

struct AnimationClip;
struct AnimatorControllerAsset;

} // namespace animation

namespace arp::scene3d {

struct TerrainHandle {
    uint32_t id = 0;

    bool valid() const { return id != 0; }

    friend bool operator==(TerrainHandle lhs, TerrainHandle rhs) { return lhs.id == rhs.id; }

    friend bool operator!=(TerrainHandle lhs, TerrainHandle rhs) { return !(lhs == rhs); }
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct TerrainMetadata {
    APROP(Serialize, Editor)
    uint32_t tileCountX = 0;
    APROP(Serialize, Editor)
    uint32_t tileCountZ = 0;
    APROP(Serialize, Editor)
    float metersPerTile = 0.0f;
    APROP(Serialize)
    bool hasExplicitMetadata = false;

    static TerrainMetadata explicitGrid(
        uint32_t tileCountX, uint32_t tileCountZ, float metersPerTile);
    bool valid() const;
};

// Native render descriptors intentionally remain outside the script ABI. They
// carry renderer-owned handles and are produced only after authored resource
// ids have been resolved by the owning native scene systems.
struct MeshComponentDesc {
    arp::renderer::MeshHandle mesh;
    arp::renderer::MaterialHandle material;
    Bounds3D localBounds;
    arp::core::Vec4 baseColorTint{1.0f, 1.0f, 1.0f, 1.0f};
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct MeshComponentEditorProperties {
    APROP(Editor, Doc = "resource:MeshAssetResource:optional")
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Editor, Doc = "resource:MaterialResource:optional")
    arp::core::pmr::String materialResourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Editor, Doc = "color:rgba")
    arp::core::Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct TerrainComponentEditorProperties {
    APROP(Editor, Doc = "resource:TerrainAssetResource:optional")
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
};

struct TerrainComponentDesc {
    // Native-only resolved state. TerrainComponent serializes resourceId_; the
    // scene preparation layer owns resolving it into these renderer handles.
    TerrainHandle terrain;
    arp::renderer::MaterialHandle material;
    Bounds3D localBounds;
    TerrainMetadata metadata;
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class MeshComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "MeshComponent"; }

    MeshComponent() = default;
    explicit MeshComponent(MeshComponentDesc desc);

    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }

    const MeshComponentDesc& desc() const { return desc_; }

    void setDesc(MeshComponentDesc desc) { desc_ = std::move(desc); }

    void setMesh(arp::renderer::MeshHandle mesh) { desc_.mesh = mesh; }

    void setMaterial(arp::renderer::MaterialHandle material) { desc_.material = material; }

    void setLocalBounds(Bounds3D bounds) { desc_.localBounds = bounds; }

    void setBaseColorTint(arp::core::Vec4 tint) { desc_.baseColorTint = tint; }

    const arp::core::pmr::String& resourceId() const { return resourceId_; }

    const arp::core::pmr::String& materialResourceId() const { return materialResourceId_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        MeshComponentEditorProperties properties;
        properties.resourceId = resourceId_;
        properties.materialResourceId = materialResourceId_;
        properties.baseColor = desc_.baseColorTint;
        return arp::reflection::object(std::move(properties));
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override;

    void setBaseColor(float r, float g, float b, float a) { setBaseColorTint({r, g, b, a}); }

    // Script-safe component surface. Renderer handles remain private to the
    // engine; scripts manipulate authored values and query residency only.
    AFUNC()

    Bounds3D local_bounds() const { return desc_.localBounds; }
    AFUNC()

    void set_local_bounds(const Bounds3D& bounds) { desc_.localBounds = bounds; }
    AFUNC()

    arp::core::Vec4 base_color_tint() const { return desc_.baseColorTint; }
    AFUNC()

    void set_base_color_tint(const arp::core::Vec4& tint) { setBaseColorTint(tint); }
    AFUNC()

    bool has_mesh() const { return desc_.mesh.valid(); }
    AFUNC()

    bool has_material() const { return desc_.material.valid(); }
    AFUNC()

    arp::core::pmr::String resource_id() const
    {
        return arp::core::pmr::String(resourceId_, &arp::core::defaultCpuMemoryResource());
    }
    AFUNC()

    void set_resource_id(const arp::core::pmr::String& value) { resourceId_ = value; }
    AFUNC()

    arp::core::pmr::String material_resource_id() const
    {
        return arp::core::pmr::String(materialResourceId_, &arp::core::defaultCpuMemoryResource());
    }
    AFUNC()

    void set_material_resource_id(const arp::core::pmr::String& value)
    {
        materialResourceId_ = value;
    }

    /// Self-describing render extraction: pushes a `scene3d.mesh` Custom payload
    /// carrying the mesh/material handles and local bounds. The 3D extractor's
    /// sink interprets this typeId and validates the handles/bounds.
    void extract(const arp::scene::RenderExtractContext& ctx,
        arp::scene::RenderExtractSink& sink) const override;

private:
    MeshComponentDesc desc_;
    // Stable authored ids stay in the component; native render handles are
    // resolved by the owning scene after resource preparation.
    arp::core::pmr::String resourceId_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String materialResourceId_{&arp::core::defaultCpuMemoryResource()};
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class TerrainComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "TerrainComponent"; }

    TerrainComponent() = default;
    explicit TerrainComponent(TerrainComponentDesc desc);

    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }

    const TerrainComponentDesc& desc() const { return desc_; }

    void setDesc(TerrainComponentDesc desc) { desc_ = std::move(desc); }

    const arp::core::pmr::String& resourceId() const { return resourceId_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(TerrainComponentEditorProperties{resourceId_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override;

    void bindResolvedTerrain(TerrainComponentDesc desc) { desc_ = std::move(desc); }

    void clearResolvedTerrain() { desc_ = TerrainComponentDesc{}; }

    AFUNC()

    Bounds3D local_bounds() const { return desc_.localBounds; }
    AFUNC()

    void set_local_bounds(const Bounds3D& bounds) { desc_.localBounds = bounds; }
    AFUNC()

    TerrainMetadata metadata() const { return desc_.metadata; }
    AFUNC()

    bool has_terrain() const { return !resourceId_.empty() || desc_.terrain.valid(); }
    AFUNC()

    arp::core::pmr::String resource_id() const
    {
        return arp::core::pmr::String(resourceId_, &arp::core::defaultCpuMemoryResource());
    }
    AFUNC()

    void set_resource_id(const arp::core::pmr::String& value) { resourceId_ = value; }

    /// Self-describing render extraction: pushes a `scene3d.terrain` Custom
    /// payload carrying terrain/material handles, local bounds and metadata.
    void extract(const arp::scene::RenderExtractContext& ctx,
        arp::scene::RenderExtractSink& sink) const override;

private:
    TerrainComponentDesc desc_;
    // The only authored terrain identity. Renderer handles and bounds are
    // runtime data resolved from TerrainAssetResource, never scene data.
    arp::core::pmr::String resourceId_{&arp::core::defaultCpuMemoryResource()};
};

AENUM()
enum class LightType : std::uint32_t {
    Directional = 0u,
    Point = 1u,
    Spot = 2u,
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct LightComponentDesc {
    APROP(Serialize, Editor)
    LightType type = LightType::Point;
    APROP(Serialize, Editor)
    arp::core::Vec3 color{1.0f, 1.0f, 1.0f};
    APROP(Serialize, Editor)
    arp::core::Vec3 direction{0.0f, 1.0f, 0.0f};
    APROP(Serialize, Editor)
    float intensity = 1.0f;
    APROP(Serialize, Editor)
    float range = 0.0f;
    APROP(Serialize, Editor)
    float decay = 2.0f;
    APROP(Serialize, Editor)
    float spotConeAngle = 0.785398f;
    APROP(Serialize, Editor)
    float spotPenumbra = 0.2f;
    APROP(Serialize, Editor)
    bool castShadow = false;

    AFUNC()

    void set_direction(float x, float y, float z) { direction = {x, y, z}; }

    AFUNC()

    void set_color(float r, float g, float b) { color = {r, g, b}; }

    AFUNC()

    void set_range(float value) { range = value; }

    AFUNC()

    void set_decay(float value) { decay = value; }
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class LightComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "LightComponent"; }

    LightComponent() = default;
    explicit LightComponent(LightComponentDesc desc);

    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }

    const LightComponentDesc& desc() const { return desc_; }

    void setDesc(LightComponentDesc desc) { desc_ = std::move(desc); }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(desc_);
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<LightComponentDesc>())
            return false;
        setDesc(value.cast<LightComponentDesc>());
        return true;
    }

    void setType(LightType type) { desc_.type = type; }

    void setColor(arp::core::Vec3 color) { desc_.color = color; }

    void setColor(float r, float g, float b) { setColor({r, g, b}); }

    void setDirection(arp::core::Vec3 direction) { desc_.direction = direction; }

    void setDirection(float x, float y, float z) { setDirection({x, y, z}); }

    void setIntensity(float intensity) { desc_.intensity = intensity; }

    void setRange(float range) { desc_.range = range; }

    void setDecay(float decay) { desc_.decay = decay; }

    void setSpotConeAngle(float angle) { desc_.spotConeAngle = angle; }

    void setSpotPenumbra(float penumbra) { desc_.spotPenumbra = penumbra; }

    void setCastShadow(bool castShadow) { desc_.castShadow = castShadow; }

    AFUNC()

    LightType type() const { return desc_.type; }
    AFUNC()

    arp::core::Vec3 color() const { return desc_.color; }
    AFUNC()

    arp::core::Vec3 direction() const { return desc_.direction; }
    AFUNC()

    float intensity() const { return desc_.intensity; }
    AFUNC()

    float range() const { return desc_.range; }
    AFUNC()

    float decay() const { return desc_.decay; }
    AFUNC()

    float spot_cone_angle() const { return desc_.spotConeAngle; }
    AFUNC()

    float spot_penumbra() const { return desc_.spotPenumbra; }
    AFUNC()

    bool cast_shadow() const { return desc_.castShadow; }

    AFUNC()

    void set_type(LightType value) { setType(value); }
    AFUNC()

    void set_color(arp::core::Vec3 value) { setColor(value); }
    AFUNC()

    void set_color_rgb(float r, float g, float b) { setColor(r, g, b); }
    AFUNC()

    void set_direction(arp::core::Vec3 value) { setDirection(value); }
    AFUNC()

    void set_direction_xyz(float x, float y, float z) { setDirection(x, y, z); }
    AFUNC()

    void set_intensity(float value) { setIntensity(value); }
    AFUNC()

    void set_range(float value) { setRange(value); }
    AFUNC()

    void set_decay(float value) { setDecay(value); }
    AFUNC()

    void set_spot_cone_angle(float value) { setSpotConeAngle(value); }
    AFUNC()

    void set_spot_penumbra(float value) { setSpotPenumbra(value); }
    AFUNC()

    void set_cast_shadow(bool value) { setCastShadow(value); }

    void extract(const arp::scene::RenderExtractContext& ctx,
        arp::scene::RenderExtractSink& sink) const override;

private:
    LightComponentDesc desc_;
};

// ---------------------------------------------------------------------------
// Domain-specific extract payloads (carried via RenderExtractPayload::Custom).
//
// These POD structs are the contract between the 3D components' extract()
// override and the Scene3DExtractSink. They live in the 3d module (not the
// base scene layer) because they reference renderer handles and 3D bounds,
// which the base layer must not depend on. The typeId strings below are the
// deterministic contract names; sinks MUST reject unknown typeIds with a
// diagnostic (no guessing, no silent fallback).
// ---------------------------------------------------------------------------

/// typeId contract: "scene3d.mesh"
struct Scene3DMeshExtractPayload {
    arp::renderer::MeshHandle mesh;
    arp::renderer::MaterialHandle material;
    Bounds3D localBounds;
    arp::core::Vec4 baseColorTint{1.0f, 1.0f, 1.0f, 1.0f};
};

/// typeId contract: "scene3d.light"
struct Scene3DLightExtractPayload {
    LightType type = LightType::Point;
    arp::core::Vec3 color{1.0f, 1.0f, 1.0f};
    arp::core::Vec3 direction{0.0f, 1.0f, 0.0f};
    float intensity = 1.0f;
    float range = 0.0f;
    float decay = 2.0f;
    float spotConeAngle = 0.785398f;
    float spotPenumbra = 0.2f;
    bool castShadow = false;
};

/// typeId contract: "scene3d.terrain"
struct Scene3DTerrainExtractPayload {
    TerrainHandle terrain;
    arp::renderer::MaterialHandle material;
    Bounds3D localBounds;
    TerrainMetadata metadata;
};

/// typeId contract: "scene3d.animation" (defined here to keep all 3D extract
/// contracts co-located; the clip/controller types are forward-declared).
struct Scene3DAnimationExtractPayload {
    const animation::AnimationClip* clip = nullptr;
    const animation::AnimatorControllerAsset* controller = nullptr;
};

} // namespace arp::scene3d
