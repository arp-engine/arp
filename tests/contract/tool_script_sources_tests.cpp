#include "core/hash/sha256.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "tools/asset_importer/tool_script_sources.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace core::io;
using namespace tools::asset_importer;

constexpr std::array<std::pair<core::StringView, core::StringView>, 4> files{
    {{"entry.arp",
         "\xef\xbb\xbfimport branch_a\r\nimport branch_b\r\nimport native_host\r\n\r\ndef main() -> int:\r\n    return 7\r\n"},
        {"branch_a.arp", "import leaf\n\nclass BranchA:\n    value: int\n"},
        {"branch_b.arp", "import leaf\n\nclass BranchB:\n    value: int\n"},
        {"leaf.arp", "# immutable shared dependency\nclass Leaf:\n    value: int\n"}}};

enum class Fault {
    None,
    Entry,
    Utf8,
    Nul,
    Syntax,
    Version,
    Bytes,
    Files,
    Names,
    Stale,
    Cancel,
    Destroy,
    Descriptor,
    Prefix
};

ToolScriptSourceDesc descriptor(core::PoolHandle mount)
{
    ToolScriptSourceDesc desc;
    desc.entry.contractVersion = 1;
    desc.entry.path = "entry.arp";
    desc.entry.pathScope = FilePathScope::MountedResource;
    desc.entry.mount = desc.moduleMount = mount;
    desc.entry.maxBytes = ToolScriptSourceLimits::maxFileBytes;
    return desc;
}

void verifyDescriptorCopy()
{
    ToolScriptSourceDesc desc;
    desc.modulePrefix = "long-explicit-source-directory";
    desc.entryDisplayName = "diagnostic-only-source-label-that-never-selects-another-file";
    desc.moduleDisplayPrefix = "diagnostic-only-module-label";
    // Copying PMR metadata must not select the process default allocator.
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    try {
        const auto copy = desc;
        assert(copy.modulePrefix == desc.modulePrefix
            && copy.entryDisplayName == desc.entryDisplayName
            && copy.moduleDisplayPrefix == desc.moduleDisplayPrefix);
        assert(copy.modulePrefix.get_allocator().resource() == &core::defaultCpuMemoryResource()
            && copy.entryDisplayName.get_allocator().resource() == &core::defaultCpuMemoryResource()
            && copy.moduleDisplayPrefix.get_allocator().resource()
                == &core::defaultCpuMemoryResource());
    } catch (...) {
        std::pmr::set_default_resource(previous);
        throw;
    }
    std::pmr::set_default_resource(previous);
}

void verify(const ToolScriptSources& source)
{
    auto* resource = &core::defaultCpuMemoryResource();
    assert(source.modules().get_allocator().resource() == resource);
    for (const auto& module : source.modules()) {
        assert(module.name.get_allocator().resource() == resource
            && module.path.get_allocator().resource() == resource
            && module.displayName.get_allocator().resource() == resource);
    }
    assert(source.ready() && source.fileCount() == files.size() && source.modules().size() == 5);
    std::uint64_t bytes = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto name = i == 0
            ? core::pmr::String(ToolScriptSourceLimits::entryModule, resource)
            : core::pmr::String(files[i].first.substr(0, files[i].first.size() - 4), resource);
        const auto* module = source.find(name);
        assert(module && !module->absent && module->source());
        const auto& input = *module->source();
        assert(input.source.bytes.size() == files[i].second.size()
            && !std::memcmp(
                input.source.bytes.data(), files[i].second.data(), files[i].second.size()));
        assert(input.textHash == core::Sha256::hexDigest(input.text));
        if (i == 0)
            assert(input.text == files[0].second.substr(3)
                && input.text.find('\r') != core::pmr::String::npos);
        bytes += files[i].second.size();
    }
    assert(source.sourceBytes() == bytes && source.textBytes() == bytes - 3);
    const auto* missing = source.find("native_host");
    assert(missing && missing->absent && !missing->source());
    assert(!source.find("not_discovered"));
}

core::SharedPtr<const ToolScriptSources> execute(
    core::SharedPtr<ManagedFileBackend> backend, Fault fault)
{
    auto router = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{1, 1, ToolScriptSourceLimits::maxFileBytes, 32, 512});
    const auto mount = router->mount("tool-sources", std::move(backend));
    assert(mount);
    core::runtime::AsyncRuntimeDesc runtimeDesc;
    runtimeDesc.enableWork = runtimeDesc.enableThreadPoolFileBackend = false;
    runtimeDesc.completionBudget = runtimeDesc.completionQueueCapacity = 2;
    runtimeDesc.fileBackend = router;
    auto runtime = core::makeUnique<core::runtime::AsyncRuntime>(std::move(runtimeDesc));
    assert(runtime->valid());
    auto desc = descriptor(mount);
    if (fault == Fault::Descriptor)
        desc.contractVersion = 99;
    if (fault == Fault::Prefix)
        desc.modulePrefix = "../escape";
    if (fault == Fault::Version) {
        desc.entry.expectedVersion.kind = FileVersionKind::ContentSha256;
        desc.entry.maxVersionBytes = ToolScriptSourceLimits::maxFileBytes;
    }
    if (fault == Fault::Bytes)
        desc.entry.maxBytes = 1;
    if (fault == Fault::Files)
        desc.budget.maxFiles = 1;
    if (fault == Fault::Names) {
        desc.budget.maxFiles = 2;
        desc.maxNames = 2;
    }
    auto task = core::makeUnique<ToolScriptSourceTask>(runtime->fileService(), std::move(desc));
    if (fault == Fault::Stale)
        assert(router->unmount(mount));
    if (fault == Fault::Cancel)
        task->cancel();
    task->advance();
    if (fault == Fault::Destroy) {
        task.reset();
        runtime->shutdown();
        assert(!router->telemetry().inFlight);
        return {};
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (task->phase() == ToolScriptSourcePhase::Pending
        && std::chrono::steady_clock::now() < deadline) {
        assert(runtime->dispatch().success());
        task->advance();
    }
    if (fault == Fault::None && task->phase() != ToolScriptSourcePhase::Ready)
        std::fprintf(stderr,
            "%s: %.*s\n",
            task->diagnosticId(),
            int(task->diagnostic().size()),
            task->diagnostic().data());
    assert(task->phase() != ToolScriptSourcePhase::Pending);
    auto owner = task->sources();
    if (fault == Fault::None)
        assert(owner && task->readCount() == 5);
    else
        assert(!owner && task->diagnosticId());
    task.reset();
    runtime->shutdown();
    assert(!runtime->completionQueue().pending() && !runtime->completionQueue().reserved());
    runtime.reset();
    assert(!router->telemetry().inFlight);
    router.reset();
    if (owner)
        verify(*owner);
    return owner;
}

void cycle(Fault fault)
{
    auto memory = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{8, 65536, 1, ToolScriptSourceLimits::maxFileBytes, 512});
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (fault == Fault::Entry && i == 0)
            continue;
        core::pmr::String text(files[i].second, &core::defaultCpuMemoryResource());
        if (i == 3 && fault == Fault::Utf8)
            text = "#\xff\n";
        if (i == 3 && fault == Fault::Nul)
            text.push_back('\0');
        if (i == 3 && fault == Fault::Syntax)
            text = "class Broken {\n";
        assert(memory->registerFile(
                   files[i].first, reinterpret_cast<const std::uint8_t*>(text.data()), text.size())
            == FileIoStatus::Success);
    }
    execute(memory, fault);
    assert(!memory->telemetry().inFlight);
}
} // namespace

int main(int argc, char** argv)
{
    verifyDescriptorCopy();
    if (argc > 1 && !std::strcmp(argv[1], "--http")) {
        assert(argc == 3);
        HttpFileBackendDesc desc;
        desc.baseUrl = argv[2];
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = 2 * ToolScriptSourceLimits::maxFileBytes;
        desc.maxPathBytes = 512;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 16384;
        desc.timeoutMs = 5000;
        desc.cancellationPollMs = 10;
        execute(core::makeShared<HttpFileBackend>(std::move(desc)), Fault::None);
    } else if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        cycle(Fault::None);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            cycle(Fault::None);
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Tool source core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    } else
        for (auto fault : {Fault::None,
                 Fault::Entry,
                 Fault::Utf8,
                 Fault::Nul,
                 Fault::Syntax,
                 Fault::Version,
                 Fault::Bytes,
                 Fault::Files,
                 Fault::Names,
                 Fault::Stale,
                 Fault::Cancel,
                 Fault::Destroy,
                 Fault::Descriptor,
                 Fault::Prefix})
            cycle(fault);
    std::puts(
        "PASS tool source closure, original bytes/Text1, Missing-only absence, retirement and faults");
}
