#include "core/diagnostics/diagnostic_error.h"
#include "core/hash/sha256.h"
#include "editor/ui/editor_ui_sources.h"
#include "editor_ui_source_identity.h"
#include "shader/generated_shader_artifact_set.h"
#include "tests/fixtures/queen_source_runtime.h"
#include "ui/ui_font_source.h"

#include <emscripten.h>
#include <malloc.h>
#include <mimalloc.h>

namespace {
using namespace arp;
using Sources = editor::EditorUiSources;
using Limits = editor::EditorUiSourceLimits;
namespace golden = tests::editor_ui_identity;

void require(bool value, const char* diagnostic)
{
    if (!value)
        throw core::DiagnosticError(diagnostic);
}

template <typename Bytes> core::pmr::String digest(const Bytes& bytes)
{
    return core::Sha256::hexDigest({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

struct HeapSample {
    std::uint64_t bytes = 0, blocks = 0;
};

HeapSample sampleHeap()
{
    HeapSample result;
    require(mi_heap_visit_blocks(
                mi_heap_get_default(),
                true,
                [](const mi_heap_t*,
                    const mi_heap_area_t*,
                    void* block,
                    std::size_t bytes,
                    void* context) {
                    auto& sample = *static_cast<HeapSample*>(context);
                    if (block) {
                        sample.bytes += bytes;
                        ++sample.blocks;
                    }
                    return true;
                },
                &result),
        "EDITOR-UI-FETCH-HEAP-SAMPLE");
    return result;
}

// clang-format off
EM_JS(void, noteUi, (double bytes, double live, double peak, double mallocBytes, double miBytes, double miBlocks), {
    (Module.uiMemory || (Module.uiMemory=[])).push({bytes,live,peak,mallocBytes,miBytes,miBlocks});
    Module.uiJournal.append('cycle '+Module.uiMemory.length+' core='+bytes+' backing='+mallocBytes);
});
EM_JS(void, noteMiStats, (const char* text), {
    const input=UTF8ToString(text);Module.miStats=(Module.miStats||"")+input;
    if(Module.miStats.length>16384)Module.miStats=Module.miStats.slice(0,16384);
});
EM_JS(void, publishUi, (double beforeBytes, double beforeLive, double afterBytes,
    double afterLive, double peakCore, double mallocBytes, int cycles, const char* rejection, const char* error), {
    globalThis.reportUi({success:!error,cycles,
        beforeBytes,beforeLive,afterBytes,afterLive,peakCore,mallocBytes,sourceRequests:Module.uiRequests,
        memory:Module.uiMemory,miStats:Module.miStats,typedOwnerAfterIo:!error&&!rejection,rejection:rejection?UTF8ToString(rejection):null,
        error:error?UTF8ToString(error):null,productionReady:false});
});
// clang-format on

// Explicitly bind all three roles to one test HTTP mount. Own callback state
// until the graph and IO have drained, then consume the independently held UI.
class UiCycle final {
public:
    UiCycle(std::string_view url, bool failure)
        : runtime_(queen_source_fixture::create(url, Limits::maxFileBytes)), failure_(failure)
    {
        executor_ = core::makeUnique<resource::ResourceGraphExecutor>(
            *manager_, runtime_.owner->fileService(), runtime_.owner->completionQueue());
        require(
            executor_->submit(Sources::request({runtime_.mount, runtime_.mount, runtime_.mount}),
                {[](void* context, resource::ResourceGraphExecutionResult result) {
                     auto& self = *static_cast<UiCycle*>(context);
                     self.result_ = std::move(result);
                     self.done_ = true;
                 },
                    this})
                == resource::ResourceGraphSubmitStatus::Accepted,
            "EDITOR-UI-FETCH-SUBMIT");
    }

    ~UiCycle()
    {
        executor_.reset();
        if (runtime_.owner)
            runtime_.owner->shutdown();
    }

    bool tick()
    {
        require(runtime_.owner->dispatch().success(), "EDITOR-UI-FETCH-DISPATCH");
        if (!done_)
            return false;
        auto owner = Sources::capture(*manager_);
        if (failure_) {
            require(!result_.resource.success && !owner && !result_.resource.diagnostics.empty(),
                "EDITOR-UI-FETCH-FAIL-CLOSED");
            rejection_ = result_.resource.diagnostics;
        } else {
            if (!result_.resource.success)
                throw core::DiagnosticError(result_.diagnosticId ? result_.diagnosticId
                        : result_.resource.diagnostics.empty()
                        ? "EDITOR-UI-FETCH-UNKNOWN"
                        : result_.resource.diagnostics.c_str());
            require(result_.resource.success && owner && result_.telemetry.submittedFileReads == 13,
                "EDITOR-UI-FETCH-PREPARE");
        }
        executor_.reset();
        runtime_.owner->shutdown();
        require(!runtime_.owner->completionQueue().pending()
                && !runtime_.owner->completionQueue().reserved(),
            "EDITOR-UI-FETCH-IO-RETIRED");
        runtime_.owner.reset();
        runtime_.mounts.reset();
        manager_.reset();
        if (owner)
            verify(*owner);
        return true;
    }

    const core::pmr::String& rejection() const noexcept { return rejection_; }

private:
    static void verify(const Sources& source)
    {
        core::pmr::String diagnostic;
        require(source.valid(diagnostic), "EDITOR-UI-FETCH-VALID");
        require(digest(source.documentYaml) == golden::document
                && digest(source.baseAtlas.rgba8) == golden::atlas,
            "EDITOR-UI-FETCH-UI-HASH");
        const auto document = ui::UiArpuiRuntimeLoader().load(source.document());
        require(
            document.ok() && document.document().nodes().size() > 400, "EDITOR-UI-FETCH-DOCUMENT");
        for (std::size_t i = 0; i < source.fonts.size(); ++i) {
            const auto& face = source.fonts[i];
            require(face.source && digest(face.source->bytes()) == golden::fonts[i]
                    && ui::UiFontFaceLoader().load(face).ok(),
                "EDITOR-UI-FETCH-FONT");
        }
        const auto& package = *source.guiShader.prepared;
        std::size_t i = 0;
        for (auto role : {shader::ShaderArtifactRole::Source,
                 shader::ShaderArtifactRole::Spirv,
                 shader::ShaderArtifactRole::Metal,
                 shader::ShaderArtifactRole::Wgsl,
                 shader::ShaderArtifactRole::Reflection})
            require(digest(package.bytes(role)) == golden::shader[i++], "EDITOR-UI-FETCH-SHADER");
    }

    queen_source_fixture::Runtime runtime_;
    core::UniquePtr<resource::ResourceManager> manager_ =
        core::makeUnique<resource::ResourceManager>();
    resource::ResourceGraphExecutionResult result_;
    core::UniquePtr<resource::ResourceGraphExecutor> executor_;
    core::pmr::String rejection_;
    bool failure_ = false, done_ = false;
};

class UiLifecycle final {
public:
    UiLifecycle(std::string_view url, bool failure) : url_(url), failure_(failure) {}

    bool tick()
    {
        if (!cycle_)
            cycle_ = core::makeUnique<UiCycle>(url_, failure_);
        if (!cycle_->tick())
            return false;
        rejection_ = cycle_->rejection();
        cycle_.reset();
        const auto after = core::defaultCpuAllocator().stats();
        const auto mallocBytes = mallinfo().uordblks;
        const auto heap = sampleHeap();
        noteUi(after.liveBytes, live(after), after.peakBytes, mallocBytes, heap.bytes, heap.blocks);
        if (!cycles_) {
            baseline_ = after;
            heapBaseline_ = heap;
        } else
            require(after.liveBytes == baseline_.liveBytes && live(after) == live(baseline_)
                    && heap.bytes == heapBaseline_.bytes && heap.blocks == heapBaseline_.blocks,
                "EDITOR-UI-FETCH-BASELINE");
        if (++cycles_ < (failure_ ? 2u : 21u))
            return false;
        publishUi(baseline_.liveBytes,
            live(baseline_),
            after.liveBytes,
            live(after),
            after.peakBytes,
            mallocBytes,
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
    core::UniquePtr<UiCycle> cycle_;
    core::CpuMemoryStats baseline_;
    HeapSample heapBaseline_;
    unsigned cycles_ = 0;
    bool failure_ = false;
};

core::UniquePtr<UiLifecycle> lifecycle;

void tick()
{
    try {
        if (lifecycle->tick()) {
            lifecycle.reset();
            emscripten_cancel_main_loop();
        }
    } catch (const std::exception& error) {
        const auto stats = core::defaultCpuAllocator().stats();
        noteUi(stats.liveBytes,
            stats.allocationCount - stats.deallocationCount,
            stats.peakBytes,
            mallinfo().uordblks,
            sampleHeap().bytes,
            sampleHeap().blocks);
        lifecycle.reset();
        // Failure-only diagnostic: inspect allocator cache after all probe
        // owners retire. Never used to turn a failed lifecycle into success.
        mi_collect(true);
        mi_stats_print_out(
            [](const char* text, void*) {
                noteMiStats(text);
            },
            nullptr);
        const auto retired = core::defaultCpuAllocator().stats();
        const auto heap = sampleHeap();
        noteUi(retired.liveBytes,
            retired.allocationCount - retired.deallocationCount,
            retired.peakBytes,
            mallinfo().uordblks,
            heap.bytes,
            heap.blocks);
        publishUi(0, 0, 0, 0, 0, 0, 0, nullptr, error.what());
        emscripten_cancel_main_loop();
    } catch (...) {
        publishUi(0, 0, 0, 0, 0, 0, 0, nullptr, "EDITOR-UI-FETCH-EXCEPTION");
        lifecycle.reset();
        emscripten_cancel_main_loop();
    }
}
} // namespace

int main(int argc, char** argv)
try {
    if (argc != 3)
        return 2;
    require(mi_option_get(mi_option_arena_reserve) == 0, "EDITOR-UI-FETCH-BACKING-POLICY");
    lifecycle = core::makeUnique<UiLifecycle>(argv[1], std::string_view(argv[2]) != "ok");
    emscripten_set_main_loop(tick, 0, 1);
} catch (const std::exception& error) {
    publishUi(0, 0, 0, 0, 0, 0, 0, nullptr, error.what());
    return 1;
} catch (...) {
    publishUi(0, 0, 0, 0, 0, 0, 0, nullptr, "EDITOR-UI-FETCH-STARTUP");
    return 1;
}
