#pragma once

#include "3d/scene_3d_render_queue.h"
#include "renderer/volumetric_cloud/cloud_contract.h"
#include "resource/cloud_asset_resource.h"

#include <thread>

namespace arp::renderer::cloud {

enum class CloudSceneReferenceMode { Unspecified, ResourceId, Alias };

struct CloudSceneBindingDesc {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    CloudSceneReferenceMode referenceMode = CloudSceneReferenceMode::Unspecified;
    std::size_t maxReferenceBytes = 0u;
    std::uint64_t maxCpuBytes = 0u;
    std::shared_ptr<core::CpuMemoryQuota> quota;
};

// Immutable renderer-domain snapshot. Registry access is cold, owner-thread
// serialized; no borrowed manager/record/handle is retained. See binding /1.
class ARP_API CloudSceneBinding final {
public:
    CloudSceneBinding() = default;
    CloudSceneBinding(const CloudSceneBinding&) = delete;
    CloudSceneBinding& operator=(const CloudSceneBinding&) = delete;

    // Failure preserves the caller's previous output and registry state.
    static CloudResult create(const scene3d::Scene3DRenderQueue&,
        const resource::ResourceManager&,
        const CloudSceneBindingDesc&,
        std::shared_ptr<const CloudSceneBinding>& output);
    CloudResult validateCurrent(
        const scene3d::Scene3DRenderQueue&, const resource::ResourceManager&) const;

    const std::shared_ptr<const assets::cloud::CloudAsset>& asset() const noexcept
    {
        return asset_;
    }

    const core::pmr::String& resourceId() const noexcept { return resourceId_; }

    const core::pmr::String& authoredReference() const noexcept { return reference_; }

    resource::ResourceGeneration generation() const noexcept { return generation_; }

private:
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::uint64_t maxCpuBytes_ = 0u;
    std::size_t maxReferenceBytes_ = 0u;
    CloudSceneReferenceMode mode_ = CloudSceneReferenceMode::Unspecified;
    std::thread::id ownerThread_;
    core::pmr::String reference_, resourceId_;
    resource::ResourceGeneration generation_ = 0u;
    std::shared_ptr<const assets::cloud::CloudAsset> asset_;
};

} // namespace arp::renderer::cloud
