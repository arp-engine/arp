#pragma once

#include "resource/internal_asset_package.h"

namespace arp::resource {

struct AssetPackageLoadRequest {
    std::filesystem::path directory;
    ResourceFileOrigin origin;
    AssetPackageReadBudget budget;
    ResourceLoadPolicy policy;
    core::runtime::CancellationToken cancellation;
    core::SharedPtr<const AssetPackageCodecRegistry> codecs;
};

struct AssetPackageLoadTelemetry {
    std::uint64_t plans = 0;
    std::uint64_t submittedReads = 0;
    std::uint64_t bytesRead = 0;
    std::uint64_t completions = 0;
    bool committed = false;
};

struct AssetPackageLoadResult {
    ResourceLoadResult resource;
    AssetPackageLoadTelemetry telemetry;
    core::io::FileIoStatus fileStatus = core::io::FileIoStatus::Success;
    const char* diagnosticId = nullptr;
};

struct AssetPackageLoadCompletion {
    void (*complete)(void*, AssetPackageLoadResult) = nullptr;
    void* userData = nullptr;
};

// Owner-dispatch service: asynchronous bounded FileService acquisition, pure
// metadata planning, then source-only atomic admission on that same owner.
// Services/manager/queue must outlive accepted jobs. The facade may be destroyed.
class ARP_API InternalAssetPackageLoader final {
public:
    InternalAssetPackageLoader(ResourceManager& manager,
        core::io::FileService& files,
        core::runtime::CompletionQueue& completions) noexcept;
    core::io::FileIoStatus submit(
        AssetPackageLoadRequest request, AssetPackageLoadCompletion completion) noexcept;

private:
    ResourceManager& manager_;
    core::io::FileService& files_;
    core::runtime::CompletionQueue& completions_;
};

} // namespace arp::resource
