#include "assets/image_decoder.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "editor/ui/editor_atlas_layout.h"
#include "editor/ui/editor_ui_assets.h"
#include "editor/ui/editor_ui_content.h"
#include "editor/workspace/editor_shell.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using Limits = editor::EditorUiContentLimits;
using Assets = editor::EditorUiAssets;

// Exercise the real cold builder. The original PNG and wrong-extent PNG are
// existing repository assets, never generated substitute production resources.
class LegacyAtlasFixture final {
public:
    LegacyAtlasFixture()
    {
        original =
            core::readBinaryFile(std::filesystem::path(ARP_LEGACY_ATLAS_ROOT) / "editor_atlas.png",
                Limits::maxAtlasBytes);
        assert(original.success && !original.bytes.empty());
        restore();
    }

    void restore()
    {
        files.write("editor_atlas.png",
            {reinterpret_cast<const char*>(original.bytes.data()), original.bytes.size()});
    }

    Assets build(editor::EditorShell& shell)
    {
        const core::pmr::Vector<renderer::ui::UiDrawCommand> commands;
        return Assets::build(shell, files.root, commands);
    }

    core::pmr::String signature()
    {
        editor::EditorShell shell;
        auto candidate = build(shell);
        if (!candidate.ok())
            std::fprintf(stderr, "%s: %s\n", candidate.stage(), candidate.diagnostic().c_str());
        assert(candidate.ok());
        const auto& atlas = candidate.atlas().atlas();
        assert(atlas.extent.width == editor::editor_atlas::kWidth
            && atlas.extent.height == editor::editor_atlas::kHeight && !atlas.srgb);
        using Y = core::YamlContract;
        auto record = Y::Node::mapping();
        record["type"] = "LegacyEditorAtlasCpuBytes";
        record["contractVersion"] = 1;
        record["pngSha256"] = core::Sha256::hexDigest(
            {reinterpret_cast<const char*>(original.bytes.data()), original.bytes.size()})
                                  .c_str();
        record["atlasSha256"] = core::Sha256::hexDigest(
            {reinterpret_cast<const char*>(atlas.rgba8.data()), atlas.rgba8.size()})
                                    .c_str();
        record["width"] = atlas.extent.width;
        record["height"] = atlas.extent.height;
        record["bytes"] = static_cast<std::int64_t>(atlas.rgba8.size());
        record["srgb"] = atlas.srgb;
        record["resourceId"] = atlas.resourceId.c_str();
        return Y::serialize(record);
    }

    void reject(const char* stage)
    {
        editor::EditorShell shell;
        const auto version = shell.stateVersion();
        auto failed = build(shell);
        assert(!failed.ok() && std::string_view(failed.stage()) == stage
            && !failed.diagnostic().empty() && shell.stateVersion() == version);
    }

    unified_io_fixture::Workspace files;
    core::BinaryFileReadResult original;
};

void contract(LegacyAtlasFixture& fixture)
{
    const auto expected = fixture.signature();
    const auto atlas = fixture.files.root / "editor_atlas.png";
    assert(std::filesystem::remove(atlas));
    fixture.reject("ATLAS-FILE");
    fixture.files.write("editor_atlas.png", "");
    fixture.reject("ATLAS-DECODE");
    fixture.files.write("editor_atlas.png", "invalid PNG");
    fixture.reject("ATLAS-DECODE");
    const auto small = core::readBinaryFile(ARP_LEGACY_ATLAS_WRONG_IMAGE, Limits::maxAtlasBytes);
    assert(small.success);
    fixture.files.write("editor_atlas.png",
        {reinterpret_cast<const char*>(small.bytes.data()), small.bytes.size()});
    fixture.reject("ATLAS-CONTRACT");
    std::filesystem::resize_file(atlas, Limits::maxAtlasBytes + 1);
    fixture.reject("ATLAS-FILE");
    fixture.restore();
    assert(fixture.signature() == expected);
    std::puts(
        "PASS original Legacy atlas PNG/combined bytes, missing/empty/corrupt/extent/budget rejection and repair");
}

void ordinary(LegacyAtlasFixture& fixture)
{
    // Destroy the complete builder result and Shell before every observation.
    {
        const auto warm = fixture.signature();
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        {
            const auto value = fixture.signature();
        }
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::printf("Legacy atlas ordinary20: core bytes=%llu live=%llu delta=0\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount));
}
} // namespace

int main(int argc, char** argv)
{
    LegacyAtlasFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--baseline") {
        std::printf("%s", fixture.signature().c_str());
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        ordinary(fixture);
    else {
        assert(argc == 1);
        contract(fixture);
    }
}
