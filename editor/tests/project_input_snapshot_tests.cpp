#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/project/project_input_snapshot.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace {
using namespace arp;
using namespace arp::editor;

class InputFixture final {
public:
    InputFixture()
    {
        root = std::filesystem::temp_directory_path()
            / ("arp-editor-inputs-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assert(std::filesystem::create_directory(root));
        root = std::filesystem::canonical(root);
        write("a.bin", std::string_view("a\0b", 3));
        write("b.bin", "xyz");
    }

    ~InputFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        assert(!error);
    }

    void write(const char* name, std::string_view bytes)
    {
        std::ofstream stream(root / name, std::ios::binary);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        assert(stream.good());
    }

    std::filesystem::path root;
};

void contracts(InputFixture& fixture)
{
    core::pmr::String diagnostic;
    ProjectInputSnapshot inputs(fixture.root, {2, 3, 6});
    assert(inputs.read(fixture.root / "b.bin", diagnostic));
    assert(inputs.read(fixture.root / "a.bin", diagnostic));
    assert(inputs.read(fixture.root / "." / "a.bin", diagnostic));
    assert(inputs.bindings().size() == 2 && inputs.bindings()[0].path == "a.bin"
        && inputs.bindings()[0].fingerprint
            == core::Sha256::hexDigest(std::string_view("a\0b", 3)));
    assert(inputs.verify(diagnostic));
    fixture.write("a.bin", "aXb");
    assert(!inputs.verify(diagnostic) && diagnostic == "PROJECT-INPUT-CONTENT-CHANGED");
    assert(!inputs.read(fixture.root / "a.bin", diagnostic) && inputs.bindings().size() == 2);
    fixture.write("a.bin", std::string_view("a\0b", 3));
    assert(inputs.verify(diagnostic));
    auto binding = inputs.takeBindings();
    assert(inputs.bindings().empty() && inputs.read(fixture.root / "b.bin", diagnostic));
    auto invalid = binding;
    invalid.push_back(binding.front());
    assert(!ProjectInputSnapshot::validateBindings(invalid, diagnostic));
    invalid = binding;
    invalid[0].path = "../escape";
    assert(!ProjectInputSnapshot::validateBindings(invalid, diagnostic));
    invalid = binding;
    invalid[0].fingerprint = "sha256:invalid";
    assert(!ProjectInputSnapshot::validateBindings(invalid, diagnostic));
    invalid = binding;
    invalid[0].byteCount = ProjectInputBudget{}.maxFileBytes + 1;
    assert(!ProjectInputSnapshot::validateBindings(invalid, diagnostic));
    invalid = binding;
    std::swap(invalid[0], invalid[1]);
    assert(!ProjectInputSnapshot::validateBindings(invalid, diagnostic));
    ProjectInputSnapshot total(fixture.root, {2, 3, 5});
    assert(total.read(fixture.root / "a.bin", diagnostic));
    assert(!total.read(fixture.root / "b.bin", diagnostic) && total.bindings().size() == 1);
    ProjectInputSnapshot count(fixture.root, {1, 3, 6});
    assert(count.read(fixture.root / "a.bin", diagnostic));
    assert(!count.read(fixture.root / "b.bin", diagnostic));
    fixture.write("large.bin", "1234");
    assert(!inputs.read(fixture.root / "large.bin", diagnostic));
    assert(!inputs.record(fixture.root.parent_path(), "abc", diagnostic));
    assert(!inputs.read(fixture.root / "missing.bin", diagnostic));
    std::filesystem::remove(fixture.root / "a.bin");
    assert(!ProjectInputSnapshot::verify(fixture.root, binding, diagnostic));
    fixture.write("a.bin", std::string_view("a\0b", 3));
    std::error_code linkError;
    std::filesystem::create_symlink(fixture.root / "b.bin", fixture.root / "alias.bin", linkError);
    if (!linkError) {
        ProjectInputSnapshot aliases(fixture.root);
        assert(aliases.read(fixture.root / "alias.bin", diagnostic));
        assert(aliases.bindings().front().path == "b.bin");
        auto forged = aliases.bindings();
        forged[0].path = "alias.bin";
        assert(!ProjectInputSnapshot::verify(fixture.root, forged, diagnostic)
            && diagnostic == "PROJECT-INPUT-TARGET-CHANGED");
    } else
        std::puts("INPUT-SYMLINK: OS denied fixture creation; case not exercised");
    std::puts(
        "PASS actual binary inputs, deterministic ordering/dedup, equal-size edits/missing files, budgets and strict canonical bindings");
}

void smoke(InputFixture& fixture)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            ProjectInputSnapshot inputs(fixture.root);
            core::pmr::String diagnostic;
            assert(inputs.read(fixture.root / "a.bin", diagnostic));
            assert(inputs.read(fixture.root / "b.bin", diagnostic));
            auto bindings = inputs.takeBindings();
            assert(ProjectInputSnapshot::verify(fixture.root, bindings, diagnostic));
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary input snapshot/read/verify/destroy cycles; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    InputFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke(fixture);
    else
        contracts(fixture);
}
