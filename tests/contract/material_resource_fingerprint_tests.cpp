#include "core/hash/stable_hash.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "resource/prefab_asset_resources.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::resource;

constexpr auto materialId = "00000000-0000-4000-8000-000000001001";
constexpr auto sourceId = "00000000-0000-4000-8000-000000001002";
constexpr auto shaderId = "00000000-0000-4000-8000-000000001003";
constexpr auto textureId = "00000000-0000-4000-8000-000000001004";
constexpr auto missingId = "00000000-0000-4000-8000-000000001005";

struct Mutation {
    const char* name;
    void (*apply)(MaterialResource&);
};

// Explicit schema inventory: adding a serialized field requires a mutation case.
const std::array<Mutation, 25> mutations{{
    {"name",
        [](auto& m) {
            m.name = "changed";
        }},
    {"sourcePath",
        [](auto& m) {
            m.sourcePath = "materials/changed";
        }},
    {"sourceResourceId",
        [](auto& m) {
            m.sourceResourceId = sourceId;
        }},
    {"shaderPackageId",
        [](auto& m) {
            m.shaderPackageId = shaderId;
        }},
    {"textureBindingCount",
        [](auto& m) {
            m.textureBindingCount = 1;
        }},
    {"propertyCount",
        [](auto& m) {
            m.propertyCount = 1;
        }},
    {"materialContractVersion",
        [](auto& m) {
            m.materialContractVersion = 2;
        }},
    {"surfaceModel",
        [](auto& m) {
            m.surfaceModel = "unsupported-test-surface";
        }},
    {"baseColorR",
        [](auto& m) {
            m.baseColorR = 0.25f;
        }},
    {"baseColorG",
        [](auto& m) {
            m.baseColorG = 0.25f;
        }},
    {"baseColorB",
        [](auto& m) {
            m.baseColorB = 0.25f;
        }},
    {"baseColorA",
        [](auto& m) {
            m.baseColorA = 0.25f;
        }},
    {"emissiveR",
        [](auto& m) {
            m.emissiveR = 0.5f;
        }},
    {"emissiveG",
        [](auto& m) {
            m.emissiveG = 0.5f;
        }},
    {"emissiveB",
        [](auto& m) {
            m.emissiveB = 0.5f;
        }},
    {"metallicFactor",
        [](auto& m) {
            m.metallicFactor = 0.25f;
        }},
    {"roughnessFactor",
        [](auto& m) {
            m.roughnessFactor = 0.25f;
        }},
    {"alphaMode",
        [](auto& m) {
            m.alphaMode = "MASK";
        }},
    {"alphaCutoff",
        [](auto& m) {
            m.alphaCutoff = 0.75f;
        }},
    {"doubleSided",
        [](auto& m) {
            m.doubleSided = true;
        }},
    {"baseColorTextureId",
        [](auto& m) {
            m.baseColorTextureId = textureId;
        }},
    {"normalTextureId",
        [](auto& m) {
            m.normalTextureId = textureId;
        }},
    {"metallicRoughnessTextureId",
        [](auto& m) {
            m.metallicRoughnessTextureId = textureId;
        }},
    {"occlusionTextureId",
        [](auto& m) {
            m.occlusionTextureId = textureId;
        }},
    {"emissiveTextureId",
        [](auto& m) {
            m.emissiveTextureId = textureId;
        }},
}};

std::uint64_t fingerprint(const MaterialResource& material)
{
    core::StableHash64 hash;
    const auto before = core::defaultCpuAllocator().stats();
    material.appendContentFingerprint(hash);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount);
    assert(before.deallocationCount == after.deallocationCount);
    return hash.value();
}

// Uses the actual package writer/reader; no Editor fixture or alternate codec.
class MaterialFixture final {
public:
    MaterialFixture()
    {
        directory = std::filesystem::temp_directory_path()
            / ("arp-material-fingerprint-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assert(std::filesystem::create_directory(directory));
        material.name = "baseline";
        material.sourcePath = "materials/baseline";
        material.materialContractVersion = kPbrMetallicRoughnessContractVersion;
        material.surfaceModel = kPbrMetallicRoughnessContract;
        material.alphaMode = "OPAQUE";
        desc.resourceId = materialId;
        desc.alias = "material/fingerprint";
        desc.displayName = "Fingerprint contract";
        desc.status = "Contract Complete";
        const auto entry = material.buildPackageEntry(desc);
        assert(entry.success);
        InternalAssetPackageDesc package;
        package.name = "Material fingerprint CPU contract";
        package.status = "Contract Complete";
        package.entries.push_back(entry.entry);
        assert(InternalAssetPackageWriter().write(package, directory).success);
        const auto read = InternalAssetPackageReader().read(directory);
        assert(read.success && read.package.entries.size() == 1);
        assert(read.package.entries.front().contentHash == entry.entry.contentHash);
        assert(read.package.entries.front().envelopeYaml == entry.entry.envelopeYaml);
    }

    ~MaterialFixture() { std::filesystem::remove_all(directory); }

    void load(ResourceManager& manager) const
    {
        assert(InternalAssetPackageReader().load(directory, manager).success);
        assert(manager.find<MaterialResource>(materialId).ready());
        // These are explicit metadata-only declarations under RequireDeclared,
        // not texture decoding, shader execution or dependency type validation.
        assert(manager.declareResource(sourceId, "ExternalAssetSourceResource").success);
        assert(manager.declareResource(shaderId, "ShaderPackageResource").success);
        assert(manager.declareResource(textureId, "TextureResource").success);
    }

    core::UniquePtr<ResourceBase> decoded(const MaterialResource& value) const
    {
        const auto entry = value.buildPackageEntry(desc);
        assert(entry.success);
        core::pmr::String diagnostic;
        auto resource = createPrefabResourceFromEnvelope(entry.entry.envelopeYaml, diagnostic);
        assert(resource && diagnostic.empty());
        return resource;
    }

    std::filesystem::path directory;
    MaterialResource material;
    AssetResourcePackageBuildDesc desc;
};

// The existing observer gates the manager's explicit retired-resource release.
class ReleaseGate final : public ResourceLifecycleObserver {
public:
    bool beforeResourceUnload(reflection::NameView) noexcept override { return allow; }

    bool beforeRetiredResourcesRelease() noexcept override { return allow; }

    bool allow = false;
};

void serializedCoverage()
{
    const auto fields = reflection::class_of<MaterialResource>()->get_all_properties();
    std::size_t serializedCount = 0;
    for (const auto& field : fields) {
        const auto& property = field.second.cast<const reflection::property&>();
        if (!property.check_hints(static_cast<std::uint8_t>(reflection::PropertyHint::Serialize)))
            continue;
        ++serializedCount;
        std::size_t matches = 0;
        for (const auto& mutation : mutations)
            matches += field.first == mutation.name;
        assert(matches == 1);
    }
    assert(serializedCount == mutations.size());
}

void reloadFields(const MaterialFixture& fixture)
{
    for (const auto& mutation : mutations) {
        ResourceManager manager;
        fixture.load(manager);
        const auto old = manager.find<MaterialResource>(materialId);
        const auto* retained = old.get();
        const auto oldHash = manager.recordSnapshot(materialId)->contentHash;
        const auto oldFingerprint = fingerprint(*retained);
        auto changed = *retained;
        mutation.apply(changed);
        const auto wanted = fingerprint(changed);
        assert(wanted != oldFingerprint);
        const auto result = manager.reloadResource(fixture.decoded(changed));
        if (!result.success)
            std::printf("FAIL field=%s diagnostic=%s\n", mutation.name, result.diagnostics.c_str());
        assert(result.success && !result.unchangedContent && result.telemetry.committed);
        const auto current = manager.find<MaterialResource>(materialId);
        assert(current.ready() && current.generation() == old.generation() + 1);
        assert(old.stale() && !old.ready() && !old.get());
        assert(manager.recordSnapshot(materialId)->contentHash != oldHash);
        assert(result.telemetry.oldContentHash == oldHash);
        assert(result.telemetry.newContentHash == manager.recordSnapshot(materialId)->contentHash);
        assert(fingerprint(*current.get()) == wanted);
        // Full serialization comparison independently checks the newly Ready values.
        assert(current.get()->buildPackageEntry(fixture.desc).entry.envelopeYaml
            == changed.buildPackageEntry(fixture.desc).entry.envelopeYaml);
        assert(manager.retainedResourceCount() == 1 && result.telemetry.retiredCount == 1);
        assert(fingerprint(*retained) == oldFingerprint);

        ReleaseGate gate;
        manager.addLifecycleObserver(gate);
        manager.releaseRetiredResources();
        assert(manager.retainedResourceCount() == 1 && fingerprint(*retained) == oldFingerprint);
        gate.allow = true;
        manager.releaseRetiredResources();
        assert(manager.retainedResourceCount() == 0);
        manager.removeLifecycleObserver(gate);
        // Never dereference the released payload pointer or outlive its manager.
        const auto unchanged = manager.reloadResource(fixture.decoded(*current.get()));
        assert(unchanged.success && unchanged.unchangedContent && !unchanged.telemetry.committed);
        assert(current.ready() && !current.stale() && manager.retainedResourceCount() == 0);
        assert(manager.recordSnapshot(materialId)->contentHash == result.telemetry.newContentHash);
        std::printf("PASS serialized field=%s generation=%llu->%llu\n",
            mutation.name,
            static_cast<unsigned long long>(old.generation()),
            static_cast<unsigned long long>(current.generation()));
    }
}

void valueBoundaries(const MaterialFixture& fixture)
{
    // Payload v1 byte contract: little-endian u64, float32 bits as u64,
    // UTF-8 strings with byte lengths; independent fixed schema probe.
    assert(fingerprint(fixture.material) == 0x57edbd21f8096bbeull);
    auto a = fixture.material;
    auto b = a;
    a.name = "ab";
    a.sourcePath = "c";
    b.name = "a";
    b.sourcePath = "bc";
    assert(fingerprint(a) != fingerprint(b));
    a.name.assign("a\0b", 3);
    b.name = "a";
    assert(fingerprint(a) != fingerprint(b));
    a = fixture.material;
    b = a;
    a.emissiveR = -0.0f;
    assert(std::signbit(a.emissiveR) && fingerprint(a) != fingerprint(b));
    a.textureBindingCount = -1;
    b.textureBindingCount = std::numeric_limits<int>::max();
    assert(fingerprint(a) != fingerprint(b));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    const auto expected = fingerprint(a);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(fingerprint(a) == expected);
    }
    assert(quota->liveBytes() == 0);

    // Fingerprinting is identity only. Existing domain validation owns validity.
    ShaderPackageResource shader;
    shader.materialInterfaceContract = kPbrMetallicRoughnessContract;
    shader.materialInterfaceVersion = kPbrMetallicRoughnessContractVersion;
    assert(validateMaterialContract(fixture.material, shader).valid());
    for (const float invalid : {std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
             -1.f,
             2.f}) {
        a = fixture.material;
        a.baseColorR = invalid;
        assert(fingerprint(a) != fingerprint(fixture.material));
        assert(fingerprint(a) == fingerprint(a));
        assert(validateMaterialContract(a, shader).diagnostic
            == MaterialContractDiagnostic::InvalidParameter);
    }
    std::puts(
        "PASS string boundaries/NUL, signed integers/zero, float bits, no core allocations; domain validity unchanged");
}

void invalidReload(const MaterialFixture& fixture)
{
    ResourceManager manager;
    fixture.load(manager);
    const auto old = manager.find<MaterialResource>(materialId);
    const auto oldGeneration = old.generation();
    const auto oldHash = manager.recordSnapshot(materialId)->contentHash;
    auto missing = fixture.material;
    missing.baseColorTextureId = missingId;
    const auto failed = manager.reloadResource(fixture.decoded(missing));
    assert(!failed.success && failed.telemetry.rolledBack && !failed.diagnostics.empty());
    const auto restored = manager.find<MaterialResource>(materialId);
    assert(restored.ready() && restored.generation() == oldGeneration
        && manager.recordSnapshot(materialId)->contentHash == oldHash);
    assert(manager.retainedResourceCount() == 0);
    assert(!manager.reloadResource({}).success);
    for (const auto type : {"", "TextureResource"}) {
        auto invalid = core::makeUnique<MaterialResource>(*restored.get());
        auto metadata = invalid->reference();
        metadata.type = type;
        invalid->configureMetadata(std::move(metadata), invalid->dependencies());
        assert(!manager.reloadResource(std::move(invalid)).success);
        assert(restored.ready() && manager.recordSnapshot(materialId)->contentHash == oldHash);
    }
    std::puts(
        "PASS missing dependency/null/wrong type reload rejected with old Ready generation/hash retained");
}

void ordinaryCycle(const MaterialFixture& fixture)
{
    ResourceManager manager;
    fixture.load(manager);
    auto unchanged = manager.find<MaterialResource>(materialId);
    const auto noOp = manager.reloadResource(fixture.decoded(*unchanged.get()));
    assert(noOp.success && noOp.unchangedContent && unchanged.ready());
    auto changed = *unchanged.get();
    changed.baseColorR = 0.25f;
    const auto result = manager.reloadResource(fixture.decoded(changed));
    assert(result.success && !result.unchangedContent && unchanged.stale());
    manager.releaseRetiredResources();
    assert(manager.retainedResourceCount() == 0);
    assert(manager.unload(materialId).success);
}

void smoke(const MaterialFixture& fixture)
{
    ordinaryCycle(fixture);
    const auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        ordinaryCycle(fixture);
        const auto now = core::defaultCpuAllocator().stats();
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS ordinary warmed 20 package/load/no-op/change/retire/unload/destroy cycles: core live bytes/count delta=0; no stress test");
}
} // namespace

int main(int argc, char** argv)
{
    registerPrefabAssetReflection();
    const MaterialFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        smoke(fixture);
    } else {
        serializedCoverage();
        reloadFields(fixture);
        valueBoundaries(fixture);
        invalidReload(fixture);
    }
    return 0;
}
