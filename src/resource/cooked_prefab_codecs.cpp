#include "assets/image_decoder.h"
#include "core/encoding/binary_stream.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "reflection/binary_document.h"
#include "resource/asset_package_payload.h"
#include "resource/cooked_resource.h"
#include "resource/particle_asset_resources.h"
#include "resource/prefab_asset_resources.h"

#include <algorithm>

namespace arp::resource {
namespace {
void require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

CookedBytes source(const std::filesystem::path& root, NameView relative, std::size_t budget)
{
    require(core::FilePath::isSafeRelativePath(relative), "COOKED-SOURCE-PATH");
    const auto path = root / std::filesystem::u8path(relative.begin(), relative.end());
    // Cook-only host access. Canonical containment is checked before any read.
    const auto resolved = std::filesystem::weakly_canonical(path),
               base = std::filesystem::weakly_canonical(root);
    auto rel = resolved.lexically_relative(base);
    require(!rel.empty() && *rel.begin() != "..", "COOKED-SOURCE-ESCAPE");
    auto bytes = core::readBinaryFile(path, budget);
    require(bytes.success, bytes.diagnostics.c_str());
    return std::move(bytes.bytes);
}

class TextureCodec final : public ReflectedCookedResourceCodec<TextureResource> {
public:
    std::uint64_t schema() const override
    {
        core::StableHash64 hash;
        hash.appendString("arp.texture.rgba8/v2");
        hash.appendU64(ReflectedCookedResourceCodec<TextureResource>::schema());
        return hash.value();
    }

    CookedBytes cookPayload(const ResourceBase& base,
        const std::filesystem::path& root,
        const CookedResourceBudget& budget) const override
    {
        const auto& t = dynamic_cast<const TextureResource&>(base);
        auto encoded = source(root, t.sourcePath, budget.maxPayloadBytes);
        // Current TextureResource authoring explicitly uses PNG. Other image
        // encodings require their own codec contract rather than extension guessing.
        auto image = assets::decodeImageToRgba8({encoded.data(), encoded.size()},
            core::pmr::String("image/png", &core::defaultCpuMemoryResource()),
            t.srgb,
            budget.maxDecodedBytes);
        require(image.success, image.diagnostics.c_str());
        core::encoding::BinaryWriter w(budget.maxPayloadBytes);
        w.integer(2, 4);
        w.integer(image.image.width, 4);
        w.integer(image.image.height, 4);
        w.integer(image.image.srgb, 1);
        // Authored alpha usage and the encoded source's channel presence are
        // independent: an opaque RGB PNG may still be bound to an alpha material.
        w.integer(t.alpha, 1);
        w.integer(image.image.alpha, 1);
        w.raw({image.image.rgba8.data(), image.image.rgba8.size()});
        return w.take();
    }

    CookedResourceResult preparePayload(ResourceBase& base,
        core::ArrayView<const std::uint8_t> bytes,
        std::size_t budget) const override
    {
        try {
            core::encoding::BinaryReader r(bytes);
            require(r.integer(4) == 2, "COOKED-TEXTURE-VERSION");
            const auto w = r.integer(4), h = r.integer(4), srgb = r.integer(1),
                       alpha = r.integer(1), sourceAlpha = r.integer(1);
            require(w && h && w <= UINT32_MAX && h <= UINT32_MAX && w <= budget / 4
                    && h <= budget / (w * 4),
                "COOKED-TEXTURE-BUDGET");
            const auto size = w * h * 4;
            require(r.remaining() == size && srgb <= 1 && alpha <= 1 && sourceAlpha <= 1,
                "COOKED-TEXTURE-SIZE");
            auto pixels = r.raw(size);
            auto& t = dynamic_cast<TextureResource&>(base);
            require(t.srgb == (srgb != 0) && t.alpha == (alpha != 0), "COOKED-TEXTURE-METADATA");
            t.decodedWidth = static_cast<std::uint32_t>(w);
            t.decodedHeight = static_cast<std::uint32_t>(h);
            t.decodedSrgb = srgb != 0;
            t.decodedHasAlpha = sourceAlpha != 0;
            t.decodedRgba8.assign(pixels.begin(), pixels.end());
            return {true};
        } catch (const std::exception& e) {
            return {false, e.what()};
        }
    }
};

core::pmr::String binaryDocument(core::StringView yaml)
{
    const auto node = core::YamlContract::parseSingleDocument(yaml);
    reflection::SerializationOptions o;
    o.maxBytes = 64u * 1024u * 1024u;
    o.maxCollectionLength = 1024u * 1024u;
    o.maxValues = 4u * 1024u * 1024u;
    return reflection::BinaryDocument::serialize(
        reflection::serialization_value_from_yaml(node, o));
}

class SourceCodec final : public ReflectedCookedResourceCodec<ExternalAssetSourceResource> {
public:
    std::uint64_t schema() const override
    {
        core::StableHash64 hash;
        hash.appendString("arp.external-source.binary-document/v1");
        hash.appendU64(ReflectedCookedResourceCodec<ExternalAssetSourceResource>::schema());
        return hash.value();
    }

    CookedBytes cookPayload(const ResourceBase& base,
        const std::filesystem::path& root,
        const CookedResourceBudget& b) const override
    {
        const auto& t = dynamic_cast<const ExternalAssetSourceResource&>(base);
        auto bytes = source(root, t.sourcePath, b.maxPayloadBytes);
        const auto document =
            binaryDocument({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        return {document.begin(), document.end(), &core::defaultCpuMemoryResource()};
    }

    CookedResourceResult preparePayload(ResourceBase& base,
        core::ArrayView<const std::uint8_t> bytes,
        std::size_t budget) const override
    {
        try {
            require(bytes.size() <= budget, "COOKED-DOCUMENT-BUDGET");
            const core::StringView document(
                reinterpret_cast<const char*>(bytes.data()), bytes.size());
            (void)reflection::BinaryDocument::parseSingleDocument(document);
            dynamic_cast<ExternalAssetSourceResource&>(base).cookedDocument = document;
            return {true};
        } catch (const reflection::BinaryDocumentError& e) {
            return {false, e.diagnostic};
        }
    }
};

class RuntimeCodec final : public ReflectedCookedResourceCodec<PrefabRuntimePayloadResource> {
public:
    std::uint64_t schema() const override
    {
        core::StableHash64 hash;
        hash.appendString("arp.prefab.runtime-blob.binary-documents/v1");
        hash.appendU64(ReflectedCookedResourceCodec<PrefabRuntimePayloadResource>::schema());
        return hash.value();
    }

    CookedBytes cookPayload(const ResourceBase& base,
        const std::filesystem::path& root,
        const CookedResourceBudget& b) const override
    {
        const auto& t = dynamic_cast<const PrefabRuntimePayloadResource&>(base);
        auto bytes = source(root, t.blobPath, b.maxPayloadBytes);
        auto decoded = decodePrefabRuntimePayload(bytes, t, b.maxDecodedBytes);
        require(decoded.success && decoded.hasCompleteControllerData, decoded.diagnostics.c_str());
        for (auto& material : decoded.asset.materials)
            if (!material.sourceMaterialDocument.empty())
                material.sourceMaterialDocument = binaryDocument(material.sourceMaterialDocument);
        PrefabRuntimePayloadWriteDesc desc;
        desc.asset = &decoded.asset;
        desc.controllerAsset = &decoded.controllerAsset;
        desc.defaultStateName = decoded.defaultStateName;
        desc.stateBindings = decoded.stateBindings;
        return encodePrefabRuntimePayload(desc);
    }

    CookedResourceResult preparePayload(ResourceBase& base,
        core::ArrayView<const std::uint8_t> bytes,
        std::size_t budget) const override
    {
        auto& t = dynamic_cast<PrefabRuntimePayloadResource&>(base);
        CookedBytes owned(bytes.begin(), bytes.end(), &core::defaultCpuMemoryResource());
        auto decoded = decodeCookedPrefabRuntimePayload(owned, t, budget);
        if (!decoded.success || !decoded.hasCompleteControllerData)
            return {false, decoded.diagnostics};
        t.runtimeAsset = core::makeShared<assets::AnimatedMeshAsset>(std::move(decoded.asset));
        t.controllerAsset = core::makeShared<animation::AnimatorControllerAsset>(
            std::move(decoded.controllerAsset));
        return {true};
    }
};

class ControllerCodec final : public ReflectedCookedResourceCodec<AnimatorControllerResource> {
public:
    CookedResourceResult beforeAdmission(
        ResourceBase& base, ResourceManager& manager) const override
    {
        for (const auto& d : base.dependencies())
            if (d.usage == "runtime-payload") {
                auto resource = manager.find<PrefabRuntimePayloadResource>(d.id);
                if (!resource.ready() || !resource.get()->controllerAsset)
                    return {false, "COOKED-CONTROLLER-DEPENDENCY"};
                dynamic_cast<AnimatorControllerResource&>(base).controllerAsset =
                    resource.get()->controllerAsset;
                return {true};
            }
        return {true};
    }
};
} // namespace

core::SharedPtr<const CookedResourceCodecRegistry> makePrefabCookedResourceCodecs()
{
    registerPrefabAssetReflection();
    auto r = core::makeShared<CookedResourceCodecRegistry>(32);
#define ADD(T)                                                                                     \
    require(r->add(core::makeShared<ReflectedCookedResourceCodec<T>>()), "COOKED-REGISTRY");
    ADD(PrefabResource);
    ADD(ModelResource);
    ADD(SkinnedMeshResource);
    ADD(SkeletonResource);
    ADD(AvatarResource);
    ADD(AnimationClipResource);
    ADD(AnimationSetResource);
    ADD(MaterialResource);
    ADD(MaterialSetResource);
    ADD(AuthoringAssetResource);
    ADD(ShaderGraphSourceResource);
    ADD(SequenceResource);
    ADD(SequenceBindingSetResource);
    ADD(PhysicsColliderResource);
    ADD(PreviewStudioResource);
    ADD(ParticleEffectResource);
#undef ADD
    require(r->add(core::makeShared<SourceCodec>()) && r->add(core::makeShared<TextureCodec>())
            && r->add(core::makeShared<RuntimeCodec>())
            && r->add(core::makeShared<ControllerCodec>()) && r->freeze(),
        "COOKED-REGISTRY");
    return r;
}

namespace {
void appendCookPackageInputs(const std::filesystem::path& packageRoot,
    core::pmr::Vector<ResourceCookInput>& inputs,
    CookedResourceBudget b)
{
    auto package =
        InternalAssetPackageReader().read(packageRoot, b.maxMetadataBytes, b.maxResources);
    require(package.success, package.diagnostics.c_str());
    // Existing typed inspector is the authoritative file/dependency binding contract.
    AssetPackagePayloadInspector inspector({}, packageRoot);
    core::pmr::Map<core::pmr::String, core::pmr::String> controllerRuntimes{
        &core::defaultCpuMemoryResource()};
    for (const auto& e : package.package.entries)
        if (e.resourceType == PrefabResource::resourceTypeName()) {
            core::pmr::String error{&core::defaultCpuMemoryResource()};
            auto value = createPrefabResourceFromEnvelope(e.envelopeYaml, error);
            require(bool(value), error.c_str());
            const auto& prefab = dynamic_cast<const PrefabResource&>(*value);
            if (!prefab.runtimePayloadId.empty() && !prefab.animatorControllerId.empty()) {
                const auto inserted = controllerRuntimes.emplace(
                    prefab.animatorControllerId, prefab.runtimePayloadId);
                require(inserted.second || inserted.first->second == prefab.runtimePayloadId,
                    "COOKED-CONTROLLER-AMBIGUOUS-RUNTIME");
            }
        }
    for (const auto& e : package.package.entries) {
        require(inputs.size() < b.maxResources, "COOKED-RESOURCE-BUDGET");
        AssetPackagePayloadIndex index;
        if (inspector.supports(e.resourceType)) {
            index = inspector.inspect(e, true);
            require(index.success, index.diagnostics.c_str());
        }
        {
            for (const auto& d : e.dependencies) {
                const auto declared = std::find_if(
                    index.dependencies.begin(), index.dependencies.end(), [&](const auto& value) {
                        return value.binding.id == d.id && value.binding.usage == d.usage;
                    });
                if (declared != index.dependencies.end())
                    continue;
                const char* expected = nullptr;
                static constexpr std::pair<const char*, const char*> types[] = {
                    {"source", "ExternalAssetSourceResource"},
                    {"model", "ModelResource"},
                    {"skinned-mesh", "SkinnedMeshResource"},
                    {"skeleton", "SkeletonResource"},
                    {"avatar", "AvatarResource"},
                    {"animation-set", "AnimationSetResource"},
                    {"animation-clip", "AnimationClipResource"},
                    {"animator-controller", "AnimatorControllerResource"},
                    {"material-set", "MaterialSetResource"},
                    {"material", "MaterialResource"},
                    {"runtime-payload", "PrefabRuntimePayloadResource"},
                    {"sequence", "SequenceResource"},
                    {"sequence-binding-set", "SequenceBindingSetResource"},
                    {"preview-studio", "PreviewStudioResource"},
                    {"shader", "ShaderPackageResource"},
                    {"texture", "TextureResource"},
                    {"prefab-binding", "PrefabResource"}};
                for (const auto& type : types)
                    if (d.usage == type.first)
                        expected = type.second;
                require(expected, "COOKED-DEPENDENCY-USAGE-UNSUPPORTED");
                index.dependencies.push_back({d, expected});
            }
        }
        ResourceCookInput input;
        core::pmr::String error{&core::defaultCpuMemoryResource()};
        input.resource = createPrefabResourceFromEnvelope(e.envelopeYaml, error);
        require(bool(input.resource), error.c_str());
        input.sourceRoot = packageRoot;
        for (const auto& d : index.dependencies)
            input.dependencies.push_back({d.binding, d.expectedType});
        const auto controller = controllerRuntimes.find(e.resourceId);
        if (controller != controllerRuntimes.end()
            && std::none_of(
                input.dependencies.begin(), input.dependencies.end(), [](const auto& d) {
                    return d.binding.usage == "runtime-payload";
                }))
            input.dependencies.push_back(
                {{controller->second, "runtime-payload"}, "PrefabRuntimePayloadResource"});
        inputs.push_back(std::move(input));
    }
}
} // namespace

CookedResourceResult ResourceCookService::cookPackage(const std::filesystem::path& packageRoot,
    const std::filesystem::path& output,
    core::SharedPtr<const CookedResourceCodecRegistry> codecs,
    CookedResourceBudget b) const
{
    try {
        core::pmr::Vector<ResourceCookInput> inputs{&core::defaultCpuMemoryResource()};
        appendCookPackageInputs(packageRoot, inputs, b);
        return cook({inputs.data(), inputs.size()}, output, std::move(codecs), b);
    } catch (const core::YamlContractError& e) {
        return {false, e.diagnostic};
    } catch (const reflection::BinaryDocumentError& e) {
        return {false, e.diagnostic};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

CookedResourceResult ResourceCookService::cookManifest(const std::filesystem::path& manifest,
    const std::filesystem::path& root,
    const std::filesystem::path& output,
    core::SharedPtr<const CookedResourceCodecRegistry> codecs,
    CookedResourceBudget b) const
{
    try {
        require(codecs && codecs->frozen(), "COOKED-REGISTRY");
        auto text = core::readTextFile(manifest, b.maxMetadataBytes);
        require(text.success, text.diagnostics.c_str());
        auto doc = core::YamlContract::parseSingleDocument(text.text);
        using Y = core::YamlContract;
        require(Y::text(doc, "type") == "ArpResourceCook"
                && Y::text(doc, "contractVersion") == "resource.cook/v1"
                && doc["packages"].is_sequence(),
            "COOKED-MANIFEST-CONTRACT");
        core::pmr::Vector<ResourceCookInput> inputs{&core::defaultCpuMemoryResource()};
        for (const auto& item : doc["packages"].get_value_ref<const Y::Node::sequence_type&>()) {
            require(item.is_string(), "COOKED-MANIFEST-PACKAGE");
            const auto& packageName = item.get_value_ref<const Y::Node::string_type&>();
            require(core::FilePath::isSafeRelativePath(packageName), "COOKED-MANIFEST-PATH");
            const auto packageRoot =
                root / std::filesystem::u8path(packageName.begin(), packageName.end());
            require(core::FilePath::contains(std::filesystem::weakly_canonical(root),
                        std::filesystem::weakly_canonical(packageRoot)),
                "COOKED-MANIFEST-SOURCE-ESCAPE");
            appendCookPackageInputs(packageRoot, inputs, b);
        }
        return cook({inputs.data(), inputs.size()}, output, std::move(codecs), b);
    } catch (const core::YamlContractError& e) {
        return {false, e.diagnostic};
    } catch (const reflection::BinaryDocumentError& e) {
        return {false, e.diagnostic};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}
} // namespace arp::resource
