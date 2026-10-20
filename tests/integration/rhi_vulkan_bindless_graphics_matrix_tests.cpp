#include "../fixtures/original_graphics_matrix_data_fixture.h"
#include "../fixtures/original_graphics_matrix_memory_fixture.h"
#if defined(ARP_BINDLESS_PBR_MATERIAL_FIXTURE) || defined(ARP_BINDLESS_TERRAIN27_FIXTURE)
#define ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE 1
#include "../fixtures/ordinary_material_reference_fixture.h"
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
#include "../fixtures/original_graphics_terrain27_data_fixture.h"
#else
#include "../fixtures/original_graphics_pbr_material_data_fixture.h"
#endif
#include "../fixtures/original_graphics_runtime_vector_memory_fixture.h"
#include "../fixtures/vulkan_completed_texture_root_fixture.h"
#endif
#ifdef ARP_BINDLESS_INSTANCE64_FIXTURE
#include "../fixtures/original_graphics_instance64_data_fixture.h"
#include "../fixtures/original_graphics_instance64_memory_fixture.h"
#endif
#ifdef ARP_BINDLESS_SKINNING256_FIXTURE
#include "../fixtures/original_graphics_skinning256_data_fixture.h"
#endif
#include "../fixtures/renderdoc_capture.h"
#include "../fixtures/vulkan_headless_runtime.h"
#include "../fixtures/vulkan_original_bindless_graphics_content_fixture.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/frontend/bindless_snapshot_allocation.h"

#ifdef NDEBUG
#error Graphics matrix tests require assertions
#endif

namespace {
class MatrixBuffers final
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    : public arp::test::VulkanCompletedTextureRootFixture
#endif
{
public:
    static constexpr bool enabled = true;
#if defined(ARP_BINDLESS_TERRAIN27_FIXTURE)
    using Data = arp::test::OriginalGraphicsTerrain27DataFixture;
#elif defined(ARP_BINDLESS_PBR_MATERIAL_FIXTURE)
    using Data = arp::test::OriginalGraphicsPbrMaterialDataFixture;
#elif defined(ARP_BINDLESS_INSTANCE64_FIXTURE)
    using Data = arp::test::OriginalGraphicsInstance64DataFixture;
#elif defined(ARP_BINDLESS_SKINNING256_FIXTURE)
    using Data = arp::test::OriginalGraphicsSkinning256DataFixture;
#else
    using Data = arp::test::OriginalGraphicsMatrixDataFixture;
#endif
    bool fault = false;

    explicit MatrixBuffers(bool value = false) : fault(value)
    {
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
        initializeTexturePixels = Data::fillTexture;
        // Same explicit 64 MiB device ceiling supplied by this runner. The
        // original upload still queries and charges its actual staging bytes.
        nativeStagingCeiling = 64u * 1024u * 1024u;
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
        mixed2DArrayTextures = true;
#endif
#endif
    }

    template <class Root, class Sources>
    void inspectOriginalRoot(
        const Root& root, const Sources& sources, arp::rhi::GpuDevice* device) const
    {
#if defined(ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE)
        arp::test::OriginalGraphicsRuntimeVectorMemoryFixture::inspect(root,
            sources,
            device,
            fault,
            Data::runtimeStride,
            Data::runtimeCapacity,
            Data::ordinaryBytes[1]);
#elif defined(ARP_BINDLESS_INSTANCE64_FIXTURE)
        arp::test::OriginalGraphicsInstance64MemoryFixture::inspect(root, sources, device, fault);
#else
        arp::test::OriginalGraphicsMatrixMemoryFixture::inspect(
            root, sources, device, fault, false, Data::ordinaryBytes);
#endif
    }

    void configureAdmission(arp::rhi::OriginalCommandAdmissionBudget& budget) const
    {
        budget.maxCommands = budget.maxReferences = 32u;
        budget.maxInputDescriptors = 16u;
        budget.maxStateObservations = 64u;
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
        budget.maxInputDescriptors = Data::sampledTextureCount + 7u;
        budget.maxCommands = budget.maxReferences = Data::sampledTextureCount + 22u;
        // Each sampled texture has a transition and draw read; the attachment
        // has a transition and pass observation. Whole-mip ranges stay whole.
        budget.maxTextureRanges = Data::sampledTextureCount * 2u + 2u;
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
        // Each image adds a parent + view source and their bounded identity
        // projections. Retain the existing non-image budget and derive this
        // corpus's additional ceiling from the original source contracts.
        budget.maxGraphOwners += Data::sampledTextureCount
            * (arp::rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount
                + 2u * arp::rhi::OriginalCommandGraphicsPipelineSource::cpuOwnerSlots);
#endif
#endif
    }

    template <class Table> void configureBuffer(arp::rhi::BufferDesc& desc, const Table&) const
    {
        desc.memory = arp::rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = sizeof(Data::Numeric);
    }

    static void write(arp::rhi::GpuBuffer& buffer, const void* data, std::size_t bytes)
    {
        const auto native = arp::rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(native && native->backendMappedData() && bytes == buffer.desc().sizeBytes);
        std::memcpy(native->backendMappedData(), data, bytes);
        arp::core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, bytes, diagnostic));
    }

    template <class Table>
    void initializeBuffer(arp::rhi::GpuBuffer& buffer, const Table&, std::uint32_t index) const
    {
        assert(index == 0u);
        Data::Numeric poison;
        poison.fill(-999.f);
        write(buffer, poison.data(), sizeof(poison));
    }

    void configureOrdinaryBuffer(arp::rhi::BufferDesc& desc,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        Data::configureOrdinaryBuffer(desc, set, binding);
    }

    void initializeOrdinaryBuffer(arp::rhi::GpuBuffer& buffer,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        assert(set == 0u);
        const auto primary = Data::ordinary0();
        const auto secondary = Data::ordinary1();
        write(buffer,
            binding.binding == 0u ? static_cast<const void*>(&primary) : &secondary,
            binding.binding == 0u ? sizeof(primary) : sizeof(secondary));
    }

    static void verify(const arp::rhi::OriginalCommandBindlessRootSource& root)
    {
        using namespace arp;
        std::size_t count = 0u;
        for (const auto& element : root.elements()) {
            if (element.declaration.write.binding !=
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
                Data::resultBinding
#else
                2u
#endif
            )
                continue;
            const auto& lease =
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(element.owner);
            const auto native = rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
            Data::Numeric actual;
            assert(native && lease->layout().sizeBytes == sizeof(actual));
            core::pmr::String diagnostic;
            assert(native->backendInvalidateMappedRead(0u, sizeof(actual), diagnostic));
            std::memcpy(actual.data(), native->backendMappedData(), sizeof(actual));
            Data::verify(actual);
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
            arp::test::OrdinaryMaterialReferenceFixture<Data>::compare(actual);
#endif
            ++count;
        }
        assert(count == 1u);
    }

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
    core::pmr::String diagnostic;
    rhi::BindlessPackageLoadBudget loadBudget{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u};
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    loadBudget.maxBindings = MatrixBuffers::Data::bindingCount;
#endif
    auto package = rhi::BindlessShaderPackage::load(argv[1], loadBudget, diagnostic);
    if (!package)
        std::fprintf(stderr, "Graphics matrix package: %s\n", diagnostic.c_str());
    assert(package);
    tests::VulkanHeadlessRuntime runtime(true);
    if (!runtime.runtime())
        return 77;
    rhi_vulkan::VulkanRhiNativeResourceAllocator allocator(runtime.runtime());
    std::uint64_t internalBytes = 0u;
    for (const auto& desc : rhi::BindlessSnapshotAllocationContract::buffers(package->artifact())) {
        const char* id = nullptr;
        const auto bytes = allocator.queryBufferAllocationBytes(desc, id);
        assert(bytes && !id);
        internalBytes += bytes;
    }
    std::fprintf(stderr,
        "Graphics matrix queried snapshot allocation: %llu bytes\n",
        static_cast<unsigned long long>(internalBytes));
    assert(internalBytes <= package->artifact().snapshotBytes);
    tests::RenderDocCaptureScope capture;
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    {
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.runtime = runtime.runtime();
        desc.capabilityContract =
            rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Vulkan);
        desc.requireNativeResourceAllocation = true;
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        desc.uploadStagingPageSizeBytes =
            test::OrdinaryMaterialReferenceFixture<MatrixBuffers::Data>::uploadStagingBytes;
        desc.uploadStagingMaxPageCount = 1u;
        auto device = rhi_vulkan::createVulkanRhiDevice(desc);
        assert(device);
        test::OrdinaryMaterialReferenceFixture<MatrixBuffers::Data>::execute(*device,
            ARP_BINDLESS_DIRECT_REFERENCE_PREFIX,
            [](const rhi::GpuBuffer& buffer, void* output, std::size_t bytes) {
                const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
                core::pmr::String diagnostic;
                assert(native && native->backendInvalidateMappedRead(0u, bytes, diagnostic));
                std::memcpy(output, native->backendMappedData(), bytes);
            });
    }
#endif
    for (const bool fault : {false, true}) {
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.runtime = runtime.runtime();
        desc.capabilityContract =
            rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Vulkan);
        desc.requireNativeResourceAllocation = true;
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        auto device = rhi_vulkan::createVulkanRhiDevice(desc);
        assert(device && device->backend() == rhi::Backend::Vulkan);
        test::VulkanOriginalBindlessGraphicsContentFixture observer;
        observer.execute = true;
        observer.expectedFaultBits = fault ? 1u : 0u;
        observer.verifyOrdinaryBuffers = MatrixBuffers::verify;
        test::validateOriginalBindlessRootSource(package,
            std::move(device),
            "unused",
            false,
            observer,
            MatrixBuffers{fault},
            true,
            fault ? "faultFragment" : "cleanFragment");
    }
#if defined(ARP_BINDLESS_TERRAIN27_FIXTURE)
    std::puts(
        "BINDLESS_VULKAN_PUBLIC_GRAPHICS_TERRAIN27_OK clean/fault original-root real-completion retirement");
#elif defined(ARP_BINDLESS_PBR_MATERIAL_FIXTURE)
    std::puts(
        "BINDLESS_VULKAN_PUBLIC_GRAPHICS_PBR_MATERIAL_OK clean/fault original-root real-completion retirement");
#elif defined(ARP_BINDLESS_INSTANCE64_FIXTURE)
    std::puts(
        "BINDLESS_VULKAN_PUBLIC_GRAPHICS_INSTANCE64_OK clean/fault original-root real-completion retirement");
#elif defined(ARP_BINDLESS_SKINNING256_FIXTURE)
    std::puts(
        "BINDLESS_VULKAN_PUBLIC_GRAPHICS_SKINNING256_OK clean/fault original-root real-completion retirement");
#else
    std::puts(
        "BINDLESS_VULKAN_PUBLIC_GRAPHICS_MATRIX_OK clean/fault original-root cancellation hot0 retirement");
#endif
}
