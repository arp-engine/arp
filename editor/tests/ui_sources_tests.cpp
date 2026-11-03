#include "assets/image_decoder.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/app/editor_ui_source_host.h"
#include "editor/ui/editor_ui_assets.h"
#include "editor/ui/editor_ui_content_preparation.h"
#include "editor/workspace/editor_shell.h"
#include "ui/ui_font_source.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using Sources = editor::EditorUiSources;
using Limits = editor::EditorUiSourceLimits;
using Y = core::YamlContract;
using namespace core::io;

struct Corpus {
    struct File {
        core::pmr::String path;
        core::pmr::Vector<std::uint8_t> bytes;
    };

    core::pmr::Vector<File> files;
    core::pmr::String yaml;
    renderer::GuiAtlasTextureDesc atlas{core::pmr::String{&core::defaultCpuMemoryResource()}};

    explicit Corpus()
    {
        const std::filesystem::path uiRoot = ARP_EDITOR_UI_RESOURCE_DIR;
        const std::filesystem::path fontRoot = ARP_EDITOR_UI_FONT_DIR;
        const std::filesystem::path shaderRoot = ARP_EDITOR_UI_SHADER_DIR;
        auto add = [&](const std::filesystem::path& root, const core::pmr::String& name) {
            assert(core::FilePath::isSafeRelativePath(name));
            auto read = core::readBinaryFile(root / name, Limits::maxFileBytes);
            assert(read.success);
            files.push_back({name, std::move(read.bytes)});
        };
        add(uiRoot, Limits::profile);
        const auto profile = Y::parseSingleDocument(
            {reinterpret_cast<const char*>(files[0].bytes.data()), files[0].bytes.size()});
        const auto document = Y::text(profile, "document"), image = Y::text(profile, "atlas");
        add(uiRoot, document);
        add(uiRoot, image);
        auto text = core::readTextFile(uiRoot / document, Limits::maxDocumentBytes);
        assert(text.success);
        yaml = std::move(text.text);
        auto decoded = assets::decodeImageToRgba8(files[2].bytes, "image/png", false);
        assert(decoded.success);
        atlas.extent = {decoded.image.width, decoded.image.height};
        atlas.rgba8 = std::move(decoded.image.rgba8);
        for (const auto& font : Y::sequence(profile, "fonts"))
            add(fontRoot, Y::text(font, "path"));
        add(shaderRoot, Limits::guiPublication);
        const auto publication = Y::parseSingleDocument(
            {reinterpret_cast<const char*>(files.back().bytes.data()), files.back().bytes.size()});
        const auto& entries = Y::required(publication, "files");
        for (const auto* role : {"source", "spirv", "metal", "wgsl", "reflection"})
            add(shaderRoot, Y::text(Y::required(entries, role), "path"));
        assert(files.size() == 13);
    }
};

void verify(const Sources& input, const Corpus& corpus)
{
    core::pmr::String diagnostic;
    assert(input.valid(diagnostic) && input.documentYaml == corpus.yaml);
    assert(input.baseAtlas.rgba8 == corpus.atlas.rgba8);
    for (std::size_t i = 0; i < input.fonts.size(); ++i) {
        const auto& expected = corpus.files[3 + i];
        const auto& actual = input.fonts[i];
        assert(core::FilePath::utf8(actual.sourcePath) == expected.path);
        const auto bytes = actual.source->bytes();
        assert(bytes.size() == expected.bytes.size()
            && !std::memcmp(bytes.data(), expected.bytes.data(), bytes.size()));
        assert(ui::UiFontFaceLoader().load(actual).ok());
    }
    const auto loaded = ui::UiArpuiRuntimeLoader().load(input.document());
    assert(loaded.ok() && loaded.document().nodes().size() > 400);
    auto invalid = input;
    invalid.fonts[3].source.reset();
    assert(!invalid.valid(diagnostic));
}

enum class Fault {
    None,
    Missing,
    Version,
    Duplicate,
    Escape,
    EmptyUi,
    BadUi,
    Png,
    Font,
    FontBudget,
    Shader,
    Cancel,
    Destroy,
    Stale,
    Budget
};

core::SharedPtr<const Sources> execute(core::SharedPtr<ManagedFileBackend> backend, Fault fault)
{
    resource::ResourceManager manager;
    auto router = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{3, 1, Limits::maxFileBytes, 32, 512});
    editor::EditorUiSourceMounts mounts{router->mount("ui", backend),
        router->mount("fonts", backend),
        router->mount("shaders", backend)};
    assert(mounts.ui && mounts.fonts && mounts.shaders);
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.completionBudget = desc.completionQueueCapacity = 2;
    desc.fileBackend = router;
    auto runtime = core::makeUnique<core::runtime::AsyncRuntime>(std::move(desc));
    assert(runtime->valid());
    core::runtime::CancellationSource cancellation;
    auto work = Sources::request(mounts, cancellation.token());
    if (fault == Fault::Budget)
        work.budget.maxSourceBytes = 1;

    struct Completion {
        resource::ResourceGraphExecutionResult result;
        bool done = false;
    } completed;

    resource::ResourceGraphExecutor executor(
        manager, runtime->fileService(), runtime->completionQueue());
    const auto accepted = executor.submit(std::move(work),
        {[](void* context, resource::ResourceGraphExecutionResult result) {
             auto& state = *static_cast<Completion*>(context);
             state.result = std::move(result);
             state.done = true;
         },
            &completed});
    assert(accepted == resource::ResourceGraphSubmitStatus::Accepted);
    if (fault == Fault::Cancel || fault == Fault::Destroy)
        cancellation.cancel();
    if (fault == Fault::Stale)
        assert(router->unmount(mounts.ui));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    if (fault == Fault::Destroy)
        runtime.reset();
    else {
        while (!completed.done && std::chrono::steady_clock::now() < deadline)
            assert(runtime->dispatch().success());
        assert(completed.done);
        runtime->shutdown();
        assert(!runtime->completionQueue().pending() && !runtime->completionQueue().reserved());
        runtime.reset();
    }
    assert(!router->telemetry().inFlight);
    router.reset();
    backend.reset();
    auto input = Sources::capture(manager);
    if (fault == Fault::None) {
        if (!completed.result.resource.success)
            std::fprintf(stderr, "UI source: %s\n", completed.result.resource.diagnostics.c_str());
        assert(input && completed.result.resource.success
            && completed.result.telemetry.submittedFileReads == 13);
    } else {
        assert(!input && !completed.result.resource.success && completed.done);
        assert(completed.result.diagnosticId || !completed.result.resource.diagnostics.empty());
    }
    return input;
}

void cycle(const Corpus& corpus, Fault fault)
{
    auto memory = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{32, Limits::maxSourceBytes, 1, Limits::maxFileBytes, 512});
    for (const auto& file : corpus.files) {
        if (fault == Fault::Missing && file.path == "Deng.ttf")
            continue;
        auto bytes = file.bytes;
        if (file.path == Limits::profile
            && (fault == Fault::Version || fault == Fault::Duplicate || fault == Fault::Escape
                || fault == Fault::FontBudget)) {
            auto node =
                Y::parseSingleDocument({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            if (fault == Fault::Version)
                node["contractVersion"] = 99;
            if (fault == Fault::Duplicate)
                node["fonts"][1]["token"] = node["fonts"][0]["token"];
            if (fault == Fault::Escape)
                node["atlas"] = "../outside.png";
            if (fault == Fault::FontBudget)
                node["fonts"][0]["maxBytes"] = 1;
            const auto text = Y::serialize(node);
            bytes.assign(text.begin(), text.end());
        }
        if (fault == Fault::EmptyUi && file.path == "editor.arpui")
            bytes.clear();
        if ((fault == Fault::BadUi && file.path == "editor.arpui")
            || (fault == Fault::Png && file.path == "editor_atlas.png")
            || (fault == Fault::Font && file.path == "Deng.ttf")
            || (fault == Fault::Shader && file.path == "gui_overlay.spv"))
            bytes[0] ^= 0xff;
        assert(
            memory->registerFile(file.path, bytes.data(), bytes.size()) == FileIoStatus::Success);
    }
    auto input = execute(memory, fault);
    assert(!memory->telemetry().inFlight);
    memory.reset();
    if (fault == Fault::None)
        verify(*input, corpus);
}

void compareAssets(const Sources& input)
{
    const auto directory = std::filesystem::temp_directory_path()
        / ("arp-ui-sources-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(directory));
    {
        editor::EditorShell cold, prepared;
        assert(
            cold.load(std::filesystem::path(ARP_EDITOR_UI_RESOURCE_DIR) / "editor.arpui", directory)
                .ok());
        assert(prepared.loadPreparedUi(input.document(), directory).ok());
        auto coldDraw = cold.drawCommands(), newDraw = prepared.drawCommands();
        assert(coldDraw.ok() && newDraw.ok()
            && coldDraw.commands().size() == newDraw.commands().size());
        auto before =
            editor::EditorUiAssets::build(cold, ARP_EDITOR_UI_RESOURCE_DIR, coldDraw.commands());
        auto after = editor::EditorUiAssets::buildPrepared(prepared, input, newDraw.commands());
        if (!after.ok())
            std::fprintf(stderr, "UI atlas: %s\n", after.diagnostic().c_str());
        assert(before.ok() && after.ok());
        assert(before.atlas().atlas().rgba8 == after.atlas().atlas().rgba8);
        assert(before.atlas().atlas().extent.width == after.atlas().atlas().extent.width
            && before.atlas().atlas().extent.height == after.atlas().atlas().extent.height);
        auto empty = input.document();
        empty.sourceYaml.clear();
        const auto version = prepared.stateVersion();
        assert(
            !prepared.loadPreparedUi(empty, directory).ok() && prepared.stateVersion() == version);
    }
    std::filesystem::remove_all(directory);
}

// The shared parser keeps Native/1 strict while accepting only consistent,
// explicitly enabled Web font aliases. Preparation survives source retirement.
void sharedContent(const Corpus& corpus)
{
    using Preparation = editor::EditorUiContentPreparation;
    const core::PoolHandle mount{&corpus, 0, 1};
    const editor::EditorUiContentMounts mounts{mount, mount};
    const editor::EditorUiContentProfile profile{Limits::profile, Limits::guiPublication, true};
    auto node = Y::parseSingleDocument({reinterpret_cast<const char*>(corpus.files[0].bytes.data()),
        corpus.files[0].bytes.size()});
    node["fonts"][1]["path"] = node["fonts"][0]["path"];
    node["fonts"][1]["family"] = node["fonts"][0]["family"];
    auto sources = [&](const Y::Node& input) {
        resource::ResourceSourceSet result;
        for (const auto& file : corpus.files) {
            if (file.path == corpus.files[4].path)
                continue;
            resource::ResourceSourceBuffer source;
            source.path = file.path;
            source.pathScope = FilePathScope::MountedResource;
            source.mount = mount;
            source.bytes = file.bytes;
            if (file.path == Limits::profile) {
                const auto yaml = Y::serialize(input);
                source.bytes.assign(yaml.begin(), yaml.end());
            }
            assert(result.add(std::move(source)));
        }
        return result;
    };
    auto files = sources(node);
    resource::ResourceLoadContext context;
    context.sources = &files;
    context.requirePreloadedSources = true;
    core::pmr::String diagnostic;
    editor::EditorUiContentBindings binding;
    assert(Preparation::parse(context, mounts, profile, binding, diagnostic));
    assert(!Preparation::parse(
        context, mounts, {Limits::profile, Limits::guiPublication, false}, binding, diagnostic));
    editor::EditorUiContent content;
    assert(Preparation::prepare(context, mounts, binding, content, diagnostic));
    files.clear();
    assert(content.valid(diagnostic) && content.fonts[0].source == content.fonts[1].source);
    assert(content.fonts[1].weight == 600 && content.fonts[0].weight == 400);
    assert(content.processedByteCount()
        == content.documentYaml.size() + content.baseAtlas.rgba8.size()
            + corpus.files[3].bytes.size() + corpus.files[5].bytes.size()
            + corpus.files[6].bytes.size());
    for (const auto* field : {"family", "maxBytes", "token", "path"}) {
        auto bad = node;
        if (!std::strcmp(field, "family"))
            bad["fonts"][1][field] = "Other family";
        else if (!std::strcmp(field, "maxBytes"))
            bad["fonts"][1][field] = 1;
        else if (!std::strcmp(field, "token"))
            bad["fonts"][1][field] = bad["fonts"][0][field];
        else
            bad["fonts"][1][field] = bad["document"];
        files = sources(bad);
        assert(!Preparation::parse(context, mounts, profile, binding, diagnostic)
            && !diagnostic.empty());
    }
    // UI admission has no accidental dependency on a native GUI publication.
    auto native = Sources{};
    static_cast<editor::EditorUiContent&>(native) = content;
    assert(!native.valid(diagnostic));
}
} // namespace

int main(int argc, char** argv)
{
    Corpus corpus;
    sharedContent(corpus);
    if (argc > 1 && std::strcmp(argv[1], "--http") == 0) {
        assert(argc == 3);
        HttpFileBackendDesc desc;
        desc.baseUrl = argv[2];
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = 2 * Limits::maxFileBytes;
        desc.maxPathBytes = 512;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 16384;
        desc.timeoutMs = 5000;
        desc.cancellationPollMs = 10;
        const auto input = execute(core::makeShared<HttpFileBackend>(std::move(desc)), Fault::None);
        verify(*input, corpus);
    } else if (argc > 1
        && (std::strcmp(argv[1], "--native") == 0 || std::strcmp(argv[1], "--assets") == 0)) {
        core::SharedPtr<const Sources> input;
        core::pmr::String diagnostic;
        assert(editor::EditorUiSourceHost::prepare(ARP_EDITOR_UI_RESOURCE_DIR,
            ARP_EDITOR_UI_FONT_DIR,
            ARP_EDITOR_UI_SHADER_DIR,
            {},
            input,
            diagnostic));
        verify(*input, corpus);
        if (std::strcmp(argv[1], "--assets") == 0)
            compareAssets(*input);
    } else {
        for (const auto fault : {Fault::None,
                 Fault::Missing,
                 Fault::Version,
                 Fault::Duplicate,
                 Fault::Escape,
                 Fault::EmptyUi,
                 Fault::BadUi,
                 Fault::Png,
                 Fault::Font,
                 Fault::FontBudget,
                 Fault::Shader,
                 Fault::Cancel,
                 Fault::Destroy,
                 Fault::Stale,
                 Fault::Budget})
            cycle(corpus, fault);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < (argc > 1 ? 20u : 1u); ++i)
            cycle(corpus, Fault::None);
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("UI source core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts("PASS UI source13, original atlas/font bytes, strict YAML and post-IO owners");
}
