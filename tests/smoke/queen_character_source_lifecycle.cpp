#include "core/diagnostics/diagnostic_error.h"
#include "core/io/http_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "examples/build_a_queen/character_source_task.h"
#include "tests/fixtures/queen_source_runtime.h"

#include <array>
#include <emscripten.h>

namespace {
using namespace arp;
using namespace core::io;
using Sources = build_a_queen::CharacterSources;
using Task = build_a_queen::CharacterSourceTask;
using Phase = build_a_queen::CharacterSourcePhase;

// clang-format off
EM_JS(void, publishSample, (const char* manifest, const char* player, const char* rival), {
    Module.queenSourceSample={manifest:UTF8ToString(manifest),player:UTF8ToString(player),rival:UTF8ToString(rival)};
});
EM_JS(void, publishResult, (int cycles, double bytes, double live, const char* error), {
    const report={success:!error,cycles,retiredCoreBytes:bytes,retiredCoreLive:live,
        sample:Module.queenSourceSample,error:error?UTF8ToString(error):null,
        scope:'FileService Fetch source/task owners only',productionReady:false};
    fetch('/result',{method:'POST',body:JSON.stringify(report)});
});
// clang-format on

void require(bool success, const char* diagnostic)
{
    if (!success)
        throw core::DiagnosticError(diagnostic);
}

std::uint64_t live(const core::CpuMemoryStats& stats)
{
    return stats.allocationCount - stats.deallocationCount;
}

class SourceLifecycleProbe final {
public:
    explicit SourceLifecycleProbe(std::string_view url) : url_(url)
    {
        baseline_ = core::defaultCpuAllocator().stats();
    }

    bool tick()
    {
        if (!runtime_) {
            start();
            return false;
        }
        if (cancelNextTick_) {
            // Retire after a real browser turn, before dispatching a possible
            // late completion. This tests destroyed tasks and Fetch detach.
            task_.reset();
            retire();
            cancelNextTick_ = false;
            ++cycles_;
            return cycles_ == 21; // One warm cycle followed by twenty measured.
        }
        require(runtime_->dispatch().success(), "QUEEN-PROBE-DISPATCH");
        task_->advance();
        if (task_->phase() == Phase::Pending)
            return false;
        require(task_->phase() == Phase::Ready, task_->diagnostic());
        {
            auto sources = task_->takeSources();
            require(sources && sources->ready(), "QUEEN-PROBE-SOURCES");
            require(runtime_->telemetry().fileService.submitted == 3, "QUEEN-PROBE-READ-COUNT");
            const std::array<const FileReadResult*, 3> inputs{sources->manifestSource(),
                sources->characterSource(0),
                sources->characterSource(1)};
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                if (!cycles_)
                    versions_[i] = inputs[i]->version;
                require(versions_[i] == inputs[i]->version, "QUEEN-PROBE-SOURCE-IDENTITY");
            }
            if (!cycles_) {
                const auto hash = [](const FileReadResult& input) {
                    return core::Sha256::hexDigest(
                        {reinterpret_cast<const char*>(input.bytes.data()), input.bytes.size()});
                };
                const auto manifest = hash(*inputs[0]), player = hash(*inputs[1]),
                           rival = hash(*inputs[2]);
                publishSample(manifest.c_str(), player.c_str(), rival.c_str());
            }
        }
        task_.reset();
        retire();
        start();
        task_->advance();
        require(runtime_->telemetry().fileService.submitted == 1, "QUEEN-PROBE-CANCEL-SUBMIT");
        cancelNextTick_ = true;
        return false;
    }

private:
    void start()
    {
        auto fixture = queen_source_fixture::create(url_, Sources::maxCharacterBytes);
        runtime_ = std::move(fixture.owner);
        FileReadRequest binding;
        binding.contractVersion = 1;
        binding.pathScope = FilePathScope::MountedResource;
        binding.mount = fixture.mount;
        binding.path = Sources::manifestFile;
        binding.maxBytes = Sources::maxManifestBytes;
        task_ = core::makeUnique<Task>(runtime_->fileService(), std::move(binding));
    }

    void retire()
    {
        runtime_->shutdown();
        require(!runtime_->completionQueue().pending() && !runtime_->completionQueue().reserved(),
            "QUEEN-PROBE-COMPLETION-OWNERS");
        runtime_.reset();
        const auto current = core::defaultCpuAllocator().stats();
        require(current.liveBytes == baseline_.liveBytes && live(current) == live(baseline_),
            "QUEEN-PROBE-CORE-BASELINE");
    }

    core::pmr::String url_;
    core::CpuMemoryStats baseline_{};
    std::array<FileVersionToken, 3> versions_;
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
    core::UniquePtr<Task> task_;
    unsigned cycles_ = 0;
    bool cancelNextTick_ = false;
};

core::UniquePtr<SourceLifecycleProbe> probe;
core::CpuMemoryStats initial;
double deadline = 0;

void tick()
try {
    require(emscripten_get_now() < deadline, "QUEEN-PROBE-DEADLINE");
    if (!probe->tick())
        return;
    probe.reset();
    const auto retired = core::defaultCpuAllocator().stats();
    require(retired.liveBytes == initial.liveBytes && live(retired) == live(initial),
        "QUEEN-PROBE-ALL-OWNERS");
    publishResult(20, retired.liveBytes, live(retired), nullptr);
    emscripten_cancel_main_loop();
} catch (const std::exception& error) {
    probe.reset();
    publishResult(0, 0, 0, error.what());
    emscripten_cancel_main_loop();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    initial = core::defaultCpuAllocator().stats();
    probe = core::makeUnique<SourceLifecycleProbe>(argv[1]);
    deadline = emscripten_get_now() + 120000;
    emscripten_set_main_loop(tick, 0, false);
    return 0;
}
