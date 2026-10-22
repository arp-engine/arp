#include "3d/fog_components.h"

#include <cmath>

namespace arp::scene3d {
namespace {
bool validAlbedo(core::Vec3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && v.x >= 0 && v.x <= 1
        && v.y >= 0 && v.y <= 1 && v.z >= 0 && v.z <= 1;
}

void extractFog(const scene::Component& component,
    const scene::RenderExtractContext& ctx,
    scene::RenderExtractSink& sink,
    const char* typeId,
    const void* data)
{
    scene::RenderExtractPayload payload;
    payload.kind = scene::RenderExtractKind::Custom;
    payload.typeId = ctx.activeInHierarchy && component.enabled() ? typeId : "scene3d.disabled";
    payload.data = data;
    sink.accept(ctx, payload);
}
} // namespace

bool validFog(const VolumetricFogDesc& d) noexcept
{
    return std::isfinite(d.density) && d.density >= 0 && std::isfinite(d.heightFalloff)
        && d.heightFalloff >= 0 && std::isfinite(d.baseHeight) && validAlbedo(d.scatteringColor)
        && std::isfinite(d.anisotropy) && d.anisotropy > -1 && d.anisotropy < 1;
}

bool validFog(const LocalFogVolumeDesc& d) noexcept
{
    return (d.shape == FogVolumeShape::Box || d.shape == FogVolumeShape::Sphere)
        && std::isfinite(d.density) && d.density >= 0 && validAlbedo(d.scatteringColor)
        && std::isfinite(d.edgeSoftness) && d.edgeSoftness >= 0 && d.edgeSoftness <= 1
        && std::isfinite(d.noiseScale) && d.noiseScale > 0 && std::isfinite(d.noiseStrength)
        && d.noiseStrength >= 0 && d.noiseStrength <= 1 && std::isfinite(d.noiseVelocity.x)
        && std::isfinite(d.noiseVelocity.y) && std::isfinite(d.noiseVelocity.z)
        && std::isfinite(d.noiseContrast) && d.noiseContrast >= 1;
}

bool validFogTransform(const scene::Transform& t) noexcept
{
    const float values[] = {t.translation.x,
        t.translation.y,
        t.translation.z,
        t.scale.x,
        t.scale.y,
        t.scale.z,
        t.rotation.x,
        t.rotation.y,
        t.rotation.z,
        t.rotation.w};
    for (const float value : values)
        if (!std::isfinite(value))
            return false;
    return t.scale.x != 0 && t.scale.y != 0 && t.scale.z != 0
        && std::isfinite(core::dot(t.rotation, t.rotation))
        && core::dot(t.rotation, t.rotation) > 0;
}

bool VolumetricFogComponent::set_fog(const VolumetricFogDesc& d)
{
    if (!validFog(d))
        return false;
    desc_ = d;
    return true;
}

bool LocalFogVolumeComponent::set_fog(const LocalFogVolumeDesc& d)
{
    if (!validFog(d))
        return false;
    desc_ = d;
    return true;
}

bool VolumetricFogComponent::applyEditorAuthoringProperties(const reflection::object& v)
{
    return v.is<VolumetricFogDesc>() && set_fog(v.cast<const VolumetricFogDesc&>());
}

bool LocalFogVolumeComponent::applyEditorAuthoringProperties(const reflection::object& v)
{
    return v.is<LocalFogVolumeDesc>() && set_fog(v.cast<const LocalFogVolumeDesc&>());
}

void VolumetricFogComponent::extract(
    const scene::RenderExtractContext& ctx, scene::RenderExtractSink& sink) const
{
    extractFog(*this, ctx, sink, "scene3d.volumetric_fog", &desc_);
}

void LocalFogVolumeComponent::extract(
    const scene::RenderExtractContext& ctx, scene::RenderExtractSink& sink) const
{
    extractFog(*this, ctx, sink, "scene3d.local_fog", &desc_);
}

} // namespace arp::scene3d
