#include "scene/scene_asset_resource.h"

#include "resource/resource_source_io.h"

namespace arp::scene {
using Y = core::YamlContract;
using Node = Y::Node;

namespace {
bool validate(const Node& root,
    const core::pmr::String& type,
    const core::pmr::String& id,
    std::uint32_t maxObjects)
{
    if (!root.is_mapping() || root.size() != 5 || Y::text(root, "type") != type
        || Y::uint32(root, "contractVersion") != 2 || Y::text(root, "resourceId") != id
        || !resource::isResourceUuid(id) || !initializeSceneAuthoringReflection())
        return false;
    (void)Y::text(root, "name");
    const auto& objects = Y::sequence(root, "objects");
    if (!maxObjects || objects.size() > maxObjects)
        return false;
    core::pmr::Vector<std::uint64_t> ids{&core::defaultCpuMemoryResource()},
        parents{&core::defaultCpuMemoryResource()};
    reflection::SerializationArchive archive;
    std::size_t components = 0;
    for (const auto& node : objects) {
        if (!node.is_mapping() || node.size() != 5)
            return false;
        const auto local = Y::uint64(Y::required(node, "id"));
        if (!local || std::find(ids.begin(), ids.end(), local) != ids.end())
            return false;
        ids.push_back(local);
        parents.push_back(Y::uint64(Y::required(node, "parent")));
        auto properties = reflection::object(SceneObjectAuthoringProperties{});
        auto transform = reflection::object(Transform{});
        if (!archive.readFields(Y::required(node, "properties"), properties)
            || !archive.readFields(Y::required(node, "transform"), transform))
            return false;
        const auto& q = transform.cast<const Transform&>().rotation;
        if (std::abs(core::dot(q, q) - 1) > 1e-5)
            return false;
        for (const auto& component : Y::sequence(node, "components")) {
            if (++components > 4096 || !component.is_mapping() || component.size() != 5
                || Y::text(component, "type").empty() || Y::uint32(component, "schemaVersion") != 1
                || !Y::required(component, "fields").is_mapping())
                return false;
            (void)Y::boolean(component, "enabled");
            (void)Y::required(component, "configuration");
        }
    }
    for (auto parent : parents) {
        std::size_t depth = 0;
        while (parent) {
            auto found = std::find(ids.begin(), ids.end(), parent);
            if (found == ids.end() || ++depth > ids.size())
                return false;
            parent = parents[std::size_t(found - ids.begin())];
        }
    }
    return true;
}
} // namespace

GameObjectId SceneObjectBindings::object(std::uint64_t local) const
{
    for (const auto& b : bindings_)
        if (b.localId == local)
            return b.objectId;
    return {};
}

std::uint64_t SceneObjectBindings::local(GameObjectId object) const
{
    for (const auto& b : bindings_)
        if (b.objectId == object)
            return b.localId;
    return 0;
}

SceneDocumentResource::SceneDocumentResource(core::pmr::String id,
    core::pmr::String path,
    std::size_t bytes,
    std::uint32_t objects,
    const char* resourceType,
    const char* documentType)
    : ResourceBase(resource::authoredResourceReference(std::move(id), resourceType)),
      path_(std::move(path)), documentType_(documentType), maxBytes_(bytes), maxObjects_(objects)
{}

bool SceneDocumentResource::collectFileReadIntents(resource::ResourceManager&,
    core::pmr::Vector<resource::ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostic) const
{
    return resource::appendResourceReadIntent(path_, maxBytes_, intents, diagnostic);
}

resource::ResourceLoadResult SceneDocumentResource::load(
    resource::ResourceManager& resources, const resource::ResourceLoadContext& context)
{
    auto fail = [&](const char* d) {
        return resource::resourceLoadFailure(*this, d);
    };
    auto source = resource::readResourceSource(path_, context, maxBytes_);
    if (!source.success)
        return fail(source.diagnostics.c_str());
    try {
        core::pmr::String yaml(
            reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size());
        auto root = Y::parseSingleDocument(yaml);
        if (Y::uint32(root, "contractVersion") == 1)
            return fail(
                "ARP-SCENE-MIGRATION-REQUIRED: run python tools/migrate_scene_v1.py source destination");
        if (!validate(root, documentType_, reference().id, maxObjects_))
            return fail("ARP-SCENE-ASSET-CONTRACT");
        yaml_ = std::move(yaml);
        return resources.admitResourceMetadata(*this, context.policy);
    } catch (...) {
        return fail("ARP-SCENE-ASSET-YAML");
    }
}

} // namespace arp::scene
