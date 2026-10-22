#pragma once

#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "scene/component.h"

#include <cmath>
#include <cstdint>

namespace arp::scene3d {

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct TerrainAuthoringEditorProperties {
    APROP(Serialize, Editor)
    float halfExtent = 300.0f;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct WaterSurfaceEditorProperties {
    APROP(Serialize, Editor)
    std::int32_t segments = 128;
    APROP(Serialize, Editor)
    float waterLevel = 4.0f;
    APROP(Serialize, Editor)
    float uvScale = 4.0f;
    APROP(Serialize, Editor)
    float shoreMargin = 0.5f;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct RoadNetworkEditorProperties {
    APROP(Serialize, Editor)
    bool generationEnabled = false;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CritterFieldEditorProperties {
    APROP(Serialize, Editor)
    float worldMaxX = 300.0f;
    APROP(Serialize, Editor)
    float worldMinZ = -300.0f;
    APROP(Serialize, Editor)
    float worldMaxZ = 300.0f;
    APROP(Serialize, Editor)
    float waterLevel = 4.0f;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct SkyEditorProperties {
    APROP(Serialize, Editor)
    core::Vec3 sunDirection{90.0f, 140.0f, 30.0f};
    APROP(Serialize, Editor)
    core::Vec3 zenith{0.35f, 0.47f, 0.60f};
    APROP(Serialize, Editor)
    core::Vec3 horizon{0.82f, 0.85f, 0.88f};
    APROP(Serialize, Editor)
    core::Vec3 sunColor{0.92f, 0.90f, 0.85f};
    APROP(Serialize, Editor)
    float domeRadius = 560.0f;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CampfireEditorProperties {
    APROP(Serialize, Editor)
    core::Vec3 position{};
    APROP(Serialize, Editor)
    core::Vec3 particleOrigin{};
};

ACLASS(Name = "TerrainAuthoringComponent", Ownership = Engine, Lifecycle = Explicit)

class TerrainAuthoringComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "TerrainAuthoringComponent"; }
    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }
    AFUNC()

    float half_extent() const { return halfExtent_; }
    AFUNC()

    void set_half_extent(float value) { halfExtent_ = value; }
    AFUNC()

    core::pmr::String resource_key() const
    {
        return core::pmr::String(resourceKey_, &core::defaultCpuMemoryResource());
    }
    AFUNC()

    void set_resource_key(const core::pmr::String& value) { resourceKey_ = value; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(TerrainAuthoringEditorProperties{halfExtent_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<TerrainAuthoringEditorProperties>())
            return false;
        const float halfExtent = value.cast<TerrainAuthoringEditorProperties>().halfExtent;
        if (!std::isfinite(halfExtent) || halfExtent <= 0.0f)
            return false;
        halfExtent_ = halfExtent;
        return true;
    }

private:
    float halfExtent_ = 300.0f;
    core::pmr::String resourceKey_{&core::defaultCpuMemoryResource()};
};

ACLASS(Name = "WaterSurfaceComponent", Ownership = Engine, Lifecycle = Explicit)

class WaterSurfaceComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "WaterSurfaceComponent"; }
    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }
    AFUNC()

    std::int32_t segments() const { return segments_; }
    AFUNC()

    float water_level() const { return waterLevel_; }
    AFUNC()

    float uv_scale() const { return uvScale_; }
    AFUNC()

    float shore_margin() const { return shoreMargin_; }
    AFUNC()

    void set_parameters(std::int32_t segments, float waterLevel, float uvScale, float shoreMargin)
    {
        segments_ = segments;
        waterLevel_ = waterLevel;
        uvScale_ = uvScale;
        shoreMargin_ = shoreMargin;
    }
    AFUNC()

    core::pmr::String resource_key() const
    {
        return core::pmr::String(resourceKey_, &core::defaultCpuMemoryResource());
    }
    AFUNC()

    void set_resource_key(const core::pmr::String& value) { resourceKey_ = value; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(
            WaterSurfaceEditorProperties{segments_, waterLevel_, uvScale_, shoreMargin_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<WaterSurfaceEditorProperties>())
            return false;
        const auto& properties = value.cast<const WaterSurfaceEditorProperties&>();
        set_parameters(
            properties.segments, properties.waterLevel, properties.uvScale, properties.shoreMargin);
        return true;
    }

private:
    std::int32_t segments_ = 128;
    float waterLevel_ = 4.0f;
    float uvScale_ = 4.0f;
    float shoreMargin_ = 0.5f;
    core::pmr::String resourceKey_{&core::defaultCpuMemoryResource()};
};

ACLASS(Name = "RoadNetworkComponent", Ownership = Engine, Lifecycle = Explicit)

class RoadNetworkComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "RoadNetworkComponent"; }
    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }
    AFUNC()

    bool generation_enabled() const { return generationEnabled_; }
    AFUNC()

    void set_generation_enabled(bool value) { generationEnabled_ = value; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(RoadNetworkEditorProperties{generationEnabled_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<RoadNetworkEditorProperties>())
            return false;
        generationEnabled_ = value.cast<RoadNetworkEditorProperties>().generationEnabled;
        return true;
    }

private:
    bool generationEnabled_ = false;
};

ACLASS(Name = "CritterFieldComponent", Ownership = Engine, Lifecycle = Explicit)

class CritterFieldComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "CritterFieldComponent"; }
    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }
    AFUNC()

    void set_bounds(float worldMaxX, float worldMinZ, float worldMaxZ, float waterLevel)
    {
        worldMaxX_ = worldMaxX;
        worldMinZ_ = worldMinZ;
        worldMaxZ_ = worldMaxZ;
        waterLevel_ = waterLevel;
    }
    AFUNC()

    float world_max_x() const { return worldMaxX_; }
    AFUNC()

    float world_min_z() const { return worldMinZ_; }
    AFUNC()

    float world_max_z() const { return worldMaxZ_; }
    AFUNC()

    float water_level() const { return waterLevel_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(
            CritterFieldEditorProperties{worldMaxX_, worldMinZ_, worldMaxZ_, waterLevel_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<CritterFieldEditorProperties>())
            return false;
        const auto& properties = value.cast<const CritterFieldEditorProperties&>();
        if (!std::isfinite(properties.worldMaxX) || properties.worldMaxX <= 0.0f
            || !std::isfinite(properties.worldMinZ) || !std::isfinite(properties.worldMaxZ)
            || properties.worldMinZ >= properties.worldMaxZ
            || !std::isfinite(properties.waterLevel))
            return false;
        set_bounds(properties.worldMaxX,
            properties.worldMinZ,
            properties.worldMaxZ,
            properties.waterLevel);
        return true;
    }

private:
    float worldMaxX_ = 300.0f;
    float worldMinZ_ = -300.0f;
    float worldMaxZ_ = 300.0f;
    float waterLevel_ = 4.0f;
};

ACLASS(Name = "SkyComponent", Ownership = Engine, Lifecycle = Explicit)

class SkyComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "SkyComponent"; }
    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }
    AFUNC()

    core::pmr::String resource_key() const
    {
        return core::pmr::String(resourceKey_, &core::defaultCpuMemoryResource());
    }
    AFUNC()

    void set_resource_key(const core::pmr::String& value) { resourceKey_ = value; }
    AFUNC()

    void set_sun_direction(float x, float y, float z) { sunDirection_ = {x, y, z}; }
    AFUNC()

    void set_zenith_color(float r, float g, float b) { zenith_ = {r, g, b}; }
    AFUNC()

    void set_horizon_color(float r, float g, float b) { horizon_ = {r, g, b}; }
    AFUNC()

    void set_sun_color(float r, float g, float b) { sunColor_ = {r, g, b}; }
    AFUNC()

    void set_dome_radius(float value) { domeRadius_ = value; }
    AFUNC()

    core::Vec3 sun_direction() const { return sunDirection_; }
    AFUNC()

    core::Vec3 zenith_color() const { return zenith_; }
    AFUNC()

    core::Vec3 horizon_color() const { return horizon_; }
    AFUNC()

    core::Vec3 sun_color() const { return sunColor_; }
    AFUNC()

    float dome_radius() const { return domeRadius_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(
            SkyEditorProperties{sunDirection_, zenith_, horizon_, sunColor_, domeRadius_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<SkyEditorProperties>())
            return false;
        const auto& properties = value.cast<const SkyEditorProperties&>();
        set_sun_direction(
            properties.sunDirection.x, properties.sunDirection.y, properties.sunDirection.z);
        set_zenith_color(properties.zenith.x, properties.zenith.y, properties.zenith.z);
        set_horizon_color(properties.horizon.x, properties.horizon.y, properties.horizon.z);
        set_sun_color(properties.sunColor.x, properties.sunColor.y, properties.sunColor.z);
        set_dome_radius(properties.domeRadius);
        return true;
    }

private:
    core::pmr::String resourceKey_{&core::defaultCpuMemoryResource()};
    core::Vec3 sunDirection_{90.0f, 140.0f, 30.0f};
    core::Vec3 zenith_{0.35f, 0.47f, 0.60f};
    core::Vec3 horizon_{0.82f, 0.85f, 0.88f};
    core::Vec3 sunColor_{0.92f, 0.90f, 0.85f};
    float domeRadius_ = 560.0f;
};

ACLASS(Name = "CampfireComponent", Ownership = Engine, Lifecycle = Explicit)

class CampfireComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "CampfireComponent"; }
    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }
    AFUNC()

    void set_position(float x, float y, float z) { position_ = {x, y, z}; }
    AFUNC()

    void set_particle_origin(float x, float y, float z) { particleOrigin_ = {x, y, z}; }
    AFUNC()

    core::Vec3 position() const { return position_; }
    AFUNC()

    core::Vec3 particle_origin() const { return particleOrigin_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(CampfireEditorProperties{position_, particleOrigin_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<CampfireEditorProperties>())
            return false;
        const auto& properties = value.cast<const CampfireEditorProperties&>();
        set_position(properties.position.x, properties.position.y, properties.position.z);
        set_particle_origin(
            properties.particleOrigin.x, properties.particleOrigin.y, properties.particleOrigin.z);
        return true;
    }

private:
    core::Vec3 position_{};
    core::Vec3 particleOrigin_{};
};

} // namespace arp::scene3d
