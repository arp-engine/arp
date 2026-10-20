#include "../fixtures/bindless_graphics_matrix_package_fixture.h"
#include "../fixtures/bindless_graphics_native_memory_package_fixture.h"
#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/original_bindless_root_source_fixture.h"
#include "../fixtures/original_graphics_matrix_memory_fixture.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/frontend/command_texture_ranges.h"
#include "rhi/semantics/bindless_fragment_store_lowering.h"
#include "rhi/semantics/bindless_graphics_entry_selection_lowering.h"
#include "rhi/semantics/bindless_graphics_member_memory_lowering.h"
#include "rhi/semantics/bindless_ordinary_buffer_range_lowering.h"
#include "rhi/semantics/bindless_ordinary_memory_lowering.h"

#ifdef NDEBUG
#error Graphics matrix memory tests require assertions
#endif

namespace {
// Deliberately untrusted metadata, used only to prove that a foreign pointer
// is rejected before any module fields can influence the selected entry.
class GraphicsEntryProbe final : public arp::rhi::RenderPipeline {
public:
    explicit GraphicsEntryProbe(const arp::rhi::RenderPipeline& original) : original_(original) {}

    const arp::rhi::BindlessPackageModule* vertex = nullptr;
    arp::rhi::ShaderStage stages = arp::rhi::ShaderStage::Vertex;
    bool authorized = true;

    const arp::rhi::PipelineLayout* layout() const override { return original_.layout(); }

    const arp::rhi::PipelineValidationReport& validationReport() const override
    {
        return original_.validationReport();
    }

    const arp::rhi::ShaderReflectionDesc& shaderReflection() const override
    {
        return original_.shaderReflection();
    }

    arp::rhi::ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return original_.shaderSourceLanguage();
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

    std::shared_ptr<const arp::rhi::BindlessAuthorization>
    bindlessAuthorization() const noexcept override
    {
        return authorized ? original_.bindlessAuthorization() : nullptr;
    }

    arp::rhi::ShaderStage bindlessShaderStages() const noexcept override { return stages; }

    const arp::rhi::BindlessPackageModule* bindlessShaderModule(
        arp::rhi::ShaderStage stage, std::uint32_t version) const noexcept override
    {
        return version == 1u && stage == arp::rhi::ShaderStage::Vertex ? vertex : nullptr;
    }

private:
    const arp::rhi::RenderPipeline& original_;
};

struct GraphicsRootBuffers {
    static constexpr bool enabled = false;
    bool undersized = false;

    void configureOrdinaryBuffer(arp::rhi::BufferDesc& desc,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        assert(set == 0u);
        desc.sizeBytes = binding.binding ? 16u : (undersized ? 319u : 320u);
    }

    void initializeOrdinaryBuffer(
        arp::rhi::GpuBuffer&, std::uint32_t, const arp::rhi::DescriptorBindingDesc&) const
    {}
};

struct GraphicsRootObserver {
    bool fault = false, undersized = false;

    // Exercise the real public state-access consumer, not a mirror of its
    // range arithmetic. Cold ownership inspection may retain an undersized
    // source, but a Graphics bind must refuse before any access is emitted.
    template <class Root, class Sources>
    void verifyAdmissionStream(const Root& root, const Sources& sources) const
    {
        using namespace arp;
        using K = rhi::CommandUsageKind;
        std::size_t attachment = SIZE_MAX;
        rhi::GpuResourceId target;
        for (std::size_t i = 0u; i < sources->sources().size(); ++i) {
            const auto* view =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    &sources->sources()[i]);
            if (view
                && (static_cast<std::uint32_t>((*view)->originalParentLease()->layout().usage)
                    & static_cast<std::uint32_t>(rhi::TextureUsage::RenderAttachment))) {
                attachment = i;
                target = rhi::OriginalCommandResourceSources::resource(sources->sources()[i]);
                break;
            }
        }
        assert(attachment != SIZE_MAX);
        const auto graphBytes = sources->originalBudget().maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Graph)];
        auto quota = core::makeShared<core::CpuMemoryQuota>(graphBytes);
        {
            const char* id = nullptr;
            rhi::RenderPassDesc desc;
            rhi::RenderPassColorAttachmentDesc color;
            color.target = target;
            color.format = rhi::TextureFormat::Rgba8Unorm;
            color.loadOp = rhi::LoadOp::Clear;
            color.storeOp = rhi::StoreOp::Store;
            desc.colorAttachments.push_back(color);
            auto pass = rhi::RenderPassArguments::prepareOriginal(
                desc, {1u, true, 1u, 0u, 0u, 0u, 0u, quota->limitBytes()}, quota, id);
            assert(pass && !id);
            const std::array<rhi::CommandUsage, 4u> commands{{{K::BeginRenderPass, 0u, 1u},
                {K::BindPreparedGraphicsBinding, 1u, 1u},
                {K::EndRenderPass, 2u, 0u},
                {K::Finish, 2u, 0u}}};
            const std::array<rhi::OriginalCommandUsage::Reference, 2u> references{
                {{static_cast<std::uint32_t>(attachment)},
                    {static_cast<std::uint32_t>(sources->originalLeaseIndex(root))}}};
            auto usage = rhi::OriginalCommandUsage::prepareOriginal(sources,
                commands.data(),
                commands.size(),
                references.data(),
                references.size(),
                quota,
                id);
            if (!usage)
                std::fprintf(stderr, "Graphics range admission usage: %s\n", id ? id : "missing");
            assert(usage && !id);
            core::pmr::Vector<rhi::CommandVariableArguments::Input> fields;
            fields.reserve(commands.size());
            for (const auto& command : commands)
                fields.push_back({*rhi::CommandScalarArguments::capture(
                    command.kind, rhi::CommandScalarArguments::None{})});
            auto arguments = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
                fields.size(),
                {1u, true, 4u, 0u, 0u, 0u, quota->limitBytes()},
                quota,
                id);
            assert(arguments && !id);
            auto scalars =
                rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, quota, id);
            assert(scalars && !id);
            const rhi::OriginalCommandInputs::OriginalDescriptor descriptor{0u, pass};
            auto inputs =
                rhi::OriginalCommandInputs::prepareOriginal(scalars, &descriptor, 1u, quota, id);
            assert(inputs && !id);
            const rhi::OriginalBindlessRootStateBudget budget{
                2u, true, 1u, root->size(), root->size() * sources->sources().size()};
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota}, [&]() noexcept -> const char* {
                    std::size_t emitted = 0u;
                    const auto result = rhi::common::OriginalCommandBindlessRootAccesses::visit(
                        inputs, budget, [&](const auto&) noexcept -> const char* {
                            ++emitted;
                            return nullptr;
                        });
                    assert(!emitted && !result.accesses);
                    assert(undersized ? result.diagnostic
                                && core::StringView(result.diagnostic)
                                    == "RHI-BINDLESS-ORDINARY-MEMORY-RANGE"
                                      : !result.diagnostic);
                    return nullptr;
                }));
        }
        assert(!quota->liveBytes());
    }

    template <class Root, class Sources>
    void operator()(const Root& root, const Sources& sources, arp::rhi::GpuDevice* device) const
    {
        arp::test::OriginalGraphicsMatrixMemoryFixture::inspect(
            root, sources, device, fault, undersized);
        if (!device)
            return;
        if (device->backend() == arp::rhi::Backend::Null)
            verifyAdmissionStream(root, sources);
        using namespace arp;
        const auto& pipeline =
            *std::get<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
                root->originalPrepared())
                 ->owners()
                 ->desc()
                 .pipeline;
        const auto& package = root->authorization()->package();
        GraphicsEntryProbe probe(pipeline);
        rhi::BindlessPackageModule foreign;
        probe.vertex = &foreign;
        const auto rejects = [&](const char* expected) {
            rhi::ShaderStage stage = rhi::ShaderStage::Compute;
            const char* diagnostic = nullptr;
            assert(!rhi::selectBindlessGraphicsEntryAccess(
                package, probe, "vertexMain", stage, diagnostic));
            assert(diagnostic && core::StringView(diagnostic) == expected
                && stage == rhi::ShaderStage::None);
        };
        rejects("RHI-BINDLESS-GRAPHICS-ENTRY-OWNER");
        probe.vertex = nullptr;
        rejects("RHI-BINDLESS-GRAPHICS-ENTRY-UNAVAILABLE");
        probe.vertex = pipeline.bindlessShaderModule(rhi::ShaderStage::Fragment, 1u);
        rejects("RHI-BINDLESS-GRAPHICS-ENTRY-MODULE");
        probe.stages = rhi::ShaderStage::Compute;
        rejects("RHI-BINDLESS-GRAPHICS-ENTRY-STAGE");
        probe.authorized = false;
        rejects("RHI-BINDLESS-GRAPHICS-ENTRY-AUTHORIZATION");
    }
};

void verifyCapture(const arp::rhi::BindlessShaderPackage& package)
{
    using namespace arp;
    using Entries = rhi::BindlessGraphicsPipelineEntries;
    rhi::ShaderPackageDesc view;
    core::pmr::String diagnostic;
    assert(rhi::appendBindlessShaderEntry(
        package, "vertexMain", rhi::ShaderStage::Vertex, view, diagnostic));
    assert(rhi::appendBindlessShaderEntry(
        package, "cleanFragment", rhi::ShaderStage::Fragment, view, diagnostic));
    rhi::RenderPipelineDesc desc;
    desc.shaderPackage = &view;
    desc.vertexShader = view.entrypoints[0];
    desc.fragmentShader = view.entrypoints[1];
    Entries captured;
    assert(!Entries::capture(package, desc, captured));
    const auto* vertex = captured.module(rhi::ShaderStage::Vertex, 1u);
    const auto* fragment = captured.module(rhi::ShaderStage::Fragment, 1u);
    assert(
        vertex && fragment && vertex->entry == "vertexMain" && fragment->entry == "cleanFragment");
    desc.fragmentShader.entryPoint = "faultFragment";
    assert(captured.module(rhi::ShaderStage::Fragment, 1u) == fragment);
    Entries fault;
    assert(!Entries::capture(package, desc, fault)
        && fault.module(rhi::ShaderStage::Fragment, 1u) != fragment
        && fault.module(rhi::ShaderStage::Fragment, 1u)->entry == "faultFragment");
    for (const auto version : {0u, 2u, 99u})
        assert(!fault.module(rhi::ShaderStage::Vertex, version));
    const auto rejects = [&](const char* expected) {
        Entries output = captured;
        const auto* id = Entries::capture(package, desc, output);
        assert(id && core::StringView(id) == expected
            && !output.module(rhi::ShaderStage::Vertex, 1u)
            && !output.module(rhi::ShaderStage::Fragment, 1u));
    };
    desc.fragmentShader.stage = rhi::ShaderStage::Compute;
    rejects("RHI-BINDLESS-GRAPHICS-ENTRY-STAGE");
    desc.fragmentShader.stage = rhi::ShaderStage::Fragment;
    desc.fragmentShader.modulePath = "unowned.slang";
    rejects("RHI-BINDLESS-GRAPHICS-ENTRY-SOURCE");
    desc.fragmentShader = view.entrypoints[1];
    desc.fragmentShader.entryPoint = "absent";
    rejects("RHI-BINDLESS-GRAPHICS-ENTRY-MODULE");
    desc.fragmentShader = {};
    assert(!Entries::capture(package, desc, captured)
        && captured.module(rhi::ShaderStage::Vertex, 1u) == vertex
        && !captured.module(rhi::ShaderStage::Fragment, 1u));
    desc.vertexShader.entryPoint = "";
    rejects("RHI-BINDLESS-GRAPHICS-ENTRY-MODULE");
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    using Store = rhi::semantics::OriginalBindlessFragmentStoreLowering;
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto identity =
            rhi::semantics::OriginalBindlessGraphicsEntrySelectionLowering::lowering(backend);
        assert(identity.inspectionAvailable && !identity.diagnostic && !identity.executionAvailable
            && identity.validatorOnly == (backend == rhi::Backend::Null));
        assert(rhi::semantics::OriginalBindlessGraphicsEntrySelectionLowering::lowering(backend, 2u)
                .diagnostic);
        const auto memory =
            rhi::semantics::OriginalBindlessOrdinaryMemoryLowering::lowering(backend, 2u);
        assert(memory.inspectionAvailable && !memory.diagnostic && !memory.executionAvailable);
        using Member = rhi::semantics::OriginalBindlessGraphicsMemberMemoryLowering;
        const auto member = Member::lowering(backend);
        assert(member.available && !member.executionAvailable && !member.diagnostic
            && member.validatorOnly == (backend == rhi::Backend::Null)
            && member.evidence
                == (backend == rhi::Backend::Vulkan ? Member::Evidence::NativeBinaryIdentity
                                                    : Member::Evidence::ValidatedTargetMembers));
        const auto runtimeMember = Member::lowering(backend, 2u);
        assert(!runtimeMember.executionAvailable);
        assert(runtimeMember.available && !runtimeMember.diagnostic
            && runtimeMember.evidence == member.evidence
            && runtimeMember.validatorOnly == member.validatorOnly);
        for (const auto version : {0u, 3u, 99u}) {
            const auto invalid = Member::lowering(backend, version);
            assert(!invalid.available && invalid.diagnostic
                && invalid.evidence == Member::Evidence::None);
        }
        const auto range =
            rhi::semantics::OriginalBindlessOrdinaryBufferRangeLowering::lowering(backend);
        assert(range.available && !range.executionAvailable && !range.diagnostic
            && range.validatorOnly == (backend == rhi::Backend::Null));
        assert(rhi::semantics::OriginalBindlessOrdinaryBufferRangeLowering::lowering(backend, 2u)
                .available);
        for (const auto version : {0u, 3u, 99u})
            assert(rhi::semantics::OriginalBindlessOrdinaryBufferRangeLowering::lowering(
                backend, version)
                    .diagnostic);
        const auto mapped = Store::lowering(
            backend, rhi::ShaderStage::Fragment, rhi::DescriptorType::StorageBuffer);
        assert(!mapped.diagnostic
            && mapped.execution
                == (backend == rhi::Backend::Null         ? rhi::BindlessExecution::Validator
                        : backend == rhi::Backend::WebGPU ? rhi::BindlessExecution::Emulated
                                                          : rhi::BindlessExecution::Native));
        assert(
            Store::lowering(backend, rhi::ShaderStage::Vertex, rhi::DescriptorType::StorageBuffer)
                .diagnostic);
        assert(
            Store::lowering(backend, rhi::ShaderStage::Fragment, rhi::DescriptorType::UniformBuffer)
                .diagnostic);
        assert(Store::lowering(
            backend, rhi::ShaderStage::Fragment, rhi::DescriptorType::StorageBuffer, 99u)
                .diagnostic);
    }
    assert(rhi::semantics::OriginalBindlessGraphicsEntrySelectionLowering::lowering(
        rhi::Backend::WebGL)
            .diagnostic);
    assert(rhi::semantics::OriginalBindlessOrdinaryMemoryLowering::lowering(rhi::Backend::WebGL, 2u)
            .diagnostic);
    assert(
        rhi::semantics::OriginalBindlessGraphicsMemberMemoryLowering::lowering(rhi::Backend::WebGL)
            .diagnostic);
    assert(rhi::semantics::OriginalBindlessGraphicsMemberMemoryLowering::lowering(
        static_cast<rhi::Backend>(UINT32_MAX))
            .diagnostic);
    assert(Store::lowering(
        rhi::Backend::WebGL, rhi::ShaderStage::Fragment, rhi::DescriptorType::StorageBuffer)
            .diagnostic);
    rhi_metal::MetalRhiNativeFeatureProbe probe;
    assert(
        rhi::semantics::OriginalBindlessOrdinaryBufferRangeLowering::lowering(rhi::Backend::WebGL)
            .diagnostic);
    probe.argumentBuffersTier2 = probe.nativeSync = true;
    probe.maxStorageBufferBindingsPerStage = 31u;
    probe.maxStorageBufferBindingSizeBytes = 65536u;
    const auto hasStore = [](const auto& caps) {
        return (static_cast<std::uint32_t>(caps.support[1u][5u].operations)
                   & static_cast<std::uint32_t>(rhi::BindlessOperation::Store))
            != 0u;
    };
    assert(!hasStore(
        rhi_metal::metalBindlessComputeCapabilities(probe, false, true, 123u, false, true)));
    assert(hasStore(rhi_metal::metalBindlessComputeCapabilities(
        probe, false, true, 123u, false, true, false, true)));
    assert(!hasStore(rhi_metal::metalBindlessComputeCapabilities(
        probe, false, false, 123u, false, true, false, true)));
    assert(!hasStore(rhi_metal::metalBindlessComputeCapabilities(
        probe, false, true, 0u, false, true, false, true)));
    assert(!hasStore(rhi_metal::metalBindlessComputeCapabilities(
        probe, false, true, 123u, false, false, false, true)));
    assert(argc == 3);
    const std::filesystem::path path = argv[1];
    test::BindlessGraphicsMatrixPackageFixture proof(path.parent_path());
    auto quota = core::makeShared<core::CpuMemoryQuota>(4u * 1024u * 1024u);
    rhi::BindlessPackageLoadBudget budget{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u, &proof.request};
    core::pmr::String diagnostic;
    diagnostic.reserve(256u);
    const auto factory = [&] {
        core::CpuMemoryQuotaScope scope(quota);
        return rhi::BindlessShaderPackage::load(path, budget, diagnostic);
    };
    budget.ordinaryMemory = nullptr;
    assert(!factory() && diagnostic == "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-MEMORY");
    budget.ordinaryMemory = &proof.request;
    auto package = factory();
    if (!package)
        std::fprintf(stderr, "Graphics matrix proof: %s\n", diagnostic.c_str());
    assert(package && package->modules().size() == 3u);
    verifyCapture(*package);
    // A real canonical Null inventory exercises transactional attachment and
    // graph retention without treating validator success as GPU execution.
    test::validateOriginalBindlessRootSource(package,
        {},
        "unused",
        false,
        test::OriginalBindlessRootFixtureObserver{},
        GraphicsRootBuffers{},
        false,
        "cleanFragment",
        "vertexMain",
        false,
        test::OrdinaryRootMutation::None,
        rhi::TextureFormat::Rgba8Unorm,
        true);
    for (const bool fault : {false, true})
        for (const bool undersized : {false, true})
            test::validateOriginalBindlessRootSource(package,
                {},
                "unused",
                !undersized,
                GraphicsRootObserver{fault, undersized},
                GraphicsRootBuffers{undersized},
                false,
                fault ? "faultFragment" : "cleanFragment");
    for (const auto& module : package->modules())
        assert(module.reflection.reflection.pushConstantSizeBytes == 0u);
    const auto charged = quota->liveBytes();
    --proof.request.entryCount;
    assert(!factory() && quota->liveBytes() == charged);
    ++proof.request.entryCount;
    const auto* entries = proof.request.entries;
    proof.request.entries = nullptr;
    assert(!factory() && quota->liveBytes() == charged);
    proof.request.entries = entries;
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{quota}, [&]() noexcept -> const char* {
            for (const auto name : {"vertexMain", "cleanFragment", "faultFragment"}) {
                shader::TargetBufferMemoryLayout facts;
                assert(!rhi::BindlessShaderPackage::originalOrdinaryBufferMemory(
                           package, name, 0u, 0u, facts)
                    && facts.source.staticOccupiedExtentBytes == 320u
                    && facts.target.minimumBindingBytes == 320u);
                assert(!rhi::BindlessShaderPackage::originalOrdinaryBufferMemory(
                           package, name, 0u, 1u, facts)
                    && facts.source.staticOccupiedExtentBytes == 16u);
            }
            return nullptr;
        }));
    package.reset();
    assert(!quota->liveBytes());
    {
        test::BindlessGraphicsNativeMemoryPackageFixture nativeProof(path.parent_path());
        budget.ordinaryMemory = &nativeProof.request;
        const auto nativeFactory = [&] {
            core::CpuMemoryQuotaScope scope(quota);
            return rhi::BindlessShaderPackage::load(argv[2], budget, diagnostic);
        };
        package = nativeFactory();
        if (!package)
            std::fprintf(stderr, "Native source custody: %s\n", diagnostic.c_str());
        assert(package);
        test::validateOriginalBindlessRootSource(
            package,
            {},
            "unused",
            false,
            [](const auto& root, const auto&, rhi::GpuDevice* device) {
                using Root = rhi::OriginalCommandBindlessRootSource;
                rhi::ShaderBufferMemberMemory members{
                    1u, 1u, rhi::ShaderBufferMemberMemory::Evidence::ValidatedTargetMembers};
                rhi::BindlessResourceAccess access;
                std::size_t source = 0u;
                const auto* id = Root::originalOrdinaryBufferMemberMemory(
                    root, "vertexMain", 0u, 0u, members, access, source);
                assert(id && !members.sourceOccupiedBytes && !members.targetMinimumBytes
                    && members.evidence == rhi::ShaderBufferMemberMemory::Evidence::None
                    && !access.resource.isValid() && source == SIZE_MAX);
                const auto* admission = Root::validateOriginalGraphicsBufferRanges(root);
                assert(admission);
                if (device) {
                    assert(device->backend() == rhi::Backend::Null);
                    assert(
                        core::StringView(id) == "RHI-NULL-MEMBER-MEMORY-NATIVE-TARGET-UNAVAILABLE"
                        && core::StringView(admission) == id);
                }
            },
            GraphicsRootBuffers{},
            false,
            "cleanFragment");
        const auto chargedNative = quota->liveBytes();
        const auto rejectsNative = [&](const char* expected) {
            assert(!nativeFactory());
            if (diagnostic.find(expected) == diagnostic.npos)
                std::fprintf(
                    stderr, "Native custody expected %s got %s\n", expected, diagnostic.c_str());
            assert(diagnostic.find(expected) != diagnostic.npos
                && quota->liveBytes() == chargedNative);
        };
        nativeProof.request.contractVersion = 1u;
        rejectsNative("NATIVE-VERSION");
        nativeProof.request.contractVersion = 2u;
        nativeProof.entries[0].buffers.contractVersion = 1u;
        rejectsNative("NATIVE-REQUEST");
        nativeProof.entries[0].buffers.contractVersion = 0u;
        nativeProof.entries[0].native = nullptr;
        rejectsNative("NATIVE-REQUEST");
        nativeProof.entries[0].native = &nativeProof.native[0];
        const auto originalEntry = nativeProof.entries[0].entry;
        nativeProof.entries[0].entry = "unpublishedNativeEntry";
        rejectsNative("MEMORY-ENTRY");
        nativeProof.entries[0].entry = originalEntry;
        nativeProof.bindings[0][1].binding = 0u;
        rejectsNative("BINDING-DUPLICATE");
        nativeProof.bindings[0][1].binding = 1u;
        --nativeProof.native[0].bindingCount;
        rejectsNative("NATIVE-COVERAGE");
        ++nativeProof.native[0].bindingCount;
        nativeProof.native[0].sourceTypes.maxNodes = 1u;
        rejectsNative("ORDINARY-BUFFER-BINARY");
        nativeProof.native[0].sourceTypes =
            test::BindlessGraphicsNativeMemoryPackageFixture::graphBudget;
        nativeProof.bindings[0][0].relation.maxPairs = 1u;
        rejectsNative("PAIR-BUDGET");
        nativeProof.bindings[0][0].relation.maxPairs = 1024u;
        nativeProof.bindings[0][0].relation.maxMatrices = 0u;
        rejectsNative("MEMORY-PLAN");
        nativeProof.bindings[0][0].relation.maxMatrices = 1u;
        nativeProof.bindings[0][0].relation.matrixCount = 0u;
        rejectsNative("MATRIX-PROOF");
        nativeProof.bindings[0][0].relation.matrixCount = 1u;
        const auto originalSourceType = nativeProof.matrices[0].sourceType;
        nativeProof.matrices[0].sourceType = SIZE_MAX;
        rejectsNative("MEMORY-MATRIX");
        nativeProof.matrices[0].sourceType = originalSourceType;
        nativeProof.matrices[0].targetType = SIZE_MAX;
        rejectsNative("MEMORY-MATRIX");
        nativeProof.matrices[0].targetType = 2u;
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(!rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic));
            assert(diagnostic.find("MEMORY-EXECUTION") != diagnostic.npos);
            assert(quota->liveBytes() == chargedNative);
        }
        rhi::BindlessPackageModule foreign;
        // Mutable caller intent cannot change any frozen rule or source graph.
        nativeProof.matrices[0].targetType = SIZE_MAX;
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                const auto* module = &package->modules()[0];
                const shader::NativeBufferMemoryDeclarations* source = nullptr;
                assert(!rhi::BindlessShaderPackage::originalNativeBufferMemory(
                    package, module, 0u, 0u, source));
                assert(source && source->version() == 1u);
                const auto* record = source->lookup(0u, 0u);
                assert(record && record->plan().matrices[0].targetType == 2u
                    && source->sourceTypes().nodes()[record->sourceType].occupiedExtentBytes
                        == 320u);
                shader::TargetBufferMemoryLayout target;
                assert(rhi::BindlessShaderPackage::originalOrdinaryBufferMemory(
                           package, module->entry, 0u, 0u, target)
                    && !target.target.minimumBindingBytes);
                assert(rhi::BindlessShaderPackage::originalNativeBufferMemory(
                           package, &foreign, 0u, 0u, source)
                    && !source);
                auto forged =
                    std::shared_ptr<const rhi::BindlessShaderPackage>(quota, package.get());
                assert(rhi::BindlessShaderPackage::originalNativeBufferMemory(
                           forged, module, 0u, 0u, source)
                    && !source);
                return nullptr;
            }));
        package.reset();
        assert(!quota->liveBytes());
    }
    budget.ordinaryMemory = nullptr;
    {
        core::CpuMemoryQuotaScope scope(quota);
        package = rhi::BindlessShaderPackage::load(argv[2], budget, diagnostic);
    }
    assert(package && package->modules().size() == 3u);
    assert(
        rhi_metal::MetalRhiBindlessDomainValidator(package->artifact(), true).validate().success);
    package.reset();
    assert(!quota->liveBytes());
    std::puts(
        "BINDLESS_GRAPHICS_MATRIX_MEMORY_OK Vertex Fragment clean/fault same320+16 explicit-per-entry hot0 retirement0 no-execution-grant");
}
