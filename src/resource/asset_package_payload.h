#pragma once

#include "resource/internal_asset_package.h"

#include <initializer_list>

namespace arp::resource {

struct AssetPackagePayloadIndex;

struct AssetPackageTypedDependency final {
    ResourceDependency binding;
    core::pmr::String expectedType{&core::defaultCpuMemoryResource()};
};

// Domain codecs are immutable, core-owned setup objects. Inspection must not
// instantiate gameplay objects; it supplies the same facts to load and closure.
class ARP_API AssetPackageCodec {
public:
    virtual ~AssetPackageCodec() = default;
    virtual NameView resourceType() const noexcept = 0;
    virtual AssetPackagePayloadIndex inspect(
        const InternalAssetPackageEntry&, const std::filesystem::path& packageRoot) const = 0;
    virtual core::UniquePtr<ResourceBase> prepare(const InternalAssetPackageEntry&,
        const std::filesystem::path& packageRoot,
        core::pmr::String& diagnostic) const = 0;
    // Default is deterministic refusal. A codec must explicitly implement a
    // source-only path before participating in a preloaded package transaction.
    virtual AssetPackagePayloadIndex inspectSources(const InternalAssetPackageEntry&,
        const std::filesystem::path& packageRoot,
        const AssetPackageSourceContext&) const;
    virtual core::UniquePtr<ResourceBase> prepareSources(const InternalAssetPackageEntry&,
        const std::filesystem::path& packageRoot,
        const AssetPackageSourceContext&,
        core::pmr::String& diagnostic) const;
    virtual bool planSourceReads(const InternalAssetPackageEntry&,
        const std::filesystem::path& packageRoot,
        const AssetPackageSourceContext&,
        core::pmr::Vector<ResourceFileReadIntent>&,
        core::pmr::String& diagnostic) const;
};

class ARP_API AssetPackageCodecRegistry final {
public:
    explicit AssetPackageCodecRegistry(std::size_t capacity);
    bool add(core::SharedPtr<const AssetPackageCodec>, core::pmr::String& diagnostic);
    bool addAll(std::initializer_list<core::SharedPtr<const AssetPackageCodec>>,
        core::pmr::String& diagnostic);
    bool freeze(core::pmr::String& diagnostic);

    bool frozen() const noexcept { return frozen_; }

    const AssetPackageCodec* find(NameView type) const noexcept;

private:
    std::size_t capacity_;
    bool frozen_ = false;
    core::pmr::Vector<core::SharedPtr<const AssetPackageCodec>> codecs_{
        &core::defaultCpuMemoryResource()};
};

struct AssetPackagePayloadIndex final {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<AssetPackageTypedDependency> dependencies{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> files{&core::defaultCpuMemoryResource()};
    // Reservation required by this codec's decoded document(s), not GPU bytes.
    std::size_t maxDecodedBytes = 0u;
};

// Explicit payload bindings shared by dependency preparation and offline closure
// validation. sourcePath on a material/source record is provenance; texture and
// shader paths are file bindings in the closed-package contract.
class ARP_API AssetPackagePayloadInspector final {
public:
    explicit AssetPackagePayloadInspector(
        core::SharedPtr<const AssetPackageCodecRegistry> codecs = {},
        std::filesystem::path root = {})
        : codecs_(std::move(codecs)), root_(std::move(root))
    {}

    AssetPackagePayloadInspector(core::SharedPtr<const AssetPackageCodecRegistry> codecs,
        std::filesystem::path root,
        AssetPackageSourceContext sources)
        : codecs_(std::move(codecs)), root_(std::move(root)), sources_(sources), preloaded_(true)
    {}

    bool supports(NameView type) const noexcept;
    AssetPackagePayloadIndex inspect(
        const InternalAssetPackageEntry& entry, bool requireFileBindings = false) const;
    bool externalReady(
        const ResourceManager& manager, const AssetPackageTypedDependency& dependency) const;

private:
    core::SharedPtr<const AssetPackageCodecRegistry> codecs_;
    std::filesystem::path root_;
    AssetPackageSourceContext sources_;
    bool preloaded_ = false;
};

} // namespace arp::resource
