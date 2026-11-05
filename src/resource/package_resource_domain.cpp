#include "resource/package_resource_domain.h"

#include "core/hash/sha256.h"
#include "resource/asset_package_payload.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_candidate_preparation.h"
#include "resource/resource_graph_plan.h"
#include "resource/resource_source_io.h"

#include <algorithm>
#include <stdexcept>

namespace arp::resource {
namespace {
struct FileBinding {
    ResourceFileReadIntent read;
    core::pmr::String relativePath{&core::defaultCpuMemoryResource()};
    core::pmr::String sha256{&core::defaultCpuMemoryResource()};
    std::size_t exactBytes = 0;
};

struct NodeSpec {
    InternalAssetPackageEntry entry;
    std::filesystem::path directory;
    ResourceFileOrigin origin;
    std::size_t maxDecodedBytes = 0;
    core::pmr::Vector<FileBinding> files{&core::defaultCpuMemoryResource()};
    core::pmr::String controllerRuntimeId{&core::defaultCpuMemoryResource()};
};

void require(bool value, const char* diagnostic)
{
    if (!value)
        throw std::runtime_error(diagnostic);
}

core::StringView bytes(const ResourceSourceBuffer& source)
{
    return {reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()};
}

const char* verifyFiles(const NodeSpec& spec, const ResourceCandidatePreparationContext& context)
{
    if (context.cancellation.cancelled())
        return "PACKAGE-DOMAIN-PREPARATION-CANCELLED";
    for (const auto& file : spec.files) {
        const auto* source = context.sources.find(file.read.path);
        if (!source)
            return "PACKAGE-DOMAIN-SOURCE-MISSING";
        if (source->pathScope != spec.origin.pathScope || !(source->mount == spec.origin.mount))
            return "PACKAGE-DOMAIN-SOURCE-ORIGIN";
        if (source->bytes.size() > file.read.maxBytes
            || (file.exactBytes && source->bytes.size() != file.exactBytes))
            return "PACKAGE-DOMAIN-SOURCE-SIZE";
        if (core::Sha256::hexDigest(bytes(*source)) != file.sha256)
            return "PACKAGE-DOMAIN-SOURCE-SEAL";
    }
    return context.cancellation.cancelled() ? "PACKAGE-DOMAIN-PREPARATION-CANCELLED" : nullptr;
}

template <class T>
class SealedCandidate : public T,
                        public ResourceCandidatePreparation,
                        public AssetPackageSourceUsage {
public:
    SealedCandidate(const T& value, core::SharedPtr<const NodeSpec> spec)
        : T(value), spec_(std::move(spec))
    {}

    bool collectFileReadIntents(ResourceManager&,
        core::pmr::Vector<ResourceFileReadIntent>& reads,
        core::pmr::String&) const override
    {
        for (const auto& file : spec_->files)
            reads.push_back(file.read);
        return true;
    }

    ResourceCandidatePreparationResult prepareResourceCandidate(
        const ResourceCandidatePreparationContext& context) override
    {
        if (const auto* failure = verifyFiles(*spec_, context))
            return {false, failure};
        for (const auto& file : spec_->files)
            usage_.push_back(
                {file.relativePath, context.sources.find(file.read.path)->bytes.size()});
        return {true};
    }

    core::ArrayView<const AssetPackageFileUsage> sourceUsage() const noexcept override
    {
        return {usage_.data(), usage_.size()};
    }

protected:
    core::SharedPtr<const NodeSpec> spec_;
    core::pmr::Vector<AssetPackageFileUsage> usage_{&core::defaultCpuMemoryResource()};
};

class TextureCandidate final : public SealedCandidate<TextureResource>,
                               public AssetPackageEncodedTextureSource {
public:
    using SealedCandidate::SealedCandidate;

    ResourceCandidatePreparationResult prepareResourceCandidate(
        const ResourceCandidatePreparationContext& input) override
    {
        auto sealed = SealedCandidate::prepareResourceCandidate(input);
        if (!sealed.success)
            return sealed;
        if (spec_->files.size() != 1 || spec_->files.front().relativePath != sourcePath)
            return {false, "PACKAGE-DOMAIN-TEXTURE-SOURCE-BINDING"};
        const auto& source = *input.sources.find(spec_->files.front().read.path);
        const auto budget = std::min(input.maxDecodedBytes, spec_->maxDecodedBytes);
        if (!budget || budget == SIZE_MAX || decodedRgba8.size() > budget || source.bytes.empty()
            || source.bytes.size() > budget - decodedRgba8.size())
            return {false, "PACKAGE-DOMAIN-TEXTURE-SOURCE-BUDGET"};
        encoded_.assign(source.bytes.begin(), source.bytes.end());
        if (input.cancellation.cancelled())
            return {false, "PACKAGE-DOMAIN-PREPARATION-CANCELLED"};
        return {true};
    }

    core::ArrayView<const std::uint8_t> encodedSourceBytes() const noexcept override
    {
        return {encoded_.data(), encoded_.size()};
    }

    std::size_t processedByteCount() const noexcept override
    {
        return TextureResource::processedByteCount() + encoded_.size();
    }

private:
    core::pmr::Vector<std::uint8_t> encoded_{&core::defaultCpuMemoryResource()};
};

class RuntimeCandidate final : public SealedCandidate<PrefabRuntimePayloadResource> {
public:
    using SealedCandidate::SealedCandidate;

    ResourceCandidatePreparationResult prepareResourceCandidate(
        const ResourceCandidatePreparationContext& input) override
    {
        auto sealed = SealedCandidate::prepareResourceCandidate(input);
        if (!sealed.success)
            return sealed;
        AssetPackageSourceContext context;
        context.origin = spec_->origin;
        context.resources.sources = &input.sources;
        context.resources.requirePreloadedSources = true;
        context.resources.maxDecodedBytes = std::min(input.maxDecodedBytes, spec_->maxDecodedBytes);
        auto decoded =
            PrefabRuntimePayloadSourceReader().readSources(spec_->entry, spec_->directory, context);
        if (!decoded.success || !decoded.hasCompleteControllerData)
            return {false, "PACKAGE-DOMAIN-RUNTIME-PREPARATION", std::move(decoded.diagnostics)};
        if (input.cancellation.cancelled())
            return {false, "PACKAGE-DOMAIN-PREPARATION-CANCELLED"};
        runtimeAsset = core::makeShared<assets::AnimatedMeshAsset>(std::move(decoded.asset));
        controllerAsset = core::makeShared<animation::AnimatorControllerAsset>(
            std::move(decoded.controllerAsset));
        return {true};
    }
};

class ControllerCandidate final : public AnimatorControllerResource {
public:
    ControllerCandidate(
        const AnimatorControllerResource& value, core::SharedPtr<const NodeSpec> spec)
        : AnimatorControllerResource(value), spec_(std::move(spec))
    {}

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override
    {
        const auto runtime = manager.find<PrefabRuntimePayloadResource>(spec_->controllerRuntimeId);
        if (!runtime.ready() || !runtime.get()->controllerAsset)
            return {false,
                ResourceState::Failed,
                reference().id,
                reference().type,
                "PACKAGE-DOMAIN-CONTROLLER-RUNTIME-NOT-READY"};
        controllerAsset = runtime.get()->controllerAsset;
        return ResourceBase::load(manager, context);
    }

private:
    core::SharedPtr<const NodeSpec> spec_;
};

core::UniquePtr<ResourceBase> descriptor(const core::SharedPtr<const NodeSpec>& spec)
{
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    auto typed = createPrefabResourceFromEnvelope(spec->entry.envelopeYaml, diagnostic);
    require(bool(typed), "PACKAGE-DOMAIN-TYPED-DESCRIPTOR");
    typed->configureMetadata({spec->entry.resourceId,
                                 spec->entry.resourceType,
                                 spec->entry.alias,
                                 spec->entry.status,
                                 {}},
        spec->entry.dependencies);
    if (const auto* runtime = dynamic_cast<const PrefabRuntimePayloadResource*>(typed.get()))
        return core::makeUnique<RuntimeCandidate>(*runtime, spec);
    if (const auto* controller = dynamic_cast<const AnimatorControllerResource*>(typed.get());
        controller && !spec->controllerRuntimeId.empty())
        return core::makeUnique<ControllerCandidate>(*controller, spec);
    if (const auto* texture = dynamic_cast<const TextureResource*>(typed.get()))
        return core::makeUnique<TextureCandidate>(*texture, spec);
    if (const auto* shader = dynamic_cast<const ShaderPackageResource*>(typed.get()))
        return core::makeUnique<SealedCandidate<ShaderPackageResource>>(*shader, spec);
    return typed;
}

core::pmr::String version(const NodeSpec& spec)
{
    core::pmr::String value("arp.package.resource/v1\n", &core::defaultCpuMemoryResource());
    value += core::Sha256::hexDigest(spec.entry.envelopeYaml);
    value += '\n';
    core::pmr::Vector<core::pmr::String> dependencies{&core::defaultCpuMemoryResource()};
    for (const auto& dependency : spec.entry.dependencies) {
        auto key = core::Sha256::hexDigest(dependency.id);
        key += core::Sha256::hexDigest(dependency.usage);
        dependencies.push_back(std::move(key));
    }
    std::sort(dependencies.begin(), dependencies.end());
    for (const auto& dependency : dependencies) {
        value += dependency;
        value += '\n';
    }
    for (const auto& file : spec.files) {
        value += core::Sha256::hexDigest(file.relativePath);
        value += file.sha256;
        value += '\n';
    }
    return core::Sha256::hexDigest(value);
}

void addFile(NodeSpec& spec,
    ResourceFileReadIntent read,
    const ResourceSourceSet& sources,
    core::ArrayView<const AssetPackageFileSeal> seals,
    bool inventory)
{
    FileBinding binding;
    binding.relativePath =
        core::FilePath::utf8(std::filesystem::path(read.path).lexically_relative(spec.directory));
    require(core::FilePath::isSafeRelativePath(binding.relativePath), "PACKAGE-DOMAIN-SOURCE-PATH");
    if (inventory) {
        const auto found = std::find_if(seals.begin(), seals.end(), [&](const auto& seal) {
            return seal.relativePath == binding.relativePath;
        });
        require(found != seals.end(), "PACKAGE-DOMAIN-FILE-SEAL-MISSING");
        require(!read.maxBytes || read.maxBytes <= found->maxBytes, "PACKAGE-DOMAIN-SOURCE-SIZE");
        binding.exactBytes = read.maxBytes;
        read.maxBytes = read.maxBytes ? read.maxBytes : found->maxBytes;
        read.pathScope = spec.origin.pathScope;
        read.mount = spec.origin.mount;
        binding.sha256 = found->sha256;
        binding.read = std::move(read);
        spec.files.push_back(std::move(binding));
        return;
    }
    const auto* source = sources.find(read.path);
    require(source && !source->bytes.empty() && source->pathScope == spec.origin.pathScope
            && source->mount == spec.origin.mount,
        "PACKAGE-DOMAIN-BOUND-SOURCE-MISSING-OR-ORIGIN");
    require(!read.maxBytes || read.maxBytes == source->bytes.size(), "PACKAGE-DOMAIN-SOURCE-SIZE");
    read.maxBytes = source->bytes.size();
    read.pathScope = spec.origin.pathScope;
    read.mount = spec.origin.mount;
    read.expectedVersion = source->version;
    read.maxVersionBytes =
        source->version.kind == core::io::FileVersionKind::None ? 0 : read.maxBytes;
    binding.exactBytes = read.maxBytes;
    binding.sha256 = core::Sha256::hexDigest(bytes(*source));
    binding.read = std::move(read);
    spec.files.push_back(std::move(binding));
}
} // namespace

AssetPackageDomainPlan AssetPackageDomainPlanner::planSources(
    const std::filesystem::path& directory,
    const AssetPackageSourceContext& sources,
    std::size_t maxMetadataBytes,
    std::size_t maxResources) const
{
    return planImpl(directory, sources, maxMetadataBytes, maxResources, {}, false);
}

AssetPackageDomainPlan AssetPackageDomainPlanner::planSealedSources(
    const std::filesystem::path& directory,
    const AssetPackageSourceContext& sources,
    std::size_t maxMetadataBytes,
    std::size_t maxResources,
    core::ArrayView<const AssetPackageFileSeal> seals) const
{
    return planImpl(directory, sources, maxMetadataBytes, maxResources, seals, true);
}

AssetPackageDomainPlan AssetPackageDomainPlanner::planImpl(const std::filesystem::path& directory,
    const AssetPackageSourceContext& sources,
    std::size_t maxMetadataBytes,
    std::size_t maxResources,
    core::ArrayView<const AssetPackageFileSeal> seals,
    bool inventory) const
{
    AssetPackageDomainPlan result;
    try {
        if (inventory) {
            constexpr core::StringView prefix = "sha256:";
            for (auto current = seals.begin(); current != seals.end(); ++current) {
                require(core::FilePath::isSafeRelativePath(current->relativePath)
                        && current->maxBytes && current->maxBytes != SIZE_MAX
                        && core::StringView(current->sha256).substr(0, prefix.size()) == prefix
                        && core::Sha256::isHexDigest(
                            core::StringView(current->sha256).substr(prefix.size())),
                    "PACKAGE-DOMAIN-FILE-SEAL-INVALID");
                require(std::none_of(seals.begin(),
                            current,
                            [&](const auto& other) {
                                return other.relativePath == current->relativePath;
                            }),
                    "PACKAGE-DOMAIN-FILE-SEAL-DUPLICATE");
            }
        }
        auto planned = InternalAssetPackageReader().planPreparationSources(
            directory, sources, maxMetadataBytes, maxResources);
        if (!planned.success) {
            result.diagnosticId = "PACKAGE-DOMAIN-METADATA-PLAN";
            result.diagnostics = std::move(planned.diagnostics);
            return result;
        }
        core::pmr::Vector<core::SharedPtr<NodeSpec>> specs{&core::defaultCpuMemoryResource()};
        for (const auto& entry : planned.package.entries) {
            require(prefabResourceFactory().supports(entry.resourceType),
                "PACKAGE-DOMAIN-TYPE-UNSUPPORTED");
            auto spec = core::makeShared<NodeSpec>();
            spec->entry = entry;
            spec->directory = directory;
            spec->origin = sources.origin;
            spec->maxDecodedBytes = planned.maxDecodedBytes;
            if (entry.resourceType == PrefabRuntimePayloadResource::resourceTypeName()) {
                auto reads = PrefabRuntimePayloadSourceReader().planSourceReads(
                    entry, directory, sources.origin);
                require(reads.success, "PACKAGE-DOMAIN-RUNTIME-SOURCE-PLAN");
                for (auto& read : reads.reads)
                    addFile(*spec, std::move(read), *sources.resources.sources, seals, inventory);
            } else if (entry.resourceType == TextureResource::resourceTypeName()
                || entry.resourceType == ShaderPackageResource::resourceTypeName()) {
                const auto index =
                    AssetPackagePayloadInspector({}, directory, sources).inspect(entry, true);
                require(index.success, "PACKAGE-DOMAIN-FILE-BINDINGS");
                for (const auto& file : index.files) {
                    std::filesystem::path resolved;
                    require(resolveResourceSourcePath(directory, file, sources.origin, resolved),
                        "PACKAGE-DOMAIN-SOURCE-PATH");
                    ResourceFileReadIntent read;
                    read.path = core::FilePath::utf8(resolved);
                    addFile(*spec, std::move(read), *sources.resources.sources, seals, inventory);
                }
            }
            std::sort(spec->files.begin(), spec->files.end(), [](const auto& a, const auto& b) {
                return a.relativePath < b.relativePath;
            });
            specs.push_back(std::move(spec));
        }
        // Controller data is part of the explicitly bound runtime binary. Make
        // that ownership dependency/version visible, rejecting ambiguous owners.
        for (auto& spec : specs) {
            if (spec->entry.resourceType != AnimatorControllerResource::resourceTypeName())
                continue;
            for (const auto& prefab : planned.package.entries) {
                if (prefab.resourceType != PrefabResource::resourceTypeName()
                    || std::none_of(prefab.dependencies.begin(),
                        prefab.dependencies.end(),
                        [&](const auto& dependency) {
                            return dependency.usage == "animator-controller"
                                && dependency.id == spec->entry.resourceId;
                        }))
                    continue;
                for (const auto& binding : prefab.dependencies)
                    if (binding.usage == "runtime-payload") {
                        require(spec->controllerRuntimeId.empty()
                                || spec->controllerRuntimeId == binding.id,
                            "PACKAGE-DOMAIN-CONTROLLER-RUNTIME-AMBIGUOUS");
                        spec->controllerRuntimeId = binding.id;
                    }
            }
            if (!spec->controllerRuntimeId.empty()
                && std::none_of(spec->entry.dependencies.begin(),
                    spec->entry.dependencies.end(),
                    [&](const auto& dependency) {
                        return dependency.id == spec->controllerRuntimeId
                            && dependency.usage == "runtime-payload";
                    }))
                spec->entry.dependencies.push_back({spec->controllerRuntimeId, "runtime-payload"});
        }
        core::pmr::Vector<ResourceRegistration> registrations{&core::defaultCpuMemoryResource()};
        for (const auto& spec : specs) {
            auto content = version(*spec);
            if (!spec->controllerRuntimeId.empty()) {
                const auto runtime =
                    std::find_if(specs.begin(), specs.end(), [&](const auto& node) {
                        return node->entry.resourceId == spec->controllerRuntimeId
                            && node->entry.resourceType
                            == PrefabRuntimePayloadResource::resourceTypeName();
                    });
                require(runtime != specs.end(), "PACKAGE-DOMAIN-CONTROLLER-RUNTIME-TYPE");
                content += version(**runtime);
                content = core::Sha256::hexDigest(content);
            }
            core::SharedPtr<const NodeSpec> immutable = spec;
            registrations.push_back(
                {{spec->entry.resourceId, spec->entry.resourceType, content}, [immutable] {
                     return descriptor(immutable);
                 }});
        }
        ResourceGraphRequest graph;
        graph.rootResourceId = planned.package.entries.front().resourceId;
        for (const auto& registration : registrations) {
            ResourceGraphNode node;
            node.resource = registration.factory();
            node.dependencies = node.resource->dependencies();
            graph.nodes.push_back(std::move(node));
        }
        const auto order = buildResourceGraphPlan(graph);
        require(order.success, "PACKAGE-DOMAIN-DEPENDENCY-GRAPH");
        result.package = std::move(planned.package);
        result.registrations = std::move(registrations);
        result.success = true;
    } catch (const std::bad_alloc&) {
        result.diagnosticId = "PACKAGE-DOMAIN-ALLOCATION";
    } catch (const std::exception& error) {
        result.diagnosticId = "PACKAGE-DOMAIN-PLAN-FAILED";
        result.diagnostics = error.what();
    }
    return result;
}
} // namespace arp::resource
