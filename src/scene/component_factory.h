#pragma once
#include "core/callback.h"
#include "scene/component.h"

namespace arp::scene {
// Native domains compose their constructors at setup. Scene owns one factory,
// with no gameplay type switches and no script-side proxy objects.
class SceneComponentFactory final {
public:
    using Create = core::SharedCallback<core::UniquePtr<Component>()>;

    bool add(core::pmr::String name, Create create)
    {
        return !name.empty() && create && types_.emplace(std::move(name), std::move(create)).second;
    }

    core::UniquePtr<Component> create(const core::pmr::String& type) const
    {
        const auto found = types_.find(type);
        return found == types_.end() ? core::UniquePtr<Component>{} : found->second();
    }

private:
    core::pmr::Map<core::pmr::String, Create> types_;
};
} // namespace arp::scene
