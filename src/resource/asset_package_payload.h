#pragma once

#include "resource/internal_asset_package.h"

namespace arp::resource {

struct AssetPackageTypedDependency final {
    ResourceDependency binding;
    const char* expectedType = nullptr;
};

struct AssetPackagePayloadIndex final {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<AssetPackageTypedDependency> dependencies{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> files{&core::defaultCpuMemoryResource()};
};

// Explicit payload bindings shared by dependency preparation and offline closure
// validation. sourcePath on a material/source record is provenance; texture and
// shader paths are file bindings in the closed-package contract.
class ARP_API AssetPackagePayloadInspector final {
public:
    bool supports(NameView type) const noexcept;
    AssetPackagePayloadIndex inspect(
        const InternalAssetPackageEntry& entry, bool requireFileBindings = false) const;
    bool externalReady(
        const ResourceManager& manager, const AssetPackageTypedDependency& dependency) const;
};

} // namespace arp::resource
