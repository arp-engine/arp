#include "core/io/file_io.h"
#include "examples/survivors/codegen_io.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
constexpr std::array<const char*, 3> names{
    "script_aot_v5.gen.h", "survivors.gen.cpp", "survivors.manifest.yaml"};
constexpr std::array<core::StringView, 3> contents{"header\n", "source\r\n", "type: Test\n"};

void verifyOutputs(const std::filesystem::path& directory)
{
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto bytes = core::readBinaryFile(directory / names[i], 4096);
        assert(bytes.success);
        core::pmr::String expected(&core::defaultCpuMemoryResource());
        for (const auto c : contents[i]) {
#if defined(_WIN32)
            if (c == '\n')
                expected += '\r';
#endif
            expected += c;
        }
        assert(bytes.bytes.size() == expected.size());
        assert(std::memcmp(bytes.bytes.data(), expected.data(), expected.size()) == 0);
    }
}

void cycle(const std::filesystem::path& output)
{
    survivors::CodegenIo io;
    core::pmr::String diagnostic(&core::defaultCpuMemoryResource());
    auto prepared = io.prepareInputs(
        ARP_SURVIVORS_ASSET_DIR, examples::NativeStartupSource::Directory, {}, diagnostic);
    assert(prepared.gameplay && prepared.content && diagnostic.empty());
    // Both source runtimes have already shut down. Exercise their retained
    // owners, without reopening Content or any game/script input.
    assert(prepared.gameplay->config.duration == 300);
    assert(!prepared.gameplay->rules.source().empty());
    assert(prepared.content->content().atlas.ready());
    assert(prepared.content->content().map.ready());
    assert(!prepared.content->sourceIdentity().empty());
    assert(prepared.content->content().sounds[0].ready());
    assert(io.saveOutputs(output, contents, 4096, diagnostic));
    verifyOutputs(output);
}

void failures(unified_io_fixture::Workspace& workspace)
{
    survivors::CodegenIo io;
    core::pmr::String diagnostic(&core::defaultCpuMemoryResource());
    const auto output = workspace.root / "output";
    assert(!io.saveOutputs(workspace.root / "uncreated", contents, 1, diagnostic));
    assert(diagnostic == "SURVIVORS-CODEGEN-OUTPUT-BUDGET");
    assert(!std::filesystem::exists(workspace.root / "uncreated"));
    assert(!io.saveOutputs(output, contents, 1, diagnostic));
    verifyOutputs(output);
    assert(!io.saveOutputs(output, contents, UINT64_MAX, diagnostic));
    assert(diagnostic == "SURVIVORS-CODEGEN-OUTPUT-OPTIONS");
    assert(!io.saveOutputs(output, {"", "source", "manifest"}, 4096, diagnostic));
    verifyOutputs(output);
    // The third baseline is rejected before either earlier output is saved.
    workspace.write("output/survivors.manifest.yaml", core::pmr::String(4097, 'x'));
    assert(!io.saveOutputs(output, {"changed", "changed", "changed"}, 4096, diagnostic));
    for (std::size_t i = 0; i < 2; ++i) {
        auto before = core::readBinaryFile(output / names[i], 4096);
        assert(before.success && before.bytes[0] == contents[i][0]);
    }
    assert(io.saveOutputs(output, contents, 8192, diagnostic));
    verifyOutputs(output);
    auto absent =
        io.prepareInputs(workspace.root, examples::NativeStartupSource::Directory, {}, diagnostic);
    assert(!absent.gameplay && !absent.content && !diagnostic.empty());
    auto emptyUrl = io.prepareInputs(
        ARP_SURVIVORS_ASSET_DIR, examples::NativeStartupSource::Http, {}, diagnostic);
    assert(!emptyUrl.gameplay && !emptyUrl.content && !diagnostic.empty());
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    const auto output = workspace.root / "output";
    cycle(output);
    if (argc > 1 && std::strcmp(argv[1], "--smoke") == 0) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            cycle(output);
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Survivors codegen IO core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes);
        assert(before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    } else
        failures(workspace);
    std::puts(
        "PASS source owners after retirement, original text-mode bytes, bounded baseline saves");
}
