#include "tests/fixtures/unified_io_fixture.h"
#include "tools/shader/spirv_verifier_source.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace core::io;
using namespace tools::shader;

void read(const std::filesystem::path& directory)
{
    const auto path = core::FilePath::utf8(directory / "shader.bin");
    auto source = SpirvVerifierSource::acquire({path, {}, 64});
    // Byte acquisition must preserve even invalid SPIR-V; proof is a separate owner.
    assert(source.status == FileIoStatus::Success && source.bytes.size() == 6);
    assert(source.bytes.front() == 3 && source.bytes[1] == 0 && source.bytes.back() == 0xff);
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    workspace.write("shader.bin", core::StringView("\x03\0\x23\x07\x80\xff", 6));
    read(workspace.root);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            read(workspace.root);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("SPIR-V source core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else {
        const auto path = core::FilePath::utf8(workspace.root / "shader.bin");
        assert(SpirvVerifierSource::acquire({path, {}, 5}).status == FileIoStatus::BudgetExceeded);
        assert(SpirvVerifierSource::acquire({path, {}, 0}).status == FileIoStatus::InvalidRequest);
        assert(SpirvVerifierSource::acquire({path, {}, UINT64_MAX}).status
            == FileIoStatus::InvalidRequest);
        const auto missing = core::FilePath::utf8(workspace.root / "missing.bin");
        assert(SpirvVerifierSource::acquire({missing, {}, 64}).status == FileIoStatus::MissingFile);
    }
    std::puts("PASS original byte owner after unified IO retirement, finite limits and refusal");
}
