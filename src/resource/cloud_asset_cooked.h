#pragma once

#include "assets/cloud/cloud_asset.h"

namespace arp::resource {

// Bounded cold preparation. Runtime decoding never regenerates noise.
class ARP_API CloudAssetCookedCodec final {
public:
    static assets::cloud::CloudAssetResult encode(const assets::cloud::CloudAsset& asset,
        std::uint64_t maxDocumentBytes,
        core::pmr::String& output);
    static assets::cloud::CloudAssetResult decode(core::StringView yaml,
        std::uint64_t maxDocumentBytes,
        std::uint64_t maxPayloadBytes,
        assets::cloud::CloudAsset& output);
};

} // namespace arp::resource
