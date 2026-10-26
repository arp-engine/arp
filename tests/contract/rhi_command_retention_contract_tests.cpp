#include "../fixtures/canonical_initialized_buffer_staging_contract_fixture.h"
#include "../fixtures/canonical_texture_content_creation_fixture.h"
#include "../fixtures/command_recording_order_fixture.h"
#include "../fixtures/command_scalar_arguments_fixture.h"
#include "../fixtures/command_usage_contract_fixture.h"
#include "../fixtures/original_buffer_state_fixture.h"
#include "../fixtures/original_command_admission_contract_fixture.h"
#include "../fixtures/original_command_buffer_upload_inputs_fixture.h"
#include "../fixtures/original_command_inputs_fixture.h"
#include "../fixtures/original_command_lifecycle_reservation_fixture.h"
#include "../fixtures/original_command_native_lifecycle_fixture.h"
#include "../fixtures/original_command_ordered_copy_attachment_fixture.h"
#include "../fixtures/original_command_queue_submission_contract_fixture.h"
#include "../fixtures/original_command_recording_seal_fixture.h"
#include "../fixtures/original_command_render_pass_inputs_fixture.h"
#include "../fixtures/original_command_resource_lease_fixture.h"
#include "../fixtures/original_command_sampler_custody_fixture.h"
#include "../fixtures/original_command_state_admission_contract_fixture.h"
#include "../fixtures/original_command_stream_copy_fixture.h"
#include "../fixtures/original_command_texture_readback_inputs_fixture.h"
#include "../fixtures/original_command_texture_upload_inputs_fixture.h"
#include "../fixtures/original_command_texture_view_source_fixture.h"
#include "../fixtures/original_command_typed_source_contract_fixture.h"
#include "../fixtures/original_descriptor_native_identity_fixture.h"
#include "../fixtures/original_device_lifetime_fixture.h"
#include "../fixtures/original_device_native_loss_observation_fixture.h"
#include "../fixtures/original_device_native_loss_observer_contract_fixture.h"
#include "../fixtures/original_geometry_readback_composition_fixture.h"
#include "../fixtures/original_observed_native_loss_binding_fixture.h"
#include "../fixtures/original_query_registration_fixture.h"
#include "../fixtures/original_resource_registration_fixture.h"
#include "../fixtures/original_texture_state_fixture.h"
#include "../fixtures/original_typed_attachment_dependency_probe_fixture.h"
#include "../fixtures/original_typed_callback_custody_fixture.h"
#include "../fixtures/original_typed_cold_command_fixture.h"
#include "../fixtures/original_typed_native_cold_contract_fixture.h"
#include "../fixtures/original_typed_native_execution_fixture.h"
#include "../fixtures/original_typed_native_loss_registration_fixture.h"
#include "../fixtures/original_typed_usage_projection_fixture.h"
#include "../fixtures/original_whole_descriptor_write_fixture.h"
#include "../fixtures/recorded_resource_state_sequence_fixture.h"
#include "../fixtures/resource_dependency_arguments_fixture.h"
#include "../fixtures/texture_readback_input_quota_fixture.h"
#include "../fixtures/texture_readback_ownership_quota_fixture.h"
#include "../fixtures/texture_readback_retained_source_fixture.h"
#include "../fixtures/texture_upload_input_quota_fixture.h"
#include "../fixtures/typed_shader_usage_scope_contract_fixture.h"
#include "../fixtures/webgpu_canonical_callback_custody_fixture.h"
#include "../fixtures/webgpu_canonical_cold_admission_fixture.h"
#include "../fixtures/webgpu_canonical_cold_command_fixture.h"
#include "../fixtures/webgpu_canonical_copy_pass_lowering_fixture.h"
#include "../fixtures/webgpu_canonical_issuer_preparation_fixture.h"
#include "../fixtures/webgpu_canonical_loss_bound_closure_fixture.h"
#include "../fixtures/webgpu_canonical_native_closure_fixture.h"
#include "../fixtures/webgpu_canonical_ordered_copy_attachment_metadata_fixture.h"
#include "../fixtures/webgpu_canonical_scoped_recording_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_backend_internal.h"
#include "rhi/frontend/command_retention_storage.h"
#include "rhi/frontend/command_retention_validation.h"

#include <array>
#include <cassert>
#include <cstring>

#ifdef NDEBUG
#error Command retention contract tests require assertions
#endif

namespace {
const char* lastDiagnostic = nullptr;

void captureDiagnostic(arp::rhi::RhiSeverity, const char* message, void*)
{
    lastDiagnostic = message;
}

void testProjectedOwnerStorage()
{
    using namespace arp;
    using rhi::common::CommandRetentionStorage;
    rhi::CommandRetentionBudget budget{1u, true, 2u, 0u, 0u, 0u, 4096u};
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    const char* diagnostic = nullptr;
    auto storage = CommandRetentionStorage::prepare(budget, quota, diagnostic);
    assert(storage && !diagnostic);
    auto first = core::makeShared<unsigned>(3u), second = core::makeShared<unsigned>(7u),
         third = core::makeShared<unsigned>(11u);

    struct Binding {
        unsigned location;
        std::shared_ptr<const void> owner;
    };

    std::array<Binding, 4u> bindings{{{1u, first}, {3u, second}, {5u, first}, {7u, third}}};
    auto ownerAt = [&](std::size_t index) noexcept -> const std::shared_ptr<const void>& {
        return bindings[index].owner;
    };
    const auto charged = quota->liveBytes(),
               allocations = core::defaultCpuAllocator().stats().allocationCount;
    core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - charged);
    auto rejected = storage->retainResourceProjection(bindings.size(), ownerAt);
    assert(rejected && std::strcmp(rejected, "RHI-COMMAND-RETENTION-OWNER-CAPACITY") == 0
        && !storage->resourceOwnerCount());
    bindings.back().owner.reset();
    rejected = storage->retainResourceProjection(bindings.size(), ownerAt);
    assert(rejected && !storage->resourceOwnerCount());
    bindings.back().owner = second;
    assert(!storage->preflightResourceProjection(bindings.size(), ownerAt)
        && !storage->resourceOwnerCount());
    assert(!storage->retainResourceProjection(bindings.size(), ownerAt)
        && storage->resourceOwnerCount() == 2u);
    assert(!storage->retainResourceProjection(bindings.size(), ownerAt));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations
        && quota->liveBytes() == quota->limitBytes());
    pressure.release();
    assert(quota->liveBytes() == charged);
    std::weak_ptr<unsigned> observed = second;
    bindings = {};
    second.reset();
    assert(!observed.expired());
    assert(storage->sealForSubmission());
    rejected = storage->preflightResourceProjection(0u, ownerAt);
    assert(rejected && std::strcmp(rejected, "RHI-COMMAND-RETENTION-RECORDING-CLOSED") == 0);
    rejected = storage->retainResourceProjection(0u, ownerAt);
    assert(rejected && std::strcmp(rejected, "RHI-COMMAND-RETENTION-RECORDING-CLOSED") == 0);
    assert(storage->abandonUnsubmitted() && observed.expired() && quota->liveBytes() == charged);
    storage.reset();
    assert(
        !quota->liveBytes() && core::defaultCpuAllocator().stats().allocationCount == allocations);
}

void testPreparedStorage()
{
    using namespace arp;
    using rhi::common::CommandRetainedQueryRecord;
    using rhi::common::CommandRetentionStorage;
    rhi::CommandRetentionBudget budget{1u, true, 2u, 1u, 2u, 1u, 4096u};
    const char* diagnostic = nullptr;
    const auto expectDiagnostic = [](const char* actual, const char* expected) {
        assert(actual && std::strcmp(actual, expected) == 0);
    };
    assert(!CommandRetentionStorage::prepare(budget, {}, diagnostic));
    expectDiagnostic(diagnostic, "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    for (const auto bytes : {0u, 4095u}) {
        auto wrongQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        assert(!CommandRetentionStorage::prepare(budget, wrongQuota, diagnostic));
        expectDiagnostic(diagnostic, "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
        assert(wrongQuota->liveBytes() == 0u);
    }
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    auto storage = CommandRetentionStorage::prepare(budget, quota, diagnostic);
    assert(storage && !diagnostic && storage->state() == CommandRetentionStorage::State::Recording);
    const auto charged = quota->liveBytes();
    assert(charged > 0u && charged <= budget.maxCpuMetadataBytes);
    // A root allocation can succeed while a later reservation fails. Use the
    // measured allocation charge, not an estimated struct/native byte size.
    auto smallerBudget = budget;
    smallerBudget.maxCpuMetadataBytes = charged - 1u;
    auto smallerQuota = core::makeShared<core::CpuMemoryQuota>(smallerBudget.maxCpuMetadataBytes);
    assert(!CommandRetentionStorage::prepare(smallerBudget, smallerQuota, diagnostic));
    expectDiagnostic(diagnostic, "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED");
    assert(smallerQuota->liveBytes() == 0u && smallerQuota->peakBytes() > 0u);
    smallerBudget.maxCpuMetadataBytes = 1u;
    smallerQuota = core::makeShared<core::CpuMemoryQuota>(1u);
    assert(!CommandRetentionStorage::prepare(smallerBudget, smallerQuota, diagnostic));
    assert(smallerQuota->liveBytes() == 0u);

    auto first = core::makeShared<unsigned>(1u), second = core::makeShared<unsigned>(2u);
    auto third = core::makeShared<unsigned>(3u);
    auto pool = core::makeShared<unsigned>(4u), otherPool = core::makeShared<unsigned>(5u);
    std::array<std::shared_ptr<const void>, 3u> resources{first, second, third};
    CommandRetainedQueryRecord timestamp{pool, 0u}, statistics{pool, 0u};
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!storage->preflightTimestamp(timestamp, resources.data(), 2u));
    assert(storage->resourceOwnerCount() == 0u && storage->queryPoolOwnerCount() == 0u
        && storage->timestamps().empty());
    expectDiagnostic(storage->admitTimestamp(timestamp, resources.data(), resources.size()),
        "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
    assert(storage->resourceOwnerCount() == 0u && storage->queryPoolOwnerCount() == 0u
        && storage->timestamps().empty());
    expectDiagnostic(
        storage->admitTimestamp({{}, 0u}), "RHI-COMMAND-RETENTION-QUERY-OWNER-MISSING");
    assert(!storage->admitTimestamp(timestamp, resources.data(), 2u));
    expectDiagnostic(storage->admitTimestamp(timestamp), "RHI-COMMAND-RETENTION-QUERY-DUPLICATE");
    expectDiagnostic(
        storage->admitStatistics({otherPool, 0u}), "RHI-COMMAND-RETENTION-QUERY-CAPACITY");
    assert(storage->statistics().empty() && storage->queryPoolOwnerCount() == 1u);
    assert(!storage->admitTimestamp({pool, 1u}));
    expectDiagnostic(storage->admitTimestamp({pool, 2u}), "RHI-COMMAND-RETENTION-QUERY-CAPACITY");
    assert(!storage->admitStatistics(statistics)); // Same pool/index, distinct query type.
    expectDiagnostic(storage->admitStatistics({pool, 1u}), "RHI-COMMAND-RETENTION-QUERY-CAPACITY");
    assert(!storage->retainResources(resources.data(), 2u));
    expectDiagnostic(
        storage->retainResources(resources.data(), 3u), "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
    assert(storage->resourceOwnerCount() == 2u && storage->timestamps().size() == 2u
        && storage->statistics().size() == 1u && quota->liveBytes() == charged);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);

    std::weak_ptr<unsigned> firstWeak = first, poolWeak = pool;
    first.reset();
    pool.reset();
    resources = {};
    timestamp = {};
    statistics = {};
    assert(!firstWeak.expired() && !poolWeak.expired());
    assert(!storage->markSubmitted() && !storage->retireAfterCompletion(true));
    assert(storage->sealForSubmission() && !storage->sealForSubmission());
    expectDiagnostic(
        storage->preflightResources(nullptr, 0u), "RHI-COMMAND-RETENTION-RECORDING-CLOSED");
    expectDiagnostic(
        storage->admitTimestamp({otherPool, 0u}), "RHI-COMMAND-RETENTION-RECORDING-CLOSED");
    assert(storage->markSubmitted() && !storage->markSubmitted());
    auto completionOwner = std::move(storage);
    assert(!storage && !completionOwner->abandonUnsubmitted()
        && !completionOwner->retireAfterCompletion(false));
    assert(!firstWeak.expired() && !poolWeak.expired());
    assert(completionOwner->retireAfterCompletion(true)); // CPU ownership proof only.
    assert(firstWeak.expired() && poolWeak.expired());
    assert(completionOwner->resourceOwnerCount() == 0u
        && completionOwner->queryPoolOwnerCount() == 0u && completionOwner->timestamps().empty()
        && completionOwner->statistics().empty());
    assert(!completionOwner->retireAfterCompletion(true) && !completionOwner->abandonUnsubmitted());
    assert(quota->liveBytes() == charged
        && core::defaultCpuAllocator().stats().allocationCount == allocations);
    completionOwner.reset();
    assert(quota->liveBytes() == 0u);

    for (const bool sealed : {false, true}) {
        auto cancelled = CommandRetentionStorage::prepare(budget, quota, diagnostic);
        auto cancelledResource = core::makeShared<unsigned>(6u);
        auto cancelledPool = core::makeShared<unsigned>(7u);
        std::shared_ptr<const void> owner = cancelledResource;
        assert(!cancelled->admitStatistics({cancelledPool, 0u}, &owner, 1u));
        std::weak_ptr<unsigned> resourceWeak = cancelledResource, queryWeak = cancelledPool;
        owner.reset();
        cancelledResource.reset();
        cancelledPool.reset();
        if (sealed)
            assert(cancelled->sealForSubmission());
        assert(cancelled->abandonUnsubmitted() && resourceWeak.expired() && queryWeak.expired());
        assert(quota->liveBytes() > 0u);
        cancelled.reset();
        assert(quota->liveBytes() == 0u);
    }
    // Zero disables each category. A query-only batch never partially retains
    // resources when its independent record or pool budget rejects it.
    auto disabled = budget;
    disabled.maxTimestampWrites = 0u;
    disabled.maxResourceOwners = 0u;
    auto limited = CommandRetentionStorage::prepare(disabled, quota, diagnostic);
    std::shared_ptr<const void> owner = second;
    expectDiagnostic(limited->admitTimestamp({otherPool, 0u}, &owner, 1u),
        "RHI-COMMAND-RETENTION-QUERY-CAPACITY");
    expectDiagnostic(limited->admitStatistics({otherPool, 0u}, &owner, 1u),
        "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
    assert(limited->queryPoolOwnerCount() == 0u && limited->statistics().empty());
    assert(!limited->admitStatistics({otherPool, 0u}));
    limited.reset();
    assert(quota->liveBytes() == 0u);
}
} // namespace

int main()
{
    {
        using namespace arp::rhi;
        auto device = createNullDevice();
        assert(device);
        BufferDesc desc;
        desc.sizeBytes = 16u;
        desc.usage = ResourceUsage::IndexBuffer | ResourceUsage::CopyDst;
        desc.initialState = ResourceState::CopyDst;
        auto buffer = device->createBuffer(desc);
        assert(buffer);
        ResourceBarrierDesc barrier;
        barrier.resource = buffer->id();
        barrier.before = ResourceState::CopyDst;
        barrier.after = ResourceState::ShaderRead;
        barrier.sourceStages = PipelineStage::Transfer;
        barrier.destinationStages = PipelineStage::VertexInput;
        barrier.sourceAccess = ResourceAccess::TransferWrite;
        barrier.destinationAccess = ResourceAccess::IndexRead;
        auto encoder = device->createCommandEncoder({});
        assert(encoder);
        auto positive = encoder->transition(*buffer, barrier);
        if (!positive.success)
            std::fprintf(stderr, "geometry Null barrier: %s\n", positive.diagnostics.c_str());
        assert(positive.success);
        encoder.reset();
        assert(buffer->state() == ResourceState::CopyDst);
        desc.initialState = ResourceState::ShaderRead;
        auto input = device->createBuffer(desc);
        assert(input);
        ResourceMemoryBarrierDesc memory;
        memory.resource = input->id();
        memory.sourceStages = memory.destinationStages = PipelineStage::VertexInput;
        memory.sourceAccess = memory.destinationAccess = ResourceAccess::IndexRead;
        encoder = device->createCommandEncoder({});
        assert(encoder);
        assert(encoder->memoryBarrier(*input, memory).success);
        encoder.reset();
        auto broken = barrier;
        broken.destinationStages = PipelineStage::VertexShader;
        encoder = device->createCommandEncoder({});
        assert(encoder);
        auto result = encoder->transition(*buffer, broken);
        assert(!result.success && buffer->state() == ResourceState::CopyDst);
        assert(result.diagnostics == "RHI-ACCESS-STAGE-MISMATCH");
        encoder.reset();
        desc.usage = ResourceUsage::StorageBuffer;
        auto wrong = device->createBuffer(desc);
        assert(wrong);
        barrier.resource = wrong->id();
        encoder = device->createCommandEncoder({});
        assert(encoder);
        result = encoder->transition(*wrong, barrier);
        assert(!result.success && wrong->state() == ResourceState::ShaderRead);
        assert(result.diagnostics == "RHI-ORIGINAL-ACCESS-BUFFER-USAGE");
        encoder.reset();
        TextureDesc textureDesc;
        textureDesc.extent = {1u, 1u, 1u};
        textureDesc.format = TextureFormat::Rgba8Unorm;
        textureDesc.usage = TextureUsage::Sampled;
        textureDesc.initialState = ResourceState::ShaderRead;
        auto texture = device->createTexture(textureDesc);
        assert(texture);
        barrier.resource = texture->id();
        encoder = device->createCommandEncoder({});
        assert(encoder);
        result = encoder->transition(*texture, barrier);
        assert(!result.success && texture->state() == ResourceState::ShaderRead);
        assert(result.diagnostics == "RHI-ORIGINAL-ACCESS-TEXTURE-USAGE");
        encoder.reset();
        memory.resource = texture->id();
        encoder = device->createCommandEncoder({});
        assert(encoder);
        result = encoder->memoryBarrier(*texture, memory);
        assert(!result.success && texture->state() == ResourceState::ShaderRead);
        assert(result.diagnostics == "RHI-ORIGINAL-ACCESS-TEXTURE-USAGE");
    }
    arp::test::OriginalDescriptorNativeIdentityFixture::verifyContract();
    arp::test::OriginalWholeDescriptorWriteFixture::verifyContract();
    {
        auto validator = arp::rhi::createNullDevice();
        assert(validator);
        arp::test::OriginalWholeDescriptorWriteFixture::verifyValidator(*validator);
    }
    arp::test::OriginalCommandTypedSourceContractFixture::verifyContract();
#if !defined(__EMSCRIPTEN__)
    arp::test::OriginalPipelineFactoryRegistrationFixture::verifyPipelines();
    arp::test::OriginalPipelineFullDefinitionFixture::verifyHost();
#endif
    arp::test::OriginalCommandTypedSourceContractFixture::verifyOriginalResources([]() {
        return arp::rhi::createNullDevice();
    });
    arp::test::OriginalCommandAdmissionContractFixture::verify();
    arp::test::OriginalCommandStateAdmissionContractFixture::verify();
    arp::test::RecordedResourceStateSequenceFixture::verify();
    arp::test::CommandUsageContractFixture::verify();
    arp::test::CommandRecordingOrderFixture::verify();
    arp::test::ResourceDependencyArgumentsFixture::verify();
    arp::test::CommandVariableArgumentsFixture::verify();
    testPreparedStorage();
    testProjectedOwnerStorage();
    using namespace arp::rhi;
    using semantics::CommandRetentionBudgetLowering;
    CommandRetentionBudget request{1u, true, 2u, 1u, 2u, 0u, 4096u};
    // Synthetic inputs test value lowering only; no device execution claim.
    CommandRetentionCapabilities native{1u, CapabilityMode::Native, true, true, false};
    assert(CommandRetentionBudgetLowering::plan(request, native).executable);
    auto equivalent = native;
    equivalent.mode = CapabilityMode::Equivalent;
    assert(CommandRetentionBudgetLowering::plan(request, equivalent).executable);
    auto validator = equivalent;
    validator.validatorOnly = true;
    validator.completionRetiresOwners = false;
    const auto validation = CommandRetentionBudgetLowering::plan(request, validator);
    assert(validation.success && !validation.executable);
    const auto reject = [&](const CommandRetentionBudget& budget,
                            const CommandRetentionCapabilities& caps,
                            const char* id) {
        const auto plan = CommandRetentionBudgetLowering::plan(budget, caps);
        assert(!plan.success && !plan.executable && plan.mode == CapabilityMode::DiagnosticFailure);
        assert(std::strcmp(plan.diagnosticId, id) == 0);
    };
    auto invalid = request;
    invalid.contractVersion = 0u;
    reject(invalid, native, "RHI-COMMAND-RETENTION-VERSION");
    invalid = request;
    invalid.optIn = false;
    reject(invalid, native, "RHI-COMMAND-RETENTION-OPT-IN-REQUIRED");
    invalid = request;
    invalid.maxCpuMetadataBytes = 0u;
    reject(invalid, native, "RHI-COMMAND-RETENTION-CPU-BUDGET");
    invalid = request;
    invalid.maxQueryPools = 0u;
    reject(invalid, native, "RHI-COMMAND-RETENTION-QUERY-POOL-BUDGET");
    invalid = request;
    invalid.maxTimestampWrites = 0u;
    invalid.maxPipelineStatisticsWrites = 1u;
    invalid.maxQueryPools = 0u;
    reject(invalid, native, "RHI-COMMAND-RETENTION-QUERY-POOL-BUDGET");
    auto unavailable = native;
    unavailable.boundedRecordingPayload = false;
    reject(request, unavailable, "RHI-COMMAND-RETENTION-PAYLOAD-MISSING");
    unavailable = native;
    unavailable.completionRetiresOwners = false;
    reject(request, unavailable, "RHI-COMMAND-RETENTION-COMPLETION-MISSING");
    for (const auto mode : {CapabilityMode::Degraded, CapabilityMode::DiagnosticFailure}) {
        unavailable = native;
        unavailable.mode = mode;
        reject(request, unavailable, "RHI-COMMAND-RETENTION-UNSUPPORTED");
    }
    unavailable = native;
    unavailable.validatorOnly = true;
    reject(request, unavailable, "RHI-COMMAND-RETENTION-UNSUPPORTED");
    CommandEncoderDesc desc;
    assert(!desc.retentionBudget);
    auto device = createNullDevice();
    assert(device);
    arp::test::OriginalTypedDependencyDecisionFixture::verifyContract();
    arp::test::OriginalTypedNativePayloadFixture::verifyContract();
    arp::test::OriginalTypedColdAdmissionFixture::verifyContract();
    arp::test::OriginalTypedColdCommandFixture::verifyContract();
    arp::test::OriginalDeviceNativeLossObserverContractFixture::verify();
    arp::test::OriginalObservedNativeLossBindingFixture::verifyContract();
    arp::test::OriginalTypedCallbackCustodyFixture::verifyContract();
    arp::test::OriginalTypedIssuerPreparationFixture::verifyContract();
    arp::test::OriginalTypedNativeExecutionFixture::verifyContract();
    arp::test::OriginalTypedAttachmentDependencyProbeFixture::verifyContract();
    arp::test::OriginalTypedNativeColdContractFixture::verifyContract();
    arp::test::OriginalGeometryReadbackCompositionFixture::verifyContract();
    arp::test::TypedShaderUsageScopeContractFixture::verifyContract();
    arp::test::OriginalTypedUsageProjectionFixture::verifyContract();
    arp::test::OriginalTypedNativeLossRegistrationFixture::verifyContract();
    arp::test::OriginalTypedNativeLossRegistrationFixture::verifyGate([]() {
        return createNullDevice();
    });
    arp::test::OriginalCommandStreamCopyFixture::verify([]() {
        return createNullDevice();
    });
    arp::test::CanonicalInitializedBufferStagingContractFixture::verify();
    arp::test::OriginalCommandOrderedCopyAttachmentFixture::verifyContract();
    arp::test::WebGpuCanonicalOrderedCopyAttachmentMetadataFixture::verifyMetadata([]() {
        return createNullDevice();
    });
    arp::test::OriginalDeviceNativeLossObservationFixture::verifyMissing([]() {
        return createNullDevice();
    });
    arp::test::OriginalCommandLifecycleReservationFixture::verify([]() {
        return createNullDevice();
    });
    arp::test::OriginalCommandRecordingSealFixture::verify([]() {
        return createNullDevice();
    });
    arp::test::OriginalCommandNativeLifecycleFixture::verify([]() {
        return createNullDevice();
    });
    arp::test::WebGpuCanonicalNativeClosureFixture::verifyContract();
    arp::test::WebGpuCanonicalColdAdmissionFixture::verifyContract();
    arp::test::WebGpuCanonicalColdCommandFixture::verifyContract();
    arp::test::WebGpuCanonicalCallbackCustodyFixture::verifyContract();
    arp::test::WebGpuCanonicalCopyPassLoweringFixture::verifyContract();
    arp::test::WebGpuCanonicalIssuerPreparationFixture::verifyContract();
    arp::test::WebGpuCanonicalScopedRecordingFixture::verifyContract();
    arp::test::OriginalCommandQueueSubmissionContractFixture::verifyContract();
#if !defined(__EMSCRIPTEN__)
    arp::test::WebGpuCanonicalColdAdmissionFixture::verify([]() {
        return createNullDevice();
    });
    arp::test::WebGpuCanonicalCopyPassLoweringFixture::verifyNullGraph([]() {
        return createNullDevice();
    });
    arp::test::WebGpuCanonicalLossBoundClosureFixture::verify([]() {
        return createNullDevice();
    });
    arp::test::WebGpuCanonicalNativeClosureFixture::verify([]() {
        return createNullDevice();
    });
#endif
    arp::test::CanonicalTextureContentCreationFixture::verifyContract([]() {
        return createNullDevice();
    });
    arp::test::OriginalCommandOrderedUploadContractFixture::verifyContract();
    arp::test::OriginalDeviceLifetimeFixture::verify([]() {
        return createNullDevice();
    });
    arp::test::OriginalResourceRegistrationFixture::verify([]() {
        return createNullDevice();
    });
    {
        auto queryDevice = createNullDevice();
        arp::test::OriginalQueryRegistrationFixture::verifyRejected(*queryDevice);
    }
    arp::test::OriginalCommandSamplerCustodyFixture::verify([]() {
        return createNullDevice();
    });
    arp::test::OriginalBufferStateFixture::verify(
        []() {
            return createNullDevice();
        },
        [](GpuBuffer& buffer,
            ResourceState before,
            ResourceState after,
            arp::core::MutationJournal* journal) {
            auto* original = dynamic_cast<null_backend::StatefulResource*>(&buffer);
            assert(original);
            if (journal && !original->beginStateRecording(*journal))
                return ResourceBarrierResult{false, "RHI-RESOURCE-STATE-RECORDING-CONFLICT"};
            return original->transitionState(before, after, journal);
        });
    arp::test::OriginalTextureStateFixture::verify(
        []() {
            return createNullDevice();
        },
        [](GpuTexture& texture,
            const TextureSubresourceRange& range,
            ResourceState before,
            ResourceState after,
            arp::core::MutationJournal* journal) {
            auto* original = dynamic_cast<null_backend::StatefulTextureResource*>(&texture);
            assert(original);
            return original->transitionSubresourceState(range, before, after, journal);
        },
        true);
    arp::test::OriginalCommandResourceLeaseFixture::verify(*device);
    arp::test::OriginalCommandInputsFixture::verify(*device);
    arp::test::OriginalCommandTextureViewSourceFixture::verify(*device);
    arp::test::OriginalCommandRenderPassInputsFixture::verify(*device);
    arp::test::OriginalCommandTextureUploadInputsFixture::verify(*device);
    arp::test::OriginalCommandBufferUploadInputsFixture::verify(*device);
    arp::test::TextureReadbackOwnershipQuotaFixture::verify(*device);
    arp::test::OriginalCommandTextureReadbackInputsFixture::verify(*device);
    arp::test::TextureReadbackRetainedSourceFixture::verify(*device);
    arp::test::TextureUploadInputQuotaFixture::verify();
    arp::test::TextureReadbackInputQuotaFixture::verify();
    assert(device->capabilities().commandRetention.contractVersion == 0u);
    desc.retentionBudget = request;
    reject(request, device->capabilities().commandRetention, "RHI-COMMAND-RETENTION-UNSUPPORTED");
    RhiErrorHandler::get().setCallback(captureDiagnostic);
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    assert(!device->createCommandEncoder(desc));
    assert(lastDiagnostic && std::strcmp(lastDiagnostic, "RHI-COMMAND-RETENTION-UNSUPPORTED") == 0);
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    desc.retentionBudget->optIn = false;
    assert(!device->createCommandEncoder(desc));
    assert(std::strcmp(lastDiagnostic, "RHI-COMMAND-RETENTION-OPT-IN-REQUIRED") == 0);
    RhiErrorHandler::get().setCallback(nullptr);
    desc.retentionBudget.reset();
    assert(device->createCommandEncoder(desc)); // Requests no new bounded contract.
}
