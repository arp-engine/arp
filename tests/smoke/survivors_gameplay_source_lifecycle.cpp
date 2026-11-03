#include "core/hash/sha256.h"
#include "examples/survivors/gameplay_source_task.h"
#include "resource/resource_manager.h"
#include "tests/fixtures/queen_source_runtime.h"
#include "tests/fixtures/survivors_gameplay_identity.h"

#include <cstdio>
#include <emscripten.h>

namespace {
using namespace arp;
using Task = survivors::GameplaySourceTask;
using Phase = examples::StartupResourcePhase;

// clang-format off
EM_JS(void, publishSample, (const char* publication, const char* hash), {
    const sample=Module.survivorsGameplaySample || (Module.survivorsGameplaySample={});
    sample[UTF8ToString(publication)]=UTF8ToString(hash);
});
EM_JS(void, publishResult, (double baselineBytes, double baselineLive, double retiredBytes,
    double retiredLive, int files, const char* error), {
    fetch('/result',{method:'POST',body:JSON.stringify({success:!error,cycles:error?0:20,
        baselineBytes,baselineLive,retiredBytes,retiredLive,files,sample:Module.survivorsGameplaySample,
        fetchAttempts:Module.gameplayRequests,
        error:error?UTF8ToString(error):null,scope:'Gameplay task/graph/Fetch and original text owners; reflection metadata remains resident',productionReady:false})});
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

class GameplayLifecycleProbe final {
public:
    explicit GameplayLifecycleProbe(std::string_view url) : url_(url)
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
                require(runtime_->dispatch(1).success(), "SURVIVORS-GAMEPLAY-PROBE-SUBMIT");
                return false;
            }
            // A real Fetch has started. Destroy its task before dispatching
            // the accepted result; shutdown must retire the graph callback.
            task_.reset();
            retire();
            cancel_ = false;
            return ++cycles_ == 21;
        }
        require(runtime_->dispatch().success(), "SURVIVORS-GAMEPLAY-PROBE-DISPATCH");
        task_->advance();
        if (task_->phase() == Phase::Pending)
            return false;
        if (task_->phase() != Phase::Ready) {
            char detail[256];
            std::snprintf(detail,
                sizeof(detail),
                "id=%s fileStatus=%u reads=%llu",
                task_->diagnosticId() ? task_->diagnosticId() : "none",
                unsigned(task_->fileStatus()),
                static_cast<unsigned long long>(task_->readCount()));
            throw core::DiagnosticError(detail);
        }
        require(task_->phase() == Phase::Ready, task_->diagnostic());
        require(task_->readCount() == 9, "SURVIVORS-GAMEPLAY-PROBE-READ-COUNT");
        if (!cycles_)
            files_ = 9;
        require(files_ == 9, "SURVIVORS-GAMEPLAY-PROBE-FILE-COUNT");
        { // Owners are consumed after task and IO retirement, before baseline measurement.
            auto owner = task_->preparation();
            require(owner && owner->rules.interpreter(), "SURVIVORS-GAMEPLAY-PROBE-READY");
            task_.reset();
            runtime_->shutdown();
            const auto sourceHash =
                core::Sha256::hexDigest(core::StringView(owner->rules.source()));
            const auto configHash =
                core::Sha256::hexDigest(core::StringView(owner->rules.configYaml()));
            require(sourceHash == tests::SurvivorsGameplayIdentity::sourceSha256,
                "SURVIVORS-GAMEPLAY-PROBE-SOURCE");
            require(configHash == tests::SurvivorsGameplayIdentity::configSha256,
                "SURVIVORS-GAMEPLAY-PROBE-CONFIG");
            require(owner->config.duration == 300
                    && owner->rules.source().size()
                        == tests::SurvivorsGameplayIdentity::sourceBytes,
                "SURVIVORS-GAMEPLAY-PROBE-DATA");
            if (!cycles_) {
                publishSample("source", sourceHash.c_str());
                publishSample("config", configHash.c_str());
            }
            owner.reset();
        }
        retire();
        start();
        task_->advance();
        cancel_ = true;
        return false;
    }

private:
    void start()
    {
        auto fixture =
            queen_source_fixture::create(url_, survivors::GameplayTextLimits::maxSourceBytes);
        runtime_ = std::move(fixture.owner);
        task_ = core::makeUnique<Task>(
            runtime_->fileService(), runtime_->completionQueue(), fixture.mount);
    }

    void retire()
    {
        runtime_->shutdown();
        require(!runtime_->completionQueue().pending() && !runtime_->completionQueue().reserved(),
            "SURVIVORS-GAMEPLAY-PROBE-COMPLETIONS");
        runtime_.reset();
        const auto current = core::defaultCpuAllocator().stats();
        require(current.liveBytes == baseline_.liveBytes && live(current) == live(baseline_),
            "SURVIVORS-GAMEPLAY-PROBE-BASELINE");
    }

    core::pmr::String url_;
    core::CpuMemoryStats baseline_{};
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
    core::UniquePtr<Task> task_;
    unsigned cycles_ = 0, files_ = 0;
    bool cancel_ = false;
};

core::UniquePtr<GameplayLifecycleProbe> probe;
core::CpuMemoryStats initial;
double deadline = 0;

void tick()
try {
    require(emscripten_get_now() < deadline, "SURVIVORS-GAMEPLAY-PROBE-DEADLINE");
    if (!probe->tick())
        return;
    const auto files = probe->files();
    probe.reset();
    const auto retired = core::defaultCpuAllocator().stats();
    require(retired.liveBytes == initial.liveBytes && live(retired) == live(initial),
        "SURVIVORS-GAMEPLAY-PROBE-OWNERS");
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
    probe = core::makeUnique<GameplayLifecycleProbe>(argv[1]);
    deadline = emscripten_get_now() + 120000;
    emscripten_set_main_loop(tick, 0, false);
}
