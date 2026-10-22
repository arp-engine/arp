#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/original_bindless_root_source_fixture.h"
#include "core/io/file_io.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/semantics/bindless_ordinary_memory_lowering.h"
#include "shader/shader_artifact_files.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Matrix package memory tests require assertions
#endif

namespace {
struct MatrixRootBuffers {
    static constexpr bool enabled = false;
    bool undersized = false;

    void configureOrdinaryBuffer(arp::rhi::BufferDesc& desc,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        assert(set == 0u);
        desc.sizeBytes = binding.binding == 0u ? (undersized ? 319u : 320u) : 16u;
    }

    void initializeOrdinaryBuffer(
        arp::rhi::GpuBuffer&, std::uint32_t, const arp::rhi::DescriptorBindingDesc&) const
    {}
};

struct MatrixRootObserver {
    bool undersized = false;

    template <class Sources>
    void operator()(const std::shared_ptr<const arp::rhi::OriginalCommandBindlessRootSource>& root,
        const Sources& sources,
        arp::rhi::GpuDevice* device) const
    {
        using namespace arp;
        using Root = rhi::OriginalCommandBindlessRootSource;
        shader::TargetBufferMemoryLayout facts;
        rhi::BindlessResourceAccess access;
        std::size_t source = 0u;
        if (!device) {
            assert(Root::originalOrdinaryBufferMemory(
                       root, "computeMain", 0u, 0u, facts, access, source)
                && !facts.target.minimumBindingBytes && !access.resource.isValid()
                && source == SIZE_MAX);
            return;
        }
        const auto quota = root->originalMetadataQuota();
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                const auto* id = Root::originalOrdinaryBufferMemory(
                    root, "computeMain", 0u, 0u, facts, access, source);
                if (undersized) {
                    assert(id && core::StringView(id) == "RHI-BINDLESS-ORDINARY-MEMORY-RANGE"
                        && !facts.target.minimumBindingBytes && !access.resource.isValid()
                        && source == SIZE_MAX);
                } else {
                    assert(!id && facts.target.minimumBindingBytes == 320u
                        && access.bufferSize == 320u && source < sources->sources().size());
                    const auto* original =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            &sources->sources()[source]);
                    assert(original && (*original)->resource() == access.resource);
                }
                assert(!Root::originalOrdinaryBufferMemory(
                           root, "computeMain", 0u, 1u, facts, access, source)
                    && facts.target.minimumBindingBytes == 16u);
                assert(Root::originalOrdinaryBufferMemory(
                           root, "absent", 0u, 0u, facts, access, source)
                    && !facts.target.minimumBindingBytes && !access.resource.isValid()
                    && source == SIZE_MAX);
                auto forged = std::shared_ptr<const Root>(quota, root.get());
                assert(Root::originalOrdinaryBufferMemory(
                    forged, "computeMain", 0u, 0u, facts, access, source));
                assert(Root::originalOrdinaryBufferMemory(
                    root, "computeMain", 0u, 0u, facts, access, source, 99u));
                return nullptr;
            }));
    }
};
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    using Package = rhi::BindlessShaderPackage;
    using Request = shader::TargetBufferMemoryLoadRequest;
    using Graph = shader::BufferDeclarationTypeLayouts;
    using Lowering = shader::SpirvWgslBufferMemoryLowering;
    assert(argc == 3);
    const std::filesystem::path path = argv[1];
    const auto binary = core::readBinaryFile(
        path.parent_path() / "MatrixOrdinary.emulated.computeMain.spv", 1024u * 1024u);
    const auto wgsl = core::readBinaryFile(
        path.parent_path() / "MatrixOrdinary.emulated.computeMain.wgsl", 1024u * 1024u);
    assert(binary.success && wgsl.success);
    constexpr Graph::Budget graphBudget{512u, 512u, 16u};
    Graph source, target;
    core::pmr::Vector<shader::SpirvBufferDeclarationLayout> sourceFacts;
    core::pmr::Vector<shader::WgslBufferDeclarationLayout> targetFacts;
    assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
        binary.bytes.data(), binary.bytes.size(), 16u, graphBudget, sourceFacts, source));
    assert(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
        shader::shaderArtifactText(wgsl.bytes), 16u, graphBudget, targetFacts, target));
    // Authored fixture: one f32 4x4 type, one four-column target wrapper.
    Lowering::MatrixColumnValue rule;
    rule.targetForm = Lowering::MatrixTargetForm::ColumnArrayStruct;
    for (std::size_t i = 0u; i < source.nodes().size(); ++i)
        if (source.nodes()[i].kind == Graph::Kind::Matrix) {
            assert(rule.sourceType == Graph::absent);
            rule.sourceType = i;
        }
    for (std::size_t i = 0u; i < target.nodes().size(); ++i) {
        const auto& n = target.nodes()[i];
        if (n.kind != Graph::Kind::Struct || n.memberCount != 1u)
            continue;
        const auto& a = target.nodes()[target.members()[n.memberBegin].type];
        if (a.kind != Graph::Kind::FixedArray || a.count != 4u
            || target.nodes()[a.child].kind != Graph::Kind::Vector)
            continue;
        assert(rule.targetType == Graph::absent);
        rule.targetType = i;
    }
    assert(rule.sourceType != Graph::absent && rule.targetType != Graph::absent);
    std::array<Request::BindingPlan, 2u> plans{
        {{0u, 0u, Lowering::RootForm::BlockStruct, {nullptr, 0u, &rule, 1u, 2u}},
            {0u, 1u, Lowering::RootForm::BlockStruct, {nullptr, 0u}}}};
    auto& plan = plans[0];
    Request buffers{2u, graphBudget, graphBudget, 1024u, 0u};
    buffers.bindings = plans.data();
    buffers.bindingCount = plans.size();
    buffers.maxMatrixRules = 1u;
    rhi::BindlessPackageMemoryLoadRequest::Entry entry{"computeMain", buffers};
    rhi::BindlessPackageMemoryLoadRequest request{1u, &entry, 1u};
    rhi::BindlessPackageLoadBudget budget{
        1024u * 1024u, 4u * 1024u * 1024u, 16u, 2u, 16u, 4u, &request};
    auto quota = core::makeShared<core::CpuMemoryQuota>(2u * 1024u * 1024u);
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    diagnostic.reserve(256u);
    const auto factory = [&] {
        core::CpuMemoryQuotaScope scope(quota);
        return Package::load(path, budget, diagnostic);
    };
    budget.ordinaryMemory = nullptr;
    assert(!factory() && diagnostic == "RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-MEMORY");
    budget.ordinaryMemory = &request;
    auto value = factory();
    if (!value)
        std::fprintf(stderr, "Matrix package request: %s\n", diagnostic.c_str());
    assert(value);
    const auto live = quota->liveBytes();
    const auto rejects = [&](const char* expected) {
        assert(!factory());
        if (diagnostic.find(expected) == diagnostic.npos)
            std::fprintf(stderr, "Expected %s got %s\n", expected, diagnostic.c_str());
        assert(diagnostic.find(expected) != diagnostic.npos && quota->liveBytes() == live);
    };
    request.contractVersion = 99u;
    rejects("MEMORY-REQUEST");
    request.contractVersion = 1u;
    request.entries = nullptr;
    rejects("MEMORY-REQUEST");
    request.entries = &entry;
    request.entryCount = 3u;
    rejects("MEMORY-REQUEST");
    request.entryCount = 1u;
    entry.entry = "";
    rejects("MEMORY-ENTRY");
    entry.entry = "computeMain";
    std::array<rhi::BindlessPackageMemoryLoadRequest::Entry, 2u> duplicateEntries{entry, entry};
    request.entries = duplicateEntries.data();
    request.entryCount = 2u;
    rejects("MEMORY-ENTRY-DUPLICATE");
    request.entries = &entry;
    request.entryCount = 1u;
    entry.entry = "unmatchedEntry";
    rejects("MEMORY-ENTRY");
    entry.entry = "computeMain";
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!Package::load(argv[2], budget, diagnostic)
            && diagnostic == "RHI-BINDLESS-PACKAGE-MEMORY-EXECUTION" && quota->liveBytes() == live);
        budget.ordinaryMemory = nullptr;
        auto native = Package::load(argv[2], budget, diagnostic);
        assert(native);
        shader::TargetBufferMemoryLayout facts;
        assert(Package::originalOrdinaryBufferMemory(native, "computeMain", 0u, 0u, facts));
        native.reset();
        assert(quota->liveBytes() == live);
        budget.ordinaryMemory = &request;
    }
    entry.buffers.maxMatrixRules = 0u;
    rejects("MATRIX-RULE-BUDGET");
    entry.buffers.maxMatrixRules = 1u;
    entry.buffers.bindingCount = 1u;
    rejects("MEMORY-COVERAGE");
    entry.buffers.bindingCount = 2u;
    plans[1].binding = 0u;
    rejects("PLAN-DUPLICATE");
    plans[1].binding = 1u;
    std::array<Lowering::MatrixColumnValue, 2u> duplicateRules{rule, rule};
    plan.arrays.matrixColumnValues = duplicateRules.data();
    plan.arrays.matrixCount = 2u;
    entry.buffers.maxMatrixRules = 2u;
    rejects("PLAN-DUPLICATE");
    plan.arrays = {nullptr, 0u, &rule, 1u, 2u};
    entry.buffers.maxMatrixRules = 1u;
    plans[1].arrays = {nullptr, 0u, &rule, 1u, 2u};
    rejects("MATRIX-RULE-BUDGET");
    entry.buffers.maxMatrixRules = 2u;
    rejects("PLAN-UNUSED");
    plans[1].arrays = {nullptr, 0u};
    entry.buffers.maxMatrixRules = 1u;
    plan.arrays.matrixCount = 0u;
    rejects("MATRIX-PROOF");
    plan.arrays.matrixCount = 1u;
    const auto saved = rule;
    rule.sourceType = Graph::absent;
    rejects("MATRIX-PLAN-FRAMING");
    rule = saved;
    entry.buffers.maxPairs = 1u;
    rejects("PAIR-BUDGET");
    entry.buffers.maxPairs = 1024u;
    entry.buffers.targetTypes.maxNodes = 1u;
    rejects("ORDINARY-BUFFER-WGSL");
    entry.buffers.targetTypes = graphBudget;
    plan.set = 1u;
    rejects("MEMORY-COVERAGE");
    plan.set = 0u;
    entry.buffers.push = true;
    rejects("MEMORY-PUSH");
    entry.buffers.push = false;
    entry.buffers.pushArrays = {nullptr, 0u, &rule, 1u, 2u};
    entry.buffers.maxMatrixRules = 2u;
    rejects("MEMORY-PUSH");
    entry.buffers.pushArrays = {nullptr, 0u};
    entry.buffers.maxMatrixRules = 1u;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto hostile = factory();
    std::pmr::set_default_resource(previous);
    assert(hostile);
    hostile.reset();
    assert(quota->liveBytes() == live);
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto decision =
            rhi::semantics::OriginalBindlessOrdinaryMemoryLowering::lowering(backend);
        assert(decision.inspectionAvailable && !decision.executionAvailable
            && decision.validatorOnly == (backend == rhi::Backend::Null));
        assert(!rhi::semantics::OriginalBindlessOrdinaryMemoryLowering::lowering(backend, 99u)
                .inspectionAvailable);
    }
    for (const bool undersized : {false, true})
        test::validateOriginalBindlessRootSource(value,
            {},
            "computeMain",
            true,
            MatrixRootObserver{undersized},
            MatrixRootBuffers{undersized});
    assert(quota->liveBytes() == live);
    const auto id = value->sha256();
    request.entries = nullptr;
    rule.sourceType = Graph::absent;
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{quota}, [&]() noexcept -> const char* {
            shader::TargetBufferMemoryLayout facts;
            assert(!Package::originalOrdinaryBufferMemory(value, "computeMain", 0u, 1u, facts)
                && facts.source.staticOccupiedExtentBytes == 16u
                && facts.target.minimumBindingBytes == 16u);
            assert(!Package::originalOrdinaryBufferMemory(value, "computeMain", 0u, 0u, facts)
                && facts.source.staticOccupiedExtentBytes == 320u
                && facts.target.minimumBindingBytes == 320u);
            auto forged = std::shared_ptr<const Package>(quota, value.get());
            facts.target.minimumBindingBytes = UINT64_MAX;
            assert(Package::originalOrdinaryBufferMemory(forged, "computeMain", 0u, 0u, facts)
                && !facts.target.minimumBindingBytes);
            assert(Package::originalOrdinaryBufferMemory(value, "absent", 0u, 0u, facts)
                && !facts.target.minimumBindingBytes);
            assert(Package::originalOrdinaryBufferMemory(value, "computeMain", 1u, 2u, facts)
                && !facts.target.minimumBindingBytes);
            assert(value->sha256() == id);
            return nullptr;
        }));
    value.reset();
    assert(!quota->liveBytes());
    std::puts(
        "BINDLESS_PACKAGE_MATRIX_MEMORY1_OK request2 original-owner frozen320 hot0 missing-invalid-unused-budget-closed no-execution-grant");
    return 0;
}
