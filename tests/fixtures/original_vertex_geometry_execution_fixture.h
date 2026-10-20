#pragma once
#include "original_command_pipeline_binding_fixture.h"
#include "original_typed_cold_command_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h"
#include "texture_readback_source_lease_fixture.h"
#include "webgpu_native_map_execution_probe_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
// Exercise the already approved immutable geometry vocabulary on the original
// Sources/Inputs/State/Stream/root/Closure6/Freeze4/Payload4/Cold4 chain.
// The composed fixture consumes Join3/Closure7/Freeze5 and explicit Execution5.
// Neither fixture grants public execution authority.
class OriginalVertexGeometryExecutionFixture final {
    using K = rhi::CommandUsageKind;
    static constexpr std::uint64_t bytes = 65536u;

public:
    enum class LifecycleCase {
        Complete,
        RecordingCancel,
        SubmissionLoss,
        MapLoss,
        RecordingUncertain,
        SubmissionUncertain,
        MapUncertain,
        RecordingScopeUncertain,
        SubmissionScopeUncertain,
        QueueRegistrationUncertain,
        ScopeRegistrationUncertain
    };

    static const char* lifecycleName(LifecycleCase mode) noexcept
    {
        switch (mode) {
        case LifecycleCase::Complete:
            return "complete";
        case LifecycleCase::RecordingCancel:
            return "recording-cancel";
        case LifecycleCase::SubmissionLoss:
            return "submission-loss";
        case LifecycleCase::MapLoss:
            return "map-loss";
        case LifecycleCase::RecordingUncertain:
            return "recording-uncertain";
        case LifecycleCase::SubmissionUncertain:
            return "submission-uncertain";
        case LifecycleCase::MapUncertain:
            return "map-uncertain";
        case LifecycleCase::RecordingScopeUncertain:
            return "recording-scope-uncertain";
        case LifecycleCase::SubmissionScopeUncertain:
            return "submission-scope-uncertain";
        case LifecycleCase::QueueRegistrationUncertain:
            return "queue-registration-uncertain";
        case LifecycleCase::ScopeRegistrationUncertain:
            return "scope-registration-uncertain";
        }
        assert(false);
        return nullptr;
    }

    static constexpr auto commands() noexcept
    {
        return std::array<rhi::CommandUsage, 14u>{{{K::BufferMemoryBarrier, 0u, 1u},
            {K::BufferMemoryBarrier, 1u, 1u},
            {K::BufferMemoryBarrier, 2u, 1u},
            {K::BeginRenderPass, 3u, 1u},
            {K::BindGraphicsPipeline, 4u, 1u},
            {K::SetViewport, 5u, 0u},
            {K::SetScissor, 5u, 0u},
            {K::BindRenderDescriptor, 5u, 1u},
            {K::BindIndexBuffer, 6u, 1u},
            {K::BindVertexBuffer, 7u, 1u},
            {K::BindVertexBuffer, 8u, 1u},
            {K::DrawIndexed, 9u, 0u},
            {K::EndRenderPass, 9u, 0u},
            {K::Finish, 9u, 0u}}};
    }

    static constexpr auto composedCommands() noexcept
    {
        return std::array<rhi::CommandUsage, 19u>{{{K::BufferMemoryBarrier, 0u, 1u},
            {K::BufferMemoryBarrier, 1u, 1u},
            {K::BufferMemoryBarrier, 2u, 1u},
            {K::TextureBarrier, 3u, 1u},
            {K::BeginRenderPass, 4u, 1u},
            {K::BindGraphicsPipeline, 5u, 1u},
            {K::SetViewport, 6u, 0u},
            {K::SetScissor, 6u, 0u},
            {K::BindRenderDescriptor, 6u, 1u},
            {K::BindIndexBuffer, 7u, 1u},
            {K::BindVertexBuffer, 8u, 1u},
            {K::BindVertexBuffer, 9u, 1u},
            {K::DrawIndexed, 10u, 0u},
            {K::EndRenderPass, 10u, 0u},
            {K::TextureBarrier, 10u, 1u},
            {K::BeginTransferPass, 11u, 0u},
            {K::TextureReadback, 11u, 2u},
            {K::EndTransferPass, 13u, 0u},
            {K::Finish, 13u, 0u}}};
    }

    static void verifyContract()
    {
        using Domain = rhi::CommandReferenceDomain;
        constexpr std::array domains{Domain::Buffer,
            Domain::Buffer,
            Domain::Buffer,
            Domain::TextureView,
            Domain::Pipeline,
            Domain::Descriptor,
            Domain::Buffer,
            Domain::Buffer,
            Domain::Buffer};
        auto authored = commands();
        assert(!rhi::CommandUsageSequenceContract::validate(authored.data(),
            authored.size(),
            domains.size(),
            [&](std::size_t i) {
                return domains[i];
            },
            {}));
        assert(
            !rhi::semantics::CommandRecordingOrder::validate(authored.data(), authored.size(), 0u));
        auto wrong = domains;
        wrong[8u] = Domain::TextureView;
        assert(rhi::CommandUsageSequenceContract::validate(authored.data(),
            authored.size(),
            wrong.size(),
            [&](std::size_t i) {
                return wrong[i];
            },
            {}));
        constexpr std::array composedDomains{Domain::Buffer,
            Domain::Buffer,
            Domain::Buffer,
            Domain::Texture,
            Domain::TextureView,
            Domain::Pipeline,
            Domain::Descriptor,
            Domain::Buffer,
            Domain::Buffer,
            Domain::Buffer,
            Domain::Texture,
            Domain::Texture,
            Domain::Readback};
        const auto composed = composedCommands();
        assert(!rhi::CommandUsageSequenceContract::validate(composed.data(),
            composed.size(),
            composedDomains.size(),
            [&](std::size_t i) {
                return composedDomains[i];
            },
            {}));
        assert(
            !rhi::semantics::CommandRecordingOrder::validate(composed.data(), composed.size(), 0u));
        auto wrongReadback = composedDomains;
        wrongReadback.back() = Domain::Texture;
        assert(rhi::CommandUsageSequenceContract::validate(composed.data(),
            composed.size(),
            wrongReadback.size(),
            [&](std::size_t i) {
                return wrongReadback[i];
            },
            {}));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto custody = rhi::semantics::OriginalCommandNativeClosureLowering::
                geometryContentLossBoundLowering(backend);
            assert(custody.resourceClosureAvailable == (backend == rhi::Backend::WebGPU));
            assert(!custody.executionAvailable);
            const auto pending =
                rhi::semantics::OriginalCommandNativeClosureLowering::geometryPendingReadLowering(
                    backend);
            assert(pending.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !pending.executionAvailable);
        }
        std::puts(
            "BINDLESS_RHI_STREAM_GEOMETRY_CONTRACT_OK original14 three-initial-clauses index1 vertex2 indexedDraw1 backends4 no-public-authority");
    }
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)                                \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
public:
    using Source = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
    using Inputs = rhi::OriginalCommandInputs;
    using Sources = rhi::OriginalCommandResourceSources;
    using Upload = rhi::OriginalCommandInitializedBufferStagingContract;

    struct Prepared {
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdAdmission> admission;
        std::array<std::shared_ptr<const Source>, 3u> initialized;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 17u> quotas;
    };

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "original stream geometry: %s\n", id);
        assert(!id);
    }

    enum class IndexPurpose { GeometryOnly, GeometryAndShaderRead };

    static Prepared prepareInitializedStreams(rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& resources,
        IndexPurpose purpose,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota)
    {
        assert(sourceQuota && sourceQuota->limitBytes() == bytes);
        Prepared result;
        result.quotas[0] = sourceQuota;
        for (std::size_t i = 1u; i < result.quotas.size(); ++i)
            result.quotas[i] = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto source = result.quotas[0];
        const char* id = nullptr;
        // Explicit IEEE754 little-endian payload. Vertex triangle0..2 covers
        // the target; instance0 is a red sentinel, instances1/2 green and blue.
        constexpr std::array<std::uint32_t, 16u> vertices{{0xbf800000u,
            0xbf800000u,
            0x40400000u,
            0xbf800000u,
            0xbf800000u,
            0x40400000u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u}};
        constexpr std::array<std::uint32_t, 18u> instances{{0u,
            0u,
            0x3f800000u,
            0u,
            0u,
            0x3f800000u,
            0u,
            0u,
            0u,
            0x3f800000u,
            0u,
            0x3f800000u,
            0u,
            0u,
            0u,
            0u,
            0x3f800000u,
            0x3f800000u}};
        for (std::size_t i = 0u; i < result.initialized.size(); ++i) {
            const std::uint64_t size = i == 0u ? 16u
                : i == 1u                      ? sizeof(vertices)
                                               : sizeof(instances);
            std::array<std::uint8_t, sizeof(instances)> data{};
            if (i == 0u)
                for (std::size_t n = 0u; n < 8u; ++n)
                    data[n * 2u] = static_cast<std::uint8_t>(n);
            else
                for (std::size_t n = 0u; n < size / 4u; ++n) {
                    const auto word = i == 1u ? vertices[n] : instances[n];
                    for (unsigned b = 0u; b < 4u; ++b)
                        data[n * 4u + b] = static_cast<std::uint8_t>(word >> (8u * b));
                }
            Upload::Budget intent{i == 0u ? 2u : 3u,
                true,
                1u,
                Upload::callbackCount,
                Upload::retainedOwnerBound,
                size,
                bytes,
                bytes,
                bytes,
                bytes,
                bytes};
            if (i == 0u) {
                intent.indexContent = {1u,
                    true,
                    rhi::IndexFormat::Uint16,
                    core::UnsignedElementSpan::ByteOrder::LittleEndian,
                    8u};
                if (purpose == IndexPurpose::GeometryAndShaderRead) {
                    intent.indexContent.shaderReadContractVersion = 1u;
                    intent.indexContent.shaderReadOptIn = true;
                }
            } else
                intent.vertexContent = {1u,
                    true,
                    i == 1u ? 8u : 24u,
                    i == 1u ? rhi::VertexInputRate::Vertex : rhi::VertexInputRate::Instance,
                    i == 1u ? 8u : 3u,
                    core::UnsignedElementSpan::ByteOrder::LittleEndian};
            rhi::BufferDesc desc;
            desc.sizeBytes = size;
            desc.usage = Upload::expectedUsage(intent);
            desc.memory = rhi::MemoryUsage::CpuToGpu;
            desc.initialState = Upload::expectedState(intent);
            core::UniquePtr<rhi::GpuBuffer> candidate;
            {
                core::CpuMemoryQuotaScope scope(resources);
                candidate = device.createBuffer(desc);
            }
            assert(candidate);
            auto loss = device.observeOriginalDeviceNativeLoss(id);
            assert(loss && !id);
            result.initialized[i] = Source::prepareOriginal(device,
                candidate,
                *loss,
                data.data(),
                size,
                99u,
                intent,
                {result.quotas[5u + i * 3u],
                    result.quotas[6u + i * 3u],
                    resources,
                    source,
                    result.quotas[7u + i * 3u]},
                id);
            require(id);
            assert(result.initialized[i] && !candidate);
            require(Source::initialize(result.initialized[i]));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return result.initialized[i]->callbacksDrained();
            }));
            require(result.initialized[i]->readyDiagnostic());
        }
        return result;
    }

private:
    static Prepared initializeStreams(
        rhi::GpuDevice& device, const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        return prepareInitializedStreams(device,
            resources,
            IndexPurpose::GeometryAndShaderRead,
            core::makeShared<core::CpuMemoryQuota>(bytes));
    }

    // Leave authored factory/descriptor arrays off the native execution wait's
    // continuation stack. All ownership remains with the same original chain.
    template <class Publish>
    static Prepared prepare(rhi::GpuDevice& device,
        Publish&& publish,
        const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        auto result = initializeStreams(device, resources);
        const auto source = result.quotas[0], inputsQuota = result.quotas[1],
                   stateQuota = result.quotas[2], streamQuota = result.quotas[3],
                   recording = result.quotas[4];
        const char* id = nullptr;
        auto attachment =
            OriginalCommandPipelineBindingFixture::attachmentSources(device, source, resources);
        std::array<Sources::Source, 5u> prefix{{attachment[0],
            attachment[1],
            result.initialized[0]->originalLease(),
            result.initialized[1]->originalLease(),
            result.initialized[2]->originalLease()}};
        auto first = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(publish(), id);
        assert(first);
        using Pipeline = rhi::OriginalCommandGraphicsPipelineSource;
        const auto adapt = [&](const auto& value) {
            return Pipeline::prepareOriginal(value,
                {1u,
                    true,
                    Pipeline::kind,
                    value->originalResourceRegistration().generation(),
                    bytes,
                    Pipeline::exclusiveOwnerCount},
                source,
                device,
                id);
        };
        auto a = adapt(first);
        assert(a);
        auto budget = OriginalCommandPipelineBindingFixture::indexedSourceBudget(true);
        // Additional immutable actors and original registrations are authored
        // before Sources publication. Production budgets are not changed.
        budget.maxGraphOwners += 2u
            * (Upload::retainedOwnerBound
                + rhi::OriginalResourceRegistrationContract::perResourceOwnerCount + 1u
                + rhi::OriginalWholeDescriptorWriteContract::quotaCount);
        budget.maxCommands = 14u;
        budget.maxReferences = budget.maxInputDescriptors = 9u;
        budget.maxStateObservations = 5u;
        budget.maxNativePayloadObjects = 9u;
        budget.maxTypedNativeCommandCalls = 12u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 3u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Descriptor)] =
            1u;
        auto sources = Sources::prepareColdOriginalTyped(
            prefix.data(), prefix.size(), budget, {2u, true, 2u}, source, id);
        require(id);
        assert(sources);
        require(Sources::appendOriginalTyped(sources, {a}, device));
        auto descriptors = OriginalCommandPipelineBindingFixture::prepareIndexedDescriptors(device,
            sources,
            result.initialized[0]->originalLease(),
            first,
            first,
            source,
            resources,
            1u);
        for (std::size_t q = 0u; q < 3u; ++q)
            result.quotas[14u + q] = descriptors.quotas[0u][q];
        const auto authored = commands();
        const std::array<rhi::OriginalCommandUsage::Reference, 9u> references{
            {{2u}, {3u}, {4u}, {1u}, {5u}, {6u}, {2u}, {3u}, {4u}}};
        auto usage = rhi::OriginalCommandUsage::prepareOriginal(sources,
            authored.data(),
            authored.size(),
            references.data(),
            references.size(),
            inputsQuota,
            id);
        require(id);
        assert(usage);
        using Scalar = rhi::CommandScalarArguments;
        const auto field = [&](std::size_t i) -> rhi::CommandVariableArguments::Input {
            if (i == 5u)
                return {*Scalar::capture(
                            K::SetViewport, rhi::ViewportState{0.f, 0.f, 2.f, 2.f, 0.f, 1.f}),
                    {}};
            if (i == 6u)
                return {*Scalar::capture(K::SetScissor, rhi::ScissorState{0u, 0u, 2u, 2u}), {}};
            if (i == 7u)
                return {
                    *Scalar::capture(K::BindRenderDescriptor, Scalar::DescriptorBinding{0u}), {}};
            if (i == 8u)
                return {*Scalar::capture(
                            K::BindIndexBuffer, Scalar::IndexBinding{rhi::IndexFormat::Uint16, 0u}),
                    {}};
            if (i == 9u || i == 10u)
                return {*Scalar::capture(K::BindVertexBuffer,
                            Scalar::VertexBinding{static_cast<std::uint32_t>(i - 9u), 0u}),
                    {}};
            if (i == 11u)
                return {
                    *Scalar::capture(K::DrawIndexed, Scalar::DrawIndexed{3u, 2u, 0u, 0, 1u}), {}};
            return {*Scalar::capture(authored[i].kind, Scalar::None{}), {}};
        };
        const std::array<rhi::CommandVariableArguments::Input, 14u> fields{{field(0u),
            field(1u),
            field(2u),
            field(3u),
            field(4u),
            field(5u),
            field(6u),
            field(7u),
            field(8u),
            field(9u),
            field(10u),
            field(11u),
            field(12u),
            field(13u)}};
        auto arguments = rhi::CommandVariableArguments::prepareOriginal(
            fields.data(), fields.size(), {1u, true, 14u, 0u, 0u, 0u, bytes}, inputsQuota, id);
        require(id);
        assert(arguments);
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, inputsQuota, id);
        require(id);
        assert(scalars);
        auto pass = OriginalCommandPipelineBindingFixture::passArguments(*sources, inputsQuota);
        const Inputs::OriginalDescriptor empty{SIZE_MAX, rhi::OriginalCommandPipelineBinding{}};
        std::array<Inputs::OriginalDescriptor, 9u> records{
            {empty, empty, empty, empty, empty, empty, empty, empty, empty}};
        for (std::size_t i = 0u; i < 3u; ++i) {
            rhi::ResourceMemoryBarrierDesc memory;
            memory.resource = result.initialized[i]->originalLease()->resource();
            memory.sourceStages = memory.destinationStages = rhi::PipelineStage::VertexInput;
            memory.sourceAccess = memory.destinationAccess =
                i == 0u ? rhi::ResourceAccess::IndexRead : rhi::ResourceAccess::VertexAttributeRead;
            if (i == 0u) {
                memory.sourceStages = memory.destinationStages =
                    memory.sourceStages | rhi::PipelineStage::FragmentShader;
                memory.sourceAccess = memory.destinationAccess =
                    memory.sourceAccess | rhi::ResourceAccess::ShaderRead;
            }
            records[i] = {
                i, *rhi::ResourceDependencyArguments::capture(K::BufferMemoryBarrier, memory)};
        }
        const auto gp = first->originalResourceRegistration().generation();
        records[3u] = {3u, pass};
        records[4u] = {
            4u, rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline, 5u, 3u, gp}};
        records[5u] = {7u,
            rhi::OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
                6u,
                5u,
                3u,
                descriptors.sources[0]
                    ->originalObject()
                    ->originalResourceRegistration()
                    .generation(),
                descriptors.sources[0]->frozenContentRevision()}};
        for (std::size_t i = 0u; i < 3u; ++i)
            records[6u + i] = {8u + i,
                rhi::OriginalCommandGeometryBinding{
                    i == 0u ? K::BindIndexBuffer : K::BindVertexBuffer,
                    2u + i,
                    5u,
                    4u,
                    3u,
                    result.initialized[i]
                        ->originalLease()
                        ->originalObject()
                        ->originalResourceRegistration()
                        .generation(),
                    gp}};
        rhi::OriginalCommandInputsBudget inputBudget{5u, true, 1u, 1u};
        inputBudget.maxIndexBindings = 1u;
        inputBudget.maxVertexBindings = 2u;
        auto inputs = Inputs::prepareOriginal(
            scalars, records.data(), records.size(), inputBudget, inputsQuota, id);
        require(id);
        assert(inputs);
        std::uint32_t owners = 0u, registrations = 0u;
        require(Sources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            budget.maxGraphOwners,
            budget.maxGraphOwners,
            owners,
            registrations));
        rhi::OriginalCommandStateAdmissionBudget state{4u,
            true,
            1u,
            registrations,
            5u,
            2u,
            owners,
            bytes,
            {3u, true, 0u, 1u, false, true, 1u, 1u, 1u, true, 1u},
            {3u, true, 0u, 1u, false, true, 14u, 1u, 1u, 1u, true, 1u}};
        state.descriptorConsumption.indexedDrawConsumptionVersion = 1u;
        state.descriptorConsumption.indexedDrawConsumptionOptIn = true;
        // Three binding footprints plus three draw-consumption footprints form
        // the complete six-access pair domain, independently of stream count.
        state.geometryUsage = {1u, true, 14u, 3u, 15u, 1u, true, 1u, 3u, 42u};
        assert(!rhi::OriginalCommandResourceStates::prepareOriginal(
            inputs, device, state, stateQuota, id));
        assert(id && core::StringView(id) == "RHI-ORIGINAL-CONSUMPTION-VERTEX-PROFILE"
            && !stateQuota->liveBytes());
        state.descriptorConsumption.vertexStreamConsumptionVersion = 1u;
        state.descriptorConsumption.vertexStreamConsumptionOptIn = true;
        state.descriptorConsumption.maxVertexStreams = 1u;
        assert(!rhi::OriginalCommandResourceStates::prepareOriginal(
            inputs, device, state, stateQuota, id));
        assert(id && core::StringView(id) == "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-CAPACITY"
            && !stateQuota->liveBytes());
        state.descriptorConsumption.maxVertexStreams = 2u;
        --state.geometryUsage.maxPairChecks;
        assert(!rhi::OriginalCommandResourceStates::prepareOriginal(
            inputs, device, state, stateQuota, id));
        assert(id && core::StringView(id) == "RHI-ORIGINAL-GEOMETRY-ACCESS-PAIR-CAPACITY"
            && !stateQuota->liveBytes());
        ++state.geometryUsage.maxPairChecks;
        std::size_t geometryVisits = 0u;
        const auto accesses = inputs->visitGeometryAccesses(
            state.geometryUsage, [&](const auto&) noexcept -> const char* {
                ++geometryVisits;
                return nullptr;
            });
        require(accesses.diagnostic);
        assert(accesses.bindings == 3u && accesses.draws == 1u && accesses.drawAccesses == 3u
            && accesses.pairChecks == 15u && geometryVisits == 6u);
        auto graph = rhi::OriginalCommandResourceStates::prepareOriginal(
            inputs, device, state, stateQuota, id);
        require(id);
        assert(graph);
        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = 4u;
        stream.prototypeOptIn = true;
        stream.maxCommands = 14u;
        stream.maxDescriptors = stream.maxReferences = 9u;
        stream.maxRenderPasses = stream.maxAttachmentReferences = 1u;
        stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = bytes;
        stream.maxPipelineBinds = stream.maxDescriptorBinds = stream.maxConsumptions = 1u;
        // One attachment cell, six explicit buffer operands, two descriptor
        // accesses and three geometry draw accesses need twelve state steps.
        stream.maxImplicitAccesses = 8u;
        stream.stateSequence = {4u, true, 14u, 0u, 5u, 12u, 8u};
        stream.descriptorConsumption = state.descriptorConsumption;
        stream.renderDescriptorUsage = state.renderDescriptorUsage;
        stream.geometryUsage = state.geometryUsage;
        const auto timeline =
            rhi::common::OriginalCommandAccessValidation::inspect(graph, stream.stateSequence);
        require(timeline.diagnostic);
        assert(timeline.steps == 12u);
        auto fewerSteps = stream.stateSequence;
        --fewerSteps.maxSteps;
        const auto shortTimeline =
            rhi::common::OriginalCommandAccessValidation::inspect(graph, fewerSteps);
        assert(shortTimeline.diagnostic
            && core::StringView(shortTimeline.diagnostic) == "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY");
        auto program =
            rhi::OriginalCommandStreamProgram::prepareOriginalTyped(graph, stream, streamQuota, id);
        require(id);
        assert(program);
        // The shader usage scope also includes the attachment: six geometry
        // contributions + two descriptor contributions + one attachment.
        rhi::common::OriginalTypedUsageProjection::Budget projected{4u,
            true,
            state.descriptorConsumption,
            state.renderDescriptorUsage,
            14u,
            1u,
            9u,
            1u,
            2u};
        projected.geometryUsage = state.geometryUsage;
        using Projection = rhi::common::OriginalTypedUsageProjection;
        const auto projectedAccesses =
            Projection::inspectStateBound(program, graph, {4u, true, projected});
        require(projectedAccesses.diagnostic);
        assert(projectedAccesses.counts.contributions == 9u);
        auto fewerContributions = projected;
        --fewerContributions.maxContributions;
        const auto shortProjection =
            Projection::inspectStateBound(program, graph, {4u, true, fewerContributions});
        assert(shortProjection.diagnostic
            && core::StringView(shortProjection.diagnostic)
                == "RHI-ORIGINAL-USAGE-PROJECTION-CONTRIBUTION-CAPACITY");
        // Complete pair traversal uses 9+1 passes, then each of three authored
        // dependencies uses two neighborhood passes: 16 passes x 9 visits.
        const rhi::common::OriginalTypedDependencyProjection::Request dependency{
            4u, true, {4u, true, projected}, 3u, 16u, 144u, 36u, 8u, 2u};
        const auto dependencies =
            rhi::common::OriginalTypedDependencyProjection::inspect(program, graph, dependency);
        require(dependencies.diagnostic);
        assert(dependencies.counts.dependencies == 3u && dependencies.counts.initial == 3u
            && dependencies.counts.projectionPasses == 16u
            && dependencies.counts.contributionVisits == 144u
            && dependencies.counts.pairChecks == 36u
            && dependencies.counts.endpointContributions == 8u);
        state.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        auto root = rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, device, state, recording, id);
        require(id);
        assert(root);
        require(root->materializeOriginalPrototypePlan(stream.stateSequence));
        auto loss = device.observeOriginalDeviceNativeLoss(id);
        assert(loss && !id);
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        // Every descriptor in the complete seven-Source inventory is bound by
        // this authored program; unused descriptor Sources remain forbidden.
        rhi::OriginalCommandNativeClosureBudget custody{6u,
            true,
            5u,
            7u,
            root->retainedOwnerCount() + 3u + 3u * Source::initializedIndexOwnerBound
                + 7u * (1u + rhi::OriginalWholeDescriptorWriteContract::quotaCount),
            0u,
            bytes,
            1u,
            1u};
        custody.geometryContent = {1u,
            true,
            3u,
            3u * Source::initializedIndexOwnerBound,
            152u,
            state.geometryUsage,
            {1u, true, 14u, 1u, 2u, 42u, 3u}};
        for (const auto& value : sources->sources())
            std::visit(
                [&](const auto& lease) {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(lease)>::element_type>;
                    if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>
                        || std::is_same_v<T, rhi::OriginalCommandTextureLease>)
                        custody.maxRetainedResourceBytes +=
                            lease->originalObject()->allocation().estimatedSizeBytes;
                },
                value);
        std::array<Closure::GeometryContentInput, 3u> contents{{{2u, result.initialized[0]},
            {3u, result.initialized[1]},
            {4u, result.initialized[2]}}};
        const auto before = recording->liveBytes(),
                   allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Closure::prepareOriginalGeometryContentLossBound(
            root, program, contents.data(), 2u, device, custody, *loss, id));
        assert(id && root->journalEmpty()
            && root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared);
        assert(recording->liveBytes() == before
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto fewerStreams = custody;
        fewerStreams.geometryContent.fetch.maxStreams = 1u;
        assert(!Closure::prepareOriginalGeometryContentLossBound(
            root, program, contents.data(), contents.size(), device, fewerStreams, *loss, id));
        assert(id && recording->liveBytes() == before
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto lessCustody = custody;
        --lessCustody.maxResourceBindings;
        assert(!Closure::prepareOriginalGeometryContentLossBound(
            root, program, contents.data(), contents.size(), device, lessCustody, *loss, id));
        assert(id && core::StringView(id) == "RHI-WEBGPU-CANONICAL-CLOSURE-BINDING-CAPACITY");
        lessCustody = custody;
        --lessCustody.maxPipelineSources;
        assert(!Closure::prepareOriginalGeometryContentLossBound(
            root, program, contents.data(), contents.size(), device, lessCustody, *loss, id));
        assert(id && core::StringView(id) == "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-SOURCE-BUDGET");
        lessCustody = custody;
        --lessCustody.maxDescriptorSources;
        assert(!Closure::prepareOriginalGeometryContentLossBound(
            root, program, contents.data(), contents.size(), device, lessCustody, *loss, id));
        assert(id && core::StringView(id) == "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-SOURCE-BUDGET");
        assert(recording->liveBytes() == before
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto closure = Closure::prepareOriginalGeometryContentLossBound(
            root, program, contents.data(), contents.size(), device, custody, *loss, id);
        require(id);
        assert(closure);
        auto frozen = rhi_webgpu::WebGpuOriginalTypedDependencyDecision::prepareOriginal(
            closure, {4u, true, dependency, closure->retainedOwnerCount() + 1u, bytes}, id);
        require(id);
        assert(frozen);
        auto payload = OriginalTypedNativePayloadFixture::verify(frozen,
            std::array{recording, stateQuota, streamQuota, inputsQuota},
            "immutable-vertex-instance",
            4u);
        result.admission = OriginalTypedColdAdmissionFixture::verifyGeometry(payload, device, true);
        return result;
    }

    template <class Publish>
    static std::array<std::shared_ptr<core::CpuMemoryQuota>, 21u> verifyComposedCustody(
        rhi::GpuDevice& device,
        Publish&& publish,
        const std::shared_ptr<core::CpuMemoryQuota>& resources,
        LifecycleCase mode)
    {
        // Same original initialization machinery, fresh actors and root. No
        // committed geometry program is recaptured or retroactively made CopySrc.
        auto prepared = initializeStreams(device, resources);
        const auto source = prepared.quotas[0], inputQuota = prepared.quotas[1],
                   stateQuota = prepared.quotas[2], streamQuota = prepared.quotas[3],
                   recording = prepared.quotas[4];
        auto contentQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        constexpr char label[] = "geometry-readback";
        constexpr std::uint32_t commandCount = 19u;
        rhi::TextureDesc td;
        td.extent = {2u, 2u, 1u};
        td.format = rhi::TextureFormat::Rgba8Unorm;
        td.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
        core::UniquePtr<rhi::GpuTexture> texture;
        core::UniquePtr<rhi::GpuTextureView> view;
        core::UniquePtr<rhi::GpuBuffer> buffer;
        {
            core::CpuMemoryQuotaScope allocation(resources);
            texture = device.createOriginalContentTexture(
                td, {2u, true, 1u, 1u, 16u, 16u, bytes, 1u}, contentQuota, id);
            require(id);
            assert(texture);
            rhi::TextureViewDesc vd;
            vd.format = td.format;
            vd.usage = rhi::TextureUsage::RenderAttachment;
            vd.aspect = rhi::TextureAspect::Color;
            view = device.createTextureView(*texture, vd);
            assert(view);
            rhi::BufferDesc bd;
            bd.sizeBytes = 512u;
            bd.usage = rhi::ResourceUsage::CopyDst;
            bd.memory = rhi::MemoryUsage::GpuToCpu;
            bd.initialState = rhi::ResourceState::CopyDst;
            buffer = device.createBuffer(bd);
            assert(buffer);
        }
        auto parent = rhi::OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, bytes}, source, id);
        require(id);
        assert(parent);
        auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            view, {1u, true, 2u, bytes}, source, id);
        require(id);
        assert(viewLease);
        auto target = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            viewLease, parent, {1u, true, 5u, bytes}, source, id);
        require(id);
        assert(target);
        auto destination = rhi::OriginalCommandBufferLease::prepareOriginal(
            buffer, {1u, true, 2u, bytes}, source, id);
        require(id);
        assert(destination);
        rhi::ReadbackTextureDesc read;
        read.sourceResource = parent->resource();
        read.destinationReadbackBuffer = destination->resource();
        read.extent = td.extent;
        read.bytesPerRow = 256u;
        read.rowsPerImage = 2u;
        read.sizeBytes = 512u;
        read.sourceSubresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
        read.label = label;
        auto readInput = rhi::TextureReadbackInputSnapshot::prepareOriginal(
            read, {1u, true, 1u, sizeof(label), bytes, bytes}, inputQuota, inputQuota, id);
        require(id);
        assert(readInput);
        auto layout = rhi::TextureReadbackLayout::prepareOriginal(
            readInput, td, {1u, true, 1u, 512u, bytes}, inputQuota, id);
        require(id);
        assert(layout);
        auto request = TextureReadbackSourceLeaseFixture::request(layout, inputQuota);
        auto rbLeaseBudget = TextureReadbackSourceLeaseFixture::budget(request);
        rbLeaseBudget.contractVersion = 2u;
        rbLeaseBudget.maxRetainedOwners =
            rhi::TextureReadbackSourceLease::retainedOriginalOwnerCount;
        auto readLease = rhi::TextureReadbackSourceLease::prepareRetainedOriginal(
            parent, request, rbLeaseBudget, source, id);
        require(id);
        assert(readLease);
        rhi::OriginalCommandReadbackSourceBudget rbBudget{
            2u, true, 8u, 1u, sizeof(label), 512u, bytes, bytes};
        auto readSource = rhi::OriginalCommandReadbackTextureSource::prepareOriginal(
            readLease, rbBudget, source, inputQuota, id);
        require(id);
        assert(readSource);
        rbBudget.maxRetainedOwners =
            rhi::OriginalCommandTextureReadbackSource::completeRetainedOwnerCount
            + rhi::TextureReadbackSourceLease::retainedOriginalOwnerCount
            - rhi::TextureReadbackSourceLease::retainedOwnerCount;
        auto readback = rhi::OriginalCommandTextureReadbackSource::prepareOriginal(
            readSource, destination, rbBudget, source, id);
        require(id);
        assert(readback);
        std::array<Sources::Source, 7u> prefix{{parent,
            target,
            prepared.initialized[0]->originalLease(),
            prepared.initialized[1]->originalLease(),
            prepared.initialized[2]->originalLease(),
            destination,
            readback}};
        auto first = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(publish(), id);
        require(id);
        assert(first);
        auto pipeline = rhi::OriginalCommandGraphicsPipelineSource::prepareOriginal(first,
            {1u,
                true,
                rhi::OriginalCommandGraphicsPipelineSource::kind,
                first->originalResourceRegistration().generation(),
                bytes,
                rhi::OriginalCommandGraphicsPipelineSource::exclusiveOwnerCount},
            source,
            device,
            id);
        require(id);
        assert(pipeline);
        const rhi::OriginalRetainedReadbackCorrespondence correspondence{1u, true, 1u, 7u};
        auto budget = OriginalCommandPipelineBindingFixture::indexedSourceBudget(true);
        budget.maxGraphOwners += 2u
                * (Upload::retainedOwnerBound
                    + rhi::OriginalResourceRegistrationContract::perResourceOwnerCount + 1u
                    + rhi::OriginalWholeDescriptorWriteContract::quotaCount)
            + rhi::OriginalCommandBufferLease::retainedOwnerCount
            + rhi::OriginalCommandTextureReadbackSource::retainedOwnerCount
            + correspondence.maxNestedOwners + 1u;
        // indexedSourceBudget already declares the legacy eight-slot command
        // graph. This workload adds four map slots and the Result storage,
        // independent quota and one snapshot before immutable publication.
        budget.maxGraphOwners +=
            4u + rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners + 1u;
        budget.maxCommands = commandCount;
        budget.maxReferences = 13u;
        budget.maxInputDescriptors = 12u;
        budget.maxStateObservations = 9u;
        budget.maxTextureRanges = 5u;
        budget.maxNativePayloadObjects = 11u;
        budget.maxTypedNativeCommandCalls = 13u;
        budget.maxMipMappings = 1u;
        budget.maxLabelBytes = 2u * sizeof(label);
        budget.maxUnpublishedResultBytes = budget.maxNativeStagingBytes = 512u;
        // One authored map adds one map callback and three scopes to the
        // existing recording/submission/queue/loss demand. Publish this finite
        // authority with Sources, before any admission or reservation exists.
        budget.maxCallbackTickets = budget.maxCallbackRegistrations = 12u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 4u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Readback)] =
            1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Descriptor)] =
            1u;
        auto sources = Sources::prepareColdOriginalTyped(
            prefix.data(), prefix.size(), budget, {2u, true, 2u}, source, id, correspondence);
        require(id);
        assert(sources);
        require(Sources::appendOriginalTyped(sources, {pipeline}, device));
        auto descriptors = OriginalCommandPipelineBindingFixture::prepareIndexedDescriptors(device,
            sources,
            prepared.initialized[0]->originalLease(),
            first,
            first,
            source,
            resources,
            1u);
        for (std::size_t q = 0u; q < 3u; ++q)
            prepared.quotas[14u + q] = descriptors.quotas[0u][q];
        const auto authored = composedCommands();
        const std::array<rhi::OriginalCommandUsage::Reference, 13u> refs{
            {{2u}, {3u}, {4u}, {0u}, {1u}, {7u}, {8u}, {2u}, {3u}, {4u}, {0u}, {0u}, {6u}}};
        auto usage = rhi::OriginalCommandUsage::prepareOriginal(
            sources, authored.data(), authored.size(), refs.data(), refs.size(), inputQuota, id);
        require(id);
        assert(usage);
        using Scalar = rhi::CommandScalarArguments;
        const auto field = [&](std::size_t i) -> rhi::CommandVariableArguments::Input {
            if (i == 6u)
                return {*Scalar::capture(
                            K::SetViewport, rhi::ViewportState{0.f, 0.f, 2.f, 2.f, 0.f, 1.f}),
                    {}};
            if (i == 7u)
                return {*Scalar::capture(K::SetScissor, rhi::ScissorState{0u, 0u, 2u, 2u}), {}};
            if (i == 8u)
                return {
                    *Scalar::capture(K::BindRenderDescriptor, Scalar::DescriptorBinding{0u}), {}};
            if (i == 9u)
                return {*Scalar::capture(
                            K::BindIndexBuffer, Scalar::IndexBinding{rhi::IndexFormat::Uint16, 0u}),
                    {}};
            if (i == 10u || i == 11u)
                return {*Scalar::capture(K::BindVertexBuffer,
                            Scalar::VertexBinding{static_cast<std::uint32_t>(i - 10u), 0u}),
                    {}};
            if (i == 12u)
                return {
                    *Scalar::capture(K::DrawIndexed, Scalar::DrawIndexed{3u, 2u, 0u, 0, 1u}), {}};
            return {*Scalar::capture(authored[i].kind, Scalar::None{}),
                i == 16u ? std::string_view(label) : std::string_view{}};
        };
        const std::array<rhi::CommandVariableArguments::Input, commandCount> fields{{field(0u),
            field(1u),
            field(2u),
            field(3u),
            field(4u),
            field(5u),
            field(6u),
            field(7u),
            field(8u),
            field(9u),
            field(10u),
            field(11u),
            field(12u),
            field(13u),
            field(14u),
            field(15u),
            field(16u),
            field(17u),
            field(18u)}};
        auto args = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, commandCount, 0u, sizeof(label), 0u, bytes},
            inputQuota,
            id);
        require(id);
        assert(args);
        auto scalars = rhi::OriginalCommandScalars::prepareOriginal(usage, args, inputQuota, id);
        require(id);
        assert(scalars);
        const Inputs::OriginalDescriptor empty{SIZE_MAX, rhi::OriginalCommandPipelineBinding{}};
        std::array<Inputs::OriginalDescriptor, 12u> records{
            {empty, empty, empty, empty, empty, empty, empty, empty, empty, empty, empty, empty}};
        for (std::size_t i = 0u; i < 3u; ++i) {
            rhi::ResourceMemoryBarrierDesc d;
            d.resource = prepared.initialized[i]->originalLease()->resource();
            d.sourceStages = d.destinationStages = rhi::PipelineStage::VertexInput;
            d.sourceAccess = d.destinationAccess =
                i == 0u ? rhi::ResourceAccess::IndexRead : rhi::ResourceAccess::VertexAttributeRead;
            if (i == 0u) {
                d.sourceStages = d.destinationStages =
                    d.sourceStages | rhi::PipelineStage::FragmentShader;
                d.sourceAccess = d.destinationAccess =
                    d.sourceAccess | rhi::ResourceAccess::ShaderRead;
            }
            records[i] = {i, *rhi::ResourceDependencyArguments::capture(K::BufferMemoryBarrier, d)};
        }
        const auto transition = [&](std::size_t command,
                                    rhi::ResourceState before,
                                    rhi::ResourceState after) {
            rhi::ResourceBarrierDesc d{parent->resource(), read.sourceSubresources, before, after};
            d.sourceStages = before == rhi::ResourceState::Undefined
                ? rhi::PipelineStage::TopOfPipe
                : rhi::PipelineStage::ColorAttachmentOutput;
            d.sourceAccess = before == rhi::ResourceState::Undefined
                ? rhi::ResourceAccess::None
                : rhi::ResourceAccess::ColorAttachmentRead
                    | rhi::ResourceAccess::ColorAttachmentWrite;
            d.destinationStages = after == rhi::ResourceState::CopySrc
                ? rhi::PipelineStage::Transfer
                : rhi::PipelineStage::ColorAttachmentOutput;
            d.destinationAccess = after == rhi::ResourceState::CopySrc
                ? rhi::ResourceAccess::TransferRead
                : rhi::ResourceAccess::ColorAttachmentRead
                    | rhi::ResourceAccess::ColorAttachmentWrite;
            return Inputs::OriginalDescriptor{
                command, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, d)};
        };
        records[3u] =
            transition(3u, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment);
        records[4u] = {
            4u, OriginalCommandPipelineBindingFixture::passArguments(*sources, inputQuota)};
        const auto gp = first->originalResourceRegistration().generation();
        records[5u] = {
            5u, rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline, 7u, 4u, gp}};
        records[6u] = {8u,
            rhi::OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
                8u,
                7u,
                4u,
                descriptors.sources[0]
                    ->originalObject()
                    ->originalResourceRegistration()
                    .generation(),
                descriptors.sources[0]->frozenContentRevision()}};
        for (std::size_t i = 0u; i < 3u; ++i)
            records[7u + i] = {9u + i,
                rhi::OriginalCommandGeometryBinding{
                    i == 0u ? K::BindIndexBuffer : K::BindVertexBuffer,
                    2u + i,
                    7u,
                    5u,
                    4u,
                    prepared.initialized[i]
                        ->originalLease()
                        ->originalObject()
                        ->originalResourceRegistration()
                        .generation(),
                    gp}};
        records[10u] =
            transition(14u, rhi::ResourceState::ColorAttachment, rhi::ResourceState::CopySrc);
        records[11u] = {16u,
            *rhi::TextureReadbackOriginalInputArguments::captureOriginal(
                readback->originalInput())};
        rhi::OriginalCommandInputsBudget inputBudget{5u, true, 1u, 1u};
        inputBudget.maxIndexBindings = 1u;
        inputBudget.maxVertexBindings = 2u;
        inputBudget.retainedReadback = correspondence;
        auto inputs = Inputs::prepareOriginal(
            scalars, records.data(), records.size(), inputBudget, inputQuota, id);
        require(id);
        assert(inputs);
        std::uint32_t owners = 0u, registrations = 0u;
        require(Sources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            budget.maxGraphOwners,
            budget.maxGraphOwners,
            owners,
            registrations));
        rhi::OriginalCommandStateAdmissionBudget state{4u,
            true,
            1u,
            registrations,
            9u,
            5u,
            owners,
            bytes,
            {3u, true, 0u, 1u, false, true, 1u, 1u, 1u, true, 1u},
            {3u, true, 0u, 1u, false, true, commandCount, 1u, 1u, 1u, true, 1u},
            1u,
            true};
        state.descriptorConsumption.indexedDrawConsumptionVersion = 1u;
        state.descriptorConsumption.indexedDrawConsumptionOptIn = true;
        state.descriptorConsumption.vertexStreamConsumptionVersion = 1u;
        state.descriptorConsumption.vertexStreamConsumptionOptIn = true;
        state.descriptorConsumption.maxVertexStreams = 2u;
        state.geometryUsage = {
            1u, true, commandCount, 3u, 15u, 1u, true, 1u, 3u, commandCount * 3u};
        auto graph = rhi::OriginalCommandResourceStates::prepareOriginal(
            inputs, device, state, stateQuota, id);
        require(id);
        assert(graph);
        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = 4u;
        stream.prototypeOptIn = true;
        stream.maxCommands = commandCount;
        stream.maxDescriptors = 12u;
        stream.maxReferences = 13u;
        stream.maxRenderPasses = stream.maxAttachmentReferences = 1u;
        stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = bytes;
        stream.maxPipelineBinds = stream.maxDescriptorBinds = stream.maxConsumptions = 1u;
        stream.maxImplicitAccesses = 8u;
        stream.stateSequence = {4u, true, commandCount, 0u, 9u, 17u, 8u};
        stream.maxTextureReadbackMips = 1u;
        stream.maxReadbackDeclaredBytes = 512u;
        stream.descriptorConsumption = state.descriptorConsumption;
        stream.renderDescriptorUsage = state.renderDescriptorUsage;
        stream.geometryUsage = state.geometryUsage;
        auto program =
            rhi::OriginalCommandStreamProgram::prepareOriginalTyped(graph, stream, streamQuota, id);
        require(id);
        assert(program);
        // Both texture observations alias one content control. Each cell visits
        // every command; the finite19-command/one-cell domain is authored here.
        state.maxRetainedOwners = graph->retainedOwnerCount() + 2u;
        auto root = rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, device, state, recording, id);
        require(id);
        assert(root);
        const rhi::OriginalTextureContentPlanBudget content{
            3u, true, 1u, 0u, 1u, 4u, 2u * commandCount, commandCount * commandCount, 1u};
        require(root->materializeOriginalPrototypePlan(stream.stateSequence, content));
        using Joins = rhi_webgpu::WebGpuCanonicalReadbackJoins;
        Joins::Budget join{3u,
            true,
            1u,
            1u,
            1u,
            budget.maxGraphOwners,
            16u,
            512u,
            bytes,
            stream,
            {2u, true, 1u, 1u, 1u, 512u, 512u, bytes}};
        const Joins::Input entry{16u, readback};
        auto joins = Joins::prepareOriginalProgram(root, program, device, &entry, 1u, join, id);
        require(id);
        assert(joins);
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        rhi::OriginalCommandNativeClosureBudget custody{
            7u, true, 6u, 11u, budget.maxGraphOwners, 680u, bytes, 1u, 1u, 1u, 1u};
        custody.geometryContent = {1u,
            true,
            3u,
            3u * Source::initializedIndexOwnerBound,
            152u,
            state.geometryUsage,
            {1u, true, commandCount, 1u, 2u, commandCount * 3u, 3u}};
        const std::array<Closure::GeometryContentInput, 3u> contents{{{2u, prepared.initialized[0]},
            {3u, prepared.initialized[1]},
            {4u, prepared.initialized[2]}}};
        auto loss = device.observeOriginalDeviceNativeLoss(id);
        require(id);
        assert(loss);
        auto closure = Closure::prepareOriginalGeometryReadbackLossBound(
            root, program, joins, contents.data(), contents.size(), device, custody, *loss, id);
        require(id);
        assert(closure);
        assert(!closure->validate() && closure->closureContractVersion() == 7u
            && joins->joinContractVersion() == 3u && joins->projectedCopyCount() == 1u
            && !closure->nativeExecutionProven() && !joins->nativeExecutionProven());
        using Projection = rhi::common::OriginalTypedUsageProjection;
        Projection::Budget projected{4u,
            true,
            state.descriptorConsumption,
            state.renderDescriptorUsage,
            commandCount,
            1u,
            11u,
            1u,
            2u,
            1u,
            1u,
            512u,
            state.geometryUsage};
        const auto accesses = Projection::inspectStateBound(program, graph, {4u, true, projected});
        require(accesses.diagnostic);
        assert(accesses.counts.contributions == 11u && accesses.counts.readbacks == 1u
            && accesses.counts.readbackMips == 1u && accesses.counts.readbackDeclaredBytes == 512u);
        // Eleven actual contributions: six geometry, two descriptors, one
        // attachment and two readback endpoints. Pair traversal has12 passes;
        // five authored dependencies add10 neighborhood passes:22 x11 visits.
        using Dependency = rhi::common::OriginalTypedDependencyProjection;
        const Dependency::Request projectedDependencies{
            4u, true, {4u, true, projected}, 5u, 22u, 242u, 55u, 11u, 2u};
        const auto dependencies = Dependency::inspect(program, graph, projectedDependencies);
        require(dependencies.diagnostic);
        assert(dependencies.counts.dependencies == 5u && dependencies.counts.initial == 4u
            && dependencies.counts.between == 1u && dependencies.counts.final == 0u
            && dependencies.counts.projectionPasses == 22u
            && dependencies.counts.contributionVisits == 242u
            && dependencies.counts.pairChecks == 55u
            && dependencies.counts.endpointContributions == 11u);
        using Frozen = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
        const Frozen::Request frozenRequest{
            5u, true, projectedDependencies, closure->retainedOwnerCount() + 1u, bytes};
        auto frozen = Frozen::prepareOriginal(closure, frozenRequest, id);
        require(id);
        assert(frozen);
        assert(!frozen->validate() && !frozen->compareOriginalClosure(closure)
            && frozen->records().size() == 5u && !frozen->nativeExecutionProven()
            && !frozen->synchronizationProven());
        auto payload = OriginalTypedNativePayloadFixture::verify(frozen,
            std::array{recording, stateQuota, streamQuota, inputQuota},
            "geometry-readback",
            5u);
        assert(payload->work().commands == 19u && payload->work().nativeCalls == 13u
            && payload->work().dependencies == 5u && payload->work().vertexBinds == 2u
            && payload->work().indexBinds == 1u && payload->work().indexedDraws == 1u
            && payload->transferPassCount() == 1u && payload->readbackCommandCount() == 1u
            && payload->readbackCopyCount() == 1u);
        const auto& copied = payload->records()[16u];
        assert(copied.readback && copied.readback->commandIndex == 16u
            && copied.readback->source == readback && copied.destination
            && copied.destination->sourceIndex == 5u && copied.readbackCopyCount == 1u);
        // Preparation must retain exact owners without acquiring command/map
        // authority; malformed joins are rejected before quota allocation.
        const auto live = recording->liveBytes(),
                   allocated = core::defaultCpuAllocator().stats().allocationCount;
        auto bad = join;
        bad.contractVersion = 2u;
        assert(!Joins::prepareOriginalProgram(root, program, device, &entry, 1u, bad, id));
        auto wrong = entry;
        wrong.commandIndex = 17u;
        assert(!Joins::prepareOriginalProgram(root, program, device, &wrong, 1u, join, id));
        auto less = custody;
        less.geometryContent.fetch.maxStreams = 1u;
        assert(!Closure::prepareOriginalGeometryReadbackLossBound(
            root, program, joins, contents.data(), contents.size(), device, less, *loss, id));
        auto badFreeze = frozenRequest;
        badFreeze.contractVersion = 4u;
        assert(!Frozen::prepareOriginal(closure, badFreeze, id));
        badFreeze = frozenRequest;
        badFreeze.prototypeOptIn = false;
        assert(!Frozen::prepareOriginal(closure, badFreeze, id));
        badFreeze = frozenRequest;
        --badFreeze.maxRetainedOwners;
        assert(!Frozen::prepareOriginal(closure, badFreeze, id));
        badFreeze = frozenRequest;
        --badFreeze.projection.maxDependencies;
        assert(!Frozen::prepareOriginal(closure, badFreeze, id));
        badFreeze = frozenRequest;
        --badFreeze.projection.maxPairChecks;
        assert(!Frozen::prepareOriginal(closure, badFreeze, id));
        badFreeze = frozenRequest;
        --badFreeze.projection.maxContributionVisits;
        assert(!Frozen::prepareOriginal(closure, badFreeze, id));
        badFreeze = frozenRequest;
        badFreeze.projection.usages.projection.maxReadbacks = 0u;
        assert(!Frozen::prepareOriginal(closure, badFreeze, id));
        auto badPayload = payload->originalRequest();
        badPayload.contractVersion = 4u;
        assert(
            !rhi_webgpu::WebGpuOriginalTypedNativePayload::prepareOriginal(frozen, badPayload, id));
        badPayload = payload->originalRequest();
        --badPayload.maxReadbackCopies;
        assert(
            !rhi_webgpu::WebGpuOriginalTypedNativePayload::prepareOriginal(frozen, badPayload, id));
        badPayload = payload->originalRequest();
        --badPayload.maxWork.indexedDraws;
        assert(
            !rhi_webgpu::WebGpuOriginalTypedNativePayload::prepareOriginal(frozen, badPayload, id));
        assert(recording->liveBytes() == live
            && core::defaultCpuAllocator().stats().allocationCount == allocated);
        const auto verifyFrozenHot = [&]() {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording}, [&]() noexcept {
                    return frozen->validate();
                }));
        };
#if defined(__EMSCRIPTEN__) && defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(verifyFrozenHot);
#else
        verifyFrozenHot();
#endif
        assert(root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared
            && root->journalEmpty());
        auto admission = OriginalTypedColdAdmissionFixture::verifyComposed(payload, device);
        auto command = OriginalTypedColdCommandFixture::verifyComposed(admission);
        std::puts(
            "BINDLESS_RHI_GEOMETRY_READBACK_NATIVE_CUSTODY_OK original19 State4 Stream4 Join3 Closure7 vertex2 index1 readback1 CopySrc-authored no-record-map-public-authority");
        std::puts(
            "BINDLESS_RHI_GEOMETRY_READBACK_FREEZE5_OK original19 Closure7 dependencies5 contributions11 passes22 visits242 pairs55 endpoints11 legacy4-refused hot0 no-record-map-public-authority");
        std::puts(
            "BINDLESS_RHI_GEOMETRY_READBACK_PAYLOAD5_OK original19 Freeze5 Closure7 calls13 vertex2 index1 indexedDraw1 transfer1 readback1 copy1 exact-original-endpoints no-record-map-public-authority");
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 21u> retired;
        std::copy(prepared.quotas.begin(), prepared.quotas.end(), retired.begin());
        retired[17u] = contentQuota;
        const auto& seal = admission->originalReservation()->originalSeal();
        retired[18u] = seal->originalSubmissionQuota();
        retired[19u] = seal->originalCallbackQuota();
        std::weak_ptr<const rhi_webgpu::WebGpuOriginalTypedColdAdmission> retained = admission;
        admission.reset();
        assert(!retained.expired() && !command->validate());
        auto resultQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        retired[20u] = resultQuota;
        executeComposed(std::move(command), device, resultQuota, mode);

        return retired;
    }

    static void executeComposed(
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdCommand> command,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& resultQuota,
        LifecycleCase mode)
    {
        executeReadback(
            std::move(command),
            device,
            resultQuota,
            mode,
            5u,
            [](const std::uint8_t* output) {
                constexpr std::array<std::uint8_t, 4u> expected{0u, 0u, 255u, 255u};
                for (std::size_t y = 0u; y < 2u; ++y)
                    for (std::size_t x = 0u; x < 2u; ++x)
                        for (std::size_t channel = 0u; channel < 4u; ++channel)
                            assert(output[y * 256u + x * 4u + channel] == expected[channel]);
            },
            "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_OK same-original19 encoder1 indexedDraw1 vertex2 instances2 firstInstance1 copy1 queue1 map1 scopes3-3-3 Result2 blueRGBA8-four-pixels caller-drop hot0-query0 public-closed");
    }

public:
    // Caller authors the exact original endpoint and numeric oracle. The same
    // Callback5/Issuer5/Execution5 performs record/submit/map/completion; this
    // helper never constructs a replacement root or grants public authority.
    template <class VerifyOutput>
    static void executeReadback(
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdCommand> command,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& resultQuota,
        LifecycleCase mode,
        std::uint32_t destinationSourceIndex,
        VerifyOutput&& verifyOutput,
        const char* completionMarker)
    {
        using Storage = rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
        using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
        using Issuers = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
        using Root = rhi::OriginalCommandStateTransaction;
        using Scopes = WebGpuErrorScopeHandoffFixture<1017u>;
        using MapScopes = WebGpuErrorScopeHandoffFixture<1018u>;
        const auto& admission = command->originalAdmission();
        const auto root = admission->originalRoot();
        const auto& seal = admission->originalReservation()->originalSeal();
        const auto recording = root->originalMetadataQuota(),
                   submission = seal->originalSubmissionQuota(),
                   callback = seal->originalCallbackQuota();
        const auto owners = admission->originalRequest().maxRetainedOwners;
        const char* id = nullptr;
        const Storage::Budget storageBudget{2u, true, 1u, 512u, bytes, owners};
        auto wrongStorage = storageBudget;
        wrongStorage.maxSnapshots = 0u;
        assert(!Storage::prepareOriginalTyped(admission, device, wrongStorage, resultQuota, id));
        assert(!Storage::prepareOriginalTyped(admission, device, storageBudget, recording, id));
        auto results =
            Storage::prepareOriginalTyped(admission, device, storageBudget, resultQuota, id);
        require(id);
        assert(results);
        const rhi::OriginalTypedCallbackCustodyRequest callbackRequest{5u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            12u,
            1u,
            owners,
            submission->limitBytes(),
            callback->limitBytes(),
            {1u, true}};
        auto missing = callbackRequest;
        missing.readbackMap = {};
        assert(!Custody::prepareOriginal(command, missing, id, results));
        missing = callbackRequest;
        --missing.maxCallbackSlots;
        assert(!Custody::prepareOriginal(command, missing, id, results));
        assert(!Custody::prepareOriginal(command, callbackRequest, id, {}));
        missing = callbackRequest;
        missing.maxRetainedOwners = command->retainedOwnerCount()
            + rhi::OriginalTypedCallbackCustodyContract::additionalOwners(callbackRequest)
            + results->retainedOwnerCount() - admission->retainedOwnerCount() - 1u;
        assert(!Custody::prepareOriginal(command, missing, id, results) && id
            && core::StringView(id) == "RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY");
        auto custody = Custody::prepareOriginal(command,
            callbackRequest,
            id,
            results,
            rhi::OriginalCommandReadbackSnapshotPublicationContract::Intent{1u, true});
        require(id);
        assert(custody);
        auto issuers = Issuers::prepareOriginal(custody,
            {5u,
                true,
                1u,
                1u,
                7u,
                owners,
                recording->limitBytes(),
                submission->limitBytes(),
                callback->limitBytes()},
            id);
        require(id);
        assert(issuers);
        std::printf(
            "BINDLESS_RHI_COMPOSED_EXECUTION_PREPARED commands%llu Result2 Callback5 Issuer5 output-unpublished\n",
            static_cast<unsigned long long>(admission->originalPayload()->work().commands));
        const rhi::OriginalTypedNativeExecutionRequest execution{5u,
            true,
            rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1,
            {1u, true}};
        auto wrong = execution;
        wrong.readbackCompletion = {};
        assert(Issuers::recordOriginal(issuers, wrong));
        for (const auto version : {1u, 2u, 3u, 4u}) {
            wrong = execution;
            wrong.contractVersion = version;
            assert(
                Issuers::recordOriginal(issuers, wrong) && Issuers::submitOriginal(issuers, wrong));
        }
        const std::array quotas{recording, submission, callback, resultQuota};
        const auto nativeDevice =
            admission->originalPayload()->originalDecision()->originalClosure()->nativeDevice();
        if (mode == LifecycleCase::RecordingUncertain
            || mode == LifecycleCase::RecordingScopeUncertain) {
            const bool partialPush = mode == LifecycleCase::RecordingScopeUncertain;
            auto snapshot = results->snapshots()[0];
            std::weak_ptr<const Storage> retainedResults = results;
            if (partialPush)
                WebGpuNativeSubmissionProbeFixture::instrumentScopePush(nativeDevice);
            else {
                Scopes::holdScopes();
                WebGpuNativeSubmissionProbeFixture::instrumentRecordingException(nativeDevice);
            }
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                id = CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    return Issuers::recordOriginal(issuers, execution);
                });
            });
            if (partialPush)
                WebGpuNativeSubmissionProbeFixture::restoreScopePush();
            else {
                WebGpuNativeSubmissionProbeFixture::restoreRecordingException();
                Scopes::restoreScopes();
                Scopes::waitHeldScopes();
            }
            assert(id
                && core::StringView(id)
                    == (partialPush ? "RHI-WEBGPU-TYPED-RECORDING-SCOPE-PUSH-UNCERTAIN"
                                    : "RHI-WEBGPU-TYPED-RECORDING-INVOCATION-UNCERTAIN"));
            results.reset();
            command.reset();
            assert(root->status() == Root::Status::Invalidated && root->journalEmpty()
                && custody->pendingOriginalRecords() == 1u && !retainedResults.expired()
                && !snapshot->publishedBytes() && !issuers->hasOriginalRecordedCommand()
                && !issuers->hasOriginalCompletedSubmission());
            assert(Issuers::recordOriginal(issuers, execution) == id
                && Issuers::submitOriginal(issuers, execution) == id);
            // Even all genuine recording receipts cannot prove that an unknown
            // native encoder effect is safe. Only actual loss closes this case.
            if (!partialPush) {
                Scopes::releaseScope(0u);
                Scopes::releaseScope(1u);
                emscripten_sleep(0u);
                assert(custody->pendingOriginalRecords() == 1u && !retainedResults.expired());
                Scopes::releaseScopes();
            }
            emscripten_sleep(0u);
            assert(custody->pendingOriginalRecords() == 1u && !retainedResults.expired());
            assert(EM_ASM_INT(
                {
                    // clang-format off
                        const d = WebGPU.getJsObject($0);
                        if (d !== Module.preinitializedWebGPUDevice)
                            return 0;
                        d.destroy();
                        return 1;
                    // clang-format on
                },
                nativeDevice));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return custody->physicalLossDelivered() && !custody->pendingOriginalRecords();
            }));
            assert(root->status() == Root::Status::Invalidated && root->journalEmpty()
                && !snapshot->publishedBytes() && !issuers->hasOriginalCompletedSubmission());
            issuers.reset();
            custody.reset();
            assert(retainedResults.expired());
            retainedResults.reset();
            snapshot.reset();
            assert(!resultQuota->liveBytes());
            std::printf(
                "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_OK %s same-original%llu %s actual-device-loss invalidated-before-drain journal-empty no-retry unpublished completion0 caller-drop public-closed\n",
                lifecycleName(mode),
                static_cast<unsigned long long>(admission->originalPayload()->work().commands),
                partialPush ? "actual-push2-then-throw no-guessed-pop encoder0 loss-only-retirement"
                            : "genuine-encoder-then-throw scopes-released-still-retained");
            return;
        }
        Scopes::holdScopes();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            id = CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return Issuers::recordOriginal(issuers, execution);
            });
        });
        require(id);
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(root->status() == Root::Status::Pending && !root->journalEmpty()
            && !results->snapshots()[0]->publishedBytes());
        std::printf(
            "BINDLESS_RHI_COMPOSED_EXECUTION_RECORDED commands%llu real-encoder-copy Pending-unpublished\n",
            static_cast<unsigned long long>(admission->originalPayload()->work().commands));
        assert(root->commitOriginalPendingOwnership());
        if (mode == LifecycleCase::RecordingCancel) {
            auto snapshot = results->snapshots()[0];
            std::weak_ptr<const Storage> retainedResults = results;
            id = custody->cancelOriginal();
            assert(id
                && core::StringView(id)
                    == "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-CALLBACKS-PENDING");
            results.reset();
            command.reset();
            Scopes::releaseScope(0u);
            Scopes::releaseScope(1u);
            emscripten_sleep(0u);
            assert(root->status() == Root::Status::Pending
                && custody->pendingOriginalRecords() == 1u && !retainedResults.expired()
                && !snapshot->publishedBytes());
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !custody->pendingOriginalRecords();
            }));
            assert(root->status() == Root::Status::Abandoned && root->journalEmpty()
                && !snapshot->publishedBytes() && !issuers->hasOriginalCompletedSubmission()
                && Issuers::submitOriginal(issuers, execution));
            issuers.reset();
            custody.reset();
            assert(retainedResults.expired());
            retainedResults.reset();
            snapshot.reset();
            assert(!resultQuota->liveBytes());
            std::printf(
                "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_OK recording-cancel same-original%llu scopes2-held1 Pending-until-drain Abandoned-journal-empty copy1 queue0 map0 unpublished caller-drop public-closed\n",
                static_cast<unsigned long long>(admission->originalPayload()->work().commands));
            return;
        }
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return issuers->hasOriginalRecordedCommand();
        }));
        wrong = execution;
        wrong.readbackCompletion = {};
        assert(Issuers::submitOriginal(issuers, wrong));
        const auto& originalSources = root->originalGraph()
                                          ->originalInputs()
                                          ->originalScalars()
                                          ->originalUsage()
                                          ->originalSources();
        assert(destinationSourceIndex < originalSources->sources().size());
        const auto destination = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
            originalSources->sources()[destinationSourceIndex]);
        auto native = rhi_webgpu::webGpuNativePayloadFor(*destination->originalObject());
        const auto saved = native->buffer;
        native->buffer = nullptr;
        assert(Issuers::submitOriginal(issuers, execution));
        native->buffer = saved;
        auto snapshot = results->snapshots()[0];
        std::weak_ptr<const Storage> retainedResults = results;
        bool mapInvoked = false;
        const bool mapFailure =
            mode == LifecycleCase::MapLoss || mode == LifecycleCase::MapUncertain;
        if (mapFailure)
            WebGpuNativeMapExecutionProbeFixture::instrument(static_cast<WGPUDevice>(nativeDevice),
                saved,
                mode == LifecycleCase::MapUncertain
                    ? WebGpuNativeMapExecutionProbeFixture::Probe::ThrowAfter
                    : WebGpuNativeMapExecutionProbeFixture::Probe::PendingLoss,
                &mapInvoked,
                [](void* value) noexcept {
                    *static_cast<bool*>(value) = true;
                    MapScopes::holdScopes();
                });
        results.reset();
        command.reset();
        assert(!retainedResults.expired());
        if (mode == LifecycleCase::Complete)
            custody.reset();
        if (mode == LifecycleCase::QueueRegistrationUncertain
            || mode == LifecycleCase::ScopeRegistrationUncertain) {
            using Probe = WebGpuNativeSubmissionProbeFixture;
            const bool queueRegistration = mode == LifecycleCase::QueueRegistrationUncertain;
            auto queue = wgpuDeviceGetQueue(static_cast<WGPUDevice>(nativeDevice));
            assert(queue);
            bool invoked = false;
            Probe::instrumentLoss(nativeDevice, queue, queueRegistration);
            if (queueRegistration)
                Scopes::holdScopes();
            else
                Probe::instrument(static_cast<WGPUDevice>(nativeDevice),
                    queue,
                    Probe::Probe::PartialScope,
                    &invoked,
                    [](void* value) noexcept {
                        *static_cast<bool*>(value) = true;
                    });
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                id = CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    return Issuers::submitOriginal(issuers, execution);
                });
            });
            if (queueRegistration)
                Scopes::restoreScopes();
            else {
                assert(invoked);
                Probe::restore();
            }
            Probe::restoreLoss();
            wgpuQueueRelease(queue);
            assert(id
                && core::StringView(id) == "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
            assert(root->status() == Root::Status::Invalidated && root->journalEmpty()
                && custody->pendingOriginalRecords() == 1u && !retainedResults.expired()
                && !snapshot->publishedBytes() && !issuers->hasOriginalCompletedSubmission());
            assert(Issuers::recordOriginal(issuers, execution) == id
                && Issuers::submitOriginal(issuers, execution) == id);
            if (queueRegistration) {
                Scopes::waitHeldScopes();
                Scopes::releaseScope(0u);
                Scopes::releaseScope(1u);
                emscripten_sleep(0u);
                assert(custody->pendingOriginalRecords() == 1u && !retainedResults.expired());
                Scopes::releaseScopes();
                emscripten_sleep(0u);
            }
            assert(custody->pendingOriginalRecords() == 1u && !retainedResults.expired()
                && !snapshot->publishedBytes());
            assert(EM_ASM_INT(
                {
                    // clang-format off
                        const d = WebGPU.getJsObject($0);
                        if (d !== Module.preinitializedWebGPUDevice)
                            return 0;
                        d.destroy();
                        return 1;
                    // clang-format on
                },
                nativeDevice));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return custody->physicalLossDelivered() && !custody->pendingOriginalRecords();
            }));
            assert(root->status() == Root::Status::Invalidated && root->journalEmpty()
                && !snapshot->publishedBytes() && !issuers->hasOriginalCompletedSubmission());
            issuers.reset();
            custody.reset();
            assert(retainedResults.expired());
            retainedResults.reset();
            snapshot.reset();
            assert(!resultQuota->liveBytes());
            std::printf(
                "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_OK %s same-original%llu authentic-listener-then-throw %s actual-device-loss foreign-callback-drain invalidated-before-drain journal-empty no-retry unpublished completion0 caller-drop public-closed\n",
                lifecycleName(mode),
                static_cast<unsigned long long>(admission->originalPayload()->work().commands),
                queueRegistration ? "scopes-released-no-queue-proof-retained"
                                  : "authentic-scope-queue-promises-held-through-loss");
            return;
        }
        bool submitInvoked = false;
        WGPUQueue uncertainQueue = nullptr;
        if (mode == LifecycleCase::SubmissionScopeUncertain)
            WebGpuNativeSubmissionProbeFixture::instrumentScopePush(nativeDevice);
        if (mode == LifecycleCase::SubmissionUncertain) {
            uncertainQueue = wgpuDeviceGetQueue(static_cast<WGPUDevice>(nativeDevice));
            assert(uncertainQueue);
            WebGpuNativeSubmissionProbeFixture::instrument(static_cast<WGPUDevice>(nativeDevice),
                uncertainQueue,
                WebGpuNativeSubmissionProbeFixture::Probe::ThrowAfter,
                &submitInvoked,
                [](void* value) noexcept {
                    *static_cast<bool*>(value) = true;
                });
        }
        if (mode != LifecycleCase::SubmissionScopeUncertain)
            Scopes::holdScopes();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            id = CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return Issuers::submitOriginal(issuers, execution);
            });
        });
        if (mode == LifecycleCase::SubmissionScopeUncertain) {
            WebGpuNativeSubmissionProbeFixture::restoreScopePush();
            assert(id
                && core::StringView(id) == "RHI-WEBGPU-CANONICAL-SUBMISSION-SCOPE-PUSH-UNCERTAIN");
            assert(Issuers::recordOriginal(issuers, execution) == id
                && Issuers::submitOriginal(issuers, execution) == id);
        } else
            Scopes::restoreScopes();
        if (mode == LifecycleCase::SubmissionUncertain) {
            WebGpuNativeSubmissionProbeFixture::restore();
            wgpuQueueRelease(uncertainQueue);
            assert(submitInvoked && id
                && core::StringView(id) == "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN");
            assert(Issuers::recordOriginal(issuers, execution) == id
                && Issuers::submitOriginal(issuers, execution) == id);
        } else if (mode != LifecycleCase::SubmissionScopeUncertain)
            require(id);
        if (mode != LifecycleCase::SubmissionScopeUncertain)
            Scopes::waitHeldScopes();
        assert(root->status()
                == (mode == LifecycleCase::SubmissionUncertain
                            || mode == LifecycleCase::SubmissionScopeUncertain
                        ? Root::Status::Invalidated
                        : Root::Status::Pending)
            && !retainedResults.expired() && !snapshot->publishedBytes());
        // Complete deliberately drops the caller's custody shell before submit;
        // the issuer/native callback cycle retains the original Result2 instead.
        if (mode != LifecycleCase::Complete)
            assert(custody->pendingOriginalRecords() == 1u);
        if (mode == LifecycleCase::SubmissionScopeUncertain) {
            emscripten_sleep(0u);
            assert(custody->pendingOriginalRecords() == 1u && !retainedResults.expired());
            assert(EM_ASM_INT(
                {
                    // clang-format off
                        const d = WebGPU.getJsObject($0);
                        if (d !== Module.preinitializedWebGPUDevice)
                            return 0;
                        d.destroy();
                        return 1;
                    // clang-format on
                },
                nativeDevice));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return custody->physicalLossDelivered() && !custody->pendingOriginalRecords();
            }));
            assert(root->status() == Root::Status::Invalidated && root->journalEmpty()
                && !snapshot->publishedBytes() && !issuers->hasOriginalCompletedSubmission());
            issuers.reset();
            custody.reset();
            assert(retainedResults.expired());
            retainedResults.reset();
            snapshot.reset();
            assert(!resultQuota->liveBytes());
            std::printf(
                "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_OK submission-scope-uncertain same-original%llu actual-push2-then-throw no-guessed-pop queue0 loss-only-retirement actual-device-loss invalidated-before-drain journal-empty no-retry unpublished completion0 caller-drop public-closed\n",
                static_cast<unsigned long long>(admission->originalPayload()->work().commands));
            return;
        }
        if (mode == LifecycleCase::SubmissionLoss || mode == LifecycleCase::SubmissionUncertain
            || mapFailure) {
            if (mapFailure) {
                Scopes::releaseScopes();
                if (mode == LifecycleCase::MapUncertain) {
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return mapInvoked && root->status() == Root::Status::Invalidated;
                    }));
                    const auto* terminal = issuers->submissionDiagnostic();
                    assert(terminal
                        && core::StringView(terminal)
                            == "RHI-WEBGPU-NATIVE-MAP-INVOCATION-UNCERTAIN"
                        && Issuers::recordOriginal(issuers, execution) == terminal
                        && Issuers::submitOriginal(issuers, execution) == terminal);
                } else {
                    assert(WebGpuAsyncWaitFixture::until([] {
                        return EM_ASM_INT({
                            // clang-format off
                                const p = Module.canonicalMapProbe;
                                return !!(p && p.pendingResolve);
                            // clang-format on
                        });
                    }));
                    assert(root->status() == Root::Status::Pending);
                }
                assert(mapInvoked && custody->pendingOriginalRecords() == 1u
                    && !retainedResults.expired() && !snapshot->publishedBytes());
                MapScopes::waitHeldScopes();
                MapScopes::restoreScopes();
                WebGpuNativeMapExecutionProbeFixture::restore(mode == LifecycleCase::MapLoss);
            }
            assert(EM_ASM_INT(
                {
                    // clang-format off
                        const d = WebGPU.getJsObject($0);
                        if (d !== Module.preinitializedWebGPUDevice)
                            return 0;
                        d.destroy();
                        return 1;
                    // clang-format on
                },
                nativeDevice));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return custody->physicalLossDelivered();
            }));
            assert(root->status() == Root::Status::Invalidated
                && custody->pendingOriginalRecords() == 1u && !retainedResults.expired()
                && !snapshot->publishedBytes() && !issuers->hasOriginalCompletedSubmission()
                && Issuers::submitOriginal(issuers, execution));
            if (mapFailure) {
                if (mode == LifecycleCase::MapLoss)
                    WebGpuNativeMapExecutionProbeFixture::retirePendingAfterLoss();
                MapScopes::releaseScope(0u);
                MapScopes::releaseScope(1u);
                emscripten_sleep(0u);
            } else {
                Scopes::releaseScope(0u);
                Scopes::releaseScope(1u);
                emscripten_sleep(0u);
            }
            assert(custody->pendingOriginalRecords() == 1u && !retainedResults.expired()
                && !snapshot->publishedBytes() && !issuers->hasOriginalCompletedSubmission());
            if (mapFailure)
                MapScopes::releaseScopes();
            else
                Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !custody->pendingOriginalRecords();
            }));
            assert(root->status() == Root::Status::Invalidated && root->journalEmpty()
                && !snapshot->publishedBytes() && !issuers->hasOriginalCompletedSubmission());
            issuers.reset();
            custody.reset();
            assert(retainedResults.expired());
            retainedResults.reset();
            snapshot.reset();
            assert(!resultQuota->liveBytes());
            std::printf(
                "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_OK %s same-original%llu actual-device-loss scopes2-held1 invalidated-before-drain journal-empty queue1 map%u no-retry unpublished completion0 caller-drop public-closed\n",
                lifecycleName(mode),
                static_cast<unsigned long long>(admission->originalPayload()->work().commands),
                unsigned(mapInvoked));
            return;
        }
        Scopes::releaseScope(0u);
        Scopes::releaseScope(1u);
        assert(!issuers->hasOriginalCompletedSubmission() && !snapshot->publishedBytes());
        Scopes::releaseScopes();
        const bool completed = WebGpuAsyncWaitFixture::until([&] {
            return issuers->hasOriginalCompletedSubmission();
        });
        if (!completed)
            std::fprintf(
                stderr, "geometry/readback completion: %s\n", issuers->submissionDiagnostic());
        assert(completed && root->status() == Root::Status::Committed && root->journalEmpty()
            && !root->validate());
        const auto* output = snapshot->publishedBytes();
        assert(output && snapshot->byteCapacity() == 512u);
        verifyOutput(output);
        assert(Issuers::recordOriginal(issuers, execution)
            && Issuers::submitOriginal(issuers, execution));
        issuers.reset();
        assert(retainedResults.expired());
        // The weak observer retains the quota-charged shared control block.
        // Release it before asserting final storage/snapshot retirement.
        retainedResults.reset();
        snapshot.reset();
        assert(!resultQuota->liveBytes());
        std::puts(completionMarker);
    }

public:
    template <class Publish>
    static void verify(rhi::GpuDevice& device,
        Publish&& publish,
        const std::shared_ptr<core::CpuMemoryQuota>& resources,
        LifecycleCase mode = LifecycleCase::Complete)
    {
        auto prepared = prepare(device, std::forward<Publish>(publish), resources);
        OriginalTypedColdAdmissionFixture::executeGeometry<true>(
            prepared.admission, prepared.initialized[0]);
        assert(prepared.admission->originalRoot()->status()
            == rhi::OriginalCommandStateTransaction::Status::Committed);
        prepared.admission.reset();
        prepared.initialized = {};
        for (const auto& quota : prepared.quotas)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_STREAM_GEOMETRY_RETIRE_OK original-owners-final-quotas-zero no-readback-or-public-authority");
        const auto composedQuotas =
            verifyComposedCustody(device, std::forward<Publish>(publish), resources, mode);
        for (const auto& quota : composedQuotas)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_GEOMETRY_READBACK_CUSTODY_RETIRE_OK original-owners18-quotas-zero no-record-map-public-authority");
        std::puts(
            "BINDLESS_RHI_GEOMETRY_READBACK_COLD5_RETIRE_OK original-owners20-quotas-zero no-command-map-public-authority");
        std::puts(
            "BINDLESS_RHI_GEOMETRY_READBACK_COMMAND5_RETIRE_OK original-owners20-quotas-zero no-encoder-map-public-authority");
        if (mode == LifecycleCase::Complete)
            std::puts(
                "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_RETIRE_OK original-owners21-quotas-zero same-root-result-completion public-closed");
        else
            std::printf(
                "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_RETIRE_OK %s original-owners21-quotas-zero unpublished completion0 public-closed\n",
                lifecycleName(mode));
    }
#endif
};
} // namespace arp::test
