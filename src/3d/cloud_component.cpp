#include "3d/cloud_component.h"

namespace arp::scene3d {

bool VolumetricCloudComponent::set_cloud(const CloudDesc& desc)
{
    if (CloudLayerValidator::diagnostic(desc.layer) || desc.densityResource.empty())
        return false;
    desc_ = desc;
    return true;
}

bool VolumetricCloudComponent::applyEditorAuthoringProperties(const reflection::object& value)
{
    return value.is<CloudDesc>() && set_cloud(value.cast<const CloudDesc&>());
}

void VolumetricCloudComponent::extract(
    const scene::RenderExtractContext& context, scene::RenderExtractSink& sink) const
{
    scene::RenderExtractPayload payload;
    payload.kind = scene::RenderExtractKind::Custom;
    payload.typeId =
        context.activeInHierarchy && enabled() ? "scene3d.volumetric_cloud" : "scene3d.disabled";
    payload.data = &desc_;
    sink.accept(context, payload);
}

} // namespace arp::scene3d
