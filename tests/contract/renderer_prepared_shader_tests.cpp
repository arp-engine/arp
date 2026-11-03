#include "core/io/filesystem_path.h"
#include "renderer/ui/gui_renderer.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/shader_source_files.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
constexpr const char* publication = "prepared/gui_overlay.artifact-set.yaml";
constexpr const char* module = "prepared/gui_overlay.slang";

class Source final : public shader::ShaderArtifactSource {
public:
    explicit Source(const shader_source_fixture::Files& files) : files_(files) {}

    shader::ShaderArtifactSourceResult read(
        const std::filesystem::path& path, std::uint64_t) const override
    {
        ++reads;
        const auto name = core::FilePath::utf8(path.lexically_relative("prepared"));
        for (const auto& file : files_.files)
            if (file.path == name)
                return {file.bytes, nullptr};
        return {{}, "PREPARED-TEST-NOT-ACQUIRED"};
    }

    mutable unsigned reads = 0;

private:
    const shader_source_fixture::Files& files_;
};

void cycle(const shader_source_fixture::Files& files)
{
    Source source(files);
    core::pmr::String diagnostic;
    shader::GeneratedShaderArtifactSetBudget budget{
        4 * 1024 * 1024, 16 * 1024 * 1024, 16 * 1024 * 1024, 32, 8, &source};
    auto owner = shader::GeneratedShaderArtifactSet::load(publication,
        budget,
        core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes),
        diagnostic);
    if (!owner)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(owner && source.reads > 1);
    const auto reads = source.reads;
    renderer::ShaderPackagePreparationDesc preparation;
    preparation.publication = publication;
    preparation.prepared = owner;
    rhi::ShaderPackageDesc package;
    assert(renderer::detail::prepareGraphicsShaderPackage(
        package, "PreparedGui", module, sizeof(renderer::GuiPushConstants), preparation)
            .success);
    assert(package.generatedArtifactSet == owner && package.generatedReflectionHydrated);
    const auto hash = rhi::stableShaderPackageHash(package);
    auto invalid = preparation;
    invalid.prepared.reset();
    assert(!renderer::detail::prepareGraphicsShaderPackage(package, "Missing", module, 64, invalid)
            .success);
    assert(rhi::stableShaderPackageHash(package) == hash);
    invalid = preparation;
    invalid.publication.clear();
    assert(!renderer::detail::prepareGraphicsShaderPackage(package, "Missing", module, 64, invalid)
            .success);
    invalid = preparation;
    invalid.budget.source = &source;
    assert(
        !renderer::detail::prepareGraphicsShaderPackage(package, "Ambiguous", module, 64, invalid)
            .success);
    invalid = preparation;
    invalid.publication = "other/gui_overlay.artifact-set.yaml";
    assert(!renderer::detail::prepareGraphicsShaderPackage(package, "Wrong", module, 64, invalid)
            .success);
    assert(!renderer::detail::prepareGraphicsShaderPackage(
        package, "Wrong", "prepared/wrong.slang", 64, preparation)
            .success);
    invalid = preparation;
    invalid.prepared =
        core::SharedPtr<const shader::GeneratedShaderArtifactSet>(owner.get(), [](const auto*) {});
    assert(!renderer::detail::prepareGraphicsShaderPackage(package, "Forged", module, 64, invalid)
            .success);
    assert(rhi::stableShaderPackageHash(package) == hash && source.reads == reads);

    auto device = rhi::createNullDevice();
    renderer::GuiRendererDesc desc;
    desc.shaderPath = module;
    desc.colorFormat = rhi::TextureFormat::Rgba8Unorm;
    desc.extent = {320, 180};
    desc.shaderPublication = publication;
    desc.preparedShaderArtifacts = owner;
    desc.hasAtlasTexture = true;
    desc.atlasTexture.resourceId = "PreparedGui.ExplicitWhite";
    desc.atlasTexture.extent = {1, 1};
    desc.atlasTexture.rgba8 = {255, 255, 255, 255};
    renderer::GuiRenderer gui(*device);
    auto rejected = desc;
    rejected.preparedShaderArtifacts.reset();
    const auto before = device->memoryStats();
    assert(!gui.create(rejected).success && !gui.graphicsPipeline());
    assert(device->memoryStats().liveResourceCount == before.liveResourceCount
        && device->memoryStats().allocatedBytes == before.allocatedBytes);
    rejected = desc;
    rejected.shaderPublication.clear();
    assert(!gui.create(rejected).success && !gui.graphicsPipeline());
    auto result = gui.create(desc);
    if (!result.success)
        std::fprintf(stderr, "GUI prepared: %s\n", result.diagnostics.c_str());
    assert(result.success && gui.graphicsPipeline());
    assert(gui.graphicsPipeline()->shaderReflection().sourceSha256
        == owner->module().reflection.sourceSha256);
    assert(gui.onResize({640, 360}).success);
    assert(gui.onDeviceLost().success);
    assert(gui.recoverFromDeviceLost(desc).success);
    auto target = device->createTexture({"PreparedGui.HiddenTarget",
        {320, 180, 1},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::RenderAttachment,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::ColorAttachment});
    assert(target);
    renderer::RenderFrameContext frame;
    frame.frame.success = true;
    frame.colorTexture = target.get();
    frame.colorTarget.extent = desc.extent;
    frame.colorTarget.format = desc.colorFormat;
    frame.colorTarget.finalState = rhi::ResourceState::ColorAttachment;
    core::pmr::Vector<renderer::ui::UiDrawCommand> commands;
    renderer::ui::UiDrawCommand box;
    box.rect = box.clipRect = {0, 0, 100, 60};
    commands.push_back(std::move(box));
    auto encoder = device->createCommandEncoder({"PreparedGui.HiddenFrame"});
    assert(encoder);
    result = gui.recordUiCommands(*encoder, frame, commands);
    if (!result.success)
        std::fprintf(stderr, "GUI record: %s\n", result.diagnostics.c_str());
    assert(result.success);
    auto buffer = encoder->finish();
    assert(buffer && device->queue().submit(*buffer).success);
    assert(gui.passTelemetry().recorded && gui.passTelemetry().commandCount == 1);
    assert(source.reads == reads);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    shader_source_fixture::Files files(argv[1], 4 * 1024 * 1024, 32);
    // The fixture's source callback is the only reader of this deliberately
    // absent logical directory. GUI and resize cannot resolve a local file.
    assert(!std::filesystem::exists(module));
    cycle(files);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < (argc > 2 ? 20u : 1u); ++i)
        cycle(files);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::puts(
        "Prepared renderer/GUI source-only, atomic failure, Null pipeline, resize/recovery: PASS");
}
