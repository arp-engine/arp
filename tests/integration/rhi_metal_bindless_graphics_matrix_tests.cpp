#if defined(ARP_BINDLESS_PBR_MATERIAL_FIXTURE) || defined(ARP_BINDLESS_TERRAIN27_FIXTURE)
#define ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE 1
#endif
#include "../fixtures/bindless_graphics_native_memory_package_fixture.h"
#include "../fixtures/bindless_metal_public_graphics_fixture.h"
#include "../fixtures/metal_original_bindless_graphics_content_fixture.h"
#include "../fixtures/original_graphics_matrix_data_fixture.h"
#include "../fixtures/original_graphics_matrix_memory_fixture.h"
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
#include "../fixtures/bindless_graphics_terrain27_package_fixture.h"
#include "../fixtures/original_graphics_terrain27_data_fixture.h"
#else
#include "../fixtures/bindless_graphics_pbr_material_package_fixture.h"
#include "../fixtures/original_graphics_pbr_material_data_fixture.h"
#endif
#include "../fixtures/metal_completed_texture_root_fixture.h"
#include "../fixtures/metal_sampled_material_readback_fixture.h"
#include "../fixtures/ordinary_material_reference_fixture.h"
#include "../fixtures/original_graphics_runtime_vector_memory_fixture.h"
#endif
#ifdef ARP_BINDLESS_INSTANCE64_FIXTURE
#include "../fixtures/bindless_graphics_instance64_package_fixture.h"
#include "../fixtures/original_graphics_instance64_data_fixture.h"
#include "../fixtures/original_graphics_instance64_memory_fixture.h"
#endif
#ifdef ARP_BINDLESS_SKINNING256_FIXTURE
#include "../fixtures/original_graphics_skinning256_data_fixture.h"
#endif
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#ifdef NDEBUG
#error Graphics matrix tests require assertions
#endif

namespace {
class MatrixBuffers final : public arp::test::MetalOriginalGraphicsBufferDataFixture
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    ,
                            public arp::test::MetalCompletedTextureUploadFixture
#endif
{
public:
#if defined(ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE)
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
    using Data = arp::test::OriginalGraphicsTerrain27DataFixture;
#else
    using Data = arp::test::OriginalGraphicsPbrMaterialDataFixture;
#endif
    static constexpr bool enabled = false;
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
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
        mixed2DArrayTextures = true;
#endif
#endif
    }

#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    void configureAdmission(arp::rhi::OriginalCommandAdmissionBudget& budget) const
    {
        MetalCompletedTextureUploadFixture::configureAdmission(budget);
        MetalOriginalGraphicsBufferDataFixture::configureAdmission(budget);
        budget.maxInputDescriptors = Data::sampledTextureCount + 7u;
        budget.maxCommands = budget.maxReferences = Data::sampledTextureCount + 22u;
        budget.maxTextureRanges = Data::sampledTextureCount * 2u + 2u;
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
        budget.maxGraphOwners += Data::sampledTextureCount
            * (arp::rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount
                + 2u * arp::rhi::OriginalCommandGraphicsPipelineSource::cpuOwnerSlots);
#endif
    }
#endif

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
        if (!device)
            return;
        using namespace arp;
        const auto& prepared =
            std::get<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
                root->originalPrepared());
        const auto& pipeline = *prepared->owners()->desc().pipeline;
        const auto native = rhi_metal::metalRhiNativePayloadFor(pipeline);
        assert(native && native->nativeRuntimeBacked);
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{root->originalMetadataQuota()}, [&]() noexcept -> const char* {
                for (unsigned i = 0u; i != 2u; ++i) {
                    const auto stage = i ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex;
                    const auto& memory = native->graphicsBufferMemberMemory[i];
                    assert(memory.version == 1u && memory.stage == stage
                        && memory.entries.size() == 2u);
                    const auto* module = pipeline.bindlessShaderModule(stage, 1u);
                    for (const auto& record : memory.entries) {
                        const shader::NativeBufferMemoryDeclarations* source = nullptr;
                        assert(!rhi::BindlessShaderPackage::originalNativeBufferMemory(
                            root->authorization()->originalPackageOwner(),
                            module,
                            record.set,
                            record.binding,
                            source));
                        const auto* original = source->lookup(record.set, record.binding);
                        const auto bytes = Data::ordinaryBytes[record.binding];
                        assert(record.set == 0u && record.slot != UINT32_MAX && original
                            && record.relation.occupiedBytes == bytes
                            && record.relation.comparedPairs
                            && record.relation.matrixColumns == (record.binding ? 0u : 1u)
                            && source->sourceTypes()
                                    .nodes()[original->sourceType]
                                    .occupiedExtentBytes
                                == bytes
                            && record.target.nodes()[record.targetType].occupiedExtentBytes
                                == bytes);
                    }
                }
                return nullptr;
            }));
        std::puts(
            "BINDLESS_METAL_ORIGINAL_PIPELINE_MEMBER_MEMORY1_OK selected-VS-FS original-source actual-driver frozen-plan hot0 no-public-Root-proof-grant");
#endif
    }

    template <class Table> void configureBuffer(arp::rhi::BufferDesc& desc, const Table&) const
    {
        desc.memory = arp::rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = sizeof(Data::Numeric);
    }

    static void write(arp::rhi::GpuBuffer& buffer, const void* data, std::size_t bytes)
    {
        arp::core::pmr::String diagnostic;
        assert(bytes == buffer.desc().sizeBytes);
        assert(arp::rhi_metal::writeMetalRhiNativeBuffer(
            arp::rhi_metal::metalRhiNativePayloadFor(buffer), data, bytes, 0u, bytes, diagnostic));
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
            Data::Numeric actual;
            assert(lease->layout().sizeBytes == sizeof(actual));
            core::pmr::String diagnostic;
            assert(rhi_metal::readMetalRhiNativeBuffer(
                rhi_metal::metalRhiNativePayloadFor(*lease->originalObject()),
                actual.data(),
                sizeof(actual),
                0u,
                sizeof(actual),
                diagnostic));
            Data::verify(actual);
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
            arp::test::OrdinaryMaterialReferenceFixture<Data>::compare(actual);
#endif
            ++count;
        }
        assert(count == 1u);
    }
};

#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
using RuntimeProof = arp::test::BindlessGraphicsTerrain27PackageFixture;
#else
using RuntimeProof = arp::test::BindlessGraphicsPbrMaterialPackageFixture;
#endif
#elif defined(ARP_BINDLESS_INSTANCE64_FIXTURE)
using RuntimeProof = arp::test::BindlessGraphicsInstance64PackageFixture;
#endif

void validateMemberMemoryRefusals(const char* publication)
{
    using namespace arp;
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    // The unowned retained factory deliberately remains buffer-only. Images
    // must enter through the existing device-authorized public Root below.
    RuntimeProof proof(std::filesystem::path(publication).parent_path(), true);
    rhi::BindlessPackageLoadBudget budget{4u * 1024u * 1024u,
        32u * 1024u * 1024u,
        64u,
        4u,
        MatrixBuffers::Data::bindingCount,
        4u,
        &proof.request};
    core::pmr::String diagnostic;
    auto package = rhi::BindlessShaderPackage::load(publication, budget, diagnostic);
    assert(package);
    rhi::RenderPipelineDesc state;
    state.dynamicViewportScissor = true;
    state.colorFormat = rhi::TextureFormat::Rgba8Unorm;
    assert(!rhi_metal::createMetalRhiRetainedGraphicsPipeline(package,
        state,
        "vertexMain",
        "cleanFragment",
        rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal),
        diagnostic));
    assert(diagnostic == "RHI-METAL-BINDLESS-GRAPHICS-RESOURCE-UNSUPPORTED");
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
    std::puts("BINDLESS_METAL_TERRAIN27_RETAINED_REFUSAL_OK image-authorization-required");
#else
    std::puts("BINDLESS_METAL_PBR_MATERIAL_RETAINED_REFUSAL_OK image-authorization-required");
#endif
#else
#if defined(ARP_BINDLESS_INSTANCE64_FIXTURE) || defined(ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE)
    RuntimeProof proof(std::filesystem::path(publication).parent_path(), true);
    rhi::BindlessPackageLoadBudget budget{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u};
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    budget.maxBindings = MatrixBuffers::Data::bindingCount;
#endif
    budget.ordinaryMemory = &proof.request;
    core::pmr::String diagnostic;
    rhi::RenderPipelineDesc state;
    state.dynamicViewportScissor = true;
    state.colorFormat = rhi::TextureFormat::Rgba8Unorm;
    const auto contract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    auto quota = core::makeShared<core::CpuMemoryQuota>(32u * 1024u * 1024u);
    for (const bool nodeBudget : {true, false}) {
        proof.nativeBindings[0][0].targetTypes = proof.graphBudget;
        if (nodeBudget)
            proof.nativeBindings[0][0].targetTypes.maxNodes = 1u;
        else
            proof.nativeBindings[0][0].targetTypes.maxMembers = 1u;
        {
            core::CpuMemoryQuotaScope scope(quota);
            auto package = rhi::BindlessShaderPackage::load(publication, budget, diagnostic);
            assert(package);
            const auto baseline = quota->liveBytes();
            assert(!rhi_metal::createMetalRhiRetainedGraphicsPipeline(
                package, state, "vertexMain", "cleanFragment", contract, diagnostic));
            if (diagnostic.find(nodeBudget ? "TYPE-NODE-BUDGET" : "TYPE-MEMBER-BUDGET")
                == diagnostic.npos)
                std::fprintf(stderr, "Runtime vector pipeline refusal: %s\n", diagnostic.c_str());
            assert(diagnostic.find(nodeBudget ? "TYPE-NODE-BUDGET" : "TYPE-MEMBER-BUDGET")
                != diagnostic.npos);
            core::pmr::String{}.swap(diagnostic);
            assert(quota->liveBytes() == baseline);
        }
        assert(!quota->liveBytes());
    }
    std::puts(
        "BINDLESS_METAL_INSTANCE64_MEMBER_REFUSALS2_OK actual-driver node-member-budget no-partial-publication quota0");
#else
    test::BindlessGraphicsNativeMemoryPackageFixture proof(
        std::filesystem::path(publication).parent_path(), MatrixBuffers::Data::packageName);
    rhi::BindlessPackageLoadBudget budget{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u};
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    budget.maxBindings = MatrixBuffers::Data::bindingCount;
#endif
    budget.ordinaryMemory = &proof.request;
    auto quota = core::makeShared<core::CpuMemoryQuota>(32u * 1024u * 1024u);
    core::pmr::String diagnostic;
    rhi::RenderPipelineDesc state;
    state.dynamicViewportScissor = true;
    state.colorFormat = rhi::TextureFormat::Rgba8Unorm;
    const auto contract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    for (unsigned mode = 0u; mode != 4u; ++mode) {
        proof.bindings[0][0].targetTypes =
            test::BindlessGraphicsNativeMemoryPackageFixture::graphBudget;
        proof.matrices[0].targetType = proof.matrices[1].targetType = 2u;
        if (!mode)
            proof.bindings[0][0].targetTypes.maxNodes = 3u;
        if (mode == 1u)
            proof.bindings[0][0].targetTypes.maxMembers = 1u;
        if (mode == 2u)
            proof.matrices[0].targetType = 0u;
        if (mode == 3u)
            proof.matrices[1].targetType = 0u;
        {
            core::CpuMemoryQuotaScope scope(quota);
            auto package = rhi::BindlessShaderPackage::load(publication, budget, diagnostic);
            assert(package);
            const auto baseline = quota->liveBytes();
            const auto create = [&](core::StringView fragment) {
                return rhi_metal::createMetalRhiRetainedGraphicsPipeline(
                    package, state, "vertexMain", fragment, contract, diagnostic);
            };
            assert(!create("cleanFragment"));
            const auto expected = mode == 0u ? "TYPE-NODE-BUDGET"
                : mode == 1u                 ? "TYPE-MEMBER-BUDGET"
                                             : "MEMORY-MATRIX-FRAMING";
            if (diagnostic.find(expected) == diagnostic.npos)
                std::fprintf(
                    stderr, "Native pipeline expected %s got %s\n", expected, diagnostic.c_str());
            assert(diagnostic.find(expected) != diagnostic.npos);
            // Diagnostics belong to the test caller; exclude their retained capacity.
            core::pmr::String{}.swap(diagnostic);
            assert(quota->liveBytes() == baseline);
            if (mode == 3u) {
                auto selectedFault = create("faultFragment");
                assert(selectedFault);
                const auto payload = rhi_metal::metalRhiNativePayloadFor(*selectedFault);
                assert(payload->graphicsBufferMemberMemory[0].version == 1u
                    && payload->graphicsBufferMemberMemory[1].version == 1u);
            }
        }
        assert(!quota->liveBytes());
    }
    std::puts(
        "BINDLESS_METAL_ORIGINAL_PIPELINE_MEMBER_REFUSALS1_OK node-member-budgets matrix-framing selected-entry quota0");
#endif
#endif
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 2 || argc == 3);
    validateMemberMemoryRefusals(argv[1]);
    core::pmr::String diagnostic;
#if defined(ARP_BINDLESS_INSTANCE64_FIXTURE) || defined(ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE)
    RuntimeProof proof(std::filesystem::path(argv[1]).parent_path(), true);
#else
    test::BindlessGraphicsNativeMemoryPackageFixture proof(
        std::filesystem::path(argv[1]).parent_path(), MatrixBuffers::Data::packageName);
#endif
    rhi::BindlessPackageLoadBudget budget{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u};
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    budget.maxBindings = MatrixBuffers::Data::bindingCount;
#endif
    budget.ordinaryMemory = &proof.request;
    auto package = rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic);
    if (!package)
        std::fprintf(stderr, "Graphics matrix package: %s\n", diagnostic.c_str());
    assert(package);
    // All later driver comparisons must use frozen package intent.
#if defined(ARP_BINDLESS_INSTANCE64_FIXTURE) || defined(ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE)
    for (auto& entry : proof.nativeBindings)
        entry[0].relation.rootForm = shader::SpirvMslBufferMemoryLowering::RootForm::Block;
#else
    for (auto& matrix : proof.matrices)
        matrix.targetType = SIZE_MAX;
#endif
    if (argc == 3) {
        rhi::FrameCaptureRequest request;
        request.outputPath = argv[2];
        const auto started = rhi_metal::beginMetalRhiFrameCapture(request);
        if (!started.success())
            std::fprintf(stderr, "Metal matrix capture: %s\n", started.diagnostics.c_str());
        assert(started.success());
    }
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    {
        auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        desc.uploadStagingPageSizeBytes =
            test::OrdinaryMaterialReferenceFixture<MatrixBuffers::Data>::uploadStagingBytes;
        desc.uploadStagingMaxPageCount = 1u;
        auto device = rhi_metal::createMetalRhiDevice(desc);
        assert(device);
        test::OrdinaryMaterialReferenceFixture<MatrixBuffers::Data>::execute(*device,
            ARP_BINDLESS_DIRECT_REFERENCE_PREFIX,
            [](const rhi::GpuBuffer& buffer, void* output, std::size_t bytes) {
                core::pmr::String diagnostic;
                assert(
                    rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
                        output,
                        bytes,
                        0u,
                        bytes,
                        diagnostic));
            });
    }
#endif
    for (const bool fault : {false, true}) {
        auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
        desc.enableBindlessGraphicsImages = true;
#endif
        auto device = rhi_metal::createMetalRhiDevice(desc);
        assert(device && device->backend() == rhi::Backend::Metal);
        test::MetalOriginalBindlessGraphicsContentFixture observer;
        observer.execute = true;
        observer.expectedFaultBits = fault ? 1u : 0u;
        observer.verifyAdditionalBuffers = MatrixBuffers::verify;
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
        observer.verifyOutputs = test::MetalSampledMaterialReadbackFixture::terrain;
#elif defined(ARP_BINDLESS_PBR_MATERIAL_FIXTURE)
        observer.verifyOutputs = test::MetalSampledMaterialReadbackFixture::pbr;
#endif
        test::validateOriginalBindlessRootSource(package,
            std::move(device),
            "unused",
            false,
            observer,
            MatrixBuffers{fault},
            true,
            fault ? "faultFragment" : "cleanFragment");
    }
    if (argc == 3)
        assert(rhi_metal::endMetalRhiFrameCapture().success());
#if defined(ARP_BINDLESS_TERRAIN27_FIXTURE)
    std::puts(
        "BINDLESS_METAL_PUBLIC_GRAPHICS_TERRAIN27_OK clean/fault original-root real-completion retirement");
#elif defined(ARP_BINDLESS_PBR_MATERIAL_FIXTURE)
    std::puts(
        "BINDLESS_METAL_PUBLIC_GRAPHICS_PBR_MATERIAL_OK clean/fault original-root real-completion retirement");
#elif defined(ARP_BINDLESS_INSTANCE64_FIXTURE)
    std::puts(
        "BINDLESS_METAL_PUBLIC_GRAPHICS_INSTANCE64_OK clean/fault original-root real-completion retirement");
#elif defined(ARP_BINDLESS_SKINNING256_FIXTURE)
    std::puts(
        "BINDLESS_METAL_PUBLIC_GRAPHICS_SKINNING256_OK clean/fault original-root real-completion retirement");
#else
    std::puts(
        "BINDLESS_METAL_PUBLIC_GRAPHICS_MATRIX_OK clean/fault original-root real-completion retirement");
#endif
}
