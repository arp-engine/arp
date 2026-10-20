#include "scene/scene_asset_resource.h"

#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::scene {
using Y = core::YamlContract;
using Node = Y::Node;

namespace {
core::pmr::String serialize(const Node& node)
{
    const auto text = Node::serialize(node);
    return core::pmr::String(text.data(), text.size());
}

bool validate(const Node& root,
    const core::pmr::String& type,
    const core::pmr::String& id,
    std::uint32_t maxObjects)
{
    if (Y::text(root, "type") != type || Y::uint32(root, "contractVersion") != 1
        || Y::text(root, "resourceId") != id || !resource::isResourceUuid(id))
        return false;
    (void)Y::text(root, "name");
    const auto& objects = Y::sequence(root, "objects");
    if (!maxObjects || objects.size() > maxObjects)
        return false;
    core::pmr::Vector<std::uint64_t> ids, parents;
    ids.reserve(objects.size());
    parents.reserve(objects.size());
    for (const auto& object : objects) {
        const auto local = Y::uint64(Y::required(object, "id"));
        if (!local || std::find(ids.begin(), ids.end(), local) != ids.end())
            return false;
        ids.push_back(local);
        parents.push_back(Y::uint64(Y::required(object, "parent")));
        (void)Y::text(object, "name");
        (void)Y::boolean(object, "active");
        const auto& transform = Y::required(object, "transform");
        for (const char* key : {"x", "y", "z", "qx", "qy", "qz", "qw", "sx", "sy", "sz"}) {
            const auto value = Y::number(transform, key);
            if (!std::isfinite(float(value)))
                return false;
        }
        const double x = Y::number(transform, "qx"), y = Y::number(transform, "qy"),
                     z = Y::number(transform, "qz"), w = Y::number(transform, "qw");
        if (std::abs(x * x + y * y + z * z + w * w - 1) > 1e-5)
            return false;
        for (const auto& component : Y::sequence(object, "components"))
            if (Y::text(component, "type").empty())
                return false;
    }
    for (std::size_t index = 0; index < ids.size(); ++index) {
        auto parent = parents[index];
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

bool SceneCodecRegistry::add(core::UniquePtr<SceneComponentCodec> codec)
{
    if (!codec || !codec->type() || find(codec->type()))
        return false;
    codecs_.push_back(std::move(codec));
    return true;
}

const SceneComponentCodec* SceneCodecRegistry::find(std::string_view type) const
{
    for (const auto& c : codecs_)
        if (type == c->type())
            return c.get();
    return nullptr;
}

const SceneComponentCodec* SceneCodecRegistry::find(const Component& component) const
{
    const SceneComponentCodec* result = nullptr;
    for (const auto& c : codecs_)
        if (c->accepts(component)) {
            if (result)
                return nullptr;
            result = c.get();
        }
    return result;
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
        auto root = Node::deserialize(yaml);
        if (!validate(root, documentType_, reference().id, maxObjects_))
            return fail("ARP-SCENE-ASSET-CONTRACT");
        yaml_ = std::move(yaml);
        return resources.admitResourceMetadata(*this, context.policy);
    } catch (...) {
        return fail("ARP-SCENE-ASSET-YAML");
    }
}

core::UniquePtr<Scene> SceneDocumentResource::instantiate(resource::ResourceManager& resources,
    const SceneCodecRegistry& codecs,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    try {
        auto root = Node::deserialize(yaml_);
        auto candidate = core::makeUnique<Scene>(Y::text(root, "name"));
        if (!populate(*candidate, candidate->rootId(), resources, codecs, diagnostic))
            return {};
        return candidate;
    } catch (...) {
        diagnostic = "ARP-SCENE-ASSET-INSTANTIATE";
        return {};
    }
}

GameObjectId SceneDocumentResource::instantiateInto(Scene& target,
    GameObjectId parent,
    resource::ResourceManager& resources,
    const SceneCodecRegistry& codecs,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    if (!target.findObject(parent)) {
        diagnostic = "ARP-SCENE-ASSET-INSTANCE-PARENT";
        return {};
    }
    GameObjectId instance;
    try {
        auto root = Node::deserialize(yaml_);
        auto& object = target.createObject(Y::text(root, "name"));
        instance = object.id();
        if (target.reparent(instance, parent).ok()
            && populate(target, instance, resources, codecs, diagnostic))
            return instance;
    } catch (...) {
        diagnostic = "ARP-SCENE-ASSET-INSTANCE-EXCEPTION";
    }
    if (instance.valid())
        target.destroyObject(instance);
    return {};
}

bool SceneDocumentResource::populate(Scene& target,
    GameObjectId instanceRoot,
    resource::ResourceManager& resources,
    const SceneCodecRegistry& codecs,
    core::pmr::String& diagnostic) const
{
    try {
        auto root = Node::deserialize(yaml_);
        core::pmr::Vector<SceneObjectBinding> bindings;
        for (const auto& node : Y::sequence(root, "objects")) {
            auto& object = target.createObject(Y::text(node, "name"));
            if (!target.reparent(object.id(), instanceRoot).ok()) {
                diagnostic = "ARP-SCENE-ASSET-INSTANCE-ROOT";
                return false;
            }
            object.setActive(Y::boolean(node, "active"));
            const auto& t = Y::required(node, "transform");
            Transform transform;
            transform.translation = {
                float(Y::number(t, "x")), float(Y::number(t, "y")), float(Y::number(t, "z"))};
            transform.rotation = {float(Y::number(t, "qx")),
                float(Y::number(t, "qy")),
                float(Y::number(t, "qz")),
                float(Y::number(t, "qw"))};
            transform.scale = {
                float(Y::number(t, "sx")), float(Y::number(t, "sy")), float(Y::number(t, "sz"))};
            object.set_local_transform(transform);
            bindings.push_back({Y::uint64(Y::required(node, "id")), object.id()});
        }
        const SceneObjectBindings resolver(bindings, target);
        for (const auto& node : Y::sequence(root, "objects")) {
            const auto objectId = resolver.object(Y::uint64(Y::required(node, "id")));
            auto* object = target.findObject(objectId);
            const auto parent = Y::uint64(Y::required(node, "parent"));
            if (parent && !target.reparent(objectId, resolver.object(parent)).ok()) {
                diagnostic = "ARP-SCENE-ASSET-PARENT";
                return false;
            }
        }
        // Resolve the full hierarchy before components observe world transforms.
        // Explicit phases ensure forward body references never depend on object order.
        for (auto phase : {SceneComponentCodec::Phase::Components,
                 SceneComponentCodec::Phase::Bodies,
                 SceneComponentCodec::Phase::Colliders,
                 SceneComponentCodec::Phase::Joints})
            for (const auto& node : Y::sequence(root, "objects")) {
                auto* object =
                    target.findObject(resolver.object(Y::uint64(Y::required(node, "id"))));
                for (const auto& component : Y::sequence(node, "components")) {
                    auto* codec = codecs.find(Y::text(component, "type"));
                    if (!codec) {
                        diagnostic = "ARP-SCENE-ASSET-CODEC-MISSING";
                        return false;
                    }
                    if (codec->phase() == phase
                        && !codec->attach(*object, component, resolver, resources, diagnostic))
                        return false;
                }
            }
        for (const auto& node : Y::sequence(root, "objects")) {
            auto* object = target.findObject(resolver.object(Y::uint64(Y::required(node, "id"))));
            for (const auto& component : Y::sequence(node, "components")) {
                auto* codec = codecs.find(Y::text(component, "type"));
                if (!codec || !codec->restore(*object, component, resolver, diagnostic))
                    return false;
            }
        }
        return true;
    } catch (...) {
        diagnostic = "ARP-SCENE-ASSET-INSTANTIATE";
        return false;
    }
}

bool SceneDocumentResource::encode(const Scene& scene,
    const SceneCodecRegistry& codecs,
    const char* type,
    const core::pmr::String& id,
    core::pmr::String& yaml,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!type
        || (std::string_view(type) != "arp.scene-asset"
            && std::string_view(type) != "arp.scene-prefab")
        || !resource::isResourceUuid(id)) {
        diagnostic = "ARP-SCENE-ASSET-ENCODE-CONFIG";
        return false;
    }
    try {
        Node root = Node::mapping();
        root["type"] = type;
        root["contractVersion"] = 1;
        root["resourceId"] = id.c_str();
        root["name"] = scene.name().c_str();
        root["objects"] = Node::sequence();
        core::pmr::Vector<SceneObjectBinding> bindings;
        for (const auto& object : scene.objects())
            bindings.push_back({bindings.size() + 1, object->id()});
        const SceneObjectBindings resolver(bindings, scene);
        for (const auto& object : scene.objects()) {
            Node node = Node::mapping();
            node["id"] = resolver.local(object->id());
            node["name"] = object->name().c_str();
            node["parent"] =
                object->parentId() == scene.rootId() ? 0 : resolver.local(object->parentId());
            node["active"] = object->active();
            const auto& t = object->local_transform();
            Node transform = Node::mapping();
            transform["x"] = t.translation.x;
            transform["y"] = t.translation.y;
            transform["z"] = t.translation.z;
            transform["qx"] = t.rotation.x;
            transform["qy"] = t.rotation.y;
            transform["qz"] = t.rotation.z;
            transform["qw"] = t.rotation.w;
            transform["sx"] = t.scale.x;
            transform["sy"] = t.scale.y;
            transform["sz"] = t.scale.z;
            node["transform"] = std::move(transform);
            node["components"] = Node::sequence();

            struct EncodedComponent {
                const SceneComponentCodec* codec;
                const Component* component;
                std::size_t ordinal;
            };

            core::pmr::Vector<EncodedComponent> ordered;
            for (const auto& component : object->components()) {
                if (dynamic_cast<const TransformComponent*>(component.get()))
                    continue;
                auto* codec = codecs.find(*component);
                if (!codec) {
                    diagnostic = "ARP-SCENE-ASSET-CODEC-MISSING-OR-AMBIGUOUS";
                    return false;
                }
                ordered.push_back({codec, component.get(), ordered.size()});
            }
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
                const auto x = std::string_view(a.codec->type()),
                           y = std::string_view(b.codec->type());
                return x != y ? x < y : a.ordinal < b.ordinal;
            });
            for (const auto& entry : ordered) {
                Node data = Node::mapping();
                data["type"] = entry.codec->type();
                if (!entry.codec->encode(*entry.component, resolver, data, diagnostic))
                    return false;
                node["components"].get_value_ref<Node::sequence_type&>().push_back(std::move(data));
            }
            root["objects"].get_value_ref<Node::sequence_type&>().push_back(std::move(node));
        }
        auto output = serialize(root);
        if (!validate(root, core::pmr::String(type), id, UINT32_MAX)) {
            diagnostic = "ARP-SCENE-ASSET-ENCODE-CONTRACT";
            return false;
        }
        yaml = std::move(output);
        return true;
    } catch (...) {
        diagnostic = "ARP-SCENE-ASSET-ENCODE";
        return false;
    }
}

bool ObjectLinksCodec::attach(GameObject& object,
    const Node& node,
    const SceneObjectBindings& bindings,
    resource::ResourceManager&,
    core::pmr::String& diagnostic) const
{
    core::pmr::Vector<SceneObjectLink> links;
    for (const auto& link : Y::sequence(node, "links")) {
        const auto name = Y::text(link, "name");
        const auto target = bindings.object(Y::uint64(Y::required(link, "target")));
        if (name.empty() || !target.valid()
            || std::any_of(links.begin(), links.end(), [&](const auto& l) {
                   return l.name == name;
               })) {
            diagnostic = "ARP-SCENE-ASSET-LOCAL-REFERENCE";
            return false;
        }
        links.push_back({name, target});
    }
    object.addComponent<ObjectLinksComponent>().links = std::move(links);
    return true;
}

bool ObjectLinksCodec::encode(const Component& component,
    const SceneObjectBindings& bindings,
    Node& node,
    core::pmr::String& diagnostic) const
{
    node["links"] = Node::sequence();
    for (const auto& link : static_cast<const ObjectLinksComponent&>(component).links) {
        auto target = bindings.local(link.target);
        if (!target) {
            diagnostic = "ARP-SCENE-ASSET-EXTERNAL-REFERENCE";
            return false;
        }
        Node data = Node::mapping();
        data["name"] = link.name.c_str();
        data["target"] = target;
        node["links"].get_value_ref<Node::sequence_type&>().push_back(std::move(data));
    }
    return true;
}

} // namespace arp::scene
