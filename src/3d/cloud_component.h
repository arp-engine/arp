#pragma once

#include "3d/cloud_types.h"
#include "core/api.h"
#include "scene/component.h"

namespace arp::scene3d {

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CloudDesc {
    APROP(Serialize, Editor)
    CloudLayerDesc layer;
    APROP(Serialize, Editor)
    core::pmr::String densityResource;
};

// Global world-space shell: GameObject transforms do not modify this explicit
// planet frame or silently change physical units.
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class ARP_API VolumetricCloudComponent final : public scene::Component {
public:
    const char* scriptTypeName() const override { return "VolumetricCloudComponent"; }

    const CloudDesc& desc() const noexcept { return desc_; }

    AFUNC()

    CloudDesc cloud() const { return desc_; }
    AFUNC()
    bool set_cloud(const CloudDesc& desc);

    reflection::object editorAuthoringProperties() const override
    {
        return reflection::object(desc_);
    }

    bool applyEditorAuthoringProperties(const reflection::object& value) override;
    void extract(
        const scene::RenderExtractContext& context, scene::RenderExtractSink& sink) const override;

private:
    CloudDesc desc_;
};

} // namespace arp::scene3d
