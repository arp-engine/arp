#pragma once
#include "core/hash/sha256.h"
#include "reflection/serialization_binary.h"
#include "resource/resource_candidate_preparation.h"
#include "resource/resource_domain.h"

#include <algorithm>
#include <filesystem>
#include <type_traits>

namespace arp::resource {
using CookedBytes = core::pmr::Vector<std::uint8_t>;

struct CookedResourceBudget {
    std::size_t maxResources = 4096;
    std::size_t maxMetadataBytes = 16u * 1024u * 1024u;
    // A full 256 MiB RGBA8 image also needs its v2 dimensions/flags header.
    std::size_t maxPayloadBytes = 256u * 1024u * 1024u + 15u;
    std::size_t maxTotalBytes = 1024u * 1024u * 1024u;
    std::size_t maxDecodedBytes = 256u * 1024u * 1024u;
};

struct CookedResourceDependency {
    ResourceDependency binding;
    core::pmr::String expectedType{&core::defaultCpuMemoryResource()};
};

struct CookedResourceEntry {
    ResourceReference reference;
    core::pmr::String file{&core::defaultCpuMemoryResource()};
    core::pmr::String contentVersion{&core::defaultCpuMemoryResource()};
    std::uint64_t schema = 0;
    std::uint64_t metadataBytes = 0;
    std::uint64_t payloadBytes = 0;
    core::pmr::String metadataHash{&core::defaultCpuMemoryResource()};
    core::pmr::String payloadHash{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<CookedResourceDependency> dependencies{&core::defaultCpuMemoryResource()};

    ResourceIdentity identity() const { return {reference.id, reference.type, contentVersion}; }
};
class CookedResourceCodec;

struct CookedResourceSpec {
    CookedResourceEntry entry;
    CookedBytes metadata{&core::defaultCpuMemoryResource()};
    ResourceFileReadIntent payloadRead;
    core::SharedPtr<const CookedResourceCodec> codec;
    std::size_t maxDecodedBytes = 0;
    reflection::SerializationOptions archiveOptions;
};

struct CookedResourceResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

// Domain codecs are frozen setup objects. Metadata must contain no encoded source
// documents that require runtime authoring parsing; heavy data belongs to payload.
class ARP_API CookedResourceCodec {
public:
    virtual ~CookedResourceCodec() = default;
    virtual NameView resourceType() const noexcept = 0;
    virtual std::uint64_t schema() const = 0;
    virtual reflection::ReflectionBinaryResult writeMetadata(
        const ResourceBase&, reflection::SerializationOptions) const = 0;
    virtual core::UniquePtr<ResourceBase> descriptor(
        core::SharedPtr<const CookedResourceSpec>) const = 0;

    // Called while planning (which may run on a worker). The repeatable factory
    // owns validated metadata; reflected codecs clone typed state without parsing
    // again during owner registration or a later load attempt.
    virtual ResourceDescriptorFactory prepareDescriptorFactory(
        core::SharedPtr<const CookedResourceSpec> spec) const
    {
        return [spec]() {
            return spec->codec->descriptor(spec);
        };
    }

    virtual CookedBytes cookPayload(
        const ResourceBase&, const std::filesystem::path&, const CookedResourceBudget&) const;
    virtual CookedResourceResult preparePayload(
        ResourceBase&, core::ArrayView<const std::uint8_t>, std::size_t) const;
    virtual CookedResourceResult beforeAdmission(ResourceBase&, ResourceManager&) const;
};

class ARP_API CookedResourceCodecRegistry final {
public:
    explicit CookedResourceCodecRegistry(std::size_t capacity) : capacity_(capacity) {}

    bool add(core::SharedPtr<const CookedResourceCodec>);
    bool freeze();

    bool frozen() const noexcept { return frozen_; }

    core::SharedPtr<const CookedResourceCodec> find(NameView type) const;

private:
    std::size_t capacity_;
    bool frozen_ = false;
    core::pmr::Vector<core::SharedPtr<const CookedResourceCodec>> codecs_{
        &core::defaultCpuMemoryResource()};
};

ARP_API bool verifyCookedPayload(const CookedResourceSpec&,
    const ResourceSourceSet&,
    core::ArrayView<const std::uint8_t>&,
    core::pmr::String&);

template <class T>
class CookedResourceCandidate final : public T, public ResourceCandidatePreparation {
public:
    explicit CookedResourceCandidate(core::SharedPtr<const CookedResourceSpec> spec)
        : spec_(std::move(spec))
    {
        auto o = spec_->archiveOptions;
        o.maxBytes = spec_->metadata.size();
        auto parsed = reflection::deserialize_from_binary(
            {spec_->metadata.data(), spec_->metadata.size()}, o, nullptr, reflection::type_of<T>());
        if (!parsed.success)
            throw std::runtime_error(parsed.diagnostics.c_str());
        static_cast<T&>(*this) = std::move(parsed.value.template cast<T&>());
        for (const auto& dependency : static_cast<const T&>(*this).dependencies()) {
            const auto found = std::find_if(spec_->entry.dependencies.begin(),
                spec_->entry.dependencies.end(),
                [&](const auto& declared) {
                    return declared.binding.id == dependency.id
                        && declared.binding.usage == dependency.usage;
                });
            if (found == spec_->entry.dependencies.end())
                throw std::runtime_error("COOKED-UNDECLARED-FIELD-DEPENDENCY");
        }
        core::pmr::Vector<ResourceDependency> deps{&core::defaultCpuMemoryResource()};
        for (const auto& d : spec_->entry.dependencies)
            deps.push_back(d.binding);
        this->configureMetadata(spec_->entry.reference, std::move(deps));
    }

    CookedResourceCandidate(core::SharedPtr<const CookedResourceSpec> spec, const T& metadata)
        : spec_(std::move(spec))
    {
        if (metadata.reference().id != spec_->entry.reference.id
            || metadata.reference().type != spec_->entry.reference.type)
            throw std::runtime_error("COOKED-PREPARED-DESCRIPTOR-IDENTITY");
        static_cast<T&>(*this) = metadata;
    }

    bool collectFileReadIntents(ResourceManager&,
        core::pmr::Vector<ResourceFileReadIntent>& reads,
        core::pmr::String&) const override
    {
        if (spec_->entry.payloadBytes)
            reads.push_back(spec_->payloadRead);
        return true;
    }

    ResourceCandidatePreparationResult prepareResourceCandidate(
        const ResourceCandidatePreparationContext& input) override
    {
        if (input.cancellation.cancelled())
            return {false, "COOKED-CANCELLED"};
        core::ArrayView<const std::uint8_t> bytes;
        core::pmr::String error{&core::defaultCpuMemoryResource()};
        if (!verifyCookedPayload(*spec_, input.sources, bytes, error))
            return {false, "COOKED-PAYLOAD-INTEGRITY", std::move(error)};
        auto result = spec_->codec->preparePayload(
            *this, bytes, std::min(spec_->maxDecodedBytes, input.maxDecodedBytes));
        prepared_ = result.success && !input.cancellation.cancelled();
        return {
            prepared_, prepared_ ? nullptr : "COOKED-PREPARATION", std::move(result.diagnostics)};
    }

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override
    {
        if (!prepared_)
            return {false, ResourceState::Failed, {}, {}, "COOKED-NOT-PREPARED"};
        const auto checked = spec_->codec->beforeAdmission(*this, manager);
        if (!checked.success)
            return {false, ResourceState::Failed, {}, {}, checked.diagnostics};
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    core::SharedPtr<const CookedResourceSpec> spec_;
    bool prepared_ = false;
};

template <class T> class ReflectedCookedResourceCodec : public CookedResourceCodec {
public:
    NameView resourceType() const noexcept override { return T::resourceTypeName(); }

    std::uint64_t schema() const override
    {
        return reflection::serialization_schema(reflection::type_of<T>());
    }

    reflection::ReflectionBinaryResult writeMetadata(
        const ResourceBase& resource, reflection::SerializationOptions o) const override
    {
        const auto* value = dynamic_cast<const T*>(&resource);
        if (!value)
            return {false, {}, "COOKED-TYPE-MISMATCH"};
        return reflection::serialize_to_binary(reflection::object(*value), o);
    }

    core::UniquePtr<ResourceBase> descriptor(
        core::SharedPtr<const CookedResourceSpec> spec) const override
    {
        return core::makeUnique<CookedResourceCandidate<T>>(std::move(spec));
    }

    ResourceDescriptorFactory prepareDescriptorFactory(
        core::SharedPtr<const CookedResourceSpec> spec) const override
    {
        if constexpr (std::is_copy_assignable_v<T>) {
            auto checked = descriptor(spec);
            if (!checked)
                throw std::runtime_error("COOKED-DESCRIPTOR");
            auto mutableMetadata = core::makeShared<T>();
            *mutableMetadata = dynamic_cast<const T&>(*checked);
            core::SharedPtr<const T> metadata = std::move(mutableMetadata);
            return [spec, metadata]() -> core::UniquePtr<ResourceBase> {
                return core::makeUnique<CookedResourceCandidate<T>>(spec, *metadata);
            };
        } else {
            return CookedResourceCodec::prepareDescriptorFactory(std::move(spec));
        }
    }
};

class ARP_API CookedResourceCatalog final {
public:
    const auto& entries() const noexcept { return entries_; }

    const CookedResourceEntry* find(NameView id) const noexcept;
    CookedResourceResult read(core::ArrayView<const std::uint8_t>,
        const CookedResourceCodecRegistry&,
        CookedResourceBudget = {});
    reflection::ReflectionBinaryResult write(CookedResourceBudget = {}) const;
    CookedResourceResult assign(core::pmr::Vector<CookedResourceEntry>,
        const CookedResourceCodecRegistry&,
        CookedResourceBudget = {});

private:
    core::pmr::Vector<CookedResourceEntry> entries_{&core::defaultCpuMemoryResource()};
};

struct CookedResourcePlan {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    CookedResourceCatalog catalog;
    core::pmr::Vector<ResourceRegistration> registrations{&core::defaultCpuMemoryResource()};
    std::uint64_t metadataReadBytes = 0;
};

class ARP_API CookedResourceLoader final {
public:
    // Cold owner-thread planning. Reads only catalog, fixed headers and metadata
    // through the supplied FileService, dispatching its runtime until completion.
    CookedResourcePlan plan(const std::filesystem::path&,
        core::runtime::AsyncRuntime&,
        core::SharedPtr<const CookedResourceCodecRegistry>,
        CookedResourceBudget = {},
        ResourceFileOrigin = {}) const;
    bool planFileReadIntents(const std::filesystem::path&,
        const ResourceSourceSet&,
        core::pmr::Vector<ResourceFileReadIntent>&,
        core::pmr::String&,
        CookedResourceBudget = {},
        ResourceFileOrigin = {}) const;
    CookedResourcePlan planSources(const std::filesystem::path&,
        const ResourceSourceSet&,
        core::SharedPtr<const CookedResourceCodecRegistry>,
        CookedResourceBudget = {},
        ResourceFileOrigin = {}) const;
    CookedResourceResult registerPlan(const CookedResourcePlan&, ResourceDomain&) const;
};

struct ResourceCookInput {
    core::UniquePtr<ResourceBase> resource;
    core::pmr::Vector<CookedResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
    std::filesystem::path sourceRoot;
};

class ARP_API ResourceCookService final {
public:
    // Deterministic v1 loose delivery -> v2 metadata catalog + payload container.
    // The destination must be absent; failed conversion never changes the input.
    CookedResourceResult pack(const std::filesystem::path& source,
        const std::filesystem::path& output,
        core::SharedPtr<const CookedResourceCodecRegistry>,
        CookedResourceBudget = {}) const;
    CookedResourceResult cook(core::ArrayView<const ResourceCookInput>,
        const std::filesystem::path&,
        core::SharedPtr<const CookedResourceCodecRegistry>,
        CookedResourceBudget = {}) const;
    CookedResourceResult cookPackage(const std::filesystem::path& source,
        const std::filesystem::path& output,
        core::SharedPtr<const CookedResourceCodecRegistry>,
        CookedResourceBudget = {}) const;
    CookedResourceResult cookManifest(const std::filesystem::path& manifest,
        const std::filesystem::path& sourceRoot,
        const std::filesystem::path& output,
        core::SharedPtr<const CookedResourceCodecRegistry>,
        CookedResourceBudget = {}) const;
};

ARP_API core::SharedPtr<const CookedResourceCodecRegistry> makePrefabCookedResourceCodecs();
} // namespace arp::resource
