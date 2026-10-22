#include "resource/cloud_asset_resource.h"

#include "resource/cloud_asset_cooked.h"
#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::resource {
bool CloudAssetResource::collectFileReadIntents(ResourceManager&,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostic) const
{
    return appendResourceReadIntent(desc_.cookedPath, desc_.maxDocumentBytes, intents, diagnostic);
}

ResourceLoadResult CloudAssetResource::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    asset_.reset();
    contentFingerprint_ = 0u;
    if (reference().type != resourceTypeName() || desc_.cookedPath.empty()
        || !desc_.maxDocumentBytes || !desc_.maxPayloadBytes)
        return resourceLoadFailure(*this, "CLOUD-RESOURCE-CONFIG");
    const auto source = readResourceSource(desc_.cookedPath, context, desc_.maxDocumentBytes);
    if (!source.success)
        return resourceLoadFailure(*this, source.diagnostics.c_str());
    auto decoded = core::makeShared<assets::cloud::CloudAsset>();
    const auto payloadBudget = context.maxDecodedBytes
        ? std::min(context.maxDecodedBytes, desc_.maxPayloadBytes)
        : desc_.maxPayloadBytes;
    const auto result = CloudAssetCookedCodec::decode(
        {reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()},
        desc_.maxDocumentBytes,
        payloadBudget,
        *decoded);
    if (!result.success)
        return resourceLoadFailure(*this, result.diagnostic);
    const auto admitted = manager.admitResourceMetadata(*this, context.policy);
    if (!admitted.success)
        return admitted;
    core::StableHash64 fingerprint;
    fingerprint.appendBytes(source.bytes.data(), source.bytes.size());
    contentFingerprint_ = fingerprint.value();
    asset_ = std::move(decoded);
    return admitted;
}

std::size_t CloudAssetResource::processedByteCount() const noexcept
{
    return asset_ ? asset_->cells.size() * sizeof(core::Vec4) : 0u;
}

void CloudAssetResource::appendContentFingerprint(core::StableHash64& hash) const noexcept
{
    hash.appendU64(contentFingerprint_);
}
} // namespace arp::resource
