#include "../fixtures/bindless_graphics_push_package_fixture.h"
#include "../fixtures/original_graphics_push_data_fixture.h"
#include "../fixtures/renderdoc_capture.h"
#include "../fixtures/vulkan_headless_runtime.h"
#include "../fixtures/vulkan_original_bindless_graphics_content_fixture.h"

#ifdef NDEBUG
#error Graphics push tests require assertions
#endif

namespace {
using namespace arp;

using Data = test::OriginalGraphicsPushDataFixture;

struct PushBuffers {
    static constexpr bool enabled = true, graphicsPush = true;

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& b) const
    {
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
        const auto payload = rhi_vulkan::vulkanRhiNativePayload(buffer);
        const core::Vec4 poison{-999, -999, -999, -999};
        assert(payload && payload->backendMappedData());
        std::memcpy(payload->backendMappedData(), &poison, sizeof(poison));
        core::pmr::String id;
        assert(payload->backendFlushMappedWrite(0u, sizeof(poison), id));
    }

    static void verify(const rhi::OriginalCommandBindlessRootSource& root, bool fault)
    {
        std::size_t count = 0u;
        for (const auto& element : root.elements()) {
            if (element.declaration.write.binding != 2u)
                continue;
            const auto& lease =
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(element.owner);
            const auto payload = rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
            core::pmr::String id;
            assert(payload && payload->backendInvalidateMappedRead(0u, sizeof(core::Vec4), id));
            core::Vec4 actual;
            std::memcpy(&actual, payload->backendMappedData(), sizeof(actual));
            const auto expected = Data::expected(fault);
            assert(!std::memcmp(&actual, &expected, sizeof(actual)));
            ++count;
        }
        assert(count == 1u);
        std::printf(
            "BINDLESS_VULKAN_PUSH_NUMERIC4_OK fault%u full224 row-memory original-frozen-command\n",
            fault);
    }

    static void clean(const rhi::OriginalCommandBindlessRootSource& root) { verify(root, false); }

    static void fault(const rhi::OriginalCommandBindlessRootSource& root) { verify(root, true); }

    template <class... Args> void operator()(const Args&...) const { assert(false); }

    template <class A, class B> void verifyRetiredResult(const A& a, const B& b) const
    {
        assert(!a && !b);
    }
};
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 2);
    test::BindlessGraphicsPushPackageFixture proof(argv[1], "native");
    rhi::BindlessPackageLoadBudget budget{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u};
    budget.push = &proof.request;
    core::pmr::String diagnostic;
    auto package = rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic);
    if (!package)
        std::fprintf(stderr, "Push package: %s\n", diagnostic.c_str());
    assert(package);
    tests::VulkanHeadlessRuntime runtime(true);
    if (!runtime.runtime())
        return 77;
    tests::RenderDocCaptureScope capture;
    for (const bool fault : {false, true}) {
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.runtime = runtime.runtime();
        desc.capabilityContract =
            rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Vulkan);
        desc.requireNativeResourceAllocation = true;
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        auto device = rhi_vulkan::createVulkanRhiDevice(desc);
        auto data = Data::block(fault);
        test::VulkanOriginalBindlessGraphicsContentFixture observer;
        observer.execute = true;
        observer.expectedFaultBits = fault ? 1u : 0u;
        observer.verifyOrdinaryBuffers = fault ? PushBuffers::fault : PushBuffers::clean;
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
    }
    std::puts(
        "BINDLESS_VULKAN_PUBLIC_GRAPHICS_PUSH_OK clean/fault native-fence cancellation hot0 retirement");
}
