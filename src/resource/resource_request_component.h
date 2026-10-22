#pragma once

#include "core/io/filesystem_path.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "scene/component.h"

#include <cstdint>

namespace arp::resource {

struct ResourceEnvironmentRequest {
    core::pmr::String kind{&core::defaultCpuMemoryResource()};
    core::pmr::String id{&core::defaultCpuMemoryResource()};
};

struct ResourceRequestState {
    bool failed = false;
    core::pmr::String meshAsset{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> contentAssets{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ResourceEnvironmentRequest> environmentAssets{
        &core::defaultCpuMemoryResource()};
    core::pmr::String terrainRecipe{&core::defaultCpuMemoryResource()};
    core::pmr::String pcgProfile{&core::defaultCpuMemoryResource()};
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ResourceRequestEditorProperties {
    APROP(Editor)
    core::pmr::String meshAsset{&core::defaultCpuMemoryResource()};
    APROP(Editor)
    core::pmr::String terrainRecipe{&core::defaultCpuMemoryResource()};
    APROP(Editor)
    core::pmr::String pcgProfile{&core::defaultCpuMemoryResource()};
};

/// Scene-owned resource declarations. The component only stores authored
/// resource identities; ResourceManager performs import and residency in the
/// candidate world after the script has finished authoring the scene graph.
ACLASS(Name = "ResourceRequestComponent", Ownership = Engine, Lifecycle = Explicit)

class ResourceRequestComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "ResourceRequestComponent"; }

    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }

    AFUNC()

    bool set_mesh_asset(const core::pmr::String& value) { return setPath(state_.meshAsset, value); }
    AFUNC()

    bool add_content_asset(const core::pmr::String& value)
    {
        if (!validPath(value))
            return fail();
        for (const auto& item : state_.contentAssets)
            if (item == value)
                return true;
        state_.contentAssets.push_back(core::pmr::String(value, &core::defaultCpuMemoryResource()));
        return true;
    }
    AFUNC()

    bool add_environment_asset(const core::pmr::String& kind, const core::pmr::String& id)
    {
        if (kind.empty() || id.empty() || kind.size() > 64u || id.size() > 256u)
            return fail();
        for (const auto& item : state_.environmentAssets)
            if (item.kind == kind && item.id == id)
                return true;
        ResourceEnvironmentRequest item;
        item.kind = kind;
        item.id = id;
        state_.environmentAssets.push_back(std::move(item));
        return true;
    }
    AFUNC()

    bool set_terrain_recipe(const core::pmr::String& value)
    {
        return setPath(state_.terrainRecipe, value);
    }
    AFUNC()

    bool set_pcg_profile(const core::pmr::String& value)
    {
        return setPath(state_.pcgProfile, value);
    }
    AFUNC()

    bool ok() const { return !state_.failed; }
    AFUNC()

    core::pmr::String mesh_asset() const
    {
        return core::pmr::String(state_.meshAsset, &core::defaultCpuMemoryResource());
    }
    AFUNC()

    std::int32_t content_asset_count() const
    {
        return static_cast<std::int32_t>(state_.contentAssets.size());
    }
    AFUNC()

    core::pmr::String content_asset(std::int32_t index) const
    {
        return index >= 0 && static_cast<std::size_t>(index) < state_.contentAssets.size()
            ? core::pmr::String(state_.contentAssets[static_cast<std::size_t>(index)],
                  &core::defaultCpuMemoryResource())
            : core::pmr::String(&core::defaultCpuMemoryResource());
    }
    AFUNC()

    std::int32_t environment_asset_count() const
    {
        return static_cast<std::int32_t>(state_.environmentAssets.size());
    }
    AFUNC()

    core::pmr::String environment_asset_kind(std::int32_t index) const
    {
        return index >= 0 && static_cast<std::size_t>(index) < state_.environmentAssets.size()
            ? core::pmr::String(state_.environmentAssets[static_cast<std::size_t>(index)].kind,
                  &core::defaultCpuMemoryResource())
            : core::pmr::String(&core::defaultCpuMemoryResource());
    }
    AFUNC()

    core::pmr::String environment_asset_id(std::int32_t index) const
    {
        return index >= 0 && static_cast<std::size_t>(index) < state_.environmentAssets.size()
            ? core::pmr::String(state_.environmentAssets[static_cast<std::size_t>(index)].id,
                  &core::defaultCpuMemoryResource())
            : core::pmr::String(&core::defaultCpuMemoryResource());
    }
    AFUNC()

    core::pmr::String terrain_recipe() const
    {
        return core::pmr::String(state_.terrainRecipe, &core::defaultCpuMemoryResource());
    }
    AFUNC()

    core::pmr::String pcg_profile() const
    {
        return core::pmr::String(state_.pcgProfile, &core::defaultCpuMemoryResource());
    }

    arp::reflection::object editorAuthoringProperties() const override
    {
        ResourceRequestEditorProperties properties;
        properties.meshAsset = state_.meshAsset;
        properties.terrainRecipe = state_.terrainRecipe;
        properties.pcgProfile = state_.pcgProfile;
        return arp::reflection::object(std::move(properties));
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<ResourceRequestEditorProperties>())
            return false;
        const auto& properties = value.cast<const ResourceRequestEditorProperties&>();
        if (!validOptionalPath(properties.meshAsset) || !validOptionalPath(properties.terrainRecipe)
            || !validOptionalPath(properties.pcgProfile))
            return false;
        state_.meshAsset.assign(properties.meshAsset);
        state_.terrainRecipe.assign(properties.terrainRecipe);
        state_.pcgProfile.assign(properties.pcgProfile);
        return true;
    }

private:
    static bool validPath(const core::pmr::String& value)
    {
        return core::FilePath::isSafeRelativePath(std::string_view(value.data(), value.size()));
    }

    static bool validOptionalPath(const core::pmr::String& value)
    {
        return value.empty() || validPath(value);
    }

    static bool setPath(core::pmr::String& target, const core::pmr::String& value)
    {
        if (!validOptionalPath(value))
            return false;
        target = value;
        return true;
    }

    bool fail()
    {
        state_.failed = true;
        return false;
    }

    ResourceRequestState state_;
};

} // namespace arp::resource
