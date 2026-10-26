#include "core/io/file_io.h"
#include "editor/project/project_resource_locations.h"
#include "editor/tests/fixtures/project_package_fixture.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace {
using namespace arp;
using namespace arp::editor;
using Package = tests::ProjectPackageFixture;

class Fixture final {
public:
    Fixture()
    {
        root = std::filesystem::temp_directory_path()
            / ("arp-definition-locations-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        root = std::filesystem::canonical(root);
        package = Package::write(root / "Assets/Base.arpkg");
        assert(resource::InternalAssetPackageReader()
                .load(root / "Assets/Base.arpkg", *owner)
                .success);
        const auto* diagnostic = capture(locations);
        if (diagnostic)
            std::fprintf(stderr, "capture: %s\n", diagnostic);
        assert(!diagnostic);
    }

    ~Fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        assert(!error);
    }

    const char* capture(ProjectResourceLocations& output) const
    {
        return ProjectResourceLocations::capture(root, packages, owner, output);
    }

    static void write(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        assert(output.good());
    }

    std::filesystem::path root;
    resource::InternalAssetPackageDesc package;
    core::SharedPtr<resource::ResourceManager> owner =
        core::makeShared<resource::ResourceManager>();
    core::pmr::Vector<core::pmr::String> packages{"Assets/Base.arpkg"};
    ProjectResourceLocations locations;
};

void contracts(Fixture& fixture)
{
    std::filesystem::path output("sentinel");
    assert(fixture.locations.size() == fixture.package.entries.size());
    for (const auto& entry : fixture.package.entries) {
        assert(fixture.locations.contains(entry.resourceId, entry.resourceType));
        assert(!fixture.locations.resolve(
            entry.resourceId, entry.resourceType, *fixture.owner, output));
        assert(output
            == fixture.root / "Assets/Base.arpkg" / std::filesystem::u8path(entry.envelopePath));
        ProjectResourceLocations::Definition definition;
        assert(!fixture.locations.identify(output, *fixture.owner, definition));
        assert(definition.resourceId == entry.resourceId
            && definition.resourceType == entry.resourceType);
    }
    const auto& entry = fixture.package.entries.front();
    output = "sentinel";
    assert(fixture.locations.resolve(entry.resourceId, "WrongType", *fixture.owner, output));
    assert(output == "sentinel");
    assert(fixture.locations.resolve("unknown", entry.resourceType, *fixture.owner, output));
    resource::ResourceManager differentOwner;
    assert(fixture.locations.resolve(entry.resourceId, entry.resourceType, differentOwner, output));
    assert(output == "sentinel");
    const auto envelope =
        fixture.root / "Assets/Base.arpkg" / std::filesystem::u8path(entry.envelopePath);
    const auto bytes = core::readTextFile(envelope, ProjectResourceLocations::budget.maxFileBytes);
    assert(bytes.success);
    const auto manifest = fixture.root / "Assets/Base.arpkg/manifest.arpkg.yaml";
    const auto manifestBytes =
        core::readTextFile(manifest, ProjectResourceLocations::budget.maxFileBytes);
    assert(manifestBytes.success);
    Fixture::write(envelope, bytes.text + "\n# external edit\n");
    assert(fixture.locations.resolve(entry.resourceId, entry.resourceType, *fixture.owner, output));
    assert(output == "sentinel");
    Fixture::write(envelope, bytes.text);
    ProjectResourceLocations::Definition definition;
    definition.resourceId = "unchanged resource identity";
    definition.resourceType = "unchanged resource type";
    const auto rejectIdentity = [&](const auto& path, const auto& owner) {
        assert(fixture.locations.identify(path, owner, definition));
        assert(definition.resourceId == "unchanged resource identity"
            && definition.resourceType == "unchanged resource type");
    };
    // Only the explicit envelope is a drag source, never a raw file, package,
    // manifest, lexical alias, relative path or path outside the project.
    Fixture::write(fixture.root / "raw.arpres.yaml", bytes.text);
    for (const auto& path : {fixture.root,
             fixture.root / "Assets/Base.arpkg",
             fixture.root / "raw.arpres.yaml",
             fixture.root / "absent.arpres.yaml",
             fixture.root / "Assets/Base.arpkg/manifest.arpkg.yaml",
             envelope.parent_path() / "." / envelope.filename(),
             fixture.root / ".." / fixture.root.filename() / "raw.arpres.yaml",
             fixture.root.parent_path() / "outside",
             envelope.lexically_relative(fixture.root)})
        rejectIdentity(path, *fixture.owner);
    rejectIdentity(envelope, differentOwner);
    Fixture::write(envelope, bytes.text + "\n# changed\n");
    rejectIdentity(envelope, *fixture.owner);
    Fixture::write(envelope, bytes.text);
    Fixture::write(manifest, manifestBytes.text + "\n# changed\n");
    rejectIdentity(envelope, *fixture.owner);
    Fixture::write(manifest, manifestBytes.text);
    std::filesystem::remove(envelope);
    rejectIdentity(envelope, *fixture.owner);
    Fixture::write(envelope, bytes.text);
    assert(
        !fixture.locations.resolve(entry.resourceId, entry.resourceType, *fixture.owner, output));
    Fixture::write(manifest, manifestBytes.text + "\n# external edit\n");
    output = "sentinel";
    assert(fixture.locations.resolve(entry.resourceId, entry.resourceType, *fixture.owner, output));
    assert(output == "sentinel");
    Fixture::write(manifest, manifestBytes.text);
    std::filesystem::remove(envelope);
    assert(fixture.locations.resolve(entry.resourceId, entry.resourceType, *fixture.owner, output));
    assert(output == "sentinel");
    Fixture::write(envelope, bytes.text);
    auto duplicate = fixture.packages;
    duplicate.push_back(duplicate.front());
    assert(ProjectResourceLocations::capture(
        fixture.root, duplicate, fixture.owner, fixture.locations));
    assert(fixture.locations.size() == fixture.package.entries.size());
    duplicate = {"../outside"};
    assert(ProjectResourceLocations::capture(
        fixture.root, duplicate, fixture.owner, fixture.locations));
    assert(fixture.locations.size() == fixture.package.entries.size());
    for (const auto quotaBytes : {1u, 1024u, 8192u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(quotaBytes);
        {
            auto reverse = definition;
            core::CpuMemoryQuotaScope scope(quota);
            output = "sentinel";
            assert(fixture.capture(fixture.locations));
            assert(fixture.locations.size() == fixture.package.entries.size());
            const auto* failure = fixture.locations.resolve(
                entry.resourceId, entry.resourceType, *fixture.owner, output);
            assert(!failure || output == "sentinel");
            const auto* reverseFailure =
                fixture.locations.identify(envelope, *fixture.owner, reverse);
            if (reverseFailure)
                assert(reverse.resourceId == "unchanged resource identity"
                    && reverse.resourceType == "unchanged resource type");
            else
                assert(reverse.resourceId == entry.resourceId
                    && reverse.resourceType == entry.resourceType);
        }
        assert(quota->liveBytes() == 0);
    }
    assert(fixture.owner->unload(entry.resourceId).success);
    rejectIdentity(envelope, *fixture.owner);
    std::puts(
        "PASS all explicit package entries, exact file, owner/type, changed/deleted files, duplicate/escape and allocation rollback");
}

void materialReloadContracts()
{
    Fixture fixture;
    for (const auto& entry : fixture.package.entries) {
        if (entry.resourceType != "MaterialResource")
            continue;
        const auto path =
            fixture.root / "Assets/Base.arpkg" / std::filesystem::u8path(entry.envelopePath);
        const auto bytes = core::readTextFile(path, ProjectResourceLocations::budget.maxFileBytes);
        assert(bytes.success);
        std::filesystem::path output;
        assert(!fixture.locations.resolve(
            entry.resourceId, entry.resourceType, *fixture.owner, output));
        const auto old = fixture.owner->find<resource::MaterialResource>(entry.resourceId);
        assert(old.ready());
        auto identical = core::makeUnique<resource::MaterialResource>(*old.get());
        const auto noOp = fixture.owner->reloadResource(std::move(identical));
        assert(noOp.success && noOp.unchangedContent && old.ready());
        assert(!fixture.locations.resolve(
            entry.resourceId, entry.resourceType, *fixture.owner, output));

        auto changed = core::makeUnique<resource::MaterialResource>(*old.get());
        const auto green = changed->baseColorG == 0.f ? 1.f : 0.f;
        changed->baseColorG = green;
        const auto result = fixture.owner->reloadResource(std::move(changed));
        const auto current = fixture.owner->find<resource::MaterialResource>(entry.resourceId);
        assert(result.success && !result.unchangedContent && old.stale() && current.ready());
        assert(current.generation() == old.generation() + 1 && current.get()->baseColorG == green);
        assert(result.telemetry.oldContentHash != result.telemetry.newContentHash);
        // Disk SHA and runtime payload identity are deliberately separate domains.
        assert(core::readTextFile(path, ProjectResourceLocations::budget.maxFileBytes).text
            == bytes.text);
        output = "sentinel";
        const auto* rejected =
            fixture.locations.resolve(entry.resourceId, entry.resourceType, *fixture.owner, output);
        assert(
            rejected && std::string_view(rejected) == "EDITOR-RESOURCE-LOCATION-METADATA-CHANGED");
        assert(output == "sentinel");
        ProjectResourceLocations::Definition definition;
        definition.resourceId = "sentinel";
        assert(fixture.locations.identify(path, *fixture.owner, definition));
        assert(definition.resourceId == "sentinel");
        fixture.owner->releaseRetiredResources();
        assert(fixture.owner->retainedResourceCount() == 0);
        std::puts(
            "PASS actual material color reload invalidates old definition/reverse mappings; identical payload preserves mapping; envelope bytes unchanged");
        return;
    }
    assert(false && "explicit fixture has no material entry");
}

void smoke(Fixture& fixture)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            ProjectResourceLocations locations;
            assert(!fixture.capture(locations));
            std::filesystem::path output;
            for (const auto& entry : fixture.package.entries) {
                assert(!locations.resolve(
                    entry.resourceId, entry.resourceType, *fixture.owner, output));
                ProjectResourceLocations::Definition definition;
                assert(!locations.identify(output, *fixture.owner, definition));
                assert(definition.resourceId == entry.resourceId
                    && definition.resourceType == entry.resourceType);
            }
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary capture/resolve/identify/destroy cycles; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke(fixture);
    else {
        contracts(fixture);
        materialReloadContracts();
    }
}
