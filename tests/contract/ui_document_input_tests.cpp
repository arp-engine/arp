#include "core/container/generation_pool.h"
#include "core/io/file_io.h"
#include "tests/fixtures/ui_document_fixture.h"
#include "ui/ui_document.h"

#include <cassert>
#include <cstdio>

namespace {

using namespace arp;
constexpr std::size_t budget = 16u * 1024u;
constexpr const char* documentId = "11111111-1111-4111-8111-111111111111";

const char* irFixture()
{
    return R"yaml(type: ArpUiAuthoringIR
contractVersion: arpui.ir.v1
status: Contract Complete
canvas: { id: source-ir, width: 320, height: 180, dpi: 96 }
textures: []
nodes:
  - id: 1
    parent: 0
    type: Panel
    layer: root
    rect: { x: 0, y: 0, width: 320, height: 180 }
    style: panel
    state: enabled
    event: none
)yaml";
}

void add(resource::ResourceSourceSet& sources,
    const core::pmr::String& path,
    core::StringView bytes,
    resource::ResourceFileOrigin origin)
{
    resource::ResourceSourceBuffer buffer;
    buffer.path = path;
    buffer.pathScope = origin.pathScope;
    buffer.mount = origin.mount;
    buffer.bytes.assign(bytes.begin(), bytes.end());
    assert(sources.add(std::move(buffer)));
}

void documentAndIrInputs()
{
    core::GenerationPool<unsigned> provenance(1);
    resource::ResourceFileOrigin origin{
        core::io::FilePathScope::MountedResource, provenance.emplace(1)};
    resource::ResourceSourceSet sources;
    const core::pmr::String path = "documents/\xE4\xB8\xAD\xE6\x96\x87.arpui";
    add(sources, path, tests::uiDocumentFixture(), origin);
    add(sources, "documents/source.arpui.ir.yaml", irFixture(), origin);
    resource::ResourceLoadContext context;
    context.sources = &sources;
    context.requirePreloadedSources = true;
    context.maxDecodedBytes = budget;

    ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = std::filesystem::u8path(path.begin(), path.end());
    desc.maxDocumentBytes = budget;
    desc.origin = origin;
    desc.sourceContext = context;
    const auto loaded = ui::UiArpuiRuntimeLoader().load(desc);
    assert(loaded.ok() && loaded.runtime().controlCount() == 3);
    desc.origin.mount.generation++;
    assert(!ui::UiArpuiRuntimeLoader().load(desc).ok());
    desc.origin = origin;
    resource::ResourceSourceSet missing;
    desc.sourceContext.sources = &missing;
    const auto absent = ui::UiArpuiRuntimeLoader().load(desc);
    assert(!absent.ok());
    assert(absent.diagnostic().message.find("NOT-PRELOADED") != core::pmr::String::npos);

    // Supplied text is authoritative even with a valid source context/path.
    desc.sourceContext = context;
    desc.sourceYamlProvided = true;
    assert(!ui::UiArpuiRuntimeLoader().load(desc).ok());
    desc.sourceYaml = "\xEF\xBB\xBF";
    desc.sourceYaml += tests::uiDocumentFixture();
    assert(ui::UiArpuiRuntimeLoader().load(desc).ok());
    desc.maxDocumentBytes = desc.sourceYaml.size() - 1;
    const auto exceeded = ui::UiArpuiRuntimeLoader().load(desc);
    assert(!exceeded.ok() && exceeded.diagnostic().message == "UI-DOCUMENT-SOURCE-BUDGET");
    desc.maxDocumentBytes = budget;
    desc.sourceYaml.push_back('\0');
    const auto nul = ui::UiArpuiRuntimeLoader().load(desc);
    assert(!nul.ok() && nul.diagnostic().message.find("NUL") != core::pmr::String::npos);
    desc.sourceYaml = tests::uiDocumentFixture();
    desc.sourceYaml += "\xC0\xAF";
    const auto invalid = ui::UiArpuiRuntimeLoader().load(desc);
    assert(!invalid.ok() && invalid.diagnostic().message.find("UTF8") != core::pmr::String::npos);
    for (auto cap : {std::size_t{0}, SIZE_MAX}) {
        desc.maxDocumentBytes = cap;
        assert(!ui::UiArpuiRuntimeLoader().load(desc).ok());
    }

    ui::UiArpuiIrBuildDesc ir;
    ir.path = "documents/source.arpui.ir.yaml";
    ir.sourceContext = context;
    ir.origin = origin;
    ir.maxDocumentBytes = budget;
    const auto built = ui::UiArpuiIrBuilder().build(ir);
    if (!built.ok())
        std::fprintf(stderr, "IR input: %s\n", built.diagnostic().message.c_str());
    assert(built.ok());
    assert(ui::UiArpuiIrRuntimeLoader().load(ir).ok());
    ir.sourceYamlProvided = true;
    assert(!ui::UiArpuiIrBuilder().build(ir).ok());
    ir.sourceYaml = irFixture();
    assert(ui::UiArpuiIrBuilder().build(ir).ok());
    ir.sourceYaml = R"yaml(type: ArpUiAuthoringIR
contractVersion: arpui.ir.v1
status: Contract Complete
canvas: {id: x, width: 320, height: 180, dpi: 96}
textures: []
nodes:
 - {id: 1, parent: 0, type: Panel, layer: r, rect: {x: 0, y: 0, width: 320, height: 180}, style: x, state: enabled, event: none}
 - {id: 2, parent: 1, type: Label, layer: l, rect: {x: 0, y: 0, width: 32, height: 20}, style: x, state: enabled, event: none, text: hi}
)yaml";
    const auto expanded = ui::UiArpuiIrBuilder().build(ir);
    assert(expanded.ok() && expanded.documentYaml().size() > ir.sourceYaml.size());
    ir.maxDocumentBytes = ir.sourceYaml.size();
    const auto tooLarge = ui::UiArpuiIrBuilder().build(ir);
    assert(!tooLarge.ok() && tooLarge.diagnostic().message == "UI-DOCUMENT-GENERATED-BUDGET");
    ir.maxDocumentBytes = 0;
    assert(!ui::UiArpuiIrBuilder().build(ir).ok());
}

void packageInputs()
{
    ui::UiArpuiIrBuildDesc ir;
    ir.sourceYaml = irFixture();
    ir.sourceYamlProvided = true;
    ir.maxDocumentBytes = budget;
    const auto built = ui::UiArpuiIrBuilder().build(ir);
    assert(built.ok());
    ui::UiArpuiResourcePackageDesc document;
    document.resourceId = documentId;
    document.alias = "ui/input-test";
    document.status = "Contract Complete";
    document.documentYaml = built.documentYaml();
    const auto packaged = ui::UiArpuiResourcePackageBuilder().build(document);
    assert(packaged.success);
    resource::InternalAssetPackageDesc package;
    package.name = "ui-source-input";
    package.status = "Contract Complete";
    package.entries.push_back(packaged.entry);
    const std::filesystem::path directory = "artifacts/ui-document-source-input";
    assert(resource::InternalAssetPackageWriter().write(package, directory).success);

    resource::ResourceSourceSet sources;
    resource::AssetPackageSourceContext context;
    core::GenerationPool<unsigned> provenance(1);
    context.origin = {core::io::FilePathScope::MountedResource, provenance.emplace(1)};
    context.resources.sources = &sources;
    context.resources.requirePreloadedSources = true;
    context.resources.maxDecodedBytes = budget;
    for (const core::pmr::String& relative :
        {core::pmr::String("manifest.arpkg.yaml"), packaged.entry.envelopePath}) {
        const auto read = core::readBinaryFile(directory / relative.c_str(), budget);
        assert(read.success);
        add(sources,
            core::pmr::String("snapshot/pkg/") + relative,
            {reinterpret_cast<const char*>(read.bytes.data()), read.bytes.size()},
            context.origin);
    }
    // Logical snapshot has no corresponding native directory.
    ui::UiArpuiPackageRuntimeLoadDesc load;
    load.packageDirectory = "snapshot/pkg";
    load.resourceId = documentId;
    load.maxMetadataBytes = load.maxDocumentBytes = budget;
    load.maxResources = 1;
    load.sources = &context;
    const auto result = ui::UiArpuiPackageRuntimeLoader().load(load);
    if (!result.ok())
        std::fprintf(stderr, "Package input: %s\n", result.diagnostic().message.c_str());
    assert(result.ok() && result.runtime().controlCount() == 1);
    load.maxDocumentBytes = 1;
    assert(!ui::UiArpuiPackageRuntimeLoader().load(load).ok());
    load.maxDocumentBytes = budget;
    context.origin.mount.generation++;
    assert(!ui::UiArpuiPackageRuntimeLoader().load(load).ok());
    context.origin.mount.generation--;
    resource::ResourceSourceSet missing;
    context.resources.sources = &missing;
    assert(!ui::UiArpuiPackageRuntimeLoader().load(load).ok());
    load.maxMetadataBytes = 0;
    assert(!ui::UiArpuiPackageRuntimeLoader().load(load).ok());
}

} // namespace

int main()
{
    // Ordinary bounded lifecycle verification, not a stress benchmark.
    for (unsigned i = 0; i < 20; ++i) {
        documentAndIrInputs();
        packageInputs();
    }
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        documentAndIrInputs();
        packageInputs();
    }
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::puts(
        "UI document/IR input: 20 warmup +20 measured cycles; core live bytes/count returned to baseline; prepared package loaded");
}
