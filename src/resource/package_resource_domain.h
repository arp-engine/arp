#pragma once

#include "resource/internal_asset_package.h"
#include "resource/resource_domain.h"

namespace arp::resource {

struct AssetPackageDomainPlan {
    bool success = false;
    const char* diagnosticId = nullptr;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    InternalAssetPackageDesc package;
    core::pmr::Vector<ResourceRegistration> registrations{&core::defaultCpuMemoryResource()};
};

// Explicit trusted inventory. SHA uses Sha256::hexDigest's "sha256:<hex>"
// encoding; maxBytes is a finite read bound, verified before Ready publication.
struct AssetPackageFileSeal {
    core::pmr::String relativePath{&core::defaultCpuMemoryResource()};
    core::pmr::String sha256{&core::defaultCpuMemoryResource()};
    std::size_t maxBytes = 0;
};

struct AssetPackageFileUsage {
    core::pmr::String relativePath{&core::defaultCpuMemoryResource()};
    std::size_t bytes = 0;
};

// Actual acquired bytes retained as lightweight accounting after transient
// acquisition buffers are released. Some typed resources retain encoded data.
// Package consumers can enforce their aggregate source budget.
class ARP_API AssetPackageSourceUsage {
public:
    virtual ~AssetPackageSourceUsage() = default;
    virtual core::ArrayView<const AssetPackageFileUsage> sourceUsage() const noexcept = 0;
};

// Immutable encoded source owned by a native package TextureResource. Borrowed
// bytes require a live lease on that resource or one of its dependency parents.
// Access performs no IO; factories do not retain this data after unload.
class ARP_API AssetPackageEncodedTextureSource {
public:
    virtual ~AssetPackageEncodedTextureSource() = default;
    virtual core::ArrayView<const std::uint8_t> encodedSourceBytes() const noexcept = 0;
};

// Native typed package adapter. Planning consumes acquired sources without host
// IO or binary decode. Repeatable factories retain metadata/seals, not sources
// or decoded Assets. Initialize the immutable reflection schema before workers.
class ARP_API AssetPackageDomainPlanner final {
public:
    AssetPackageDomainPlan planSources(const std::filesystem::path& directory,
        const AssetPackageSourceContext&,
        std::size_t maxMetadataBytes,
        std::size_t maxResources) const;
    AssetPackageDomainPlan planSealedSources(const std::filesystem::path& directory,
        const AssetPackageSourceContext& metadata,
        std::size_t maxMetadataBytes,
        std::size_t maxResources,
        core::ArrayView<const AssetPackageFileSeal>) const;

private:
    AssetPackageDomainPlan planImpl(const std::filesystem::path&,
        const AssetPackageSourceContext&,
        std::size_t,
        std::size_t,
        core::ArrayView<const AssetPackageFileSeal>,
        bool) const;
};

} // namespace arp::resource
