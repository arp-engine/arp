#include "platform/render_system.h"
#include "renderer/postprocess/display_transfer_renderer.h"
#include "renderer/scene_2d_renderer.h"
#include "tests/fixtures/postprocess_fixture.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/renderer_shader_sources.h"
#include "tests/fixtures/scene_2d_fixture.h"

#include <cstdio>
#include <cstring>

namespace {
using namespace arp;

template <class Result> void checked(const Result& result)
{
    if (!result.success)
        throw std::runtime_error(result.diagnostics.c_str());
}

void feature(const tests::postprocess::PostProcessFixtureResult& result)
{
    if (!result.success)
        throw std::runtime_error(result.diagnostic.c_str());
    for (const auto& metric : result.attachmentMetrics) {
        assert(result.readbackDeferredToRenderDoc && !metric.available);
        std::printf("deferred-to-RenderDoc %s/%s=%g\n",
            metric.attachment.c_str(),
            metric.metric.c_str(),
            metric.value);
    }
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    try {
        const bool cold = argc > 1 && std::strcmp(argv[1], "--cold") == 0;
        const bool lifecycle = argc > 1 && std::strcmp(argv[1], "--lifecycle") == 0;
        shader_source_fixture::Files files(ARP_PREPARED_FIXTURE_DIR,
            renderer_source_fixture::fileBytes,
            renderer_source_fixture::names.size() * 32);
        auto inputs = renderer_source_fixture::acquire(files);
        for (const auto* name : renderer_source_fixture::names)
            assert(!std::filesystem::exists(core::pmr::String(name) + ".slang"));
        platform::WindowSystem platform;
        assert(platform.initialize().success);
        auto window = platform.createWindow({"Prepared renderer sources", 640, 360, true, true});
        assert(window.valid());
        auto system =
            platform::createDefaultRenderSystem(platform, window, "Prepared renderer sources");
        if (!system.valid())
            throw std::runtime_error(system.diagnostics().c_str());
        assert(system.backend() == rhi::Backend::Vulkan);
        auto& device = *system.gpuDevice();
        tests::RenderDocCaptureScope capture;
        const auto drawAll = [&] {
            constexpr auto replay = tests::postprocess::FixtureReadbackPolicy::RenderDocReplay;
            feature(tests::postprocess::runPostFeatureFixture(
                device, {32, 24}, cold ? nullptr : &inputs[0], replay));
            feature(tests::postprocess::runTaaFeatureFixture(
                device, {32, 24}, cold ? nullptr : &inputs[1], replay));
            feature(tests::postprocess::runSsaoFeatureFixture(
                device, {32, 24}, cold ? nullptr : &inputs[2], replay));
            {
                renderer::DisplayTransferRenderer transfer;
                constexpr rhi::Extent2D size{32, 24};
                const auto transform =
                    rhi::semantics::textureUvYTransformForBackend(device.backend());
                if (cold)
                    transfer.create(device,
                        ARP_SHADER_DIR "/display_transfer.slang",
                        rhi::TextureFormat::Rgba8Unorm,
                        size,
                        1,
                        transform);
                else
                    transfer.create(device,
                        "display_transfer.slang",
                        rhi::TextureFormat::Rgba8Unorm,
                        size,
                        1,
                        transform,
                        inputs[3]);
                auto output = device.createTexture({"Prepared.TransferOutput",
                    {size.width, size.height, 1},
                    rhi::TextureFormat::Rgba8Unorm,
                    rhi::TextureUsage::RenderAttachment,
                    rhi::MemoryUsage::GpuOnly,
                    rhi::ResourceState::Undefined});
                assert(output);
                auto encoder = device.createCommandEncoder({"Prepared.Transfer"});
                assert(encoder);
                checked(encoder->transition(*transfer.linearTarget(0),
                    rhi::ResourceState::Undefined,
                    rhi::ResourceState::ColorAttachment));
                rhi::RenderPassDesc clear;
                clear.label = "Prepared.TransferInput";
                rhi::RenderPassColorAttachmentDesc input;
                input.target = transfer.linearTarget(0)->id();
                input.format = rhi::TextureFormat::Rgba16Float;
                input.loadOp = rhi::LoadOp::Clear;
                input.storeOp = rhi::StoreOp::Store;
                input.clearColor = {0.18f, 0.35f, 0.7f, 1};
                clear.colorAttachments.push_back(input);
                auto clearPass = encoder->beginRenderPass(clear);
                assert(clearPass);
                checked(clearPass->end());
                checked(encoder->transition(*transfer.linearTarget(0),
                    rhi::ResourceState::ColorAttachment,
                    rhi::ResourceState::ShaderRead));
                checked(encoder->transition(
                    *output, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment));
                rhi::RenderPassDesc passDesc;
                passDesc.label = "Prepared.TransferOutput";
                input.target = output->id();
                input.format = output->desc().format;
                passDesc.colorAttachments.push_back(input);
                auto pass = encoder->beginRenderPass(passDesc);
                assert(pass);
                transfer.record(*pass, 0);
                checked(pass->end());
                auto commands = encoder->finish();
                assert(commands);
                checked(device.queue().submit(*commands));
                device.waitIdle();
            }
            {
                resource::ResourceManager resources;
                assert(tests::loadSpriteFixture(resources));
                renderer::Scene2DRenderer sprites(device, resources);
                renderer::Scene2DRendererDesc desc{
                    cold ? ARP_SHADER_DIR "/scene_2d_sprites.slang" : "scene_2d_sprites.slang",
                    system.colorFormat(),
                    16,
                    2,
                    2};
                checked(cold ? sprites.create(desc) : sprites.create(desc, inputs[4]));
                for (const auto id : {tests::atlasA, tests::atlasB})
                    checked(sprites.prepareAtlas(resources.find<scene2d::SpriteAtlasResource>(id)));
                core::pmr::Vector<scene2d::SpriteDrawItem> items;
                for (std::uint32_t i = 0; i < 5; ++i) {
                    scene2d::SpriteDrawItem item;
                    item.sprite = resources.find<scene2d::SpriteResource>(
                        i == 2 ? tests::spriteB : tests::spriteA);
                    item.sorting.objectId = i + 1;
                    item.world.translation = {float(i) * 1.2f - 2.5f, 0, 0};
                    item.world.scale = {2, 2, 1};
                    item.flipX = i == 1;
                    item.flipY = i == 3;
                    if (i == 4)
                        item.world.rotation = {0, 0, .38268343f, .92387953f};
                    items.push_back(item);
                }
                scene2d::SpriteDrawPlan plan(16);
                assert(plan.build(resources, items));
                scene2d::Viewport2DMapping mapping;
                assert(mapping.configure(640, 360, {640, 360}, false));
                auto frame = system.swapchain()->acquireNextFrame();
                assert(frame.success);
                auto image = system.swapchain()->acquiredImage(frame);
                assert(image.success && image.texture);
                checked(sprites.draw(*image.texture, plan, mapping, {}, 60, frame.frameIndex));
                checked(system.swapchain()->present(frame));
                system.waitGpuIdle();
                assert(sprites.lastDrawCount() == 3);
            }
            system.waitGpuIdle();
        };
        if (lifecycle) {
            // Prime the declared swapchain image slots and persistent metadata.
            // Measured cycles remain ordinary create/draw/destroy operations.
            for (std::uint32_t i = 0; i < system.swapchain()->imageCount(); ++i)
                drawAll();
            const auto before = core::defaultCpuAllocator().stats();
            const auto gpuBefore = device.memoryStats();
            for (unsigned i = 0; i < 20; ++i) {
                drawAll();
                const auto after = core::defaultCpuAllocator().stats();
                const auto gpuAfter = device.memoryStats();
                std::printf("native20 cycle=%u core=%llu/%llu gpu=%llu/%llu\n",
                    i,
                    (unsigned long long)after.liveBytes,
                    (unsigned long long)(after.allocationCount - after.deallocationCount),
                    (unsigned long long)gpuAfter.allocatedBytes,
                    (unsigned long long)gpuAfter.liveResourceCount);
                assert(before.liveBytes == after.liveBytes
                    && before.allocationCount - before.deallocationCount
                        == after.allocationCount - after.deallocationCount);
                assert(gpuBefore.allocatedBytes == gpuAfter.allocatedBytes
                    && gpuBefore.liveResourceCount == gpuAfter.liveResourceCount);
            }
        } else {
            drawAll();
        }
        capture.end();
        std::printf("PASS five actual Vulkan renderers; source=%s; graph/IO retired before GPU\n",
            cold ? "cold-reference" : "prepared");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "prepared Vulkan: %s\n", error.what());
        return 1;
    }
}
