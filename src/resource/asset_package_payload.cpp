#include "resource/asset_package_payload.h"

#include "core/io/filesystem_path.h"
#include "resource/prefab_asset_resources.h"

namespace arp::resource {

AssetPackageCodecRegistry::AssetPackageCodecRegistry(std::size_t capacity) : capacity_(capacity)
{
    codecs_.reserve(capacity);
}

const AssetPackageCodec* AssetPackageCodecRegistry::find(NameView type) const noexcept
{
    for (const auto& codec : codecs_)
        if (codec->resourceType() == type)
            return codec.get();
    return nullptr;
}

bool AssetPackageCodecRegistry::add(
    core::SharedPtr<const AssetPackageCodec> codec, core::pmr::String& diagnostic)
{
    return addAll({std::move(codec)}, diagnostic);
}

bool AssetPackageCodecRegistry::addAll(
    std::initializer_list<core::SharedPtr<const AssetPackageCodec>> codecs,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (frozen_ || codecs.size() == 0) {
        diagnostic = "PACKAGE-CODEC-REGISTRATION-INVALID-OR-DUPLICATE";
        return false;
    }
    if (codecs.size() > capacity_ - codecs_.size()) {
        diagnostic = "PACKAGE-CODEC-CAPACITY";
        return false;
    }
    for (const auto& codec : codecs) {
        if (!codec || codec->resourceType().empty() || find(codec->resourceType())
            || AssetPackagePayloadInspector().supports(codec->resourceType())
            || codec->resourceType() == "TerrainAssetResource"
            || codec->resourceType() == "VegetationCatalogResource"
            || prefabResourceFactory().supports(codec->resourceType())) {
            diagnostic = "PACKAGE-CODEC-REGISTRATION-INVALID-OR-DUPLICATE";
            return false;
        }
        for (const auto& other : codecs)
            if (&other != &codec && other && other->resourceType() == codec->resourceType()) {
                diagnostic = "PACKAGE-CODEC-REGISTRATION-INVALID-OR-DUPLICATE";
                return false;
            }
    }
    for (const auto& codec : codecs)
        codecs_.push_back(codec);
    return true;
}

bool AssetPackageCodecRegistry::freeze(core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!capacity_) {
        diagnostic = "PACKAGE-CODEC-CAPACITY";
        return false;
    }
    frozen_ = true;
    return true;
}

bool AssetPackagePayloadInspector::supports(NameView type) const noexcept
{
    return (codecs_ && codecs_->frozen() && codecs_->find(type))
        || type == MaterialResource::resourceTypeName()
        || type == TextureResource::resourceTypeName()
        || type == ShaderPackageResource::resourceTypeName()
        || type == ExternalAssetSourceResource::resourceTypeName();
}

AssetPackagePayloadIndex AssetPackagePayloadInspector::inspect(
    const InternalAssetPackageEntry& entry, bool requireFileBindings) const
{
    AssetPackagePayloadIndex result;
    if (codecs_ && !codecs_->frozen()) {
        result.diagnostics = "PACKAGE-CODEC-REGISTRY-NOT-FROZEN";
        return result;
    }
    if (codecs_)
        if (const auto* codec = codecs_->find(entry.resourceType)) {
            try {
                result = codec->inspect(entry, root_);
                if (!result.success)
                    return result;
                const auto root = std::filesystem::canonical(root_);
                for (const auto& file : result.files) {
                    if (file.find('\0') != core::pmr::String::npos
                        || !core::FilePath::isSafeRelativePath(file)
                        || !core::FilePath::contains(root,
                            std::filesystem::canonical(
                                root / std::filesystem::path(file.c_str())))) {
                        result.success = false;
                        result.diagnostics = "PACKAGE-CODEC-UNSAFE-FILE-BINDING";
                        return result;
                    }
                }
                for (const auto& d : result.dependencies)
                    if (!isResourceUuid(d.binding.id) || d.binding.usage.empty()
                        || d.expectedType.empty()) {
                        result.success = false;
                        result.diagnostics = "PACKAGE-CODEC-INVALID-DEPENDENCY";
                        return result;
                    }
                return result;
            } catch (...) {
                result.success = false;
                result.diagnostics = "PACKAGE-CODEC-INSPECTION-FAILED";
                return result;
            }
        }
    if (!supports(entry.resourceType)) {
        result.diagnostics = "PACKAGE-PAYLOAD-CONTRACT-UNSUPPORTED";
        return result;
    }
    registerPrefabAssetReflection();
    auto payload = createPrefabResourceFromEnvelope(entry.envelopeYaml, result.diagnostics);
    if (!payload)
        return result;
    const auto validateId = [&](const core::pmr::String& id) {
        return id.empty() || isResourceUuid(id);
    };
    const auto file = [&](const core::pmr::String& path) {
        if (path.find('\0') != core::pmr::String::npos || !core::FilePath::isSafeRelativePath(path))
            return false;
        result.files.push_back(path);
        return true;
    };
    if (const auto* material = dynamic_cast<const MaterialResource*>(payload.get())) {
        for (const auto* id : {&material->sourceResourceId,
                 &material->shaderPackageId,
                 &material->baseColorTextureId,
                 &material->normalTextureId,
                 &material->metallicRoughnessTextureId,
                 &material->occlusionTextureId,
                 &material->emissiveTextureId}) {
            if (!validateId(*id)) {
                result.diagnostics = "PACKAGE-PAYLOAD-INVALID-RESOURCE-ID";
                return result;
            }
        }
        for (auto binding : material->dependencies()) {
            const auto* expected = binding.usage == "source"
                ? ExternalAssetSourceResource::resourceTypeName()
                : binding.usage == "shader" ? ShaderPackageResource::resourceTypeName()
                                            : TextureResource::resourceTypeName();
            result.dependencies.push_back({std::move(binding), expected});
        }
    } else if (const auto* texture = dynamic_cast<const TextureResource*>(payload.get())) {
        if (!validateId(texture->sourceResourceId)) {
            result.diagnostics = "PACKAGE-PAYLOAD-INVALID-RESOURCE-ID";
            return result;
        }
        for (auto binding : texture->dependencies())
            result.dependencies.push_back(
                {std::move(binding), ExternalAssetSourceResource::resourceTypeName()});
        if (requireFileBindings && !file(texture->sourcePath)) {
            result.diagnostics = "PACKAGE-TEXTURE-FILE-BINDING";
            return result;
        }
    } else if (const auto* shader = dynamic_cast<const ShaderPackageResource*>(payload.get())) {
        if (requireFileBindings)
            for (const auto* path :
                {&shader->slangPath, &shader->spirvPath, &shader->mslPath, &shader->reflectionPath})
                if (!file(*path)) {
                    result.diagnostics = "PACKAGE-SHADER-FILE-BINDING";
                    return result;
                }
    } else if (!dynamic_cast<const ExternalAssetSourceResource*>(payload.get())) {
        result.diagnostics = "PACKAGE-PAYLOAD-TYPE-MISMATCH";
        return result;
    }
    result.success = true;
    return result;
}

bool AssetPackagePayloadInspector::externalReady(
    const ResourceManager& manager, const AssetPackageTypedDependency& dependency) const
{
    const NameView type = dependency.expectedType;
    if (type == TextureResource::resourceTypeName())
        return manager.find<TextureResource>(dependency.binding.id).ready();
    if (type == ShaderPackageResource::resourceTypeName())
        return manager.find<ShaderPackageResource>(dependency.binding.id).ready();
    if (type == ExternalAssetSourceResource::resourceTypeName())
        return manager.find<ExternalAssetSourceResource>(dependency.binding.id).ready();
    const auto* record = manager.recordSnapshot(dependency.binding.id);
    return record && record->state == ResourceState::Ready && record->type == type;
}
} // namespace arp::resource
