#include "tests/fixtures/audio_memory_sources.h"

#include <cstdio>
#include <emscripten.h>

namespace {
using namespace arp;

// clang-format off
EM_JS(void, publishAudio, (double beforeBytes, double beforeLive, double afterBytes,
    double afterLive, double peak, const char* error), {
    fetch('/result',{method:'POST',body:JSON.stringify({success:!error,cycles:error?0:20,
        files:4,beforeBytes,beforeLive,afterBytes,afterLive,decoderPeak:peak,
        sourceRequests:Module.audioRequests,pcmMatchesGolden:!error,
        error:error?UTF8ToString(error):null,productionReady:false})});
});
// clang-format on

class AudioLifecycle final {
public:
    explicit AudioLifecycle(std::string_view url) : url_(url) {}

    bool tick()
    {
        if (!cycle_)
            cycle_ = core::makeUnique<audio_fixture::SourceCycle>(url_);
        if (!cycle_->tick())
            return false;
        peak_ = std::max(peak_, cycle_->peak());
        cycle_.reset();
        const auto after = core::defaultCpuAllocator().stats();
        if (!cycles_)
            baseline_ = after;
        else
            audio_fixture::require(
                after.liveBytes == baseline_.liveBytes && live(after) == live(baseline_),
                "AUDIO-FETCH-LIFECYCLE-BASELINE");
        if (++cycles_ < 21)
            return false;
        publishAudio(
            baseline_.liveBytes, live(baseline_), after.liveBytes, live(after), peak_, nullptr);
        return true;
    }

private:
    static std::uint64_t live(const core::CpuMemoryStats& value)
    {
        return value.allocationCount - value.deallocationCount;
    }

    core::pmr::String url_;
    core::UniquePtr<audio_fixture::SourceCycle> cycle_;
    core::CpuMemoryStats baseline_;
    std::uint64_t peak_ = 0;
    unsigned cycles_ = 0;
};

core::UniquePtr<AudioLifecycle> lifecycle;

void tick()
{
    try {
        if (lifecycle->tick()) {
            lifecycle.reset();
            emscripten_cancel_main_loop();
        }
    } catch (const std::exception& error) {
        publishAudio(0, 0, 0, 0, 0, error.what());
        lifecycle.reset();
        emscripten_cancel_main_loop();
    } catch (...) {
        publishAudio(0, 0, 0, 0, 0, "AUDIO-FETCH-LIFECYCLE-EXCEPTION");
        lifecycle.reset();
        emscripten_cancel_main_loop();
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    lifecycle = core::makeUnique<AudioLifecycle>(argv[1]);
    emscripten_set_main_loop(tick, 0, 1);
}
