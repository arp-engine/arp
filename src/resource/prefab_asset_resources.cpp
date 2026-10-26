#include "resource/prefab_asset_resources.h"

#include "core/encoding/yaml_contract.h"
#include "core/hash/sha1.h"
#include "core/io/filesystem_path.h"
#include "reflection/serialization.h"
#include "resource/particle_asset_resources.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fkYAML/Node.hpp>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

namespace arp::reflection::generated {
void register_prefab_asset_resources_reflection();
}

namespace arp::resource {

namespace {

constexpr const char* kSourceKindUnityPrefabFolder = "UnityPrefabFolder";
constexpr const char* kPrototypeStatus = "Prototype Complete";

arp::core::pmr::String prototypeStatus()
{
    return arp::core::pmr::String(kPrototypeStatus, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String makeString(arp::reflection::NameView text)
{
    return arp::core::pmr::String(text.data(), text.size(), &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String makeDiagnostic(const char* text)
{
    return arp::core::pmr::String(text, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String makeDiagnostic(const char* prefix, arp::reflection::NameView value)
{
    arp::core::pmr::String result(prefix, &arp::core::defaultCpuMemoryResource());
    result.append(value.data(), value.size());
    return result;
}

ResourceDependency dependency(const arp::core::pmr::String& id, const char* usage)
{
    ResourceDependency result;
    result.id = id;
    result.usage = makeString(usage);
    return result;
}

bool hasDependencyId(const arp::core::pmr::String& id)
{
    return !id.empty() && isResourceUuid(id);
}

void pushDependencyIfValid(arp::core::pmr::Vector<ResourceDependency>& out,
    const arp::core::pmr::String& id,
    const char* usage)
{
    if (hasDependencyId(id)) {
        out.push_back(dependency(id, usage));
    }
}

arp::core::pmr::Vector<ResourceDependency> noDependencies()
{
    return arp::core::pmr::Vector<ResourceDependency>{&arp::core::defaultCpuMemoryResource()};
}

arp::core::pmr::String pathString(const std::filesystem::path& path)
{
    return makeString(path.generic_string());
}

arp::core::pmr::String filenameString(const std::filesystem::path& path)
{
    return makeString(path.filename().generic_string());
}

arp::core::pmr::String lowerString(arp::core::pmr::String value)
{
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool readTextFile(const std::filesystem::path& path, arp::core::pmr::String& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = arp::core::toPmrString(buffer.str());
    return true;
}

std::uint32_t hashText(arp::reflection::NameView text)
{
    std::uint32_t hash = 2166136261u;
    for (const auto ch : text) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 16777619u;
    }
    return hash;
}

std::uint32_t hashFileIfReadable(const std::filesystem::path& path)
{
    arp::core::pmr::String text(&arp::core::defaultCpuMemoryResource());
    if (!readTextFile(path, text)) {
        return 0u;
    }
    return hashText(text);
}

arp::core::pmr::String metaGuidFor(const std::filesystem::path& path)
{
    arp::core::pmr::String text(&arp::core::defaultCpuMemoryResource());
    if (!readTextFile(std::filesystem::path(path.generic_string() + ".meta"), text)) {
        return arp::core::pmr::String(&arp::core::defaultCpuMemoryResource());
    }
    constexpr arp::reflection::NameView prefix = "guid:";
    const auto offset = text.find(prefix);
    if (offset == arp::core::pmr::String::npos) {
        return arp::core::pmr::String(&arp::core::defaultCpuMemoryResource());
    }
    auto cursor = offset + prefix.size();
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    auto end = cursor;
    while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    return arp::core::pmr::String(
        text.data() + cursor, end - cursor, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String assetTypeForExtension(const std::filesystem::path& path)
{
    const auto extension = lowerString(pathString(path.extension()));
    if (extension == ".fbx") {
        return makeString("Model");
    }
    if (extension == ".prefab") {
        return makeString("Prefab");
    }
    if (extension == ".mat") {
        return makeString("Material");
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
        return makeString("Texture");
    }
    if (extension == ".psd" || extension == ".ai" || extension == ".pdf" || extension == ".svg") {
        return makeString("Authoring");
    }
    if (extension == ".shadergraph" || extension == ".shadersubgraph") {
        return makeString("ShaderGraphSource");
    }
    if (extension == ".playable") {
        return makeString("Sequence");
    }
    if (extension == ".unity") {
        return makeString("Scene");
    }
    if (extension == ".asset") {
        return makeString("SettingsAsset");
    }
    return makeString("External");
}

bool isPackageRelevantSource(const std::filesystem::path& path)
{
    const auto extension = lowerString(pathString(path.extension()));
    return extension == ".fbx" || extension == ".prefab" || extension == ".mat"
        || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".psd"
        || extension == ".ai" || extension == ".pdf" || extension == ".svg"
        || extension == ".shadergraph" || extension == ".shadersubgraph" || extension == ".playable"
        || extension == ".unity" || extension == ".asset";
}

arp::core::pmr::String sanitizePathPart(const arp::core::pmr::String& value)
{
    arp::core::pmr::String out(&arp::core::defaultCpuMemoryResource());
    out.reserve(value.size());
    for (const auto ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            out.push_back(static_cast<char>(std::tolower(uch)));
        } else if (ch == '.' || ch == '-' || ch == '_') {
            out.push_back(ch);
        } else {
            out.push_back('-');
        }
    }
    if (out.empty()) {
        out = makeString("asset");
    }
    return out;
}

arp::core::pmr::String resourceEnvelopePath(
    const char* prefix, const arp::core::pmr::String& sourceName, const arp::core::pmr::String& id)
{
    arp::core::pmr::String result("resources/", &arp::core::defaultCpuMemoryResource());
    result += prefix;
    result += "/";
    result += sanitizePathPart(sourceName);
    if (id.size() >= 8u) {
        result += "-";
        result.append(id.data(), 8u);
    }
    result += ".arpres.yaml";
    return result;
}

arp::core::pmr::String deterministicUuid(const arp::core::pmr::String& seed, const char* role)
{
    return deriveResourceUuid(std::string_view(seed.data(), seed.size()), role);
}

arp::core::pmr::String graphSeed(const PrefabAssetGraphBuildDesc& desc)
{
    if (!desc.packageSeed.empty()) {
        return desc.packageSeed;
    }
    arp::core::pmr::String seed(
        "ArpInternalAssetPackage.PrefabAssetGraph.v1|", &arp::core::defaultCpuMemoryResource());
    seed += desc.sourcePath;
    seed += "|";
    seed += desc.alias;
    return seed;
}

PrefabAssetGraphIds allocateGraphIds(const PrefabAssetGraphBuildDesc& desc)
{
    const auto seed = graphSeed(desc);
    PrefabAssetGraphIds ids = desc.ids;
    if (ids.prefabId.empty()) {
        ids.prefabId = hasDependencyId(ids.rootId) ? ids.rootId
                                                   : deterministicUuid(seed, "PrefabResource/root");
    }
    if (ids.rootId.empty()) {
        ids.rootId = ids.prefabId;
    }
    if (ids.modelId.empty()) {
        ids.modelId = deterministicUuid(seed, "ModelResource/model");
    }
    if (ids.skinnedMeshId.empty()) {
        ids.skinnedMeshId = deterministicUuid(seed, "SkinnedMeshResource/skinned-mesh");
    }
    if (ids.skeletonId.empty()) {
        ids.skeletonId = deterministicUuid(seed, "SkeletonResource/skeleton");
    }
    if (ids.avatarId.empty()) {
        ids.avatarId = deterministicUuid(seed, "AvatarResource/avatar");
    }
    if (ids.animationSetId.empty()) {
        ids.animationSetId = deterministicUuid(seed, "AnimationSetResource/animation-set");
    }
    if (ids.animatorControllerId.empty()) {
        ids.animatorControllerId = deterministicUuid(seed, "AnimatorControllerResource/animator");
    }
    if (ids.materialSetId.empty()) {
        ids.materialSetId = deterministicUuid(seed, "MaterialSetResource/material-set");
    }
    if (ids.runtimePayloadId.empty()) {
        ids.runtimePayloadId =
            deterministicUuid(seed, "PrefabRuntimePayloadResource/runtime-payload");
    }
    if (ids.sequenceId.empty()) {
        ids.sequenceId = deterministicUuid(seed, "SequenceResource/sequence");
    }
    if (ids.sequenceBindingSetId.empty()) {
        ids.sequenceBindingSetId =
            deterministicUuid(seed, "SequenceBindingSetResource/sequence-bindings");
    }
    if (ids.previewStudioId.empty()) {
        ids.previewStudioId = deterministicUuid(seed, "PreviewStudioResource/preview-studio");
    }
    if (ids.basicShaderId.empty()) {
        ids.basicShaderId = deterministicUuid(seed, "ShaderPackageResource/shader/basic");
    }
    if (ids.stylizedShaderId.empty()) {
        ids.stylizedShaderId = deterministicUuid(seed, "ShaderPackageResource/shader/stylized");
    }
    if (ids.guiShaderId.empty()) {
        ids.guiShaderId = deterministicUuid(seed, "ShaderPackageResource/shader/gui");
    }
    if (ids.startupSplashShaderId.empty()) {
        ids.startupSplashShaderId =
            deterministicUuid(seed, "ShaderPackageResource/shader/startup-splash");
    }
    return ids;
}

bool validateUniqueGraphIds(const PrefabAssetGraphIds& ids, arp::core::pmr::String& diagnostics)
{
    const arp::core::pmr::String* values[] = {
        &ids.prefabId,
        &ids.modelId,
        &ids.skinnedMeshId,
        &ids.skeletonId,
        &ids.avatarId,
        &ids.animationSetId,
        &ids.animatorControllerId,
        &ids.materialSetId,
        &ids.runtimePayloadId,
        &ids.sequenceId,
        &ids.sequenceBindingSetId,
        &ids.previewStudioId,
        &ids.basicShaderId,
        &ids.stylizedShaderId,
        &ids.guiShaderId,
        &ids.startupSplashShaderId,
    };
    for (std::size_t outer = 0; outer < std::size(values); ++outer) {
        if (values[outer]->empty() || !isResourceUuid(*values[outer])) {
            diagnostics = makeDiagnostic("missing or invalid internal resource UUID");
            return false;
        }
        for (std::size_t inner = outer + 1u; inner < std::size(values); ++inner) {
            if (*values[outer] == *values[inner]) {
                diagnostics = makeDiagnostic("duplicate internal resource UUID: ", *values[outer]);
                return false;
            }
        }
    }
    return true;
}

arp::core::pmr::String childAlias(const arp::core::pmr::String& rootAlias, const char* suffix)
{
    arp::core::pmr::String result(rootAlias, &arp::core::defaultCpuMemoryResource());
    result += suffix;
    return result;
}

arp::core::pmr::String displayWithSuffix(
    const arp::core::pmr::String& displayName, const char* suffix)
{
    arp::core::pmr::String result(displayName, &arp::core::defaultCpuMemoryResource());
    result += suffix;
    return result;
}

AssetResourcePackageBuildDesc packageBuildDesc(const arp::core::pmr::String& resourceId,
    const arp::core::pmr::String& alias,
    const arp::core::pmr::String& displayName,
    const char* envelopePath)
{
    AssetResourcePackageBuildDesc desc;
    desc.resourceId = resourceId;
    desc.alias = alias;
    desc.displayName = displayName;
    desc.status = prototypeStatus();
    desc.envelopePath = makeString(envelopePath);
    return desc;
}

template <typename T>
AssetResourcePackageBuildResult buildPackageEntryFor(const T& payload,
    const char* resourceType,
    const char* defaultEnvelopePath,
    const AssetResourcePackageBuildDesc& desc)
{
    AssetResourcePackageBuildResult result;
    ResourceEnvelopeBuildDesc envelopeDesc;
    envelopeDesc.resourceId = desc.resourceId;
    envelopeDesc.resourceType = makeString(resourceType);
    envelopeDesc.alias = desc.alias;
    envelopeDesc.displayName = desc.displayName.empty() ? desc.alias : desc.displayName;
    envelopeDesc.status = desc.status.empty() ? prototypeStatus() : desc.status;
    envelopeDesc.dependencies = payload.dependencies();

    const auto envelope = buildResourceEnvelope(envelopeDesc, arp::reflection::object(payload));
    if (!envelope.success) {
        result.diagnostics = envelope.diagnostics;
        return result;
    }

    result.entry.resourceId = envelopeDesc.resourceId;
    result.entry.resourceType = envelopeDesc.resourceType;
    result.entry.alias = envelopeDesc.alias;
    result.entry.envelopePath =
        desc.envelopePath.empty() ? makeString(defaultEnvelopePath) : desc.envelopePath;
    result.entry.status = envelopeDesc.status;
    result.entry.contentHash = envelope.contentHash;
    result.entry.dependencies = envelopeDesc.dependencies;
    result.entry.envelopeYaml = envelope.envelopeYaml;
    result.success = true;
    return result;
}

template <typename T>
ResourceLoadResult loadIntoFor(const T& payload,
    ResourceManager& manager,
    const char* resourceType,
    const char* defaultEnvelopePath,
    const AssetResourceRuntimeLoadDesc& desc)
{
    const auto entry =
        buildPackageEntryFor(payload, resourceType, defaultEnvelopePath, desc.package);
    if (!entry.success) {
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            desc.package.resourceId,
            makeString(resourceType),
            entry.diagnostics,
        };
    }
    auto typed = core::makeUnique<T>(payload);
    typed->configureMetadata(
        ResourceReference{
            entry.entry.resourceId,
            entry.entry.resourceType,
            entry.entry.alias,
            entry.entry.status,
            desc.package.displayName.empty() ? desc.package.alias : desc.package.displayName,
        },
        entry.entry.dependencies);
    (void)desc.preserveRuntimeObject;
    return manager.loadResource(std::move(typed));
}

bool hasSlangExtension(const arp::core::pmr::String& path)
{
    constexpr arp::reflection::NameView extension = ".slang";
    if (path.size() < extension.size()) {
        return false;
    }
    return arp::reflection::NameView(path.data() + path.size() - extension.size(), extension.size())
        == extension;
}

arp::core::pmr::String replaceSlangExtension(
    const arp::core::pmr::String& slangPath, const char* replacement)
{
    arp::core::pmr::String result(slangPath.data(),
        slangPath.size() - arp::reflection::NameView(".slang").size(),
        &arp::core::defaultCpuMemoryResource());
    result += replacement;
    return result;
}

bool buildShaderPackage(const char* name,
    const arp::core::pmr::String& slangPath,
    ShaderPackageResource& out,
    arp::core::pmr::String& diagnostics)
{
    if (slangPath.empty() || !hasSlangExtension(slangPath)) {
        diagnostics = makeDiagnostic("shader resource requires Slang: ", name);
        return false;
    }
    out.name = makeString(name);
    out.slangPath = slangPath;
    out.spirvPath = replaceSlangExtension(slangPath, ".spv");
    out.mslPath = replaceSlangExtension(slangPath, ".metal");
    out.reflectionPath = replaceSlangExtension(slangPath, ".reflection.yaml");
    return true;
}

constexpr std::uint32_t kPrefabRuntimeBlobMagic = 0x41525046u;
constexpr std::uint32_t kPrefabRuntimeBlobLegacyV1 = 1u;
constexpr std::uint32_t kPrefabRuntimeBlobVersionWithSourceDocument = 2u;
constexpr std::uint32_t kPrefabRuntimeBlobVersionWithMaterialMetadata = 3u;
constexpr std::uint32_t kPrefabRuntimeBlobVersion = 4u;

std::uint32_t hashBytes(const arp::core::pmr::Vector<std::uint8_t>& bytes)
{
    std::uint32_t hash = 2166136261u;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

bool readBinaryFile(const std::filesystem::path& path,
    std::size_t expectedSize,
    arp::core::pmr::Vector<std::uint8_t>& out,
    arp::core::pmr::String& diagnostics)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        diagnostics = makeDiagnostic("missing runtime payload blob: ");
        diagnostics += pathString(path);
        return false;
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0 || static_cast<std::uintmax_t>(size) != expectedSize) {
        diagnostics = makeDiagnostic("runtime payload blob byte size mismatch");
        return false;
    }
    if (static_cast<std::uintmax_t>(size)
        > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        diagnostics = makeDiagnostic("runtime payload blob exceeds stream read range");
        diagnostics += pathString(path);
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    if (!file.good()) {
        diagnostics = makeDiagnostic("failed to read runtime payload blob: ");
        diagnostics += pathString(path);
        return false;
    }
    return true;
}

bool writeBinaryFile(const std::filesystem::path& path,
    const arp::core::pmr::Vector<std::uint8_t>& bytes,
    arp::core::pmr::String& diagnostics)
{
    try {
        std::filesystem::create_directories(path.parent_path());
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("failed to create runtime payload directory: ");
        diagnostics += err.what();
        return false;
    }
    std::ofstream file(path, std::ios::binary);
    if (!file.good()) {
        diagnostics = makeDiagnostic("failed to write runtime payload blob: ");
        diagnostics += pathString(path);
        return false;
    }
    file.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file.good()) {
        diagnostics = makeDiagnostic("failed to flush runtime payload blob: ");
        diagnostics += pathString(path);
        return false;
    }
    return true;
}

class RuntimeBlobWriter final {
public:
    template <typename T> void POD(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* ptr = reinterpret_cast<const std::uint8_t*>(&value);
        bytes_.insert(bytes_.end(), ptr, ptr + sizeof(T));
    }

    void string(const std::pmr::string& value)
    {
        const auto size = static_cast<std::uint32_t>(value.size());
        POD(size);
        const auto* ptr = reinterpret_cast<const std::uint8_t*>(value.data());
        bytes_.insert(bytes_.end(), ptr, ptr + value.size());
    }

    const arp::core::pmr::Vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    arp::core::pmr::Vector<std::uint8_t> bytes_{&arp::core::defaultCpuMemoryResource()};
};

class RuntimeBlobReader final {
public:
    explicit RuntimeBlobReader(const arp::core::pmr::Vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    template <typename T> bool POD(T& out)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (cursor_ + sizeof(T) > bytes_.size()) {
            return false;
        }
        std::memcpy(&out, bytes_.data() + cursor_, sizeof(T));
        cursor_ += sizeof(T);
        return true;
    }

    bool string(std::pmr::string& out)
    {
        std::uint32_t size = 0;
        if (!POD(size) || size > remaining()) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes_.data() + cursor_), size);
        cursor_ += size;
        return true;
    }

    bool done() const { return cursor_ == bytes_.size(); }

    std::size_t remaining() const { return bytes_.size() - cursor_; }

private:
    const arp::core::pmr::Vector<std::uint8_t>& bytes_;
    std::size_t cursor_ = 0;
};

void writeVec3(RuntimeBlobWriter& writer, const animation::Vec3& value)
{
    writer.POD(value.x);
    writer.POD(value.y);
    writer.POD(value.z);
}

void writeQuat(RuntimeBlobWriter& writer, const animation::Quat& value)
{
    writer.POD(value.x);
    writer.POD(value.y);
    writer.POD(value.z);
    writer.POD(value.w);
}

void writeTransform(RuntimeBlobWriter& writer, const animation::JointTransform& value)
{
    writeVec3(writer, value.translation);
    writeQuat(writer, value.rotation);
    writeVec3(writer, value.scale);
}

void writeTransform(RuntimeBlobWriter& writer, const animation::LocalTransform& value)
{
    writeVec3(writer, value.translation);
    writeQuat(writer, value.rotation);
    writeVec3(writer, value.scale);
}

bool readVec3(RuntimeBlobReader& reader, animation::Vec3& out)
{
    return reader.POD(out.x) && reader.POD(out.y) && reader.POD(out.z);
}

bool readQuat(RuntimeBlobReader& reader, animation::Quat& out)
{
    return reader.POD(out.x) && reader.POD(out.y) && reader.POD(out.z) && reader.POD(out.w);
}

bool readTransform(RuntimeBlobReader& reader, animation::JointTransform& out)
{
    return readVec3(reader, out.translation) && readQuat(reader, out.rotation)
        && readVec3(reader, out.scale);
}

bool readTransform(RuntimeBlobReader& reader, animation::LocalTransform& out)
{
    return readVec3(reader, out.translation) && readQuat(reader, out.rotation)
        && readVec3(reader, out.scale);
}

template <typename T, typename Fn>
void writeVector(RuntimeBlobWriter& writer, const T& values, Fn&& writeOne)
{
    const auto count = static_cast<std::uint32_t>(values.size());
    writer.POD(count);
    for (const auto& value : values) {
        writeOne(value);
    }
}

template <typename T, typename Fn>
bool readVector(RuntimeBlobReader& reader, T& values, Fn&& readOne)
{
    std::uint32_t count = 0;
    if (!reader.POD(count) || count > reader.remaining()) {
        return false;
    }
    values.clear();
    for (std::uint32_t index = 0; index < count; ++index) {
        typename T::value_type value{};
        if (!readOne(value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool readImportedMaterialRuntimeBlob(
    RuntimeBlobReader& reader, std::uint32_t version, arp::assets::ImportedMaterialAsset& material)
{
    if (!reader.string(material.name)) {
        return false;
    }
    if (version >= kPrefabRuntimeBlobVersionWithSourceDocument) {
        if (!reader.string(material.sourceMaterialDocument)) {
            return false;
        }
    } else {
        material.sourceMaterialDocument = makeString("");
    }
    for (auto& value : material.baseColorFactor)
        if (!reader.POD(value))
            return false;
    for (auto& value : material.emissiveFactor)
        if (!reader.POD(value))
            return false;
    return reader.POD(material.metallicFactor) && reader.POD(material.roughnessFactor)
        && reader.POD(material.baseColorTexture) && reader.POD(material.normalTexture)
        && reader.POD(material.metallicRoughnessTexture) && reader.POD(material.occlusionTexture)
        && reader.POD(material.emissiveTexture) && reader.POD(material.clearcoatTexture)
        && reader.POD(material.clearcoatRoughnessTexture)
        && reader.POD(material.clearcoatNormalTexture) && reader.POD(material.specularTexture)
        && reader.POD(material.specularColorTexture) && reader.POD(material.sheenColorTexture)
        && reader.POD(material.sheenRoughnessTexture) && reader.POD(material.transmissionTexture)
        && reader.POD(material.thicknessTexture) && reader.POD(material.iridescenceTexture)
        && reader.POD(material.iridescenceThicknessTexture)
        && reader.POD(material.occlusionStrength) && reader.POD(material.clearcoatFactor)
        && reader.POD(material.clearcoatRoughnessFactor) && reader.POD(material.specularFactor) &&
        [&]() {
            for (auto& value : material.specularColorFactor)
                if (!reader.POD(value))
                    return false;
            for (auto& value : material.sheenColorFactor)
                if (!reader.POD(value))
                    return false;
            return true;
        }()
        && reader.POD(material.sheenRoughnessFactor) && reader.POD(material.transmissionFactor)
        && reader.POD(material.thicknessFactor) &&
        [&]() {
            for (auto& value : material.attenuationColor)
                if (!reader.POD(value))
                    return false;
            return true;
        }()
        && reader.POD(material.attenuationDistance) && reader.POD(material.ior)
        && reader.POD(material.iridescenceFactor) && reader.POD(material.iridescenceIor)
        && reader.POD(material.emissiveStrength) && reader.POD(material.alphaMode)
        && reader.POD(material.alphaCutoff) && reader.POD(material.doubleSided)
        && (version < kPrefabRuntimeBlobVersionWithMaterialMetadata
            || (reader.POD(material.baseColorTextureCoordinateSet)
                && reader.POD(material.normalTextureCoordinateSet)
                && reader.POD(material.metallicRoughnessTextureCoordinateSet)
                && reader.POD(material.occlusionTextureCoordinateSet)
                && reader.POD(material.emissiveTextureCoordinateSet)
                && reader.POD(material.hasCompleteMaterialImportMetadata)
                && reader.POD(material.hasUnsupportedMaterialExtensions)));
}

arp::core::pmr::String defaultClipForStateBindings(
    const arp::core::pmr::Vector<PrefabRuntimeStateBinding>& bindings,
    const arp::core::pmr::String& defaultStateName)
{
    for (const auto& binding : bindings) {
        if (binding.stateName == defaultStateName) {
            return binding.clipName;
        }
    }
    return bindings.empty() ? arp::core::pmr::String(&arp::core::defaultCpuMemoryResource())
                            : bindings.front().clipName;
}

bool validateAnimatorControllerForPayload(const animation::AnimatorControllerAsset& controller,
    const arp::assets::AnimatedMeshAsset& asset,
    const arp::core::pmr::String& defaultStateName,
    const arp::core::pmr::Vector<PrefabRuntimeStateBinding>& stateBindings,
    arp::core::pmr::String& diagnostics)
{
    if (controller.name.empty() || controller.defaultStateName != defaultStateName
        || controller.states.empty()) {
        diagnostics = makeDiagnostic("runtime payload animator controller is incomplete");
        return false;
    }
    const auto stateExists = [&](const std::pmr::string& name) {
        return std::find_if(controller.states.begin(),
                   controller.states.end(),
                   [&](const animation::AnimatorState& state) {
                       return state.name == name;
                   })
            != controller.states.end();
    };
    const auto parameter =
        [&](const std::pmr::string& name) -> const animation::AnimatorParameterDefinition* {
        const auto found = std::find_if(controller.parameters.begin(),
            controller.parameters.end(),
            [&](const animation::AnimatorParameterDefinition& value) {
                return value.name == name;
            });
        return found == controller.parameters.end() ? nullptr : &*found;
    };
    if (!stateExists(controller.defaultStateName)) {
        diagnostics = makeDiagnostic("runtime payload animator default state is missing");
        return false;
    }
    for (std::size_t index = 0; index < controller.parameters.size(); ++index) {
        const auto& value = controller.parameters[index];
        if (value.name.empty()
            || static_cast<unsigned>(value.type)
                > static_cast<unsigned>(animation::AnimatorParameterType::Trigger)
            || std::find_if(controller.parameters.begin(),
                   controller.parameters.begin() + static_cast<std::ptrdiff_t>(index),
                   [&](const auto& previous) {
                       return previous.name == value.name;
                   })
                != controller.parameters.begin() + static_cast<std::ptrdiff_t>(index)) {
            diagnostics = makeDiagnostic("runtime payload animator parameter is invalid");
            return false;
        }
    }
    for (std::size_t index = 0; index < controller.states.size(); ++index) {
        const auto& state = controller.states[index];
        const auto clip = std::find_if(
            asset.clips.begin(), asset.clips.end(), [&](const animation::AnimationClip& value) {
                return value.name == state.clipName;
            });
        if (state.name.empty() || state.clipName.empty() || !std::isfinite(state.speed)
            || state.speed <= 0.0f || clip == asset.clips.end()
            || std::find_if(controller.states.begin(),
                   controller.states.begin() + static_cast<std::ptrdiff_t>(index),
                   [&](const auto& previous) {
                       return previous.name == state.name;
                   })
                != controller.states.begin() + static_cast<std::ptrdiff_t>(index)) {
            diagnostics = makeDiagnostic("runtime payload animator state is invalid");
            return false;
        }
    }
    for (const auto& transition : controller.transitions) {
        if (!stateExists(transition.sourceState) || !stateExists(transition.destinationState)
            || !std::isfinite(transition.durationSeconds) || transition.durationSeconds < 0.0f
            || !std::isfinite(transition.exitTime)) {
            diagnostics = makeDiagnostic("runtime payload animator transition is invalid");
            return false;
        }
        for (const auto& condition : transition.conditions) {
            if (parameter(condition.parameter) == nullptr || !std::isfinite(condition.threshold)
                || static_cast<unsigned>(condition.mode)
                    > static_cast<unsigned>(animation::AnimatorConditionMode::NotEqual)) {
                diagnostics = makeDiagnostic("runtime payload animator condition is invalid");
                return false;
            }
        }
    }
    for (const auto& binding : stateBindings) {
        const auto state = std::find_if(controller.states.begin(),
            controller.states.end(),
            [&](const animation::AnimatorState& value) {
                return value.name == binding.stateName;
            });
        if (state == controller.states.end() || state->clipName != binding.clipName
            || state->speed != binding.speed) {
            diagnostics = makeDiagnostic("runtime payload state binding mismatches controller");
            return false;
        }
    }
    return true;
}

void writeAnimatorController(
    RuntimeBlobWriter& writer, const animation::AnimatorControllerAsset& controller)
{
    writer.string(controller.name);
    writer.string(controller.defaultStateName);
    writeVector(
        writer, controller.parameters, [&](const animation::AnimatorParameterDefinition& value) {
            writer.string(value.name);
            writer.POD(value.type);
            writer.POD(value.defaultFloat);
            writer.POD(value.defaultInt);
            writer.POD(value.defaultBool);
        });
    writeVector(writer, controller.states, [&](const animation::AnimatorState& value) {
        writer.string(value.name);
        writer.string(value.clipName);
        writer.POD(value.clipDurationSeconds);
        writer.POD(value.speed);
    });
    writeVector(writer, controller.transitions, [&](const animation::AnimatorTransition& value) {
        writer.string(value.sourceState);
        writer.string(value.destinationState);
        writer.POD(value.durationSeconds);
        writer.POD(value.hasExitTime);
        writer.POD(value.exitTime);
        writeVector(writer, value.conditions, [&](const animation::AnimatorCondition& condition) {
            writer.string(condition.parameter);
            writer.POD(condition.mode);
            writer.POD(condition.threshold);
        });
    });
}

bool readAnimatorController(
    RuntimeBlobReader& reader, animation::AnimatorControllerAsset& controller)
{
    return reader.string(controller.name) && reader.string(controller.defaultStateName)
        && readVector(reader,
            controller.parameters,
            [&](animation::AnimatorParameterDefinition& value) {
                return reader.string(value.name) && reader.POD(value.type)
                    && reader.POD(value.defaultFloat) && reader.POD(value.defaultInt)
                    && reader.POD(value.defaultBool);
            })
        && readVector(reader,
            controller.states,
            [&](animation::AnimatorState& value) {
                return reader.string(value.name) && reader.string(value.clipName)
                    && reader.POD(value.clipDurationSeconds) && reader.POD(value.speed);
            })
        && readVector(reader, controller.transitions, [&](animation::AnimatorTransition& value) {
               return reader.string(value.sourceState) && reader.string(value.destinationState)
                   && reader.POD(value.durationSeconds) && reader.POD(value.hasExitTime)
                   && reader.POD(value.exitTime)
                   && readVector(
                       reader, value.conditions, [&](animation::AnimatorCondition& condition) {
                           return reader.string(condition.parameter) && reader.POD(condition.mode)
                               && reader.POD(condition.threshold);
                       });
           });
}

void writeRuntimeBlob(RuntimeBlobWriter& writer, const PrefabRuntimePayloadWriteDesc& desc)
{
    writer.POD(kPrefabRuntimeBlobMagic);
    writer.POD(kPrefabRuntimeBlobVersion);
    writer.string(desc.defaultStateName);
    writeVector(writer, desc.stateBindings, [&](const PrefabRuntimeStateBinding& binding) {
        writer.string(binding.stateName);
        writer.string(binding.clipName);
        writer.POD(binding.speed);
    });
    writeAnimatorController(writer, *desc.controllerAsset);

    const auto& asset = *desc.asset;
    writeVector(writer, asset.mesh.vertices, [&](const arp::assets::MeshVertex& vertex) {
        for (const auto value : vertex.position)
            writer.POD(value);
        for (const auto value : vertex.normal)
            writer.POD(value);
        for (const auto value : vertex.uv)
            writer.POD(value);
        for (const auto value : vertex.tangent)
            writer.POD(value);
    });
    writeVector(writer, asset.mesh.indices, [&](const std::uint32_t value) {
        writer.POD(value);
    });
    writeVector(writer, asset.mesh.submeshes, [&](const arp::assets::Submesh& submesh) {
        writer.POD(submesh.firstIndex);
        writer.POD(submesh.indexCount);
        writer.POD(submesh.materialSlot);
    });
    writeVector(writer, asset.skinnedVertices, [&](const arp::assets::SkinnedVertex& vertex) {
        for (const auto value : vertex.joints)
            writer.POD(value);
        for (const auto value : vertex.weights)
            writer.POD(value);
    });
    writeVector(writer, asset.skeleton.joints, [&](const animation::Joint& joint) {
        writer.string(joint.name);
        writer.POD(joint.parentIndex);
        for (const auto value : joint.inverseBindMatrix.m)
            writer.POD(value);
    });
    writeVector(writer, asset.bindPose, [&](const animation::LocalTransform& transform) {
        writeTransform(writer, transform);
    });
    writeVector(writer, asset.clips, [&](const animation::AnimationClip& clip) {
        writer.string(clip.name);
        writer.POD(clip.durationSeconds);
        writeVector(writer, clip.channels, [&](const animation::AnimationChannel& channel) {
            writer.POD(channel.jointIndex);
            writer.string(channel.targetPath);
            writer.string(channel.jointName);
            writeVector(writer, channel.keyframes, [&](const animation::JointKeyframe& keyframe) {
                writer.POD(keyframe.timeSeconds);
                writeTransform(writer, keyframe.transform);
            });
        });
        writeVector(writer, clip.events, [&](const animation::AnimationEvent& event) {
            writer.string(event.name);
            writer.POD(event.timeSeconds);
        });
    });
    writeVector(writer, asset.materials, [&](const arp::assets::ImportedMaterialAsset& material) {
        writer.string(material.name);
        writer.string(material.sourceMaterialDocument);
        for (const auto value : material.baseColorFactor)
            writer.POD(value);
        for (const auto value : material.emissiveFactor)
            writer.POD(value);
        writer.POD(material.metallicFactor);
        writer.POD(material.roughnessFactor);
        writer.POD(material.baseColorTexture);
        writer.POD(material.normalTexture);
        writer.POD(material.metallicRoughnessTexture);
        writer.POD(material.occlusionTexture);
        writer.POD(material.emissiveTexture);
        writer.POD(material.clearcoatTexture);
        writer.POD(material.clearcoatRoughnessTexture);
        writer.POD(material.clearcoatNormalTexture);
        writer.POD(material.specularTexture);
        writer.POD(material.specularColorTexture);
        writer.POD(material.sheenColorTexture);
        writer.POD(material.sheenRoughnessTexture);
        writer.POD(material.transmissionTexture);
        writer.POD(material.thicknessTexture);
        writer.POD(material.iridescenceTexture);
        writer.POD(material.iridescenceThicknessTexture);
        writer.POD(material.occlusionStrength);
        writer.POD(material.clearcoatFactor);
        writer.POD(material.clearcoatRoughnessFactor);
        writer.POD(material.specularFactor);
        for (const auto value : material.specularColorFactor)
            writer.POD(value);
        for (const auto value : material.sheenColorFactor)
            writer.POD(value);
        writer.POD(material.sheenRoughnessFactor);
        writer.POD(material.transmissionFactor);
        writer.POD(material.thicknessFactor);
        for (const auto value : material.attenuationColor)
            writer.POD(value);
        writer.POD(material.attenuationDistance);
        writer.POD(material.ior);
        writer.POD(material.iridescenceFactor);
        writer.POD(material.iridescenceIor);
        writer.POD(material.emissiveStrength);
        writer.POD(material.alphaMode);
        writer.POD(material.alphaCutoff);
        writer.POD(material.doubleSided);
        writer.POD(material.baseColorTextureCoordinateSet);
        writer.POD(material.normalTextureCoordinateSet);
        writer.POD(material.metallicRoughnessTextureCoordinateSet);
        writer.POD(material.occlusionTextureCoordinateSet);
        writer.POD(material.emissiveTextureCoordinateSet);
        writer.POD(material.hasCompleteMaterialImportMetadata);
        writer.POD(material.hasUnsupportedMaterialExtensions);
    });
    writeVector(writer, asset.textures, [&](const arp::assets::ImportedTextureAsset& texture) {
        writer.string(texture.name);
        writer.POD(texture.sourceImage);
        writer.POD(texture.sampler);
    });
    writeVector(writer, asset.images, [&](const arp::assets::ImportedImageAsset& image) {
        writer.string(image.name);
        writer.string(image.uri);
        writer.string(image.mimeType);
        writer.POD(image.bufferView);
        writeVector(writer, image.encodedBytes, [&](const std::uint8_t value) {
            writer.POD(value);
        });
        writer.POD(image.alphaIsTransparency);
        writer.POD(image.alphaTransparencyReference);
    });
}

bool readRuntimeBlob(RuntimeBlobReader& reader, PrefabRuntimePayloadLoadResult& out)
{
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!reader.POD(magic) || !reader.POD(version) || magic != kPrefabRuntimeBlobMagic) {
        out.diagnostics = makeDiagnostic("runtime payload blob header is invalid");
        return false;
    }
    if (version != kPrefabRuntimeBlobVersion
        && version != kPrefabRuntimeBlobVersionWithMaterialMetadata
        && version != kPrefabRuntimeBlobVersionWithSourceDocument
        && version != kPrefabRuntimeBlobLegacyV1) {
        std::ostringstream message;
        message << "runtime payload blob version is unsupported: " << version;
        out.diagnostics = arp::core::toPmrString(message.str());
        return false;
    }
    if (!reader.string(out.defaultStateName)) {
        out.diagnostics = makeDiagnostic("runtime payload blob header is invalid");
        return false;
    }
    if (!readVector(reader, out.stateBindings, [&](PrefabRuntimeStateBinding& binding) {
            return reader.string(binding.stateName) && reader.string(binding.clipName)
                && reader.POD(binding.speed);
        })) {
        out.diagnostics = makeDiagnostic("runtime payload state bindings are invalid");
        return false;
    }
    if (version >= kPrefabRuntimeBlobVersion) {
        if (!readAnimatorController(reader, out.controllerAsset)) {
            out.diagnostics = makeDiagnostic("runtime payload animator controller is invalid");
            return false;
        }
        out.hasCompleteControllerData = true;
    }

    auto& asset = out.asset;
    if (!readVector(reader,
            asset.mesh.vertices,
            [&](arp::assets::MeshVertex& vertex) {
                for (auto& value : vertex.position)
                    if (!reader.POD(value))
                        return false;
                for (auto& value : vertex.normal)
                    if (!reader.POD(value))
                        return false;
                for (auto& value : vertex.uv)
                    if (!reader.POD(value))
                        return false;
                for (auto& value : vertex.tangent)
                    if (!reader.POD(value))
                        return false;
                return true;
            })
        || !readVector(reader,
            asset.mesh.indices,
            [&](std::uint32_t& value) {
                return reader.POD(value);
            })
        || !readVector(reader,
            asset.mesh.submeshes,
            [&](arp::assets::Submesh& submesh) {
                return reader.POD(submesh.firstIndex) && reader.POD(submesh.indexCount)
                    && reader.POD(submesh.materialSlot);
            })
        || !readVector(reader,
            asset.skinnedVertices,
            [&](arp::assets::SkinnedVertex& vertex) {
                for (auto& value : vertex.joints)
                    if (!reader.POD(value))
                        return false;
                for (auto& value : vertex.weights)
                    if (!reader.POD(value))
                        return false;
                return true;
            })
        || !readVector(reader,
            asset.skeleton.joints,
            [&](animation::Joint& joint) {
                if (!reader.string(joint.name) || !reader.POD(joint.parentIndex))
                    return false;
                for (auto& value : joint.inverseBindMatrix.m)
                    if (!reader.POD(value))
                        return false;
                return true;
            })
        || !readVector(reader,
            asset.bindPose,
            [&](animation::LocalTransform& transform) {
                return readTransform(reader, transform);
            })
        || !readVector(reader,
            asset.clips,
            [&](animation::AnimationClip& clip) {
                return reader.string(clip.name) && reader.POD(clip.durationSeconds)
                    && readVector(reader,
                        clip.channels,
                        [&](animation::AnimationChannel& channel) {
                            return reader.POD(channel.jointIndex)
                                && reader.string(channel.targetPath)
                                && reader.string(channel.jointName)
                                && readVector(reader,
                                    channel.keyframes,
                                    [&](animation::JointKeyframe& keyframe) {
                                        return reader.POD(keyframe.timeSeconds)
                                            && readTransform(reader, keyframe.transform);
                                    });
                        })
                    && readVector(reader, clip.events, [&](animation::AnimationEvent& event) {
                           return reader.string(event.name) && reader.POD(event.timeSeconds);
                       });
            })
        || !readVector(reader,
            asset.materials,
            [&](arp::assets::ImportedMaterialAsset& material) {
                return readImportedMaterialRuntimeBlob(reader, version, material);
            })
        || !readVector(reader,
            asset.textures,
            [&](arp::assets::ImportedTextureAsset& texture) {
                return reader.string(texture.name) && reader.POD(texture.sourceImage)
                    && reader.POD(texture.sampler);
            })
        || !readVector(reader, asset.images, [&](arp::assets::ImportedImageAsset& image) {
               return reader.string(image.name) && reader.string(image.uri)
                   && reader.string(image.mimeType) && reader.POD(image.bufferView)
                   && readVector(reader,
                       image.encodedBytes,
                       [&](std::uint8_t& value) {
                           return reader.POD(value);
                       })
                   && reader.POD(image.alphaIsTransparency)
                   && reader.POD(image.alphaTransparencyReference);
           })) {
        out.diagnostics = makeDiagnostic("runtime payload blob body is invalid");
        return false;
    }
    if (!reader.done()) {
        out.diagnostics = makeDiagnostic("runtime payload blob has trailing bytes");
        return false;
    }
    return true;
}

const fkyaml::node* findYamlValue(const fkyaml::node& mapping, const char* key)
{
    if (!mapping.is_mapping()) {
        return nullptr;
    }
    const auto& values = mapping.get_value_ref<const fkyaml::node::mapping_type&>();
    const auto found = values.find(fkyaml::node(key));
    return found == values.end() ? nullptr : &found->second;
}

arp::core::pmr::String yamlStringValue(const fkyaml::node& node)
{
    const auto& value = node.get_value_ref<const fkyaml::node::string_type&>();
    return makeString(arp::reflection::NameView(value.data(), value.size()));
}

bool parseRuntimePayloadResource(const InternalAssetPackageEntry& entry,
    PrefabRuntimePayloadResource& out,
    arp::core::pmr::String& diagnostics)
{
    fkyaml::node root;
    try {
        root = fkyaml::node::deserialize(
            entry.envelopeYaml.data(), entry.envelopeYaml.data() + entry.envelopeYaml.size());
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("runtime payload envelope YAML parse failed: ");
        diagnostics += err.what();
        return false;
    }
    const auto* payload = findYamlValue(root, "payload");
    const auto* fields = payload == nullptr ? nullptr : findYamlValue(*payload, "fields");
    if (fields == nullptr || !fields->is_mapping()) {
        diagnostics = makeDiagnostic("PrefabRuntimePayloadResource payload missing fields");
        return false;
    }
    const auto* blobPath = findYamlValue(*fields, "blobPath");
    const auto* blobHash = findYamlValue(*fields, "blobHash");
    const auto* blobByteSize = findYamlValue(*fields, "blobByteSize");
    const auto* defaultStateName = findYamlValue(*fields, "defaultStateName");
    const auto* defaultClipName = findYamlValue(*fields, "defaultClipName");
    if (blobPath == nullptr || !blobPath->is_string() || blobHash == nullptr
        || !blobHash->is_integer() || blobByteSize == nullptr || !blobByteSize->is_integer()
        || defaultStateName == nullptr || !defaultStateName->is_string()
        || defaultClipName == nullptr || !defaultClipName->is_string()) {
        diagnostics = makeDiagnostic("PrefabRuntimePayloadResource fields are incomplete");
        return false;
    }
    out.blobPath = yamlStringValue(*blobPath);
    const auto parsedHash = blobHash->get_value<fkyaml::node::integer_type>();
    const auto parsedByteSize = blobByteSize->get_value<fkyaml::node::integer_type>();
    if (!arp::core::FilePath::isSafeRelativePath(out.blobPath)) {
        diagnostics =
            makeDiagnostic("runtime payload blob path is not a safe relative package path");
        return false;
    }
    if (parsedHash < std::numeric_limits<int>::min()
        || parsedHash
            > static_cast<fkyaml::node::integer_type>(std::numeric_limits<std::uint32_t>::max())
        || parsedByteSize <= 0 || parsedByteSize > std::numeric_limits<int>::max()) {
        diagnostics = makeDiagnostic("PrefabRuntimePayloadResource blob metadata is invalid");
        return false;
    }
    out.blobHash = static_cast<int>(parsedHash);
    out.blobByteSize = static_cast<int>(parsedByteSize);
    out.defaultStateName = yamlStringValue(*defaultStateName);
    out.defaultClipName = yamlStringValue(*defaultClipName);
    return true;
}

} // namespace

PrefabRuntimePayloadWriteResult writePrefabRuntimePayload(const PrefabRuntimePayloadWriteDesc& desc)
{
    PrefabRuntimePayloadWriteResult result;
    if (desc.packageRoot.empty() || desc.blobPath.empty() || desc.displayName.empty()
        || desc.asset == nullptr || desc.controllerAsset == nullptr) {
        result.diagnostics = makeDiagnostic(
            "Prefab runtime payload write requires package root, blob path, display name, asset, and animator controller");
        return result;
    }
    if (!desc.asset->mesh.validForIndexedDraw()
        || desc.asset->skinnedVertices.size() != desc.asset->mesh.vertices.size()
        || !desc.asset->skeleton.valid()
        || desc.asset->bindPose.size() != desc.asset->skeleton.jointCount()
        || desc.asset->clips.empty() || desc.defaultStateName.empty()
        || desc.stateBindings.empty()) {
        result.diagnostics = makeDiagnostic("Prefab runtime payload asset is incomplete");
        return result;
    }
    if (!validateAnimatorControllerForPayload(*desc.controllerAsset,
            *desc.asset,
            desc.defaultStateName,
            desc.stateBindings,
            result.diagnostics)) {
        return result;
    }
    const auto defaultClipName =
        defaultClipForStateBindings(desc.stateBindings, desc.defaultStateName);
    if (defaultClipName.empty()) {
        result.diagnostics =
            makeDiagnostic("Prefab runtime payload requires a default clip binding");
        return result;
    }

    RuntimeBlobWriter writer;
    writeRuntimeBlob(writer, desc);
    if (!writeBinaryFile(desc.packageRoot / std::filesystem::path(desc.blobPath.c_str()),
            writer.bytes(),
            result.diagnostics)) {
        return result;
    }

    result.resource.name = desc.displayName;
    result.resource.blobPath = desc.blobPath;
    result.resource.blobHash = static_cast<int>(hashBytes(writer.bytes()));
    result.resource.blobByteSize = static_cast<int>(writer.bytes().size());
    result.resource.defaultStateName = desc.defaultStateName;
    result.resource.defaultClipName = defaultClipName;
    result.resource.stateBindingCount = static_cast<int>(desc.stateBindings.size());
    result.resource.materialCount = static_cast<int>(desc.asset->materials.size());
    result.resource.textureCount = static_cast<int>(desc.asset->textures.size());
    result.resource.imageCount = static_cast<int>(desc.asset->images.size());
    result.success = true;
    return result;
}

namespace {
PrefabRuntimePayloadLoadResult loadPrefabRuntimePayloadFromPackageKey(
    const std::filesystem::path& packageRoot,
    const arp::core::pmr::String& prefabKey,
    bool matchResourceId)
{
    PrefabRuntimePayloadLoadResult result;
    const auto read = InternalAssetPackageReader().read(packageRoot);
    if (!read.success) {
        result.diagnostics = read.diagnostics;
        return result;
    }

    const InternalAssetPackageEntry* prefabEntry = nullptr;
    for (const auto& entry : read.package.entries) {
        if (entry.resourceType == PrefabResource::resourceTypeName()
            && (matchResourceId ? entry.resourceId == prefabKey : entry.alias == prefabKey)) {
            prefabEntry = &entry;
            break;
        }
    }
    if (prefabEntry == nullptr) {
        result.diagnostics = makeDiagnostic(matchResourceId
                ? "internal package missing requested PrefabResource ID: "
                : "internal package missing requested PrefabResource alias: ");
        result.diagnostics += prefabKey;
        return result;
    }

    arp::core::pmr::String runtimePayloadId(&arp::core::defaultCpuMemoryResource());
    for (const auto& dependency : prefabEntry->dependencies) {
        if (dependency.usage == "runtime-payload") {
            runtimePayloadId = dependency.id;
            break;
        }
    }
    if (runtimePayloadId.empty()) {
        result.diagnostics = makeDiagnostic("PrefabResource is missing runtime-payload dependency");
        return result;
    }

    const InternalAssetPackageEntry* payloadEntry = nullptr;
    for (const auto& entry : read.package.entries) {
        if (entry.resourceId == runtimePayloadId
            && entry.resourceType == PrefabRuntimePayloadResource::resourceTypeName()) {
            payloadEntry = &entry;
            break;
        }
    }
    if (payloadEntry == nullptr) {
        result.diagnostics =
            makeDiagnostic("internal package missing PrefabRuntimePayloadResource: ");
        result.diagnostics += runtimePayloadId;
        return result;
    }

    PrefabRuntimePayloadResource payload;
    if (!parseRuntimePayloadResource(*payloadEntry, payload, result.diagnostics)) {
        return result;
    }
    std::error_code pathError;
    const auto canonicalPackageRoot = std::filesystem::canonical(packageRoot, pathError);
    if (pathError) {
        result.diagnostics = makeDiagnostic("runtime payload package root cannot be resolved");
        return result;
    }
    const auto canonicalBlobPath = std::filesystem::canonical(
        canonicalPackageRoot / std::filesystem::path(payload.blobPath.c_str()), pathError);
    if (pathError || !arp::core::FilePath::contains(canonicalPackageRoot, canonicalBlobPath)) {
        result.diagnostics = makeDiagnostic("runtime payload blob path escapes package root");
        return result;
    }
    arp::core::pmr::Vector<std::uint8_t> bytes{&arp::core::defaultCpuMemoryResource()};
    if (!readBinaryFile(canonicalBlobPath,
            static_cast<std::size_t>(payload.blobByteSize),
            bytes,
            result.diagnostics)) {
        return result;
    }
    if (static_cast<int>(hashBytes(bytes)) != payload.blobHash) {
        result.diagnostics = makeDiagnostic("runtime payload blob hash mismatch");
        return result;
    }

    RuntimeBlobReader reader(bytes);
    if (!readRuntimeBlob(reader, result)) {
        return result;
    }
    if (result.hasCompleteControllerData
        && !validateAnimatorControllerForPayload(result.controllerAsset,
            result.asset,
            result.defaultStateName,
            result.stateBindings,
            result.diagnostics)) {
        return result;
    }
    if (!result.hasCompleteControllerData) {
        result.diagnostics =
            makeDiagnostic("legacy runtime payload has no complete animator controller data");
    }
    if (result.defaultStateName != payload.defaultStateName) {
        result.diagnostics = makeDiagnostic("runtime payload default state metadata mismatch");
        result.success = false;
        return result;
    }
    if (!result.asset.mesh.validForIndexedDraw()
        || result.asset.skinnedVertices.size() != result.asset.mesh.vertices.size()
        || !result.asset.skeleton.valid()
        || result.asset.bindPose.size() != result.asset.skeleton.jointCount()
        || result.asset.clips.empty() || result.stateBindings.empty()) {
        result.diagnostics = makeDiagnostic("runtime payload asset is incomplete");
        result.success = false;
        return result;
    }
    result.success = true;
    return result;
}
} // namespace

PrefabRuntimePayloadLoadResult loadPrefabRuntimePayloadFromPackage(
    const std::filesystem::path& packageRoot, const arp::core::pmr::String& prefabAlias)
{
    return loadPrefabRuntimePayloadFromPackageKey(packageRoot, prefabAlias, false);
}

PrefabRuntimePayloadLoadResult loadPrefabRuntimePayloadByResourceIdFromPackage(
    const std::filesystem::path& packageRoot, const arp::core::pmr::String& prefabResourceId)
{
    if (prefabResourceId.empty()) {
        PrefabRuntimePayloadLoadResult result;
        result.diagnostics = makeDiagnostic("PrefabResource ID is required");
        return result;
    }
    return loadPrefabRuntimePayloadFromPackageKey(packageRoot, prefabResourceId, true);
}

arp::core::pmr::Vector<ResourceDependency> ExternalAssetSourceResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult ExternalAssetSourceResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult ExternalAssetSourceResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> PrefabResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    pushDependencyIfValid(result, modelId, "model");
    pushDependencyIfValid(result, skinnedMeshId, "skinned-mesh");
    pushDependencyIfValid(result, skeletonId, "skeleton");
    pushDependencyIfValid(result, avatarId, "avatar");
    pushDependencyIfValid(result, animationSetId, "animation-set");
    pushDependencyIfValid(result, animatorControllerId, "animator-controller");
    pushDependencyIfValid(result, materialSetId, "material-set");
    pushDependencyIfValid(result, runtimePayloadId, "runtime-payload");
    pushDependencyIfValid(result, sequenceId, "sequence");
    pushDependencyIfValid(result, sequenceBindingSetId, "sequence-binding-set");
    pushDependencyIfValid(result, previewStudioId, "preview-studio");
    pushDependencyIfValid(result, sourceResourceId, "source");
    for (const auto& shaderIdValue : shaderPackageIds) {
        pushDependencyIfValid(result, shaderIdValue.cast<arp::core::pmr::String>(), "shader");
    }
    return result;
}

AssetResourcePackageBuildResult PrefabResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult PrefabResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> PrefabRuntimePayloadResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult PrefabRuntimePayloadResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    if (blobPath.empty() || blobHash == 0 || blobByteSize <= 0) {
        AssetResourcePackageBuildResult result;
        result.diagnostics =
            makeDiagnostic("PrefabRuntimePayloadResource requires blob path, hash, and byte size");
        return result;
    }
    if (defaultStateName.empty() || defaultClipName.empty()) {
        AssetResourcePackageBuildResult result;
        result.diagnostics =
            makeDiagnostic("PrefabRuntimePayloadResource requires default state and clip");
        return result;
    }
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult PrefabRuntimePayloadResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> ModelResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    pushDependencyIfValid(result, sourceResourceId, "source");
    pushDependencyIfValid(result, skinnedMeshId, "skinned-mesh");
    pushDependencyIfValid(result, skeletonId, "skeleton");
    pushDependencyIfValid(result, avatarId, "avatar");
    pushDependencyIfValid(result, animationSetId, "animation-set");
    pushDependencyIfValid(result, materialSetId, "material-set");
    return result;
}

AssetResourcePackageBuildResult ModelResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult ModelResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> SkinnedMeshResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult SkinnedMeshResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult SkinnedMeshResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> SkeletonResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult SkeletonResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult SkeletonResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> AvatarResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult AvatarResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult AvatarResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> AnimationClipResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult AnimationClipResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult AnimationClipResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> AnimationSetResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    for (const auto& clipIdValue : clipIds) {
        pushDependencyIfValid(result, clipIdValue.cast<arp::core::pmr::String>(), "animation-clip");
    }
    return result;
}

AssetResourcePackageBuildResult AnimationSetResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult AnimationSetResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

void MaterialResource::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    constexpr std::uint64_t payloadFingerprintVersion = 1u;
    hasher.appendU64(payloadFingerprintVersion);
    const auto appendText = [&hasher](const core::pmr::String& value) noexcept {
        hasher.appendU64(value.size());
        hasher.appendString({value.data(), value.size()});
    };
    appendText(name);
    appendText(sourcePath);
    appendText(sourceResourceId);
    appendText(shaderPackageId);
    hasher.appendU64(static_cast<std::uint64_t>(textureBindingCount));
    hasher.appendU64(static_cast<std::uint64_t>(propertyCount));
    hasher.appendU64(materialContractVersion);
    appendText(surfaceModel);
    hasher.appendFloat(baseColorR);
    hasher.appendFloat(baseColorG);
    hasher.appendFloat(baseColorB);
    hasher.appendFloat(baseColorA);
    hasher.appendFloat(emissiveR);
    hasher.appendFloat(emissiveG);
    hasher.appendFloat(emissiveB);
    hasher.appendFloat(metallicFactor);
    hasher.appendFloat(roughnessFactor);
    appendText(alphaMode);
    hasher.appendFloat(alphaCutoff);
    hasher.appendU64(doubleSided ? 1u : 0u);
    appendText(baseColorTextureId);
    appendText(normalTextureId);
    appendText(metallicRoughnessTextureId);
    appendText(occlusionTextureId);
    appendText(emissiveTextureId);
}

arp::core::pmr::Vector<ResourceDependency> MaterialResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    pushDependencyIfValid(result, sourceResourceId, "source");
    pushDependencyIfValid(result, shaderPackageId, "shader");
    pushDependencyIfValid(result, baseColorTextureId, "texture/baseColor");
    pushDependencyIfValid(result, normalTextureId, "texture/normal");
    pushDependencyIfValid(result, metallicRoughnessTextureId, "texture/metallicRoughness");
    pushDependencyIfValid(result, occlusionTextureId, "texture/occlusion");
    pushDependencyIfValid(result, emissiveTextureId, "texture/emissive");
    return result;
}

MaterialContractValidation validateMaterialContract(
    const MaterialResource& material, const ShaderPackageResource& shader)
{
    if (material.materialContractVersion != kPbrMetallicRoughnessContractVersion)
        return {MaterialContractDiagnostic::UnsupportedVersion};
    if (material.surfaceModel != kPbrMetallicRoughnessContract)
        return {MaterialContractDiagnostic::UnsupportedSurfaceModel};
    if (shader.materialInterfaceContract != kPbrMetallicRoughnessContract
        || shader.materialInterfaceVersion != kPbrMetallicRoughnessContractVersion)
        return {MaterialContractDiagnostic::ShaderInterfaceMismatch};
    const float baseColor[] = {
        material.baseColorR,
        material.baseColorG,
        material.baseColorB,
        material.baseColorA,
    };
    const float emissive[] = {material.emissiveR, material.emissiveG, material.emissiveB};
    for (const auto value : baseColor) {
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            return {MaterialContractDiagnostic::InvalidParameter};
    }
    for (const auto value : emissive) {
        if (!std::isfinite(value) || value < 0.0f)
            return {MaterialContractDiagnostic::InvalidParameter};
    }
    if (!std::isfinite(material.metallicFactor) || material.metallicFactor < 0.0f
        || material.metallicFactor > 1.0f || !std::isfinite(material.roughnessFactor)
        || material.roughnessFactor < 0.0f || material.roughnessFactor > 1.0f
        || !std::isfinite(material.alphaCutoff) || material.alphaCutoff < 0.0f
        || material.alphaCutoff > 1.0f)
        return {MaterialContractDiagnostic::InvalidParameter};
    if (material.alphaMode != "OPAQUE" && material.alphaMode != "MASK"
        && material.alphaMode != "BLEND")
        return {MaterialContractDiagnostic::InvalidParameter};
    return {};
}

AssetResourcePackageBuildResult MaterialResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult MaterialResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> MaterialSetResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    for (const auto& materialIdValue : materialIds) {
        pushDependencyIfValid(result, materialIdValue.cast<arp::core::pmr::String>(), "material");
    }
    return result;
}

AssetResourcePackageBuildResult MaterialSetResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult MaterialSetResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> TextureResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    pushDependencyIfValid(result, sourceResourceId, "source");
    return result;
}

AssetResourcePackageBuildResult TextureResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult TextureResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> AuthoringAssetResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    pushDependencyIfValid(result, sourceResourceId, "source");
    return result;
}

AssetResourcePackageBuildResult AuthoringAssetResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult AuthoringAssetResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> ShaderGraphSourceResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    pushDependencyIfValid(result, sourceResourceId, "source");
    return result;
}

AssetResourcePackageBuildResult ShaderGraphSourceResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult ShaderGraphSourceResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> SequenceResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    pushDependencyIfValid(result, sourceResourceId, "source");
    return result;
}

AssetResourcePackageBuildResult SequenceResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult SequenceResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> SequenceBindingSetResource::dependencies() const
{
    arp::core::pmr::Vector<ResourceDependency> result{&arp::core::defaultCpuMemoryResource()};
    pushDependencyIfValid(result, sequenceId, "sequence");
    return result;
}

AssetResourcePackageBuildResult SequenceBindingSetResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult SequenceBindingSetResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> PhysicsColliderResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult PhysicsColliderResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult PhysicsColliderResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> AnimatorControllerResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult AnimatorControllerResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult AnimatorControllerResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> ShaderPackageResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult ShaderPackageResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult ShaderPackageResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

arp::core::pmr::Vector<ResourceDependency> PreviewStudioResource::dependencies() const
{
    return noDependencies();
}

AssetResourcePackageBuildResult PreviewStudioResource::buildPackageEntry(
    const AssetResourcePackageBuildDesc& desc) const
{
    return buildPackageEntryFor(*this, resourceTypeName(), defaultEnvelopePath(), desc);
}

ResourceLoadResult PreviewStudioResource::loadInto(
    ResourceManager& manager, const AssetResourceRuntimeLoadDesc& desc) const
{
    return loadIntoFor(*this, manager, resourceTypeName(), defaultEnvelopePath(), desc);
}

void registerPrefabAssetReflection()
{
    arp::reflection::generated::register_prefab_asset_resources_reflection();
}

bool PrefabResourceFactory::registerType(arp::reflection::NameView resourceType, Creator creator)
{
    const auto key = makeString(resourceType);
    if (creators_.find(key) != creators_.end()) {
        return false;
    }
    creators_[key] = creator;
    return true;
}

core::UniquePtr<ResourceBase> PrefabResourceFactory::create(
    arp::reflection::NameView resourceType, const arp::reflection::object& payload) const
{
    const auto found = creators_.find(makeString(resourceType));
    if (found == creators_.end()) {
        return nullptr;
    }
    return found->second(payload);
}

bool PrefabResourceFactory::supports(arp::reflection::NameView resourceType) const
{
    return creators_.find(makeString(resourceType)) != creators_.end();
}

template <typename T>
core::UniquePtr<ResourceBase> makeTypedResource(const arp::reflection::object& payload)
{
    auto* typed = payload.cast<T*>();
    if (typed == nullptr) {
        return nullptr;
    }
    return core::makeUnique<T>(*typed);
}

PrefabResourceFactory& prefabResourceFactory()
{
    static PrefabResourceFactory factory;
    static bool initialized = false;
    if (!initialized) {
        factory.registerType(ExternalAssetSourceResource::resourceTypeName(),
            makeTypedResource<ExternalAssetSourceResource>);
        factory.registerType(PrefabResource::resourceTypeName(), makeTypedResource<PrefabResource>);
        factory.registerType(PrefabRuntimePayloadResource::resourceTypeName(),
            makeTypedResource<PrefabRuntimePayloadResource>);
        factory.registerType(ModelResource::resourceTypeName(), makeTypedResource<ModelResource>);
        factory.registerType(
            SkinnedMeshResource::resourceTypeName(), makeTypedResource<SkinnedMeshResource>);
        factory.registerType(
            SkeletonResource::resourceTypeName(), makeTypedResource<SkeletonResource>);
        factory.registerType(AvatarResource::resourceTypeName(), makeTypedResource<AvatarResource>);
        factory.registerType(
            AnimationClipResource::resourceTypeName(), makeTypedResource<AnimationClipResource>);
        factory.registerType(
            AnimationSetResource::resourceTypeName(), makeTypedResource<AnimationSetResource>);
        factory.registerType(
            MaterialResource::resourceTypeName(), makeTypedResource<MaterialResource>);
        factory.registerType(
            MaterialSetResource::resourceTypeName(), makeTypedResource<MaterialSetResource>);
        factory.registerType(
            TextureResource::resourceTypeName(), makeTypedResource<TextureResource>);
        factory.registerType(
            AuthoringAssetResource::resourceTypeName(), makeTypedResource<AuthoringAssetResource>);
        factory.registerType(ShaderGraphSourceResource::resourceTypeName(),
            makeTypedResource<ShaderGraphSourceResource>);
        factory.registerType(
            SequenceResource::resourceTypeName(), makeTypedResource<SequenceResource>);
        factory.registerType(SequenceBindingSetResource::resourceTypeName(),
            makeTypedResource<SequenceBindingSetResource>);
        factory.registerType(PhysicsColliderResource::resourceTypeName(),
            makeTypedResource<PhysicsColliderResource>);
        factory.registerType(AnimatorControllerResource::resourceTypeName(),
            makeTypedResource<AnimatorControllerResource>);
        factory.registerType(
            ShaderPackageResource::resourceTypeName(), makeTypedResource<ShaderPackageResource>);
        factory.registerType(
            PreviewStudioResource::resourceTypeName(), makeTypedResource<PreviewStudioResource>);
        factory.registerType(
            ParticleEffectResource::resourceTypeName(), makeTypedResource<ParticleEffectResource>);
        initialized = true;
    }
    return factory;
}

core::UniquePtr<ResourceBase> createPrefabResourceFromEnvelope(
    arp::reflection::NameView envelopeYaml, core::pmr::String& diagnostics)
{
    using YamlNode = fkyaml::node;

    auto findMappingValue = [](const YamlNode& mapping, const char* key) -> const YamlNode* {
        if (!mapping.is_mapping()) {
            return nullptr;
        }
        const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
        const auto found = values.find(YamlNode(key));
        return found == values.end() ? nullptr : &found->second;
    };

    auto nodeString = [](const YamlNode& node) -> core::pmr::String {
        const auto& value = node.get_value_ref<const YamlNode::string_type&>();
        return core::pmr::String(value.data(), value.size(), &core::defaultCpuMemoryResource());
    };

    YamlNode root;
    try {
        root =
            YamlNode::deserialize(envelopeYaml.data(), envelopeYaml.data() + envelopeYaml.size());
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("resource envelope YAML parse failed: ");
        diagnostics += err.what();
        return nullptr;
    }
    if (!root.is_mapping()) {
        diagnostics = makeDiagnostic("resource envelope root must be a mapping");
        return nullptr;
    }

    const auto* resourceTypeNode = findMappingValue(root, "resourceType");
    const auto* payload = findMappingValue(root, "payload");
    if (resourceTypeNode == nullptr || !resourceTypeNode->is_string() || payload == nullptr
        || !payload->is_mapping()) {
        diagnostics = makeDiagnostic("envelope missing resourceType or payload");
        return nullptr;
    }

    const auto resourceType = nodeString(*resourceTypeNode);
    const auto payloadYaml = core::YamlContract::serialize(*payload);

    auto deserialized = arp::reflection::deserialize_from_yaml(payloadYaml);
    if (!deserialized.success) {
        diagnostics = deserialized.diagnostics;
        return nullptr;
    }

    auto typed = prefabResourceFactory().create(resourceType, deserialized.value);
    if (!typed) {
        diagnostics = makeDiagnostic("unsupported prefab resource type for typed load: ");
        diagnostics += resourceType;
        return nullptr;
    }

    const auto* resourceIdNode = findMappingValue(root, "resourceId");
    const auto* aliasNode = findMappingValue(root, "alias");
    const auto* statusNode = findMappingValue(root, "status");
    const auto* displayNameNode = findMappingValue(root, "displayName");

    core::pmr::Vector<ResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
    const auto* dependenciesNode = findMappingValue(root, "dependencies");
    if (dependenciesNode != nullptr && dependenciesNode->is_sequence()) {
        for (const auto& item : dependenciesNode->get_value_ref<const YamlNode::sequence_type&>()) {
            if (!item.is_mapping()) {
                continue;
            }
            const auto* idNode = findMappingValue(item, "id");
            const auto* usageNode = findMappingValue(item, "usage");
            if (idNode != nullptr && idNode->is_string() && usageNode != nullptr
                && usageNode->is_string()) {
                dependencies.push_back(ResourceDependency{
                    nodeString(*idNode),
                    nodeString(*usageNode),
                });
            }
        }
    }

    typed->configureMetadata(
        ResourceReference{
            resourceIdNode != nullptr && resourceIdNode->is_string()
                ? nodeString(*resourceIdNode)
                : core::pmr::String(&core::defaultCpuMemoryResource()),
            resourceType,
            aliasNode != nullptr && aliasNode->is_string()
                ? nodeString(*aliasNode)
                : core::pmr::String(&core::defaultCpuMemoryResource()),
            statusNode != nullptr && statusNode->is_string()
                ? nodeString(*statusNode)
                : core::pmr::String(&core::defaultCpuMemoryResource()),
            displayNameNode != nullptr && displayNameNode->is_string()
                ? nodeString(*displayNameNode)
                : core::pmr::String(&core::defaultCpuMemoryResource()),
        },
        dependencies);
    return typed;
}

namespace compatibility {

ResourceLoadResult loadFromEnvelope(
    ResourceManager& manager, arp::reflection::NameView envelopeYaml, ResourceLoadPolicy policy)
{
    return manager.loadFromEnvelope(envelopeYaml, policy);
}

} // namespace compatibility

namespace {

struct SourceRecord {
    std::filesystem::path path;
    arp::core::pmr::String relativePath{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String sourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String sourceType{&arp::core::defaultCpuMemoryResource()};
};

std::size_t countTextOccurrences(const arp::core::pmr::String& text, const char* needle)
{
    std::size_t count = 0;
    std::size_t cursor = 0;
    const auto needleSize = arp::reflection::NameView(needle).size();
    while (needleSize > 0u) {
        const auto found = text.find(needle, cursor);
        if (found == arp::core::pmr::String::npos) {
            break;
        }
        ++count;
        cursor = found + needleSize;
    }
    return count;
}

float maxDurationInText(const arp::core::pmr::String& text)
{
    float result = 0.0f;
    constexpr arp::reflection::NameView key = "m_Duration:";
    std::size_t cursor = 0;
    while (true) {
        const auto found = text.find(key, cursor);
        if (found == arp::core::pmr::String::npos) {
            break;
        }
        const char* valueStart = text.data() + found + key.size();
        char* valueEnd = nullptr;
        const auto parsed = std::strtof(valueStart, &valueEnd);
        if (valueEnd != valueStart && parsed > result) {
            result = parsed;
        }
        cursor = found + key.size();
    }
    return result;
}

arp::core::pmr::String timelineDiagnostics(int emptyBindings)
{
    arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
    if (emptyBindings > 0) {
        diagnostics = makeString("Prototype Complete: sequence contains empty track bindings");
    }
    return diagnostics;
}

bool extensionEquals(const std::filesystem::path& path, const char* extension)
{
    return lowerString(pathString(path.extension())) == extension;
}

bool sourceTypeEquals(const SourceRecord& record, const char* type)
{
    return record.sourceType == type;
}

const SourceRecord* findFirstSourceOfType(
    const arp::core::pmr::Vector<SourceRecord>& sources, const char* type)
{
    for (const auto& source : sources) {
        if (sourceTypeEquals(source, type)) {
            return &source;
        }
    }
    return nullptr;
}

const SourceRecord* findPreferredModelSource(const arp::core::pmr::Vector<SourceRecord>& sources)
{
    const SourceRecord* firstModel = nullptr;
    for (const auto& source : sources) {
        if (!sourceTypeEquals(source, "Model")) {
            continue;
        }
        if (firstModel == nullptr) {
            firstModel = &source;
        }
        if (source.relativePath.find("Models/") != arp::core::pmr::String::npos
            || source.relativePath.find("Models\\") != arp::core::pmr::String::npos) {
            return &source;
        }
    }
    return firstModel;
}

template <typename T>
bool addGraphEntry(InternalAssetPackageDesc& package,
    const arp::core::pmr::String& resourceId,
    const arp::core::pmr::String& alias,
    const arp::core::pmr::String& envelopePath,
    const T& payload,
    arp::core::pmr::String& diagnostics)
{
    AssetResourcePackageBuildDesc desc;
    desc.resourceId = resourceId;
    desc.alias = alias;
    desc.displayName = alias;
    desc.status = prototypeStatus();
    desc.envelopePath = envelopePath;
    const auto built = payload.buildPackageEntry(desc);
    if (!built.success) {
        diagnostics = built.diagnostics;
        return false;
    }
    package.entries.push_back(built.entry);
    return true;
}

arp::core::pmr::String aliasWithSuffix(const arp::core::pmr::String& alias, const char* suffix)
{
    arp::core::pmr::String result(alias, &arp::core::defaultCpuMemoryResource());
    result += suffix;
    return result;
}

bool buildGraphShader(const char* name,
    const arp::core::pmr::String& path,
    ShaderPackageResource& out,
    arp::core::pmr::String& diagnostics)
{
    return buildShaderPackage(name, path, out, diagnostics);
}

} // namespace

PrefabAssetGraphBuildResult PrefabAssetGraphBuilder::buildPackage(
    const PrefabAssetGraphBuildDesc& desc) const
{
    registerPrefabAssetReflection();

    PrefabAssetGraphBuildResult result;
    if (desc.alias.empty() || desc.displayName.empty() || desc.sourcePath.empty()) {
        result.diagnostics =
            makeDiagnostic("PrefabAssetGraph requires alias, displayName, and sourcePath");
        return result;
    }

    result.ids = allocateGraphIds(desc);
    if (!validateUniqueGraphIds(result.ids, result.diagnostics)) {
        return result;
    }

    ShaderPackageResource basicShader;
    ShaderPackageResource stylizedShader;
    ShaderPackageResource guiShader;
    ShaderPackageResource startupShader;
    if (!buildGraphShader("material_preview_pbr",
            desc.shaderPaths.basicMaterialPreview,
            basicShader,
            result.diagnostics)
        || !buildGraphShader(
            "stylized_scene", desc.shaderPaths.stylizedScene, stylizedShader, result.diagnostics)
        || !buildGraphShader(
            "gui_overlay", desc.shaderPaths.guiOverlay, guiShader, result.diagnostics)
        || !buildGraphShader(
            "startup_splash", desc.startupSplashShaderPath, startupShader, result.diagnostics)) {
        return result;
    }

    result.package.name = desc.displayName;
    result.package.status = prototypeStatus();

    const auto rootPath = std::filesystem::path(desc.sourcePath.c_str());
    const bool hasExternalSource = desc.importedCharacter != nullptr;
    if (hasExternalSource
        && (!std::filesystem::exists(rootPath) || !std::filesystem::is_directory(rootPath))) {
        result.diagnostics =
            makeDiagnostic("external character folder is unreadable: ", desc.sourcePath);
        return result;
    }

    arp::core::pmr::Vector<SourceRecord> sources{&arp::core::defaultCpuMemoryResource()};
    if (hasExternalSource) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(rootPath)) {
            if (!entry.is_regular_file() || !isPackageRelevantSource(entry.path())) {
                continue;
            }
            SourceRecord record;
            record.path = entry.path();
            record.relativePath = pathString(entry.path().lexically_relative(rootPath));
            record.sourceType = assetTypeForExtension(entry.path());
            arp::core::pmr::String role(
                "ExternalAssetSourceResource/", &arp::core::defaultCpuMemoryResource());
            role += record.relativePath;
            result.package.name = desc.displayName;
            record.sourceId = deterministicUuid(graphSeed(desc), role.c_str());
            sources.push_back(std::move(record));
        }
        std::sort(
            sources.begin(), sources.end(), [](const SourceRecord& lhs, const SourceRecord& rhs) {
                return lhs.relativePath < rhs.relativePath;
            });
    } else {
        SourceRecord synthetic;
        synthetic.path = rootPath;
        synthetic.relativePath = desc.sourcePath;
        synthetic.sourceType = makeString("Synthetic");
        synthetic.sourceId =
            deterministicUuid(graphSeed(desc), "ExternalAssetSourceResource/synthetic");
        sources.push_back(std::move(synthetic));
    }

    for (const auto& source : sources) {
        ExternalAssetSourceResource payload;
        payload.name = filenameString(source.path);
        payload.sourcePath = source.relativePath;
        payload.sourceGuid = metaGuidFor(source.path);
        payload.fileId = makeString("0");
        payload.sourceAssetType = source.sourceType;
        payload.importerKind =
            desc.sourceKind.empty() ? makeString(kSourceKindUnityPrefabFolder) : desc.sourceKind;
        payload.contentHash = static_cast<int>(hashFileIfReadable(source.path));
        if (!addGraphEntry(result.package,
                source.sourceId,
                aliasWithSuffix(desc.alias, "/source/") + sanitizePathPart(source.relativePath),
                resourceEnvelopePath("source", source.relativePath, source.sourceId),
                payload,
                result.diagnostics)) {
            return result;
        }
    }

    const SourceRecord* modelSource = findPreferredModelSource(sources);
    const SourceRecord* prefabSource = findFirstSourceOfType(sources, "Prefab");
    const SourceRecord* sequenceSource = findFirstSourceOfType(sources, "Sequence");
    const SourceRecord* sceneSource = findFirstSourceOfType(sources, "Scene");

    // Imported character data is provided by the caller (tools/asset_importer).
    // The engine resource layer consumes the already-imported CharacterImportResult.
    const auto& imported = *desc.importedCharacter;
    const bool importedRuntimeData = hasExternalSource;

    if (!addGraphEntry(result.package,
            result.ids.basicShaderId,
            aliasWithSuffix(desc.alias, "/shader/basic"),
            makeString("resources/shader-basic.arpres.yaml"),
            basicShader,
            result.diagnostics)
        || !addGraphEntry(result.package,
            result.ids.stylizedShaderId,
            aliasWithSuffix(desc.alias, "/shader/stylized"),
            makeString("resources/shader-stylized.arpres.yaml"),
            stylizedShader,
            result.diagnostics)
        || !addGraphEntry(result.package,
            result.ids.guiShaderId,
            aliasWithSuffix(desc.alias, "/shader/gui"),
            makeString("resources/shader-gui.arpres.yaml"),
            guiShader,
            result.diagnostics)
        || !addGraphEntry(result.package,
            result.ids.startupSplashShaderId,
            aliasWithSuffix(desc.alias, "/shader/startup-splash"),
            makeString("resources/shader-startup-splash.arpres.yaml"),
            startupShader,
            result.diagnostics)) {
        return result;
    }

    arp::core::pmr::Vector<arp::core::pmr::String> materialIds{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<arp::core::pmr::String> animationClipIds{
        &arp::core::defaultCpuMemoryResource()};
    int textureCount = 0;
    int authoringCount = 0;
    int shaderGraphCount = 0;
    int materialCount = 0;

    for (const auto& source : sources) {
        if (sourceTypeEquals(source, "Texture")) {
            TextureResource texture;
            texture.name = filenameString(source.path);
            texture.sourcePath = source.relativePath;
            texture.sourceResourceId = source.sourceId;
            texture.usage = lowerString(pathString(source.path.filename())).find("normal")
                    != arp::core::pmr::String::npos
                ? makeString("normal")
                : makeString("color");
            texture.srgb = texture.usage != "normal";
            texture.alpha = extensionEquals(source.path, ".png");
            const auto textureId = deterministicUuid(graphSeed(desc),
                (arp::core::pmr::String("TextureResource/", &arp::core::defaultCpuMemoryResource())
                    + source.relativePath)
                    .c_str());
            if (!addGraphEntry(result.package,
                    textureId,
                    aliasWithSuffix(desc.alias, "/texture/")
                        + sanitizePathPart(source.relativePath),
                    resourceEnvelopePath("texture", source.relativePath, textureId),
                    texture,
                    result.diagnostics)) {
                return result;
            }
            ++textureCount;
        } else if (sourceTypeEquals(source, "Authoring")) {
            AuthoringAssetResource authoring;
            authoring.name = filenameString(source.path);
            authoring.sourcePath = source.relativePath;
            authoring.sourceResourceId = source.sourceId;
            authoring.authoringType = lowerString(pathString(source.path.extension()));
            authoring.contentHash = static_cast<int>(hashFileIfReadable(source.path));
            const auto authoringId = deterministicUuid(graphSeed(desc),
                (arp::core::pmr::String(
                     "AuthoringAssetResource/", &arp::core::defaultCpuMemoryResource())
                    + source.relativePath)
                    .c_str());
            if (!addGraphEntry(result.package,
                    authoringId,
                    aliasWithSuffix(desc.alias, "/authoring/")
                        + sanitizePathPart(source.relativePath),
                    resourceEnvelopePath("authoring", source.relativePath, authoringId),
                    authoring,
                    result.diagnostics)) {
                return result;
            }
            ++authoringCount;
        } else if (sourceTypeEquals(source, "ShaderGraphSource")) {
            ShaderGraphSourceResource shaderGraph;
            shaderGraph.name = filenameString(source.path);
            shaderGraph.sourcePath = source.relativePath;
            shaderGraph.sourceResourceId = source.sourceId;
            arp::core::pmr::String text(&arp::core::defaultCpuMemoryResource());
            if (readTextFile(source.path, text)) {
                shaderGraph.dependencyCount =
                    static_cast<int>(countTextOccurrences(text, "\"guid\""));
            }
            const auto shaderGraphId = deterministicUuid(graphSeed(desc),
                (arp::core::pmr::String(
                     "ShaderGraphSourceResource/", &arp::core::defaultCpuMemoryResource())
                    + source.relativePath)
                    .c_str());
            if (!addGraphEntry(result.package,
                    shaderGraphId,
                    aliasWithSuffix(desc.alias, "/shader-graph/")
                        + sanitizePathPart(source.relativePath),
                    resourceEnvelopePath("shader-graph", source.relativePath, shaderGraphId),
                    shaderGraph,
                    result.diagnostics)) {
                return result;
            }
            ++shaderGraphCount;
        } else if (sourceTypeEquals(source, "Material")) {
            MaterialResource material;
            material.name = filenameString(source.path);
            material.sourcePath = source.relativePath;
            material.sourceResourceId = source.sourceId;
            material.shaderPackageId = result.ids.stylizedShaderId;
            arp::core::pmr::String text(&arp::core::defaultCpuMemoryResource());
            if (readTextFile(source.path, text)) {
                material.textureBindingCount =
                    static_cast<int>(countTextOccurrences(text, "m_Texture:"));
                material.propertyCount = static_cast<int>(countTextOccurrences(text, "m_Name:"));
            }
            const auto materialId = deterministicUuid(graphSeed(desc),
                (arp::core::pmr::String("MaterialResource/", &arp::core::defaultCpuMemoryResource())
                    + source.relativePath)
                    .c_str());
            materialIds.push_back(materialId);
            if (!addGraphEntry(result.package,
                    materialId,
                    aliasWithSuffix(desc.alias, "/material/")
                        + sanitizePathPart(source.relativePath),
                    resourceEnvelopePath("material", source.relativePath, materialId),
                    material,
                    result.diagnostics)) {
                return result;
            }
            ++materialCount;
        }
    }

    if (materialIds.empty()) {
        materialIds.push_back(deterministicUuid(graphSeed(desc), "MaterialResource/synthetic"));
        MaterialResource material;
        material.name = makeString("SyntheticMaterial");
        material.sourcePath = desc.sourcePath;
        material.shaderPackageId = result.ids.stylizedShaderId;
        if (!addGraphEntry(result.package,
                materialIds.back(),
                aliasWithSuffix(desc.alias, "/material/synthetic"),
                makeString("resources/material/synthetic.arpres.yaml"),
                material,
                result.diagnostics)) {
            return result;
        }
        materialCount = 1;
    }

    MaterialSetResource materialSet;
    materialSet.name = desc.displayName;
    materialSet.sourcePath = desc.sourcePath;
    materialSet.materialCount = static_cast<int>(materialIds.size());
    for (const auto& materialId : materialIds) {
        materialSet.materialIds.emplace_back(materialId);
    }
    if (!addGraphEntry(result.package,
            result.ids.materialSetId,
            aliasWithSuffix(desc.alias, "/materials"),
            makeString("resources/material-set.arpres.yaml"),
            materialSet,
            result.diagnostics)) {
        return result;
    }

    const int vertexCount =
        importedRuntimeData ? static_cast<int>(imported.character.mesh.vertices.size()) : 3;
    const int indexCount =
        importedRuntimeData ? static_cast<int>(imported.character.mesh.indices.size()) : 3;
    const int submeshCount =
        importedRuntimeData ? static_cast<int>(imported.character.mesh.submeshes.size()) : 1;
    const int clipCount =
        importedRuntimeData ? static_cast<int>(imported.character.clips.size()) : 1;

    SkinnedMeshResource mesh;
    mesh.name = desc.displayName;
    mesh.sourcePath = modelSource == nullptr ? desc.sourcePath : modelSource->relativePath;
    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;
    mesh.submeshCount = submeshCount;
    mesh.materialSlotCount = materialCount;
    if (!addGraphEntry(result.package,
            result.ids.skinnedMeshId,
            aliasWithSuffix(desc.alias, "/mesh/skinned"),
            makeString("resources/skinned-mesh.arpres.yaml"),
            mesh,
            result.diagnostics)) {
        return result;
    }

    SkeletonResource skeleton;
    skeleton.name = desc.displayName;
    skeleton.sourcePath = mesh.sourcePath;
    skeleton.jointCount =
        importedRuntimeData ? static_cast<int>(imported.character.skeleton.joints.size()) : 1;
    skeleton.bindPoseCount = skeleton.jointCount;
    if (!addGraphEntry(result.package,
            result.ids.skeletonId,
            aliasWithSuffix(desc.alias, "/skeleton"),
            makeString("resources/skeleton.arpres.yaml"),
            skeleton,
            result.diagnostics)) {
        return result;
    }

    AvatarResource avatar;
    avatar.name = desc.displayName;
    avatar.sourcePath = mesh.sourcePath;
    avatar.humanoid =
        !importedRuntimeData || imported.avatar.kind == arp::assets::AvatarKind::Humanoid;
    avatar.humanBoneCount = skeleton.jointCount;
    if (!addGraphEntry(result.package,
            result.ids.avatarId,
            aliasWithSuffix(desc.alias, "/avatar"),
            makeString("resources/avatar.arpres.yaml"),
            avatar,
            result.diagnostics)) {
        return result;
    }

    for (int index = 0; index < clipCount; ++index) {
        AnimationClipResource clip;
        if (importedRuntimeData && index < static_cast<int>(imported.character.clips.size())) {
            clip.name = imported.character.clips[static_cast<std::size_t>(index)].name;
            clip.durationSeconds =
                imported.character.clips[static_cast<std::size_t>(index)].durationSeconds;
            clip.channelCount = static_cast<int>(
                imported.character.clips[static_cast<std::size_t>(index)].channels.size());
        } else {
            clip.name = makeString("Prototype");
            clip.durationSeconds = 1.0f;
            clip.channelCount = 1;
        }
        clip.sourcePath = mesh.sourcePath;
        arp::core::pmr::String role(
            "AnimationClipResource/", &arp::core::defaultCpuMemoryResource());
        role += clip.name;
        const auto clipId = deterministicUuid(graphSeed(desc), role.c_str());
        animationClipIds.push_back(clipId);
        if (!addGraphEntry(result.package,
                clipId,
                aliasWithSuffix(desc.alias, "/animation/") + sanitizePathPart(clip.name),
                resourceEnvelopePath("animation", clip.name, clipId),
                clip,
                result.diagnostics)) {
            return result;
        }
    }

    AnimationSetResource animationSet;
    animationSet.name = desc.displayName;
    animationSet.sourcePath = desc.sourcePath;
    animationSet.clipCount = static_cast<int>(animationClipIds.size());
    for (const auto& clipId : animationClipIds) {
        animationSet.clipIds.emplace_back(clipId);
    }
    if (!addGraphEntry(result.package,
            result.ids.animationSetId,
            aliasWithSuffix(desc.alias, "/animation-set"),
            makeString("resources/animation-set.arpres.yaml"),
            animationSet,
            result.diagnostics)) {
        return result;
    }

    AnimatorControllerResource animator;
    animator.name = desc.displayName;
    animator.sourcePath = desc.sourcePath;
    animator.defaultStateName = importedRuntimeData
        ? imported.stateMachine.animator.defaultStateName
        : makeString("Prototype");
    animator.stateCount =
        importedRuntimeData ? static_cast<int>(imported.stateMachine.animator.states.size()) : 1;
    animator.parameterCount = importedRuntimeData
        ? static_cast<int>(imported.stateMachine.animator.parameters.size())
        : 0;
    if (animator.defaultStateName.empty()) {
        result.diagnostics = makeDiagnostic("external animator missing default state");
        return result;
    }
    if (!addGraphEntry(result.package,
            result.ids.animatorControllerId,
            aliasWithSuffix(desc.alias, "/animator"),
            makeString("resources/animator.arpres.yaml"),
            animator,
            result.diagnostics)) {
        return result;
    }

    bool hasRuntimePayload = false;
    if (importedRuntimeData) {
        if (desc.runtimePayloadPackageRoot.empty()) {
            result.diagnostics = makeDiagnostic(
                "runtime payload package root is required when importing external source");
            return result;
        }
        PrefabRuntimePayloadWriteDesc payloadDesc;
        auto animatorController = imported.stateMachine.animator;
        if (animatorController.name.empty()) {
            animatorController.name.assign(desc.displayName.data(), desc.displayName.size());
        }
        payloadDesc.packageRoot = desc.runtimePayloadPackageRoot;
        payloadDesc.blobPath = makeString("blobs/prefab-runtime.arpa.bin");
        payloadDesc.displayName = desc.displayName;
        payloadDesc.asset = &imported.character;
        payloadDesc.controllerAsset = &animatorController;
        payloadDesc.defaultStateName = imported.stateMachine.animator.defaultStateName;
        for (const auto& state : imported.stateMachine.animator.states) {
            PrefabRuntimeStateBinding binding;
            binding.stateName = state.name;
            binding.clipName = state.clipName;
            binding.speed = state.speed;
            payloadDesc.stateBindings.push_back(std::move(binding));
        }
        const auto payloadWritten = writePrefabRuntimePayload(payloadDesc);
        if (!payloadWritten.success) {
            result.diagnostics = payloadWritten.diagnostics;
            return result;
        }
        if (!addGraphEntry(result.package,
                result.ids.runtimePayloadId,
                aliasWithSuffix(desc.alias, "/runtime-payload"),
                makeString(PrefabRuntimePayloadResource::defaultEnvelopePath()),
                payloadWritten.resource,
                result.diagnostics)) {
            return result;
        }
        hasRuntimePayload = true;
    }

    ModelResource model;
    model.name = desc.displayName;
    model.sourcePath = mesh.sourcePath;
    model.sourceResourceId = modelSource == nullptr
        ? arp::core::pmr::String(&arp::core::defaultCpuMemoryResource())
        : modelSource->sourceId;
    model.skinnedMeshId = result.ids.skinnedMeshId;
    model.skeletonId = result.ids.skeletonId;
    model.avatarId = result.ids.avatarId;
    model.animationSetId = result.ids.animationSetId;
    model.materialSetId = result.ids.materialSetId;
    model.materialRemapCount = materialCount;
    model.importedAnimationCount = clipCount;
    model.humanoid = true;
    if (!addGraphEntry(result.package,
            result.ids.modelId,
            aliasWithSuffix(desc.alias, "/model"),
            makeString("resources/model.arpres.yaml"),
            model,
            result.diagnostics)) {
        return result;
    }

    SequenceResource sequence;
    sequence.name = desc.displayName;
    sequence.sourcePath =
        sequenceSource == nullptr ? desc.sourcePath : sequenceSource->relativePath;
    sequence.sourceResourceId = sequenceSource == nullptr
        ? arp::core::pmr::String(&arp::core::defaultCpuMemoryResource())
        : sequenceSource->sourceId;
    if (sequenceSource != nullptr) {
        arp::core::pmr::String text(&arp::core::defaultCpuMemoryResource());
        if (!readTextFile(sequenceSource->path, text)) {
            result.diagnostics =
                makeDiagnostic("missing playable source: ", sequenceSource->relativePath);
            return result;
        }
        sequence.durationSeconds = maxDurationInText(text);
        sequence.trackCount = static_cast<int>(countTextOccurrences(text, "m_Clips:"));
        sequence.clipCount = static_cast<int>(countTextOccurrences(text, "m_DisplayName:"));
        sequence.recordedCurveCount =
            static_cast<int>(countTextOccurrences(text, "attribute: blendShape."));
    }

    SequenceBindingSetResource bindings;
    bindings.name = desc.displayName;
    bindings.sourcePath = sceneSource == nullptr ? desc.sourcePath : sceneSource->relativePath;
    bindings.sequenceId = result.ids.sequenceId;
    if (sceneSource != nullptr) {
        arp::core::pmr::String text(&arp::core::defaultCpuMemoryResource());
        if (readTextFile(sceneSource->path, text)) {
            bindings.bindingCount = static_cast<int>(
                countTextOccurrences(text, "guid: e623df1495e8c044aa234c1c48cf32da"));
            bindings.emptyBindingCount =
                static_cast<int>(countTextOccurrences(text, "value: {fileID: 0}"));
        }
    }
    sequence.emptyBindingCount = bindings.emptyBindingCount;
    sequence.diagnostics = timelineDiagnostics(sequence.emptyBindingCount);
    bindings.diagnostics = timelineDiagnostics(bindings.emptyBindingCount);
    if (!addGraphEntry(result.package,
            result.ids.sequenceId,
            aliasWithSuffix(desc.alias, "/sequence"),
            makeString("resources/sequence.arpres.yaml"),
            sequence,
            result.diagnostics)
        || !addGraphEntry(result.package,
            result.ids.sequenceBindingSetId,
            aliasWithSuffix(desc.alias, "/sequence-bindings"),
            makeString("resources/sequence-bindings.arpres.yaml"),
            bindings,
            result.diagnostics)) {
        return result;
    }

    PreviewStudioResource preview;
    preview.lightingPreset = makeString("Neutral");
    if (!addGraphEntry(result.package,
            result.ids.previewStudioId,
            aliasWithSuffix(desc.alias, "/preview-studio"),
            makeString("resources/preview-studio.arpres.yaml"),
            preview,
            result.diagnostics)) {
        return result;
    }

    const SourceRecord* prefabRootSource =
        prefabSource != nullptr ? prefabSource : (sources.empty() ? nullptr : &sources.front());

    PrefabResource prefab;
    prefab.name = desc.displayName;
    prefab.sourcePath =
        prefabRootSource == nullptr ? desc.sourcePath : prefabRootSource->relativePath;
    prefab.sourceResourceId = prefabRootSource == nullptr
        ? arp::core::pmr::String(&arp::core::defaultCpuMemoryResource())
        : prefabRootSource->sourceId;
    prefab.modelId = result.ids.modelId;
    prefab.skinnedMeshId = result.ids.skinnedMeshId;
    prefab.skeletonId = result.ids.skeletonId;
    prefab.avatarId = result.ids.avatarId;
    prefab.animationSetId = result.ids.animationSetId;
    prefab.animatorControllerId = result.ids.animatorControllerId;
    prefab.materialSetId = result.ids.materialSetId;
    if (hasRuntimePayload) {
        prefab.runtimePayloadId = result.ids.runtimePayloadId;
    }
    prefab.sequenceId = result.ids.sequenceId;
    prefab.sequenceBindingSetId = result.ids.sequenceBindingSetId;
    prefab.previewStudioId = result.ids.previewStudioId;
    prefab.shaderPackageIds.emplace_back(result.ids.basicShaderId);
    prefab.shaderPackageIds.emplace_back(result.ids.stylizedShaderId);
    prefab.shaderPackageIds.emplace_back(result.ids.guiShaderId);
    prefab.shaderPackageIds.emplace_back(result.ids.startupSplashShaderId);
    if (prefabSource != nullptr) {
        arp::core::pmr::String text(&arp::core::defaultCpuMemoryResource());
        if (readTextFile(prefabSource->path, text)) {
            prefab.objectCount = static_cast<int>(countTextOccurrences(text, "GameObject:"));
            prefab.componentCount = static_cast<int>(countTextOccurrences(text, "m_Component:"));
            prefab.physicsColliderCount = static_cast<int>(countTextOccurrences(text, "Collider"));
            prefab.lightHelperCount = static_cast<int>(countTextOccurrences(text, "Light"));
        }
    } else {
        prefab.objectCount = 1;
        prefab.componentCount = 1;
    }
    if (!addGraphEntry(result.package,
            result.ids.prefabId,
            desc.alias,
            makeString(PrefabResource::defaultEnvelopePath()),
            prefab,
            result.diagnostics)) {
        return result;
    }

    (void)textureCount;
    (void)authoringCount;
    (void)shaderGraphCount;
    result.success = true;
    return result;
}

} // namespace arp::resource
