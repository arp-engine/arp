#pragma once
#include "scene/scene_asset_resource.h"
#include "scene/script_component_module.h"
#include "scriptvm/runtime/script_value_yaml.h"

namespace arp::scriptvm {
class ScriptComponentCodec final : public scene::SceneComponentCodec {
public:
    ScriptComponentCodec(
        core::SharedPtr<ScriptComponentModule> module, std::size_t maxValues, std::size_t maxDepth)
        : module_(std::move(module)), maxValues_(maxValues), maxDepth_(maxDepth)
    {}

    const char* type() const override { return "arp.script-component/1"; }

    bool accepts(const scene::Component& c) const override
    {
        const auto* value = dynamic_cast<const scene::ScriptBehaviorComponent*>(&c);
        return value && value->scriptResourceId() == module_->resourceId();
    }

    bool attach(scene::GameObject&,
        const core::YamlContract::Node&,
        const scene::SceneObjectBindings&,
        resource::ResourceManager&,
        core::pmr::String&) const override;
    bool restore(scene::GameObject&,
        const core::YamlContract::Node&,
        const scene::SceneObjectBindings&,
        core::pmr::String&) const override;
    bool encode(const scene::Component&,
        const scene::SceneObjectBindings&,
        core::YamlContract::Node&,
        core::pmr::String&) const override;

private:
    bool reference(const TypeNode&,
        const Value&,
        core::YamlContract::Node&,
        const scene::SceneObjectBindings&) const;
    bool reference(const TypeNode&,
        const core::YamlContract::Node&,
        Value&,
        const scene::SceneObjectBindings&) const;
    core::SharedPtr<ScriptComponentModule> module_;
    std::size_t maxValues_, maxDepth_;
};
} // namespace arp::scriptvm
