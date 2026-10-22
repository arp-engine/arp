#include "core/memory/cpu_allocator.h"
#include "renderer/particles/particle_texture_resource.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <filesystem>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

std::filesystem::path projectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "tests" / "fixtures" / "unity_particle")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
}

void particle_texture_resource_loads_renderer_facing_rgba8_payload()
{
    arp::resource::ResourceManager manager;

    arp::renderer::particles::ParticleTextureResourceLoadDesc desc;
    desc.sourceRoot = projectRoot() / "tests" / "fixtures" / "unity_particle";
    desc.sourcePath = "textures/pika_flipbook_explosion.png";
    desc.resourceId = text("8c7fe3b9-7ed1-4cb0-a31a-1fa74c8bf401");
    desc.alias = text("particle/flipbook/explosion");
    desc.status = text("Prototype Complete");
    desc.srgb = true;
    desc.intent.resourceUsage = text("particle.sprite.sampled");
    desc.intent.uploadPolicy = text("renderer-owned-staging");
    desc.intent.requiredFormat = text("Rgba8Unorm");
    desc.intent.requiredDescriptorSet = text("ParticleTextureSet");

    const auto loaded = arp::renderer::particles::loadParticleTextureResource(manager, desc);
    assert(loaded.success);
    assert(loaded.resource.resourceType == text("ParticleTextureResource"));

    const auto handle = manager.findByAlias<arp::renderer::particles::ParticleTextureResource>(
        text("particle/flipbook/explosion"));
    assert(handle.ready());
    assert(handle.get() != nullptr);
    assert(handle.get()->data != nullptr);
    assert(handle.get()->data->valid());
    assert(handle.get()->data->intent.backendOwnedUploadStaging);
    assert(!handle.get()->data->exposesNativeBackendHandles);
    assert(handle.get()->data->width == 256u);
    assert(handle.get()->data->height == 256u);
    assert(handle.get()->data->textureResourceId == text("8c7fe3b9-7ed1-4cb0-a31a-1fa74c8bf401"));
}

void particle_texture_resource_rejects_missing_source_path()
{
    arp::resource::ResourceManager manager;

    arp::renderer::particles::ParticleTextureResourceLoadDesc desc;
    desc.sourceRoot = projectRoot() / "tests" / "fixtures" / "unity_particle";
    desc.resourceId = text("8c7fe3b9-7ed1-4cb0-a31a-1fa74c8bf401");
    desc.alias = text("particle/flipbook/explosion");
    desc.status = text("Prototype Complete");
    desc.intent.resourceUsage = text("particle.sprite.sampled");
    desc.intent.uploadPolicy = text("renderer-owned-staging");
    desc.intent.requiredFormat = text("Rgba8Unorm");
    desc.intent.requiredDescriptorSet = text("ParticleTextureSet");

    const auto loaded = arp::renderer::particles::loadParticleTextureResource(manager, desc);
    assert(!loaded.success);
    assert(loaded.diagnostics.find("sourcePath") != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    particle_texture_resource_loads_renderer_facing_rgba8_payload();
    particle_texture_resource_rejects_missing_source_path();
    return 0;
}
