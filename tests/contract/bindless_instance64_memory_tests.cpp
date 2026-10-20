#if defined(ARP_BINDLESS_PBR_MATERIAL_FIXTURE) || defined(ARP_BINDLESS_TERRAIN27_FIXTURE)
#define ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE 1
#endif
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
#include "../fixtures/bindless_graphics_terrain27_package_fixture.h"
#include "../fixtures/original_graphics_terrain27_data_fixture.h"
#else
#include "../fixtures/bindless_graphics_pbr_material_package_fixture.h"
#include "../fixtures/original_graphics_pbr_material_data_fixture.h"
#endif
#include "../fixtures/original_graphics_runtime_vector_memory_fixture.h"
#else
#include "../fixtures/bindless_graphics_instance64_package_fixture.h"
#include "../fixtures/original_graphics_instance64_data_fixture.h"
#include "../fixtures/original_graphics_instance64_memory_fixture.h"
#endif
#include "../fixtures/original_bindless_root_source_fixture.h"

#ifdef NDEBUG
#error Instance64 memory tests require assertions
#endif

namespace {
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
using Data = arp::test::OriginalGraphicsTerrain27DataFixture;
using Proof = arp::test::BindlessGraphicsTerrain27PackageFixture;
#else
using Data = arp::test::OriginalGraphicsPbrMaterialDataFixture;
using Proof = arp::test::BindlessGraphicsPbrMaterialPackageFixture;
#endif
#else
using Data = arp::test::OriginalGraphicsInstance64DataFixture;
using Proof = arp::test::BindlessGraphicsInstance64PackageFixture;
#endif
struct Buffers {
    static constexpr bool enabled = false;
    bool undersized = false;

#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
    void configureAdmission(arp::rhi::OriginalCommandAdmissionBudget& budget) const
    {
        budget.maxGraphOwners += Data::sampledTextureCount
            * (arp::rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount
                + 2u * arp::rhi::OriginalCommandGraphicsPipelineSource::cpuOwnerSlots);
    }
#endif

    void configureOrdinaryBuffer(arp::rhi::BufferDesc& desc,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        Data::configureOrdinaryBuffer(desc, set, binding);
        if (undersized && !binding.binding)
            desc.sizeBytes = Data::runtimeStride - 1u;
    }

    void initializeOrdinaryBuffer(
        arp::rhi::GpuBuffer&, std::uint32_t, const arp::rhi::DescriptorBindingDesc&) const
    {}
};
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 3);
    auto quota = core::makeShared<core::CpuMemoryQuota>(4u * 1024u * 1024u);
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    rhi::BindlessPackageLoadBudget budget{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u};
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
    // Actual sealed physical demand includes sampled resources, result,
    // metadata/fault and both ordinary buffers. Refuse one binding short.
    assert(!rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-PACKAGE-BINDING-BUDGET");
    budget.maxBindings = Data::bindingCount - 1u;
    assert(!rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-PACKAGE-BINDING-BUDGET");
    budget.maxBindings = Data::bindingCount;
#endif
    Proof proof(std::filesystem::path(argv[1]).parent_path());
    const auto load = [&](const char* path) {
        core::CpuMemoryQuotaScope scope(quota);
        return rhi::BindlessShaderPackage::load(path, budget, diagnostic);
    };
    // This matrix-free shape also has the original deterministic automatic
    // source/target relation. Explicit intent below must agree with that ABI.
    auto automatic = load(argv[1]);
    if (!automatic)
        std::fprintf(stderr, "Instance64 automatic correspondence: %s\n", diagnostic.c_str());
    assert(automatic);
    automatic.reset();
    budget.ordinaryMemory = &proof.request;
    auto package = load(argv[1]);
    if (!package)
        std::fprintf(stderr, "Instance64 Emulated package: %s\n", diagnostic.c_str());
    assert(package);
    // Caller proof mutation must not change factory-frozen correspondence.
    proof.emulatedBindings[0][0].form =
        shader::SpirvWgslBufferMemoryLowering::RootForm::BlockStruct;
    assert(!load(argv[1]));
    for (const bool fault : {false, true})
        for (const bool undersized : {false, true})
            test::validateOriginalBindlessRootSource(
                package,
                {},
                "unused",
                false,
                [=](const auto& root, const auto& sources, rhi::GpuDevice* device) {
#ifdef ARP_BINDLESS_SAMPLED_MATERIAL_FIXTURE
                    test::OriginalGraphicsRuntimeVectorMemoryFixture::inspect(root,
                        sources,
                        device,
                        fault,
                        Data::runtimeStride,
                        Data::runtimeCapacity,
                        Data::ordinaryBytes[1],
                        undersized);
#else
                    test::OriginalGraphicsInstance64MemoryFixture::inspect(
                        root, sources, device, fault, undersized);
#endif
                },
                Buffers{undersized},
                false,
                fault ? "faultFragment" : "cleanFragment");
    package.reset();
    assert(!quota->liveBytes());

    Proof nativeProof(std::filesystem::path(argv[2]).parent_path(), true);
    budget.ordinaryMemory = &nativeProof.request;
    package = load(argv[2]);
    assert(package);
    nativeProof.nativeBindings[0][0].relation.rootForm =
        shader::SpirvMslBufferMemoryLowering::RootForm::Block;
    assert(!load(argv[2]));
    for (const auto& module : package->modules()) {
        const shader::NativeBufferMemoryDeclarations* declarations = nullptr;
        assert(!rhi::BindlessShaderPackage::originalNativeBufferMemory(
            package, &module, 0u, 0u, declarations));
        const auto* record = declarations->lookup(0u, 0u);
        assert(record
            && record->rootForm == shader::SpirvMslBufferMemoryLowering::RootForm::RuntimeElement
            && record->relationVersion == 2u);
    }
    package.reset();
    assert(!quota->liveBytes());
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto decision =
            rhi::semantics::OriginalBindlessGraphicsMemberMemoryLowering::lowering(backend, 2u);
        assert(decision.available && !decision.executionAvailable
            && decision.validatorOnly == (backend == rhi::Backend::Null));
    }
#ifdef ARP_BINDLESS_TERRAIN27_FIXTURE
    std::puts(
        "BINDLESS_TERRAIN27_MEMORY_OK runtime32 static16 original-package frozen-intent undersized31-refused binding32-refused Null-hot0 retirement0 no-execution-grant");
#elif defined(ARP_BINDLESS_PBR_MATERIAL_FIXTURE)
    std::puts(
        "BINDLESS_PBR_MATERIAL_MEMORY_OK runtime96 static16 original-package frozen-intent undersized95-refused Null-hot0 retirement0 no-execution-grant");
#else
    std::puts(
        "BINDLESS_INSTANCE64_MEMORY_OK original-package runtime64 static224 frozen-intent undersized63-refused Null-hot0 retirement0 no-execution-grant");
#endif
}
