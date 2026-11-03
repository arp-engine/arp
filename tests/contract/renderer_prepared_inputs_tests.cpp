#include "core/io/http_file_backend.h"
#include "renderer/postprocess/display_transfer_renderer.h"
#include "renderer/postprocess/fxaa_renderer.h"
#include "renderer/postprocess/ssao_renderer.h"
#include "renderer/postprocess/taa_renderer.h"
#include "renderer/scene_2d_renderer.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/renderer_shader_sources.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using Preparation = renderer::ShaderPackagePreparationDesc;
constexpr rhi::Extent2D extent{32, 24};
constexpr auto format = rhi::TextureFormat::Rgba8Unorm;

class RejectSource final : public shader::ShaderArtifactSource {
public:
    shader::ShaderArtifactSourceResult read(
        const std::filesystem::path&, std::uint64_t) const override
    {
        ++reads;
        return {{}, "PREPARED-INPUT-UNEXPECTED-READ"};
    }

    mutable unsigned reads = 0;
};

void unchanged(rhi::GpuDevice& device, const rhi::MemoryStats& before)
{
    const auto after = device.memoryStats();
    assert(before.liveResourceCount == after.liveResourceCount
        && before.allocatedBytes == after.allocatedBytes);
}

template <class Fn> void rejectVariants(const Preparation& source, Fn test)
{
    test(Preparation{});
    auto invalid = source;
    invalid.prepared.reset();
    test(invalid);
    invalid = source;
    invalid.publication.clear();
    test(invalid);
    invalid = source;
    invalid.publication = "other/wrong.artifact-set.yaml";
    test(invalid);
    invalid = source;
    invalid.prepared = core::SharedPtr<const shader::GeneratedShaderArtifactSet>(
        source.prepared.get(), [](const auto*) {});
    test(invalid);
    invalid = source;
    RejectSource reject;
    invalid.budget.source = &reject;
    test(invalid);
    assert(!reject.reads);
}

core::UniquePtr<rhi::GpuTexture> texture(
    rhi::GpuDevice& device, const char* label, rhi::TextureFormat f)
{
    return device.createTexture({label,
        {extent.width, extent.height, 1},
        f,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment,
        rhi::MemoryUsage::GpuOnly,
        f == rhi::TextureFormat::D32Float ? rhi::ResourceState::DepthAttachment
                                          : rhi::ResourceState::ColorAttachment});
}

renderer::RenderFrameContext frame(rhi::GpuTexture& target)
{
    renderer::RenderFrameContext result;
    result.frame.success = true;
    result.colorTexture = &target;
    result.colorTarget.extent = extent;
    result.colorTarget.format = target.desc().format;
    result.colorTarget.finalState = rhi::ResourceState::ColorAttachment;
    return result;
}

template <class Renderer, class Desc>
void checkCreate(
    rhi::GpuDevice& device, Renderer& renderer, const Desc& desc, const Preparation& preparation)
{
    rejectVariants(preparation, [&](const auto& invalid) {
        const auto before = device.memoryStats();
        assert(!renderer.create(desc, invalid).success && !renderer.graphicsPipeline());
        unchanged(device, before);
    });
    const auto result = renderer.create(desc, preparation);
    if (!result.success)
        std::fprintf(stderr, "prepared create: %s\n", result.diagnostics.c_str());
    assert(result.success && renderer.graphicsPipeline());
    auto* pipeline = renderer.graphicsPipeline();
    assert(pipeline->shaderReflection().sourceSha256
        == preparation.prepared->module().reflection.sourceSha256);
    // Rejected preparation also cannot reset a live renderer.
    const auto before = device.memoryStats();
    assert(
        !renderer.create(desc, Preparation{}).success && renderer.graphicsPipeline() == pipeline);
    unchanged(device, before);
}

void cycle(const renderer_source_fixture::Inputs& inputs, const std::filesystem::path& localRoot)
{
    auto device = rhi::createNullDevice();
    assert(device);
    {
        renderer::FxaaRenderer fxaa(*device);
        core::pmr::String diagnostic;
        Preparation stageProof;
        stageProof.publication = localRoot / "stage-proof/fxaa.artifact-set.yaml";
        stageProof.prepared = shader::GeneratedShaderArtifactSet::load(stageProof.publication,
            {renderer_source_fixture::fileBytes,
                renderer_source_fixture::totalBytes,
                renderer_source_fixture::totalBytes,
                32,
                8},
            core::makeShared<core::CpuMemoryQuota>(renderer_source_fixture::totalBytes),
            diagnostic);
        assert(stageProof.prepared);
        const auto beforeStage = device->memoryStats();
        const auto stageRejected = fxaa.create(
            {core::FilePath::utf8(localRoot / "stage-proof/fxaa.slang"), format, extent},
            stageProof);
        assert(!stageRejected.success
            && stageRejected.diagnostics == "RENDERER-PREPARED-SHADER-PUSH-ABI");
        unchanged(*device, beforeStage);
        checkCreate(
            *device, fxaa, renderer::FxaaRendererDesc{"fxaa.slang", format, extent}, inputs[0]);
        renderer::TaaRenderer taa(*device);
        checkCreate(
            *device, taa, renderer::TaaRendererDesc{"wocc_taa.slang", format, extent}, inputs[1]);
        renderer::SsaoRenderer ssao(*device);
        checkCreate(*device,
            ssao,
            renderer::SsaoRendererDesc{"ssao.slang", rhi::TextureFormat::R8Unorm, extent},
            inputs[2]);
        // A different original owner is valid in isolation but cannot satisfy
        // this renderer's module/entry/push-constant ABI.
        const auto beforeWrong = device->memoryStats();
        assert(!fxaa.create({"fxaa.slang", format, extent}, inputs[1]).success);
        unchanged(*device, beforeWrong);
        assert(taa.resizeHistory({64, 48}).success && taa.resizeHistory(extent).success);
        auto color = texture(*device, "Prepared.Color", format);
        auto output = texture(*device, "Prepared.Output", format);
        auto depth = texture(*device, "Prepared.Depth", rhi::TextureFormat::D32Float);
        auto velocity = texture(*device, "Prepared.Velocity", rhi::TextureFormat::Rg16Float);
        auto reactive = texture(*device, "Prepared.Reactive", rhi::TextureFormat::R8Unorm);
        auto moments = texture(*device, "Prepared.Moments", rhi::TextureFormat::Rg16Float);
        auto ao = texture(*device, "Prepared.Ao", rhi::TextureFormat::R8Unorm);
        assert(color && output && depth && velocity && reactive && moments && ao);
        assert(fxaa.drawFrame(frame(*output), *color).success);
        assert(ssao.drawFrame(frame(*ao), *depth).success);
        assert(
            taa.drawFrame(frame(*output), *color, *velocity, *depth, *reactive, *moments).success);
        device->waitIdle();

        renderer::DisplayTransferRenderer transfer;
        rejectVariants(inputs[3], [&](const auto& invalid) {
            const auto before = device->memoryStats();
            bool rejected = false;
            try {
                transfer.create(*device,
                    "display_transfer.slang",
                    format,
                    extent,
                    1,
                    rhi::semantics::TextureUvYTransform::Identity,
                    invalid);
            } catch (const std::exception&) {
                rejected = true;
            }
            assert(rejected);
            unchanged(*device, before);
        });
        transfer.create(*device,
            "display_transfer.slang",
            format,
            extent,
            1,
            rhi::semantics::TextureUvYTransform::Identity,
            inputs[3]);
        transfer.resize({64, 48});
        transfer.resize(extent);
        auto encoder = device->createCommandEncoder({"Prepared.DisplayTransfer"});
        assert(encoder);
        assert(encoder
                ->transition(*transfer.linearTarget(0),
                    rhi::ResourceState::Undefined,
                    rhi::ResourceState::ShaderRead)
                .success);
        rhi::RenderPassDesc passDesc;
        passDesc.label = "Prepared.DisplayTransfer";
        rhi::RenderPassColorAttachmentDesc attachment;
        attachment.label = "Prepared.Output";
        attachment.target = output->id();
        attachment.format = format;
        attachment.loadOp = rhi::LoadOp::Clear;
        attachment.storeOp = rhi::StoreOp::Store;
        attachment.clearColor = {0, 0, 0, 1};
        passDesc.colorAttachments.push_back(attachment);
        auto pass = encoder->beginRenderPass(passDesc);
        assert(pass);
        transfer.record(*pass, 0);
        assert(pass->end().success);
        auto commands = encoder->finish();
        assert(commands && device->queue().submit(*commands).success);
        device->waitIdle();
    }
    {
        resource::ResourceManager resources;
        renderer::Scene2DRenderer sprites(*device, resources);
        renderer::Scene2DRendererDesc desc{"scene_2d_sprites.slang", format, 16, 2, 2};
        rejectVariants(inputs[4], [&](const auto& invalid) {
            const auto before = device->memoryStats();
            assert(!sprites.create(desc, invalid).success);
            unchanged(*device, before);
        });
        const auto before = device->memoryStats();
        const auto rejected = sprites.create(desc, inputs[4]);
        assert(!rejected.success && rejected.diagnostics == "ARP-2D-NULL-VALIDATOR-ONLY");
        unchanged(*device, before);
    }
    // The real cold files are available here. Missing prepared ownership still
    // rejects instead of borrowing them. Existing cold overloads remain usable.
    {
        renderer::FxaaRenderer fxaa(*device);
        const renderer::FxaaRendererDesc desc{
            core::FilePath::utf8(localRoot / "fxaa.slang"), format, extent};
        const auto before = device->memoryStats();
        assert(!fxaa.create(desc, Preparation{}).success);
        unchanged(*device, before);
        assert(fxaa.create(desc).success);
    }
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    const bool http = std::strcmp(argv[1], "--http") == 0;
    const std::filesystem::path root = http ? ARP_PREPARED_FIXTURE_DIR : argv[1];
    shader_source_fixture::Files files(
        root, renderer_source_fixture::fileBytes, renderer_source_fixture::names.size() * 32);
    for (auto name : renderer_source_fixture::names)
        assert(!std::filesystem::exists(core::pmr::String(name) + ".slang"));
    renderer_source_fixture::Inputs inputs;
    if (http) {
        assert(argc == 3);
        core::io::HttpFileBackendDesc desc;
        desc.baseUrl = argv[2];
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = 2 * renderer_source_fixture::fileBytes;
        desc.maxPathBytes = 512;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 16384;
        desc.timeoutMs = 5000;
        desc.cancellationPollMs = 10;
        inputs = renderer_source_fixture::acquire(
            core::makeShared<core::io::HttpFileBackend>(std::move(desc)));
    } else {
        inputs = renderer_source_fixture::acquire(files);
    }
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        rhi::ShaderPackageDesc candidate;
        assert(renderer::detail::prepareGraphicsShaderPackage(candidate,
            "Targets",
            inputs[i].prepared->filename(shader::ShaderArtifactRole::Source),
            inputs[i].prepared->module().reflection.pushConstantSizeBytes,
            inputs[i])
                .success);
        for (const auto target : {shader::TargetBackend::SpirV,
                 shader::TargetBackend::Msl,
                 shader::TargetBackend::Wgsl}) {
            rhi::GeneratedShaderTargetInput selected;
            assert(rhi::selectGeneratedShaderPackageTarget(candidate, target, selected).success);
            assert(selected.bytes && !selected.bytes->empty());
        }
        candidate.label = "RetainedOnFailure";
        const auto result = renderer::detail::prepareGraphicsShaderPackage(candidate,
            "WrongAbi",
            inputs[i].prepared->filename(shader::ShaderArtifactRole::Source),
            inputs[i].prepared->module().reflection.pushConstantSizeBytes + 4,
            inputs[i]);
        assert(!result.success && result.diagnostics == "RENDERER-PREPARED-SHADER-PUSH-ABI");
        assert(candidate.label == "RetainedOnFailure");
    }
    cycle(inputs, root);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < (!http && argc > 2 ? 20u : 1u); ++i)
        cycle(inputs, root);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Prepared five-renderer core=%llu/%llu -> %llu/%llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)after.liveBytes,
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
