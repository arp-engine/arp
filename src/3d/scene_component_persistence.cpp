#include "3d/scene_component_persistence.h"

#include "3d/authoring_components.h"
#include "3d/components.h"

#include <cmath>

namespace arp::scriptvm {
class ModuleRegistration;
}

namespace arp::reflection::generated {
bool register_scene3d_authoring_reflection(arp::scriptvm::ModuleRegistration*);
}

namespace arp::scene3d {
namespace {
bool finite(core::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool color(core::Vec3 v)
{
    return finite(v) && v.x >= 0 && v.y >= 0 && v.z >= 0;
}

bool direction(core::Vec3 v)
{
    return finite(v) && std::isfinite(core::dot(v, v)) && core::dot(v, v) > 0;
}

bool lightValid(const LightComponentDesc& d)
{
    return (d.type == LightType::Directional || d.type == LightType::Point
               || d.type == LightType::Spot)
        && color(d.color) && direction(d.direction) && std::isfinite(d.intensity)
        && d.intensity >= 0 && std::isfinite(d.range) && d.range >= 0 && std::isfinite(d.decay)
        && d.decay >= 0 && std::isfinite(d.spotConeAngle) && d.spotConeAngle > 0
        && d.spotConeAngle < std::acos(-1.0f) && std::isfinite(d.spotPenumbra)
        && d.spotPenumbra >= 0 && d.spotPenumbra <= 1;
}

bool skyValid(const SkyEditorProperties& d)
{
    return direction(d.sunDirection) && color(d.zenith) && color(d.horizon) && color(d.sunColor)
        && std::isfinite(d.domeRadius) && d.domeRadius > 0;
}
} // namespace

bool registerScene3DPersistence(scene::SceneComponentFactory& factory)
{
    static const bool schema =
        reflection::generated::register_scene3d_authoring_reflection(nullptr);
    if (!schema)
        return false;
    const auto add = [&](const char* type, scene::SceneComponentFactory::Create create) {
        return factory.contains(type) || factory.add(type, std::move(create));
    };
    return add("MeshComponent",
               [] {
                   return core::makeUnique<MeshComponent>();
               })
        && add("LightComponent",
            [] {
                return core::makeUnique<LightComponent>();
            })
        && add("SkyComponent",
            [] {
                return core::makeUnique<SkyComponent>();
            })
        && factory.persistValue<MeshComponent, MeshComponentEditorProperties>("arp.mesh/1",
            "MeshComponent",
            [](const MeshComponentEditorProperties& p) {
                return std::isfinite(p.baseColor.x) && std::isfinite(p.baseColor.y)
                    && std::isfinite(p.baseColor.z) && std::isfinite(p.baseColor.w);
            })
        && factory.persistValue<LightComponent, LightComponentDesc>(
            "arp.light/1", "LightComponent", lightValid)
        && factory.persistValue<SkyComponent, SkyEditorProperties>(
            "arp.sky/1", "SkyComponent", skyValid);
}
} // namespace arp::scene3d
