#include "examples/survivors/content_source_task.h"
#include "examples/survivors/gameplay_source_task.h"
#include "tests/fixtures/audio_memory_sources.h"
#include "tests/fixtures/survivors_gameplay_identity.h"

#include <cstdio>
#include <emscripten.h>

namespace {
using namespace arp;
using namespace survivors;
using Phase = examples::StartupResourcePhase;
using audio_fixture::require;

// clang-format off
EM_JS(void, publishContent, (double beforeBytes, double beforeLive, double afterBytes,
    double afterLive, double peak, int cycles, const char* rejection, const char* error), {
    fetch('/result',{method:'POST',body:JSON.stringify({success:!error,cycles,
        beforeBytes,beforeLive,afterBytes,afterLive,decoderPeak:peak,
        sourceRequests:Module.contentRequests,typedOwnerAfterIo:!error&&!rejection,
        rejection:rejection?UTF8ToString(rejection):null,
        error:error?UTF8ToString(error):null,productionReady:false})});
});
// clang-format on

// Executes the real Gameplay9 then Content10 tasks on one HTTP mount. No cold
// loader, local resource, preload, user script execution or GPU is involved.
class ContentCycle final {
public:
    ContentCycle(std::string_view url, bool expectFailure)
        : runtime_(queen_source_fixture::create(url, ContentSourceTask::maxSourceFileBytes)),
          expectFailure_(expectFailure)
    {
        gameplay_ = core::makeUnique<GameplaySourceTask>(
            runtime_.owner->fileService(), runtime_.owner->completionQueue(), runtime_.mount);
    }

    ~ContentCycle()
    {
        content_.reset();
        gameplay_.reset();
        if (runtime_.owner)
            runtime_.owner->shutdown();
    }

    bool tick()
    {
        require(runtime_.owner->dispatch().success(), "CONTENT-FETCH-DISPATCH");
        if (gameplay_) {
            gameplay_->advance();
            if (gameplay_->phase() == Phase::Pending)
                return false;
            require(gameplay_->phase() == Phase::Ready && gameplay_->readCount() == 9,
                "CONTENT-FETCH-GAMEPLAY");
            rules_ = gameplay_->preparation();
            gameplay_.reset();
            require(rules_ && rules_->rules.interpreter(), "CONTENT-FETCH-INTERPRETER");
            require(core::Sha256::hexDigest(core::StringView(rules_->rules.source()))
                        == tests::SurvivorsGameplayIdentity::sourceSha256
                    && core::Sha256::hexDigest(core::StringView(rules_->rules.configYaml()))
                        == tests::SurvivorsGameplayIdentity::configSha256,
                "CONTENT-FETCH-GAMEPLAY-IDENTITY");
            content_ = core::makeUnique<ContentSourceTask>(runtime_.owner->fileService(),
                runtime_.owner->completionQueue(),
                runtime_.mount,
                rules_->config);
        }
        content_->advance();
        if (content_->phase() == Phase::Pending)
            return false;
        if (expectFailure_) {
            require(content_->phase() != Phase::Ready && !content_->preparation()
                    && !content_->diagnostic().empty(),
                "CONTENT-FETCH-FAIL-CLOSED");
            rejection_ = content_->diagnostic();
        } else {
            require(content_->phase() == Phase::Ready && content_->readCount() == 10,
                "CONTENT-FETCH-PREPARE");
            prepared_ = content_->preparation();
        }
        content_.reset();
        rules_.reset();
        runtime_.owner->shutdown();
        require(!runtime_.owner->completionQueue().pending()
                && !runtime_.owner->completionQueue().reserved(),
            "CONTENT-FETCH-RETIRE");
        runtime_.owner.reset();
        runtime_.mounts.reset();
        if (prepared_)
            verify();
        return true;
    }

    std::uint64_t peak() const noexcept { return peak_; }

    const core::pmr::String& rejection() const noexcept { return rejection_; }

private:
    void verify()
    {
        require(prepared_->sourceIdentity()
                == "sha256-base64:dhXCPfKEJh4/W1bLou9TmiZXBmeyzvOGqvQFPefE/Gs=",
            "CONTENT-FETCH-IDENTITY");
        auto& value = prepared_->content();
        require(value.processedByteCount() == 1832027 && value.atlas.ready() && value.map.ready()
                && value.orbitPrefab.ready(),
            "CONTENT-FETCH-TYPED");
        const auto& image = value.atlas.get()->image();
        require(image.width == 512 && image.height == 256 && image.rgba8.size() == 512u * 256u * 4u,
            "CONTENT-FETCH-ATLAS");
        require(value.sprite("tile_0104").ready() && value.font() && !value.font()->empty()
                && !value.map.get()->chunks().empty() && !value.orbitPrefab.get()->yaml().empty(),
            "CONTENT-FETCH-REFERENCES");
        for (std::size_t i = 0; i < value.sounds.size(); ++i) {
            const auto pcm = value.sounds[i].get()->view();
            require(pcm.valid() && pcm.channels == 2
                    && pcm.frameCount * pcm.channels == audio_fixture::inputs[i].samples,
                "CONTENT-FETCH-PCM");
            require(core::Sha256::hexDigest({reinterpret_cast<const char*>(pcm.samples),
                        audio_fixture::inputs[i].samples * sizeof(float)})
                    == audio_fixture::inputs[i].pcmSha256,
                "CONTENT-FETCH-PCM-IDENTITY");
        }
        peak_ = value.audioDecoderPeakBytes();
        require(peak_ && peak_ <= ContentBindings::maxAudioDecoderBytes,
            "CONTENT-FETCH-DECODER-BUDGET");
    }

    queen_source_fixture::Runtime runtime_;
    core::UniquePtr<GameplaySourceTask> gameplay_;
    core::UniquePtr<ContentSourceTask> content_;
    core::SharedPtr<const PreparedGameplay> rules_;
    core::SharedPtr<PreparedContent> prepared_;
    core::pmr::String rejection_;
    std::uint64_t peak_ = 0;
    bool expectFailure_ = false;
};

class ContentLifecycle final {
public:
    ContentLifecycle(std::string_view url, bool failure) : url_(url), failure_(failure) {}

    bool tick()
    {
        if (!cycle_)
            cycle_ = core::makeUnique<ContentCycle>(url_, failure_);
        if (!cycle_->tick())
            return false;
        peak_ = std::max(peak_, cycle_->peak());
        rejection_ = cycle_->rejection();
        cycle_.reset();
        const auto after = core::defaultCpuAllocator().stats();
        if (!cycles_)
            baseline_ = after;
        else
            require(after.liveBytes == baseline_.liveBytes && live(after) == live(baseline_),
                "CONTENT-FETCH-LIFECYCLE-BASELINE");
        if (++cycles_ < (failure_ ? 2u : 21u))
            return false;
        publishContent(baseline_.liveBytes,
            live(baseline_),
            after.liveBytes,
            live(after),
            peak_,
            cycles_ - 1,
            failure_ ? rejection_.c_str() : nullptr,
            nullptr);
        return true;
    }

private:
    static std::uint64_t live(const core::CpuMemoryStats& value)
    {
        return value.allocationCount - value.deallocationCount;
    }

    core::pmr::String url_, rejection_;
    core::UniquePtr<ContentCycle> cycle_;
    core::CpuMemoryStats baseline_;
    std::uint64_t peak_ = 0;
    unsigned cycles_ = 0;
    bool failure_ = false;
};

core::UniquePtr<ContentLifecycle> lifecycle;

void tick()
{
    try {
        if (lifecycle->tick()) {
            lifecycle.reset();
            emscripten_cancel_main_loop();
        }
    } catch (const std::exception& error) {
        publishContent(0, 0, 0, 0, 0, 0, nullptr, error.what());
        lifecycle.reset();
        emscripten_cancel_main_loop();
    } catch (...) {
        publishContent(0, 0, 0, 0, 0, 0, nullptr, "CONTENT-FETCH-EXCEPTION");
        lifecycle.reset();
        emscripten_cancel_main_loop();
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    lifecycle = core::makeUnique<ContentLifecycle>(argv[1], std::string_view(argv[2]) != "ok");
    emscripten_set_main_loop(tick, 0, 1);
}
