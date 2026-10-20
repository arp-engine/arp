#include "core/cpu_allocator.h"
#include "reflection/reflection.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <string>

namespace {

constexpr const char* kMaterialId = "4b577590-724f-4d74-9fef-6ca8a7ad8f4a";
constexpr const char* kTextureId = "dd1540d3-e170-4948-8daa-f98e94e54ab7";

struct SmokeMaterialResource {
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String baseColorTexture{&arp::core::defaultCpuMemoryResource()};
};

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

void registerSmokeResourceType()
{
    arp::reflection::registration::class_<SmokeMaterialResource>("SmokeMaterialResource")
        .construct<>()
        .property("name", &SmokeMaterialResource::name)
        .property("baseColorTexture", &SmokeMaterialResource::baseColorTexture);
}

arp::core::pmr::String indentPayload(const arp::core::pmr::String& payload)
{
    arp::core::pmr::String indented(&arp::core::defaultCpuMemoryResource());
    indented += "  ";
    for (const char ch : payload) {
        indented.push_back(ch);
        if (ch == '\n') {
            indented += "  ";
        }
    }
    return indented;
}

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    registerSmokeResourceType();

    arp::core::pmr::String payload("type: SmokeMaterialResource\n"
                                   "fields:\n"
                                   "  name: SmokeRock\n"
                                   "  baseColorTexture: texture/smoke_base\n",
        &arp::core::defaultCpuMemoryResource());
    const auto hash = arp::resource::computeResourcePayloadHash(payload);
    assert(hash.success);

    arp::core::pmr::String envelope(&arp::core::defaultCpuMemoryResource());
    envelope += "format: ArpRuntimeResource\n";
    envelope += "version: 1\n";
    envelope += "resourceId: ";
    envelope += kMaterialId;
    envelope += "\n";
    envelope += "resourceType: SmokeMaterialResource\n";
    envelope += "alias: material/smoke_rock\n";
    envelope += "displayName: SmokeRock\n";
    envelope += "status: Prototype Complete\n";
    envelope += "contentHash: ";
    envelope += std::to_string(hash.hash).c_str();
    envelope += "\n";
    envelope += "dependencies:\n";
    envelope += "  - id: ";
    envelope += kTextureId;
    envelope += "\n";
    envelope += "    usage: texture/baseColor\n";
    envelope += "payload:\n";
    envelope += indentPayload(payload);

    arp::resource::ResourceManager manager;
    assert(manager.declareResource(kTextureId, "SmokeTextureResource").success);
    const auto loaded = arp::resource::compatibility::loadFromEnvelope(manager, envelope);
    assert(loaded.success);

    // Reflection fallback removed from ResourceHandle<T>; legacy envelope load
    // no longer supports find<T>() for reflected payload ownership.
    const auto handle = manager.find<SmokeMaterialResource>(kMaterialId);
    assert(!handle.ready());
    assert(handle.get() == nullptr);
    assert(manager.stateOf(kMaterialId) == arp::resource::ResourceState::Ready);
    assert(manager.unload(kMaterialId).success);
    assert(manager.stateOf(kMaterialId) == arp::resource::ResourceState::Unloaded);

    const auto broken = arp::resource::compatibility::loadFromEnvelope(manager,
        "format: ArpRuntimeResource\n"
        "version: 1\n"
        "resourceId: f11e53b4-a063-4795-8eee-861dff076c62\n"
        "payload:\n"
        "  type: SmokeMaterialResource\n"
        "  fields: {}\n");
    assert(!broken.success);
    assertContains(broken.diagnostics, "missing required resourceType");

    return 0;
}
