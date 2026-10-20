#include "../fixtures/bindless_graphics_native_memory_package_fixture.h"
#include "../fixtures/original_bindless_root_source_fixture.h"
#include "../fixtures/original_graphics_runtime_memory_fixture.h"

#ifdef NDEBUG
#error Runtime member memory tests require assertions
#endif

namespace {
struct Buffers {
    static constexpr bool enabled = false;
    bool undersized = false;

    void configureOrdinaryBuffer(arp::rhi::BufferDesc& desc,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        desc.sizeBytes = undersized && set == 2u && binding.binding == 3u ? 3u : 16u;
    }

    void initializeOrdinaryBuffer(
        arp::rhi::GpuBuffer&, std::uint32_t, const arp::rhi::DescriptorBindingDesc&) const
    {}
};

void verifyNativeSourceCustody(const std::filesystem::path& publication)
{
    using namespace arp;
    test::BindlessGraphicsRuntimeNativeMemoryPackageFixture intent(publication.parent_path());
    rhi::BindlessPackageLoadBudget budget{
        2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u};
    budget.ordinaryMemory = &intent.request;
    core::pmr::String diagnostic;
    auto package = rhi::BindlessShaderPackage::load(publication, budget, diagnostic);
    if (!package)
        std::fprintf(stderr, "Runtime Native source package: %s\n", diagnostic.c_str());
    assert(package);
    // Borrowed request bytes cannot change factory-frozen source intent.
    for (auto& entry : intent.bindings)
        for (auto& binding : entry)
            binding.relation.rootForm = shader::SpirvMslBufferMemoryLowering::RootForm::Block;
    for (const auto& module : package->modules()) {
        assert(module.nativeMemory.version() == 2u);
        for (const auto& binary : module.ordinaryBinaryMemory) {
            const shader::NativeBufferMemoryDeclarations* source = nullptr;
            assert(!rhi::BindlessShaderPackage::originalNativeBufferMemory(
                package, &module, binary.set, binary.binding, source));
            const auto* record = source->lookup(binary.set, binary.binding);
            using Form = shader::SpirvMslBufferMemoryLowering::RootForm;
            assert(record && record->relationVersion == 2u
                && record->rootForm
                    == (binary.hasRuntimeArray ? Form::RuntimeElement : Form::Value));
            auto forged = std::shared_ptr<const rhi::BindlessShaderPackage>(
                std::shared_ptr<void>{}, package.get());
            assert(rhi::BindlessShaderPackage::originalNativeBufferMemory(
                       forged, &module, binary.set, binary.binding, source)
                && !source);
        }
    }
    // A request1 or a value/runtime framing mutation must fail while loading,
    // before any selected pipeline or target facts can be published.
    intent.native[0].contractVersion = 1u;
    assert(!rhi::BindlessShaderPackage::load(publication, budget, diagnostic));
    intent.native[0].contractVersion = 2u;
    assert(!rhi::BindlessShaderPackage::load(publication, budget, diagnostic));
    budget.ordinaryMemory = nullptr;
    auto legacy = rhi::BindlessShaderPackage::load(publication, budget, diagnostic);
    assert(legacy);
    const shader::NativeBufferMemoryDeclarations* source = nullptr;
    assert(rhi::BindlessShaderPackage::originalNativeBufferMemory(
               legacy, &legacy->modules().front(), 2u, 3u, source)
        && !source);
    std::puts(
        "BINDLESS_RUNTIME_NATIVE_SOURCE2_OK original-frozen explicit-forms legacy-and-invalid-refusal no-target-authority");
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 2 || argc == 3);
    if (argc == 3)
        verifyNativeSourceCustody(argv[2]);
    auto quota = core::makeShared<core::CpuMemoryQuota>(4u * 1024u * 1024u);
    std::shared_ptr<const rhi::BindlessShaderPackage> package;
    core::pmr::String diagnostic;
    {
        core::CpuMemoryQuotaScope scope(quota);
        package = rhi::BindlessShaderPackage::load(
            argv[1], {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    }
    if (!package)
        std::fprintf(stderr, "Runtime member package: %s\n", diagnostic.c_str());
    assert(package);
    for (const bool fault : {false, true})
        for (const bool undersized : {false, true})
            test::validateOriginalBindlessRootSource(
                package,
                {},
                "unused",
                false,
                [=](const auto& root, const auto& sources, rhi::GpuDevice* device) {
                    test::OriginalGraphicsRuntimeMemoryFixture::inspect(
                        root, sources, device, fault, undersized);
                },
                Buffers{undersized},
                false,
                fault ? "faultFragment" : "cleanFragment");
    package.reset();
    assert(!quota->liveBytes());
    std::puts(
        "BINDLESS_GRAPHICS_RUNTIME_MEMBER2_NULL_OK exact-and-undersized original-generation retirement0 no-execution-grant");
}
