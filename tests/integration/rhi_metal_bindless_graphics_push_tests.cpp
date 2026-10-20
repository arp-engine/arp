#include "../fixtures/bindless_graphics_push_package_fixture.h"
#include "../fixtures/bindless_metal_public_graphics_fixture.h"
#include "../fixtures/metal_original_bindless_graphics_content_fixture.h"
#include "../fixtures/original_graphics_push_data_fixture.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#ifdef NDEBUG
#error Graphics push tests require assertions
#endif

namespace {
using namespace arp;
using Data = test::OriginalGraphicsPushDataFixture;

class PushBuffers final : public test::MetalOriginalGraphicsBufferDataFixture {
public:
    static constexpr bool graphicsPush = true;

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& b) const
    {
        MetalOriginalGraphicsBufferDataFixture::configureAdmission(b);
        b.maxCommands = b.maxReferences = 32u;
        b.maxInputDescriptors = 16u;
        b.maxStateObservations = 64u;
        b.maxPushBytes = sizeof(Data::Block);
    }

    template <class Table> void configureBuffer(rhi::BufferDesc& d, const Table&) const
    {
        d.memory = rhi::MemoryUsage::CpuToGpu;
        d.sizeBytes = sizeof(core::Vec4);
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table&, std::uint32_t index) const
    {
        assert(!index);
        core::pmr::String id;
        const core::Vec4 poison{-999, -999, -999, -999};
        assert(rhi_metal::writeMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            &poison,
            sizeof(poison),
            0u,
            sizeof(poison),
            id));
    }

    static void verify(const rhi::OriginalCommandBindlessRootSource& root, bool fault)
    {
        std::size_t count = 0u;
        for (const auto& element : root.elements()) {
            if (element.declaration.write.binding != 2u)
                continue;
            const auto& lease =
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(element.owner);
            core::pmr::String id;
            core::Vec4 actual;
            assert(rhi_metal::readMetalRhiNativeBuffer(
                rhi_metal::metalRhiNativePayloadFor(*lease->originalObject()),
                &actual,
                sizeof(actual),
                0u,
                sizeof(actual),
                id));
            const auto expected = Data::expected(fault);
            if (std::memcmp(&actual, &expected, sizeof(actual)))
                std::fprintf(stderr,
                    "Metal push actual %f,%f,%f,%f fault%u\n",
                    actual.x,
                    actual.y,
                    actual.z,
                    actual.w,
                    fault);
            assert(!std::memcmp(&actual, &expected, sizeof(actual)));
            ++count;
        }
        assert(count == 1u);
        std::printf(
            "BINDLESS_METAL_PUSH_NUMERIC4_OK fault%u full224 row-memory original-frozen-command\n",
            fault);
    }

    static void clean(const rhi::OriginalCommandBindlessRootSource& root) { verify(root, false); }

    static void fault(const rhi::OriginalCommandBindlessRootSource& root) { verify(root, true); }
};
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 2 || argc == 3);
    test::BindlessGraphicsPushPackageFixture proof(argv[1], "native");
    rhi::BindlessPackageLoadBudget budget{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u};
    budget.push = &proof.request;
    core::pmr::String diagnostic;
    auto package = rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic);
    if (!package)
        std::fprintf(stderr, "Push package: %s\n", diagnostic.c_str());
    assert(package);
    if (argc == 3) {
        rhi::FrameCaptureRequest request;
        request.outputPath = argv[2];
        const auto started = rhi_metal::beginMetalRhiFrameCapture(request);
        if (!started.success())
            std::fprintf(stderr, "Metal push capture: %s\n", started.diagnostics.c_str());
        assert(started.success());
    }
    for (const bool cancel : {false, true}) {
        // This host's Metal capture layer fails resource deallocation for a
        // cancelled native command even in an engine-free clear-pass probe.
        // Capture both submitted GPU executions, close the trace, then still
        // execute both cancellation/rollback cases with API Validation enabled.
        if (cancel && argc == 3) {
            assert(rhi_metal::endMetalRhiFrameCapture().success());
            std::puts("BINDLESS_METAL_PUSH_CAPTURE_SUBMITTED_END cancellation-validation-follows");
        }
        for (const bool fault : {false, true}) {
            auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
            desc.memoryBudgetBytes = 64u * 1024u * 1024u;
            auto device = rhi_metal::createMetalRhiDevice(desc);
            assert(device && device->backend() == rhi::Backend::Metal);
            auto data = Data::block(fault);
            test::MetalOriginalBindlessGraphicsContentFixture observer;
            observer.execute = true;
            observer.cancel = cancel;
            observer.expectedFaultBits = fault ? 1u : 0u;
            observer.verifyAdditionalBuffers = fault ? PushBuffers::fault : PushBuffers::clean;
            observer.push = {
                rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0u, sizeof(data), &data};
            test::validateOriginalBindlessRootSource(package,
                std::move(device),
                "unused",
                false,
                observer,
                PushBuffers{},
                true,
                "fragmentMain");
            if (cancel)
                std::printf(
                    "BINDLESS_METAL_PUBLIC_GRAPHICS_PUSH_CANCEL_OK fault%u unpublished quota0\n",
                    fault);
        }
    }
    std::puts(
        "BINDLESS_METAL_PUBLIC_GRAPHICS_PUSH_OK clean/fault real-completion cancellation hot0 retirement");
}
