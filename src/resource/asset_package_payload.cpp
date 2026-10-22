#include "resource/asset_package_payload.h"

#include "core/io/filesystem_path.h"
#include "resource/prefab_asset_resources.h"

namespace arp::resource {

bool AssetPackagePayloadInspector::supports(NameView type) const noexcept
{
    return type == MaterialResource::resourceTypeName()
        || type == TextureResource::resourceTypeName()
        || type == ShaderPackageResource::resourceTypeName()
        || type == ExternalAssetSourceResource::resourceTypeName();
}

AssetPackagePayloadIndex AssetPackagePayloadInspector::inspect(
    const InternalAssetPackageEntry& entry, bool requireFileBindings) const
{
    AssetPackagePayloadIndex result;
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
    return false;
}
} // namespace arp::resource
