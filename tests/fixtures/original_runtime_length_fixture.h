#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_single_entry_input.h"
#include "shader/graphics_shader_module_artifact.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace arp::test {
// Authored fixture policy: scalar runtime arrays, two logical bindings, no push.
class OriginalRuntimeLengthFixture final {
public:
    using Entry = shader::SingleEntryShaderArtifact;
    using Module = shader::GraphicsShaderModuleArtifact;
    using Facts = shader::RuntimeArrayLengthOperations::Facts;
    using Memory = shader::TargetBufferMemoryLoadRequest;
    Memory::BindingPlan plans[2]{
        {0u, 0u, Memory::Lowering::RootForm::StorageRuntimeArray, {nullptr, 0u}},
        {0u, 1u, Memory::Lowering::RootForm::StorageRuntimeArray, {nullptr, 0u}}};
    Memory memory{1u, {128u, 128u, 3u}, {128u, 128u, 3u}, 128u, 0u, plans, 2u};
    shader::RuntimeLengthOperationLoadRequest operations{1u, {8u, 2048u}};
    shader::GraphicsTargetMemoryLoadRequest graphicsMemory{
        1u, memory, memory, {128u, 128u, 3u}, {128u, 128u, 3u}, 128u};
    shader::GraphicsRuntimeLengthOperationLoadRequest graphicsOperations{
        1u, operations, operations, operations, memory};

    OriginalRuntimeLengthFixture() { graphicsMemory.vertex.bindingCount = 1u; }

    shader::SingleEntryShaderLoadBudget entryBudget() const noexcept
    {
        return {1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 4u, &memory, &operations};
    }

    shader::GraphicsShaderModuleLoadBudget moduleBudget() const noexcept
    {
        return {1024u * 1024u,
            16u * 1024u * 1024u,
            4u * 1024u * 1024u,
            4u,
            4u,
            256u,
            2u,
            &graphicsMemory,
            &graphicsOperations};
    }

    template <class Artifact>
    static void hot(const std::shared_ptr<const Artifact>& original, std::size_t count)
    {
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{original->quota()}, [&]() noexcept -> const char* {
                for (std::size_t i = 0u; i < count; ++i) {
                    Facts facts;
                    facts.sourceResultId = UINT32_MAX;
                    assert(!Artifact::originalRuntimeLengthOperation(original, i, facts)
                        && facts.sourceResultId && facts.runtimeStrideBytes
                        && !facts.runtimeOffsetBytes && !facts.sourceMember);
                    assert(core::StringView(shader::shaderArtifactText(
                                                original->bytes(shader::ShaderArtifactRole::Wgsl)))
                               .substr(facts.targetByteOffset, 11u)
                        == "arrayLength");
                }
                Facts facts;
                facts.sourceResultId = UINT32_MAX;
                assert(Artifact::originalRuntimeLengthOperation(original, count, facts)
                    && !facts.sourceResultId);
                auto forged = std::shared_ptr<const Artifact>(original->quota(), original.get());
                for (const auto& owner : std::array{std::shared_ptr<const Artifact>{}, forged}) {
                    facts.sourceResultId = UINT32_MAX;
                    assert(Artifact::originalRuntimeLengthOperation(owner, 0u, facts)
                        && !facts.sourceResultId);
                }
                return Artifact::validateOriginal(original);
            }));
        const auto quota = original->quota();
        const auto limit = quota->limitBytes();
        quota->setLimitBytes(limit - 1u);
        Facts facts;
        facts.sourceResultId = UINT32_MAX;
        assert(
            Artifact::originalRuntimeLengthOperation(original, 0u, facts) && !facts.sourceResultId);
        quota->setLimitBytes(limit);
        assert(original->bytes(shader::ShaderArtifactRole::Metal).empty()
            && original->targetDiagnostic(shader::ShaderArtifactRole::Metal)
                == "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"
            && !original->nativeExecutionProven());
    }

    template <class Artifact> static void targets(const std::shared_ptr<const Artifact>& original)
    {
        using Input = std::conditional_t<std::is_same_v<Artifact, Entry>,
            rhi::OriginalSingleEntryPipelineInput,
            rhi::OriginalGraphicsPipelineInput>;
        constexpr std::uint64_t metadataBytes = 64u * 1024u; // authored test-only input quota
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        auto input = Input::prepare(original, {1u, metadataBytes}, quota, diagnostic);
        assert(!input && diagnostic == "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"
            && !quota->liveBytes());
        // No input owner is published. Every backend refuses its absent owner.
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto target = Input::selectTarget(input, backend);
            assert(target.diagnostic && !target.validatorOnly);
            if constexpr (std::is_same_v<Artifact, Entry>)
                assert(!target.bytes);
            else
                assert(!target.sourceCount && !target.sources[0] && !target.sources[1]);
        }
    }

    template <class Factory>
    static void allocations(const std::shared_ptr<core::CpuMemoryQuota>& quota, Factory&& factory)
    {
        CpuQuotaFactoryFixture fixture(quota);
        const auto peak = fixture.measure(factory);
        fixture.verify(peak, factory);
        const auto before = quota->liveBytes();
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto hostile = factory();
        std::pmr::set_default_resource(previous);
        assert(hostile);
        hostile.reset();
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - before);
            assert(!factory());
        }
        assert(quota->liveBytes() == before);
    }

    static void verify(
        const std::filesystem::path& path, bool graphics = false, bool deleteInputs = false)
    {
        OriginalRuntimeLengthFixture fixture;
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(256u);
        if (graphics) {
            auto budget = fixture.moduleBudget();
            auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
            const auto factory = [&] {
                return Module::load(path, budget, quota, diagnostic);
            };
            auto value = factory();
            if (!value)
                std::fprintf(stderr, "Original runtime graphics: %s\n", diagnostic.c_str());
            assert(value);
            allocations(quota, factory);
            hot(value, 3u);
            hot(value->vertex(), 1u);
            hot(value->fragment(), 2u);
            targets(value);
            const auto baseline = quota->liveBytes();
            budget.runtimeLength = nullptr;
            assert(!factory() && diagnostic.find("REQUEST-REQUIRED") != diagnostic.npos);
            budget.runtimeLength = &fixture.graphicsOperations;
            fixture.graphicsOperations.combined.operations.maxTokens = 1u;
            assert(!factory() && diagnostic.find("TOKEN-BUDGET") != diagnostic.npos
                && quota->liveBytes() == baseline);
            fixture.graphicsOperations.combined.operations.maxTokens = 2048u;
            fixture.graphicsOperations.fragment.operations.maxOperations = 1u;
            assert(!factory() && diagnostic.find("OPERATION-BUDGET") != diagnostic.npos
                && quota->liveBytes() == baseline);
            fixture.graphicsOperations.fragment.operations.maxOperations = 8u;
            // Copies and mutated requests do not change records in an original owner.
            auto copied = fixture.graphicsOperations;
            copied.contractVersion = 99u;
            budget.runtimeLength = &copied;
            assert(!factory() && quota->liveBytes() == baseline);
            fixture.graphicsOperations.combinedMemory.bindings = nullptr;
            fixture.graphicsOperations.combinedMemory.bindingCount = SIZE_MAX;
            if (deleteInputs)
                std::filesystem::remove_all(path.parent_path());
            hot(value, 3u);
            hot(value->vertex(), 1u);
            hot(value->fragment(), 2u);
        } else {
            auto budget = fixture.entryBudget();
            auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
            const auto factory = [&] {
                return Entry::load(path, budget, quota, diagnostic);
            };
            auto value = factory();
            if (!value)
                std::fprintf(stderr, "Original runtime entry: %s\n", diagnostic.c_str());
            assert(value);
            allocations(quota, factory);
            hot(value, 1u);
            targets(value);
            const auto baseline = quota->liveBytes();
            budget.runtimeLength = nullptr;
            assert(!factory() && diagnostic.find("REQUEST-REQUIRED") != diagnostic.npos);
            budget.runtimeLength = &fixture.operations;
            budget.targetMemory = nullptr;
            assert(!factory() && diagnostic.find("REQUEST") != diagnostic.npos);
            budget.targetMemory = &fixture.memory;
            fixture.operations.operations.maxOperations = 0u;
            assert(!factory() && diagnostic.find("BUDGET") != diagnostic.npos);
            fixture.operations.operations = {8u, 1u};
            assert(!factory() && diagnostic.find("TOKEN-BUDGET") != diagnostic.npos);
            fixture.operations = {99u, {8u, 2048u}};
            assert(!factory() && diagnostic.find("REQUEST") != diagnostic.npos);
            assert(quota->liveBytes() == baseline);
            fixture.memory.bindings = nullptr;
            fixture.memory.bindingCount = SIZE_MAX;
            if (deleteInputs)
                std::filesystem::remove_all(path.parent_path());
            hot(value, 1u);
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_RUNTIME_LENGTH_OK original-owner byte-operation-role frozen-request hostile-pmr quota-cleanup hot0 no-native-bound-authority");
    }
};
} // namespace arp::test
