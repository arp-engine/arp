#pragma once

#include "core/api.h"
#include "core/math/math.h"
#include "scene/component.h"
#include "scene/render_extract.h"

namespace arp::scene3d {

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct VolumetricFogDesc {
    APROP(Serialize, Editor)
    float density = 0.01f;
    APROP(Serialize, Editor)
    float heightFalloff = 0.1f;
    APROP(Serialize, Editor)
    float baseHeight = 0.0f;
    APROP(Serialize, Editor)
    core::Vec3 scatteringColor{0.9f, 0.95f, 1.0f};
    APROP(Serialize, Editor)
    float anisotropy = 0.2f;
};

AENUM()
enum class FogVolumeShape : std::uint32_t {
    Box,
    Sphere,
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct LocalFogVolumeDesc {
    APROP(Serialize, Editor)
    FogVolumeShape shape = FogVolumeShape::Box;
    APROP(Serialize, Editor)
    float density = 0.05f;
    APROP(Serialize, Editor)
    core::Vec3 scatteringColor{0.9f, 0.95f, 1.0f};
    APROP(Serialize, Editor)
    float edgeSoftness = 0.2f;
    APROP(Serialize, Editor)
    std::uint32_t noiseSeed = 1u;
    APROP(Serialize, Editor)
    float noiseScale = 0.2f;
    APROP(Serialize, Editor)
    float noiseStrength = 0.5f;
    APROP(Serialize, Editor)
    core::Vec3 noiseVelocity{0.1f, 0.0f, 0.0f};
    // FogDensityNoiseContrast/1: pow(value noise, contrast), 1 preserves Noise3/1.
    APROP(Serialize, Editor)
    float noiseContrast = 1.0f;
};

bool validFog(const VolumetricFogDesc& desc) noexcept;
bool validFog(const LocalFogVolumeDesc& desc) noexcept;
bool validFogTransform(const scene::Transform& transform) noexcept;

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class ARP_API VolumetricFogComponent final : public scene::Component {
public:
    const char* scriptTypeName() const override { return "VolumetricFogComponent"; }

    VolumetricFogComponent() = default;

    explicit VolumetricFogComponent(VolumetricFogDesc desc) : desc_(desc) {}

    const VolumetricFogDesc& desc() const { return desc_; }
    AFUNC()

    VolumetricFogDesc fog() const { return desc_; }
    AFUNC()
    bool set_fog(const VolumetricFogDesc& desc);
    AFUNC()

    void set_enabled(bool value) { setEnabled(value); }

    reflection::object editorAuthoringProperties() const override
    {
        return reflection::object(desc_);
    }

    bool applyEditorAuthoringProperties(const reflection::object& value) override;
    void extract(
        const scene::RenderExtractContext& ctx, scene::RenderExtractSink& sink) const override;

private:
    VolumetricFogDesc desc_;
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class ARP_API LocalFogVolumeComponent final : public scene::Component {
public:
    const char* scriptTypeName() const override { return "LocalFogVolumeComponent"; }

    LocalFogVolumeComponent() = default;

    explicit LocalFogVolumeComponent(LocalFogVolumeDesc desc) : desc_(desc) {}

    const LocalFogVolumeDesc& desc() const { return desc_; }
    AFUNC()

    LocalFogVolumeDesc fog() const { return desc_; }
    AFUNC()
    bool set_fog(const LocalFogVolumeDesc& desc);
    AFUNC()

    void set_enabled(bool value) { setEnabled(value); }

    reflection::object editorAuthoringProperties() const override
    {
        return reflection::object(desc_);
    }

    bool applyEditorAuthoringProperties(const reflection::object& value) override;
    void extract(
        const scene::RenderExtractContext& ctx, scene::RenderExtractSink& sink) const override;

private:
    LocalFogVolumeDesc desc_;
};

} // namespace arp::scene3d
