#pragma once

#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "scene/component.h"

#include <cstdint>

namespace arp::scene {

struct SceneRuntimeProfileState {
    std::uint32_t worldSeed = 0u;
    bool server = false;
    bool supportsRendering = true;
    bool supportsPhysics = true;
    bool shadowsEnabled = false;
    bool prototypeMode = false;
    bool minimalBaseline = false;
    bool environmentZoneConfigured = false;
    bool spawnPointAvailable = false;
    core::Vec3 spawnPoint{};
    core::Vec3 lightingKeyDirection{0.3f, -0.8f, -0.5f};
    float lightingKeyIntensity = 1.2f;
    core::Vec3 lightingColor{1.0f, 0.95f, 0.9f};
    float lightingAmbientFactor = 0.05f;
    core::pmr::Vector<core::pmr::String> creatureNames{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> creatureHostile{&core::defaultCpuMemoryResource()};
};

/// Read-only runtime facts and authoritative content roster supplied by the
/// candidate world. It contains no WOCC phase methods; scripts only read
/// capabilities and use the returned facts to author real scene objects.
ACLASS(Name = "SceneRuntimeProfileComponent", Ownership = Engine, Lifecycle = Explicit)

class SceneRuntimeProfileComponent final : public Component {
public:
    const char* scriptTypeName() const override { return "SceneRuntimeProfileComponent"; }

    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }

    AFUNC()

    std::uint32_t world_seed() const { return state_.worldSeed; }
    AFUNC()

    bool is_server() const { return state_.server; }
    AFUNC()

    bool supports_rendering() const { return state_.supportsRendering; }
    AFUNC()

    bool supports_physics() const { return state_.supportsPhysics; }
    AFUNC()

    bool shadows_enabled() const { return state_.shadowsEnabled; }
    AFUNC()

    bool prototype_mode() const { return state_.prototypeMode; }
    AFUNC()

    bool minimal_baseline() const { return state_.minimalBaseline; }
    AFUNC()

    bool environment_zone_configured() const { return state_.environmentZoneConfigured; }
    AFUNC()

    bool spawn_point_available() const { return state_.spawnPointAvailable; }
    AFUNC()

    float spawn_point_x() const { return state_.spawnPoint.x; }
    AFUNC()

    float spawn_point_y() const { return state_.spawnPoint.y; }
    AFUNC()

    float spawn_point_z() const { return state_.spawnPoint.z; }
    AFUNC()

    float lighting_key_direction_x() const { return state_.lightingKeyDirection.x; }
    AFUNC()

    float lighting_key_direction_y() const { return state_.lightingKeyDirection.y; }
    AFUNC()

    float lighting_key_direction_z() const { return state_.lightingKeyDirection.z; }
    AFUNC()

    float lighting_key_intensity() const { return state_.lightingKeyIntensity; }
    AFUNC()

    float lighting_color_r() const { return state_.lightingColor.x; }
    AFUNC()

    float lighting_color_g() const { return state_.lightingColor.y; }
    AFUNC()

    float lighting_color_b() const { return state_.lightingColor.z; }
    AFUNC()

    float lighting_ambient_factor() const { return state_.lightingAmbientFactor; }

    AFUNC()

    std::int32_t creature_count() const
    {
        return static_cast<std::int32_t>(state_.creatureNames.size());
    }
    AFUNC()

    core::pmr::String creature_name(std::int32_t index) const
    {
        return index >= 0 && static_cast<std::size_t>(index) < state_.creatureNames.size()
            ? core::pmr::String(state_.creatureNames[static_cast<std::size_t>(index)],
                  &core::defaultCpuMemoryResource())
            : core::pmr::String(&core::defaultCpuMemoryResource());
    }
    AFUNC()

    bool creature_hostile(std::int32_t index) const
    {
        return index >= 0 && static_cast<std::size_t>(index) < state_.creatureHostile.size()
            && state_.creatureHostile[static_cast<std::size_t>(index)] != 0u;
    }

    void initializeRuntime(std::uint32_t worldSeed,
        bool server,
        bool supportsRendering,
        bool supportsPhysics,
        bool shadowsEnabled,
        bool prototypeMode,
        bool minimalBaseline,
        bool environmentZoneConfigured,
        bool spawnPointAvailable,
        const core::Vec3& spawnPoint,
        const core::Vec3& lightingKeyDirection,
        float lightingKeyIntensity,
        const core::Vec3& lightingColor,
        float lightingAmbientFactor)
    {
        state_.worldSeed = worldSeed;
        state_.server = server;
        state_.supportsRendering = supportsRendering;
        state_.supportsPhysics = supportsPhysics;
        state_.shadowsEnabled = shadowsEnabled;
        state_.prototypeMode = prototypeMode;
        state_.minimalBaseline = minimalBaseline;
        state_.environmentZoneConfigured = environmentZoneConfigured;
        state_.spawnPointAvailable = spawnPointAvailable;
        state_.spawnPoint = spawnPoint;
        state_.lightingKeyDirection = lightingKeyDirection;
        state_.lightingKeyIntensity = lightingKeyIntensity;
        state_.lightingColor = lightingColor;
        state_.lightingAmbientFactor = lightingAmbientFactor;
    }

    void addCreatureRuntimeInfo(const core::pmr::String& name, bool hostile)
    {
        state_.creatureNames.push_back(core::pmr::String(name, &core::defaultCpuMemoryResource()));
        state_.creatureHostile.push_back(hostile ? 1u : 0u);
    }

    void resetCreatureRuntimeInfo()
    {
        state_.creatureNames.clear();
        state_.creatureHostile.clear();
    }

private:
    SceneRuntimeProfileState state_;
};

} // namespace arp::scene
