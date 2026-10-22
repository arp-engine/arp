#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <string>

using namespace arp::reflection;

namespace {

constexpr const char* kMaterialId = "7f4b9d5a-1f63-4f4c-9a45-3c1a9e7e92b1";
constexpr const char* kSecondMaterialId = "3adf41a8-fd2b-4662-b3d6-861d00f8fd4f";
constexpr const char* kTextureId = "9e3f0b60-93f7-42b8-88e7-32e0c9f0bc40";

struct TestTextureResource {
    arp::core::pmr::String path{&arp::core::defaultCpuMemoryResource()};
};

struct TestMaterialResource {
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String baseColorTexture{&arp::core::defaultCpuMemoryResource()};
};

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

void registerTypes()
{
    registration::class_<TestTextureResource>("UuidTestTextureResource")
        .construct<>()
        .property("path", &TestTextureResource::path);

    registration::class_<TestMaterialResource>("UuidTestMaterialResource")
        .construct<>()
        .property("name", &TestMaterialResource::name)
        .property("baseColorTexture", &TestMaterialResource::baseColorTexture);
}

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
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

arp::core::pmr::String materialPayload(const char* name = "Male000")
{
    arp::core::pmr::String payload(&arp::core::defaultCpuMemoryResource());
    payload += "type: UuidTestMaterialResource\n";
    payload += "fields:\n";
    payload += "  name: ";
    payload += name;
    payload += "\n";
    payload += "  baseColorTexture: character/male000/base\n";
    return payload;
}

arp::core::pmr::String makeEnvelope(const char* resourceId,
    const char* alias,
    const arp::core::pmr::String& payload,
    std::uint32_t hash,
    const char* dependencyId = kTextureId)
{
    arp::core::pmr::String envelope(&arp::core::defaultCpuMemoryResource());
    envelope += "format: ArpRuntimeResource\n";
    envelope += "version: 1\n";
    envelope += "resourceId: ";
    envelope += resourceId;
    envelope += "\nresourceType: UuidTestMaterialResource\n";
    envelope += "alias: ";
    envelope += alias;
    envelope += "\n";
    envelope += "displayName: Male000\n";
    envelope += "status: Contract Complete\n";
    envelope += "contentHash: ";
    envelope += std::to_string(hash).c_str();
    envelope += "\n";
    if (dependencyId != nullptr) {
        envelope += "dependencies:\n";
        envelope += "  - id: ";
        envelope += dependencyId;
        envelope += "\n";
        envelope += "    usage: texture/baseColor\n";
    } else {
        envelope += "dependencies: []\n";
    }
    envelope += "payload:\n";
    envelope += indentPayload(payload);
    return envelope;
}

arp::core::pmr::String validEnvelope(const char* resourceId = kMaterialId,
    const char* alias = "character/male000",
    const char* dependencyId = kTextureId)
{
    const auto payload = materialPayload();
    const auto hash = arp::resource::computeResourcePayloadHash(payload);
    assert(hash.success);
    return makeEnvelope(resourceId, alias, payload, hash.hash, dependencyId);
}

} // namespace

int main()
{
    registerTypes();

    assert(arp::resource::isResourceUuid(kMaterialId));
    assert(!arp::resource::isResourceUuid("character/male000"));
    assert(!arp::resource::isResourceUuid("7f4b9d5a-1f63-4f4c-9a45-3c1a9e7e92b"));

    {
        arp::resource::ResourceManager manager;
        assert(manager.declareResource(kTextureId, "UuidTestTextureResource").success);
        const auto loaded =
            arp::resource::compatibility::loadFromEnvelope(manager, validEnvelope());
        assert(loaded.success);
        assert(loaded.resourceId == text(kMaterialId));

        const auto resolved = manager.resolveAlias("character/male000");
        assert(resolved.success);
        assert(resolved.resourceId == text(kMaterialId));

        // Reflection fallback removed from ResourceHandle<T>; legacy envelope load
        // no longer supports find<T>() for reflected payload ownership.
        const auto handle = manager.findByAlias<TestMaterialResource>("character/male000");
        assert(!handle.ready());
        assert(handle.get() == nullptr);
    }

    {
        arp::resource::ResourceManager manager;
        const auto declared =
            manager.declareResource("character/male000", "UuidTestMaterialResource");
        assert(!declared.success);
        assertContains(declared.diagnostics, "resource id must be UUID");
    }

    {
        arp::resource::ResourceManager manager;
        const auto dependency = manager.declareResource(kTextureId, "UuidTestTextureResource");
        assert(dependency.success);
        const auto loaded = arp::resource::compatibility::loadFromEnvelope(
            manager, validEnvelope("character/male000"));
        assert(!loaded.success);
        assertContains(loaded.diagnostics, "resource id must be UUID");
    }

    {
        arp::resource::ResourceManager manager;
        const auto loaded = arp::resource::compatibility::loadFromEnvelope(
            manager, validEnvelope(kMaterialId, "character/male000", "texture/base"));
        assert(!loaded.success);
        assertContains(loaded.diagnostics, "dependency id must be UUID");
    }

    {
        arp::resource::ResourceManager manager;
        assert(manager.declareResource(kTextureId, "UuidTestTextureResource").success);
        assert(arp::resource::compatibility::loadFromEnvelope(manager, validEnvelope()).success);
        const auto duplicate =
            arp::resource::compatibility::loadFromEnvelope(manager, validEnvelope());
        assert(!duplicate.success);
        assertContains(duplicate.diagnostics, "duplicate resource id");
    }

    {
        arp::resource::ResourceManager manager;
        assert(manager.declareResource(kTextureId, "UuidTestTextureResource").success);
        assert(arp::resource::compatibility::loadFromEnvelope(manager, validEnvelope()).success);
        const auto duplicateAlias = arp::resource::compatibility::loadFromEnvelope(
            manager, validEnvelope(kSecondMaterialId, "character/male000", kTextureId));
        assert(!duplicateAlias.success);
        assertContains(duplicateAlias.diagnostics, "duplicate alias");
    }

    {
        arp::resource::ResourceManager manager;
        const auto missing = manager.resolveAlias("character/missing");
        assert(!missing.success);
        assertContains(missing.diagnostics, "alias not found");
        const auto handle = manager.findByAlias<TestMaterialResource>("character/missing");
        assert(!handle.ready());
        assertContains(handle.diagnostics(), "alias not found");
    }

    return 0;
}
