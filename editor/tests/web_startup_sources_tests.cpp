#include "core/diagnostics/diagnostic_error.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/io/memory_file_backend.h"
#include "editor/app/editor_web_startup_host.h"
#include "editor/viewport/editor_scene_viewport_pipeline_contract.h"
#include "editor/viewport/editor_scene_viewport_sprite_contract.h"
#include "editor_web_startup_identity.h"
#include "ui/ui_font_source.h"

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
using Sources = editor::EditorWebStartupSources;
using Limits = editor::EditorWebStartupLimits;
namespace golden = tests::editor_web_startup_identity;

void require(bool value, const char* diagnostic)
{
    if (!value)
        throw core::DiagnosticError(diagnostic);
}

template <typename Bytes> core::pmr::String digest(const Bytes& bytes)
{
    return core::Sha256::hexDigest({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

void verify(const Sources& source)
{
    core::pmr::String diagnostic;
    require(source.ui().valid(diagnostic), "EDITOR-WEB-FETCH-UI");
    require(source.ui().fonts[0].source == source.ui().fonts[1].source
            && source.ui().fonts[1].weight == 600 && source.ui().fonts[0].weight == 400,
        "EDITOR-WEB-FETCH-FONT-ALIAS");
    require(digest(source.ui().baseAtlas.rgba8) == golden::atlas, "EDITOR-WEB-FETCH-ATLAS-HASH");
    for (const auto& file : golden::files) {
        if (file.role == 2) {
            const auto bytes = source.shaders().read(file.path, Limits::maxShaderFileBytes);
            require(!bytes.diagnostic && digest(bytes.bytes) == file.hash,
                "EDITOR-WEB-FETCH-SHADER-HASH");
        } else if (file.role == 1) {
            bool found = false;
            for (const auto& face : source.ui().fonts)
                if (face.source->desc().path == file.path) {
                    require(digest(face.source->bytes()) == file.hash
                            && ui::UiFontFaceLoader().load(face).ok(),
                        "EDITOR-WEB-FETCH-FONT-HASH");
                    found = true;
                }
            require(found, "EDITOR-WEB-FETCH-FONT-MISSING");
        } else if (!std::strcmp(file.path, "editor.arpui"))
            require(
                digest(source.ui().documentYaml) == file.hash, "EDITOR-WEB-FETCH-DOCUMENT-HASH");
    }
    require(
        ui::UiArpuiRuntimeLoader().load(source.ui().document()).ok(), "EDITOR-WEB-FETCH-DOCUMENT");
    require(editor::validateEditorSceneViewportSpriteReflection(source.spriteReflection()),
        "EDITOR-WEB-FETCH-SPRITE-ABI");
    const auto surface = editor::EditorSceneViewportSurfaceProfile::baseline();
    require(
        editor::buildEditorSceneViewportPipelineContract(
            source.pbrReflection(), "editor/viewport_pbr.slang", surface.color, surface.depth, 1)
            .valid(),
        "EDITOR-WEB-FETCH-PBR-ABI");
    // Fail-closed projection: an absent publication cannot read a host file.
    require(source.shaders().read("not-present.yaml", Limits::maxShaderFileBytes).diagnostic,
        "EDITOR-WEB-FETCH-MISSING-PROJECTION");
}

enum class Fault {
    None,
    Missing,
    Alias,
    Program,
    Module,
    Cancel,
    Destroy,
    CancelInFlight,
    DestroyInFlight
};

class Cycle final {
public:
    explicit Cycle(core::UniquePtr<editor::EditorWebStartupHost> host, Fault fault)
        : host_(std::move(host)), fault_(fault)
    {
        if (fault == Fault::CancelInFlight || fault == Fault::DestroyInFlight)
            host_->poll();
        if (fault == Fault::Cancel)
            host_->cancel();
        if (fault == Fault::Destroy)
            host_.reset();
    }

    bool poll()
    {
        if (fault_ == Fault::CancelInFlight || fault_ == Fault::DestroyInFlight) {
            // Let a browser event-loop turn actually start the Fetch before
            // retiring it. Immediate shutdown may only cancel a queued producer.
            if (!retirementYielded_) {
                retirementYielded_ = true;
                return false;
            }
            if (fault_ == Fault::DestroyInFlight) {
                host_.reset();
                return true;
            }
            host_->cancel();
        }
        if (!host_)
            return true;
        host_->poll();
        if (!host_->done())
            return false;
        auto owner = host_->sources();
        rejection_ = host_->diagnostic();
        if (fault_ == Fault::None) {
            if (!host_->ready())
                throw core::DiagnosticError(rejection_);
            require(owner && host_->result().telemetry.submittedFileReads == golden::files.size(),
                "EDITOR-WEB-FETCH-COUNT");
        } else
            require(
                !host_->ready() && !owner && !rejection_.empty(), "EDITOR-WEB-FETCH-FAIL-CLOSED");
        // Retire all IO/callbacks/ResourceManager before using immutable inputs.
        host_.reset();
        if (owner)
            verify(*owner);
        return true;
    }

    const core::pmr::String& rejection() const { return rejection_; }

private:
    core::UniquePtr<editor::EditorWebStartupHost> host_;
    Fault fault_;
    bool retirementYielded_ = false;
    core::pmr::String rejection_;
};

#if !defined(__EMSCRIPTEN__)
class Corpus final {
public:
    Corpus()
    {
        for (const auto& input : golden::files) {
            auto read = core::readBinaryFile(input.host, Limits::maxFileBytes);
            require(read.success && digest(read.bytes) == input.hash, "EDITOR-WEB-CORPUS-IDENTITY");
            bytes_.push_back(std::move(read.bytes));
        }
    }

    core::UniquePtr<editor::EditorWebStartupHost> host(Fault fault) const
    {
        using namespace core::io;
        std::array<core::SharedPtr<ManagedFileBackend>, 3> backends;
        std::array<core::SharedPtr<MemoryFileBackend>, 3> memory;
        for (std::size_t i = 0; i < 3; ++i) {
            memory[i] = core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{
                Limits::maxFiles, Limits::maxSourceBytes, 1, Limits::maxFileBytes, 512});
            backends[i] = memory[i];
        }
        for (const auto* directory : {"Vertex",
                 "Fragment",
                 "environment",
                 "environment/Vertex",
                 "environment/Fragment",
                 "sprite",
                 "sprite/Vertex",
                 "sprite/Fragment",
                 "pbr",
                 "pbr/Vertex",
                 "pbr/Fragment"})
            require(memory[2]->registerDirectory(directory) == FileIoStatus::Success,
                "EDITOR-WEB-CORPUS-DIRECTORY");
        for (std::size_t i = 0; i < golden::files.size(); ++i) {
            const auto& file = golden::files[i];
            if (fault == Fault::Missing && !std::strcmp(file.path, "cjk.ttf"))
                continue;
            auto bytes = bytes_[i];
            if (fault == Fault::Alias && !std::strcmp(file.path, Limits::profile)) {
                using Y = core::YamlContract;
                auto node = Y::parseSingleDocument(
                    {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
                node["fonts"][1]["maxBytes"] = 1;
                const auto text = Y::serialize(node);
                bytes.assign(text.begin(), text.end());
            }
            if ((fault == Fault::Program && std::strstr(file.path, "vertexMain.entry.glsl-es-100")
                    && !std::strstr(file.path, ".yaml"))
                || (fault == Fault::Module
                    && !std::strcmp(file.path, "pbr/editor_viewport_pbr.reflection.yaml")))
                bytes[0] ^= 0xff;
            require(memory[file.role]->registerFile(file.path, bytes.data(), bytes.size())
                    == FileIoStatus::Success,
                "EDITOR-WEB-CORPUS-REGISTER");
        }
        return core::makeUnique<editor::EditorWebStartupHost>(backends);
    }

private:
    core::pmr::Vector<core::pmr::Vector<std::uint8_t>> bytes_;
};

void run(const Corpus& corpus, Fault fault)
{
    Cycle cycle(corpus.host(fault), fault);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!cycle.poll())
        require(std::chrono::steady_clock::now() < deadline, "EDITOR-WEB-NATIVE-DEADLINE");
}
#else
// clang-format off
EM_JS(void, noteMemory, (double bytes, double live, double backing, double miBytes, double miBlocks), {
    (Module.memory||(Module.memory=[])).push({bytes,live,backing,miBytes,miBlocks});
    Module.uiJournal.append('cycle '+Module.memory.length+' core='+bytes+' backing='+backing);
});
EM_JS(void, report, (const char* error, const char* rejection, int cycles), {
    globalThis.reportUi({success:!error,error:error?UTF8ToString(error):null,
        rejection:rejection?UTF8ToString(rejection):null,cycles,memory:Module.memory,
        sourceRequests:Module.uiRequests,productionReady:false});
});
// clang-format on

class Lifecycle final {
public:
    Lifecycle(std::string_view url, Fault fault) : url_(url), fault_(fault) {}

    void tick()
    {
        if (!cycle_) {
            std::array<core::pmr::String, 3> urls;
            for (auto& item : urls)
                item = url_;
            cycle_ = core::makeUnique<Cycle>(
                core::makeUnique<editor::EditorWebStartupHost>(urls), fault_);
        }
        if (!cycle_->poll())
            return;
        rejection_ = cycle_->rejection();
        cycle_.reset();
        const auto stats = core::defaultCpuAllocator().stats();
        std::uint64_t miBytes = 0, miBlocks = 0;
        std::array<std::uint64_t*, 2> heap{&miBytes, &miBlocks};
        require(mi_heap_visit_blocks(
                    mi_heap_get_default(),
                    true,
                    [](const mi_heap_t*,
                        const mi_heap_area_t*,
                        void* block,
                        std::size_t size,
                        void* context) {
                        if (block) {
                            const auto& heap =
                                *static_cast<std::array<std::uint64_t*, 2>*>(context);
                            *heap[0] += size;
                            ++*heap[1];
                        }
                        return true;
                    },
                    &heap),
            "EDITOR-WEB-FETCH-HEAP");
        const std::array<std::uint64_t, 4> current{
            stats.liveBytes, stats.allocationCount - stats.deallocationCount, miBytes, miBlocks};
        noteMemory(current[0], current[1], mallinfo().uordblks, current[2], current[3]);
        if (!cycles_)
            baseline_ = current;
        else
            require(current == baseline_, "EDITOR-WEB-FETCH-BASELINE");
        if (++cycles_ < (fault_ == Fault::None ? 21u : 2u))
            return;
        report(nullptr, rejection_.empty() ? nullptr : rejection_.c_str(), cycles_ - 1);
        emscripten_cancel_main_loop();
    }

private:
    core::pmr::String url_, rejection_;
    Fault fault_;
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
    require(argc == 3 && mi_option_get(mi_option_arena_reserve) == 0,
        "EDITOR-WEB-FETCH-ARGUMENTS-POLICY");
    Fault fault = Fault::None;
    if (!std::strcmp(argv[2], "missing"))
        fault = Fault::Missing;
    else if (!std::strcmp(argv[2], "module"))
        fault = Fault::Module;
    else if (!std::strcmp(argv[2], "cancel"))
        fault = Fault::Cancel;
    else if (!std::strcmp(argv[2], "destroy"))
        fault = Fault::Destroy;
    else if (!std::strcmp(argv[2], "cancel-inflight"))
        fault = Fault::CancelInFlight;
    else if (!std::strcmp(argv[2], "destroy-inflight"))
        fault = Fault::DestroyInFlight;
    else
        require(!std::strcmp(argv[2], "ok"), "EDITOR-WEB-FETCH-MODE");
    lifecycle = core::makeUnique<Lifecycle>(argv[1], fault);
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
    Corpus corpus;
    run(corpus, Fault::None);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run(corpus, Fault::None);
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Web startup core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        require(before.liveBytes == after.liveBytes
                && before.allocationCount - before.deallocationCount
                    == after.allocationCount - after.deallocationCount,
            "EDITOR-WEB-NATIVE-BASELINE");
    } else
        for (auto fault : {Fault::Missing,
                 Fault::Alias,
                 Fault::Program,
                 Fault::Module,
                 Fault::Cancel,
                 Fault::Destroy,
                 Fault::CancelInFlight,
                 Fault::DestroyInFlight})
            run(corpus, fault);
    std::puts(
        "PASS Web startup86, original ES100/module identities, aliases, retirement and faults");
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
