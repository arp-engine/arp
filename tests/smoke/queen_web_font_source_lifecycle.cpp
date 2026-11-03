#include "core/hash/sha256.h"
#include "examples/build_a_queen/web_font_source_task.h"
#include "resource/resource_manager.h"
#include "tests/fixtures/queen_source_runtime.h"

#include <algorithm>
#include <array>
#include <emscripten.h>

namespace {
using namespace arp;
using Task = build_a_queen::WebFontSourceTask;
using Phase = build_a_queen::WebFontSourcePhase;

// clang-format off
EM_JS(void, publishSample, (const char* publication, const char* hash), {
    const sample=Module.queenFontSample || (Module.queenFontSample={});
    sample[UTF8ToString(publication)]=UTF8ToString(hash);
});
EM_JS(void, publishResult, (double baselineBytes, double baselineLive, double retiredBytes,
    double retiredLive, int files, const char* error), {
    fetch('/result',{method:'POST',body:JSON.stringify({success:!error,cycles:error?0:20,
        baselineBytes,baselineLive,retiredBytes,retiredLive,files,sample:Module.queenFontSample,
        error:error?UTF8ToString(error):null,scope:'Font task/graph/Fetch and decoded CPU owners; reflection metadata remains resident',productionReady:false})});
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

class FontLifecycleProbe final {
public:
    explicit FontLifecycleProbe(std::string_view url) : url_(url)
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
                require(runtime_->dispatch(1).success(), "QUEEN-FONT-PROBE-SUBMIT");
                return false;
            }
            // A real Fetch has started. Destroy its task before dispatching
            // the accepted result; shutdown must retire the graph callback.
            task_.reset();
            retire();
            cancel_ = false;
            return ++cycles_ == 21;
        }
        require(runtime_->dispatch().success(), "QUEEN-FONT-PROBE-DISPATCH");
        task_->advance();
        if (task_->phase() == Phase::Pending)
            return false;
        require(task_->phase() == Phase::Ready, task_->diagnostic());
        require(task_->readCount() == build_a_queen::WebFontResources::inputs.size(),
            "QUEEN-FONT-PROBE-READ-COUNT");
        if (!cycles_)
            files_ = build_a_queen::WebFontResources::inputs.size();
        require(files_ == build_a_queen::WebFontResources::inputs.size(),
            "QUEEN-FONT-PROBE-FILE-COUNT");
        { // Hash bytes and the decoded owner must retire before measuring the baseline.
            auto owner = task_->resources();
            require(owner && owner->font() && owner->font()->valid(), "QUEEN-FONT-PROBE-READY");
            const auto& image = owner->font()->atlas().get()->image();
            const core::StringView text(
                reinterpret_cast<const char*>(image.rgba8.data()), image.rgba8.size());
            const auto hash = core::Sha256::digest(text);
            if (!cycles_) {
                std::copy(hash.begin(), hash.end(), identity_.begin());
                const auto digest = core::Sha256::hexDigest(text);
                publishSample("font.rgba8", digest.c_str());
            }
            require(
                std::equal(hash.begin(), hash.end(), identity_.begin()), "QUEEN-FONT-PROBE-PIXELS");
            task_.reset();
            // Retain decoded resources across IO shutdown, as the real renderer does.
            runtime_->shutdown();
            require(
                owner->font()->glyph('A') && owner->font()->glyph('~'), "QUEEN-FONT-PROBE-GLYPHS");
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
            queen_source_fixture::create(url_, build_a_queen::WebFontResources::maxFileBytes);
        runtime_ = std::move(fixture.owner);
        task_ = core::makeUnique<Task>(
            runtime_->fileService(), runtime_->completionQueue(), fixture.mount);
    }

    void retire()
    {
        runtime_->shutdown();
        require(!runtime_->completionQueue().pending() && !runtime_->completionQueue().reserved(),
            "QUEEN-FONT-PROBE-COMPLETIONS");
        runtime_.reset();
        const auto current = core::defaultCpuAllocator().stats();
        require(current.liveBytes == baseline_.liveBytes && live(current) == live(baseline_),
            "QUEEN-FONT-PROBE-BASELINE");
    }

    core::pmr::String url_;
    core::CpuMemoryStats baseline_{};
    std::array<std::uint8_t, 32> identity_{};
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
    core::UniquePtr<Task> task_;
    unsigned cycles_ = 0, files_ = 0;
    bool cancel_ = false;
};

core::UniquePtr<FontLifecycleProbe> probe;
core::CpuMemoryStats initial;
double deadline = 0;

void tick()
try {
    require(emscripten_get_now() < deadline, "QUEEN-FONT-PROBE-DEADLINE");
    if (!probe->tick())
        return;
    const auto files = probe->files();
    probe.reset();
    const auto retired = core::defaultCpuAllocator().stats();
    require(retired.liveBytes == initial.liveBytes && live(retired) == live(initial),
        "QUEEN-FONT-PROBE-OWNERS");
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
    probe = core::makeUnique<FontLifecycleProbe>(argv[1]);
    deadline = emscripten_get_now() + 120000;
    emscripten_set_main_loop(tick, 0, false);
}
