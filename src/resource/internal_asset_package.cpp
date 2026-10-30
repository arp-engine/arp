#include "resource/internal_asset_package.h"

#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "reflection/serialization.h"
#include "resource/asset_package_closure.h"
#include "resource/asset_package_payload.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_graph_plan.h"
#include "resource/resource_source_io.h"
#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_cooked.h"

#include <charconv>
#include <exception>
#include <fkYAML/Node.hpp>
#include <fstream>
#include <limits>

namespace arp::resource {

namespace {

using YamlNode = fkyaml::node;

core::pmr::String makeString(NameView text)
{
    return core::pmr::String(text.data(), text.size(), &core::defaultCpuMemoryResource());
}

core::pmr::String makeDiagnostic(const char* text)
{
    return core::pmr::String(text, &core::defaultCpuMemoryResource());
}

core::pmr::String makeDiagnostic(const char* prefix, NameView value)
{
    core::pmr::String result(prefix, &core::defaultCpuMemoryResource());
    result.append(value.data(), value.size());
    return result;
}

void appendUint(core::pmr::String& out, std::uint32_t value)
{
    char buffer[16]{};
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (converted.ec == std::errc{}) {
        out.append(buffer, static_cast<std::size_t>(converted.ptr - buffer));
    }
}

core::pmr::String indentYaml(NameView yaml)
{
    core::pmr::String indented(&core::defaultCpuMemoryResource());
    indented += "  ";
    for (const char ch : yaml) {
        indented.push_back(ch);
        if (ch == '\n') {
            indented += "  ";
        }
    }
    return indented;
}

core::pmr::String dependencyListYaml(const core::pmr::Vector<ResourceDependency>& dependencies)
{
    core::pmr::String yaml(&core::defaultCpuMemoryResource());
    if (dependencies.empty()) {
        yaml += "dependencies: []\n";
        return yaml;
    }

    yaml += "dependencies:\n";
    for (const auto& dependency : dependencies) {
        yaml += "  - id: ";
        yaml += dependency.id;
        yaml += "\n";
        yaml += "    usage: ";
        yaml += dependency.usage;
        yaml += "\n";
    }
    return yaml;
}

const YamlNode* findMappingValue(const YamlNode& mapping, const char* key)
{
    if (!mapping.is_mapping()) {
        return nullptr;
    }
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = values.find(YamlNode(key));
    return found == values.end() ? nullptr : &found->second;
}

core::pmr::String nodeString(const YamlNode& node)
{
    const auto& value = node.get_value_ref<const YamlNode::string_type&>();
    return makeString(NameView(value.data(), value.size()));
}

bool requireString(
    const YamlNode& root, const char* key, core::pmr::String& out, core::pmr::String& diagnostics)
{
    const YamlNode* value = findMappingValue(root, key);
    if (value == nullptr || !value->is_string()) {
        diagnostics = makeDiagnostic("missing required ", key);
        return false;
    }
    out = nodeString(*value);
    if (out.empty()) {
        diagnostics = makeDiagnostic("empty required ", key);
        return false;
    }
    return true;
}

bool requireUInt(
    const YamlNode& root, const char* key, std::uint32_t& out, core::pmr::String& diagnostics)
{
    const YamlNode* value = findMappingValue(root, key);
    if (value == nullptr || !value->is_integer()) {
        diagnostics = makeDiagnostic("missing required ", key);
        return false;
    }
    const auto parsed = value->get_value<YamlNode::integer_type>();
    if (parsed < 0
        || parsed
            > static_cast<YamlNode::integer_type>(std::numeric_limits<std::uint32_t>::max())) {
        diagnostics = makeDiagnostic("integer out of range for ", key);
        return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parseDependencies(const YamlNode& root,
    core::pmr::Vector<ResourceDependency>& out,
    core::pmr::String& diagnostics)
{
    const YamlNode* dependencies = findMappingValue(root, "dependencies");
    if (dependencies == nullptr || dependencies->is_null()) {
        return true;
    }
    if (!dependencies->is_sequence()) {
        diagnostics = makeDiagnostic("dependencies must be a sequence");
        return false;
    }
    const auto& sequence = dependencies->get_value_ref<const YamlNode::sequence_type&>();
    for (const auto& item : sequence) {
        if (!item.is_mapping()) {
            diagnostics = makeDiagnostic("dependency entry must be a mapping");
            return false;
        }
        ResourceDependency dependency;
        if (!requireString(item, "id", dependency.id, diagnostics)
            || !requireString(item, "usage", dependency.usage, diagnostics)) {
            return false;
        }
        if (!isResourceUuid(dependency.id)) {
            diagnostics = makeDiagnostic("dependency id must be UUID: ", dependency.id);
            return false;
        }
        out.push_back(std::move(dependency));
    }
    return true;
}

bool requireSequenceSize(
    const YamlNode& root, const char* key, std::size_t expectedSize, core::pmr::String& diagnostics)
{
    const YamlNode* value = findMappingValue(root, key);
    if (value == nullptr || !value->is_sequence()) {
        diagnostics = makeDiagnostic("missing required ", key);
        return false;
    }
    const auto& sequence = value->get_value_ref<const YamlNode::sequence_type&>();
    if (sequence.size() != expectedSize) {
        diagnostics = makeDiagnostic("unexpected element count for ", key);
        return false;
    }
    return true;
}

bool safeRelativePackagePath(const core::pmr::String& value)
{
    return value.find('\0') == core::pmr::String::npos && core::FilePath::isSafeRelativePath(value);
}

bool parseTypedAssetManifestPath(
    NameView envelopeYaml, core::pmr::String& outManifestPath, core::pmr::String& diagnostics)
{
    YamlNode root;
    try {
        root =
            YamlNode::deserialize(envelopeYaml.data(), envelopeYaml.data() + envelopeYaml.size());
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("Typed asset envelope YAML parse failed: ");
        diagnostics += err.what();
        return false;
    }
    const auto* payload = findMappingValue(root, "payload");
    const auto* fields = payload == nullptr ? nullptr : findMappingValue(*payload, "fields");
    if (fields == nullptr || !fields->is_mapping()
        || !requireString(*fields, "manifestPath", outManifestPath, diagnostics)
        || !safeRelativePackagePath(outManifestPath)) {
        if (diagnostics.empty()) {
            diagnostics =
                makeDiagnostic("Typed asset payload requires a safe relative manifestPath");
        }
        return false;
    }
    try {
        core::YamlContract::onlyKeys(*payload, {"type", "fields"});
        if (core::YamlContract::text(*payload, "type")
            != core::YamlContract::text(root, "resourceType"))
            core::YamlContract::fail();
        core::YamlContract::onlyKeys(*fields, {"manifestPath"});
    } catch (...) {
        diagnostics = "TYPED-ASSET-PACKAGE-UNKNOWN-FIELD";
        return false;
    }
    return true;
}

bool isManifestAsset(NameView type)
{
    return type == TerrainAssetResource::resourceTypeName()
        || type == kVegetationCatalogCookedContract.type;
}

bool validateSkinnedMeshFields(const YamlNode& fields, core::pmr::String& diagnostics)
{
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t submeshCount = 0;
    if (!requireUInt(fields, "vertexCount", vertexCount, diagnostics)
        || !requireUInt(fields, "indexCount", indexCount, diagnostics)
        || !requireUInt(fields, "submeshCount", submeshCount, diagnostics)) {
        return false;
    }
    if (vertexCount == 0u || indexCount == 0u || submeshCount == 0u) {
        diagnostics = makeDiagnostic("SkinnedMeshResource counts must be non-zero");
        return false;
    }
    std::uint32_t materialSlotCount = 0;
    return requireUInt(fields, "materialSlotCount", materialSlotCount, diagnostics);
}

bool parseEnvelopeEntry(
    NameView envelopeYaml, InternalAssetPackageEntry& out, core::pmr::String& diagnostics)
{
    YamlNode root;
    try {
        root =
            YamlNode::deserialize(envelopeYaml.data(), envelopeYaml.data() + envelopeYaml.size());
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("resource envelope YAML parse failed: ");
        diagnostics += err.what();
        return false;
    }
    if (!root.is_mapping()) {
        diagnostics = makeDiagnostic("resource envelope root must be a mapping");
        return false;
    }

    core::pmr::String format(&core::defaultCpuMemoryResource());
    std::uint32_t version = 0;
    if (!requireString(root, "format", format, diagnostics)
        || !requireUInt(root, "version", version, diagnostics)
        || !requireString(root, "resourceId", out.resourceId, diagnostics)
        || !requireString(root, "resourceType", out.resourceType, diagnostics)
        || !requireUInt(root, "contentHash", out.contentHash, diagnostics)) {
        return false;
    }
    if (format != "ArpRuntimeResource") {
        diagnostics = makeDiagnostic("unsupported resource format: ", format);
        return false;
    }
    if (version != 1u) {
        diagnostics = makeDiagnostic("unsupported resource version");
        return false;
    }
    if (!isResourceUuid(out.resourceId)) {
        diagnostics = makeDiagnostic("resource id must be UUID: ", out.resourceId);
        return false;
    }

    const YamlNode* alias = findMappingValue(root, "alias");
    if (alias != nullptr && alias->is_string()) {
        out.alias = nodeString(*alias);
    }
    const YamlNode* status = findMappingValue(root, "status");
    if (status != nullptr && status->is_string()) {
        out.status = nodeString(*status);
    }
    if (!parseDependencies(root, out.dependencies, diagnostics)) {
        return false;
    }

    const YamlNode* payload = findMappingValue(root, "payload");
    if (payload == nullptr || !payload->is_mapping()) {
        diagnostics = makeDiagnostic("missing required payload");
        return false;
    }
    if (out.resourceType == "ShaderPackageResource") {
        const YamlNode* fields = findMappingValue(*payload, "fields");
        if (fields == nullptr || !fields->is_mapping()) {
            diagnostics = makeDiagnostic("ShaderPackageResource payload missing fields");
            return false;
        }
        core::pmr::String slangPath(&core::defaultCpuMemoryResource());
        core::pmr::String spirvPath(&core::defaultCpuMemoryResource());
        core::pmr::String mslPath(&core::defaultCpuMemoryResource());
        core::pmr::String reflectionPath(&core::defaultCpuMemoryResource());
        if (!requireString(*fields, "slangPath", slangPath, diagnostics)
            || !requireString(*fields, "spirvPath", spirvPath, diagnostics)
            || !requireString(*fields, "mslPath", mslPath, diagnostics)
            || !requireString(*fields, "reflectionPath", reflectionPath, diagnostics)) {
            return false;
        }
    }
    if (out.resourceType == "SkinnedMeshResource") {
        const YamlNode* fields = findMappingValue(*payload, "fields");
        if (fields == nullptr || !fields->is_mapping()) {
            diagnostics = makeDiagnostic("SkinnedMeshResource payload missing fields");
            return false;
        }
        if (!validateSkinnedMeshFields(*fields, diagnostics)) {
            return false;
        }
    }
    if (isManifestAsset(out.resourceType)) {
        core::pmr::String manifestPath(&core::defaultCpuMemoryResource());
        if (!parseTypedAssetManifestPath(envelopeYaml, manifestPath, diagnostics)) {
            return false;
        }
    }
    const auto payloadText = core::YamlContract::serialize(*payload);
    const auto hash = computeResourcePayloadHash(payloadText);
    if (!hash.success) {
        diagnostics = hash.diagnostics;
        return false;
    }
    if (hash.hash != out.contentHash) {
        diagnostics = makeDiagnostic("contentHash mismatch");
        return false;
    }
    out.envelopeYaml = makeString(envelopeYaml);
    return true;
}

bool matchesEnvelopeMetadata(
    const InternalAssetPackageEntry& entry, const InternalAssetPackageEntry& envelope)
{
    if (entry.resourceId != envelope.resourceId || entry.resourceType != envelope.resourceType
        || entry.contentHash != envelope.contentHash || entry.status != envelope.status
        || entry.alias != envelope.alias
        || entry.dependencies.size() != envelope.dependencies.size())
        return false;
    // Dependencies are a set of explicit ID/usage bindings. Array order is not semantic.
    for (const auto& dependency : entry.dependencies) {
        const auto matches = [&](const auto& other) {
            return other.id == dependency.id && other.usage == dependency.usage;
        };
        if (std::count_if(entry.dependencies.begin(), entry.dependencies.end(), matches) != 1
            || std::count_if(envelope.dependencies.begin(), envelope.dependencies.end(), matches)
                != 1)
            return false;
    }
    return true;
}

bool validatePackageDesc(const InternalAssetPackageDesc& desc, core::pmr::String& diagnostics)
{
    if (desc.name.empty()) {
        diagnostics = makeDiagnostic("package requires name");
        return false;
    }
    if (desc.status.empty()) {
        diagnostics = makeDiagnostic("package requires status");
        return false;
    }
    if (desc.status != "Contract Complete" && desc.status != "Prototype Complete") {
        diagnostics = makeDiagnostic("unsupported package status: ", desc.status);
        return false;
    }
    if (desc.entries.empty()) {
        diagnostics = makeDiagnostic("package requires at least one resource entry");
        return false;
    }

    if (!desc.closurePath.empty()
        && (!safeRelativePackagePath(desc.closurePath)
            || desc.closurePath == "manifest.arpkg.yaml")) {
        diagnostics = "PACKAGE-CLOSURE-PATH";
        return false;
    }
    core::pmr::UnorderedSet<core::pmr::String> ids{&core::defaultCpuMemoryResource()};
    core::pmr::UnorderedSet<core::pmr::String> paths{&core::defaultCpuMemoryResource()};
    for (const auto& entry : desc.entries) {
        if (entry.resourceId.empty() || !isResourceUuid(entry.resourceId)) {
            diagnostics = makeDiagnostic("package entry requires UUID resourceId");
            return false;
        }
        if (entry.resourceType.empty()) {
            diagnostics = makeDiagnostic("package entry requires resourceType");
            return false;
        }
        if (!core::FilePath::isSafeRelativePath(
                std::string_view(entry.envelopePath.data(), entry.envelopePath.size()))) {
            diagnostics = makeDiagnostic("package entry requires a safe relative envelopePath");
            return false;
        }
        if (!desc.closurePath.empty() && entry.envelopePath == desc.closurePath) {
            diagnostics = "PACKAGE-CLOSURE-PATH-COLLISION";
            return false;
        }
        if (entry.envelopeYaml.empty()) {
            diagnostics = makeDiagnostic("package entry requires envelope YAML");
            return false;
        }
        if (!ids.insert(entry.resourceId).second) {
            diagnostics = makeDiagnostic("duplicate package resource id: ", entry.resourceId);
            return false;
        }
        if (!paths.insert(entry.envelopePath).second) {
            diagnostics = makeDiagnostic("duplicate package envelope path: ", entry.envelopePath);
            return false;
        }
    }

    for (const auto& entry : desc.entries) {
        for (const auto& dependency : entry.dependencies) {
            if (ids.find(dependency.id) == ids.end()) {
                diagnostics = makeDiagnostic("unresolved package dependency: ", dependency.id);
                return false;
            }
        }
    }
    for (const auto& entry : desc.entries) {
        InternalAssetPackageEntry parsed;
        if (!parseEnvelopeEntry(entry.envelopeYaml, parsed, diagnostics))
            return false;
        if (!matchesEnvelopeMetadata(entry, parsed)) {
            diagnostics = makeDiagnostic(
                "PACKAGE-ENVELOPE-METADATA-MISMATCH: metadata does not match envelope: ",
                entry.resourceId);
            return false;
        }
    }
    return true;
}

core::pmr::String manifestYaml(const InternalAssetPackageDesc& desc)
{
    core::pmr::String yaml(&core::defaultCpuMemoryResource());
    yaml += "format: ";
    yaml += kInternalAssetPackageFormat;
    yaml += "\nversion: 1\nname: ";
    yaml += desc.name;
    yaml += "\nstatus: ";
    yaml += desc.status;
    yaml += "\n";
    if (!desc.closurePath.empty()) {
        auto declaration = core::YamlContract::Node::mapping();
        declaration["closure"] = core::YamlContract::Node::mapping();
        declaration["closure"]["contractVersion"] = "resource.package.closure/v1";
        declaration["closure"]["path"] = desc.closurePath.c_str();
        yaml += core::YamlContract::serialize(declaration);
    }
    yaml += "entries:\n";
    for (const auto& entry : desc.entries) {
        yaml += "  - resourceId: ";
        yaml += entry.resourceId;
        yaml += "\n    resourceType: ";
        yaml += entry.resourceType;
        yaml += "\n    alias: ";
        yaml += entry.alias;
        yaml += "\n    envelopePath: ";
        yaml += entry.envelopePath;
        yaml += "\n    status: ";
        yaml += entry.status;
        yaml += "\n    contentHash: ";
        appendUint(yaml, entry.contentHash);
        yaml += "\n";
        if (entry.dependencies.empty()) {
            yaml += "    dependencies: []\n";
        } else {
            yaml += "    dependencies:\n";
            for (const auto& dependency : entry.dependencies) {
                yaml += "      - id: ";
                yaml += dependency.id;
                yaml += "\n        usage: ";
                yaml += dependency.usage;
                yaml += "\n";
            }
        }
    }
    return yaml;
}

bool writeTextFile(const std::filesystem::path& path, NameView text, core::pmr::String& diagnostics)
{
    std::ofstream file(path, std::ios::binary);
    if (!file.good()) {
        diagnostics = makeDiagnostic("failed to write file: ");
        diagnostics += makeString(path.generic_string());
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

bool readTextFile(const std::filesystem::path& path,
    core::pmr::String& out,
    core::pmr::String& diagnostics,
    std::size_t maxBytes)
{
    const auto read = core::readTextFile(path, maxBytes ? maxBytes : UINT64_MAX);
    if (!read.success) {
        diagnostics = makeDiagnostic("missing envelope file: ");
        diagnostics += makeString(path.generic_string());
        return false;
    }
    core::TextDecodeDesc desc;
    desc.bom = core::TextBomPolicy::Strip;
    desc.nul = core::TextNulPolicy::Reject;
    // The legacy zero-limit host entry retains its read budget semantics. Text
    // conversion itself is bounded by these already acquired original bytes.
    desc.maxOutputBytes = read.text.size();
    auto decoded = core::TextDecoder::decode(read.text, desc);
    if (decoded.status != core::TextDecodeStatus::Success) {
        diagnostics = decoded.diagnosticId;
        return false;
    }
    out = std::move(decoded.text);
    return true;
}

bool parseManifest(NameView yamlText,
    InternalAssetPackageDesc& out,
    core::pmr::String& diagnostics,
    std::size_t maxResources)
{
    YamlNode root;
    try {
        root = YamlNode::deserialize(yamlText.data(), yamlText.data() + yamlText.size());
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("package manifest YAML parse failed: ");
        diagnostics += err.what();
        return false;
    }
    if (!root.is_mapping()) {
        diagnostics = makeDiagnostic("package manifest root must be a mapping");
        return false;
    }

    core::pmr::String format(&core::defaultCpuMemoryResource());
    std::uint32_t version = 0;
    if (!requireString(root, "format", format, diagnostics)
        || !requireUInt(root, "version", version, diagnostics)
        || !requireString(root, "name", out.name, diagnostics)
        || !requireString(root, "status", out.status, diagnostics)) {
        return false;
    }
    if (format != kInternalAssetPackageFormat) {
        diagnostics = makeDiagnostic("unsupported package format: ", format);
        return false;
    }
    if (version != kInternalAssetPackageVersion) {
        diagnostics = makeDiagnostic("unsupported package version");
        return false;
    }

    if (const auto* closure = findMappingValue(root, "closure")) {
        try {
            core::YamlContract::onlyKeys(*closure, {"contractVersion", "path"});
            if (core::YamlContract::text(*closure, "contractVersion")
                != "resource.package.closure/v1")
                core::YamlContract::fail();
            out.closurePath = core::YamlContract::text(*closure, "path");
            if (!safeRelativePackagePath(out.closurePath))
                core::YamlContract::fail();
        } catch (...) {
            diagnostics = "PACKAGE-CLOSURE-CONTRACT";
            return false;
        }
    }
    const YamlNode* entries = findMappingValue(root, "entries");
    if (entries == nullptr || !entries->is_sequence()) {
        diagnostics = makeDiagnostic("package entries must be a sequence");
        return false;
    }
    const auto& sequence = entries->get_value_ref<const YamlNode::sequence_type&>();
    if (maxResources && sequence.size() > maxResources) {
        diagnostics = "PACKAGE-RESOURCE-COUNT-BUDGET";
        return false;
    }
    for (const auto& item : sequence) {
        if (!item.is_mapping()) {
            diagnostics = makeDiagnostic("package entry must be a mapping");
            return false;
        }
        InternalAssetPackageEntry entry;
        if (!requireString(item, "resourceId", entry.resourceId, diagnostics)
            || !requireString(item, "resourceType", entry.resourceType, diagnostics)
            || !requireString(item, "envelopePath", entry.envelopePath, diagnostics)
            || !requireUInt(item, "contentHash", entry.contentHash, diagnostics)) {
            return false;
        }
        const YamlNode* alias = findMappingValue(item, "alias");
        if (alias != nullptr && alias->is_string()) {
            entry.alias = nodeString(*alias);
        }
        const YamlNode* status = findMappingValue(item, "status");
        if (status != nullptr && status->is_string()) {
            entry.status = nodeString(*status);
        }
        if (!parseDependencies(item, entry.dependencies, diagnostics)) {
            return false;
        }
        out.entries.push_back(std::move(entry));
    }
    return true;
}

} // namespace

ResourceEnvelopeBuildResult buildResourceEnvelope(
    const ResourceEnvelopeBuildDesc& desc, const reflection::object& payload)
{
    const auto yaml = reflection::serialize_to_yaml(payload);
    if (!yaml.success)
        return {false, {}, 0, yaml.diagnostics};
    return buildResourceEnvelopeFromYaml(desc, yaml.yaml);
}

ResourceEnvelopeBuildResult buildResourceEnvelopeFromYaml(
    const ResourceEnvelopeBuildDesc& desc, NameView payloadYaml)
{
    ResourceEnvelopeBuildResult result;
    if (desc.resourceId.empty() || !isResourceUuid(desc.resourceId)) {
        result.diagnostics = makeDiagnostic("resource envelope requires UUID resourceId");
        return result;
    }
    if (desc.resourceType.empty()) {
        result.diagnostics = makeDiagnostic("resource envelope requires resourceType");
        return result;
    }
    if (desc.status.empty()) {
        result.diagnostics = makeDiagnostic("resource envelope requires status");
        return result;
    }

    const auto hash = computeResourcePayloadHash(payloadYaml);
    if (!hash.success) {
        result.diagnostics = hash.diagnostics;
        return result;
    }

    core::pmr::String envelope(&core::defaultCpuMemoryResource());
    envelope += "format: ArpRuntimeResource\n";
    envelope += "version: 1\n";
    envelope += "resourceId: ";
    envelope += desc.resourceId;
    envelope += "\nresourceType: ";
    envelope += desc.resourceType;
    envelope += "\n";
    if (!desc.alias.empty()) {
        envelope += "alias: ";
        envelope += desc.alias;
        envelope += "\n";
    }
    if (!desc.displayName.empty()) {
        envelope += "displayName: ";
        envelope += desc.displayName;
        envelope += "\n";
    }
    envelope += "status: ";
    envelope += desc.status;
    envelope += "\ncontentHash: ";
    appendUint(envelope, hash.hash);
    envelope += "\n";
    envelope += dependencyListYaml(desc.dependencies);
    envelope += "payload:\n";
    envelope += indentYaml(payloadYaml);

    result.success = true;
    result.envelopeYaml = std::move(envelope);
    result.contentHash = hash.hash;
    return result;
}

InternalAssetPackageValidationResult InternalAssetPackageWriter::write(
    const InternalAssetPackageDesc& desc, const std::filesystem::path& outputDirectory) const
{
    InternalAssetPackageValidationResult result;
    result.package = desc;

    core::pmr::String diagnostics(&core::defaultCpuMemoryResource());
    if (!validatePackageDesc(desc, diagnostics)) {
        result.diagnostics = diagnostics;
        return result;
    }

    try {
        std::filesystem::create_directories(outputDirectory);
    } catch (const std::exception& err) {
        result.diagnostics = makeDiagnostic("failed to create package directory: ");
        result.diagnostics += err.what();
        return result;
    }

    for (const auto& entry : desc.entries) {
        InternalAssetPackageEntry parsed;
        if (!parseEnvelopeEntry(entry.envelopeYaml, parsed, diagnostics)) {
            result.diagnostics = diagnostics;
            return result;
        }
        if (!matchesEnvelopeMetadata(entry, parsed)) {
            result.diagnostics = makeDiagnostic(
                "package entry metadata does not match envelope: ", entry.resourceId);
            return result;
        }

        const auto envelopePath =
            outputDirectory / std::filesystem::path(entry.envelopePath.c_str());
        try {
            std::filesystem::create_directories(envelopePath.parent_path());
        } catch (const std::exception& err) {
            result.diagnostics = makeDiagnostic("failed to create envelope directory: ");
            result.diagnostics += err.what();
            return result;
        }
        if (!writeTextFile(envelopePath, entry.envelopeYaml, result.diagnostics)) {
            return result;
        }
    }

    if (!writeTextFile(
            outputDirectory / "manifest.arpkg.yaml", manifestYaml(desc), result.diagnostics)) {
        return result;
    }

    result.success = true;
    return result;
}

InternalAssetPackageValidationResult InternalAssetPackageReader::read(
    const std::filesystem::path& directory,
    std::size_t maxMetadataBytes,
    std::size_t maxResources) const
{
    return readImpl(directory, maxMetadataBytes, maxResources, nullptr);
}

InternalAssetPackageValidationResult InternalAssetPackageReader::readSources(
    const std::filesystem::path& directory,
    const AssetPackageSourceContext& sources,
    std::size_t maxMetadataBytes,
    std::size_t maxResources) const
{
    if (!sources.resources.requirePreloadedSources || !sources.resources.sources
        || !maxMetadataBytes || maxMetadataBytes == SIZE_MAX || !maxResources
        || maxResources == SIZE_MAX)
        return {false, "PACKAGE-IO-SOURCE-CONFIG", {}};
    return readImpl(directory, maxMetadataBytes, maxResources, &sources);
}

AssetPackageReadPlan InternalAssetPackageReader::planSourceReads(
    const std::filesystem::path& directory,
    const AssetPackageSourceContext& sources,
    const AssetPackageReadBudget& budget,
    core::SharedPtr<const AssetPackageCodecRegistry> codecs) const
{
    AssetPackageReadPlan result;
    if (!sources.resources.requirePreloadedSources || !sources.resources.sources
        || (codecs && !codecs->frozen())) {
        result.diagnostics = "PACKAGE-IO-SOURCE-CONFIG-OR-CODECS";
        return result;
    }
    for (auto value : {budget.maxMetadataBytes,
             budget.maxResources,
             budget.maxFileReads,
             budget.maxFileBytes,
             budget.maxTotalBytes,
             budget.maxDecodedBytes})
        if (!value || value == SIZE_MAX) {
            result.diagnostics = "PACKAGE-IO-BUDGET-REQUIRED";
            return result;
        }
    std::filesystem::path manifest;
    if (!resolveResourceSourcePath(directory, "manifest.arpkg.yaml", sources.origin, manifest)) {
        result.diagnostics = "PACKAGE-IO-UNSAFE-ROOT";
        return result;
    }
    const auto manifestPath = core::FilePath::utf8(manifest);
    const auto add = [&](core::StringView relative, std::size_t bytes) {
        std::filesystem::path path;
        if (!resolveResourceSourcePath(directory, relative, sources.origin, path)) {
            result.diagnostics = "PACKAGE-IO-UNSAFE-BINDING";
            return false;
        }
        return appendResourceReadIntent(
            core::FilePath::utf8(path), bytes, result.reads, result.diagnostics, sources.origin);
    };
    if (!add("manifest.arpkg.yaml", budget.maxMetadataBytes))
        return result;
    if (!sources.resources.sources->find(manifestPath)) {
        result.success = true;
        return result;
    }
    auto text =
        readResourceText(manifestPath, sources.resources, budget.maxMetadataBytes, &sources.origin);
    InternalAssetPackageDesc metadata;
    if (!text.success
        || !parseManifest(text.text, metadata, result.diagnostics, budget.maxResources)) {
        if (!text.success)
            result.diagnostics = std::move(text.diagnostics);
        return result;
    }
    bool missing = false;
    for (const auto& entry : metadata.entries) {
        if (!add(entry.envelopePath, budget.maxMetadataBytes))
            return result;
        missing |= !sources.resources.sources->find(result.reads.back().path);
    }
    if (!metadata.closurePath.empty()) {
        if (!add(metadata.closurePath, budget.maxMetadataBytes))
            return result;
        missing |= !sources.resources.sources->find(result.reads.back().path);
    }
    if (missing) {
        result.success = true;
        return result;
    }
    auto validated = readSources(directory, sources, budget.maxMetadataBytes, budget.maxResources);
    if (!validated.success) {
        result.diagnostics = std::move(validated.diagnostics);
        return result;
    }
    if (!metadata.closurePath.empty()) {
        auto closure = AssetPackageClosureService(codecs).planSourceReads(directory,
            sources,
            {budget.maxResources,
                budget.maxFileReads,
                budget.maxFileBytes,
                budget.maxTotalBytes,
                budget.maxDecodedBytes});
        if (!closure.success) {
            result.diagnostics = std::move(closure.diagnostics);
            return result;
        }
        for (auto& read : closure.reads)
            result.reads.push_back(std::move(read));
    } else
        for (const auto& entry : validated.package.entries) {
            if (codecs && codecs->find(entry.resourceType)) {
                if (!codecs->find(entry.resourceType)
                        ->planSourceReads(
                            entry, directory, sources, result.reads, result.diagnostics))
                    return result;
            } else if (entry.resourceType == PrefabRuntimePayloadResource::resourceTypeName()) {
                auto blob = PrefabRuntimePayloadSourceReader().planSourceReads(
                    entry, directory, sources.origin);
                if (!blob.success) {
                    result.diagnostics = std::move(blob.diagnostics);
                    return result;
                }
                for (auto& read : blob.reads)
                    result.reads.push_back(std::move(read));
            } else if (isManifestAsset(entry.resourceType)) {
                core::pmr::String relative;
                if (!parseTypedAssetManifestPath(entry.envelopeYaml, relative, result.diagnostics))
                    return result;
                std::filesystem::path path;
                if (!resolveResourceSourcePath(directory, relative, sources.origin, path)) {
                    result.diagnostics = "PACKAGE-IO-UNSAFE-BINDING";
                    return result;
                }
                const auto full = core::FilePath::utf8(path);
                if (!sources.resources.sources->find(full)) {
                    if (!appendResourceReadIntent(full,
                            budget.maxMetadataBytes,
                            result.reads,
                            result.diagnostics,
                            sources.origin))
                        return result;
                    continue;
                }
                const ResourceReference reference{
                    entry.resourceId, entry.resourceType, entry.alias, entry.status, {}};
                if (entry.resourceType == kVegetationCatalogCookedContract.type) {
                    auto index = VegetationCatalogCookedCodec().inspect(
                        reference, path, sources.resources, sources.origin);
                    if (!index.success) {
                        result.diagnostics = std::move(index.diagnostics);
                        return result;
                    }
                    for (auto& read : index.files)
                        result.reads.push_back(std::move(read));
                } else {
                    auto index = TerrainAssetResource(reference, path, sources.origin)
                                     .inspectManifest(sources.resources);
                    if (!index.success) {
                        result.diagnostics = std::move(index.diagnostics);
                        return result;
                    }
                    for (auto& read : index.files)
                        result.reads.push_back(std::move(read));
                }
            }
        }
    result.success = true;
    return result;
}

InternalAssetPackageValidationResult InternalAssetPackageReader::readImpl(
    const std::filesystem::path& packageDirectory,
    std::size_t maxMetadataBytes,
    std::size_t maxResources,
    const AssetPackageSourceContext* sources) const
{
    InternalAssetPackageValidationResult result;
    core::pmr::String diagnostics(&core::defaultCpuMemoryResource());
    core::pmr::String manifest(&core::defaultCpuMemoryResource());
    std::error_code pathError;
    auto canonicalPackage = packageDirectory;
    auto canonicalManifest = packageDirectory / "manifest.arpkg.yaml";
    if (!sources) {
        canonicalPackage = std::filesystem::canonical(packageDirectory, pathError);
        if (pathError || !std::filesystem::is_directory(canonicalPackage, pathError) || pathError) {
            result.diagnostics = makeDiagnostic("asset package path is not a readable directory");
            return result;
        }
        canonicalManifest =
            std::filesystem::canonical(canonicalPackage / "manifest.arpkg.yaml", pathError);
        if (pathError || !core::FilePath::contains(canonicalPackage, canonicalManifest)) {
            result.diagnostics =
                makeDiagnostic("asset package manifest resolves outside package directory");
            return result;
        }
    } else if (!resolveResourceSourcePath(
                   packageDirectory, "manifest.arpkg.yaml", sources->origin, canonicalManifest)) {
        result.diagnostics = "PACKAGE-IO-UNSAFE-ROOT";
        return result;
    }
    const auto readDocument = [&](const std::filesystem::path& path, core::pmr::String& output) {
        if (!sources)
            return readTextFile(path, output, diagnostics, maxMetadataBytes);
        auto read = readResourceText(
            core::FilePath::utf8(path), sources->resources, maxMetadataBytes, &sources->origin);
        output = std::move(read.text);
        diagnostics = std::move(read.diagnostics);
        return read.success;
    };
    if (!readDocument(canonicalManifest, manifest)) {
        result.diagnostics = diagnostics;
        return result;
    }
    if (!parseManifest(manifest, result.package, diagnostics, maxResources)) {
        result.diagnostics = diagnostics;
        return result;
    }
    for (auto& entry : result.package.entries) {
        if (!core::FilePath::isSafeRelativePath(
                std::string_view(entry.envelopePath.data(), entry.envelopePath.size()))) {
            result.diagnostics =
                makeDiagnostic("unsafe package envelope path: ", entry.envelopePath);
            return result;
        }
        const auto relativePath = std::filesystem::u8path(
            entry.envelopePath.data(), entry.envelopePath.data() + entry.envelopePath.size());
        const auto unresolvedEnvelopePath = canonicalPackage / relativePath;
        auto envelopePath = unresolvedEnvelopePath;
        if (!sources) {
            const bool envelopeExists = std::filesystem::exists(unresolvedEnvelopePath, pathError);
            if (!pathError && !envelopeExists) {
                result.diagnostics = makeDiagnostic("missing envelope file: ", entry.envelopePath);
                return result;
            }
            if (pathError) {
                result.diagnostics =
                    makeDiagnostic("cannot inspect package envelope path: ", entry.envelopePath);
                return result;
            }
            envelopePath = std::filesystem::canonical(unresolvedEnvelopePath, pathError);
            if (pathError || !core::FilePath::contains(canonicalPackage, envelopePath)) {
                result.diagnostics = makeDiagnostic(
                    "package envelope resolves outside package directory: ", entry.envelopePath);
                return result;
            }
        }
        if (!readDocument(envelopePath, entry.envelopeYaml)) {
            result.diagnostics = diagnostics;
            return result;
        }
        InternalAssetPackageEntry parsed;
        if (!parseEnvelopeEntry(entry.envelopeYaml, parsed, diagnostics)) {
            result.diagnostics = diagnostics;
            return result;
        }
        if (!matchesEnvelopeMetadata(entry, parsed)) {
            result.diagnostics =
                makeDiagnostic("manifest entry does not match envelope: ", entry.resourceId);
            return result;
        }
    }
    if (!validatePackageDesc(result.package, diagnostics)) {
        result.diagnostics = diagnostics;
        return result;
    }

    result.success = true;
    return result;
}

bool extractPayloadYaml(
    NameView envelopeYaml, core::pmr::String& outPayloadYaml, core::pmr::String& diagnostics)
{
    YamlNode root;
    try {
        root =
            YamlNode::deserialize(envelopeYaml.data(), envelopeYaml.data() + envelopeYaml.size());
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("resource envelope YAML parse failed: ");
        diagnostics += err.what();
        return false;
    }
    if (!root.is_mapping()) {
        diagnostics = makeDiagnostic("resource envelope root must be a mapping");
        return false;
    }
    const auto* payload = findMappingValue(root, "payload");
    if (payload == nullptr || !payload->is_mapping()) {
        diagnostics = makeDiagnostic("missing required payload");
        return false;
    }
    outPayloadYaml = core::YamlContract::serialize(*payload);
    return true;
}

ResourceLoadResult InternalAssetPackageReader::load(const std::filesystem::path& packageDirectory,
    ResourceManager& manager,
    ResourceLoadPolicy policy) const
{
    return load(packageDirectory, manager, {}, policy);
}

ResourceLoadResult InternalAssetPackageReader::load(const std::filesystem::path& packageDirectory,
    ResourceManager& manager,
    core::SharedPtr<const AssetPackageCodecRegistry> codecs,
    ResourceLoadPolicy policy) const
{
    return loadImpl(packageDirectory, manager, std::move(codecs), policy, nullptr);
}

ResourceLoadResult InternalAssetPackageReader::loadSources(const std::filesystem::path& directory,
    ResourceManager& manager,
    const AssetPackageSourceContext& sources,
    core::SharedPtr<const AssetPackageCodecRegistry> codecs) const
{
    if (!sources.resources.requirePreloadedSources || !sources.resources.sources)
        return {false, ResourceState::Failed, {}, {}, "PACKAGE-IO-SOURCE-CONFIG"};
    if (!sources.resources.maxDecodedBytes || sources.resources.maxDecodedBytes == SIZE_MAX)
        return {false, ResourceState::Failed, {}, {}, "PACKAGE-IO-DECODE-BUDGET"};
    return loadImpl(directory, manager, std::move(codecs), sources.resources.policy, &sources);
}

ResourceLoadResult InternalAssetPackageReader::loadImpl(
    const std::filesystem::path& packageDirectory,
    ResourceManager& manager,
    core::SharedPtr<const AssetPackageCodecRegistry> codecs,
    ResourceLoadPolicy policy,
    const AssetPackageSourceContext* sources) const
{
    if (codecs && !codecs->frozen())
        return {false, ResourceState::Failed, {}, {}, "PACKAGE-CODEC-REGISTRY-NOT-FROZEN"};
    auto readResult = sources ? readSources(packageDirectory,
                                    *sources,
                                    AssetPackageClosureService::maxMetadataBytes,
                                    AssetPackageClosureService::maxResources)
                              : read(packageDirectory,
                                    AssetPackageClosureService::maxMetadataBytes,
                                    AssetPackageClosureService::maxResources);
    if (!readResult.success) {
        return ResourceLoadResult{
            false,
            ResourceState::Failed,
            core::pmr::String(&core::defaultCpuMemoryResource()),
            core::pmr::String(&core::defaultCpuMemoryResource()),
            readResult.diagnostics,
        };
    }

    ResourceLoadContext loadContext = sources ? sources->resources : ResourceLoadContext{policy};
    if (!readResult.package.closurePath.empty()) {
        const auto closure = sources
            ? AssetPackageClosureService(codecs).inspectSources(packageDirectory, *sources)
            : AssetPackageClosureService(codecs).inspect(packageDirectory);
        if (!closure.success)
            return {false, ResourceState::Failed, {}, {}, closure.diagnostics};
        loadContext.maxDecodedBytes = closure.maxDecodedBytes;
        const auto callerBudget = manager.telemetry().budget.maxDecodedBytes;
        if (callerBudget != 0u)
            loadContext.maxDecodedBytes = std::min(loadContext.maxDecodedBytes, callerBudget);
    }
    if (sources) {
        registerPrefabAssetReflection();
        const auto managerLimit = manager.telemetry().budget.maxDecodedBytes;
        if (managerLimit)
            loadContext.maxDecodedBytes = std::min(loadContext.maxDecodedBytes, managerLimit);
        for (const auto& entry : readResult.package.entries) {
            if (entry.resourceType != PrefabResource::resourceTypeName())
                continue;
            core::pmr::String diagnostic;
            const auto typed = createPrefabResourceFromEnvelope(entry.envelopeYaml, diagnostic);
            const auto* prefab = dynamic_cast<const PrefabResource*>(typed.get());
            if (!prefab)
                return {
                    false, ResourceState::Failed, entry.resourceId, entry.resourceType, diagnostic};
            std::size_t runtimeBindings = 0;
            bool matches = false;
            for (const auto& binding : entry.dependencies)
                if (binding.usage == "runtime-payload") {
                    ++runtimeBindings;
                    matches = binding.id == prefab->runtimePayloadId;
                }
            if (runtimeBindings > 1
                || (!prefab->runtimePayloadId.empty() && (runtimeBindings != 1 || !matches))
                || (prefab->runtimePayloadId.empty() && runtimeBindings))
                return {false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    "PACKAGE-PREFAB-RUNTIME-BINDING-MISMATCH"};
        }
    }

    const bool atomicTypedPackage = sources || !readResult.package.closurePath.empty()
        || std::any_of(readResult.package.entries.begin(),
            readResult.package.entries.end(),
            [&](const auto& entry) {
                return isManifestAsset(entry.resourceType)
                    || (codecs && codecs->find(entry.resourceType));
            });
    for (const auto& entry : readResult.package.entries) {
        if (atomicTypedPackage) {
            if (!isManifestAsset(entry.resourceType)
                && !prefabResourceFactory().supports(entry.resourceType)
                && !(codecs && codecs->find(entry.resourceType)))
                return {false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    makeDiagnostic("TERRAIN-PACKAGE-TYPED-FACTORY-REQUIRED")};
        } else {
            const auto declared = manager.declareResource(entry.resourceId, entry.resourceType);
            if (!declared.success
                && declared.diagnostics.find("duplicate resource id") == core::pmr::String::npos) {
                return declared;
            }
        }
    }

    // Payload references are authoritative even when an envelope omits them.
    // Discover and type-check them before any package resource is admitted.
    std::size_t preparedCodecDecodedBytes = 0;
    if (atomicTypedPackage) {
        AssetPackagePayloadInspector inspector = sources
            ? AssetPackagePayloadInspector(codecs, packageDirectory, *sources)
            : AssetPackagePayloadInspector(codecs, packageDirectory);
        std::size_t codecDecodedBytes = 0;
        const auto decodeLimit = loadContext.maxDecodedBytes
            ? loadContext.maxDecodedBytes
            : manager.telemetry().budget.maxDecodedBytes;
        for (auto& entry : readResult.package.entries) {
            if (!inspector.supports(entry.resourceType))
                continue;
            const auto payload = inspector.inspect(entry);
            if (!payload.success)
                return {false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    payload.diagnostics};
            if (payload.maxDecodedBytes > SIZE_MAX - codecDecodedBytes
                || (decodeLimit && payload.maxDecodedBytes > decodeLimit - codecDecodedBytes))
                return {false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    "PACKAGE-CODEC-DECODED-BUDGET"};
            codecDecodedBytes += payload.maxDecodedBytes;
            for (const auto& dependency : payload.dependencies) {
                const auto found = std::find_if(readResult.package.entries.begin(),
                    readResult.package.entries.end(),
                    [&](const auto& candidate) {
                        return candidate.resourceId == dependency.binding.id;
                    });
                if ((found != readResult.package.entries.end()
                        && found->resourceType != dependency.expectedType)
                    || (found == readResult.package.entries.end()
                        && !inspector.externalReady(manager, dependency)))
                    return {false,
                        ResourceState::Failed,
                        entry.resourceId,
                        entry.resourceType,
                        makeDiagnostic("PACKAGE-PAYLOAD-DEPENDENCY-NOT-READY-OR-TYPE: ",
                            dependency.binding.id)};
                if (std::none_of(entry.dependencies.begin(),
                        entry.dependencies.end(),
                        [&](const auto& existing) {
                            return existing.id == dependency.binding.id
                                && existing.usage == dependency.binding.usage;
                        }))
                    entry.dependencies.push_back(dependency.binding);
            }
        }
        preparedCodecDecodedBytes = codecDecodedBytes;
    }

    ResourceGraphRequest ordering;
    ordering.rootResourceId = readResult.package.entries.front().resourceId;
    for (auto& entry : readResult.package.entries) {
        ResourceReference reference{
            entry.resourceId, entry.resourceType, entry.alias, entry.status, {}};
        if (isManifestAsset(entry.resourceType)) {
            core::pmr::String manifestPath{&core::defaultCpuMemoryResource()};
            core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
            if (!parseTypedAssetManifestPath(entry.envelopeYaml, manifestPath, diagnostics))
                return {false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    diagnostics};
            std::error_code error;
            auto packageRoot = packageDirectory;
            std::filesystem::path resolvedManifest;
            if (sources) {
                if (!resolveResourceSourcePath(
                        packageDirectory, manifestPath, sources->origin, resolvedManifest))
                    return {false,
                        ResourceState::Failed,
                        entry.resourceId,
                        entry.resourceType,
                        "TERRAIN-PACKAGE-MANIFEST-PATH-ESCAPE"};
            } else {
                packageRoot = std::filesystem::canonical(packageDirectory, error);
                if (error)
                    return {false,
                        ResourceState::Failed,
                        entry.resourceId,
                        entry.resourceType,
                        "TERRAIN-PACKAGE-ROOT"};
                resolvedManifest = std::filesystem::canonical(
                    packageRoot / std::filesystem::u8path(manifestPath.begin(), manifestPath.end()),
                    error);
                if (error || !core::FilePath::contains(packageRoot, resolvedManifest))
                    return {false,
                        ResourceState::Failed,
                        entry.resourceId,
                        entry.resourceType,
                        "TERRAIN-PACKAGE-MANIFEST-PATH-ESCAPE"};
            }

            struct ManifestDependencies final {
                bool success;
                core::pmr::String diagnostics;
                core::pmr::Vector<ResourceDependency> dependencies;
            };

            const auto index = [&]() -> ManifestDependencies {
                if (entry.resourceType == kVegetationCatalogCookedContract.type) {
                    auto cooked = VegetationCatalogCookedCodec().inspect(reference,
                        resolvedManifest,
                        loadContext,
                        sources ? sources->origin : ResourceFileOrigin{});
                    return {cooked.success,
                        std::move(cooked.diagnostics),
                        std::move(cooked.dependencies)};
                }
                const auto terrain = TerrainAssetResource(
                    reference, resolvedManifest, sources ? sources->origin : ResourceFileOrigin{})
                                         .inspectManifest(loadContext);
                return {terrain.success, terrain.diagnostics, terrain.dependencies};
            }();
            if (!index.success)
                return {false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    index.diagnostics};
            for (const auto& dependency : index.dependencies) {
                const auto found = std::find_if(
                    entry.dependencies.begin(), entry.dependencies.end(), [&](const auto& item) {
                        return item.id == dependency.id && item.usage == dependency.usage;
                    });
                if (found == entry.dependencies.end())
                    entry.dependencies.push_back(dependency);
            }
        }
        ResourceGraphNode node;
        node.resource = core::makeUnique<ResourceBase>(std::move(reference));
        node.dependencies = entry.dependencies;
        ordering.nodes.push_back(std::move(node));
    }
    // Already-loaded resources may be supplied by another explicit package.
    const auto packageNodeCount = ordering.nodes.size();
    for (std::size_t i = 0; i < packageNodeCount; ++i) {
        const auto dependencies = ordering.nodes[i].dependencies;
        for (const auto& dependency : dependencies) {
            if (std::any_of(ordering.nodes.begin(), ordering.nodes.end(), [&](const auto& node) {
                    return node.resource->reference().id == dependency.id;
                }))
                continue;
            const auto* record = manager.recordSnapshot(dependency.id);
            if (!record || record->state != ResourceState::Ready)
                return {false,
                    ResourceState::Failed,
                    dependency.id,
                    {},
                    makeDiagnostic("PACKAGE-DEPENDENCY-NOT-READY")};
            ResourceGraphNode external;
            external.resource = core::makeUnique<ResourceBase>(
                ResourceReference{record->id, record->type, {}, record->status, {}});
            ordering.nodes.push_back(std::move(external));
        }
    }
    const auto plan = buildResourceGraphPlan(ordering);
    if (!plan.success)
        return {false, ResourceState::Failed, plan.resourceId, plan.resourceType, plan.diagnostics};
    ResourceLoadResult last;
    core::pmr::Vector<core::UniquePtr<ResourceBase>> prepared{&core::defaultCpuMemoryResource()};

    struct PreparedPrefabRuntime {
        core::SharedPtr<const assets::AnimatedMeshAsset> asset;
        core::SharedPtr<const animation::AnimatorControllerAsset> controller;
        core::pmr::String diagnostic;
    };

    core::pmr::UnorderedMap<core::pmr::String, PreparedPrefabRuntime> prefabRuntimes;
    std::size_t prefabDecodedBytes = preparedCodecDecodedBytes;
    const auto prepareRuntime =
        [&](const core::pmr::String& payloadId,
            const InternalAssetPackageEntry* prefab) -> PreparedPrefabRuntime& {
        auto [cached, inserted] = prefabRuntimes.try_emplace(payloadId);
        auto& value = cached->second;
        if (!inserted)
            return value;
        PrefabRuntimePayloadLoadResult decoded;
        if (sources) {
            const auto payload = std::find_if(readResult.package.entries.begin(),
                readResult.package.entries.end(),
                [&](const auto& e) {
                    return e.resourceId == payloadId
                        && e.resourceType == PrefabRuntimePayloadResource::resourceTypeName();
                });
            if (payload == readResult.package.entries.end()) {
                value.diagnostic = "PACKAGE-PREFAB-PAYLOAD-MISSING";
                return value;
            }
            if (prefabDecodedBytes >= loadContext.maxDecodedBytes) {
                value.diagnostic = "PACKAGE-PREFAB-DECODED-BUDGET";
                return value;
            }
            auto context = *sources;
            context.resources.maxDecodedBytes = loadContext.maxDecodedBytes - prefabDecodedBytes;
            decoded =
                PrefabRuntimePayloadSourceReader().readSources(*payload, packageDirectory, context);
        } else if (prefab)
            decoded = loadPrefabRuntimePayloadByResourceIdFromPackage(
                packageDirectory, prefab->resourceId);
        else {
            value.diagnostic = "PACKAGE-PREFAB-PAYLOAD-OWNER-MISSING";
            return value;
        }
        if (!decoded.success || !decoded.hasCompleteControllerData) {
            value.diagnostic = decoded.diagnostics.empty()
                ? core::pmr::String("runtime payload lacks complete controller data")
                : std::move(decoded.diagnostics);
            return value;
        }
        prefabDecodedBytes += decoded.decodedBytes;
        value.asset = core::makeShared<assets::AnimatedMeshAsset>(std::move(decoded.asset));
        value.controller = core::makeShared<animation::AnimatorControllerAsset>(
            std::move(decoded.controllerAsset));
        return value;
    };
    for (const auto entryIndex : plan.order) {
        if (entryIndex >= packageNodeCount)
            continue;
        const auto& entry = readResult.package.entries[entryIndex];
        if (codecs && codecs->find(entry.resourceType)) {
            core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
            core::UniquePtr<ResourceBase> typed;
            try {
                typed = sources
                    ? codecs->find(entry.resourceType)
                          ->prepareSources(entry, packageDirectory, *sources, diagnostic)
                    : codecs->find(entry.resourceType)
                          ->prepare(entry, packageDirectory, diagnostic);
            } catch (...) {
                diagnostic = "PACKAGE-CODEC-PREPARATION-FAILED";
            }
            if (!typed || typed->reference().id != entry.resourceId
                || typed->reference().type != entry.resourceType)
                return {false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    diagnostic.empty() ? core::pmr::String("PACKAGE-CODEC-PREPARE-TYPE-OR-ID")
                                       : diagnostic};
            typed->configureMetadata(
                ResourceReference{
                    entry.resourceId, entry.resourceType, entry.alias, entry.status, {}},
                entry.dependencies);
            prepared.push_back(std::move(typed));
            last = {true, ResourceState::Ready, entry.resourceId, entry.resourceType, {}};
        } else if (isManifestAsset(entry.resourceType)) {
            core::pmr::String manifestPath(&core::defaultCpuMemoryResource());
            core::pmr::String diagnostics(&core::defaultCpuMemoryResource());
            if (!parseTypedAssetManifestPath(entry.envelopeYaml, manifestPath, diagnostics)) {
                return ResourceLoadResult{
                    false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    diagnostics,
                };
            }
            ResourceReference reference{
                entry.resourceId, entry.resourceType, entry.alias, entry.status, {}};
            const auto manifest = packageDirectory
                / std::filesystem::u8path(manifestPath.begin(), manifestPath.end());
            core::UniquePtr<ResourceBase> typed;
            if (entry.resourceType == kVegetationCatalogCookedContract.type) {
                VegetationCatalogResourceLoadDesc desc;
                desc.resourceId = entry.resourceId;
                desc.alias = entry.alias;
                desc.status = entry.status;
                desc.catalogPath = manifest;
                desc.input = VegetationCatalogInput::Cooked;
                desc.sourceOrigin = sources ? sources->origin : ResourceFileOrigin{};
                typed = core::makeUnique<VegetationCatalogResource>(std::move(desc));
            } else
                typed = core::makeUnique<TerrainAssetResource>(std::move(reference),
                    manifest,
                    sources ? sources->origin : ResourceFileOrigin{});
            typed->configureMetadata(typed->reference(), entry.dependencies);
            if (atomicTypedPackage) {
                prepared.push_back(std::move(typed));
                last = {true, ResourceState::Ready, entry.resourceId, entry.resourceType, {}};
            } else
                last = manager.loadResource(std::move(typed), {policy});
        } else if (prefabResourceFactory().supports(entry.resourceType)) {
            core::pmr::String payloadYaml(&core::defaultCpuMemoryResource());
            core::pmr::String diagnostics(&core::defaultCpuMemoryResource());
            if (!extractPayloadYaml(entry.envelopeYaml, payloadYaml, diagnostics)) {
                return ResourceLoadResult{
                    false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    diagnostics,
                };
            }
            // Package loading is also a standalone entry point; it cannot depend
            // on an editor/game having registered the typed schema beforehand.
            registerPrefabAssetReflection();
            auto payload = arp::reflection::deserialize_from_yaml(payloadYaml);
            if (!payload.success) {
                return ResourceLoadResult{
                    false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    payload.diagnostics,
                };
            }
            auto typed = prefabResourceFactory().create(entry.resourceType, payload.value);
            if (!typed) {
                return ResourceLoadResult{
                    false,
                    ResourceState::Failed,
                    entry.resourceId,
                    entry.resourceType,
                    makeDiagnostic("failed to create typed resource from envelope payload"),
                };
            }
            if (entry.resourceType == PrefabRuntimePayloadResource::resourceTypeName()) {
                const auto prefabEntry = std::find_if(readResult.package.entries.begin(),
                    readResult.package.entries.end(),
                    [&](const InternalAssetPackageEntry& candidate) {
                        if (candidate.resourceType != PrefabResource::resourceTypeName())
                            return false;
                        return std::find_if(candidate.dependencies.begin(),
                                   candidate.dependencies.end(),
                                   [&](const ResourceDependency& dependency) {
                                       return dependency.id == entry.resourceId
                                           && dependency.usage == "runtime-payload";
                                   })
                            != candidate.dependencies.end();
                    });
                if (prefabEntry != readResult.package.entries.end() || sources) {
                    const auto& decoded = prepareRuntime(entry.resourceId,
                        prefabEntry != readResult.package.entries.end() ? &*prefabEntry : nullptr);
                    if (!decoded.asset || !decoded.controller)
                        return {false,
                            ResourceState::Failed,
                            entry.resourceId,
                            entry.resourceType,
                            decoded.diagnostic};
                    auto* runtimeResource = static_cast<PrefabRuntimePayloadResource*>(typed.get());
                    runtimeResource->runtimeAsset = decoded.asset;
                    runtimeResource->controllerAsset = decoded.controller;
                }
            } else if (entry.resourceType == AnimatorControllerResource::resourceTypeName()) {
                const auto prefabEntry = std::find_if(readResult.package.entries.begin(),
                    readResult.package.entries.end(),
                    [&](const InternalAssetPackageEntry& candidate) {
                        if (candidate.resourceType != PrefabResource::resourceTypeName())
                            return false;
                        const auto referencesController =
                            std::find_if(candidate.dependencies.begin(),
                                candidate.dependencies.end(),
                                [&](const ResourceDependency& dependency) {
                                    return dependency.id == entry.resourceId
                                        && dependency.usage == "animator-controller";
                                })
                            != candidate.dependencies.end();
                        const auto referencesRuntime =
                            std::find_if(candidate.dependencies.begin(),
                                candidate.dependencies.end(),
                                [](const ResourceDependency& dependency) {
                                    return dependency.usage == "runtime-payload";
                                })
                            != candidate.dependencies.end();
                        return referencesController && referencesRuntime;
                    });
                if (prefabEntry != readResult.package.entries.end()) {
                    const auto runtime = std::find_if(prefabEntry->dependencies.begin(),
                        prefabEntry->dependencies.end(),
                        [](const auto& d) {
                            return d.usage == "runtime-payload";
                        });
                    const auto& decoded = prepareRuntime(runtime->id, &*prefabEntry);
                    if (!decoded.asset || !decoded.controller)
                        return {false,
                            ResourceState::Failed,
                            entry.resourceId,
                            entry.resourceType,
                            decoded.diagnostic};
                    auto* controllerResource =
                        static_cast<AnimatorControllerResource*>(typed.get());
                    controllerResource->controllerAsset = decoded.controller;
                }
            }
            typed->configureMetadata(
                ResourceReference{
                    entry.resourceId,
                    entry.resourceType,
                    entry.alias,
                    entry.status,
                    core::pmr::String(&core::defaultCpuMemoryResource()),
                },
                entry.dependencies);
            if (atomicTypedPackage) {
                prepared.push_back(std::move(typed));
                last = {true, ResourceState::Ready, entry.resourceId, entry.resourceType, {}};
            } else
                last = manager.loadResource(std::move(typed), {policy});
        } else {
            last = compatibility::loadFromEnvelope(manager, entry.envelopeYaml, policy);
        }
        if (!last.success) {
            return last;
        }
    }
    if (atomicTypedPackage) {
        const auto batch = manager.loadResourcesAtomically(std::move(prepared), loadContext);
        if (!batch.success)
            return {false, ResourceState::Failed, batch.failedResourceId, {}, batch.diagnostics};
    }
    return last;
}

} // namespace arp::resource
