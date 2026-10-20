#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_descriptor_consumption_fixture.h"
#include "original_command_descriptor_state_fixture.h"
#include "original_descriptor_native_identity_fixture.h"
#include "original_typed_cold_admission_fixture.h"
#include "original_typed_native_payload_fixture.h"
#include "original_whole_descriptor_write_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h"
#include "rhi/frontend/command_geometry_fetch.h"
#include "rhi/frontend/command_shader_usage_validation.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/command_usage_projection.h"
#include "rhi/semantics/command_lowering.h"
#include "webgpu_async_wait_fixture.h"

namespace arp::test {
class OriginalCommandPipelineBindingFixture final {
    using Inputs = rhi::OriginalCommandInputs;
    using Contract = rhi::OriginalCommandPipelineBindingContract;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "pipeline binding expected %s, got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static constexpr std::uint32_t indexedDescriptorCount = 2u;
    static constexpr std::uint32_t indexedSourceCount = 3u + 2u + indexedDescriptorCount;
    // Independent cold inventory, declared before Sources publication: State,
    // Stream and Transaction; Closure, loss subscription and retained Stream;
    // initialized actor owners excluding its already owned original lease;
    // bounded native entry/quota slots; one frozen decision and one immutable
    // Payload4 owner. These demands are declared before Sources publication.
    static constexpr std::uint32_t indexedGeometryColdOwnerDemand =
        rhi_webgpu::WebGpuOriginalTypedColdAdmission::additionalOwners;
    static constexpr std::uint32_t indexedGeometryCompleteCommandCount = 20u;
    static constexpr std::uint32_t indexedGeometryCompleteNativeCalls = 19u;
    static constexpr std::uint32_t indexedGeometryFreezeOwnerDemand = 3u + 3u
        + rhi::OriginalCommandInitializedBufferStagingContract::retainedOwnerBound - 1u
        + indexedSourceCount * (1u + rhi::OriginalWholeDescriptorWriteContract::quotaCount) + 1u
        + rhi::OriginalTypedNativePayloadContract::additionalOwners;

    static rhi::OriginalCommandAdmissionBudget indexedSourceBudget(bool readable)
    {
        rhi::OriginalCommandAdmissionBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        // Existing pipeline/common union16 + attachment5 + buffer2 + Scalars2,
        // Inputs1 and pass1: exact authored Graph owner ceiling27.
        // The same retained Sources also admits the three explicitly authored
        // indexed draws in the fetch projection fixture (11+3 commands).
        // Two distinct pipeline owners require two independently committed
        // Source2 descriptors, one shared registered arena and whole-write quotas.
        budget.maxCommands = 17u;
        budget.maxReferences = 11u;
        budget.maxNativePayloadObjects = indexedSourceCount;
        budget.maxGraphOwners = 27u
            + indexedDescriptorCount
                * (rhi::OriginalCommandDescriptorSetSource::canonicalExclusiveRetainedOwnerCount
                    + rhi::OriginalWholeDescriptorWriteContract::quotaCount)
            + rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
        // The readable workload now requests Freeze4, so its complete native
        // custody inventory is admitted before the immutable Sources exists.
        // The binding/state-only CopyDst negative keeps its original budget.
        if (readable) {
            budget.maxGraphOwners += indexedGeometryFreezeOwnerDemand
                + indexedGeometryColdOwnerDemand
                + OriginalTypedColdAdmissionFixture::geometryExecutionOwnerDemand();
            budget.maxCommands = indexedGeometryCompleteCommandCount;
            // Seven native resources plus the independently declared encoder
            // and finished command; same eight original callback participants.
            budget.maxNativePayloadObjects = indexedSourceCount + 2u;
            budget.maxTypedNativeCommandCalls = indexedGeometryCompleteNativeCalls;
            const auto cold =
                rhi_webgpu::WebGpuOriginalTypedColdAdmission::lifecycleRequirements(4u);
            budget.maxPendingSubmissions = cold.capacity.pendingSubmissions;
            budget.maxCallbackTickets = cold.capacity.callbackTickets;
            budget.maxCallbackRegistrations = cold.capacity.callbackRegistrations;
        }
        budget.maxInputDescriptors = 11u;
        budget.maxStateObservations = 3u;
        budget.maxTextureRanges = 2u;
        budget.maxStateTransactions = 1u;
        budget.maxPassColorAttachments = 2u;
        budget.maxPassAttachmentReferences = 2u;
        budget.maxCpuBytes.fill(bytes);
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            2u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)] =
            1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Descriptor)] =
            indexedDescriptorCount;
        return budget;
    }

    template <bool CompleteRenderState> static auto geometryDependencyCommands() noexcept
    {
        using K = rhi::CommandUsageKind;
        if constexpr (CompleteRenderState) {
            return std::array<rhi::CommandUsage, 20u>{{{K::BufferMemoryBarrier, 0u, 1u},
                {K::BeginRenderPass, 1u, 1u},
                {K::BindGraphicsPipeline, 2u, 1u},
                {K::SetViewport, 3u, 0u},
                {K::SetScissor, 3u, 0u},
                {K::BindRenderDescriptor, 3u, 1u},
                {K::BindIndexBuffer, 4u, 1u},
                {K::DrawIndexed, 5u, 0u},
                {K::DrawIndexed, 5u, 0u},
                {K::EndRenderPass, 5u, 0u},
                {K::BufferMemoryBarrier, 5u, 1u},
                {K::BeginRenderPass, 6u, 1u},
                {K::BindGraphicsPipeline, 7u, 1u},
                {K::SetViewport, 8u, 0u},
                {K::SetScissor, 8u, 0u},
                {K::BindRenderDescriptor, 8u, 1u},
                {K::BindIndexBuffer, 9u, 1u},
                {K::DrawIndexed, 10u, 0u},
                {K::EndRenderPass, 10u, 0u},
                {K::Finish, 10u, 0u}}};
        } else {
            return std::array<rhi::CommandUsage, 16u>{{{K::BufferMemoryBarrier, 0u, 1u},
                {K::BeginRenderPass, 1u, 1u},
                {K::BindGraphicsPipeline, 2u, 1u},
                {K::BindRenderDescriptor, 3u, 1u},
                {K::BindIndexBuffer, 4u, 1u},
                {K::DrawIndexed, 5u, 0u},
                {K::DrawIndexed, 5u, 0u},
                {K::EndRenderPass, 5u, 0u},
                {K::BufferMemoryBarrier, 5u, 1u},
                {K::BeginRenderPass, 6u, 1u},
                {K::BindGraphicsPipeline, 7u, 1u},
                {K::BindRenderDescriptor, 8u, 1u},
                {K::BindIndexBuffer, 9u, 1u},
                {K::DrawIndexed, 10u, 0u},
                {K::EndRenderPass, 10u, 0u},
                {K::Finish, 10u, 0u}}};
        }
    }

    static void verifyGeometryAuthoredProgram()
    {
        using D = rhi::CommandReferenceDomain;
        constexpr std::array domains{D::Buffer,
            D::TextureView,
            D::Pipeline,
            D::Descriptor,
            D::Buffer,
            D::Buffer,
            D::TextureView,
            D::Pipeline,
            D::Descriptor,
            D::Buffer};
        const auto validate = [&](const auto& commands) {
            return rhi::CommandUsageSequenceContract::validate(commands.data(),
                commands.size(),
                domains.size(),
                [&](std::size_t i) {
                    return domains[i];
                },
                {});
        };
        const auto legacy = geometryDependencyCommands<false>();
        auto complete = geometryDependencyCommands<true>();
        assert(!validate(legacy) && !validate(complete));
        assert(!rhi::semantics::CommandRecordingOrder::validate(legacy.data(), legacy.size(), 0u));
        assert(
            !rhi::semantics::CommandRecordingOrder::validate(complete.data(), complete.size(), 0u));
        // The former setters-before-pipeline sequence must fail in Host too.
        std::swap(complete[2u], complete[3u]);
        is(rhi::semantics::CommandRecordingOrder::validate(complete.data(), complete.size(), 0u),
            "RHI-COMMAND-ORDER-PIPELINE");
        auto source = indexedSourceBudget(true);
        auto required = rhi_webgpu::WebGpuOriginalTypedColdAdmission::lifecycleRequirements(4u);
        required.work.commands = indexedGeometryCompleteCommandCount;
        required.work.nativeCalls = indexedGeometryCompleteNativeCalls;
        assert(source.maxCommands == 20u && source.maxNativePayloadObjects == 9u
            && source.maxTypedNativeCommandCalls == 19u && source.maxCallbackTickets == 8u
            && source.maxCallbackRegistrations == 8u);
        using C = rhi::OriginalTypedNativeColdContract;
        assert(!C::validateSourceAuthority(required, indexedSourceCount, source));
        --source.maxNativePayloadObjects;
        is(C::validateSourceAuthority(required, indexedSourceCount, source),
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-CAPACITY");
        source = indexedSourceBudget(false);
        assert(source.maxCommands == 17u && source.maxNativePayloadObjects == 7u
            && !source.maxTypedNativeCommandCalls && !source.maxCallbackTickets);
        is(C::validateSourceAuthority(required, indexedSourceCount, source),
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-CAPACITY");
        std::puts(
            "BINDLESS_RHI_GEOMETRY_AUTHORED_PROGRAM_OK shared-sequence20 pipeline-before-setters reference10 source9 calls19 callbacks8 legacy7-native-authority-refused");
    }

    static void verifyContract()
    {
        verifyGeometryAuthoredProgram();
        using Pending = rhi::OriginalInitializedBufferPendingContract;
        using PendingUse = rhi::OriginalInitializedBufferPendingUse;
        using State = rhi::ResourceState;
        assert(!Pending::validateUse(1u, PendingUse::CopySource1));
        assert(!Pending::validateUse(1u, PendingUse::ImmutableGeometryRead1));
        assert(Pending::validateUse(2u, PendingUse::ImmutableGeometryRead1));
        assert(!Pending::validateUse(2u, PendingUse::ImmutableUniformRead2));
        assert(Pending::validateUse(1u, PendingUse::ImmutableUniformRead2));
        assert(Pending::validateUse(2u, PendingUse::CopySource1));
        assert(Pending::validateUse(3u, PendingUse::ImmutableUniformRead2));
        assert(Pending::validateStates(PendingUse::ImmutableUniformRead2,
            State::ShaderRead,
            State::ShaderRead,
            State::ShaderRead,
            State::ShaderRead));
        assert(!Pending::validateStates(PendingUse::ImmutableUniformRead2,
            State::ShaderRead,
            State::ShaderRead,
            State::ShaderRead,
            State::ShaderRead,
            2u));
        for (const auto wrong :
            {State::Undefined, State::CopySrc, State::CopyDst, State::ShaderWrite}) {
            assert(Pending::validateStates(PendingUse::ImmutableUniformRead2,
                wrong,
                State::ShaderRead,
                State::ShaderRead,
                State::ShaderRead,
                2u));
            assert(Pending::validateStates(PendingUse::ImmutableUniformRead2,
                State::ShaderRead,
                wrong,
                State::ShaderRead,
                State::ShaderRead,
                2u));
            assert(Pending::validateStates(PendingUse::ImmutableUniformRead2,
                State::ShaderRead,
                State::ShaderRead,
                wrong,
                State::ShaderRead,
                2u));
            assert(Pending::validateStates(PendingUse::ImmutableUniformRead2,
                State::ShaderRead,
                State::ShaderRead,
                State::ShaderRead,
                wrong,
                2u));
        }
        assert(Pending::validateUse(1u, static_cast<PendingUse>(255u)));
        for (const auto use : {PendingUse::CopySource1, PendingUse::ImmutableGeometryRead1}) {
            const auto expected = Pending::state(use);
            assert(!Pending::validateStates(use, expected, expected, expected, expected));
            for (const auto wrong : {State::Undefined,
                     State::CopyDst,
                     State::ShaderWrite,
                     use == PendingUse::CopySource1 ? State::ShaderRead : State::CopySrc}) {
                assert(Pending::validateStates(use, wrong, expected, expected, expected));
                assert(Pending::validateStates(use, expected, wrong, expected, expected));
                assert(Pending::validateStates(use, expected, expected, wrong, expected));
                assert(Pending::validateStates(use, expected, expected, expected, wrong));
            }
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                rhi::semantics::OriginalCommandNativeClosureLowering::geometryPendingReadLowering(
                    backend);
            assert(decision.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.recordingIssuerAvailable && !decision.executionAvailable
                && decision.diagnostic);
            assert(
                !rhi::semantics::OriginalCommandNativeClosureLowering::geometryPendingReadLowering(
                    backend, 2u)
                    .resourceClosureAvailable);
        }
        rhi::OriginalGeometryUsageBudget drawAccess{1u, true, 17u, 3u, 15u};
        assert(!drawAccess.diagnostic() && !drawAccess.empty());
        const auto bindingOnly = drawAccess;
        drawAccess.drawConsumptionContractVersion = 1u;
        drawAccess.drawConsumptionOptIn = true;
        drawAccess.maxDraws = 3u;
        drawAccess.maxDrawAccesses = 3u;
        drawAccess.maxBindingScans = 14u;
        assert(!drawAccess.diagnostic() && !drawAccess.matches(bindingOnly));
        auto invalidDraw = drawAccess;
        invalidDraw.drawConsumptionOptIn = false;
        is(invalidDraw.diagnostic(), "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-OPT-IN");
        invalidDraw = drawAccess;
        invalidDraw.drawConsumptionContractVersion = 2u;
        is(invalidDraw.diagnostic(), "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-VERSION");
        invalidDraw = drawAccess;
        invalidDraw.maxDrawAccesses = 0u;
        is(invalidDraw.diagnostic(), "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-BUDGET");
        invalidDraw = {};
        invalidDraw.maxBindingScans = 1u;
        assert(!invalidDraw.empty());
        using Fetch = rhi::OriginalIndexedDrawFetchContract;
        using ClosureBudget = rhi::OriginalCommandNativeClosureBudget;
        ClosureBudget geometryClosure{6u, true, 3u, 7u, 64u, 64u, bytes, 2u, 2u};
        geometryClosure.geometryContent = {
            1u, true, 1u, 32u, 16u, drawAccess, {1u, true, 17u, 3u, 0u, 14u, 6u}};
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(geometryClosure));
        auto badClosure = geometryClosure;
        badClosure.contractVersion = 5u;
        badClosure.indexContent = {1u, true, 1u, 32u, 16u};
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badClosure),
            "RHI-ORIGINAL-NATIVE-CLOSURE-GEOMETRY-SCHEMA");
        badClosure = geometryClosure;
        badClosure.geometryContent.prototypeOptIn = false;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badClosure),
            "RHI-ORIGINAL-GEOMETRY-CUSTODY-OPT-IN");
        badClosure = geometryClosure;
        badClosure.geometryContent.usage.drawConsumptionOptIn = false;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badClosure),
            "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-OPT-IN");
        badClosure = geometryClosure;
        badClosure.geometryContent.maxSources = 0u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badClosure),
            "RHI-ORIGINAL-GEOMETRY-CUSTODY-BUDGET");
        badClosure = geometryClosure;
        badClosure.geometryContent.fetch.maxAnalyzedIndices = 0u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badClosure),
            "RHI-ORIGINAL-INDEXED-FETCH-BUDGET");
        badClosure = geometryClosure;
        badClosure.geometryContent.fetch.contractVersion = 2u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badClosure),
            "RHI-ORIGINAL-GEOMETRY-CUSTODY-FETCH-PROFILE");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = rhi::semantics::OriginalCommandNativeClosureLowering::
                geometryContentLossBoundLowering(backend);
            assert(d.diagnostic && !d.executionAvailable && !d.recordingIssuerAvailable
                && d.resourceClosureAvailable == (backend == rhi::Backend::WebGPU));
        }
        Fetch::Budget fetch{1u, true, 14u, 3u, 0u, 24u, 6u};
        assert(!fetch.diagnostic());
        std::size_t fetchOutput = 0u;
        const auto absent = [](const auto&, const auto&, auto&) noexcept -> const char* {
            return "unreachable";
        };
        is(Fetch::visit({},
               fetch,
               absent,
               [&](const auto&) noexcept -> const char* {
                   ++fetchOutput;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-INPUTS-OWNER");
        assert(!fetchOutput);
        fetch.contractVersion = 0u;
        is(fetch.diagnostic(), "RHI-ORIGINAL-INDEXED-FETCH-VERSION");
        fetch.contractVersion = 1u;
        fetch.prototypeOptIn = false;
        is(fetch.diagnostic(), "RHI-ORIGINAL-INDEXED-FETCH-OPT-IN");
        fetch.prototypeOptIn = true;
        fetch.maxBindingScans = 0u;
        is(fetch.diagnostic(), "RHI-ORIGINAL-INDEXED-FETCH-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Fetch::lowering(backend);
            assert(!decision.executionAvailable && decision.diagnostic);
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            assert(decision.projectionAvailable
                == (backend == rhi::Backend::Null || backend == rhi::Backend::WebGPU));
        }
        OriginalCommandDescriptorConsumptionFixture::verifyContract();
        OriginalCommandDescriptorStateFixture::verifyContract();
        OriginalDescriptorNativeIdentityFixture::verifyContract();
        assert(!Contract::validateBudget({}));
        assert(!Contract::validateBudget({2u, true, 1u}));
        is(Contract::validateBudget({0u, true, 1u}), "RHI-ORIGINAL-INPUTS-VERSION");
        is(Contract::validateBudget({8u, true, 1u}), "RHI-ORIGINAL-INPUTS-VERSION");
        is(Contract::validateBudget({7u, true, 1u}), "RHI-ORIGINAL-GEOMETRY-INPUT-BUDGET");
        is(Contract::validateBudget({6u, true, 1u}), "RHI-ORIGINAL-INPUTS-GENERAL-FINITE-SCHEMA");
        is(Contract::validateBudget({5u, true, 1u}), "RHI-ORIGINAL-GEOMETRY-INPUT-BUDGET");
        rhi::OriginalCommandInputsBudget geometry{5u, true, 1u};
        geometry.maxIndexBindings = 1u;
        assert(!Contract::validateBudget(geometry));
        geometry.contractVersion = 4u;
        is(Contract::validateBudget(geometry), "RHI-ORIGINAL-INPUTS-VERSION");
        is(Contract::validateBudget({4u, true, 1u}), "RHI-ORIGINAL-TYPED-INPUT-BUDGET");
        is(Contract::validateBudget({1u, true, 1u}), "RHI-ORIGINAL-INPUTS-VERSION");
        is(Contract::validateBudget({2u, false, 1u}), "RHI-ORIGINAL-TYPED-INPUT-OPT-IN");
        is(Contract::validateBudget({2u, true, 0u}), "RHI-ORIGINAL-TYPED-INPUT-BUDGET");
        assert(!Contract::validateBudget({3u, true, 1u, 1u, 0u, 1u}));
        for (const auto version : {1u, 2u}) {
            is(Contract::validateBudget({version, true, 1u, 1u, 0u, 1u}),
                "RHI-ORIGINAL-INPUTS-VERSION");
            is(Contract::validateBudget({version, false, 0u, 0u, 1u, 0u}),
                "RHI-ORIGINAL-INPUTS-VERSION");
        }
        is(Contract::validateBudget({3u, false, 1u, 1u, 0u, 1u}),
            "RHI-ORIGINAL-TYPED-INPUT-OPT-IN");
        is(Contract::validateBudget({3u, true, 1u}), "RHI-ORIGINAL-TYPED-INPUT-BUDGET");
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandPipelineBindingLowering::lowering(backend);
            assert(!d.executionAvailable);
            assert(d.positionalAvailable == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
            const auto geometryDecision =
                rhi::OriginalCommandGeometryBindingContract::lowering(backend);
            assert(!geometryDecision.executionAvailable && !geometryDecision.sourceAdapterAvailable
                && geometryDecision.validatorOnly == (backend == rhi::Backend::Null));
            assert((d.diagnostic == nullptr) == d.positionalAvailable);
            const auto table =
                arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::tableLowering(
                    backend);
            assert(table.mode == rhi::CapabilityMode::DiagnosticFailure
                && !table.positionalAvailable && !table.executionAvailable);
            assert(table.validatorOnly == (backend == rhi::Backend::Null));
            is(table.diagnostic, "RHI-ORIGINAL-TYPED-INPUT-TABLE-PROFILE-UNAVAILABLE");
        }
        assert(!arp::rhi::semantics::OriginalCommandPipelineBindingLowering::lowering(
            static_cast<rhi::Backend>(255u))
                .positionalAvailable);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_RHI_PIPELINE_POSITIONAL_CONTRACT_OK inputs2 opt-in bounded-records backends4 descriptors-state-native-closed");
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_POSITIONAL_CONTRACT_OK inputs3 bounded-original-records old-schema-reject table-profile-closed backends4 state-native-closed");
    }

    static auto attachmentSources(rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& source,
        const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        const char* diagnostic = nullptr;
        rhi::TextureDesc desc;
        desc.extent = {2u, 2u, 1u};
        desc.format = rhi::TextureFormat::Rgba8Unorm;
        desc.usage = rhi::TextureUsage::RenderAttachment;
        desc.initialState = rhi::ResourceState::ColorAttachment;
        core::UniquePtr<rhi::GpuTexture> texture;
        core::UniquePtr<rhi::GpuTextureView> view;
        {
            core::CpuMemoryQuotaScope scope(resources);
            texture = device.createTexture(desc);
            assert(texture);
            rhi::TextureViewDesc fields;
            fields.format = desc.format;
            fields.usage = desc.usage;
            fields.aspect = rhi::TextureAspect::Color;
            view = device.createTextureView(*texture, fields);
            assert(view);
        }
        auto parent = rhi::OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, bytes}, source, diagnostic);
        assert(parent);
        auto lease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            view, {1u, true, 2u, bytes}, source, diagnostic);
        assert(lease);
        auto original = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            lease, parent, {1u, true, 5u, bytes}, source, diagnostic);
        assert(original);
        return std::array<rhi::OriginalCommandResourceSources::Source, 2u>{{parent, original}};
    }

    static auto passArguments(const rhi::OriginalCommandResourceSources& root,
        const std::shared_ptr<core::CpuMemoryQuota>& graph)
    {
        const auto& view = std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
            root.sources()[1u]);
        rhi::RenderPassDesc desc;
        rhi::RenderPassColorAttachmentDesc attachment;
        attachment.target = view->resource();
        attachment.format = view->originalViewLease()->layout().format;
        attachment.loadOp = rhi::LoadOp::Clear;
        attachment.storeOp = rhi::StoreOp::Store;
        desc.colorAttachments.push_back(attachment);
        const char* diagnostic = nullptr;
        auto result = rhi::RenderPassArguments::prepareOriginal(
            desc, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, graph, diagnostic);
        assert(result);
        return result;
    }

    template <class Publish>
    static void verifyGraphics(rhi::GpuDevice& device,
        Publish&& publish,
        const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using A = rhi::OriginalCommandGraphicsPipelineSource;
        auto source = core::makeShared<core::CpuMemoryQuota>(bytes),
             graph = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* diagnostic = nullptr;
        auto prefix = attachmentSources(device, source, resources);
        auto first = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(publish(), diagnostic),
             second = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(publish(), diagnostic);
        assert(first && second);
        const auto adapter = [&](const auto& pipeline) {
            return A::prepareOriginal(pipeline,
                {1u,
                    true,
                    A::kind,
                    pipeline->originalResourceRegistration().generation(),
                    bytes,
                    A::exclusiveOwnerCount},
                source,
                device,
                diagnostic);
        };
        auto a = adapter(first), a2 = adapter(second);
        assert(a && a2);
        rhi::OriginalCommandAdmissionBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        // Two graphics wrappers/common receipt-layout union16 plus view-parent5,
        // Scalars2, Inputs1 and deduplicated pass1: exact Graph owner ceiling25.
        budget.maxCommands = 8u;
        budget.maxReferences = 5u;
        budget.maxGraphOwners = 25u;
        budget.maxInputDescriptors = 5u;
        budget.maxPassColorAttachments = 2u;
        budget.maxPassAttachmentReferences = 2u;
        budget.maxCpuBytes.fill(bytes);
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            2u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)] =
            1u;
        auto root = Sources::prepareColdOriginalTyped(
            prefix.data(), prefix.size(), budget, {2u, true, 2u}, source, diagnostic);
        assert(root);
        assert(!Sources::appendOriginalTyped(root, {a}, device)
            && !Sources::appendOriginalTyped(root, {a2}, device));
        auto inputs = verifyActual<rhi::RenderPipeline>(root, graph, device);
        std::weak_ptr<const Sources> observer = root;
        root.reset();
        prefix = {};
        first.reset();
        second.reset();
        a.reset();
        a2.reset();
        assert(!observer.expired() && !inputs->validate());
        inputs.reset();
        assert(observer.expired());
        observer.reset();
        assert(!source->liveBytes() && !graph->liveBytes());
    }

    static void verifyGeometryIndexCustody(
        const std::shared_ptr<const rhi::OriginalCommandResourceStates>& graph,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program,
        const rhi::GpuDevice& device,
        rhi::OriginalCommandStateAdmissionBudget state,
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized,
        bool expectUnboundDescriptor = false,
        bool expectPipelineMismatch = false,
        bool expectGeometryConsumption = false)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Root = rhi::OriginalCommandStateTransaction;
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        using Source = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
        auto recording = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* diagnostic = nullptr;
        state.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        auto invalid = state;
        --invalid.geometryUsage.maxBindings;
        assert(!Root::prepareOriginalOwnership(graph, device, invalid, recording, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-TRANSACTION-GEOMETRY-FOOTPRINT-MISMATCH");
        invalid = state;
        ++invalid.renderDescriptorUsage.maxCommands;
        assert(!Root::prepareOriginalOwnership(graph, device, invalid, recording, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-TRANSACTION-RENDER-FOOTPRINT-MISMATCH");
        auto root = Root::prepareOriginalOwnership(graph, device, state, recording, diagnostic);
        if (!root)
            std::fprintf(stderr, "geometry root: %s\n", diagnostic);
        assert(root);
        auto shortPlan = program->originalBudget().stateSequence;
        --shortPlan.maxSteps;
        const auto rootBytes = recording->liveBytes();
        const auto planAllocations = core::defaultCpuAllocator().stats().allocationCount;
        is(root->materializeOriginalPrototypePlan(shortPlan),
            "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY");
        assert(!root->hasOriginalPrototypePlan() && !root->plannedAccessStepCount()
            && recording->liveBytes() == rootBytes
            && core::defaultCpuAllocator().stats().allocationCount == planAllocations);
        const auto* planDiagnostic =
            root->materializeOriginalPrototypePlan(program->originalBudget().stateSequence);
        if (planDiagnostic)
            std::fprintf(stderr, "geometry State4 plan: %s\n", planDiagnostic);
        assert(!planDiagnostic);
        // Two attachment steps and three explicit index bindings; the joined
        // sequence additionally consumes three render-binding and three shader
        // footprints. The independently counted Sequence4 plan must agree.
        assert(root->plannedAccessStepCount()
            == (expectGeometryConsumption     ? 14u
                    : expectUnboundDescriptor ? 5u
                                              : 11u));
        auto loss = device.observeOriginalDeviceNativeLoss(diagnostic);
        assert(loss && !diagnostic);
        const auto& sources = program->originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources()
                                  ->sources();
        rhi::OriginalCommandNativeClosureBudget b{5u,
            true,
            3u,
            static_cast<std::uint32_t>(sources.size()),
            root->retainedOwnerCount() + 3u + Source::initializedIndexOwnerBound
                + static_cast<std::uint32_t>(sources.size())
                    * (1u + rhi::OriginalWholeDescriptorWriteContract::quotaCount),
            0u,
            bytes,
            2u,
            2u};
        b.indexContent = {1u, true, 1u, Source::initializedIndexOwnerBound, 16u};
        for (const auto& source : sources)
            std::visit(
                [&](const auto& lease) {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(lease)>::element_type>;
                    if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>
                        || std::is_same_v<T, rhi::OriginalCommandTextureLease>)
                        b.maxRetainedResourceBytes +=
                            lease->originalObject()->allocation().estimatedSizeBytes;
                },
                source);
        std::array<Closure::IndexContentInput, 2> indices{{{2u, initialized}, {2u, initialized}}};
        std::size_t count = 1u;
        const auto make = [&] {
            return b.contractVersion == 6u
                ? Closure::prepareOriginalGeometryContentLossBound(
                      root, program, indices.data(), count, device, b, *loss, diagnostic)
                : Closure::prepareOriginalIndexContentLossBound(
                      root, program, indices.data(), count, device, b, *loss, diagnostic);
        };
        auto original = b;
        const auto refuse = [&](const char* id) {
            const auto before = recording->liveBytes();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!make());
            is(diagnostic, id);
            assert(recording->liveBytes() == before
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
            b = original;
        };
        if (expectGeometryConsumption) {
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-CONSUMPTION-JOIN-MISSING");
            b.contractVersion = 6u;
            b.indexContent = {};
            b.geometryContent = {1u,
                true,
                1u,
                Source::initializedIndexOwnerBound,
                16u,
                state.geometryUsage,
                {1u, true, 17u, 3u, 0u, 14u, 6u}};
            original = b;
            b.geometryContent.prototypeOptIn = false;
            refuse("RHI-ORIGINAL-GEOMETRY-CUSTODY-OPT-IN");
            --b.geometryContent.usage.maxBindingScans;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-FOOTPRINT-MISMATCH");
            --b.geometryContent.maxInitializedBytes;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-BYTE-CAPACITY");
            --b.geometryContent.maxRetainedOwners;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-OWNER-CAPACITY");
            count = 0u;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-SOURCE-CAPACITY");
            count = 1u;
            indices[0].sourceIndex = 0u;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-SOURCE");
            indices[0].sourceIndex = 2u;
            indices[0].source = std::shared_ptr<const Source>(recording, initialized.get());
            refuse("RHI-CANONICAL-UPLOAD-SOURCE-OWNER");
            indices[0].source = initialized;
            count = 2u;
            b.geometryContent.maxSources = 2u;
            b.geometryContent.maxRetainedOwners *= 2u;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-DUPLICATE");
            count = 1u;
            --b.geometryContent.fetch.maxDraws;
            refuse("RHI-ORIGINAL-INDEXED-FETCH-DRAW-CAPACITY");
            --b.geometryContent.fetch.maxBindingScans;
            refuse("RHI-ORIGINAL-INDEXED-FETCH-SCAN-CAPACITY");
            --b.geometryContent.fetch.maxAnalyzedIndices;
            refuse("RHI-ORIGINAL-INDEXED-FETCH-ANALYSIS-CAPACITY");
        } else {
            b.indexContent.prototypeOptIn = false;
            refuse("RHI-ORIGINAL-INDEX-CUSTODY-OPT-IN");
            --b.indexContent.maxInitializedBytes;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-BYTE-CAPACITY");
            --b.indexContent.maxRetainedOwners;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-OWNER-CAPACITY");
            count = 0u;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-SOURCE-CAPACITY");
            count = 1u;
            indices[0].sourceIndex = 0u;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-SOURCE");
            indices[0].sourceIndex = 2u;
            indices[0].source = std::shared_ptr<const Source>(recording, initialized.get());
            refuse("RHI-CANONICAL-UPLOAD-SOURCE-OWNER");
            indices[0].source = initialized;
            count = 2u;
            b.indexContent.maxSources = 2u;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-OWNER-CAPACITY");
            b.indexContent.maxSources = 2u;
            b.indexContent.maxRetainedOwners *= 2u;
            refuse("RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-DUPLICATE");
            count = 1u;
        }
        if (expectUnboundDescriptor) {
            const auto before = recording->liveBytes();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!make());
            is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-UNBOUND");
            assert(recording->liveBytes() == before
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
            root.reset();
            assert(!recording->liveBytes());
            std::puts(
                "BINDLESS_RHI_GEOMETRY_UNBOUND_CUSTODY_REFUSED same-sources unused-descriptor original-binding-only-stream unchanged-native-validator no-allocation");
            return;
        }
        if (expectPipelineMismatch) {
            const auto before = recording->liveBytes();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!make());
            is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-PIPELINE");
            assert(recording->liveBytes() == before
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
            root.reset();
            assert(!recording->liveBytes());
            std::puts(
                "BINDLESS_RHI_GEOMETRY_PIPELINE_CUSTODY_REFUSED same-layout distinct-original-pipelines wrong-whole-write-owner no-allocation");
            return;
        }
        auto closure = make();
        if (!closure)
            std::fprintf(stderr, "geometry closure: %s\n", diagnostic);
        assert(closure);
        b.maxRetainedOwners = closure->retainedOwnerCount();
        b.maxRetainedResourceBytes = closure->retainedResourceBytes();
        closure.reset();
        const auto exact = b;
        --b.maxRetainedOwners;
        assert(!make());
        is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-OWNER-CAPACITY");
        b = exact;
        --b.maxRetainedResourceBytes;
        assert(!make());
        is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-ALLOCATION-CAPACITY");
        b = exact;
        CpuQuotaFactoryFixture pressure(recording);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        closure = make();
        assert(closure && !closure->validate() && !closure->nativeExecutionProven());
        const auto hot = [&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording}, [&]() noexcept -> const char* {
                    assert(closure->executionDiagnostic());
                    return closure->validate();
                }));
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(hot);
        std::weak_ptr<const Source> retained = initialized;
        indices = {};
        initialized.reset();
        assert(!retained.expired() && !closure->validate());
        // Restore the same caller for the following old Closure5 regression;
        // its existing final-owner retirement check remains independent.
        if (expectGeometryConsumption)
            initialized = retained.lock();
        closure.reset();
        assert(expectGeometryConsumption ? !retained.expired() : retained.expired());
        retained.reset();
        root.reset();
        assert(!recording->liveBytes());
        if (expectGeometryConsumption) {
            std::printf(
                "BINDLESS_RHI_GEOMETRY_CONTENT_CUSTODY_OK closure6 same-inputs5-state4-stream4 original-plan14 draw3-index-analysis6 binding-scans14 exact-less-forged-duplicate-refused caller-drop-restored old5-still-closed hot0-query0 no-native-recording peak%llu\n",
                static_cast<unsigned long long>(peak));
            return;
        }
        std::printf(
            "BINDLESS_RHI_GEOMETRY_INDEX_CUSTODY_OK closure5 exact-state4-stream4 same-original-source-lease three-binding-positions cold-exact-one-less duplicate-forged-budget-refusal original-root-journal-empty caller-drop source-retained-and-released hot0-query0 native-draw-closed peak%llu\n",
            static_cast<unsigned long long>(peak));
#else
        (void)graph;
        (void)program;
        (void)device;
        (void)state;
        (void)initialized;
        (void)expectUnboundDescriptor;
        (void)expectPipelineMismatch;
#endif
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyGeometryDrawConsumption(const std::shared_ptr<const Inputs>& inputs,
        const rhi::GpuDevice& device,
        rhi::OriginalCommandStateAdmissionBudget state,
        rhi::OriginalCommandStreamBudget stream,
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized)
    {
        using Graph = rhi::OriginalCommandResourceStates;
        using Program = rhi::OriginalCommandStreamProgram;
        using G = rhi::OriginalCommandGeometryBindingContract;
        using P = rhi::common::OriginalTypedUsageProjection;
        using D = rhi::common::OriginalTypedDependencyProjection;
        using S = rhi::semantics::TypedShaderUsageScopeContract;
        auto& footprint = state.geometryUsage;
        footprint.drawConsumptionContractVersion = 1u;
        footprint.drawConsumptionOptIn = true;
        footprint.maxDraws = 3u;
        footprint.maxDrawAccesses = 3u;
        footprint.maxBindingScans = 14u;
        footprint.maxPairChecks = 15u;
        std::size_t consumed = 0u;
        const auto accesses = inputs->visitGeometryAccesses(
            footprint, [&](const G::Access& access) noexcept -> const char* {
                if (access.drawConsumption()) {
                    const std::array<std::size_t, 3u> draws{4u, 8u, 14u}, binds{3u, 7u, 13u};
                    assert(consumed < 3u && access.commandIndex == draws[consumed]
                        && access.bindingCommandIndex == binds[consumed]);
                    assert(access.binding.sourceIndex == 2u
                        && access.scope.stages == rhi::PipelineStage::VertexInput
                        && access.scope.access == rhi::ResourceAccess::IndexRead
                        && !access.nativeExecutionProven());
                    ++consumed;
                } else
                    assert(access.commandIndex == access.bindingCommandIndex);
                return nullptr;
            });
        if (accesses.diagnostic)
            std::fprintf(stderr, "geometry draw consumption: %s\n", accesses.diagnostic);
        assert(!accesses.diagnostic && consumed == 3u && accesses.bindings == 3u
            && accesses.draws == 3u && accesses.drawAccesses == 3u && accesses.bindingScans == 14u
            && accesses.pairChecks == 15u);
        const auto refuse = [&](const G::AccessBudget& budget, const char* id) {
            std::size_t visits = 0u;
            is(inputs
                    ->visitGeometryAccesses(budget,
                        [&](const auto&) noexcept -> const char* {
                            ++visits;
                            return nullptr;
                        })
                    .diagnostic,
                id);
            assert(!visits);
        };
        auto less = footprint;
        --less.maxDraws;
        refuse(less, "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-DRAW-CAPACITY");
        less = footprint;
        --less.maxDrawAccesses;
        refuse(less, "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-ACCESS-CAPACITY");
        less = footprint;
        --less.maxBindingScans;
        refuse(less, "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-SCAN-CAPACITY");
        less = footprint;
        --less.maxPairChecks;
        refuse(less, "RHI-ORIGINAL-GEOMETRY-ACCESS-PAIR-CAPACITY");
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes),
             streamQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* diagnostic = nullptr;
        auto graph = Graph::prepareOriginal(inputs, device, state, stateQuota, diagnostic);
        if (!graph)
            std::fprintf(stderr, "geometry consumption State4: %s\n", diagnostic);
        assert(graph);
        stream.geometryUsage = footprint;
        stream.maxImplicitAccesses = 12u;
        stream.stateSequence.maxImplicitAccesses = 12u;
        stream.stateSequence.maxSteps = 14u;
        stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        const auto timeline =
            rhi::common::OriginalCommandAccessValidation::inspect(graph, stream.stateSequence);
        assert(!timeline.diagnostic && timeline.steps == 14u);
        auto shortSequence = stream.stateSequence;
        --shortSequence.maxSteps;
        is(rhi::common::OriginalCommandAccessValidation::inspect(graph, shortSequence).diagnostic,
            "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY");
        auto program = Program::prepareOriginalTyped(graph, stream, streamQuota, diagnostic);
        if (!program)
            std::fprintf(stderr, "geometry consumption Stream4: %s\n", diagnostic);
        assert(program);
        assert(program->counts().implicitAccesses == 12u && program->counts().geometryBinds == 3u
            && program->counts().geometryDraws == 3u
            && program->counts().geometryDrawAccesses == 3u);
        std::size_t ordinal = 0u;
        for (const auto command : {4u, 8u, 14u}) {
            assert(program->operations()[command].implicitAccessCount == 2u
                && program->operations()[command].firstImplicitAccess == 2u + 4u * ordinal);
            ++ordinal;
        }
        P::Budget usage{4u,
            true,
            state.descriptorConsumption,
            state.renderDescriptorUsage,
            17u,
            2u,
            14u,
            1u,
            2u};
        usage.geometryUsage = footprint;
        const P::StateBoundRequest bound{4u, true, usage};
        const auto hot = [&]() noexcept -> const char* {
            std::size_t points = 0u;
            const auto result = P::visitStateBound(
                program, graph, bound, [&](const P::Point& point) noexcept -> const char* {
                    if (const auto* geometry = std::get_if<P::GeometryPoint>(&point)) {
                        assert(geometry->use.contribution
                            == (geometry->original.drawConsumption()
                                    ? S::Contribution::GeometryConsumption
                                    : S::Contribution::GeometryBinding));
                        ++points;
                    }
                    return nullptr;
                });
            assert(!result.diagnostic && points == 6u && result.counts.geometryDrawAccesses == 3u
                && result.counts.geometryDraws == 3u && result.counts.contributions == 14u);
            return result.diagnostic;
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{stateQuota, streamQuota}, hot));
        });
        auto mismatch = usage;
        --mismatch.geometryUsage.maxBindingScans;
        is(P::inspectStateBound(program, graph, {4u, true, mismatch}).diagnostic,
            "RHI-ORIGINAL-USAGE-PROJECTION-GEOMETRY-FOOTPRINT-MISMATCH");
        const D::Request dependencies{4u, true, bound, 1u, 15u, 210u, 91u, 1u, 2u};
        // This original sequence has no authored dependency. Inspect all fourteen
        // geometry/shader/attachment contributions and their pairs, then refuse
        // EMPTY; no implicit barrier or alternate dependency graph is invented.
        is(D::inspect(program, graph, dependencies).diagnostic,
            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-EMPTY");
        verifyGeometryIndexCustody(graph, program, device, state, initialized, false, false, true);
        program.reset();
        graph.reset();
        assert(!stateQuota->liveBytes() && !streamQuota->liveBytes());
        std::puts(
            "BINDLESS_RHI_GEOMETRY_DRAW_CONSUMPTION_OK independent1 same-inputs5 state4 sequence14 stream4 implicit12 usage14 geometry6 draw3 scans14 pair15 no-output-on-refusal hot0 query0 original-plan14 native-closure-refused");
    }

    // A separately authored program over the same Sources supplies real
    // initial/inter-pass clauses. The old barrier-free program stays negative.
    struct GeometryDependencyOwners {
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdAdmission> admission;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> quotas;
        std::weak_ptr<const rhi_webgpu::WebGpuCanonicalNativeClosure> retained;
    };

    // Return the same immutable admission before awaiting native callbacks.
    // Authored arrays and negative-probe temporaries must leave the continuation
    // stack; the existing admission still retains the original ownership chain.
    template <bool CompleteRenderState = false>
    static GeometryDependencyOwners prepareGeometryDependencyFreeze(
        const std::shared_ptr<const Inputs>& original,
        const rhi::GpuDevice& device,
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized)
    {
        using K = rhi::CommandUsageKind;
        using Usage = rhi::OriginalCommandUsage;
        using Scalar = rhi::CommandScalarArguments;
        using Binding = rhi::OriginalCommandGeometryBinding;
        using Graph = rhi::OriginalCommandResourceStates;
        using Program = rhi::OriginalCommandStreamProgram;
        using Root = rhi::OriginalCommandStateTransaction;
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        using Source = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
        using Projection = rhi::common::OriginalTypedUsageProjection;
        using Dependency = rhi::common::OriginalTypedDependencyProjection;
        using Freeze = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
        auto inputsQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto streamQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto recording = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto& sources = original->originalScalars()->originalUsage()->originalSources();
        const auto& buffer = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
            sources->sources()[2u]);
        const auto& first =
            std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                sources->sources()[3u]);
        const auto& second =
            std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                sources->sources()[4u]);
        const auto& descriptor =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                sources->sources()[5u]);
        const auto& descriptor2 =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                sources->sources()[6u]);
        const char* id = nullptr;
        constexpr std::size_t commandCount = CompleteRenderState ? 20u : 16u;
        constexpr std::array<std::size_t, 16u> completePositions{
            {0u, 1u, 2u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 15u, 16u, 17u, 18u, 19u}};
        const auto position = [&](std::size_t ordinal) noexcept {
            return CompleteRenderState ? completePositions[ordinal] : ordinal;
        };
        const auto commands = geometryDependencyCommands<CompleteRenderState>();
        const std::array<Usage::Reference, 10u> refs{
            {{2u}, {1u}, {3u}, {5u}, {2u}, {2u}, {1u}, {4u}, {6u}, {2u}}};
        auto usage = Usage::prepareOriginal(
            sources, commands.data(), commands.size(), refs.data(), refs.size(), inputsQuota, id);
        if (!usage)
            std::fprintf(stderr,
                "geometry freeze Usage complete=%u: %s\n",
                CompleteRenderState ? 1u : 0u,
                id);
        assert(usage);
        auto pass = passArguments(*sources, inputsQuota);
        rhi::ResourceMemoryBarrierDesc memory;
        memory.resource = buffer->resource();
        memory.sourceStages = memory.destinationStages =
            rhi::PipelineStage::FragmentShader | rhi::PipelineStage::VertexInput;
        memory.sourceAccess = memory.destinationAccess =
            rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::IndexRead;
        const auto previous = original->originalScalars()->originalArguments();
        const auto fieldsFor = [&](std::size_t i) {
            if (commands[i].kind == K::DrawIndexed)
                return rhi::CommandVariableArguments::Input{
                    previous->records()[i == position(5u) ? 4u
                                    : i == position(6u)   ? 8u
                                                          : 14u]
                        .scalar,
                    {}};
            if (commands[i].kind == K::BindIndexBuffer)
                return rhi::CommandVariableArguments::Input{
                    *Scalar::capture(K::BindIndexBuffer,
                        Scalar::IndexBinding{
                            rhi::IndexFormat::Uint16, i == position(12u) ? 14u : 0u}),
                    {}};
            if (commands[i].kind == K::BindRenderDescriptor)
                return rhi::CommandVariableArguments::Input{
                    *Scalar::capture(K::BindRenderDescriptor,
                        Scalar::DescriptorBinding{descriptor->frozenLogicalSet()}),
                    {}};
            const auto extent = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                sources->sources()[0u])
                                    ->originalObject()
                                    ->desc()
                                    .extent;
            if (commands[i].kind == K::SetViewport)
                return rhi::CommandVariableArguments::Input{
                    *Scalar::capture(K::SetViewport,
                        rhi::ViewportState{0.0f,
                            0.0f,
                            static_cast<float>(extent.width),
                            static_cast<float>(extent.height),
                            0.0f,
                            1.0f}),
                    {}};
            if (commands[i].kind == K::SetScissor)
                return rhi::CommandVariableArguments::Input{
                    *Scalar::capture(
                        K::SetScissor, rhi::ScissorState{0u, 0u, extent.width, extent.height}),
                    {}};
            return rhi::CommandVariableArguments::Input{
                *Scalar::capture(commands[i].kind, Scalar::None{}), {}};
        };
        const auto fields = [&] {
            if constexpr (CompleteRenderState) {
                return std::array<rhi::CommandVariableArguments::Input, 20u>{{fieldsFor(0u),
                    fieldsFor(1u),
                    fieldsFor(2u),
                    fieldsFor(3u),
                    fieldsFor(4u),
                    fieldsFor(5u),
                    fieldsFor(6u),
                    fieldsFor(7u),
                    fieldsFor(8u),
                    fieldsFor(9u),
                    fieldsFor(10u),
                    fieldsFor(11u),
                    fieldsFor(12u),
                    fieldsFor(13u),
                    fieldsFor(14u),
                    fieldsFor(15u),
                    fieldsFor(16u),
                    fieldsFor(17u),
                    fieldsFor(18u),
                    fieldsFor(19u)}};
            } else {
                return std::array<rhi::CommandVariableArguments::Input, 16u>{{fieldsFor(0u),
                    fieldsFor(1u),
                    fieldsFor(2u),
                    fieldsFor(3u),
                    fieldsFor(4u),
                    fieldsFor(5u),
                    fieldsFor(6u),
                    fieldsFor(7u),
                    fieldsFor(8u),
                    fieldsFor(9u),
                    fieldsFor(10u),
                    fieldsFor(11u),
                    fieldsFor(12u),
                    fieldsFor(13u),
                    fieldsFor(14u),
                    fieldsFor(15u)}};
            }
        }();
        auto arguments = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, commandCount, 0u, 0u, 0u, bytes},
            inputsQuota,
            id);
        assert(arguments);
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, inputsQuota, id);
        assert(scalars);
        const auto generation =
            buffer->originalObject()->originalResourceRegistration().generation();
        const auto gp = first->originalObject()->originalResourceRegistration().generation();
        const auto gp2 = second->originalObject()->originalResourceRegistration().generation();
        std::array<Inputs::OriginalDescriptor, 10u> records{{{position(0u),
                                                                 *rhi::ResourceDependencyArguments::
                                                                     capture(K::BufferMemoryBarrier,
                                                                         memory)},
            {position(1u), pass},
            {position(2u),
                rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline, 3u, position(1u), gp}},
            {position(3u),
                rhi::OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
                    5u,
                    3u,
                    position(1u),
                    descriptor->originalObject()->originalResourceRegistration().generation(),
                    descriptor->frozenContentRevision()}},
            {position(4u),
                Binding{K::BindIndexBuffer, 2u, 3u, position(2u), position(1u), generation, gp}},
            {position(8u),
                *rhi::ResourceDependencyArguments::capture(K::BufferMemoryBarrier, memory)},
            {position(9u), pass},
            {position(10u),
                rhi::OriginalCommandPipelineBinding{
                    K::BindGraphicsPipeline, 4u, position(9u), gp2}},
            {position(11u),
                rhi::OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
                    6u,
                    4u,
                    position(9u),
                    descriptor2->originalObject()->originalResourceRegistration().generation(),
                    descriptor2->frozenContentRevision()}},
            {position(12u),
                Binding{
                    K::BindIndexBuffer, 2u, 4u, position(10u), position(9u), generation, gp2}}}};
        rhi::OriginalCommandInputsBudget input{5u, true, 2u, 2u};
        input.maxIndexBindings = 2u;
        auto inputs = Inputs::prepareOriginal(
            scalars, records.data(), records.size(), input, inputsQuota, id);
        if (!inputs)
            std::fprintf(stderr, "geometry freeze Inputs: %s\n", id);
        assert(inputs);
        std::uint32_t owners = 0u, registrations = 0u;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(
            sources, inputs->retainedOwnerCount() + 1u, 64u, 64u, owners, registrations));
        rhi::OriginalCommandStateAdmissionBudget state{4u,
            true,
            1u,
            registrations,
            3u,
            2u,
            owners,
            bytes,
            {3u, true, 0u, 1u, false, true, 3u, 3u, 1u, true, 1u},
            {3u, true, 0u, 1u, false, true, commandCount, 2u, 2u, 1u, true, 1u}};
        state.descriptorConsumption.indexedDrawConsumptionVersion = 1u;
        state.descriptorConsumption.indexedDrawConsumptionOptIn = true;
        state.geometryUsage = {1u, true, commandCount, 2u, 10u};
        state.geometryUsage.drawConsumptionContractVersion = 1u;
        state.geometryUsage.drawConsumptionOptIn = true;
        state.geometryUsage.maxDraws = state.geometryUsage.maxDrawAccesses = 3u;
        state.geometryUsage.maxBindingScans = 13u;
        auto graph = Graph::prepareOriginal(inputs, device, state, stateQuota, id);
        if (!graph)
            std::fprintf(stderr, "geometry freeze State4: %s\n", id);
        assert(graph);
        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = 4u;
        stream.prototypeOptIn = true;
        stream.maxCommands = commandCount;
        stream.maxDescriptors = stream.maxReferences = 10u;
        stream.maxRenderPasses = stream.maxAttachmentReferences = 2u;
        stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = bytes;
        stream.maxPipelineBinds = stream.maxDescriptorBinds = 2u;
        stream.maxConsumptions = 3u;
        stream.maxImplicitAccesses = 10u;
        stream.stateSequence = {4u, true, commandCount, 2u, 3u, 14u, 10u};
        stream.descriptorConsumption = state.descriptorConsumption;
        stream.renderDescriptorUsage = state.renderDescriptorUsage;
        stream.geometryUsage = state.geometryUsage;
        auto program = Program::prepareOriginalTyped(graph, stream, streamQuota, id);
        if (!program)
            std::fprintf(stderr, "geometry freeze Stream4: %s\n", id);
        assert(program);
        Projection::Budget projected{4u,
            true,
            state.descriptorConsumption,
            state.renderDescriptorUsage,
            commandCount,
            2u,
            12u,
            1u,
            2u};
        projected.geometryUsage = state.geometryUsage;
        const Dependency::Request dependency{
            4u, true, {4u, true, projected}, 2u, 17u, 204u, 66u, 16u, 2u};
        const auto inspected = Dependency::inspect(program, graph, dependency);
        if (inspected.diagnostic)
            std::fprintf(stderr, "geometry freeze projection: %s\n", inspected.diagnostic);
        assert(!inspected.diagnostic && inspected.counts.dependencies == 2u
            && inspected.counts.initial == 1u && inspected.counts.between == 1u
            && inspected.counts.endpointContributions == 16u);
        state.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        auto root = Root::prepareOriginalOwnership(graph, device, state, recording, id);
        assert(root);
        assert(!root->materializeOriginalPrototypePlan(stream.stateSequence)
            && root->plannedAccessStepCount() == 14u);
        auto loss = device.observeOriginalDeviceNativeLoss(id);
        assert(loss && !id);
        rhi::OriginalCommandNativeClosureBudget custody{6u,
            true,
            3u,
            static_cast<std::uint32_t>(sources->sources().size()),
            root->retainedOwnerCount() + 3u + Source::initializedIndexOwnerBound
                + static_cast<std::uint32_t>(sources->sources().size())
                    * (1u + rhi::OriginalWholeDescriptorWriteContract::quotaCount),
            0u,
            bytes,
            2u,
            2u};
        custody.geometryContent = {1u,
            true,
            1u,
            Source::initializedIndexOwnerBound,
            16u,
            state.geometryUsage,
            {1u, true, commandCount, 3u, 0u, 13u, 6u}};
        for (const auto& source : sources->sources())
            std::visit(
                [&](const auto& lease) {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(lease)>::element_type>;
                    if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>
                        || std::is_same_v<T, rhi::OriginalCommandTextureLease>)
                        custody.maxRetainedResourceBytes +=
                            lease->originalObject()->allocation().estimatedSizeBytes;
                },
                source);
        Closure::GeometryContentInput content{2u, initialized};
        auto closure = Closure::prepareOriginalGeometryContentLossBound(
            root, program, &content, 1u, device, custody, *loss, id);
        if (!closure)
            std::fprintf(stderr, "geometry freeze Closure6: %s\n", id);
        assert(closure);
        Freeze::Request request{4u, true, dependency, closure->retainedOwnerCount() + 1u, bytes};
        const auto make = [&] {
            return Freeze::prepareOriginal(closure, request, id);
        };
        const auto exact = request;
        const auto previousGraphOnlyOwnerCapacity = sources->originalBudget().maxGraphOwners
            - indexedGeometryFreezeOwnerDemand - indexedGeometryColdOwnerDemand
            - OriginalTypedColdAdmissionFixture::geometryExecutionOwnerDemand();
        assert(previousGraphOnlyOwnerCapacity < request.maxRetainedOwners);
        request.maxRetainedOwners = previousGraphOnlyOwnerCapacity;
        const auto beforeFreeze = recording->liveBytes();
        const auto beforeFreezeAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!make());
        is(id, "RHI-WEBGPU-TYPED-DEPENDENCY-OWNER-CAPACITY");
        assert(recording->liveBytes() == beforeFreeze
            && core::defaultCpuAllocator().stats().allocationCount == beforeFreezeAllocations);
        request = exact;
        --request.maxRetainedOwners;
        assert(!make());
        is(id, "RHI-WEBGPU-TYPED-DEPENDENCY-OWNER-CAPACITY");
        request = exact;
        --request.projection.maxEndpointContributions;
        assert(!make());
        is(id, "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-CAPACITY");
        request = exact;
        --request.maxCpuMetadataBytes;
        assert(!make());
        is(id, "RHI-WEBGPU-TYPED-DEPENDENCY-QUOTA");
        request = exact;
        CpuQuotaFactoryFixture pressure(recording);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        auto frozen = make();
        if (!frozen)
            std::fprintf(stderr, "geometry Freeze4: %s\n", id);
        assert(frozen);
        assert(frozen->records().size() == 2u && !frozen->synchronizationProven()
            && !frozen->nativeExecutionProven());
        assert(!frozen->compareOriginalClosure(closure));
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        auto payload = [&] {
            if constexpr (CompleteRenderState)
                return OriginalTypedNativePayloadFixture::verify(frozen,
                    std::array{recording, stateQuota, streamQuota, inputsQuota},
                    "geometry-complete-indexed3",
                    4u);
            else
                return OriginalTypedNativePayloadFixture::verify(frozen,
                    std::array{recording, stateQuota, streamQuota, inputsQuota},
                    "geometry-indexed3",
                    4u);
        }();
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdAdmission> admission;
        if constexpr (CompleteRenderState) {
            admission = OriginalTypedColdAdmissionFixture::verifyGeometry(payload, device);
        } else
            OriginalTypedColdAdmissionFixture::verifyGeometryRenderStateRefusal(payload);
        assert(payload->work().commands == commandCount
            && payload->work().nativeCalls == (CompleteRenderState ? 19u : 15u)
            && payload->work().indexBinds == 2u && payload->work().indexedDraws == 3u
            && !payload->work().vertexBinds && !payload->work().draws);
        for (const auto version : {1u, 2u, 3u}) {
            auto legacy = payload->originalRequest();
            legacy.contractVersion = version;
            const auto allocationCount = core::defaultCpuAllocator().stats().allocationCount;
            assert(
                !rhi_webgpu::WebGpuOriginalTypedNativePayload::prepareOriginal(frozen, legacy, id));
            is(id, "RHI-ORIGINAL-TYPED-PAYLOAD-GEOMETRY-SCHEMA");
            assert(allocationCount == core::defaultCpuAllocator().stats().allocationCount);
        }
#endif
        if constexpr (!CompleteRenderState)
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{recording}, [&]() noexcept -> const char* {
                        assert(frozen->executionDiagnostic());
                        return frozen->validate();
                    }));
            });
        std::weak_ptr<const Closure> retained = closure;
        closure.reset();
        root.reset();
        graph.reset();
        program.reset();
        assert(!retained.expired());
        assert(!frozen->validate());
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        frozen.reset();
        assert(!retained.expired());
        assert(!payload->validate());
        payload.reset();
        if constexpr (CompleteRenderState) {
            assert(!retained.expired()
                && admission->originalRoot()->status() == Root::Status::Prepared
                && !admission->validate());
            inputs.reset();
            scalars.reset();
            arguments.reset();
            usage.reset();
            records[1u].arguments = records[6u].arguments = rhi::OriginalCommandPipelineBinding{};
            pass.reset();
            return {std::move(admission),
                {inputsQuota, stateQuota, streamQuota, recording},
                std::move(retained)};
        }
#endif
        frozen.reset();
        assert(retained.expired());
        retained.reset();
        assert(!recording->liveBytes() && !stateQuota->liveBytes() && !streamQuota->liveBytes());
        inputs.reset();
        scalars.reset();
        arguments.reset();
        usage.reset();
        records[1u].arguments = records[6u].arguments = rhi::OriginalCommandPipelineBinding{};
        pass.reset();
        assert(!inputsQuota->liveBytes());
        if constexpr (!CompleteRenderState)
            std::printf(
                "BINDLESS_RHI_GEOMETRY_DEPENDENCY_FREEZE_OK freeze4 closure6 same-original-sources commands16 draw3 clauses2 initial-between endpoint16 explicit-barriers union-index-shader exact-less caller-drop hot0-query0 native-execution-closed peak%llu\n",
                static_cast<unsigned long long>(peak));
        return {};
    }

    template <bool CompleteRenderState = false>
    static void verifyGeometryDependencyFreeze(const std::shared_ptr<const Inputs>& original,
        const rhi::GpuDevice& device,
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized)
    {
        auto owners =
            prepareGeometryDependencyFreeze<CompleteRenderState>(original, device, initialized);
        if constexpr (CompleteRenderState) {
            using Root = rhi::OriginalCommandStateTransaction;
            assert(owners.admission && !owners.retained.expired());
            OriginalTypedColdAdmissionFixture::executeGeometry(owners.admission, initialized);
            assert(owners.admission->originalRoot()->status() == Root::Status::Committed);
            const auto& sources = original->originalScalars()->originalUsage()->originalSources();
            const auto submission =
                owners.admission->originalReservation()->originalSeal()->originalSubmissionQuota();
            const auto callback =
                owners.admission->originalReservation()->originalSeal()->originalCallbackQuota();
            owners.admission.reset();
            assert(owners.retained.expired());
            owners.retained.reset();
            assert(!submission->liveBytes() && !callback->liveBytes());
            assert(!sources->originalReservedSubmissionCount()
                && !sources->originalReservedCallbackTicketCount()
                && !sources->originalReservedCallbackRegistrationCount());
            for (const auto& quota : owners.quotas)
                assert(!quota->liveBytes());
        } else
            assert(!owners.admission);
    }

    template <class Analyzer, class Visitor>
    static void verifyIndexedStateStream(const std::shared_ptr<const Inputs>& inputs,
        const rhi::GpuDevice& device,
        const rhi::OriginalIndexedDrawFetchContract::Budget& fetch,
        Analyzer&& analyze,
        Visitor&& observe,
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized,
        bool expectPipelineMismatch = false)
    {
        using Graph = rhi::OriginalCommandResourceStates;
        using Program = rhi::OriginalCommandStreamProgram;
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto streamQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto& root = inputs->originalScalars()->originalUsage()->originalSources();
        std::uint32_t owners = 0u, registrations = 0u;
        const char* diagnostic = nullptr;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(
            root, inputs->retainedOwnerCount() + 1u, 64u, 64u, owners, registrations));
        rhi::OriginalCommandStateAdmissionBudget state{4u,
            true,
            1u,
            registrations,
            3u,
            2u,
            owners,
            bytes,
            {3u, true, 0u, 1u, false, true, 3u, 3u, 1u, true, 1u},
            {3u, true, 0u, 1u, false, true, 17u, 3u, 3u, 1u, true, 1u}};
        state.geometryUsage = {1u, true, 17u, 3u, 3u};
        const auto stateFactory = [&] {
            return Graph::prepareOriginal(inputs, device, state, stateQuota, diagnostic);
        };
        assert(!stateFactory());
        is(diagnostic, "RHI-ORIGINAL-CONSUMPTION-COMMAND-PROFILE");
        state.descriptorConsumption.indexedDrawConsumptionVersion = 1u;
        state.descriptorConsumption.indexedDrawConsumptionOptIn = true;
        auto graph = stateFactory();
        if (!graph)
            std::fprintf(stderr, "indexed consumption State4: %s\n", diagnostic);
        assert(graph);
        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = 4u;
        stream.prototypeOptIn = true;
        stream.maxCommands = 17u;
        stream.maxDescriptors = 11u;
        stream.maxReferences = 11u;
        stream.maxRenderPasses = 2u;
        stream.maxAttachmentReferences = 2u;
        stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = bytes;
        stream.maxPipelineBinds = 3u;
        stream.maxDescriptorBinds = 3u;
        stream.maxConsumptions = 3u;
        stream.maxImplicitAccesses = 9u;
        stream.descriptorConsumption = state.descriptorConsumption;
        stream.renderDescriptorUsage = state.renderDescriptorUsage;
        stream.geometryUsage = state.geometryUsage;
        stream.stateSequence = {4u, true, 17u, 0u, 3u, 11u, 9u};
        const auto streamFactory = [&] {
            return Program::prepareOriginalTyped(graph, stream, streamQuota, diagnostic);
        };
        auto mismatch = stream;
        mismatch.descriptorConsumption.indexedDrawConsumptionOptIn = false;
        mismatch.descriptorConsumption.indexedDrawConsumptionVersion = 0u;
        std::size_t visits = 0u;
        is(Program::visitOriginalTyped(graph,
               mismatch,
               [&](const auto&) noexcept -> const char* {
                   ++visits;
                   return nullptr;
               }),
            "RHI-ORIGINAL-STREAM-FOOTPRINT-MISMATCH");
        assert(!visits);
        auto less = stream;
        --less.maxConsumptions;
        assert(!Program::prepareOriginalTyped(graph, less, streamQuota, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-STREAM-CONSUMPTION-CAPACITY");
        assert(!streamQuota->liveBytes());
        less = stream;
        --less.maxDescriptorBinds;
        assert(!Program::prepareOriginalTyped(graph, less, streamQuota, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-STREAM-DESCRIPTOR-BIND-CAPACITY");
        assert(!streamQuota->liveBytes());
        less = stream;
        --less.maxImplicitAccesses;
        assert(!Program::prepareOriginalTyped(graph, less, streamQuota, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-STREAM-IMPLICIT-CAPACITY");
        assert(!streamQuota->liveBytes());
        auto program = streamFactory();
        if (!program)
            std::fprintf(stderr, "indexed consumption Stream4: %s\n", diagnostic);
        assert(program);
        assert(program->counts().consumptions == 3u && program->counts().geometryBinds == 3u
            && program->counts().descriptorBinds == 3u && program->counts().implicitAccesses == 9u);
        const auto hot = [&]() noexcept -> const char* {
            std::size_t emitted = 0u;
            const auto result = program->visitIndexedDrawFetch(
                fetch, analyze, [&](const auto& point) noexcept -> const char* {
                    // The original observer has its own three-draw count; use the
                    // retained stream's indexed ordinal for this joined traversal.
                    assert(program->operations()[point.commandIndex].firstElement == emitted++);
                    return nullptr;
                });
            assert(emitted == 3u && result.draws == 3u && result.analyzedIndices == 6u
                && result.bindingScans == 14u);
            return result.diagnostic;
        };
        (void)observe;
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{stateQuota, streamQuota}, hot));
        });
        auto shortFetch = fetch;
        --shortFetch.maxAnalyzedIndices;
        visits = 0u;
        is(program
                ->visitIndexedDrawFetch(shortFetch,
                    analyze,
                    [&](const auto&) noexcept -> const char* {
                        ++visits;
                        return nullptr;
                    })
                .diagnostic,
            "RHI-ORIGINAL-INDEXED-FETCH-ANALYSIS-CAPACITY");
        assert(!visits);
        std::weak_ptr<const Graph> stateRetained = graph;
        graph.reset();
        assert(!stateRetained.expired() && !hot());
        // Custody consumes the exact joined State4/Stream4, not the earlier
        // binding-only sequence that leaves this same root's descriptor unused.
        graph = stateRetained.lock();
        assert(graph);
        if (!expectPipelineMismatch) {
            verifyGeometryDrawConsumption(inputs, device, state, stream, initialized);
            verifyGeometryDependencyFreeze(inputs, device, initialized);
            verifyGeometryDependencyFreeze<true>(inputs, device, initialized);
        }
        verifyGeometryIndexCustody(
            graph, program, device, state, initialized, false, expectPipelineMismatch);
        graph.reset();
        assert(!stateRetained.expired() && !program->validate());
        program.reset();
        assert(stateRetained.expired());
        stateRetained.reset();
        assert(!stateQuota->liveBytes() && !streamQuota->liveBytes());
        if (!expectPipelineMismatch)
            std::puts(
                "BINDLESS_RHI_INDEXED_STATE_STREAM_OK same-inputs5 state4 stream4 commands17 descriptors3 draws3 indexed-ordinals3 analysis6 scans14 independent-opt-in footprint-mismatch-no-output exact-less caller-drop hot0 query0 native-closed");
    }
#endif

    static void verifyIndexedDrawFetch(const std::shared_ptr<const Inputs>& bindings,
        const rhi::GpuDevice& device,
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Fetch = rhi::OriginalIndexedDrawFetchContract;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Usage = rhi::OriginalCommandUsage;
        using Binding = rhi::OriginalCommandGeometryBinding;
        const auto& root = bindings->originalScalars()->originalUsage()->originalSources();
        const auto quota = bindings->originalMetadataQuota();
        const auto& old = bindings->descriptors();
        unsigned invalid = 0u;
        const char* diagnostic = nullptr;
        const auto make = [&]() {
            std::array<rhi::CommandUsage, 14u> commands{{{K::BeginRenderPass, 0u, 1u},
                {K::BindGraphicsPipeline, 1u, 1u},
                {K::BindIndexBuffer, 2u, 1u},
                {K::DrawIndexed, 3u, 0u},
                {K::BindGraphicsPipeline, 3u, 1u},
                {K::BindIndexBuffer, 4u, 1u},
                {K::DrawIndexed, 5u, 0u},
                {K::EndRenderPass, 5u, 0u},
                {K::BeginRenderPass, 5u, 1u},
                {K::BindGraphicsPipeline, 6u, 1u},
                {K::BindIndexBuffer, 7u, 1u},
                {K::DrawIndexed, 8u, 0u},
                {K::EndRenderPass, 8u, 0u},
                {K::Finish, 8u, 0u}}};
            if (invalid == 5u) {
                commands[5] = {K::DrawIndexed, 4u, 0u};
                commands[6] = {K::BindIndexBuffer, 4u, 1u};
            }
            const std::array<Usage::Reference, 8u> refs{
                {{1u}, {3u}, {2u}, {4u}, {2u}, {1u}, {3u}, {2u}}};
            auto usage = Usage::prepareOriginal(root,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                quota,
                diagnostic);
            assert(usage);
            const auto field = [&](std::size_t command) {
                const auto kind = commands[command].kind;
                if (kind == K::BindIndexBuffer)
                    return rhi::CommandVariableArguments::Input{
                        *Scalar::capture(kind,
                            Scalar::IndexBinding{
                                rhi::IndexFormat::Uint16, command == 10u ? 14u : 0u}),
                        {}};
                if (kind == K::DrawIndexed) {
                    Scalar::DrawIndexed draw = command == 3u
                        ? Scalar::DrawIndexed{3u, 2u, 1u, -1, 3u}
                        : command == 11u ? Scalar::DrawIndexed{1u, 1u, 0u, -7, 0u}
                                         : Scalar::DrawIndexed{2u, 3u, 2u, 3, 1u};
                    // Keep the authored total analysis ceiling at six when
                    // testing two indices against the final one-element tail.
                    // Otherwise the separate budget failure correctly wins.
                    if (invalid == 2u && command == 6u)
                        draw.indices = 1u;
                    if (command == 11u) {
                        if (invalid == 1u)
                            draw.vertexOffset = -8;
                        if (invalid == 2u)
                            draw.indices = 2u;
                        if (invalid == 3u)
                            draw.firstIndex = 1u;
                        if (invalid == 4u)
                            draw.instances = 0u;
                    }
                    return rhi::CommandVariableArguments::Input{*Scalar::capture(kind, draw), {}};
                }
                return rhi::CommandVariableArguments::Input{
                    *Scalar::capture(kind, Scalar::None{}), {}};
            };
            const std::array<rhi::CommandVariableArguments::Input, 14u> fields{{field(0),
                field(1),
                field(2),
                field(3),
                field(4),
                field(5),
                field(6),
                field(7),
                field(8),
                field(9),
                field(10),
                field(11),
                field(12),
                field(13)}};
            auto arguments = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
                fields.size(),
                {1u, true, 14u, 0u, 0u, 0u, bytes},
                quota,
                diagnostic);
            assert(arguments);
            auto scalars =
                rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, quota, diagnostic);
            assert(scalars);
            auto first = std::get<Binding>(old[2].arguments),
                 second = std::get<Binding>(old[4].arguments),
                 last = std::get<Binding>(old[7].arguments);
            second.pipelineBindCommandIndex = 4u;
            last.pipelineBindCommandIndex = 9u;
            last.scopeBeginCommandIndex = 8u;
            auto lastPipeline = std::get<rhi::OriginalCommandPipelineBinding>(old[6].arguments);
            lastPipeline.scopeBeginCommandIndex = 8u;
            const std::array<Inputs::OriginalDescriptor, 8u> records{{{0u, old[0].arguments},
                {1u, old[1].arguments},
                {2u, first},
                {4u, old[3].arguments},
                {invalid == 5u ? 6u : 5u, second},
                {8u, old[5].arguments},
                {9u, lastPipeline},
                {10u, last}}};
            rhi::OriginalCommandInputsBudget b{5u, true, 3u};
            b.maxIndexBindings = 3u;
            auto result = Inputs::prepareOriginal(
                scalars, records.data(), records.size(), b, quota, diagnostic);
            if (!result)
                std::fprintf(stderr, "indexed fetch Inputs: %s\n", diagnostic);
            assert(result);
            return result;
        };
        Fetch::Budget b{1u, true, 14u, 3u, 0u, 24u, 6u};
        const auto analyze = [&](const Fetch::Point& point, const auto& lease, auto& out) noexcept {
            return rhi_webgpu::WebGpuCanonicalBufferUploadSource::analyzeInitializedIndices(
                initialized,
                lease,
                point.index.format,
                point.index.offset,
                point.draw.firstIndex,
                point.draw.indices,
                point.draw.vertexOffset,
                out);
        };
        auto inputs = make();
        std::size_t emitted = 0u;
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota}, [&]() noexcept -> const char* {
                    std::uint64_t scanned = 0u;
                    auto first = inputs->latestGeometryBinding(
                        K::BindIndexBuffer, 0u, 1u, 3u, b.maxBindingScans, scanned);
                    assert(first.record == &inputs->descriptors()[2u] && !first.scanCapacityExceeded
                        && scanned == 6u);
                    const auto exact = scanned;
                    scanned = 0u;
                    auto limited = inputs->latestGeometryBinding(
                        K::BindIndexBuffer, 0u, 1u, 3u, exact - 1u, scanned);
                    assert(
                        !limited.record && limited.scanCapacityExceeded && scanned == exact - 1u);
                    scanned = 0u;
                    auto last =
                        inputs->latestGeometryBinding(K::BindIndexBuffer, 0u, 9u, 11u, 1u, scanned);
                    assert(last.record == &inputs->descriptors()[7u] && !last.scanCapacityExceeded
                        && scanned == 1u);
                    scanned = 0u;
                    auto absent = inputs->latestGeometryBinding(
                        K::BindVertexBuffer, 1u, 9u, 11u, inputs->descriptors().size(), scanned);
                    assert(!absent.record && !absent.scanCapacityExceeded
                        && scanned == inputs->descriptors().size());
                    scanned = 0u;
                    auto invalid =
                        inputs->latestGeometryBinding(K::DrawIndexed, 0u, 1u, 3u, exact, scanned);
                    assert(!invalid.record && !invalid.scanCapacityExceeded && !scanned);
                    invalid = inputs->latestGeometryBinding(
                        K::BindIndexBuffer, 0u, 3u, 3u, exact, scanned);
                    assert(!invalid.record && !invalid.scanCapacityExceeded && !scanned);
                    return nullptr;
                }));
        });
        std::puts(
            "BINDLESS_RHI_GEOMETRY_LATEST_BINDING_OK shared-inputs5 core-bounded-reverse lookup-only exact-one-less scan-shared no-match invalid-kind-ordinal hot0 query0");
        const auto observe = [&](const Fetch::Point& point) noexcept -> const char* {
            const std::array<std::size_t, 3> draws{3u, 6u, 11u}, indexBinds{2u, 5u, 10u},
                pipelineBinds{1u, 4u, 9u};
            const std::array<std::uint64_t, 3> firstVertices{0u, 5u, 0u}, counts{3u, 2u, 1u};
            assert(emitted < 3u && point.commandIndex == draws[emitted]
                && point.indexBind == indexBinds[emitted]
                && point.pipelineBind == pipelineBinds[emitted] && point.indexSource == 2u
                && point.fetch.firstVertex == firstVertices[emitted]
                && point.fetch.vertexCount == counts[emitted] && !point.vertexStreams
                && !point.nativeExecutionProven());
            ++emitted;
            return nullptr;
        };
        const auto result = Fetch::visit(inputs, b, analyze, observe);
        if (result.diagnostic)
            std::fprintf(stderr, "indexed fetch projection: %s\n", result.diagnostic);
        assert(!result.diagnostic && emitted == 3u && result.draws == 3u
            && result.analyzedIndices == 6u && result.bindingScans == 11u && !result.streams);
        const auto refuse = [&](const Fetch::Budget& budget, const char* expected) {
            std::size_t output = 0u;
            const auto result =
                Fetch::visit(inputs, budget, analyze, [&](const auto&) noexcept -> const char* {
                    ++output;
                    return nullptr;
                });
            is(result.diagnostic, expected);
            assert(!output);
        };
        auto less = b;
        --less.maxCommands;
        refuse(less, "RHI-ORIGINAL-INDEXED-FETCH-COMMAND-CAPACITY");
        less = b;
        --less.maxDraws;
        refuse(less, "RHI-ORIGINAL-INDEXED-FETCH-DRAW-CAPACITY");
        less = b;
        less.maxBindingScans = result.bindingScans - 1u;
        refuse(less, "RHI-ORIGINAL-INDEXED-FETCH-SCAN-CAPACITY");
        less = b;
        --less.maxAnalyzedIndices;
        refuse(less, "RHI-ORIGINAL-INDEXED-FETCH-ANALYSIS-CAPACITY");
        for (const auto& [mode, id] : std::array<std::pair<unsigned, const char*>, 5>{
                 {{1u, "RHI-INDEX-INPUT-BASE-VERTEX-NEGATIVE"},
                     {2u, "CORE-UNSIGNED-SPAN-RANGE-INVALID"},
                     {3u, "CORE-UNSIGNED-SPAN-RANGE-INVALID"},
                     {4u, "RHI-ORIGINAL-INDEXED-FETCH-DRAW-EMPTY"},
                     {5u, "RHI-ORIGINAL-INDEXED-FETCH-INDEX-UNBOUND"}}}) {
            invalid = mode;
            inputs = make();
            refuse(b, id);
            if (mode == 4u || mode == 5u) {
                rhi::OriginalGeometryUsageBudget geometry{1u, true, 14u, 3u, 15u};
                geometry.drawConsumptionContractVersion = 1u;
                geometry.drawConsumptionOptIn = true;
                geometry.maxDraws = 3u;
                geometry.maxDrawAccesses = 3u;
                geometry.maxBindingScans = 24u;
                std::size_t visits = 0u;
                is(inputs
                        ->visitGeometryAccesses(geometry,
                            [&](const auto&) noexcept -> const char* {
                                ++visits;
                                return nullptr;
                            })
                        .diagnostic,
                    mode == 4u ? "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-DRAW-EMPTY"
                               : "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-UNBOUND");
                assert(!visits);
            }
        }
        invalid = 0u;
        inputs = make();
        std::size_t output = 0u;
        const auto forged =
            [&](const auto& point, const auto& lease, auto& out) noexcept -> const char* {
            if (const auto* id = analyze(point, lease, out))
                return id;
            ++out.indexCount;
            return nullptr;
        };
        is(Fetch::visit(inputs,
               b,
               forged,
               [&](const auto&) noexcept -> const char* {
                   ++output;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-INDEXED-FETCH-ANALYZER-CORRESPONDENCE");
        assert(!output);
        const auto hot = [&] {
            assert(
                !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
                    return Fetch::visit(inputs,
                        b,
                        analyze,
                        [](const auto&) noexcept -> const char* {
                            return nullptr;
                        })
                        .diagnostic;
                }));
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(hot);
        // The geometry-only sequence deliberately omits the pipeline's real
        // descriptor set. Consumption must reject it before any output.
        rhi::OriginalCommandDescriptorConsumptionContract::Budget consumption{
            3u, true, 0u, 1u, false, true, 3u, 3u, 1u, true, 1u};
        consumption.indexedDrawConsumptionVersion = 1u;
        consumption.indexedDrawConsumptionOptIn = true;
        output = 0u;
        is(rhi::common::OriginalCommandDescriptorConsumption::visit(inputs,
               device,
               consumption,
               [&](const auto&) noexcept -> const char* {
                   ++output;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-CONSUMPTION-DESCRIPTOR-MISSING");
        assert(!output);

        // Join the same initialized index lease and original Sources to genuine
        // whole-write descriptor bindings after every pipeline epoch change.
        const auto& descriptor =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                root->sources()[5u]);
        const auto& secondDescriptor =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                root->sources()[6u]);
        const std::array<rhi::CommandUsage, 17u> joinedCommands{{{K::BeginRenderPass, 0u, 1u},
            {K::BindGraphicsPipeline, 1u, 1u},
            {K::BindRenderDescriptor, 2u, 1u},
            {K::BindIndexBuffer, 3u, 1u},
            {K::DrawIndexed, 4u, 0u},
            {K::BindGraphicsPipeline, 4u, 1u},
            {K::BindRenderDescriptor, 5u, 1u},
            {K::BindIndexBuffer, 6u, 1u},
            {K::DrawIndexed, 7u, 0u},
            {K::EndRenderPass, 7u, 0u},
            {K::BeginRenderPass, 7u, 1u},
            {K::BindGraphicsPipeline, 8u, 1u},
            {K::BindRenderDescriptor, 9u, 1u},
            {K::BindIndexBuffer, 10u, 1u},
            {K::DrawIndexed, 11u, 0u},
            {K::EndRenderPass, 11u, 0u},
            {K::Finish, 11u, 0u}}};
        std::array<Usage::Reference, 11u> joinedRefs{
            {{1u}, {3u}, {5u}, {2u}, {4u}, {5u}, {2u}, {1u}, {3u}, {5u}, {2u}}};
        const auto& previousArguments = *inputs->originalScalars()->originalArguments();
        const std::array<std::size_t, 17u> previousCommand{0u,
            1u,
            SIZE_MAX,
            2u,
            3u,
            4u,
            SIZE_MAX,
            5u,
            6u,
            7u,
            8u,
            9u,
            SIZE_MAX,
            10u,
            11u,
            12u,
            13u};
        const rhi::CommandVariableArguments::Input empty{
            *Scalar::capture(K::Finish, Scalar::None{}), {}};
        std::array<rhi::CommandVariableArguments::Input, 17u> joinedFields{{empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty,
            empty}};
        for (std::size_t i = 0u; i < joinedFields.size(); ++i)
            joinedFields[i].scalar = previousCommand[i] == SIZE_MAX
                ? *Scalar::capture(K::BindRenderDescriptor,
                      Scalar::DescriptorBinding{descriptor->frozenLogicalSet()})
                : previousArguments.records()[previousCommand[i]].scalar;
        auto joinedArguments = rhi::CommandVariableArguments::prepareOriginal(joinedFields.data(),
            joinedFields.size(),
            {1u, true, 17u, 0u, 0u, 0u, bytes},
            quota,
            diagnostic);
        assert(joinedArguments);
        const auto& previous = inputs->descriptors();
        auto firstIndex = std::get<Binding>(previous[2u].arguments),
             secondIndex = std::get<Binding>(previous[4u].arguments),
             lastIndex = std::get<Binding>(previous[7u].arguments);
        secondIndex.pipelineBindCommandIndex = 5u;
        lastIndex.pipelineBindCommandIndex = 11u;
        lastIndex.scopeBeginCommandIndex = 10u;
        auto lastPipeline = std::get<rhi::OriginalCommandPipelineBinding>(previous[6u].arguments);
        lastPipeline.scopeBeginCommandIndex = 10u;
        const auto descriptorBinding = [&](std::size_t pipeline, std::size_t scope) {
            return rhi::OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
                5u,
                pipeline,
                scope,
                descriptor->originalObject()->originalResourceRegistration().generation(),
                descriptor->frozenContentRevision()};
        };
        std::array<Inputs::OriginalDescriptor, 11u> joinedRecords{{{0u, previous[0u].arguments},
            {1u, previous[1u].arguments},
            {2u, descriptorBinding(3u, 0u)},
            {3u, firstIndex},
            {5u, previous[3u].arguments},
            {6u, descriptorBinding(4u, 0u)},
            {7u, secondIndex},
            {10u, previous[5u].arguments},
            {11u, lastPipeline},
            {12u, descriptorBinding(3u, 10u)},
            {13u, lastIndex}}};
        rhi::OriginalCommandInputsBudget joinedBudget{5u, true, 3u, 3u};
        joinedBudget.maxIndexBindings = 3u;
        const auto join = [&] {
            auto joinedUsage = Usage::prepareOriginal(root,
                joinedCommands.data(),
                joinedCommands.size(),
                joinedRefs.data(),
                joinedRefs.size(),
                quota,
                diagnostic);
            assert(joinedUsage);
            auto joinedScalars = rhi::OriginalCommandScalars::prepareOriginal(
                joinedUsage, joinedArguments, quota, diagnostic);
            assert(joinedScalars);
            auto joinedInputs = Inputs::prepareOriginal(joinedScalars,
                joinedRecords.data(),
                joinedRecords.size(),
                joinedBudget,
                quota,
                diagnostic);
            if (!joinedInputs)
                std::fprintf(stderr, "indexed joined Inputs: %s\n", diagnostic);
            assert(joinedInputs);
            return joinedInputs;
        };
        auto joinedFetch = b;
        joinedFetch.maxCommands = 17u;
        joinedFetch.maxBindingScans = 14u;
        // Identical layouts do not authorize sharing a whole-write payload
        // across distinct original pipeline owners. Keep that failure explicit.
        verifyIndexedStateStream(join(), device, joinedFetch, analyze, observe, initialized, true);
        joinedRefs[5u] = {6u};
        joinedRecords[5u] = {6u,
            rhi::OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
                6u,
                4u,
                0u,
                secondDescriptor->originalObject()->originalResourceRegistration().generation(),
                secondDescriptor->frozenContentRevision()}};
        assert(descriptor->originalObject() != secondDescriptor->originalObject());
        verifyIndexedStateStream(join(), device, joinedFetch, analyze, observe, initialized);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_INDEXED_FETCH_OK inputs5 original-scalars-three-draws same-initialized-index-lease source2 pipeline-pass-epochs signed-base offsets14 total-analysis6 scans11 late-negative-no-output forged-range-refused hot0 query0 native-closed vertex-stream-integration-pending");
#else
        (void)bindings;
        (void)device;
        (void)initialized;
#endif
    }

    static void verifyGeometryStateStream(const std::shared_ptr<const Inputs>& inputs,
        const rhi::GpuDevice& device,
        const rhi::OriginalGeometryUsageBudget& footprint,
        bool readable,
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized)
    {
        using Graph = rhi::OriginalCommandResourceStates;
        using Sequence = rhi::common::OriginalCommandStateSequence;
        using Access = rhi::common::OriginalCommandAccessValidation;
        using Program = rhi::OriginalCommandStreamProgram;
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto streamQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        std::uint32_t owners = 0u, registrations = 0u;
        const char* id = nullptr;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(
            sources, inputs->retainedOwnerCount() + 1u, 64u, 64u, owners, registrations));
        rhi::OriginalCommandStateAdmissionBudget state{4u,
            true,
            1u,
            registrations,
            3u,
            2u,
            owners,
            bytes,
            {3u, true, 0u, 0u, false, false, 1u, 1u, 1u, true, 1u},
            {3u, true, 0u, 0u, false, false, 11u, 1u, 1u, 1u, true, 1u}};
        state.geometryUsage = footprint;
        const auto factory = [&] {
            return Graph::prepareOriginal(inputs, device, state, stateQuota, id);
        };
        const auto refuse = [&](const char* expected) {
            const auto live = stateQuota->liveBytes();
            assert(!factory());
            is(id, expected);
            assert(live == stateQuota->liveBytes());
        };
        const auto authored = state;
        --state.geometryUsage.maxPairChecks;
        refuse("RHI-ORIGINAL-GEOMETRY-ACCESS-PAIR-CAPACITY");
        state = authored;
        --state.geometryUsage.maxBindings;
        refuse("RHI-ORIGINAL-GEOMETRY-ACCESS-BINDING-CAPACITY");
        state = authored;
        --state.maxStateObservations;
        refuse("RHI-ORIGINAL-RESOURCE-STATES-OBSERVATION-CAPACITY");
        state = authored;
        --state.maxResourceRegistrations;
        refuse("RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
        state = authored;
        --state.maxRetainedOwners;
        refuse("RHI-ORIGINAL-STATE-OWNER-CAPACITY");
        state = authored;
        auto legacy = state;
        legacy.contractVersion = 3u;
        legacy.geometryUsage = {};
        legacy.descriptorConsumption.contractVersion = 2u;
        legacy.renderDescriptorUsage.contractVersion = 2u;
        assert(!Graph::prepareOriginal(inputs, device, legacy, stateQuota, id));
        is(id, "RHI-ORIGINAL-RENDER-USAGE-INPUTS-VERSION");
        CpuQuotaFactoryFixture statePressure(stateQuota);
        const auto statePeak = statePressure.measure(factory);
        statePressure.verify(statePeak, factory);
        auto graph = factory();
        if (!graph)
            std::fprintf(stderr, "geometry State4 failed: %s\n", id ? id : "unknown");
        assert(graph);
        assert(graph->stateContractVersion() == 4u && graph->bufferEntries().size() == 1u
            && graph->textureEntries().size() == 2u);
        rhi::OriginalCommandStateSequenceBudget sequence{4u, true, 11u, 0u, 3u, 5u, 3u};
        if (!readable) {
            is(Sequence::inspect(graph, sequence).diagnostic,
                "RHI-ORIGINAL-SEQUENCE-GEOMETRY-STATE");
            is(Access::inspect(graph, sequence).diagnostic, "RHI-ORIGINAL-SEQUENCE-GEOMETRY-STATE");
            graph.reset();
            assert(!stateQuota->liveBytes() && !streamQuota->liveBytes());
            std::puts(
                "BINDLESS_RHI_GEOMETRY_STATE_REFUSAL_OK original-CopyDst snapshot-no-read-authority native-closed");
            return;
        }
        const auto timeline = Access::inspect(graph, sequence);
        assert(!timeline.diagnostic && timeline.stateCells == 3u && timeline.steps == 5u);
        auto shortSequence = sequence;
        --shortSequence.maxImplicitAccesses;
        is(Access::inspect(graph, shortSequence).diagnostic,
            "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY");
        shortSequence = sequence;
        --shortSequence.maxSteps;
        is(Access::inspect(graph, shortSequence).diagnostic, "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY");
        shortSequence = sequence;
        --shortSequence.maxStateCells;
        is(Access::inspect(graph, shortSequence).diagnostic, "RHI-ORIGINAL-SEQUENCE-CELL-CAPACITY");
        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = 4u;
        stream.prototypeOptIn = true;
        stream.maxCommands = 11u;
        stream.maxDescriptors = 8u;
        stream.maxReferences = 8u;
        stream.maxRenderPasses = 2u;
        stream.maxAttachmentReferences = 2u;
        stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = bytes;
        stream.maxPipelineBinds = 3u;
        stream.maxDescriptorBinds = 1u;
        stream.maxConsumptions = 1u;
        stream.maxImplicitAccesses = 3u;
        stream.descriptorConsumption = state.descriptorConsumption;
        stream.renderDescriptorUsage = state.renderDescriptorUsage;
        stream.geometryUsage = footprint;
        stream.stateSequence = sequence;
        const auto streamFactory = [&] {
            return Program::prepareOriginalTyped(graph, stream, streamQuota, id);
        };
        const auto shortStream = [&](const char* expected) {
            std::size_t emitted = 0u;
            is(Program::visitOriginalTyped(graph,
                   stream,
                   [&](const auto&) noexcept -> const char* {
                       ++emitted;
                       return nullptr;
                   }),
                expected);
            assert(!emitted && !streamFactory());
            is(id, expected);
            assert(!streamQuota->liveBytes());
        };
        const auto originalStream = stream;
        --stream.geometryUsage.maxPairChecks;
        shortStream("RHI-ORIGINAL-STREAM-GEOMETRY-FOOTPRINT-MISMATCH");
        stream = originalStream;
        --stream.maxImplicitAccesses;
        shortStream("RHI-ORIGINAL-STREAM-IMPLICIT-CAPACITY");
        stream = originalStream;
        --stream.maxDescriptors;
        shortStream("RHI-ORIGINAL-STREAM-DESCRIPTOR-CAPACITY");
        stream = originalStream;
        --stream.maxReferences;
        shortStream("RHI-ORIGINAL-STREAM-REFERENCE-CAPACITY");
        stream = originalStream;
        --stream.maxRetainedOwners;
        shortStream("RHI-ORIGINAL-STREAM-OWNER-CAPACITY");
        stream = originalStream;
        auto oldStream = stream;
        oldStream.contractVersion = 3u;
        oldStream.geometryUsage = {};
        oldStream.stateSequence.contractVersion = 3u;
        oldStream.descriptorConsumption.contractVersion = 2u;
        oldStream.renderDescriptorUsage.contractVersion = 2u;
        is(Program::inspectTyped(graph, oldStream).diagnostic, "RHI-ORIGINAL-STREAM-STATE-PROFILE");
        CpuQuotaFactoryFixture streamPressure(streamQuota);
        const auto streamPeak = streamPressure.measure(streamFactory);
        streamPressure.verify(streamPeak, streamFactory);
        auto program = streamFactory();
        if (!program)
            std::fprintf(stderr, "geometry Stream4 failed: %s\n", id ? id : "unknown");
        assert(program);
        const auto hot = [&]() noexcept -> const char* {
            if (const auto* failure = graph->validate())
                return failure;
            if (const auto* failure = program->validate())
                return failure;
            const auto inspection = Program::inspectTyped(graph, stream);
            if (inspection.diagnostic)
                return inspection.diagnostic;
            assert(inspection.counts.commands == 11u && inspection.counts.descriptors == 8u
                && inspection.counts.references == 8u && inspection.counts.pipelineBinds == 3u
                && inspection.counts.geometryBinds == 3u && !inspection.counts.descriptorBinds
                && !inspection.counts.consumptions && inspection.counts.implicitAccesses == 3u);
            std::size_t visits = 0u;
            std::uint64_t bindings = 0u;
            const auto failure = Program::visitOriginalTyped(
                graph, stream, [&](const auto& op) noexcept -> const char* {
                    assert(op.matches(program->operations()[visits++]));
                    if (rhi::OriginalCommandGeometryBindingContract::isBinding(op.kind)) {
                        assert(op.elementCount == 1u && op.firstElement == bindings
                            && op.firstImplicitAccess == bindings && op.implicitAccessCount == 1u);
                        ++bindings;
                    } else
                        assert(!op.implicitAccessCount);
                    return nullptr;
                });
            assert(visits == 11u && bindings == 3u);
            return failure;
        };
        const auto verifyHot = [&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    inputs->originalMetadataQuota(),
                    stateQuota,
                    streamQuota},
                hot));
        };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(verifyHot);
#else
        verifyHot();
#endif
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
            assert(program->executionDiagnostic(backend) && !program->nativeExecutionProven());
        if (initialized) {
            verifyGeometryIndexCustody(graph, program, device, state, initialized, true);
            verifyIndexedDrawFetch(inputs, device, initialized);
        }
        std::weak_ptr<const Graph> retained = graph;
        graph.reset();
        assert(!retained.expired() && !program->validate());
        program.reset();
        assert(retained.expired());
        retained.reset();
        assert(!stateQuota->liveBytes() && !streamQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GEOMETRY_STATE_STREAM_OK state4 sequence4 stream4 inputs5 index3 original-observations exact-footprint no-output-on-refusal hot0 query0 caller-drop quotas-zero state%llu stream%llu native-closed\n",
            static_cast<unsigned long long>(statePeak),
            static_cast<unsigned long long>(streamPeak));
    }

    static void verifyGeometryDependencies(
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& root,
        const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::RenderPassArguments>& inheritedPass)
    {
        using Usage = rhi::OriginalCommandUsage;
        using Scalar = rhi::CommandScalarArguments;
        using K = rhi::CommandUsageKind;
        using Binding = rhi::OriginalCommandGeometryBinding;
        using Graph = rhi::OriginalCommandResourceStates;
        using Program = rhi::OriginalCommandStreamProgram;
        using P = rhi::common::OriginalTypedUsageProjection;
        using D = rhi::common::OriginalTypedDependencyProjection;
        using S = rhi::semantics::TypedShaderUsageScopeContract;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto streamQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto pass = passArguments(*root, quota);
        const auto& buffer =
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(root->sources()[2u]);
        const auto& pipeline =
            std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                root->sources()[3u]);
        const auto gb = buffer->originalObject()->originalResourceRegistration().generation();
        const auto gp = pipeline->originalObject()->originalResourceRegistration().generation();
        assert(buffer->originalObject()->state() == rhi::ResourceState::CopyDst);
        const char* id = nullptr;
        const std::array<rhi::CommandUsage, 11u> commands{{{K::BufferBarrier, 0u, 1u},
            {K::BeginRenderPass, 1u, 1u},
            {K::BindGraphicsPipeline, 2u, 1u},
            {K::BindIndexBuffer, 3u, 1u},
            {K::EndRenderPass, 4u, 0u},
            {K::BufferMemoryBarrier, 4u, 1u},
            {K::BeginRenderPass, 5u, 1u},
            {K::BindGraphicsPipeline, 6u, 1u},
            {K::BindIndexBuffer, 7u, 1u},
            {K::EndRenderPass, 8u, 0u},
            {K::Finish, 8u, 0u}}};
        const std::array<Usage::Reference, 8u> refs{
            {{2u}, {1u}, {3u}, {2u}, {2u}, {1u}, {3u}, {2u}}};
        auto usage = Usage::prepareOriginal(
            root, commands.data(), commands.size(), refs.data(), refs.size(), quota, id);
        assert(usage);
        const auto field = [&](std::size_t i) {
            return rhi::CommandVariableArguments::Input{commands[i].kind == K::BindIndexBuffer
                    ? *Scalar::capture(K::BindIndexBuffer,
                          Scalar::IndexBinding{rhi::IndexFormat::Uint16, i == 8u ? 14u : 0u})
                    : *Scalar::capture(commands[i].kind, Scalar::None{}),
                {}};
        };
        const std::array<rhi::CommandVariableArguments::Input, 11u> fields{{field(0u),
            field(1u),
            field(2u),
            field(3u),
            field(4u),
            field(5u),
            field(6u),
            field(7u),
            field(8u),
            field(9u),
            field(10u)}};
        auto arguments = rhi::CommandVariableArguments::prepareOriginal(
            fields.data(), fields.size(), {1u, true, 11u, 0u, 0u, 0u, bytes}, quota, id);
        assert(arguments);
        auto scalars = rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, quota, id);
        assert(scalars);
        rhi::ResourceBarrierDesc enter;
        enter.resource = buffer->resource();
        enter.before = rhi::ResourceState::CopyDst;
        enter.after = rhi::ResourceState::ShaderRead;
        enter.sourceStages = rhi::PipelineStage::Transfer;
        enter.sourceAccess = rhi::ResourceAccess::TransferWrite;
        enter.destinationStages = rhi::PipelineStage::VertexInput;
        enter.destinationAccess = rhi::ResourceAccess::IndexRead;
        rhi::ResourceMemoryBarrierDesc between;
        between.resource = buffer->resource();
        between.sourceStages = between.destinationStages = rhi::PipelineStage::VertexInput;
        between.sourceAccess = between.destinationAccess = rhi::ResourceAccess::IndexRead;
        std::array<Inputs::OriginalDescriptor, 8u> descriptors{
            {{0u, *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, enter)},
                {1u, pass},
                {2u, rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline, 3u, 1u, gp}},
                {3u, Binding{K::BindIndexBuffer, 2u, 3u, 2u, 1u, gb, gp}},
                {5u, *rhi::ResourceDependencyArguments::capture(K::BufferMemoryBarrier, between)},
                {6u, pass},
                {7u, rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline, 3u, 6u, gp}},
                {8u, Binding{K::BindIndexBuffer, 2u, 3u, 7u, 6u, gb, gp}}}};
        rhi::OriginalCommandInputsBudget inputBudget{5u, true, 2u};
        inputBudget.maxIndexBindings = 2u;
        auto wrongQuota = descriptors;
        wrongQuota[1u].arguments = wrongQuota[5u].arguments = inheritedPass;
        const auto before = quota->liveBytes();
        assert(!Inputs::prepareOriginal(
            scalars, wrongQuota.data(), wrongQuota.size(), inputBudget, quota, id));
        is(id, "RHI-ORIGINAL-INPUTS-PASS-QUOTA");
        assert(quota->liveBytes() == before);
        auto inputs = Inputs::prepareOriginal(
            scalars, descriptors.data(), descriptors.size(), inputBudget, quota, id);
        if (!inputs)
            std::fprintf(stderr, "geometry dependency inputs: %s\n", id ? id : "unknown");
        assert(inputs);
        std::uint32_t owners = 0u, registrations = 0u;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(
            root, inputs->retainedOwnerCount() + 1u, 64u, 64u, owners, registrations));
        rhi::OriginalCommandStateAdmissionBudget state{4u,
            true,
            1u,
            registrations,
            3u,
            2u,
            owners,
            bytes,
            {3u, true, 0u, 0u, false, false, 1u, 1u, 1u, true, 1u},
            {3u, true, 0u, 0u, false, false, 11u, 1u, 1u, 1u, true, 1u}};
        state.geometryUsage = {1u, true, 11u, 2u, 1u};
        auto graph = Graph::prepareOriginal(inputs, device, state, stateQuota, id);
        assert(graph);
        const rhi::OriginalCommandStateSequenceBudget sequence{4u, true, 11u, 2u, 3u, 6u, 2u};
        const auto timeline =
            rhi::common::OriginalCommandAccessValidation::inspect(graph, sequence);
        if (timeline.diagnostic)
            std::fprintf(stderr, "geometry dependency timeline: %s\n", timeline.diagnostic);
        assert(!timeline.diagnostic && timeline.steps == 6u && timeline.stateCells == 3u);
        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = 4u;
        stream.prototypeOptIn = true;
        stream.maxCommands = 11u;
        stream.maxDescriptors = stream.maxReferences = 8u;
        stream.maxRenderPasses = 2u;
        stream.maxAttachmentReferences = 2u;
        stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = bytes;
        stream.maxPipelineBinds = 2u;
        stream.maxDescriptorBinds = stream.maxConsumptions = 1u;
        stream.maxImplicitAccesses = 2u;
        stream.stateSequence = sequence;
        stream.descriptorConsumption = state.descriptorConsumption;
        stream.renderDescriptorUsage = state.renderDescriptorUsage;
        stream.geometryUsage = state.geometryUsage;
        auto program = Program::prepareOriginalTyped(graph, stream, streamQuota, id);
        assert(program);
        P::Budget projected{4u,
            true,
            state.descriptorConsumption,
            state.renderDescriptorUsage,
            11u,
            2u,
            4u,
            1u,
            2u};
        projected.geometryUsage = state.geometryUsage;
        const P::StateBoundRequest bound{4u, true, projected};
        const D::Request dependencies{4u, true, bound, 2u, 9u, 36u, 6u, 3u, 2u};
        const auto inspect = D::inspect(program, graph, dependencies);
        if (inspect.diagnostic)
            std::fprintf(stderr, "geometry dependency projection: %s\n", inspect.diagnostic);
        assert(!inspect.diagnostic && inspect.counts.dependencies == 2u
            && inspect.counts.initial == 1u && inspect.counts.between == 1u && !inspect.counts.final
            && inspect.counts.endpointContributions == 3u && inspect.counts.projectionPasses == 9u
            && inspect.counts.contributionVisits == 36u && inspect.counts.pairChecks == 6u
            && !inspect.counts.textureCells);
        std::array<D::Record, 2u> records{};
        std::size_t emitted = 0u;
        assert(!D::visit(program,
                   graph,
                   dependencies,
                   [&](const D::Record& record) noexcept -> const char* {
                       assert(emitted < records.size());
                       records[emitted++] = record;
                       return nullptr;
                   })
                   .diagnostic
            && emitted == 2u);
        assert(records[0].commandIndex == 0u && records[0].following && !records[0].preceding
            && records[0].following->scope.matches({S::Kind::RenderPass, 1u, 4u})
            && records[0].following->contributions == 1u);
        assert(records[1].commandIndex == 5u && records[1].preceding && records[1].following
            && records[1].preceding->scope.matches({S::Kind::RenderPass, 1u, 4u})
            && records[1].following->scope.matches({S::Kind::RenderPass, 6u, 9u}));
        for (const auto& record : records)
            assert(record.projectionVersion == 4u && record.sourceIndex == 2u
                && record.resource == buffer->resource() && record.registrationGeneration == gb
                && !D::validateCoverage(record));
        auto missing = records[1];
        auto weakBarrier = between;
        weakBarrier.destinationStages = rhi::PipelineStage::FragmentShader;
        weakBarrier.destinationAccess = rhi::ResourceAccess::ShaderRead;
        missing.original =
            rhi::ResourceDependencyArguments::capture(K::BufferMemoryBarrier, weakBarrier);
        is(D::validateCoverage(missing), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-COVERAGE");
        D::Counts frozenCounts;
        assert(!D::validateFrozenBinding(program, graph, dependencies, records[1], frozenCounts));
        missing = records[1];
        ++missing.registrationGeneration;
        frozenCounts = {};
        is(D::validateFrozenBinding(program, graph, dependencies, missing, frozenCounts),
            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-FROZEN-BINDING");
        auto shortUsage = projected;
        --shortUsage.maxContributions;
        emitted = 0u;
        is(P::visitStateBound(program,
               graph,
               {4u, true, shortUsage},
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-USAGE-PROJECTION-CONTRIBUTION-CAPACITY");
        assert(!emitted);
        shortUsage = projected;
        --shortUsage.geometryUsage.maxBindings;
        is(P::inspectStateBound(program, graph, {4u, true, shortUsage}).diagnostic,
            "RHI-ORIGINAL-USAGE-PROJECTION-GEOMETRY-FOOTPRINT-MISMATCH");
        shortUsage = projected;
        shortUsage.contractVersion = 2u;
        shortUsage.geometryUsage = {};
        shortUsage.shaderConsumption.contractVersion = shortUsage.renderBindings.contractVersion =
            2u;
        is(P::inspectStateBound(program, graph, {2u, true, shortUsage}).diagnostic,
            "RHI-ORIGINAL-STATE-BOUND-USAGE-STATE-VERSION");
        auto shortDependency = dependencies;
        const auto refuse = [&](const char* expected) {
            emitted = 0u;
            is(D::visit(program,
                   graph,
                   shortDependency,
                   [&](const auto&) noexcept -> const char* {
                       ++emitted;
                       return nullptr;
                   })
                    .diagnostic,
                expected);
            assert(!emitted);
            shortDependency = dependencies;
        };
        --shortDependency.maxPairChecks;
        refuse("RHI-ORIGINAL-DEPENDENCY-PROJECTION-PAIR-CAPACITY");
        --shortDependency.maxProjectionPasses;
        refuse("RHI-ORIGINAL-DEPENDENCY-PROJECTION-PASS-CAPACITY");
        --shortDependency.maxContributionVisits;
        refuse("RHI-ORIGINAL-DEPENDENCY-PROJECTION-VISIT-CAPACITY");
        --shortDependency.maxEndpointContributions;
        refuse("RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-CAPACITY");
        --shortDependency.maxDependencies;
        refuse("RHI-ORIGINAL-DEPENDENCY-PROJECTION-DEPENDENCY-CAPACITY");
        const auto hot = [&]() noexcept -> const char* {
            std::size_t geometry = 0u, attachments = 0u;
            const auto capture = [&](const P::Point& point) noexcept -> const char* {
                if (const auto* g = std::get_if<P::GeometryPoint>(&point)) {
                    assert(g->original.binding.registrationGeneration == gb
                        && g->original.binding.pipelineRegistrationGeneration == gp
                        && g->original.binding.sourceIndex == 2u
                        && g->use.resource == buffer->resource()
                        && g->use.contribution == S::Contribution::GeometryBinding
                        && g->use.usage == S::BufferUsage::Index
                        && g->use.access.state == rhi::ResourceState::ShaderRead
                        && g->use.access.stages == rhi::PipelineStage::VertexInput
                        && g->use.access.access == rhi::ResourceAccess::IndexRead
                        && g->use.offsetBytes == (g->use.commandIndex == 8u ? 14u : 0u)
                        && g->use.sizeBytes == 16u - g->use.offsetBytes);
                    ++geometry;
                } else {
                    assert(std::holds_alternative<P::AttachmentPoint>(point));
                    ++attachments;
                }
                return nullptr;
            };
            auto output = P::visit(program, device, projected, capture);
            if (output.diagnostic)
                return output.diagnostic;
            assert(output.counts.geometryBindings == 2u && output.counts.contributions == 4u
                && output.counts.textureCells == 2u && geometry == 2u && attachments == 2u
                && !output.nativeExecutionProven() && !output.synchronizationProven());
            geometry = attachments = 0u;
            output = P::visitStateBound(program, graph, bound, capture);
            if (output.diagnostic)
                return output.diagnostic;
            assert(geometry == 2u && attachments == 2u);
            const auto result = D::inspect(program, graph, dependencies);
            assert(result.counts.matches(inspect.counts) && !result.nativeExecutionProven()
                && !result.synchronizationProven());
            return result.diagnostic;
        };
        const auto verifyHot = [&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{root->originalMetadataQuota(), quota, stateQuota, streamQuota}, hot));
        };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(verifyHot);
#else
        verifyHot();
#endif
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            assert(!rhi::semantics::OriginalTypedDependencyProjectionLowering::lowering(backend, 4u)
                    .executionAvailable);
            const auto freeze =
                rhi::semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(
                    backend, 4u);
            assert(freeze.projectionAvailable == (backend == rhi::Backend::WebGPU)
                && !freeze.executionAvailable);
            assert(rhi::semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(
                backend, 8u)
                    .diagnostic);
            assert(program->executionDiagnostic(backend));
        }
        assert(buffer->originalObject()->state() == rhi::ResourceState::CopyDst);
        std::weak_ptr<const Graph> observer = graph;
        graph.reset();
        assert(!observer.expired() && !program->validate());
        program.reset();
        assert(observer.expired());
        observer.reset();
        inputs.reset();
        scalars.reset();
        arguments.reset();
        usage.reset();
        descriptors[1u].arguments = descriptors[5u].arguments =
            rhi::OriginalCommandPipelineBinding{};
        pass.reset();
        assert(!quota->liveBytes() && !stateQuota->liveBytes() && !streamQuota->liveBytes());
        std::puts(
            "BINDLESS_RHI_GEOMETRY_USAGE_DEPENDENCY_OK usage4 state-bound4 dependency4 original-index-provenance initial-and-between explicit-VertexInput-IndexRead complete4 no-output-on-refusal hot0 query0 caller-drop quotas-zero native-closed");
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    struct IndexedDescriptorOwners {
        std::shared_ptr<rhi::DescriptorArena> arena;
        std::array<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>,
            indexedDescriptorCount>
            sources;
        std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>, indexedDescriptorCount>
            quotas;
    };

    // Keep descriptor factory/write/freeze locals out of the geometry function's
    // continuation frame. These are the same original owners and finite quotas;
    // the helper neither issues draws nor changes the Asyncify budget.
    template <class Pipeline>
    static IndexedDescriptorOwners prepareIndexedDescriptors(rhi::GpuDevice& device,
        const std::shared_ptr<rhi::OriginalCommandResourceSources>& root,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& bufferLease,
        const std::shared_ptr<Pipeline>& first,
        const std::shared_ptr<Pipeline>& second,
        const std::shared_ptr<core::CpuMemoryQuota>& source,
        const std::shared_ptr<core::CpuMemoryQuota>& resources,
        std::uint32_t descriptorCount = indexedDescriptorCount)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        assert(descriptorCount && descriptorCount <= indexedDescriptorCount);
        IndexedDescriptorOwners owners;
        const char* diagnostic = nullptr;
        auto& descriptorArena = owners.arena;
        auto& descriptorSources = owners.sources;
        auto& descriptorQuotaSets = owners.quotas;
        {
            core::CpuMemoryQuotaScope scope(resources);
            rhi::DescriptorArenaDesc arena;
            arena.maxSets = descriptorCount;
            arena.maxDescriptors = descriptorCount;
            descriptorArena = core::shareUnique(device.createOriginalDescriptorArena(arena));
            if (!descriptorArena) {
                const auto id = device.lastOriginalResourceRegistrationDiagnostic();
                std::fprintf(stderr,
                    "indexed descriptor arena registration: %.*s\n",
                    static_cast<int>(id.size()),
                    id.data());
            }
            assert(descriptorArena);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto live = resources->liveBytes();
            assert(!device.createOriginalDescriptorArena(arena));
            is(device.lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
            assert(resources->liveBytes() == live
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        for (std::size_t pipelineIndex = 0u; pipelineIndex < descriptorCount; ++pipelineIndex) {
            const auto& pipeline = pipelineIndex == 0u ? first : second;
            auto& descriptorSource = descriptorSources[pipelineIndex];
            auto& descriptorQuotas = descriptorQuotaSets[pipelineIndex];
            core::UniquePtr<rhi::DescriptorSet> descriptor;
            {
                core::CpuMemoryQuotaScope scope(resources);
                rhi::DescriptorSetDesc set;
                set.layout = rhi::DescriptorStateCopy::copy(
                    pipeline->originalLayoutOwner()->desc().descriptorLayouts[0u]);
                assert(set.layout.bindings.size() == 1u && set.layout.bindings[0u].binding == 3u
                    && set.layout.bindings[0u].type == rhi::DescriptorType::ReadOnlyStorageBuffer
                    && set.layout.bindings[0u].stages == rhi::ShaderStage::Fragment);
                descriptor = device.allocateOriginalDescriptorSet(descriptorArena, set);
                if (!descriptor) {
                    const auto id = device.lastOriginalResourceRegistrationDiagnostic();
                    std::fprintf(stderr,
                        "indexed descriptor set registration: %.*s\n",
                        static_cast<int>(id.size()),
                        id.data());
                }
                assert(descriptor);
            }
            rhi::OriginalWholeDescriptorBufferWrite write;
            write.write.binding =
                pipeline->originalLayoutOwner()->desc().descriptorLayouts[0u].bindings[0u].binding;
            write.buffer = bufferLease;
            write.write.type = rhi::DescriptorType::ReadOnlyStorageBuffer;
            write.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
            write.write.resource = bufferLease->resource();
            write.write.resourceState = rhi::ResourceState::ShaderRead;

            struct Context {
                rhi::GpuDevice* device;
            };

            Context context{&device};
            // Logical slot3 is distinct from the target's physical mapping.
            // A write to absent logical slot0 must not mutate descriptor content.
            for (auto& quota : descriptorQuotas)
                quota = core::makeShared<core::CpuMemoryQuota>(bytes);
            using Whole = rhi::OriginalWholeDescriptorWriteContract;
            const rhi::OriginalWholeDescriptorWriteRequest request{1u,
                true,
                1u,
                static_cast<std::uint32_t>(Whole::ownerSlots(1u)),
                Whole::callbackCount,
                {bytes, bytes, bytes}};
            auto wrongBinding = write;
            wrongBinding.write.binding = 0u;
            const auto revision = descriptor->contentRevision();
            const auto native = rhi_webgpu::webGpuNativePayloadFor(*descriptor);
            is(device.beginOriginalWholeDescriptorWrite(
                   *descriptor, pipeline, 0u, &wrongBinding, 1u, request, descriptorQuotas),
                "RHI-ORIGINAL-WHOLE-WRITE-BINDING");
            assert(descriptor->contentRevision() == revision
                && rhi_webgpu::webGpuNativePayloadFor(*descriptor) == native);
            for (const auto& quota : descriptorQuotas)
                assert(!quota->liveBytes());
            OriginalWholeDescriptorWriteFixture::commitActual(
                context, pipeline, *descriptor, 0u, std::array{write}, descriptorQuotas);
            rhi::OriginalCommandDescriptorSetSource::Element element;
            element.write = write.write;
            element.sourceIndex = 2u;
            element.registrationGeneration =
                bufferLease->originalObject()->originalResourceRegistration().generation();
            element.bufferAccessBytes = 16u;
            element.stages = rhi::PipelineStage::FragmentShader;
            element.access = rhi::ResourceAccess::ShaderRead;
            descriptorSource = rhi::OriginalCommandDescriptorSetSource::prepareOriginal(descriptor,
                descriptorArena,
                root,
                &element,
                1u,
                {2u,
                    true,
                    1u,
                    1u,
                    rhi::OriginalCommandDescriptorSetSource::canonicalExclusiveRetainedOwnerCount,
                    bytes},
                source,
                device,
                diagnostic);
            if (!descriptorSource)
                std::fprintf(stderr, "indexed descriptor Source2: %s\n", diagnostic);
            assert(descriptorSource);
            assert(!Sources::appendOriginalTyped(root, {descriptorSource}, device));
        }
        {
            core::CpuMemoryQuotaScope scope(resources);
            rhi::DescriptorSetDesc extra;
            extra.layout = rhi::DescriptorStateCopy::copy(
                first->originalLayoutOwner()->desc().descriptorLayouts[0u]);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto live = resources->liveBytes();
            const auto stats = descriptorArena->stats();
            assert(!device.allocateOriginalDescriptorSet(descriptorArena, extra));
            is(device.lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
            assert(resources->liveBytes() == live
                && core::defaultCpuAllocator().stats().allocationCount == allocations
                && descriptorArena->stats().allocatedSets == stats.allocatedSets
                && descriptorArena->stats().allocatedDescriptors == stats.allocatedDescriptors);
            std::puts(
                "BINDLESS_RHI_GEOMETRY_DESCRIPTOR_INVENTORY_OK registered-arena1 sets2 original-pipelines2 exact-authored-count extra-arena-set-refused allocation0 quotas-unchanged");
        }
        return owners;
    }
#endif

    template <class Publish>
    static void verifyGeometryGraphics(rhi::GpuDevice& device,
        Publish&& publish,
        const std::shared_ptr<core::CpuMemoryQuota>& resources,
        bool readable = true)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using A = rhi::OriginalCommandGraphicsPipelineSource;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Binding = rhi::OriginalCommandGeometryBinding;
        auto source = core::makeShared<core::CpuMemoryQuota>(bytes),
             graph = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* diagnostic = nullptr;
        auto attachments = attachmentSources(device, source, resources);
        rhi::BufferDesc desc;
        desc.sizeBytes = 16u;
        desc.usage = rhi::ResourceUsage::IndexBuffer | rhi::ResourceUsage::CopyDst
            | rhi::ResourceUsage::StorageBuffer;
        desc.initialState = readable ? rhi::ResourceState::ShaderRead : rhi::ResourceState::CopyDst;
        core::UniquePtr<rhi::GpuBuffer> buffer;
        {
            core::CpuMemoryQuotaScope scope(resources);
            buffer = device.createBuffer(desc);
        }
        assert(buffer);
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource> initialized;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> initializationQuotas;
        std::shared_ptr<const rhi::OriginalCommandBufferLease> bufferLease;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (readable) {
            buffer.reset();
            desc.usage = desc.usage | rhi::ResourceUsage::CopySrc;
            desc.memory = rhi::MemoryUsage::CpuToGpu;
            {
                core::CpuMemoryQuotaScope scope(resources);
                buffer = device.createBuffer(desc);
            }
            assert(buffer);
            for (auto& quota : initializationQuotas)
                quota = core::makeShared<core::CpuMemoryQuota>(bytes);
            const std::array<std::uint8_t, 16> payload{
                0u, 0u, 1u, 0u, 2u, 0u, 3u, 0u, 4u, 0u, 5u, 0u, 6u, 0u, 7u, 0u};
            auto loss = device.observeOriginalDeviceNativeLoss(diagnostic);
            assert(loss && !diagnostic);
            using Content = rhi::OriginalCommandInitializedBufferStagingContract;
            Content::Budget upload{2u,
                true,
                1u,
                Content::callbackCount,
                Content::retainedOwnerBound,
                16u,
                bytes,
                bytes,
                bytes,
                bytes,
                bytes};
            upload.indexContent = {1u,
                true,
                rhi::IndexFormat::Uint16,
                core::UnsignedElementSpan::ByteOrder::LittleEndian,
                8u};
            // Storage usage is independently authorized by the immutable
            // readonly intent; the old index-only profile must still refuse.
            const auto* original = buffer.get();
            assert(!rhi_webgpu::WebGpuCanonicalBufferUploadSource::prepareOriginal(device,
                buffer,
                *loss,
                payload.data(),
                payload.size(),
                99u,
                upload,
                {initializationQuotas[0],
                    initializationQuotas[1],
                    resources,
                    source,
                    initializationQuotas[2]},
                diagnostic));
            is(diagnostic, "RHI-INITIALIZED-CANONICAL-STAGING-LAYOUT");
            assert(buffer.get() == original && !buffer->hostWritesSealed());
            upload.indexContent.shaderReadContractVersion = 1u;
            upload.indexContent.shaderReadOptIn = true;
            initialized = rhi_webgpu::WebGpuCanonicalBufferUploadSource::prepareOriginal(device,
                buffer,
                *loss,
                payload.data(),
                payload.size(),
                99u,
                upload,
                {initializationQuotas[0],
                    initializationQuotas[1],
                    resources,
                    source,
                    initializationQuotas[2]},
                diagnostic);
            if (!initialized)
                std::fprintf(stderr, "indexed readonly initialization: %s\n", diagnostic);
            assert(initialized && !diagnostic);
            assert(!rhi_webgpu::WebGpuCanonicalBufferUploadSource::initialize(initialized));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return initialized->callbacksDrained();
            }));
            assert(!initialized->readyDiagnostic());
            bufferLease = initialized->originalLease();
            assert(bufferLease->originalObject()->hostWritesSealed()
                && bufferLease->originalObject()->gpuWritesSealed()
                && bufferLease->originalObject()->gpuTransferWritesSealed());
        } else
#endif
        {
            bufferLease = rhi::OriginalCommandBufferLease::prepareOriginal(
                buffer, {1u, true, 2u, bytes}, source, diagnostic);
            assert(bufferLease);
        }
        std::array<Sources::Source, 3u> prefix{{attachments[0], attachments[1], bufferLease}};
        auto first = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(publish(), diagnostic);
        auto second = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(publish(), diagnostic);
        assert(first && second);
        const auto adapter = [&](const auto& p) {
            return A::prepareOriginal(p,
                {1u,
                    true,
                    A::kind,
                    p->originalResourceRegistration().generation(),
                    bytes,
                    A::exclusiveOwnerCount},
                source,
                device,
                diagnostic);
        };
        auto a = adapter(first), a2 = adapter(second);
        assert(a && a2);
        const auto budget = indexedSourceBudget(readable);
        // Source assembly reserves every declared typed owner: two pipeline
        // sources plus the two descriptor sources, before any append occurs.
        const auto typedSources =
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)]
            + budget.maxSourceOwners[static_cast<std::size_t>(
                rhi::CommandReferenceDomain::Descriptor)];
        assert(typedSources == 4u);
        const auto sourceBytes = source->liveBytes();
        assert(!Sources::prepareColdOriginalTyped(prefix.data(),
            prefix.size(),
            budget,
            {2u, true, typedSources - 1u},
            source,
            diagnostic));
        is(diagnostic, "RHI-ORIGINAL-SOURCES-ASSEMBLY-CAPACITY");
        assert(source->liveBytes() == sourceBytes);
        auto root = Sources::prepareColdOriginalTyped(
            prefix.data(), prefix.size(), budget, {2u, true, typedSources}, source, diagnostic);
        if (!root)
            std::fprintf(stderr, "indexed original source assembly: %s\n", diagnostic);
        assert(root);
        assert(!Sources::appendOriginalTyped(root, {a}, device)
            && !Sources::appendOriginalTyped(root, {a2}, device));
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        IndexedDescriptorOwners descriptorOwners;
        if (readable)
            descriptorOwners = prepareIndexedDescriptors(
                device, root, bufferLease, first, second, source, resources);
#endif
        const std::array<rhi::CommandUsage, 11u> commands{{{K::BeginRenderPass, 0u, 1u},
            {K::BindGraphicsPipeline, 1u, 1u},
            {K::BindIndexBuffer, 2u, 1u},
            {K::BindGraphicsPipeline, 3u, 1u},
            {K::BindIndexBuffer, 4u, 1u},
            {K::EndRenderPass, 5u, 0u},
            {K::BeginRenderPass, 5u, 1u},
            {K::BindGraphicsPipeline, 6u, 1u},
            {K::BindIndexBuffer, 7u, 1u},
            {K::EndRenderPass, 8u, 0u},
            {K::Finish, 8u, 0u}}};
        const std::array<Usage::Reference, 8u> refs{
            {{1u}, {3u}, {2u}, {4u}, {2u}, {1u}, {3u}, {2u}}};
        auto usage = Usage::prepareOriginal(
            root, commands.data(), commands.size(), refs.data(), refs.size(), graph, diagnostic);
        assert(usage);
        const auto field = [&](std::size_t i) {
            return rhi::CommandVariableArguments::Input{commands[i].kind == K::BindIndexBuffer
                    ? *Scalar::capture(K::BindIndexBuffer,
                          Scalar::IndexBinding{rhi::IndexFormat::Uint16, i == 8u ? 14u : 0u})
                    : *Scalar::capture(commands[i].kind, Scalar::None{}),
                {}};
        };
        const std::array<rhi::CommandVariableArguments::Input, 11u> fields{{field(0u),
            field(1u),
            field(2u),
            field(3u),
            field(4u),
            field(5u),
            field(6u),
            field(7u),
            field(8u),
            field(9u),
            field(10u)}};
        auto arguments = rhi::CommandVariableArguments::prepareOriginal(
            fields.data(), fields.size(), {1u, true, 11u, 0u, 0u, 0u, bytes}, graph, diagnostic);
        assert(arguments);
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, graph, diagnostic);
        assert(scalars);
        auto pass = passArguments(*root, graph);
        const auto g1 = first->originalResourceRegistration().generation(),
                   g2 = second->originalResourceRegistration().generation();
        const auto gb = bufferLease->originalObject()->originalResourceRegistration().generation();
        const Inputs::OriginalDescriptor empty{SIZE_MAX, rhi::OriginalCommandPipelineBinding{}};
        std::array<Inputs::OriginalDescriptor, 8u> records{
            {empty, empty, empty, empty, empty, empty, empty, empty}};
        records[0] = {0u, pass};
        records[1] = {1u, rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline, 3u, 0u, g1}};
        records[2] = {2u, Binding{K::BindIndexBuffer, 2u, 3u, 1u, 0u, gb, g1}};
        records[3] = {3u, rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline, 4u, 0u, g2}};
        records[4] = {4u, Binding{K::BindIndexBuffer, 2u, 4u, 3u, 0u, gb, g2}};
        records[5] = {6u, pass};
        records[6] = {7u, rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline, 3u, 6u, g1}};
        records[7] = {8u, Binding{K::BindIndexBuffer, 2u, 3u, 7u, 6u, gb, g1}};
        rhi::OriginalCommandInputsBudget b{5u, true, 3u};
        b.maxIndexBindings = 3u;
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, records.data(), records.size(), b, graph, diagnostic);
        };
        const auto before = graph->liveBytes();
        auto saved = records;
        const auto reject = [&](const char* id) {
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            auto rejected = factory();
            if (rejected)
                std::fprintf(stderr, "geometry Inputs unexpectedly accepted negative: %s\n", id);
            assert(!rejected);
            is(diagnostic, id);
            assert(graph->liveBytes() == before);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
            records = saved;
        };
        ++std::get<Binding>(records[2].arguments).registrationGeneration;
        reject("RHI-ORIGINAL-GEOMETRY-GENERATION");
        ++std::get<Binding>(records[2].arguments).pipelineRegistrationGeneration;
        reject("RHI-ORIGINAL-GEOMETRY-GENERATION");
        std::get<Binding>(records[2].arguments).sourceIndex = 0u;
        reject("RHI-ORIGINAL-GEOMETRY-SOURCE");
        std::get<Binding>(records[2].arguments).pipelineSourceIndex = 4u;
        reject("RHI-ORIGINAL-GEOMETRY-PIPELINE-EPOCH");
        std::get<Binding>(records[4].arguments).pipelineBindCommandIndex = 1u;
        reject("RHI-ORIGINAL-GEOMETRY-PIPELINE-EPOCH");
        std::get<Binding>(records[7].arguments).scopeBeginCommandIndex = 0u;
        reject("RHI-ORIGINAL-GEOMETRY-SCOPE");
        records[2].commandIndex = 4u;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        const auto authored = b;
        b.maxIndexBindings = 2u;
        reject("RHI-ORIGINAL-GEOMETRY-BINDING-CAPACITY");
        b = authored;
        // The parent inventory also admits the joined descriptor/draw fixture.
        // Its actual capacity is authoritative for exact/one-over negatives.
        const auto geometryCapacity = root->originalBudget().maxInputDescriptors;
        assert(geometryCapacity < UINT32_MAX);
        b.maxIndexBindings = geometryCapacity;
        {
            auto exactCapacity = factory();
            assert(exactCapacity);
        }
        assert(graph->liveBytes() == before);
        b.maxIndexBindings = geometryCapacity + 1u;
        reject("RHI-ORIGINAL-GEOMETRY-GRAPH-CAPACITY");
        b = authored;
        b.maxVertexBindings = geometryCapacity;
        {
            auto exactCapacity = factory();
            assert(exactCapacity);
        }
        assert(graph->liveBytes() == before);
        b.maxVertexBindings = geometryCapacity + 1u;
        reject("RHI-ORIGINAL-GEOMETRY-GRAPH-CAPACITY");
        b = authored;
        b = {2u, true, 3u};
        reject("RHI-ORIGINAL-GEOMETRY-POSITIONAL-PROFILE");
        b = authored;
        assert(!Inputs::prepareOriginal(
            scalars, records.data(), records.size() - 1u, b, graph, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-INPUTS-ORDER");
        CpuQuotaFactoryFixture pressure(graph);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        auto inputs = factory();
        assert(inputs && inputs->retainedOwnerCount() == scalars->retainedOwnerCount() + 2u);
        using G = rhi::OriginalCommandGeometryBindingContract;
        using S = rhi::semantics::TypedShaderUsageScopeContract;
        G::AccessBudget accessBudget{1u, true, 11u, 3u, 3u};
        std::array<S::BufferUse, 3u> accesses{};
        std::size_t emitted = 0u;
        const auto capture = [&](const G::Access& access) noexcept -> const char* {
            assert(emitted < accesses.size() && access.resource == bufferLease->resource()
                && access.binding.registrationGeneration == gb && !access.nativeExecutionProven());
            assert(access.bufferBytes == 16u
                && access.offsetBytes == (access.commandIndex == 8u ? 14u : 0u)
                && access.sizeBytes == 16u - access.offsetBytes);
            assert(access.binding.scopeBeginCommandIndex == (access.commandIndex == 8u ? 6u : 0u)
                && access.scopeEndCommandIndex == (access.commandIndex == 8u ? 9u : 5u));
            S::BufferUse out;
            out.resource = access.resource;
            out.bufferBytes = access.bufferBytes;
            out.offsetBytes = access.offsetBytes;
            out.sizeBytes = access.sizeBytes;
            out.commandIndex = access.commandIndex;
            out.scope = {S::Kind::RenderPass,
                access.binding.scopeBeginCommandIndex,
                access.scopeEndCommandIndex};
            out.contribution = S::Contribution::GeometryBinding;
            out.usage = S::BufferUsage::Index;
            out.access = access.scope;
            accesses[emitted++] = out;
            return nullptr;
        };
        auto projected = inputs->visitGeometryAccesses(accessBudget, capture);
        assert(!projected.diagnostic && projected.bindings == 3u && projected.pairChecks == 3u
            && emitted == 3u);

        struct Operation {
            std::size_t commandIndex;
            K kind;
        };

        std::array<Operation, 11u> operations{};
        for (std::size_t i = 0u; i < operations.size(); ++i)
            operations[i] = {i, commands[i].kind};
        const S::Budget scopes{4u, true, 11u, 3u, 3u};
        assert(!S::inspect(operations, accesses, scopes).diagnostic);
        const auto rejectAccess = [&](G::AccessBudget b, const char* id) {
            std::size_t calls = 0u;
            is(inputs
                    ->visitGeometryAccesses(b,
                        [&](const auto&) noexcept -> const char* {
                            ++calls;
                            return nullptr;
                        })
                    .diagnostic,
                id);
            assert(!calls);
        };
        auto lessAccess = accessBudget;
        --lessAccess.maxPairChecks;
        rejectAccess(lessAccess, "RHI-ORIGINAL-GEOMETRY-ACCESS-PAIR-CAPACITY");
        lessAccess = accessBudget;
        --lessAccess.maxBindings;
        rejectAccess(lessAccess, "RHI-ORIGINAL-GEOMETRY-ACCESS-BINDING-CAPACITY");
        lessAccess = accessBudget;
        --lessAccess.maxCommands;
        rejectAccess(lessAccess, "RHI-ORIGINAL-GEOMETRY-ACCESS-COMMAND-CAPACITY");
        lessAccess = accessBudget;
        lessAccess.prototypeOptIn = false;
        rejectAccess(lessAccess, "RHI-ORIGINAL-GEOMETRY-ACCESS-OPT-IN");
        lessAccess = accessBudget;
        lessAccess.contractVersion = 2u;
        rejectAccess(lessAccess, "RHI-ORIGINAL-GEOMETRY-ACCESS-VERSION");
        lessAccess = accessBudget;
        ++lessAccess.maxBindings;
        rejectAccess(lessAccess, "RHI-ORIGINAL-GEOMETRY-ACCESS-INPUT-CAPACITY");
        verifyGeometryStateStream(inputs, device, accessBudget, readable, initialized);
        if (!readable)
            verifyGeometryDependencies(root, device, pass);
        const Inputs::IncomingReference incomingBuffer{bufferLease->originalObject()};
        const Inputs::Incoming incoming{fields[2], {}, &incomingBuffer, 1u};
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{source, graph, resources}, [&]() noexcept {
                if (const auto* id = inputs->validate())
                    return id;
                if (const auto* id = inputs
                        ->visitGeometryAccesses(accessBudget,
                            [](const auto&) noexcept -> const char* {
                                return nullptr;
                            })
                        .diagnostic)
                    return id;
                return inputs->compareOriginalInput(2u, incoming);
            }));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
            assert(inputs->executionDiagnostic(backend) && !inputs->nativeExecutionProven());
        std::weak_ptr<const Sources> observer = root;
        root.reset();
        usage.reset();
        scalars.reset();
        arguments.reset();
        pass.reset();
        records.fill(empty);
        saved.fill(empty);
        prefix = {};
        attachments = {};
        bufferLease.reset();
        first.reset();
        second.reset();
        a.reset();
        a2.reset();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        descriptorOwners.sources = {};
        descriptorOwners.arena.reset();
#endif
        assert(!observer.expired() && !inputs->validate());
        assert(!inputs
                ->visitGeometryAccesses(accessBudget,
                    [](const auto&) noexcept -> const char* {
                        return nullptr;
                    })
                .diagnostic);
        inputs.reset();
        assert(observer.expired());
        observer.reset();
        assert(!source->liveBytes() && !graph->liveBytes());
        for (const auto& quota : initializationQuotas)
            if (quota)
                assert(!quota->liveBytes());
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        for (const auto& quotas : descriptorOwners.quotas)
            for (const auto& quota : quotas)
                if (quota)
                    assert(!quota->liveBytes());
#endif
        std::printf(
            "BINDLESS_RHI_GEOMETRY_POSITIONAL_OK inputs5 index3 exact-original-buffer pipeline-rebind-pass-epochs generation missing-reordered-quota hot0 caller-drop state-content-native-closed quota%llu\n",
            static_cast<unsigned long long>(peak));
        std::puts(
            "BINDLESS_RHI_GEOMETRY_ACCESS_OK inputs5 scope4 index3 bounded-pairs full-bound-tail original-owner-generations no-output-on-refusal hot0 native-closed");
    }

    template <class Pipeline>
    static auto verifyActual(const std::shared_ptr<const rhi::OriginalCommandResourceSources>& root,
        const std::shared_ptr<core::CpuMemoryQuota>& graph,
        const rhi::GpuDevice& device)
    {
        using Usage = rhi::OriginalCommandUsage;
        using A = rhi::OriginalCommandPipelineSource<Pipeline>;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        constexpr bool graphics = std::is_same_v<Pipeline, rhi::RenderPipeline>;
        constexpr auto begin = graphics ? K::BeginRenderPass : K::BeginComputePass;
        constexpr auto bind = graphics ? K::BindGraphicsPipeline : K::BindComputePipeline;
        constexpr auto end = graphics ? K::EndRenderPass : K::EndComputePass;
        const std::uint32_t firstSource = graphics ? 2u : 1u, secondSource = firstSource + 1u;
        constexpr std::uint32_t attachment = graphics ? 1u : 0u,
                                totalReferences = 3u + 2u * attachment;
        const std::array<rhi::CommandUsage, 8u> commands{{{begin, 0u, attachment},
            {bind, attachment, 1u},
            {bind, attachment + 1u, 1u},
            {end, attachment + 2u, 0u},
            {begin, attachment + 2u, attachment},
            {bind, 2u * attachment + 2u, 1u},
            {end, totalReferences, 0u},
            {K::Finish, totalReferences, 0u}}};
        std::array<Usage::Reference, totalReferences> refs{};
        if constexpr (graphics)
            refs = {{{1u}, {firstSource}, {secondSource}, {1u}, {firstSource}}};
        else
            refs = {{{firstSource}, {secondSource}, {firstSource}}};
        const char* diagnostic = nullptr;
        auto usage = Usage::prepareOriginal(
            root, commands.data(), commands.size(), refs.data(), refs.size(), graph, diagnostic);
        assert(usage);
        const auto field = [&](std::size_t i) {
            return rhi::CommandVariableArguments::Input{
                *Scalar::capture(commands[i].kind, Scalar::None{}), {}};
        };
        const std::array<rhi::CommandVariableArguments::Input, 8u> fields{{field(0u),
            field(1u),
            field(2u),
            field(3u),
            field(4u),
            field(5u),
            field(6u),
            field(7u)}};
        auto arguments = rhi::CommandVariableArguments::prepareOriginal(
            fields.data(), fields.size(), {1u, true, 8u, 0u, 0u, 0u, bytes}, graph, diagnostic);
        assert(arguments);
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, graph, diagnostic);
        assert(scalars);
        const auto& a = std::get<std::shared_ptr<const A>>(root->sources()[firstSource]);
        const auto& a2 = std::get<std::shared_ptr<const A>>(root->sources()[secondSource]);
        const auto generation = a->originalObject()->originalResourceRegistration().generation();
        const auto generation2 = a2->originalObject()->originalResourceRegistration().generation();
        const Inputs::OriginalDescriptor empty{SIZE_MAX, rhi::OriginalCommandPipelineBinding{}};
        std::array<Inputs::OriginalDescriptor, 5u> descriptors{{empty, empty, empty, empty, empty}};
        std::size_t count = 0u;
        std::shared_ptr<const rhi::RenderPassArguments> pass;
        if constexpr (graphics) {
            pass = passArguments(*root, graph);
            descriptors[count++] = {0u, pass};
        }
        descriptors[count++] = {
            1u, rhi::OriginalCommandPipelineBinding{bind, firstSource, 0u, generation}};
        descriptors[count++] = {
            2u, rhi::OriginalCommandPipelineBinding{bind, secondSource, 0u, generation2}};
        if constexpr (graphics)
            descriptors[count++] = {4u, pass};
        descriptors[count++] = {
            5u, rhi::OriginalCommandPipelineBinding{bind, firstSource, 4u, generation}};
        const rhi::OriginalCommandInputsBudget budget{2u, true, 3u};
        const auto before = graph->liveBytes();
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, descriptors.data(), count, budget, graph, diagnostic);
        };
        auto reject = [&](const char* id) {
            assert(!factory());
            is(diagnostic, id);
            assert(graph->liveBytes() == before);
        };
        for (const auto invalid : std::array<rhi::OriginalCommandInputsBudget, 3u>{
                 {{4u, true, 3u}, {2u, false, 3u}, {2u, true, 0u}}}) {
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Inputs::prepareOriginal(
                scalars, descriptors.data(), count, invalid, graph, diagnostic));
            assert(diagnostic && graph->liveBytes() == before
                && allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        // /1 reaches the same genuine fields/references and keeps its old gate.
        assert(!Inputs::prepareOriginal(scalars, descriptors.data(), count, graph, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING");
        auto less = budget;
        --less.maxPipelineBindings;
        assert(
            !Inputs::prepareOriginal(scalars, descriptors.data(), count, less, graph, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-CAPACITY");
        const auto index = graphics ? 1u : 0u;
        const auto saved = descriptors[index];
        auto& binding = std::get<rhi::OriginalCommandPipelineBinding>(descriptors[index].arguments);
        ++binding.registrationGeneration;
        reject("RHI-ORIGINAL-TYPED-INPUT-PIPELINE-GENERATION");
        descriptors[index] = saved;
        std::get<rhi::OriginalCommandPipelineBinding>(descriptors[index].arguments).sourceIndex =
            secondSource;
        reject("RHI-ORIGINAL-TYPED-INPUT-PIPELINE-SOURCE");
        descriptors[index] = saved;
        std::get<rhi::OriginalCommandPipelineBinding>(descriptors[index].arguments)
            .scopeBeginCommandIndex = 4u;
        reject("RHI-ORIGINAL-TYPED-INPUT-PIPELINE-SCOPE");
        descriptors[index] = saved;
        std::get<rhi::OriginalCommandPipelineBinding>(descriptors[index].arguments).commandKind =
            graphics ? K::BindComputePipeline : K::BindGraphicsPipeline;
        reject("RHI-ORIGINAL-INPUTS-KIND");
        descriptors[index] = saved;
        descriptors[index].commandIndex = 2u;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        descriptors[index] = saved;
        --count;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        ++count;
        CpuQuotaFactoryFixture pressure(graph);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(graph, bytes - graph->liveBytes());
            assert(full.charged() && !factory());
            is(diagnostic, "RHI-ORIGINAL-INPUTS-ALLOCATION");
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto inputs = factory();
        std::pmr::set_default_resource(previous);
        assert(inputs);
        assert(inputs->retainedOwnerCount()
            == scalars->retainedOwnerCount() + 1u + (graphics ? 1u : 0u));
        assert(inputs->originalBudget().contractVersion == 2u
            && inputs->descriptors().size() == count);
        const Inputs::IncomingReference first{a->originalObject()}, second{a2->originalObject()};
        Inputs::Incoming incoming{fields[1u], {}, &first, 1u};
        assert(!inputs->compareOriginalInput(1u, incoming));
        incoming.references = &second;
        is(inputs->compareOriginalInput(1u, incoming), "RHI-ORIGINAL-USAGE-REFERENCE-OBJECT");
        incoming.references = &first;
        incoming.scalar = fields[3u];
        is(inputs->compareOriginalInput(1u, incoming), "RHI-ORIGINAL-SCALARS-INPUT");
        incoming.scalar = fields[1u];
        std::get<rhi::OriginalCommandPipelineBinding>(descriptors[index].arguments)
            .registrationGeneration = 0u;
        assert(!inputs->validate() && !inputs->compareOriginalInput(1u, incoming));
        descriptors[index] = saved;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{graph}, [&]() noexcept {
            return inputs->compareOriginalInput(1u, incoming);
        }));
        // Check structural pass reset with genuine immutable sources, through
        // the same cold factory; no second replay plan/root is materialized.
        auto invalidCommands = commands;
        if constexpr (graphics)
            invalidCommands[5u].kind = K::BindComputePipeline;
        else
            invalidCommands[4u].kind = K::InsertDebugMarker;
        auto wrongUsage = Usage::prepareOriginal(root,
            invalidCommands.data(),
            invalidCommands.size(),
            refs.data(),
            refs.size(),
            graph,
            diagnostic);
        assert(!wrongUsage);
        is(diagnostic, "RHI-COMMAND-ORDER-SCOPE");
        // Reassemble genuine Sources only for negative authored-budget probes.
        // Their indices/owners are unchanged; no alternate root is published.
        for (const bool ownerCapacity : std::array{false, true}) {
            auto authored = root->originalBudget();
            if (ownerCapacity)
                --authored.maxGraphOwners;
            else
                authored.maxInputDescriptors = static_cast<std::uint32_t>(count - 1u);
            auto limited = rhi::OriginalCommandResourceSources::prepareColdOriginalTyped(
                root->sources().data(),
                firstSource,
                authored,
                {2u, true, 2u},
                root->originalMetadataQuota(),
                diagnostic);
            assert(limited);
            assert(!rhi::OriginalCommandResourceSources::appendOriginalTyped(limited, {a}, device));
            assert(
                !rhi::OriginalCommandResourceSources::appendOriginalTyped(limited, {a2}, device));
            auto limitedUsage = Usage::prepareOriginal(limited,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                graph,
                diagnostic);
            assert(limitedUsage);
            auto limitedScalars = rhi::OriginalCommandScalars::prepareOriginal(
                limitedUsage, arguments, graph, diagnostic);
            assert(limitedScalars);
            assert(!Inputs::prepareOriginal(
                limitedScalars, descriptors.data(), count, budget, graph, diagnostic));
            is(diagnostic,
                ownerCapacity ? "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY"
                              : "RHI-ORIGINAL-INPUTS-CAPACITY");
        }
        std::printf(
            "BINDLESS_RHI_PIPELINE_POSITIONAL_NATIVE_OK %s bytes%llu commands8 rebind3 scopes2 exact-generation-source pass-reset pressure-retry hot0 descriptors-state-native-closed\n",
            graphics ? "graphics" : "compute",
            static_cast<unsigned long long>(peak));
        return inputs;
    }

    template <class Pipeline>
    static auto retainForLoss(
        const std::shared_ptr<const rhi::OriginalCommandPipelineSource<Pipeline>>& adapter,
        rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& source,
        const std::shared_ptr<core::CpuMemoryQuota>& graph,
        const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Usage = rhi::OriginalCommandUsage;
        using Sources = rhi::OriginalCommandResourceSources;
        using A = rhi::OriginalCommandPipelineSource<Pipeline>;
        constexpr bool graphics = std::is_same_v<Pipeline, rhi::RenderPipeline>;
        constexpr auto begin = graphics ? K::BeginRenderPass : K::BeginComputePass,
                       bind = graphics ? K::BindGraphicsPipeline : K::BindComputePipeline,
                       end = graphics ? K::EndRenderPass : K::EndComputePass;
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = 4u;
        admission.maxReferences = graphics ? 2u : 1u;
        admission.maxInputDescriptors = graphics ? 2u : 1u;
        admission.maxGraphOwners = A::exclusiveOwnerCount + 5u + (graphics ? 6u : 0u);
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            1u;
        admission.maxCpuBytes.fill(bytes);
        std::array<Sources::Source, 2u> prefix{};
        if constexpr (graphics) {
            prefix = attachmentSources(device, source, resources);
            admission
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
                1u;
            admission.maxSourceOwners[static_cast<std::size_t>(
                rhi::CommandReferenceDomain::TextureView)] = 1u;
            admission.maxPassColorAttachments = 1u;
            admission.maxPassAttachmentReferences = 1u;
        }
        const char* diagnostic = nullptr;
        auto root = Sources::prepareColdOriginalTyped(
            prefix.data(), graphics ? 2u : 0u, admission, {2u, true, 1u}, source, diagnostic);
        assert(root);
        assert(!Sources::appendOriginalTyped(root, Sources::Source{adapter}, device));
        constexpr std::uint32_t attachment = graphics ? 1u : 0u, totalReferences = attachment + 1u;
        const std::array<rhi::CommandUsage, 4u> commands{{{begin, 0u, attachment},
            {bind, attachment, 1u},
            {end, totalReferences, 0u},
            {K::Finish, totalReferences, 0u}}};
        std::array<Usage::Reference, totalReferences> refs{};
        if constexpr (graphics)
            refs = {{{1u}, {2u}}};
        else
            refs = {{{0u}}};
        auto usage = Usage::prepareOriginal(
            root, commands.data(), commands.size(), refs.data(), refs.size(), graph, diagnostic);
        assert(usage);
        const auto field = [&](std::size_t i) {
            return rhi::CommandVariableArguments::Input{
                *Scalar::capture(commands[i].kind, Scalar::None{}), {}};
        };
        const std::array<rhi::CommandVariableArguments::Input, 4u> fields{
            {field(0u), field(1u), field(2u), field(3u)}};
        auto arguments = rhi::CommandVariableArguments::prepareOriginal(
            fields.data(), fields.size(), {1u, true, 4u, 0u, 0u, 0u, bytes}, graph, diagnostic);
        assert(arguments);
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, graph, diagnostic);
        assert(scalars);
        const Inputs::OriginalDescriptor binding{1u,
            rhi::OriginalCommandPipelineBinding{bind,
                graphics ? 2u : 0u,
                0u,
                adapter->originalObject()->originalResourceRegistration().generation()}};
        auto result = [&] {
            if constexpr (graphics) {
                auto pass = passArguments(*root, graph);
                const std::array<Inputs::OriginalDescriptor, 2u> descriptors{{{0u, pass}, binding}};
                return Inputs::prepareOriginal(scalars,
                    descriptors.data(),
                    descriptors.size(),
                    {2u, true, 1u},
                    graph,
                    diagnostic);
            } else
                return Inputs::prepareOriginal(
                    scalars, &binding, 1u, {2u, true, 1u}, graph, diagnostic);
        }();
        assert(result && !result->validate());
        return result;
    }
};
} // namespace arp::test
