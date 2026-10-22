#pragma once

#include "assets/cloud/cloud_asset.h"
#include "resource/resource_manager.h"

namespace arp::resource {

struct CloudAssetResourceDesc {
    ResourceReference reference;
    core::pmr::String cookedPath;
    std::size_t maxDocumentBytes = 0u;
    std::size_t maxPayloadBytes = 0u;
};

// Cooked-only loader: no generation, filename heuristics or partial publication.
class ARP_API CloudAssetResource final : public ResourceBase {
public:
    explicit CloudAssetResource(CloudAssetResourceDesc desc)
        : ResourceBase(desc.reference), desc_(std::move(desc))
    {}

    static constexpr const char* resourceTypeName() noexcept { return "CloudAssetResource"; }

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;
    bool collectFileReadIntents(ResourceManager& manager,
        core::pmr::Vector<ResourceFileReadIntent>& intents,
        core::pmr::String& diagnostic) const override;

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::YamlParse;
    }

    std::size_t processedByteCount() const noexcept override;
    void appendContentFingerprint(core::StableHash64& hash) const noexcept override;

    const std::shared_ptr<const assets::cloud::CloudAsset>& asset() const noexcept
    {
        return asset_;
    }

private:
    CloudAssetResourceDesc desc_;
    std::shared_ptr<const assets::cloud::CloudAsset> asset_;
    std::uint64_t contentFingerprint_ = 0u;
};
} // namespace arp::resource
