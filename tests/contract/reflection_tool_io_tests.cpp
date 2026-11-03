#include "tests/fixtures/unified_io_fixture.h"
#include "tools/reflect_codegen/reflection_tool_io.h"

#include <cassert>
#include <cstring>

namespace {
using namespace arp;
using namespace tools::reflect_codegen;
using Status = core::io::FileIoStatus;

void cycle(const std::filesystem::path& root)
{
    ReflectionToolIo files({}, false, {});
    auto result = files.readHeader(root / "header.h");
    assert(result.status() == Status::Success);
    files.retire();
    assert(result.decoded.text == "\xef\xbb\xbf// source\r\n");
    assert(result.source.bytes.size() == result.decoded.text.size());
    assert(files.readHeader(root / "header.h").status() == Status::ServiceClosed);
}

void failures(unified_io_fixture::Workspace& workspace)
{
    for (const auto* name : {"utf8.h", "nul.h", "missing.h"}) {
        ReflectionToolIo files({}, false, {});
        const auto result = files.readHeader(workspace.root / name);
        assert(result.status() != Status::Success);
        if (std::strcmp(name, "missing.h"))
            assert(result.diagnosticId());
        files.retire();
    }
    ReflectionIoLimits limited;
    limited.maxHeaderBytes = 1;
    ReflectionToolIo tiny(limited, false, {});
    assert(tiny.readHeader(workspace.root / "header.h").status() == Status::BudgetExceeded);
    limited = {};
    limited.maxHeaders = 1;
    ReflectionToolIo count(limited, false, {});
    assert(count.readHeader(workspace.root / "header.h").status() == Status::Success);
    const auto repeated = count.readHeader(workspace.root / "header.h");
    assert(repeated.status() == Status::BudgetExceeded);
    assert(!std::strcmp(repeated.diagnosticId(), "REFLECT-IO-HEADER-COUNT"));
    limited = {};
    limited.maxInputBytes = 20;
    ReflectionToolIo total(limited, false, {});
    assert(total.readHeader(workspace.root / "header.h").status() == Status::Success);
    assert(total.readHeader(workspace.root / "header.h").status() == Status::BudgetExceeded);
    bool refused = false;
    try {
        ReflectionToolIo emptyUrl({}, true, {});
    } catch (const std::exception&) {
        refused = true;
    }
    assert(refused);
#if defined(_WIN32)
    ReflectionToolIo control({}, false, {});
    const auto ctrlZ = control.readHeader(workspace.root / "ctrlz.h");
    assert(ctrlZ.status() != Status::Success);
    assert(!std::strcmp(ctrlZ.diagnosticId(), "REFLECT-IO-TEXT-EOF-UNSUPPORTED"));
#endif
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    workspace.write("header.h", "\xef\xbb\xbf// source\r\n");
    workspace.write("utf8.h", "\xff");
    workspace.write("nul.h", {"x\0y", 3});
    workspace.write("ctrlz.h", "// source\x1a");
    cycle(workspace.root);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            cycle(workspace.root);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Reflection IO core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else
        failures(workspace);
    std::puts("PASS explicit header text/budgets, source owner after retirement and no fallback");
}
