#include "renderer/volumetric_cloud/cloud_scene_binding.h"

namespace arp::renderer::cloud {
namespace {

CloudResult validateQueue(const scene3d::Scene3DRenderQueue& queue, std::size_t maxBytes)
{
    if (!queue.hasCloud())
        return CloudResult::failure("CLOUD-SCENE-CLOUD-ABSENT");
    if (const auto* id = scene3d::CloudLayerValidator::diagnostic(queue.cloud().layer))
        return CloudResult::failure(id);
    if (queue.cloud().densityResource.empty() || queue.cloud().densityResource.size() > maxBytes)
        return CloudResult::failure("CLOUD-SCENE-REFERENCE-BUDGET");
    return {};
}

CloudResult resolve(const resource::ResourceManager& resources,
    CloudSceneReferenceMode mode,
    const core::pmr::String& reference,
    core::pmr::String& resourceId)
{
    if (mode == CloudSceneReferenceMode::ResourceId) {
        if (!resource::isResourceUuid(reference))
            return CloudResult::failure("CLOUD-SCENE-RESOURCE-ID");
        resourceId = reference;
    } else {
        const auto resolved = resources.resolveAlias(reference);
        if (!resolved.success)
            return CloudResult::failure("CLOUD-SCENE-REFERENCE-UNRESOLVED");
        resourceId = resolved.resourceId;
    }
    return {};
}

CloudResult ready(const resource::ResourceManager& resources,
    const core::pmr::String& id,
    resource::ResourceHandle<resource::CloudAssetResource>& handle)
{
    if (resources.stateOf(id) != resource::ResourceState::Ready)
        return CloudResult::failure("CLOUD-SCENE-RESOURCE-NOT-READY");
    handle = resources.find<resource::CloudAssetResource>(id);
    if (!handle.ready()
        || handle.get()->reference().type != resource::CloudAssetResource::resourceTypeName())
        return CloudResult::failure("CLOUD-SCENE-RESOURCE-TYPE");
    if (!handle.get()->asset())
        return CloudResult::failure("CLOUD-SCENE-RESOURCE-PAYLOAD");
    return {};
}

} // namespace

CloudResult CloudSceneBinding::create(const scene3d::Scene3DRenderQueue& queue,
    const resource::ResourceManager& resources,
    const CloudSceneBindingDesc& desc,
    std::shared_ptr<const CloudSceneBinding>& output)
try {
    if (desc.contractVersion != 1u || !desc.prototypeOptIn)
        return CloudResult::failure("CLOUD-SCENE-BINDING-OPT-IN");
    if (desc.referenceMode != CloudSceneReferenceMode::ResourceId
        && desc.referenceMode != CloudSceneReferenceMode::Alias)
        return CloudResult::failure("CLOUD-SCENE-REFERENCE-MODE");
    if (!desc.maxReferenceBytes || !desc.maxCpuBytes || !desc.quota
        || desc.quota->limitBytes() != desc.maxCpuBytes)
        return CloudResult::failure("CLOUD-SCENE-BINDING-QUOTA");
    if (const auto result = validateQueue(queue, desc.maxReferenceBytes); !result.success)
        return result;
    core::CpuMemoryQuotaScope scope(desc.quota);
    core::pmr::String id;
    if (const auto result =
            resolve(resources, desc.referenceMode, queue.cloud().densityResource, id);
        !result.success)
        return result;
    if (id.size() > desc.maxReferenceBytes)
        return CloudResult::failure("CLOUD-SCENE-REFERENCE-BUDGET");
    resource::ResourceHandle<resource::CloudAssetResource> handle;
    if (const auto result = ready(resources, id, handle); !result.success)
        return result;
    auto next = core::makeShared<CloudSceneBinding>();
    next->quota_ = desc.quota;
    next->maxCpuBytes_ = desc.maxCpuBytes;
    next->maxReferenceBytes_ = desc.maxReferenceBytes;
    next->mode_ = desc.referenceMode;
    next->ownerThread_ = std::this_thread::get_id();
    next->reference_ = queue.cloud().densityResource;
    next->resourceId_ = std::move(id);
    next->generation_ = handle.generation();
    next->asset_ = handle.get()->asset();
    output = std::move(next);
    return {};
} catch (const std::bad_alloc&) {
    return CloudResult::failure("CLOUD-SCENE-BINDING-BUDGET");
}

CloudResult CloudSceneBinding::validateCurrent(
    const scene3d::Scene3DRenderQueue& queue, const resource::ResourceManager& resources) const
try {
    if (!asset_)
        return CloudResult::failure("CLOUD-SCENE-BINDING-EMPTY");
    if (ownerThread_ != std::this_thread::get_id())
        return CloudResult::failure("CLOUD-SCENE-BINDING-OWNER-THREAD");
    if (!quota_ || quota_->limitBytes() != maxCpuBytes_)
        return CloudResult::failure("CLOUD-SCENE-BINDING-QUOTA");
    if (const auto result = validateQueue(queue, maxReferenceBytes_); !result.success)
        return result;
    if (queue.cloud().densityResource != reference_)
        return CloudResult::failure("CLOUD-SCENE-REFERENCE-CHANGED");
    core::CpuMemoryQuotaScope scope(quota_);
    core::pmr::String id;
    if (const auto result = resolve(resources, mode_, reference_, id); !result.success)
        return result;
    if (id != resourceId_)
        return CloudResult::failure("CLOUD-SCENE-ALIAS-CHANGED");
    resource::ResourceHandle<resource::CloudAssetResource> handle;
    if (const auto result = ready(resources, id, handle); !result.success)
        return result;
    if (handle.generation() != generation_)
        return CloudResult::failure("CLOUD-SCENE-RESOURCE-GENERATION");
    const auto& current = handle.get()->asset();
    if (current.get() != asset_.get() || current.owner_before(asset_)
        || asset_.owner_before(current))
        return CloudResult::failure("CLOUD-SCENE-RESOURCE-OWNER");
    return {};
} catch (const std::bad_alloc&) {
    return CloudResult::failure("CLOUD-SCENE-BINDING-BUDGET");
}

} // namespace arp::renderer::cloud
