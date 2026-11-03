#include "core/diagnostics/diagnostic_error.h"
#include "tools/io/cold_tool_files.h"
#include "tools/io/native_tool_outputs.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#if !defined(__EMSCRIPTEN__)
#error This contract exercises Wasm cold-host rejection, not a native fixture.
#endif

int main()
{
    using namespace arp;
    const auto before = core::defaultCpuAllocator().stats();
    bool rejected = false;
    try {
        tools::io::ColdToolFiles files(4096, 0, tools::io::ColdToolFileScope::NativeHostFilesystem);
    } catch (const core::DiagnosticError& error) {
        rejected = !std::strcmp(error.what(), "TOOL-FILE-HOST-NATIVE-ONLY");
    }
    assert(rejected);
    {
        const tools::io::NativeToolOutputFile output{"never-created/output.cpp", "code\n"};
        const auto saved = tools::io::NativeToolOutputWriter{}.save({{&output, 1}, 1, 4096});
        assert(!saved.success() && !saved.committedFiles
            && saved.lastSave.status == core::io::FileIoStatus::Unsupported
            && !std::strcmp(saved.diagnosticId, "TOOL-OUTPUT-NATIVE-ONLY"));
    }
    {
        tools::io::ColdToolFiles files(4096, 1);
        core::io::FileReadRequest request;
        request.contractVersion = 1;
        request.pathScope = core::io::FilePathScope::MountedResource;
        request.path = "never-submitted";
        request.maxBytes = 4096;
        const auto read = files.read(request);
        assert(read.status == core::io::FileIoStatus::Unsupported
            && !std::strcmp(read.diagnosticId, "TOOL-FILE-NATIVE-WAIT-UNAVAILABLE"));
        files.retire();
        assert(!files.service().telemetry().submitted);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::puts(
        "PASS Wasm rejects native Host/output profiles and blocking waits without IO admission");
}
