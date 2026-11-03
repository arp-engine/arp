#include "core/encoding/yaml_contract.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tools/ai_asset_generator/ai_asset_generator.h"
#include "tools/io/cold_tool_files.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace tools::ai_asset_generator;
constexpr auto budget = PromptManifestLimits::defaultDocumentBytes;

void roundTrip()
{
    auto manifest = buildProductionDemoPromptManifest();
    assert(manifest.assets.size() == 6 && manifest.assets.front().name == "cliff_rock_cluster"
        && manifest.assets.back().file == "hero_proxy.glb");
    manifest.outputDirectory = u8"目录: \"quoted\" & #*\nsecond\tline";
    manifest.assets[0].name = "null";
    manifest.assets[0].prompt = u8"Draw: \"ARP\" & typed metadata # * 'single'\n中文\tend";
    const auto encoded = PromptManifestCodec::encode(manifest, budget);
    if (!encoded.success)
        std::fprintf(stderr, "%s\n", encoded.diagnostics.c_str());
    assert(encoded.success);
    assert(encoded.yaml.size() <= budget);
    const auto decoded = PromptManifestCodec::decode(encoded.yaml, budget);
    assert(decoded.success && decoded.manifest == manifest);
    // Copying owned descriptors cannot silently select the process PMR default.
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    try {
        const auto copy = decoded.manifest;
        assert(copy == manifest);
        const auto again = PromptManifestCodec::encode(copy, budget);
        assert(again.success);
        const auto resultCopy = again;
        assert(resultCopy.yaml == again.yaml
            && resultCopy.yaml.get_allocator().resource() == &core::defaultCpuMemoryResource());
        Options options;
        options.outputDirectory = manifest.outputDirectory;
        const auto optionsCopy = options;
        assert(optionsCopy.outputDirectory == options.outputDirectory
            && optionsCopy.outputDirectory.get_allocator().resource()
                == &core::defaultCpuMemoryResource());
        assert(copy.assets.get_allocator().resource() == &core::defaultCpuMemoryResource());
        for (const auto& asset : copy.assets)
            assert(asset.prompt.get_allocator().resource() == &core::defaultCpuMemoryResource());
    } catch (...) {
        std::pmr::set_default_resource(previous);
        throw;
    }
    std::pmr::set_default_resource(previous);
}

void reject()
{
    auto original = buildProductionDemoPromptManifest();
    const auto encoded = PromptManifestCodec::encode(original, budget);
    assert(encoded.success && !PromptManifestCodec::encode(original, 1).success);
    assert(!PromptManifestCodec::encode(original, 0).success);
    assert(!PromptManifestCodec::decode(encoded.yaml, encoded.yaml.size() - 1).success);
    auto invalid = original;
    invalid.assets[1].name = invalid.assets[0].name;
    assert(!PromptManifestCodec::encode(invalid, budget).success);
    invalid = original;
    invalid.assets[1].file = invalid.assets[0].file;
    assert(!PromptManifestCodec::encode(invalid, budget).success);
    invalid.assets[1].file = "../outside.glb";
    assert(!PromptManifestCodec::encode(invalid, budget).success);
    invalid = original;
    invalid.assets.front().prompt.clear();
    assert(!PromptManifestCodec::encode(invalid, budget).success);
    invalid.assets.front().prompt.assign(PromptManifestLimits::maxFieldBytes + 1, 'x');
    assert(!PromptManifestCodec::encode(invalid, budget).success);
    invalid.assets.front().prompt = "\xff";
    assert(!PromptManifestCodec::encode(invalid, budget).success);
    invalid.assets.front().prompt.assign("a\0b", 3);
    assert(!PromptManifestCodec::encode(invalid, budget).success);
    invalid = original;
    while (invalid.assets.size() <= PromptManifestLimits::maxAssets)
        invalid.assets.push_back(original.assets.front());
    assert(!PromptManifestCodec::encode(invalid, budget).success);

    using Yaml = core::YamlContract;
    auto document = Yaml::parseSingleDocument(encoded.yaml);
    document["contractVersion"] = 2;
    assert(!PromptManifestCodec::decode(Yaml::serialize(document), budget).success);
    document["contractVersion"] = 1;
    document["extra"] = "unknown";
    assert(!PromptManifestCodec::decode(Yaml::serialize(document), budget).success);
    assert(!PromptManifestCodec::decode("type: [", budget).success);
    assert(!PromptManifestCodec::decode("\xff", budget).success);
    assert(!PromptManifestCodec::decode(core::StringView("a\0b", 3), budget).success);
    const char* args[]{"tool", "--max-document-bytes", "bad", "--max-document-bytes", "4096"};
    assert(!parseOptions(5, args).valid);
    assert(!PromptManifestApplication{}.run(Options{}).success);
}

void publish(const std::filesystem::path& directory)
{
    Options options;
    options.manifestOnly = true;
    options.outputDirectory = core::FilePath::utf8(directory);
    const auto saved = PromptManifestApplication{}.run(options);
    if (!saved.success)
        std::fprintf(stderr, "%s\n", saved.diagnostics.c_str());
    assert(saved.success && saved.saved.commitState == core::io::FileCommitState::Committed);
    tools::io::ColdToolFiles files(budget, 1);
    core::io::FileTextReadRequest read;
    read.file.contractVersion = 1;
    read.file.mount = files.mountDirectory("result", directory);
    read.file.pathScope = core::io::FilePathScope::MountedResource;
    read.file.path = "asset_manifest.yaml";
    read.file.maxBytes = read.text.maxOutputBytes = budget;
    const auto loaded = files.readText(read);
    files.retire();
    assert(loaded.status() == core::io::FileIoStatus::Success);
    const auto decoded = PromptManifestCodec::decode(loaded.decoded.text, budget);
    assert(decoded.success && decoded.manifest.outputDirectory == options.outputDirectory
        && decoded.manifest.assets.size() == 6);
}
} // namespace

int main(int argc, char** argv)
{
    roundTrip();
    unified_io_fixture::Workspace workspace;
#if defined(_WIN32)
    publish(workspace.root);
#endif
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i) {
            roundTrip();
#if defined(_WIN32)
            publish(workspace.root);
#endif
        }
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Prompt manifest core %llu/%llu -> %llu/%llu, peak=%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount),
            (unsigned long long)after.peakBytes);
    } else
        reject();
    std::puts("PASS offline prompt YAML, typed round-trip, original FileService publication");
}
