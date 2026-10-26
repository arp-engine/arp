#include "scene/scene_asset_package.h"

#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "resource/serialization_reference_resolver.h"
#include "scene/scene_asset_resource.h"

namespace arp::scene {
namespace {
using Y = core::YamlContract;
using Node = Y::Node;

void require(bool value, const char* diagnostic)
{
    if (!value)
        throw core::YamlContractError{diagnostic};
}

struct DocumentBinding {
    core::pmr::String relative;
    core::pmr::String absolute;
    std::size_t bytes = 0;
    std::uint32_t objects = 0;
};

struct InspectedComponent {
    std::uint64_t object = 0;
    const SceneComponentFactory::Persistence* entry = nullptr;
    const Node* fields = nullptr;
    SceneComponentFactory::Inspection schema;
};

// A cold local reference records archive identity only, never a runtime handle.
struct InspectedReference {
    std::uint64_t object = 0;
    core::pmr::String componentType;
    std::uint32_t ordinal = 0;
};

DocumentBinding binding(
    const resource::InternalAssetPackageEntry& entry, const std::filesystem::path& directory)
{
    const auto envelope = Y::parseSingleDocument(entry.envelopeYaml);
    require(Y::text(envelope, "resourceId") == entry.resourceId
            && Y::text(envelope, "resourceType") == entry.resourceType,
        "ARP-SCENE-PACKAGE-ENVELOPE-IDENTITY");
    const auto& payload = Y::required(envelope, "payload");
    Y::onlyKeys(
        payload, {"type", "contractVersion", "documentPath", "maxDocumentBytes", "maxObjects"});
    require(Y::text(payload, "type") == "arp.scene-package"
            && Y::uint32(payload, "contractVersion") == 1,
        "ARP-SCENE-PACKAGE-PAYLOAD-VERSION");
    DocumentBinding b;
    b.relative = Y::text(payload, "documentPath");
    const auto bytes = Y::uint64(Y::required(payload, "maxDocumentBytes"));
    b.objects = Y::uint32(payload, "maxObjects");
    require(bytes && bytes <= SIZE_MAX && b.objects, "ARP-SCENE-PACKAGE-BUDGET");
    b.bytes = static_cast<std::size_t>(bytes);
    require(b.relative.find('\0') == core::pmr::String::npos
            && core::FilePath::isSafeRelativePath(b.relative),
        "ARP-SCENE-PACKAGE-DOCUMENT-PATH");
    std::error_code error;
    const auto root = std::filesystem::canonical(directory, error);
    require(!error, "ARP-SCENE-PACKAGE-ROOT");
    const auto path =
        std::filesystem::canonical(root / std::filesystem::path(b.relative.c_str()), error);
    require(!error && core::FilePath::contains(root, path)
            && std::filesystem::is_regular_file(path, error) && !error,
        "ARP-SCENE-PACKAGE-DOCUMENT-PATH");
    b.absolute = core::FilePath::utf8(path);
    return b;
}

// Reflection owns traversal. Reference metadata alone determines dependency
// type/optionality; UUID-looking ordinary strings are never interpreted.
class PackageReferences final : public reflection::SerializationContext {
public:
    PackageReferences(const Node& document,
        const SceneComponentFactory& factory,
        const core::pmr::Vector<InspectedComponent>& components,
        resource::AssetPackagePayloadIndex& result)
        : document_(document), factory_(factory), components_(components), result_(result)
    {}

    bool writeReference(const reflection::property&,
        const reflection::object&,
        Node&,
        core::pmr::String&) const override
    {
        return false;
    }

    bool readReference(const reflection::SerializationDescriptor& type,
        const Node& node,
        reflection::object& value,
        core::pmr::String& diagnostic) const override
    {
        if (!node.is_mapping() || !node.contains("object")) {
            diagnostic = "ARP-SCENE-PACKAGE-SCRIPT-REFERENCE-SHAPE";
            return false;
        }
        InspectedReference reference;
        reference.object = Y::uint64(Y::required(node, "object"));
        bool foundObject = false;
        for (const auto& object : Y::sequence(document_, "objects"))
            foundObject |= Y::uint64(Y::required(object, "id")) == reference.object;
        if (!foundObject) {
            diagnostic = "ARP-SCENE-PACKAGE-SCRIPT-REFERENCE-OBJECT";
            return false;
        }
        if (type.referenceType == "GameObject"
            || type.referenceClass == reflection::class_of<GameObject>()) {
            if (node.size() != 1) {
                diagnostic = "ARP-SCENE-PACKAGE-SCRIPT-REFERENCE-SHAPE";
                return false;
            }
            value = reflection::object(std::move(reference));
            return true;
        }
        if (node.size() != 3 || !node.contains("componentType") || !node.contains("ordinal")) {
            diagnostic = "ARP-SCENE-PACKAGE-SCRIPT-REFERENCE-SHAPE";
            return false;
        }
        reference.componentType = Y::text(node, "componentType");
        reference.ordinal = Y::uint32(node, "ordinal");
        const auto compatible = [&](const core::pmr::String& name,
                                    const reflection::class_info* nativeClass) {
            return type.referenceType == name || type.referenceType == "Component"
                || (type.referenceClass && nativeClass
                    && type.referenceClass->is_base_of(nativeClass));
        };
        // Runtime creates the implicit Transform before serialized components.
        if (reference.componentType == "TransformComponent" && reference.ordinal == 0
            && compatible(reference.componentType, reflection::class_of<TransformComponent>())) {
            value = reflection::object(std::move(reference));
            return true;
        }
        auto ordinal = reference.ordinal;
        for (const auto& component : components_)
            if (component.object == reference.object
                && component.schema.componentType == reference.componentType && ordinal-- == 0) {
                if (compatible(component.schema.componentType, component.schema.componentClass)) {
                    value = reflection::object(std::move(reference));
                    return true;
                }
                break;
            }
        diagnostic = "ARP-SCENE-PACKAGE-SCRIPT-REFERENCE-TYPE-OR-ORDINAL";
        return false;
    }

    bool readReference(const reflection::property& property,
        const Node& node,
        reflection::object& value,
        core::pmr::String& diagnostic) const override
    {
        const auto& m = property.reference();
        if (m.kind == reflection::ReferenceKind::Resource) {
            const auto id = Y::text(node);
            if (!resource::SerializationReferenceResolver::validate(nullptr, m, id, diagnostic))
                return false;
            value = reflection::object(id);
            if (id.empty())
                return true;
            if (m.targetType.empty() || m.targetType == "*") {
                diagnostic = "ARP-SCENE-PACKAGE-REFERENCE-TYPE-REQUIRED";
                return false;
            }
            const core::pmr::String usage = core::pmr::String("scene/resource/") + m.targetType;
            for (const auto& d : result_.dependencies)
                if (d.binding.id == id) {
                    if (d.expectedType == m.targetType)
                        return true;
                    diagnostic = "ARP-SCENE-PACKAGE-REFERENCE-TYPE-CONFLICT";
                    return false;
                }
            result_.dependencies.push_back({{id, usage}, m.targetType});
            return true;
        }
        const auto local = Y::uint64(node);
        value = reflection::object(local);
        if (!local && !m.required)
            return true;
        for (const auto& object : Y::sequence(document_, "objects")) {
            if (Y::uint64(Y::required(object, "id")) != local)
                continue;
            if (m.targetType == "*")
                return true;
            std::size_t matches = 0;
            for (const auto& c : Y::sequence(object, "components")) {
                const auto* e = factory_.persistence(Y::text(c, "type"));
                if (e && e->nativeType == m.targetType)
                    ++matches;
            }
            if (matches == 1)
                return true;
        }
        diagnostic = "ARP-SCENE-PACKAGE-LOCAL-REFERENCE-DANGLING-OR-TYPE";
        return false;
    }

private:
    const Node& document_;
    const SceneComponentFactory& factory_;
    const core::pmr::Vector<InspectedComponent>& components_;
    resource::AssetPackagePayloadIndex& result_;
};

class ScenePackageCodec final : public resource::AssetPackageCodec {
public:
    ScenePackageCodec(bool prefab, core::SharedPtr<const SceneComponentFactory> factory)
        : prefab_(prefab), factory_(std::move(factory))
    {}

    resource::NameView resourceType() const noexcept override
    {
        return prefab_ ? "ScenePrefabResource" : "SceneAssetResource";
    }

    resource::AssetPackagePayloadIndex inspect(const resource::InternalAssetPackageEntry& entry,
        const std::filesystem::path& root) const override
    {
        resource::AssetPackagePayloadIndex result;
        try {
            require(entry.resourceType == resourceType(), "ARP-SCENE-PACKAGE-RESOURCE-TYPE");
            const auto b = binding(entry, root);
            const auto source =
                core::readTextFile(std::filesystem::path(b.absolute.c_str()), b.bytes);
            require(source.success, "ARP-SCENE-PACKAGE-DOCUMENT-READ-OR-BUDGET");
            const auto document = Y::parseSingleDocument(source.text);
            require(SceneDocumentResource::validateDocument(document,
                        prefab_ ? "arp.scene-prefab" : "arp.scene-asset",
                        entry.resourceId,
                        b.objects),
                "ARP-SCENE-PACKAGE-DOCUMENT-TYPE-ID-OR-CONTRACT");
            core::pmr::Vector<InspectedComponent> components;
            reflection::SerializationArchive configurationArchive;
            for (const auto& object : Y::sequence(document, "objects"))
                for (const auto& c : Y::sequence(object, "components")) {
                    const auto* e = factory_->persistence(Y::text(c, "type"));
                    require(
                        e && (e->inspectConfiguration || (e->inspectionState && !e->descriptor)),
                        "ARP-SCENE-PACKAGE-COMPONENT-INSPECTION-CONTRACT-REQUIRED");
                    InspectedComponent candidate;
                    candidate.object = Y::uint64(Y::required(object, "id"));
                    candidate.entry = e;
                    candidate.fields = &Y::required(c, "fields");
                    if (e->inspectConfiguration) {
                        reflection::object configuration;
                        if (e->configuration) {
                            configuration = e->configuration(nullptr);
                            if (!configurationArchive.readFields(
                                    Y::required(c, "configuration"), configuration)) {
                                result.diagnostics = configurationArchive.diagnostics();
                                return result;
                            }
                        } else
                            require(Y::required(c, "configuration").is_null(),
                                "ARP-SCENE-PACKAGE-CONFIGURATION-INSPECTION-UNSUPPORTED");
                        if (!e->inspectConfiguration(
                                configuration, candidate.schema, result.diagnostics))
                            return result;
                        require(!candidate.schema.state.is_undefined()
                                && !candidate.schema.componentType.empty(),
                            "ARP-SCENE-PACKAGE-COMPONENT-INSPECTION-STATE");
                    } else {
                        require(Y::required(c, "configuration").is_null(),
                            "ARP-SCENE-PACKAGE-CONFIGURATION-INSPECTION-UNSUPPORTED");
                        candidate.schema.state = e->inspectionState();
                        candidate.schema.componentType = e->nativeType;
                        candidate.schema.componentClass = e->nativeClass;
                    }
                    components.push_back(std::move(candidate));
                }
            PackageReferences references(document, *factory_, components, result);
            reflection::SerializationArchive archive({}, &references);
            for (auto& component : components) {
                const auto& descriptor = component.schema.descriptor;
                const bool read = descriptor
                    ? archive.readValue(*descriptor, *component.fields, component.schema.state)
                    : archive.readFields(*component.fields, component.schema.state);
                if (!read) {
                    result.diagnostics = archive.diagnostics();
                    return result;
                }
                require(!component.entry->validateState
                        || component.entry->validateState(component.schema.state),
                    "ARP-SCENE-PACKAGE-COMPONENT-STATE");
            }
            require(result.dependencies.size() == entry.dependencies.size(),
                "ARP-SCENE-PACKAGE-DEPENDENCY-BINDINGS");
            for (const auto& d : result.dependencies)
                require(std::count_if(entry.dependencies.begin(),
                            entry.dependencies.end(),
                            [&](const auto& other) {
                                return other.id == d.binding.id && other.usage == d.binding.usage;
                            })
                        == 1,
                    "ARP-SCENE-PACKAGE-DEPENDENCY-BINDINGS");
            result.files.push_back(b.relative);
            result.maxDecodedBytes = b.bytes;
            result.success = true;
        } catch (const core::YamlContractError& e) {
            result.diagnostics = e.diagnostic;
        } catch (...) {
            result.diagnostics = "ARP-SCENE-PACKAGE-INVALID";
        }
        return result;
    }

    core::UniquePtr<resource::ResourceBase> prepare(
        const resource::InternalAssetPackageEntry& entry,
        const std::filesystem::path& root,
        core::pmr::String& diagnostic) const override
    {
        const auto checked = inspect(entry, root);
        if (!checked.success) {
            diagnostic = checked.diagnostics;
            return {};
        }
        try {
            const auto b = binding(entry, root);
            if (prefab_)
                return core::makeUnique<ScenePrefabResource>(
                    entry.resourceId, b.absolute, b.bytes, b.objects);
            return core::makeUnique<SceneAssetResource>(
                entry.resourceId, b.absolute, b.bytes, b.objects);
        } catch (...) {
            diagnostic = "ARP-SCENE-PACKAGE-PREPARE";
            return {};
        }
    }

private:
    bool prefab_;
    core::SharedPtr<const SceneComponentFactory> factory_;
};
} // namespace

bool registerSceneAssetPackageCodecs(resource::AssetPackageCodecRegistry& registry,
    core::SharedPtr<const SceneComponentFactory> factory,
    core::pmr::String& diagnostic)
{
    if (!factory) {
        diagnostic = "ARP-SCENE-PACKAGE-FACTORY-REQUIRED";
        return false;
    }
    auto snapshot = core::makeShared<SceneComponentFactory>(*factory);
    return registry.addAll({core::makeShared<ScenePackageCodec>(false, snapshot),
                               core::makeShared<ScenePackageCodec>(true, snapshot)},
        diagnostic);
}
} // namespace arp::scene
