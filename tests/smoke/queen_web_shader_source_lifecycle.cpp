#include "examples/build_a_queen/web_shader_source_task.h"
#include "resource/resource_manager.h"
#include "shader/shader_artifact_files.h"
#include "tests/fixtures/queen_source_runtime.h"

#include <algorithm>
#include <array>
#include <emscripten.h>

namespace {
using namespace arp;
using Task = build_a_queen::WebShaderSourceTask;
using Phase = build_a_queen::WebShaderSourcePhase;

// clang-format off
EM_JS(void, publishSample, (const char* publication, const char* hash), {
    const sample=Module.queenShaderSample || (Module.queenShaderSample={});
    sample[UTF8ToString(publication)]=UTF8ToString(hash);
});
EM_JS(void, publishResult, (double baselineBytes, double baselineLive, double retiredBytes,
    double retiredLive, int files, const char* error), {
    fetch('/result',{method:'POST',body:JSON.stringify({success:!error,cycles:error?0:20,
        baselineBytes,baselineLive,retiredBytes,retiredLive,files,sample:Module.queenShaderSample,
        error:error?UTF8ToString(error):null,scope:'Shader task/graph/Fetch owners; reflection metadata remains resident',productionReady:false})});
});
// clang-format on

void require(bool ok, std::string_view diagnostic)
{
    if (!ok)
        throw core::DiagnosticError(diagnostic);
}

std::uint64_t live(const core::CpuMemoryStats& stats)
{
    return stats.allocationCount - stats.deallocationCount;
}

class ShaderLifecycleProbe final {
public:
    explicit ShaderLifecycleProbe(std::string_view url) : url_(url)
    {
        baseline_ = core::defaultCpuAllocator().stats();
    }

    unsigned files() const noexcept { return files_; }

    bool tick()
    {
        if (!runtime_) {
            start();
            return false;
        }
        if (cancel_) {
            if (!runtime_->telemetry().fileService.submitted) {
                require(runtime_->dispatch(1).success(), "QUEEN-SHADER-PROBE-SUBMIT");
                return false;
            }
            // A real Fetch has started. Destroy its task before dispatching
            // the accepted result; shutdown must retire the graph callback.
            task_.reset();
            retire();
            cancel_ = false;
            return ++cycles_ == 21;
        }
        require(runtime_->dispatch().success(), "QUEEN-SHADER-PROBE-DISPATCH");
        task_->advance();
        if (task_->phase() == Phase::Pending)
            return false;
        require(task_->phase() == Phase::Ready, task_->diagnostic());
        require(task_->readCount() == task_->fileCount(), "QUEEN-SHADER-PROBE-READ-COUNT");
        if (!cycles_)
            files_ = task_->fileCount();
        require(files_ == task_->fileCount(), "QUEEN-SHADER-PROBE-FILE-COUNT");
        for (std::size_t i = 0; i < Task::publications.size(); ++i) {
            auto raw = task_->source()->read(Task::publications[i], Task::maxFileBytes);
            require(!raw.diagnostic, raw.diagnostic ? raw.diagnostic : "QUEEN-SHADER-PROBE-READ");
            const core::StringView text(
                reinterpret_cast<const char*>(raw.bytes.data()), raw.bytes.size());
            const auto hash = core::Sha256::digest(text);
            if (!cycles_) {
                std::copy(hash.begin(), hash.end(), identities_[i].begin());
                const auto name = core::Sha256::hexDigest(text);
                publishSample(Task::publications[i], name.c_str());
            }
            require(std::equal(hash.begin(), hash.end(), identities_[i].begin()),
                "QUEEN-SHADER-PROBE-IDENTITY");
        }
        task_.reset();
        retire();
        start();
        task_->advance();
        cancel_ = true;
        return false;
    }

private:
    void start()
    {
        auto fixture = queen_source_fixture::create(url_, Task::maxFileBytes);
        runtime_ = std::move(fixture.owner);
        task_ = core::makeUnique<Task>(
            runtime_->fileService(), runtime_->completionQueue(), fixture.mount);
    }

    void retire()
    {
        runtime_->shutdown();
        require(!runtime_->completionQueue().pending() && !runtime_->completionQueue().reserved(),
            "QUEEN-SHADER-PROBE-COMPLETIONS");
        runtime_.reset();
        const auto current = core::defaultCpuAllocator().stats();
        require(current.liveBytes == baseline_.liveBytes && live(current) == live(baseline_),
            "QUEEN-SHADER-PROBE-BASELINE");
    }

    core::pmr::String url_;
    core::CpuMemoryStats baseline_{};
    std::array<std::array<std::uint8_t, 32>, 4> identities_{};
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
    core::UniquePtr<Task> task_;
    unsigned cycles_ = 0, files_ = 0;
    bool cancel_ = false;
};

core::UniquePtr<ShaderLifecycleProbe> probe;
core::CpuMemoryStats initial;
double deadline = 0;

void tick()
try {
    require(emscripten_get_now() < deadline, "QUEEN-SHADER-PROBE-DEADLINE");
    if (!probe->tick())
        return;
    const auto files = probe->files();
    probe.reset();
    const auto retired = core::defaultCpuAllocator().stats();
    require(retired.liveBytes == initial.liveBytes && live(retired) == live(initial),
        "QUEEN-SHADER-PROBE-OWNERS");
    publishResult(
        initial.liveBytes, live(initial), retired.liveBytes, live(retired), files, nullptr);
    emscripten_cancel_main_loop();
} catch (const std::exception& error) {
    probe.reset();
    publishResult(initial.liveBytes, live(initial), 0, 0, 0, error.what());
    emscripten_cancel_main_loop();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    { // Existing reflection database and undefined singleton have process lifetime.
        resource::ResourceManager metadata;
        resource::ResourceBase carrier(resource::ResourceReference{});
        (void)reflection::object::undefined();
    }
    initial = core::defaultCpuAllocator().stats();
    probe = core::makeUnique<ShaderLifecycleProbe>(argv[1]);
    deadline = emscripten_get_now() + 120000;
    emscripten_set_main_loop(tick, 0, false);
}
