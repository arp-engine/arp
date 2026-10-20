#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_shader_usage_validation.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/semantics/command_state_lowering.h"

#include <array>
#include <cstdio>
#include <string_view>

namespace arp::test {
class TypedShaderUsageScopeContractFixture final {
    using C = rhi::semantics::TypedShaderUsageScopeContract;
    using K = rhi::CommandUsageKind;

    struct Operation {
        std::size_t commandIndex;
        K kind;
    };

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(
                stderr, "usage scope expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

    template <std::size_t N> static auto operations(const std::array<K, N>& kinds)
    {
        std::array<Operation, N> output{};
        for (std::size_t i = 0u; i < N; ++i)
            output[i] = {i, kinds[i]};
        return output;
    }

    static C::BufferUse use(std::size_t command,
        C::Scope scope,
        C::BufferUsage usage,
        std::uint32_t binding = 0u,
        C::Contribution contribution = C::Contribution::ShaderConsumption)
    {
        C::BufferUse u;
        u.resource = {1u};
        u.bufferBytes = 128u;
        u.offsetBytes = binding ? 64u : 0u;
        u.sizeBytes = 64u;
        u.commandIndex = command;
        u.binding = binding;
        u.scope = scope;
        u.usage = usage;
        u.contribution = contribution;
        u.access = {usage == C::BufferUsage::Storage ? rhi::ResourceState::ShaderWrite
                                                     : rhi::ResourceState::ShaderRead,
            scope.kind == C::Kind::ComputeDispatch ? rhi::PipelineStage::ComputeShader
                                                   : rhi::PipelineStage::FragmentShader,
            usage == C::BufferUsage::Storage
                ? rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite
                : rhi::ResourceAccess::ShaderRead};
        return u;
    }

    static void verifyDependentPassScopes()
    {
        const auto sequence = operations(std::array{K::BeginComputePass,
            K::BindComputePipeline,
            K::BindComputeDescriptor,
            K::BindComputeDescriptor,
            K::Dispatch,
            K::EndComputePass,
            K::BufferMemoryBarrier,
            K::BufferMemoryBarrier,
            K::BeginComputePass,
            K::BindComputePipeline,
            K::BindComputeDescriptor,
            K::BindComputeDescriptor,
            K::Dispatch,
            K::EndComputePass,
            K::Finish});
        const C::Budget budget{1u, true, sequence.size(), 4u, 6u};
        C::Scope first, second;
        assert(
            !C::resolve(sequence, budget, 4u, first) && !C::resolve(sequence, budget, 12u, second));
        std::array uses{use(4u, first, C::BufferUsage::StorageRead),
            use(4u, first, C::BufferUsage::Storage, 1u),
            use(12u, second, C::BufferUsage::StorageRead),
            use(12u, second, C::BufferUsage::Storage, 1u)};
        // Exact A/B identities exchange roles across distinct dispatch scopes.
        uses[0].resource = {1u};
        uses[1].resource = {2u};
        uses[2].resource = {2u};
        uses[3].resource = {1u};
        for (auto& u : uses) {
            u.offsetBytes = 0u;
            u.access.state = rhi::ResourceState::ShaderWrite;
        }
        const auto result = C::inspect(sequence, uses, budget);
        assert(!result.diagnostic && result.contributions == 4u && result.pairChecks == 6u
            && !result.synchronizationProven() && !result.nativeExecutionProven());
        auto invalid = uses;
        invalid[1].resource = invalid[0].resource;
        is(C::inspect(sequence, invalid, budget).diagnostic,
            "RHI-TYPED-USAGE-SCOPE-INCOMPATIBLE-BUFFER-USAGE");
        invalid = uses;
        invalid[2].scope = first;
        is(C::inspect(sequence, invalid, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-BINDING");
        auto less = budget;
        --less.maxPairChecks;
        auto rejected = C::inspect(sequence, uses, less);
        is(rejected.diagnostic, "RHI-TYPED-USAGE-SCOPE-PAIR-CAPACITY");
        assert(!rejected.contributions && !rejected.pairChecks);
        std::puts(
            "BINDLESS_RHI_TYPED_SHADER_DEPENDENCY_PROBE_CONTRACT_OK independent-passes A-B-A exact-identities scope-alias-less-refusal synchronization-unproven four-native-gates-closed");
    }

public:
    static void verifyContract()
    {
        verifyGeneralShaderResources();
        const auto drawGeometry = operations(std::array{
            K::BeginRenderPass, K::BindGraphicsPipeline, K::Draw, K::EndRenderPass, K::Finish});
        C::Budget drawBudget{4u, true, drawGeometry.size(), 1u, 0u};
        C::Scope drawScope;
        assert(!C::resolve(drawGeometry, drawBudget, 2u, drawScope));
        auto drawRead =
            use(2u, drawScope, C::BufferUsage::Vertex, 0u, C::Contribution::GeometryConsumption);
        drawRead.access = {rhi::ResourceState::ShaderRead,
            rhi::PipelineStage::VertexInput,
            rhi::ResourceAccess::VertexAttributeRead};
        assert(!C::validateBufferContribution(drawGeometry, drawBudget, drawRead));
        assert(!C::inspect(drawGeometry, std::array{drawRead}, drawBudget).diagnostic);
        auto wrong = drawRead;
        wrong.usage = C::BufferUsage::Index;
        wrong.access.access = rhi::ResourceAccess::IndexRead;
        is(C::validateBufferContribution(drawGeometry, drawBudget, wrong),
            "RHI-TYPED-USAGE-SCOPE-GEOMETRY-ACCESS");
        wrong = drawRead;
        wrong.contribution = C::Contribution::GeometryBinding;
        is(C::validateBufferContribution(drawGeometry, drawBudget, wrong),
            "RHI-TYPED-USAGE-SCOPE-GEOMETRY-ACCESS");
        for (const auto version : {1u, 2u, 3u}) {
            auto old = drawBudget;
            old.contractVersion = version;
            is(C::validateBufferContribution(drawGeometry, old, drawRead),
                "RHI-TYPED-USAGE-SCOPE-GEOMETRY-PROFILE");
            is(C::inspect(drawGeometry, std::array{drawRead}, old).diagnostic,
                "RHI-TYPED-USAGE-SCOPE-GEOMETRY-PROFILE");
        }
        std::puts(
            "BINDLESS_RHI_GEOMETRY_CONSUMPTION_SCOPE_OK draw-vertex-only indexed-independent distinct-binding-consumption old-profiles-refused");
        const auto indexed = operations(std::array{K::BeginRenderPass,
            K::BindGraphicsPipeline,
            K::BindRenderDescriptor,
            K::BindIndexBuffer,
            K::DrawIndexed,
            K::EndRenderPass,
            K::Finish});
        C::Budget indexedBudget{4u, true, indexed.size(), 1u, 1u};
        C::Scope indexedScope;
        assert(!C::resolve(indexed, indexedBudget, 4u, indexedScope));
        auto indexedGeometry = drawRead;
        indexedGeometry.commandIndex = 4u;
        indexedGeometry.scope = indexedScope;
        indexedGeometry.usage = C::BufferUsage::Index;
        indexedGeometry.access.access = rhi::ResourceAccess::IndexRead;
        assert(!C::validateBufferContribution(indexed, indexedBudget, indexedGeometry));
        const std::array indexedUse{use(4u, indexedScope, C::BufferUsage::StorageRead)};
        const auto indexedResult = C::inspect(indexed, indexedUse, indexedBudget);
        assert(!indexedResult.diagnostic && indexedResult.contributions == 1u
            && !indexedResult.nativeExecutionProven());
        indexedBudget.contractVersion = 3u;
        is(C::validateOperations(indexed, indexedBudget), "RHI-TYPED-USAGE-SCOPE-COMMAND-PROFILE");
        verifyGeometryAccess();
        verifyGeometryScopes();
        verifyDependentPassScopes();
        verifyReadbackScopes();
        const auto tables = operations(std::array{K::BeginRenderPass,
            K::BindGraphicsPipeline,
            K::BindRenderDescriptorTable,
            K::BindRenderDescriptorTable,
            K::Draw,
            K::EndRenderPass,
            K::Finish});
        const C::Budget textureBudget{2u, true, tables.size(), 10u, 45u};
        assert(!C::validateOperations(tables, textureBudget));
        auto oldTexture = textureBudget;
        oldTexture.contractVersion = 1u;
        is(C::validateOperations(tables, oldTexture), "RHI-TYPED-USAGE-SCOPE-COMMAND-PROFILE");
        C::TextureUse sampled;
        sampled.resource = {1u};
        sampled.registrationGeneration = 7u;
        sampled.range = {1u, 1u, 2u, 1u, rhi::TextureAspect::Depth};
        sampled.commandIndex = 2u;
        sampled.usage = C::TextureUsage::SampledRead;
        sampled.contribution = C::Contribution::RenderBinding;
        sampled.access = {rhi::ResourceState::ShaderRead,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderRead};
        assert(!C::resolve(tables, textureBudget, 2u, sampled.scope));
        assert(!C::validateTextureContribution(tables, textureBudget, sampled));
        auto duplicate = sampled;
        duplicate.commandIndex = 3u;
        assert(!C::validateTexturePair(sampled, duplicate));
        duplicate.registrationGeneration = 8u;
        is(C::validateTexturePair(sampled, duplicate), "RHI-TYPED-USAGE-SCOPE-TEXTURE-IDENTITY");
        auto attachment = sampled;
        attachment.commandIndex = 0u;
        attachment.usage = C::TextureUsage::Attachment;
        attachment.contribution = C::Contribution::Attachment;
        attachment.access = {rhi::ResourceState::DepthAttachment,
            rhi::PipelineStage::DepthStencil,
            rhi::ResourceAccess::DepthStencilRead | rhi::ResourceAccess::DepthStencilWrite};
        assert(!C::validateTextureContribution(tables, textureBudget, attachment));
        is(C::validateTexturePair(sampled, attachment),
            "RHI-TYPED-USAGE-SCOPE-TEXTURE-USAGE-ALIAS");
        is(C::validateTexturePair(attachment, attachment),
            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ATTACHMENT-ALIAS");
        duplicate = attachment;
        duplicate.range.baseMipLevel = 0u;
        assert(!C::validateTexturePair(sampled, duplicate));
        duplicate = attachment;
        duplicate.range.baseArrayLayer = 1u;
        assert(!C::validateTexturePair(sampled, duplicate));
        duplicate = attachment;
        duplicate.range.aspect = rhi::TextureAspect::Stencil;
        assert(!C::validateTexturePair(sampled, duplicate));
        duplicate = sampled;
        duplicate.access = {rhi::ResourceState::ShaderWrite,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderWrite};
        is(C::validateTexturePair(sampled, duplicate), "RHI-TYPED-USAGE-SCOPE-TEXTURE-USAGE-ALIAS");
        is(C::validateTextureContribution(tables, textureBudget, duplicate),
            "RHI-TYPED-USAGE-SCOPE-TEXTURE-SHADER-ACCESS");
        duplicate = sampled;
        duplicate.contribution = C::Contribution::ShaderConsumption;
        is(C::validateTextureContribution(tables, textureBudget, duplicate),
            "RHI-TYPED-USAGE-SCOPE-CONTRIBUTION-KIND");
        duplicate.commandIndex = 4u;
        assert(!C::validateTextureContribution(tables, textureBudget, duplicate));
        duplicate.scope.last = 4u;
        is(C::validateTextureContribution(tables, textureBudget, duplicate),
            "RHI-TYPED-USAGE-SCOPE-BINDING");
        const auto compute = operations(std::array{K::BeginComputePass,
            K::BindComputePipeline,
            K::BindComputeDescriptor,
            K::Dispatch,
            K::BindComputeDescriptor,
            K::Dispatch,
            K::EndComputePass,
            K::Finish});
        const auto render = operations(std::array{K::BeginRenderPass,
            K::BindGraphicsPipeline,
            K::BindRenderDescriptor,
            K::BindRenderDescriptor,
            K::Draw,
            K::EndRenderPass,
            K::Finish});
        const C::Budget budget{1u, true, 8u, 2u, 1u};
        C::Scope first, second;
        assert(!C::resolve(compute, budget, 3u, first) && !C::resolve(compute, budget, 5u, second)
            && !first.matches(second));
        assert(first.kind == C::Kind::ComputeDispatch && first.first == 3u && first.last == 3u);
        C::Scope cleared = first;
        is(C::resolve(compute, budget, 2u, cleared), "RHI-TYPED-USAGE-SCOPE-CONTRIBUTION-POSITION");
        assert(cleared.kind == C::Kind::Unspecified && cleared.first == SIZE_MAX
            && cleared.last == SIZE_MAX);
        std::array uses{
            use(3u, first, C::BufferUsage::Storage), use(5u, second, C::BufferUsage::StorageRead)};
        auto result = C::inspect(compute, uses, budget);
        assert(!result.diagnostic && result.contributions == 2u && result.pairChecks == 1u
            && !result.synchronizationProven() && !result.nativeExecutionProven());
        assert(!C::resolve(render, budget, 2u, first) && !C::resolve(render, budget, 3u, second)
            && first.matches(second));
        assert(first.kind == C::Kind::RenderPass && first.first == 0u && first.last == 5u);
        // Shadowed and unused render binds remain in the whole-pass scope.
        uses = {use(2u, first, C::BufferUsage::Storage, 0u, C::Contribution::RenderBinding),
            use(3u, second, C::BufferUsage::StorageRead, 1u, C::Contribution::RenderBinding)};
        const auto rejected = C::inspect(render, uses, budget);
        is(rejected.diagnostic, "RHI-TYPED-USAGE-SCOPE-INCOMPATIBLE-BUFFER-USAGE");
        assert(!rejected.contributions && !rejected.pairChecks);
        // All-storage is compatible even for overlapping shadowed bind ranges;
        // the per-draw writable-alias rule is checked separately.
        uses[1].usage = C::BufferUsage::Storage;
        uses[1].offsetBytes = 0u;
        assert(!C::inspect(render, uses, budget).diagnostic);
        assert(!C::resolve(render, budget, 4u, first));
        uses = {
            use(4u, first, C::BufferUsage::Storage), use(4u, first, C::BufferUsage::Storage, 1u)};
        assert(!C::inspect(render, uses, budget).diagnostic);
        uses[1].offsetBytes = 63u;
        is(C::inspect(render, uses, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-WRITABLE-ALIAS");
        uses[1].offsetBytes = 64u;
        uses[1].usage = C::BufferUsage::StorageRead;
        uses[1].access.access = rhi::ResourceAccess::ShaderRead;
        is(C::inspect(render, uses, budget).diagnostic,
            "RHI-TYPED-USAGE-SCOPE-INCOMPATIBLE-BUFFER-USAGE");
        uses[0] = use(4u, first, C::BufferUsage::StorageRead);
        assert(!C::inspect(render, uses, budget).diagnostic);
        uses[1].resource = {2u};
        uses[1].binding = 0u;
        is(C::inspect(render, uses, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-DUPLICATE-BINDING");
        uses[1].binding = 1u;
        uses[1].sizeBytes = UINT64_MAX;
        is(C::inspect(render, uses, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-BUFFER-RANGE");
        uses[1] = use(4u, first, C::BufferUsage::StorageRead, 1u);
        uses[1].bufferBytes = 129u;
        is(C::inspect(render, uses, budget).diagnostic,
            "RHI-TYPED-USAGE-SCOPE-BUFFER-SIZE-BINDING");
        uses[1] = use(4u, first, C::BufferUsage::StorageRead, 1u);
        uses[1].access.stages = rhi::PipelineStage::ComputeShader;
        is(C::inspect(render, uses, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-SHADER-ACCESS");
        uses[1] = use(4u, first, C::BufferUsage::StorageRead, 1u);
        uses[1].scope.last = 4u;
        is(C::inspect(render, uses, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-BINDING");
        uses[1] = use(4u, first, C::BufferUsage::StorageRead, 1u);
        auto less = budget;
        less.maxPairChecks = 0u;
        is(C::inspect(render, uses, less).diagnostic, "RHI-TYPED-USAGE-SCOPE-PAIR-CAPACITY");
        less = budget;
        less.maxContributions = 1u;
        is(C::inspect(render, uses, less).diagnostic,
            "RHI-TYPED-USAGE-SCOPE-CONTRIBUTION-CAPACITY");
        less = budget;
        less.maxCommands = 6u;
        is(C::inspect(render, uses, less).diagnostic, "RHI-TYPED-USAGE-SCOPE-COMMAND-CAPACITY");
        less = budget;
        less.contractVersion = 0u;
        is(C::validateBudget(less), "RHI-TYPED-USAGE-SCOPE-VERSION");
        less = budget;
        less.prototypeOptIn = false;
        is(C::validateBudget(less), "RHI-TYPED-USAGE-SCOPE-OPT-IN");
        auto bad = render;
        bad[4].commandIndex = 3u;
        is(C::inspect(bad, uses, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-POSITION");
        bad = render;
        bad[4].kind = K::DrawIndexed;
        is(C::inspect(bad, uses, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-COMMAND-PROFILE");
        bad = render;
        bad[5].kind = K::Finish;
        assert(C::inspect(bad, uses, budget).diagnostic);
        auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto d = C::lowering(backend);
                    assert(d.diagnostic && !d.coldJoinAvailable && !d.recordingAvailable
                        && !d.executionAvailable);
                }
                rhi::ResourceBarrierDesc barrier;
                barrier.resource = {1u};
                barrier.before = rhi::ResourceState::ShaderWrite;
                barrier.after = rhi::ResourceState::ShaderRead;
                barrier.sourceStages = barrier.destinationStages =
                    rhi::PipelineStage::ComputeShader;
                barrier.sourceAccess = rhi::ResourceAccess::ShaderWrite;
                barrier.destinationAccess = rhi::ResourceAccess::ShaderRead;
                is(C::rejectUnfrozenTransition(barrier, barrier.before),
                    "RHI-TYPED-USAGE-SCOPE-DEPENDENCY-NOT-FROZEN");
                rhi::ResourceMemoryBarrierDesc memory;
                memory.resource = {1u};
                memory.sourceStages = memory.destinationStages = rhi::PipelineStage::ComputeShader;
                memory.sourceAccess = rhi::ResourceAccess::ShaderWrite;
                memory.destinationAccess = rhi::ResourceAccess::ShaderRead;
                is(C::rejectUnfrozenMemory(memory, rhi::ResourceState::ShaderWrite),
                    "RHI-TYPED-USAGE-SCOPE-DEPENDENCY-NOT-FROZEN");
                barrier.before = rhi::ResourceState::CopyDst;
                assert(C::rejectUnfrozenTransition(barrier, rhi::ResourceState::ShaderWrite));
                return C::inspect(render, uses, budget).diagnostic;
            }));
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_USAGE_SCOPE_VALUE_OK positional1 compute-per-dispatch render-whole-pass shadowed-unused-binds whole-buffer-compatibility storage-exception separate-writable-alias bounded-pairs exact-less full-pressure hot0 dependency-unfrozen four-native-gates-closed no-original-coverage-or-sync-proof");
    }

private:
    static void verifyGeneralShaderResources()
    {
        const auto render = operations(std::array{K::BeginRenderPass,
            K::BindGraphicsPipeline,
            K::BindRenderDescriptor,
            K::BindRenderDescriptorTable,
            K::Draw,
            K::Draw,
            K::EndRenderPass,
            K::Finish});
        const C::Budget budget{5u, true, render.size(), 4u, 6u};
        C::Scope scope;
        assert(!C::resolve(render, budget, 4u, scope));
        auto uniform = use(4u, scope, C::BufferUsage::UniformRead);
        auto readonly = use(4u, scope, C::BufferUsage::StorageRead, 1u);
        std::array bufferUses{uniform, readonly};
        assert(!C::inspect(render, bufferUses, budget).diagnostic);
        auto old = budget;
        old.contractVersion = 4u;
        is(C::inspect(render, bufferUses, old).diagnostic, "RHI-TYPED-USAGE-SCOPE-BUFFER-PROFILE");
        bufferUses[0].usage = C::BufferUsage::Storage;
        is(C::inspect(render, bufferUses, budget).diagnostic,
            "RHI-TYPED-USAGE-SCOPE-INCOMPATIBLE-BUFFER-USAGE");
        uniform.commandIndex = 2u;
        uniform.contribution = C::Contribution::RenderBinding;
        assert(!C::validateBufferContribution(render, budget, uniform));
        uniform.commandIndex = 3u;
        assert(!C::validateBufferContribution(render, budget, uniform));
        uniform.access.stages = rhi::PipelineStage::ComputeShader;
        is(C::validateBufferContribution(render, budget, uniform),
            "RHI-TYPED-USAGE-SCOPE-SHADER-ACCESS");
        uniform.access = {rhi::ResourceState::ShaderWrite,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderWrite};
        is(C::validateBufferContribution(render, budget, uniform),
            "RHI-TYPED-USAGE-SCOPE-SHADER-ACCESS");
        C::TextureUse storage;
        storage.resource = {9u};
        storage.registrationGeneration = 7u;
        storage.range = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
        storage.commandIndex = 4u;
        storage.scope = scope;
        storage.contribution = C::Contribution::ShaderConsumption;
        storage.usage = C::TextureUsage::Storage;
        storage.access = {rhi::ResourceState::ShaderWrite,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite};
        assert(!C::validateTextureContribution(render, budget, storage));
        is(C::validateTextureContribution(render, old, storage),
            "RHI-TYPED-USAGE-SCOPE-TEXTURE-PROFILE");
        auto other = storage;
        is(C::validateTexturePair(storage, other), "RHI-TYPED-USAGE-SCOPE-TEXTURE-WRITABLE-ALIAS");
        other.commandIndex = 5u;
        assert(!C::validateTextureContribution(render, budget, other));
        assert(!C::validateTexturePair(storage, other));
        other = storage;
        other.commandIndex = 2u;
        other.contribution = C::Contribution::RenderBinding;
        assert(!C::validateTextureContribution(render, budget, other));
        assert(!C::validateTexturePair(storage, other));
        other.commandIndex = 3u;
        assert(!C::validateTextureContribution(render, budget, other));
        other = storage;
        other.range.baseMipLevel = 1u;
        assert(!C::validateTexturePair(storage, other));
        other = storage;
        other.range.baseArrayLayer = 1u;
        assert(!C::validateTexturePair(storage, other));
        const std::array disjoint{storage, other};
        const auto disjointResult = C::inspect(render, disjoint, budget);
        assert(!disjointResult.diagnostic && disjointResult.contributions == 2u
            && disjointResult.pairChecks == 1u && !disjointResult.synchronizationProven()
            && !disjointResult.nativeExecutionProven());
        auto less = budget;
        less.maxPairChecks = 0u;
        const auto noPairs = C::inspect(render, disjoint, less);
        is(noPairs.diagnostic, "RHI-TYPED-USAGE-SCOPE-PAIR-CAPACITY");
        assert(!noPairs.contributions && !noPairs.pairChecks);
        less = budget;
        less.maxContributions = 1u;
        is(C::inspect(render, disjoint, less).diagnostic,
            "RHI-TYPED-USAGE-SCOPE-CONTRIBUTION-CAPACITY");
        other = storage;
        const auto aliased = C::inspect(render, std::array{storage, other}, budget);
        is(aliased.diagnostic, "RHI-TYPED-USAGE-SCOPE-TEXTURE-WRITABLE-ALIAS");
        assert(!aliased.contributions && !aliased.pairChecks);
        other = storage;
        other.registrationGeneration = 8u;
        is(C::validateTexturePair(storage, other), "RHI-TYPED-USAGE-SCOPE-TEXTURE-IDENTITY");
        other = storage;
        other.usage = C::TextureUsage::StorageRead;
        other.access.access = rhi::ResourceAccess::ShaderRead;
        assert(!C::validateTextureContribution(render, budget, other));
        is(C::validateTexturePair(storage, other), "RHI-TYPED-USAGE-SCOPE-TEXTURE-USAGE-ALIAS");
        auto sampled = other;
        sampled.usage = C::TextureUsage::SampledRead;
        sampled.access.state = other.access.state = rhi::ResourceState::ShaderRead;
        assert(!C::validateTextureContribution(render, budget, sampled));
        assert(!C::validateTexturePair(sampled, other));
        assert(!C::inspect(render, std::array{sampled, other}, budget).diagnostic);
        other.range.mipLevelCount = 0u;
        is(C::validateTextureContribution(render, budget, other),
            "RHI-TYPED-USAGE-SCOPE-TEXTURE-IDENTITY");
        other = sampled;
        other.usage = C::TextureUsage::StorageRead;
        other.access = {rhi::ResourceState::ShaderWrite,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderWrite};
        is(C::validateTextureContribution(render, budget, other),
            "RHI-TYPED-USAGE-SCOPE-TEXTURE-SHADER-ACCESS");
        other = storage;
        other.range.aspect = rhi::TextureAspect::Depth;
        is(C::validateTextureContribution(render, budget, other),
            "RHI-TYPED-USAGE-SCOPE-TEXTURE-STORAGE-ASPECT");
        other = storage;
        other.access.stages = rhi::PipelineStage::ComputeShader;
        is(C::validateTextureContribution(render, budget, other),
            "RHI-TYPED-USAGE-SCOPE-TEXTURE-SHADER-ACCESS");
        const auto compute = operations(std::array{K::BeginComputePass,
            K::BindComputePipeline,
            K::BindComputeDescriptorTable,
            K::Dispatch,
            K::Dispatch,
            K::EndComputePass,
            K::Finish});
        C::Budget computeBudget = budget;
        computeBudget.maxCommands = compute.size();
        storage.commandIndex = 3u;
        storage.access.stages = rhi::PipelineStage::ComputeShader;
        assert(!C::resolve(compute, computeBudget, 3u, storage.scope));
        assert(!C::validateTextureContribution(compute, computeBudget, storage));
        other = storage;
        other.commandIndex = 4u;
        assert(!C::resolve(compute, computeBudget, 4u, other.scope));
        assert(!C::validateTexturePair(storage, other));
        uniform = use(3u, storage.scope, C::BufferUsage::UniformRead);
        assert(!C::validateBufferContribution(compute, computeBudget, uniform));
        auto vertex =
            use(4u, scope, C::BufferUsage::Vertex, 1u, C::Contribution::GeometryConsumption);
        vertex.access = {rhi::ResourceState::ShaderRead,
            rhi::PipelineStage::VertexInput,
            rhi::ResourceAccess::VertexAttributeRead};
        uniform = use(4u, scope, C::BufferUsage::UniformRead);
        assert(!C::inspect(render, std::array{uniform, vertex}, budget).diagnostic);
        vertex.usage = C::BufferUsage::Index;
        vertex.access.access = rhi::ResourceAccess::IndexRead;
        // Indexed geometry keeps its existing explicit operation profile.
        const auto indexed = operations(std::array{K::BeginRenderPass,
            K::BindGraphicsPipeline,
            K::BindIndexBuffer,
            K::BindRenderDescriptor,
            K::BindRenderDescriptorTable,
            K::DrawIndexed,
            K::Draw,
            K::EndRenderPass,
            K::Finish});
        auto indexedBudget = budget;
        indexedBudget.maxCommands = indexed.size();
        assert(!C::resolve(indexed, indexedBudget, 5u, scope));
        uniform.commandIndex = vertex.commandIndex = 5u;
        uniform.scope = vertex.scope = scope;
        assert(!C::inspect(indexed, std::array{uniform, vertex}, indexedBudget).diagnostic);
        const auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto d = C::lowering(backend);
                    assert(d.diagnostic && !d.recordingAvailable && !d.executionAvailable);
                }
                return C::validateTextureContribution(compute, computeBudget, storage);
            }));
        std::puts(
            "BINDLESS_RHI_GENERAL_SHADER_USAGE5_VALUE_OK uniform-constant storage-texture-read-write readonly-mix whole-pass-storage-exception per-draw-writable-alias compute-per-dispatch legacy-refused hot0 four-execution-gates-closed");
    }

    static void verifyGeometryScopes()
    {
        std::uint8_t pairs = 1u;
        assert(core::countUnorderedPairsUnsigned<std::uint8_t>(0u, pairs) && pairs == 0u);
        assert(core::countUnorderedPairsUnsigned<std::uint8_t>(1u, pairs) && pairs == 0u);
        assert(core::countUnorderedPairsUnsigned<std::uint8_t>(23u, pairs) && pairs == 253u);
        assert(!core::countUnorderedPairsUnsigned<std::uint8_t>(24u, pairs) && pairs == 0u);
        std::uint64_t wide = 1u;
        assert(!core::countUnorderedPairsUnsigned(UINT64_MAX, wide) && wide == 0u);
        const auto render = operations(std::array{K::BeginRenderPass,
            K::BindGraphicsPipeline,
            K::BindVertexBuffer,
            K::BindIndexBuffer,
            K::BindRenderDescriptor,
            K::Draw,
            K::EndRenderPass,
            K::Finish});
        C::Budget budget{4u, true, render.size(), 3u, 3u};
        auto vertex = use(2u, {}, C::BufferUsage::Vertex, 0u, C::Contribution::GeometryBinding);
        vertex.access = {rhi::ResourceState::ShaderRead,
            rhi::PipelineStage::VertexInput,
            rhi::ResourceAccess::VertexAttributeRead};
        auto index = vertex;
        index.commandIndex = 3u;
        index.binding = 1u;
        index.usage = C::BufferUsage::Index;
        index.offsetBytes = 64u;
        index.access.access = rhi::ResourceAccess::IndexRead;
        auto shader = use(4u, {}, C::BufferUsage::StorageRead, 0u, C::Contribution::RenderBinding);
        std::array uses{vertex, index, shader};
        for (auto& item : uses)
            assert(!C::resolve(render, budget, item.commandIndex, item.scope));
        auto result = C::inspect(render, uses, budget);
        assert(!result.diagnostic && result.contributions == 3u && result.pairChecks == 3u
            && !result.synchronizationProven() && !result.nativeExecutionProven());
        for (auto old : {1u, 2u, 3u}) {
            auto b = budget;
            b.contractVersion = old;
            is(C::inspect(render, uses, b).diagnostic, "RHI-TYPED-USAGE-SCOPE-COMMAND-PROFILE");
        }
        auto bad = uses;
        bad[2].usage = C::BufferUsage::Storage;
        bad[2].access = {rhi::ResourceState::ShaderWrite,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite};
        // Whole-buffer writable compatibility ignores disjoint byte ranges and
        // whether the binding is shadowed or actually consumed by a draw.
        bad[2].offsetBytes = 96u;
        bad[2].sizeBytes = 32u;
        is(C::inspect(render, bad, budget).diagnostic,
            "RHI-TYPED-USAGE-SCOPE-INCOMPATIBLE-BUFFER-USAGE");
        bad = uses;
        bad[0].access.access = rhi::ResourceAccess::IndexRead;
        is(C::inspect(render, bad, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-GEOMETRY-ACCESS");
        bad = uses;
        bad[1].contribution = C::Contribution::ShaderConsumption;
        is(C::inspect(render, bad, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-GEOMETRY-ACCESS");
        bad = uses;
        bad[1].access.stages = rhi::PipelineStage::VertexShader;
        is(C::inspect(render, bad, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-GEOMETRY-ACCESS");
        auto less = budget;
        --less.maxPairChecks;
        is(C::inspect(render, uses, less).diagnostic, "RHI-TYPED-USAGE-SCOPE-PAIR-CAPACITY");
        auto draw = render;
        draw[5].kind = K::DrawIndexed;
        const auto indexed = C::inspect(draw, uses, budget);
        assert(!indexed.diagnostic && !indexed.nativeExecutionProven());
        draw[5].kind = K::DrawIndexedIndirect;
        is(C::inspect(draw, uses, budget).diagnostic, "RHI-TYPED-USAGE-SCOPE-COMMAND-PROFILE");
        std::puts(
            "BINDLESS_RHI_GEOMETRY_SCOPE_CONTRACT_OK scope4 vertex-index-readonly-compatible writable-disjoint-alias-refused indexed-CPU-only old-profiles-indirect-native-closed");
    }

    static void verifyGeometryAccess()
    {
        using S = rhi::PipelineStage;
        using A = rhi::ResourceAccess;
        using R = rhi::ResourceState;
        using U = rhi::ResourceUsage;
        using V = rhi::semantics::ResourceAccessUsageValidation;
        for (const auto access :
            {A::VertexAttributeRead, A::IndexRead, A::VertexAttributeRead | A::IndexRead}) {
            assert(rhi::semantics::resourceAccessReads(access)
                && !rhi::semantics::resourceAccessWrites(access));
            assert(!rhi::semantics::validateResourceAccessScope(
                {R::ShaderRead, S::VertexInput, access}));
            is(rhi::semantics::validateResourceAccessScope(
                   {R::ShaderRead, S::VertexShader, access}),
                "RHI-ACCESS-STAGE-MISMATCH");
            is(rhi::semantics::validateResourceAccessScope(
                   {R::ShaderWrite, S::VertexInput, access}),
                "RHI-ACCESS-STATE-MISMATCH");
            rhi::TextureDesc texture;
            texture.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment;
            is(V::validate(texture, access), "RHI-ORIGINAL-ACCESS-TEXTURE-USAGE");
        }
        rhi::BufferDesc buffer;
        buffer.sizeBytes = 16u;
        buffer.usage = U::VertexBuffer | U::IndexBuffer;
        assert(!V::validate(buffer, A::VertexAttributeRead | A::IndexRead));
        buffer.usage = U::StorageBuffer;
        is(V::validate(buffer, A::VertexAttributeRead), "RHI-ORIGINAL-ACCESS-BUFFER-USAGE");
        is(V::validate(buffer, A::IndexRead), "RHI-ORIGINAL-ACCESS-BUFFER-USAGE");
        buffer.usage = U::VertexBuffer;
        assert(!V::validate(buffer, A::VertexAttributeRead));
        is(V::validate(buffer, A::IndexRead), "RHI-ORIGINAL-ACCESS-BUFFER-USAGE");
        buffer.usage = U::IndexBuffer;
        assert(!V::validate(buffer, A::IndexRead));
        is(V::validate(buffer, A::VertexAttributeRead), "RHI-ORIGINAL-ACCESS-BUFFER-USAGE");
        using L = rhi::semantics::GeometryInputAccessLowering;
        rhi::ResourceBarrierDesc transition;
        transition.destinationStages = S::VertexInput;
        transition.destinationAccess = A::IndexRead;
        transition.sourceStages = S::Transfer;
        transition.sourceAccess = A::TransferWrite;
        buffer.usage = U::IndexBuffer | U::CopyDst;
        assert(!V::validateGeometryBarrier(buffer, transition, R::CopyDst, R::ShaderRead));
        auto wrong = transition;
        wrong.destinationStages = S::VertexShader;
        is(V::validateGeometryBarrier(buffer, wrong, R::CopyDst, R::ShaderRead),
            "RHI-ACCESS-STAGE-MISMATCH");
        buffer.usage = U::IndexBuffer;
        is(V::validateGeometryBarrier(buffer, transition, R::CopyDst, R::ShaderRead),
            "RHI-ORIGINAL-ACCESS-BUFFER-USAGE");
        rhi::TextureDesc texture;
        texture.usage = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
        is(V::validateGeometryBarrier(texture, transition, R::CopyDst, R::ShaderRead),
            "RHI-ORIGINAL-ACCESS-TEXTURE-USAGE");
        rhi::ResourceMemoryBarrierDesc memory;
        memory.sourceStages = S::VertexInput;
        memory.sourceAccess = A::VertexAttributeRead;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = L::geometryInputAccessLowering(backend);
            assert(decision.contractVersion == 1u);
            assert((decision.diagnostic == nullptr) == decision.barrierMappingAvailable);
            assert(L::geometryInputBarrierDiagnostic(backend, transition) == decision.diagnostic);
            assert(L::geometryInputBarrierDiagnostic(backend, memory) == decision.diagnostic);
            rhi::ResourceBarrierDesc ordinary;
            ordinary.destinationStages = S::ComputeShader;
            ordinary.destinationAccess = A::ShaderRead;
            assert(!L::geometryInputBarrierDiagnostic(backend, ordinary));
        }
        std::puts(
            "BINDLESS_RHI_GEOMETRY_ACCESS_CONTRACT_OK vertex-input index-vertex-reads exact-state-stage-usage Null-validator Vulkan-mapping Metal-WebGPU-dependency-closed no-draw-authority");
    }

    static void verifyReadbackScopes()
    {
        const auto mixed = operations(std::array{K::BeginRenderPass,
            K::BindGraphicsPipeline,
            K::BindRenderDescriptorTable,
            K::Draw,
            K::EndRenderPass,
            K::TextureBarrier,
            K::BeginTransferPass,
            K::TextureReadback,
            K::TextureReadback,
            K::EndTransferPass,
            K::Finish});
        const C::Budget budget{3u, true, mixed.size(), 4u, 6u};
        assert(!C::validateOperations(mixed, budget));
        C::Scope first, second;
        assert(!C::resolve(mixed, budget, 7u, first) && !C::resolve(mixed, budget, 8u, second));
        assert(first.kind == C::Kind::TransferCopy && first.first == 7u && first.last == 7u
            && !first.matches(second));
        C::TextureUse source{{1u},
            7u,
            {1u, 1u, 2u, 1u, rhi::TextureAspect::Color},
            7u,
            first,
            C::Contribution::Readback,
            C::TextureUsage::CopySrc,
            {rhi::ResourceState::CopySrc,
                rhi::PipelineStage::Transfer,
                rhi::ResourceAccess::TransferRead}};
        auto destination = use(7u, first, C::BufferUsage::CopyDst, 0u, C::Contribution::Readback);
        destination.access = {rhi::ResourceState::CopyDst,
            rhi::PipelineStage::Transfer,
            rhi::ResourceAccess::TransferWrite};
        assert(!C::validateTextureContribution(mixed, budget, source));
        assert(!C::validateBufferContribution(mixed, budget, destination));
        auto repeated = source;
        repeated.commandIndex = 8u;
        repeated.scope = second;
        assert(!C::validateTexturePair(source, repeated));
        auto badSource = source;
        badSource.access.stages = rhi::PipelineStage::FragmentShader;
        is(C::validateTextureContribution(mixed, budget, badSource), "RHI-ACCESS-STAGE-MISMATCH");
        badSource = source;
        badSource.access.access = rhi::ResourceAccess::TransferWrite;
        assert(C::validateTextureContribution(mixed, budget, badSource));
        badSource = source;
        badSource.contribution = C::Contribution::ShaderConsumption;
        is(C::validateTextureContribution(mixed, budget, badSource),
            "RHI-TYPED-USAGE-SCOPE-TEXTURE-READBACK-ACCESS");
        badSource = source;
        badSource.scope.last = 8u;
        is(C::validateTextureContribution(mixed, budget, badSource),
            "RHI-TYPED-USAGE-SCOPE-BINDING");
        auto badDestination = destination;
        badDestination.access.access = rhi::ResourceAccess::TransferRead;
        is(C::validateBufferContribution(mixed, budget, badDestination),
            "RHI-TYPED-USAGE-SCOPE-BUFFER-READBACK-ACCESS");
        badDestination = destination;
        badDestination.sizeBytes = UINT64_MAX;
        is(C::validateBufferContribution(mixed, budget, badDestination),
            "RHI-TYPED-USAGE-SCOPE-BUFFER-RANGE");
        for (const auto version : {1u, 2u}) {
            auto old = budget;
            old.contractVersion = version;
            is(C::validateOperations(mixed, old), "RHI-TYPED-USAGE-SCOPE-COMMAND-PROFILE");
        }
        auto upload = mixed;
        upload[7].kind = K::TextureUpload;
        is(C::validateOperations(upload, budget), "RHI-TYPED-USAGE-SCOPE-COMMAND-PROFILE");
        auto malformed = mixed;
        malformed[6].kind = K::BeginComputePass;
        assert(C::validateOperations(malformed, budget));
        std::puts(
            "BINDLESS_RHI_TYPED_USAGE_SCOPE3_READBACK_OK render-transfer exact-single-copy-scopes source-read-destination-write repeated-copies legacy-upload-malformed-refused no-sync-execution-authority");
    }
};
} // namespace arp::test
