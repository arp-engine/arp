#include "cloud_cook_tool_identity.h"
#include "core/diagnostics/diagnostic_error.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "tools/cloud_asset/cloud_cook_session.h"

#include <array>
#include <emscripten.h>
#include <malloc.h>
#include <mimalloc.h>

namespace {
using namespace arp;
using namespace core::io;
using namespace tools::cloud_asset;
namespace golden = tests::cloud_cook_tool_identity;
constexpr std::uint64_t documentBudget = 4194304;

void require(bool value, const char* id)
{
    if (!value)
        throw core::DiagnosticError(id);
}

class Cycle final {
public:
    Cycle(core::StringView url, bool success) : expectSuccess_(success)
    {
        mounts_ = core::makeShared<MountedFileBackend>(
            MountedFileBackendDesc{2, 1, 2 * documentBudget, 32, 512});
        HttpFileBackendDesc http;
        http.baseUrl = url;
        http.maxInFlight = 1;
        http.maxBytesInFlight = documentBudget + 1024 * 1024;
        http.maxPathBytes = 512;
        http.maxHeaderBytes = 8192;
        http.ioChunkBytes = 16384;
        http.timeoutMs = 5000;
        http.cancellationPollMs = 10;
        const auto source =
            mounts_->mount("recipe", core::makeShared<HttpFileBackend>(std::move(http)));
        const auto output = mounts_->mount("cooked",
            core::makeShared<MemoryFileBackend>(
                MemoryFileBackendDesc{1, documentBudget, 1, 2 * documentBudget, 512, true}));
        require(source && output, "CLOUD-FETCH-MOUNT");
        core::runtime::AsyncRuntimeDesc runtime;
        runtime.enableWork = runtime.enableThreadPoolFileBackend = false;
        runtime.completionQueueCapacity = runtime.completionBudget = 2;
        runtime.fileBackend = mounts_;
        runtime_ = core::makeUnique<core::runtime::AsyncRuntime>(std::move(runtime));
        require(runtime_->valid(), "CLOUD-FETCH-RUNTIME");
        CloudCookSessionDesc desc;
        desc.source.contractVersion = 1;
        desc.source.pathScope = FilePathScope::MountedResource;
        desc.source.mount = source;
        desc.source.path = "recipe.yaml";
        desc.source.maxBytes = desc.maxDocumentBytes = documentBudget;
        desc.outputMount = output;
        desc.outputPath = "cooked.yaml";
        outputRequest_.contractVersion = 1;
        outputRequest_.pathScope = FilePathScope::MountedResource;
        outputRequest_.mount = output;
        outputRequest_.path = desc.outputPath;
        outputRequest_.maxBytes = documentBudget;
        session_ = core::makeUnique<CloudCookSession>(runtime_->fileService(), desc);
    }

    ~Cycle()
    {
        if (runtime_)
            runtime_->shutdown();
    }

    bool poll()
    {
        require(runtime_->dispatch().success(), "CLOUD-FETCH-DISPATCH");
        session_->advance();
        if (!session_->terminal())
            return false;
        if (!expectSuccess_) {
            require(session_->phase() == CloudCookPhase::Failed && session_->diagnostic(),
                "CLOUD-FETCH-FAIL-CLOSED");
            rejection_ = session_->diagnostic();
            retire();
            return true;
        }
        require(session_->phase() == CloudCookPhase::Complete,
            session_->diagnostic() ? session_->diagnostic() : "CLOUD-FETCH-NOT-COMPLETE");
        if (!readSubmitted_) {
            require(session_->sourceIdentity().kind == FileVersionKind::ContentSha256
                    && core::Sha256::matchesRawHexDigest(
                        session_->sourceIdentity().digest, golden::sourceHash),
                "CLOUD-FETCH-SOURCE-IDENTITY");
            require(runtime_->fileService().read(outputRequest_,
                        {[](void* context, FileReadResult result) {
                             auto& self = *static_cast<Cycle*>(context);
                             self.output_ = std::move(result);
                             self.readDone_ = true;
                         },
                            this})
                    == FileIoStatus::Accepted,
                "CLOUD-FETCH-OUTPUT-READ");
            readSubmitted_ = true;
            return false;
        }
        if (!readDone_)
            return false;
        require(output_.status == FileIoStatus::Success
                && output_.bytes.size() == golden::documentBytes,
            "CLOUD-FETCH-OUTPUT-SIZE");
        require(core::Sha256::hexDigest(
                    {reinterpret_cast<const char*>(output_.bytes.data()), output_.bytes.size()})
                == golden::cookedHash,
            "CLOUD-FETCH-ORIGINAL-COOKED-BYTES");
        retire();
        return true;
    }

    const core::pmr::String& rejection() const noexcept { return rejection_; }

private:
    void retire()
    {
        session_.reset();
        runtime_->shutdown();
        require(!runtime_->completionQueue().pending() && !runtime_->completionQueue().reserved()
                && !mounts_->telemetry().inFlight,
            "CLOUD-FETCH-NOT-RETIRED");
        runtime_.reset();
        mounts_.reset();
    }

    bool expectSuccess_, readSubmitted_ = false, readDone_ = false;
    core::pmr::String rejection_{&core::defaultCpuMemoryResource()};
    core::SharedPtr<MountedFileBackend> mounts_;
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
    core::UniquePtr<CloudCookSession> session_;
    FileReadRequest outputRequest_;
    FileReadResult output_;
};

// clang-format off
EM_JS(void, noteMemory, (double bytes, double live, double backing, double miBytes, double miBlocks), {
    (Module.memory||(Module.memory=[])).push({bytes,live,backing,miBytes,miBlocks});
    Module.journal.append('cycle '+Module.memory.length+' core='+bytes+' backing='+backing);
});
EM_JS(void, report, (const char* error, const char* rejection, int cycles), {
    globalThis.reportCloudCook({success:!error,error:error?UTF8ToString(error):null,
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
            cycle_ = core::makeUnique<Cycle>(url_, mode_ == "ok");
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
                            auto& values = *static_cast<std::array<std::uint64_t, 4>*>(context);
                            values[2] += bytes;
                            ++values[3];
                        }
                        return true;
                    },
                    &sample),
            "CLOUD-FETCH-HEAP-SAMPLE");
        noteMemory(sample[0], sample[1], mallinfo().uordblks, sample[2], sample[3]);
        if (!cycles_)
            baseline_ = sample;
        else
            require(sample == baseline_, "CLOUD-FETCH-BASELINE");
        if (++cycles_ < (mode_ == "ok" ? 21u : 2u))
            return;
        report(nullptr, rejection_.empty() ? nullptr : rejection_.c_str(), cycles_ - 1);
        emscripten_cancel_main_loop();
    }

private:
    core::pmr::String url_, mode_;
    core::pmr::String rejection_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<Cycle> cycle_;
    unsigned cycles_ = 0;
    std::array<std::uint64_t, 4> baseline_{};
};

core::UniquePtr<Lifecycle> lifecycle;
} // namespace

int main(int argc, char** argv)
{
    core::installDefaultCpuAllocatorAsPmrDefault();
    require(argc == 3 && mi_option_get(mi_option_arena_reserve) == 0, "CLOUD-FETCH-POLICY");
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
        0);
}
