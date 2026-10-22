#pragma once
#include "core/concurrency/callback.h"
#include "scene/component.h"

namespace arp::reflection {
struct SerializationDescriptor;
}

namespace arp::scene {
// Native domains compose their constructors at setup. Scene owns one factory,
// with no gameplay type switches and no script-side proxy objects.
class SceneComponentFactory final {
public:
    using Create = core::SharedCallback<core::UniquePtr<Component>()>;

    // Construction and activation consume typed state. YAML field traversal
    // belongs exclusively to the reflection archive.
    struct Persistence {
        core::pmr::String type;
        core::SharedCallback<bool(const Component&)> accepts;
        core::SharedCallback<reflection::object(const Component*)> configuration;
        core::SharedCallback<core::UniquePtr<Component>(GameObject&, const reflection::object&)>
            construct;
        core::SharedCallback<reflection::object(const Component&)> state;
        core::SharedCallback<core::SharedPtr<reflection::SerializationDescriptor>(const Component&)>
            descriptor;
        core::SharedCallback<bool(Component&, const reflection::object&)> prepare;
        core::SharedCallback<bool(Component&)> publish;
        core::pmr::Vector<core::pmr::String> after{&core::defaultCpuMemoryResource()};
    };

    bool addPersistence(Persistence entry)
    {
        if (entry.type.empty() || !entry.accepts || !entry.construct || !entry.state
            || !entry.prepare || persistence(entry.type))
            return false;
        persistence_.push_back(std::move(entry));
        return true;
    }

    const Persistence* persistence(std::string_view type) const
    {
        for (const auto& entry : persistence_)
            if (std::string_view(entry.type) == type)
                return &entry;
        return nullptr;
    }

    const Persistence* persistence(const Component& component) const
    {
        const Persistence* found = nullptr;
        for (const auto& entry : persistence_)
            if (entry.accepts(component)) {
                if (found)
                    return nullptr;
                found = &entry;
            }
        return found;
    }

    template <class C>
    bool persist(core::pmr::String stableType,
        const core::pmr::String& nativeType,
        core::pmr::Vector<core::pmr::String> after = {},
        core::SharedCallback<bool(C&)> validate = {})
    {
        const auto found = types_.find(nativeType);
        if (found == types_.end())
            return false;
        Persistence entry;
        entry.type = std::move(stableType);
        entry.accepts = [](const Component& value) {
            return dynamic_cast<const C*>(&value) != nullptr;
        };
        entry.construct = [create = found->second](GameObject&, const reflection::object&) {
            return create();
        };
        entry.state = [](const Component& value) {
            return value.editorAuthoringProperties();
        };
        entry.prepare = [validate](Component& value, const reflection::object& state) {
            return value.applyEditorAuthoringProperties(state)
                && (!validate || validate(static_cast<C&>(value)));
        };
        entry.after = std::move(after);
        return addPersistence(std::move(entry));
    }

    bool add(core::pmr::String name, Create create)
    {
        return !name.empty() && create && types_.emplace(std::move(name), std::move(create)).second;
    }

    bool contains(const core::pmr::String& name) const { return types_.find(name) != types_.end(); }

    core::UniquePtr<Component> create(const core::pmr::String& type) const
    {
        const auto found = types_.find(type);
        return found == types_.end() ? core::UniquePtr<Component>{} : found->second();
    }

private:
    core::pmr::Map<core::pmr::String, Create> types_;
    core::pmr::Vector<Persistence> persistence_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::scene
