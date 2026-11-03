#include "core/diagnostics/diagnostic_error.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/web/browser_workspace.h"

#include <cassert>
#include <chrono>
#include <cstdio>

using namespace arp;

namespace {
class ArchiveFixture final {
public:
    explicit ArchiveFixture(const core::ByteBuffer& manifest)
    {
        const auto destination = std::filesystem::temp_directory_path()
            / ("arp-browser-archive-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assert(directory_.begin(destination));
        std::filesystem::create_directory(root() / "assets");
        write("project.arp.yaml", manifest);
        write("assets/payload.bin", {0, 65, 13, 10, 255});
        write("empty.bin", {});
    }

    const std::filesystem::path& root() const noexcept { return directory_.path(); }

    void check(std::string_view encoded, const core::ByteBuffer& manifest) const
    {
        const auto archive = editor::BrowserWorkspace::decode(encoded);
        assert(archive.scene.empty() && archive.files.size() == 3);
        assert(archive.files[0].path == "assets/payload.bin"
            && archive.files[0].bytes == core::ByteBuffer({0, 65, 13, 10, 255}));
        assert(archive.files[1].path == "empty.bin" && archive.files[1].bytes.empty());
        assert(archive.files[2].path == "project.arp.yaml" && archive.files[2].bytes == manifest);
    }

private:
    void write(std::string_view relative, const core::ByteBuffer& bytes)
    {
        assert(core::io::replaceFileAtomically(
            core::FilePath::utf8(root() / std::filesystem::path(relative)), bytes)
                .success);
    }

    core::io::StagedDirectory directory_;
};

void encodeContract(const core::ByteBuffer& manifest, unsigned cycles)
{
    ArchiveFixture fixture(manifest);
    const auto encoded = editor::BrowserWorkspace::encode(fixture.root(), "");
    fixture.check(encoded, manifest);
    std::printf("Archive original bytes: %zu SHA256=%s\n",
        encoded.size(),
        core::Sha256::hexDigest(encoded).c_str());
    const auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < cycles; ++i) {
        {
            const auto next = editor::BrowserWorkspace::encode(fixture.root(), "");
            assert(next == encoded);
            fixture.check(next, manifest);
        }
        const auto current = core::defaultCpuAllocator().stats();
        assert(current.liveBytes == baseline.liveBytes);
        assert(current.allocationCount - current.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::printf("Archive encode/decode %u cycles: core bytes=%llu live=%llu delta=0\n",
        cycles,
        static_cast<unsigned long long>(baseline.liveBytes),
        static_cast<unsigned long long>(baseline.allocationCount - baseline.deallocationCount));
}
} // namespace

int main(int argc, char** argv)
{
    using Y = core::YamlContract;
    auto file = core::readBinaryFile(
        std::filesystem::path(ARP_SOURCE_DIR) / "editor/web/project/project.arp.yaml");
    assert(file.success);
    auto document = Y::Node::mapping();
    document["type"] = "arp.editor.workspace";
    document["contractVersion"] = 1;
    document["scene"] = "";
    auto entry = Y::Node::mapping();
    entry["path"] = "project.arp.yaml";
    const auto base64 = core::Base64::encode(file.bytes);
    entry["base64"] = Y::Node::string_type(base64.begin(), base64.end());
    document["files"] = Y::Node::sequence();
    document["files"].get_value_ref<Y::Node::sequence_type&>().push_back(entry);
    auto source = Y::serialize(document);
    auto validate = [&](const Y::Node& node, bool accepted) {
        bool success = false;
        try {
            auto archive = editor::BrowserWorkspace::decode(Y::serialize(node));
            success = archive.files.size() == 1;
        } catch (const core::DiagnosticError&) {
        }
        assert(success == accepted);
    };
    validate(document, true);
    auto invalid = document;
    invalid["contractVersion"] = 2;
    validate(invalid, false);
    invalid = document;
    invalid["files"][0]["path"] = "../project.arp.yaml";
    validate(invalid, false);
    invalid = document;
    invalid["files"].get_value_ref<Y::Node::sequence_type&>().push_back(entry);
    validate(invalid, false);
    invalid = document;
    invalid["files"][0]["base64"] = "invalid!";
    validate(invalid, false);
    invalid = document;
    invalid["scene"] = "absent.yaml";
    validate(invalid, false);
    {
        auto warm = editor::BrowserWorkspace::decode(source);
    }
    const auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        {
            auto archive = editor::BrowserWorkspace::decode(source);
            assert(archive.files[0].bytes == file.bytes);
        }
        const auto current = core::defaultCpuAllocator().stats();
        assert(current.liveBytes == baseline.liveBytes);
        assert(current.allocationCount - current.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts("PASS: strict workspace YAML/path/version and 20 ordinary core lifecycles");
    encodeContract(file.bytes, argc == 2 && std::string_view(argv[1]) == "--smoke" ? 20 : 1);
}
