#include "core/diagnostics/diagnostic_error.h"
#include "core/hash/sha256.h"
#include "core/io/http_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "tool_source_identity.h"
#include "tools/asset_importer/tool_script_source_host.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <malloc.h>
#include <mimalloc.h>
#endif

namespace {
using namespace arp;
using namespace core::io;
using namespace tools::asset_importer;
namespace golden = tests::tool_source_identity;
constexpr std::array<const char*, 4> absent{"io", "yaml", "asset", "arp_import_host"};

void require(bool value, const char* diagnostic)
{
    if (!value)
        throw core::DiagnosticError(diagnostic);
}

void verify(const ToolScriptSources& input)
{
    require(input.ready() && input.fileCount() == golden::files.size()
            && input.modules().size() == golden::files.size() + absent.size(),
        "TOOL-FETCH-CLOSURE");
    for (const auto& expected : golden::files) {
        const auto* module =
            !std::strcmp(expected.name, "unity_import") ? input.entry() : input.find(expected.name);
        require(module && !module->absent && module->source(), "TOOL-FETCH-MODULE");
        const auto& entry = *module->source();
        const auto hash = core::Sha256::hexDigest(
            {reinterpret_cast<const char*>(entry.source.bytes.data()), entry.source.bytes.size()});
        require(hash == expected.sha256 && entry.textHash == core::Sha256::hexDigest(entry.text),
            "TOOL-FETCH-ORIGINAL-HASH");
    }
    for (const auto* name : absent) {
        const auto* module = input.find(name);
        require(module && module->absent && !module->source(), "TOOL-FETCH-ABSENT");
    }
}

class Cycle final {
public:
    Cycle(core::StringView url, core::StringView mode)
        : mode_(mode, &core::defaultCpuMemoryResource())
    {
        HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = 2 * ToolScriptSourceLimits::maxFileBytes;
        desc.maxPathBytes = 512;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 16384;
        desc.timeoutMs = 5000;
        desc.cancellationPollMs = 10;
        mounts_ = core::makeShared<MountedFileBackend>(
            MountedFileBackendDesc{1, 1, ToolScriptSourceLimits::maxFileBytes, 32, 512});
        const auto mount =
            mounts_->mount("tool-sources", core::makeShared<HttpFileBackend>(std::move(desc)));
        require(bool(mount), "TOOL-FETCH-MOUNT");
        core::runtime::AsyncRuntimeDesc runtimeDesc;
        runtimeDesc.enableWork = runtimeDesc.enableThreadPoolFileBackend = false;
        runtimeDesc.completionBudget = runtimeDesc.completionQueueCapacity = 2;
        runtimeDesc.fileBackend = mounts_;
        runtime_ = core::makeUnique<core::runtime::AsyncRuntime>(std::move(runtimeDesc));
        require(runtime_->valid(), "TOOL-FETCH-RUNTIME");
        ToolScriptSourceDesc source;
        source.entry.contractVersion = 1;
        source.entry.path = "unity_import.arp";
        source.entry.pathScope = FilePathScope::MountedResource;
        source.entry.mount = source.moduleMount = mount;
        source.entry.maxBytes = ToolScriptSourceLimits::maxFileBytes;
        task_ = core::makeUnique<ToolScriptSourceTask>(runtime_->fileService(), std::move(source));
        task_->advance();
    }

    ~Cycle()
    {
        task_.reset();
        if (runtime_)
            runtime_->shutdown();
    }

    bool poll()
    {
        // Retirement tests yield once so the real transport observes a request.
        if (mode_ == "cancel" || mode_ == "destroy") {
            if (!yielded_) {
                yielded_ = true;
                return false;
            }
            if (mode_ == "destroy") {
                task_.reset();
                retire();
                return true;
            }
            task_->cancel();
        }
        require(runtime_->dispatch().success(), "TOOL-FETCH-DISPATCH");
        task_->advance();
        if (task_->phase() == ToolScriptSourcePhase::Pending)
            return false;
        auto owner = task_->sources();
        if (mode_ == "ok")
            require(owner && task_->readCount() == golden::files.size() + absent.size(),
                task_->diagnosticId() ? task_->diagnosticId() : "TOOL-FETCH-READY");
        else {
            require(!owner && task_->diagnosticId(), "TOOL-FETCH-FAIL-CLOSED");
            rejection_ = task_->diagnostic();
            require(!rejection_.empty(), "TOOL-FETCH-REJECTION");
        }
        task_.reset();
        retire();
        if (owner)
            verify(*owner);
        return true;
    }

    const core::pmr::String& rejection() const { return rejection_; }

private:
    void retire()
    {
        runtime_->shutdown();
        require(!runtime_->completionQueue().pending() && !runtime_->completionQueue().reserved()
                && !mounts_->telemetry().inFlight,
            "TOOL-FETCH-NOT-RETIRED");
        runtime_.reset();
        mounts_.reset();
    }

    core::pmr::String mode_;
    core::pmr::String rejection_{&core::defaultCpuMemoryResource()};
    bool yielded_ = false;
    core::SharedPtr<MountedFileBackend> mounts_;
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
    core::UniquePtr<ToolScriptSourceTask> task_;
};

#if defined(__EMSCRIPTEN__)
// clang-format off
EM_JS(void, noteMemory, (double bytes, double live, double backing, double miBytes, double miBlocks), {
    (Module.memory||(Module.memory=[])).push({bytes,live,backing,miBytes,miBlocks});
    Module.journal.append('cycle '+Module.memory.length+' core='+bytes+' backing='+backing);
});
EM_JS(void, report, (const char* error, const char* rejection, int cycles), {
    globalThis.reportTool({success:!error,error:error?UTF8ToString(error):null,
        rejection:rejection?UTF8ToString(rejection):null,cycles,memory:Module.memory,
        requests:Module.sourceRequests,productionReady:false});
});
// clang-format on

class Lifecycle final {
public:
    Lifecycle(core::StringView url, core::StringView mode)
        : url_(url, &core::defaultCpuMemoryResource()),
          mode_(mode, &core::defaultCpuMemoryResource())
    {}

    void tick()
    {
        if (!cycle_)
            cycle_ = core::makeUnique<Cycle>(url_, mode_);
        if (!cycle_->poll())
            return;
        rejection_ = cycle_->rejection();
        cycle_.reset();
        const auto stats = core::defaultCpuAllocator().stats();
        std::array<std::uint64_t, 4> sample{
            stats.liveBytes, stats.allocationCount - stats.deallocationCount, 0, 0};
        require(mi_heap_visit_blocks(
                    mi_heap_get_default(),
                    true,
                    [](const mi_heap_t*,
                        const mi_heap_area_t*,
                        void* block,
                        std::size_t bytes,
                        void* context) {
                        if (block) {
                            auto& sample = *static_cast<std::array<std::uint64_t, 4>*>(context);
                            sample[2] += bytes;
                            ++sample[3];
                        }
                        return true;
                    },
                    &sample),
            "TOOL-FETCH-HEAP-SAMPLE");
        noteMemory(sample[0], sample[1], mallinfo().uordblks, sample[2], sample[3]);
        if (!cycles_)
            baseline_ = sample;
        else
            require(sample == baseline_, "TOOL-FETCH-BASELINE");
        if (++cycles_ < (mode_ == "ok" ? 21u : 2u))
            return;
        report(nullptr, rejection_.empty() ? nullptr : rejection_.c_str(), cycles_ - 1);
        emscripten_cancel_main_loop();
    }

private:
    core::pmr::String url_, mode_;
    core::pmr::String rejection_{&core::defaultCpuMemoryResource()};
    unsigned cycles_ = 0;
    std::array<std::uint64_t, 4> baseline_{};
    core::UniquePtr<Cycle> cycle_;
};

core::UniquePtr<Lifecycle> lifecycle;
#endif
} // namespace

int main(int argc, char** argv)
try {
#if defined(__EMSCRIPTEN__)
    require(
        argc == 3 && mi_option_get(mi_option_arena_reserve) == 0, "TOOL-FETCH-ARGUMENTS-POLICY");
    lifecycle = core::makeUnique<Lifecycle>(argv[1], argv[2]);
    emscripten_set_main_loop(
        [] {
            try {
                lifecycle->tick();
            } catch (const std::exception& error) {
                report(error.what(), nullptr, 0);
                emscripten_cancel_main_loop();
            }
        },
        0,
        false);
#else
    require(argc >= 2, "TOOL-SOURCE-LIFECYCLE-ARGUMENTS");
    if (!std::strcmp(argv[1], "--native")) {
        const std::filesystem::path root = ARP_SOURCE_DIR;
        auto prepared = ToolScriptSourceHost::prepareNative(
            root / "tools/unity_import/unity_import.arp", root / "tools/unity_import");
        require(bool(prepared.sources), prepared.diagnostics.c_str());
        verify(*prepared.sources);
    } else {
        require(argc == 4, "TOOL-SOURCE-LIFECYCLE-HTTP-ARGUMENTS");
        Cycle cycle(argv[2], argv[3]);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!cycle.poll())
            require(std::chrono::steady_clock::now() < deadline, "TOOL-SOURCE-LIFECYCLE-DEADLINE");
    }
    std::puts("PASS original Unity tool source closure, SHA/Text1, Absent and post-IO ownership");
#endif
    return 0;
} catch (const std::exception& error) {
#if defined(__EMSCRIPTEN__)
    report(error.what(), nullptr, 0);
#else
    std::fprintf(stderr, "%s\n", error.what());
#endif
    return 1;
}
