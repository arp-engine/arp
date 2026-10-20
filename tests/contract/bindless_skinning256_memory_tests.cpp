#include "../fixtures/bindless_graphics_matrix_package_fixture.h"
#include "../fixtures/original_bindless_root_source_fixture.h"
#include "../fixtures/original_graphics_matrix_memory_fixture.h"
#include "../fixtures/original_graphics_skinning256_data_fixture.h"

#ifdef NDEBUG
#error Skinning256 memory tests require assertions
#endif

namespace {
struct Buffers {
    static constexpr bool enabled = false;
    bool undersized = false;

    void configureOrdinaryBuffer(arp::rhi::BufferDesc& desc,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        arp::test::OriginalGraphicsSkinning256DataFixture::configureOrdinaryBuffer(
            desc, set, binding);
        if (undersized && !binding.binding)
            --desc.sizeBytes;
    }

    void initializeOrdinaryBuffer(
        arp::rhi::GpuBuffer&, std::uint32_t, const arp::rhi::DescriptorBindingDesc&) const
    {}
};
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 2);
    const std::filesystem::path path = argv[1];
    test::BindlessGraphicsMatrixPackageFixture proof(path.parent_path(),
        "GraphicsSkinning256",
        {"    float4x4 skinMatrices[kMaxGpuSkinningJoints];",
            "    SkinningPalette current;",
            "    SkinningPalette previous;"});
    auto quota = core::makeShared<core::CpuMemoryQuota>(4u * 1024u * 1024u);
    rhi::BindlessPackageLoadBudget budget{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u};
    core::pmr::String diagnostic;
    diagnostic.reserve(256u);
    const auto load = [&] {
        core::CpuMemoryQuotaScope scope(quota);
        return rhi::BindlessShaderPackage::load(path, budget, diagnostic);
    };
    assert(!load() && diagnostic == "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-MEMORY");
    budget.ordinaryMemory = &proof.request;
    auto package = load();
    if (!package)
        std::fprintf(stderr, "Skinning256 package: %s\n", diagnostic.c_str());
    assert(package && package->modules().size() == 3u);
    for (const bool fault : {false, true})
        for (const bool undersized : {false, true})
            test::validateOriginalBindlessRootSource(
                package,
                {},
                "unused",
                false,
                [=](const auto& root, const auto& sources, rhi::GpuDevice* device) {
                    test::OriginalGraphicsMatrixMemoryFixture::inspect(root,
                        sources,
                        device,
                        fault,
                        undersized,
                        test::OriginalGraphicsSkinning256DataFixture::ordinaryBytes);
                },
                Buffers{undersized},
                false,
                fault ? "faultFragment" : "cleanFragment");
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{quota}, [&]() noexcept -> const char* {
            for (const auto* entry : {"vertexMain", "cleanFragment", "faultFragment"})
                for (std::uint32_t binding = 0u; binding != 2u; ++binding) {
                    shader::TargetBufferMemoryLayout facts;
                    const auto bytes =
                        test::OriginalGraphicsSkinning256DataFixture::ordinaryBytes[binding];
                    assert(!rhi::BindlessShaderPackage::originalOrdinaryBufferMemory(
                               package, entry, 0u, binding, facts)
                        && facts.source.staticOccupiedExtentBytes == bytes
                        && facts.target.minimumBindingBytes == bytes);
                }
            return nullptr;
        }));
    package.reset();
    assert(!quota->liveBytes());
    std::puts(
        "BINDLESS_SKINNING256_MEMORY_OK original-Emulated-package Root-static-members exact32864+32 undersized-refused hot0 retirement0 no-execution-grant");
}
