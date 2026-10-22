#include "core/diagnostics/diagnostic_error.h"
#include "renderer/gi/ddgi_renderer.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/compute_record_failure.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <cassert>
#include <iostream>
#include <string_view>

namespace {

void diagnostic_owns_first_failure()
{
    using namespace arp;
    core::requireSuccessfulResult(rhi::CommandValidationResult{true, "warning", 0u}, "fallback");
    bool continued = false;
    try {
        rhi::CommandValidationResult result{false, "original-command-failure", 0u};
        core::requireSuccessfulResult(result, "must-not-replace-original");
        continued = true;
    } catch (const core::DiagnosticError& error) {
        // The producer result and its string have already been destroyed.
        assert(std::string_view(error.what()) == "original-command-failure");
    }
    assert(!continued);
    bool fallbackCaught = false;
    try {
        core::requireSuccessfulResult(rhi::CommandValidationResult{}, "empty-producer-failure");
    } catch (const core::DiagnosticError& error) {
        fallbackCaught = std::string_view(error.what()) == "empty-producer-failure";
    }
    assert(fallbackCaught);
}

void all_recording_entries_reject_unready_renderer()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    assert(device);
    tests::CapabilityOverrideDevice wrapper(*device, nullptr);
    renderer::DdgiRenderer ddgi(wrapper);
    auto texture = device->createTexture({"RecordGuard",
        {4u, 4u, 1u},
        rhi::TextureFormat::Rgba16Float,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined});
    assert(texture);
    renderer::DdgiPushConstants pc{};
    for (unsigned lifecycle = 0; lifecycle != 6; ++lifecycle) {
        if (lifecycle == 1) {
            // A real failed create must not make recording callable.
            const auto created = ddgi.create({}, {}, 1u, 1u, 1u, 4u, 4u);
            assert(!created.success);
            assert(created.diagnostics == "DDGI-TERRAIN-SURFACE-INPUT-INVALID");
        } else if (lifecycle == 2) {
            ddgi.destroy();
        } else if (lifecycle == 3) {
            assert(
                ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                        {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                        2u,
                        2u,
                        2u,
                        4u,
                        4u)
                    .success);
            renderer::DdgiSceneBvhTriangle triangle{};
            triangle.v0[0] = -1;
            triangle.v1[0] = 1;
            triangle.v2[2] = 1;
            renderer::DdgiSceneBvhInput input;
            input.triangles = &triangle;
            input.triangleCount = 1u;
            input.staticGeometryRevision = input.dynamicGeometryRevision = 1u;
            wrapper.failBufferWrites("DdgiSceneBvhLeaves", true);
            const auto failed = ddgi.updateSceneBvh(input);
            assert(!failed.success
                && failed.diagnostic.find("TEST-BUFFER-UPLOAD-FAILED-AFTER-WRITE")
                    != std::string_view::npos);
        } else if (lifecycle >= 4) {
            wrapper.failBufferWrites({});
            assert(
                ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                        {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                        2u,
                        2u,
                        2u,
                        4u,
                        4u)
                    .success);
            wrapper.failBufferWrites(
                lifecycle == 4 ? "DdgiSkyEnvironmentUbo" : "DdgiCsmShadowUbo", true);
            renderer::DdgiSkyEnvironmentDesc sky;
            sky.horizonRadiance[0] = 0.75f;
            renderer::DdgiCsmShadowDesc csm;
            csm.lightViewProjection[0][0] = 0.75f;
            const auto failed =
                lifecycle == 4 ? ddgi.setSkyEnvironment(sky) : ddgi.setCsmShadow(csm);
            assert(!failed.success
                && failed.diagnostics.find("TEST-BUFFER-UPLOAD-FAILED-AFTER-WRITE")
                    != std::string_view::npos);
        }
        const auto submittedBefore = device->queue().submitCount();
        if (lifecycle < 3)
            assert(submittedBefore == 0u);
        for (unsigned entry = 0; entry != 9; ++entry) {
            auto encoder = device->createCommandEncoder({"DdgiRecordingGuard"});
            assert(encoder);
            render_graph::RenderContext context(*device, *encoder);
            const auto history = ddgi.currentReadIndex();
            bool rejected = false;
            try {
                switch (entry) {
                case 0:
                    ddgi.recordHistoryCopyPass(context, *texture, *texture, *texture, *texture);
                    break;
                case 1:
                    ddgi.recordProbeBorderSourceCopyPass(
                        context, *texture, *texture, *texture, *texture);
                    break;
                case 2:
                    ddgi.recordProbeRelocatePass(context, *texture, *texture, pc, 0u);
                    break;
                case 3:
                    ddgi.recordProbeTracePass(context, *texture, *texture, pc, 0u);
                    break;
                case 4:
                    ddgi.recordProbeUpdatePass(context, *texture, *texture, pc, 0u);
                    break;
                case 5:
                    ddgi.recordProbeBlendIrradiancePass(context, pc, 0u);
                    break;
                case 6:
                    ddgi.recordProbeBlendDistancePass(context, pc, 0u);
                    break;
                case 7:
                    ddgi.recordProbeBorderUpdatePass(context, pc, 0u);
                    break;
                case 8:
                    ddgi.recordResolvePass(context, *texture, *texture, pc, 0u);
                    break;
                }
            } catch (const core::DiagnosticError& error) {
                const auto expected = lifecycle == 3 ? "DDGI-BVH-INPUT-INVALID"
                    : lifecycle == 4                 ? "DDGI-SKY-INPUT-INVALID"
                    : lifecycle == 5                 ? "DDGI-CSM-INPUT-INVALID"
                                                     : "DDGI-RECORDING-NOT-READY";
                rejected = std::string_view(error.what()) == expected;
            }
            assert(rejected);
            assert(ddgi.currentReadIndex() == history);
            assert(texture->state() == rhi::ResourceState::Undefined);
            auto commands = encoder->finish();
            assert(commands && commands->commandCount() == 0u);
            assert(device->queue().submitCount() == submittedBefore);
        }
    }
}

void partial_material_write_must_not_publish_cache_success()
{
    using namespace arp;
    auto native = rhi::createNullDevice();
    tests::CapabilityOverrideDevice wrapper(*native, nullptr);
    auto* device = &wrapper;
    renderer::DdgiRenderer ddgi(*device);
    const auto created = ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
        {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
        2u,
        2u,
        2u,
        4u,
        4u);
    if (!created.success)
        std::cerr << created.diagnostics << '\n';
    assert(created.success);
    renderer::DdgiPushConstants scheduleInput{};
    auto completionOwner = core::makeShared<core::UniquePtr<rhi::GpuFence>>(device->createFence());
    std::shared_ptr<rhi::GpuFence> completion(completionOwner, completionOwner->get());
    assert(completion);
    completion->signal(); // Explicit Null acceptance/completion simulation below.
    for (unsigned axis = 0u; axis != 3u; ++axis)
        scheduleInput.probeGridSpacingStrength[axis] = 1.0f;
    scheduleInput.updateParams[0] = 2.0f;
    assert(ddgi.acceptedProbeScheduleGeneration() == 0u);
    assert(!ddgi.acceptPreparedHistory({}, completion));
    assert(ddgi.prepareProbeUpdateSchedule(scheduleInput, 0u).success);
    const auto staleReceipt = ddgi.preparedHistoryToken();
    assert(ddgi.telemetry().newlyExposedProbeCount == 8u);
    assert(ddgi.prepareProbeUpdateSchedule(scheduleInput, 0u).success);
    const auto preparedReceipt = ddgi.preparedHistoryToken();
    assert(!ddgi.isPreparedHistory(staleReceipt)
        && !ddgi.acceptPreparedHistory(staleReceipt, completion));
    assert(!ddgi.acceptPreparedHistory(
        {preparedReceipt.ownerId + 1u, preparedReceipt.generation}, completion));
    assert(ddgi.isPreparedHistory(preparedReceipt));
    assert(ddgi.telemetry().newlyExposedProbeCount == 8u);
    assert(ddgi.acceptedProbeScheduleGeneration() == 0u);
    auto depth = device->createTexture({"Depth",
        {4u, 4u, 1u},
        rhi::TextureFormat::D32Float,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::ShaderRead});
    auto normal = device->createTexture({"Normal",
        {4u, 4u, 1u},
        rhi::TextureFormat::Rgba16Float,
        rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::ShaderRead});
    assert(depth && normal);
    auto original = device->createTextureView(
        *normal, {"OriginalMaterial", rhi::TextureFormat::Rgba16Float, rhi::TextureUsage::Sampled});
    auto replacement = device->createTextureView(*normal,
        {"PartialReplacement", rhi::TextureFormat::Rgba16Float, rhi::TextureUsage::Sampled});
    auto invalid = device->createTextureView(
        *normal, {"ExpiredMaterial", rhi::TextureFormat::Rgba16Float, rhi::TextureUsage::Sampled});
    auto sampler = device->createSampler({});
    assert(original && replacement && invalid && sampler);
    ddgi.setMaterialAlbedo(0u, *original, *sampler);
    ddgi.setMaterialAlbedo(1u, *original, *sampler);
    renderer::DdgiPushConstants pc{};
    const auto record = [&] {
        auto encoder = device->createCommandEncoder({"ResolveRetry"});
        assert(encoder);
        render_graph::RenderContext context(*device, *encoder);
        ddgi.recordResolvePass(context, *depth, *normal, pc, 0u);
        auto commands = encoder->finish();
        assert(commands && commands->commandCount() > 0u);
    };
    record(); // Establish the original descriptor signature and material cache.
    ddgi.setMaterialAlbedo(0u, *replacement, *sampler);
    ddgi.setMaterialAlbedo(1u, *invalid, *sampler);
    invalid.reset();
    bool failed = false;
    try {
        record();
    } catch (const core::DiagnosticError& error) {
        std::cout << "Partial material write: " << error.what() << std::endl;
        failed = std::string_view(error.what()).find("RHI-DESCRIPTOR-RESOURCE-NOT-LIVE")
            != std::string_view::npos;
    }
    assert(failed);
    // Binding 12 was changed before binding 13 failed. Return to exactly the
    // original signature: a stale cache hit would leave the now-dead partial
    // replacement at binding 12, which the real Null bind/dispatch rejects.
    replacement.reset();
    ddgi.setMaterialAlbedo(0u, *original, *sampler);
    ddgi.setMaterialAlbedo(1u, *original, *sampler);
    record();
    assert(ddgi.currentReadIndex() == 0);
    const auto initialSubmits = device->queue().submitCount();
    for (unsigned point = 0; point != 6; ++point) {
        auto inner = device->createCommandEncoder({"RealEncoderForFailure"});
        tests::ComputeRecordFailure commands(
            *inner, static_cast<tests::ComputeRecordFailure::Point>(point));
        render_graph::RenderContext context(*device, commands);
        bool caught = false;
        try {
            ddgi.recordResolvePass(context, *depth, *normal, pc, 0u);
        } catch (const core::DiagnosticError& error) {
            caught = std::string_view(error.what()) == "TEST-COMPUTE-RECORD-FIRST-FAILURE";
        }
        assert(caught && commands.failed && commands.attempts == point + 1u);
        assert(ddgi.currentReadIndex() == 0);
        assert(device->queue().submitCount() == initialSubmits);
    }
    record(); // Failed recorders were abandoned; a fresh recorder still works.
    // Explicit Null acceptance simulation tests the renderer's transaction;
    // actual queue-acceptance and GPU content are verified in the Metal graph/app.
    assert(!ddgi.acceptPreparedHistory(preparedReceipt, {}));
    assert(ddgi.acceptPreparedHistory(preparedReceipt, completion));
    const auto acceptedGeneration = ddgi.acceptedProbeScheduleGeneration();
    assert(acceptedGeneration != 0u && ddgi.currentReadIndex() == 1);
    assert(!ddgi.acceptPreparedHistory(preparedReceipt, completion));
    assert(ddgi.acceptedProbeScheduleGeneration() == acceptedGeneration);
    assert(ddgi.prepareProbeUpdateSchedule(scheduleInput, 0u).success);
    assert(ddgi.telemetry().newlyExposedProbeCount == 0u);
    scheduleInput.probeGridOriginHysteresis[0] = 1.0f;
    scheduleInput.scrollOffsetFrame[0] = 1.0f;
    for (const auto label : {"DdgiProbeUpdateList0", "DdgiSamplingConstants0"}) {
        wrapper.failBufferWrites(label);
        const auto failedUpload = ddgi.prepareProbeUpdateSchedule(scheduleInput, 1u);
        assert(!failedUpload.success);
        assert(
            failedUpload.diagnostics.find("TEST-BUFFER-UPLOAD-FAILED") != std::string_view::npos);
        assert(!ddgi.acceptPreparedHistory(ddgi.preparedHistoryToken(), completion));
        assert(ddgi.acceptedProbeScheduleGeneration() == acceptedGeneration);
        assert(ddgi.currentReadIndex() == 1);
        wrapper.failBufferWrites({});
        assert(ddgi.prepareProbeUpdateSchedule(scheduleInput, 1u).success);
        assert(ddgi.telemetry().newlyExposedProbeCount == 4u);
    }
    const auto destroyedReceipt = ddgi.preparedHistoryToken();
    ddgi.destroy();
    assert(ddgi.acceptedProbeScheduleGeneration() == 0u
        && !ddgi.acceptPreparedHistory(destroyedReceipt, completion));
    bool destroyedRejected = false;
    try {
        record();
    } catch (const core::DiagnosticError& error) {
        destroyedRejected = std::string_view(error.what()) == "DDGI-RECORDING-NOT-READY";
    }
    assert(destroyedRejected);
    assert(
        ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                2u,
                2u,
                2u,
                4u,
                4u)
            .success);
    assert(ddgi.prepareProbeUpdateSchedule(scheduleInput, 1u).success);
    assert(!ddgi.isPreparedHistory(destroyedReceipt)
        && !ddgi.acceptPreparedHistory(destroyedReceipt, completion));
}

void input_slots_wait_for_their_actual_consumer()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    renderer::DdgiRenderer ddgi(*device);
    assert(
        ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                2u,
                2u,
                2u,
                4u,
                4u)
            .success);
    const auto fence = [&] {
        auto owner = core::makeShared<core::UniquePtr<rhi::GpuFence>>(device->createFence());
        return std::shared_ptr<rhi::GpuFence>(owner, owner->get());
    };
    auto first = fence();
    auto second = fence();
    rhi::GpuInputSlotLease lease;
    assert(lease.ready() && !lease.retain({}));
    assert(lease.retain(first) && !lease.ready());
    assert(lease.retain(first) && !lease.retain(second));

    renderer::DdgiPushConstants input{};
    for (unsigned axis = 0u; axis != 3u; ++axis)
        input.probeGridSpacingStrength[axis] = 1.0f;
    input.updateParams[0] = 2.0f;
    assert(ddgi.prepareProbeUpdateSchedule(input, 0u).success);
    const auto a = ddgi.preparedHistoryToken();
    assert(ddgi.isPreparedInput(a));
    assert(!ddgi.acceptPreparedInput(a, {}));
    assert(ddgi.acceptPreparedInput(a, first)); // Pure sampling, no update/history.
    assert(!ddgi.isPreparedInput(a) && !ddgi.inputSlotReady(0u));
    assert(!ddgi.isPreparedHistory(a)); // Another graph must reject before acceptance.
    assert(ddgi.currentReadIndex() == 0 && ddgi.acceptedProbeScheduleGeneration() == 0u);
    assert(ddgi.acceptPreparedInput(a, first)); // Same submission's second domain notification.
    assert(!ddgi.acceptPreparedInput(a, second));
    const auto newlyExposed = ddgi.telemetry().newlyExposedProbeCount;
    input.scrollOffsetFrame[3] = 2.0f;
    input.probeGridOriginHysteresis[0] = 1.0f;
    const auto busy = ddgi.prepareProbeUpdateSchedule(input, 1u);
    assert(!busy.success && busy.diagnostics == "DDGI-INPUT-SLOT-NOT-READY");
    assert(ddgi.preparedHistoryToken().generation == a.generation);
    assert(ddgi.telemetry().newlyExposedProbeCount == newlyExposed);
    input.scrollOffsetFrame[3] = 1.0f;
    assert(ddgi.inputSlotReady(1u) && ddgi.prepareProbeUpdateSchedule(input, 1u).success);
    assert(!ddgi.acceptPreparedInput(a, first)); // Replaced identity stays stale.
    first->signal();
    assert(ddgi.inputSlotReady(0u) && lease.ready() && lease.retain(second));
    input.scrollOffsetFrame[3] = 2.0f;
    assert(ddgi.prepareProbeUpdateSchedule(input, 1u).success);
    const auto b = ddgi.preparedHistoryToken();
    assert(ddgi.acceptPreparedHistory(b, second));
    assert(ddgi.currentReadIndex() == 1 && !ddgi.inputSlotReady(0u));
    assert(ddgi.acceptPreparedInput(b, second));
    assert(!ddgi.acceptPreparedHistory(b, second));
    second->signal();
    assert(ddgi.inputSlotReady(0u));
    input.scrollOffsetFrame[3] = std::numeric_limits<float>::infinity();
    assert(!ddgi.prepareProbeUpdateSchedule(input, 1u).success);
}

void shared_lights_wait_for_every_slot_reader()
{
    using namespace arp;
    for (const bool reverse : {false, true}) {
        auto device = rhi::createNullDevice();
        tests::CapabilityOverrideDevice wrapper(*device, nullptr);
        renderer::DdgiRenderer ddgi(wrapper);
        assert(
            ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                    {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                    2u,
                    2u,
                    2u,
                    4u,
                    4u)
                .success);
        const auto fence = [&] {
            auto owner = core::makeShared<core::UniquePtr<rhi::GpuFence>>(device->createFence());
            return std::shared_ptr<rhi::GpuFence>(owner, owner->get());
        };
        auto first = fence();
        auto second = fence();
        renderer::DdgiLocalLightDesc light;
        light.range = 4.0f;
        light.intensity = 1.0f;
        assert(ddgi.setLocalLights(&light, 1u).success);
        renderer::DdgiPushConstants input{};
        for (unsigned axis = 0u; axis < 3u; ++axis)
            input.probeGridSpacingStrength[axis] = 1.0f;
        input.updateParams[0] = 2.0f;
        assert(ddgi.prepareProbeUpdateSchedule(input, 0u).success);
        assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), first));
        input.scrollOffsetFrame[3] = 1.0f;
        // Same shared contents can be consumed by the second free slot.
        assert(ddgi.prepareProbeUpdateSchedule(input, 0u).success);
        const auto receipt = ddgi.preparedHistoryToken();
        assert(ddgi.acceptPreparedInput(receipt, second));
        assert(!ddgi.sharedInputsReady());
        assert(!ddgi.setLocalLights(nullptr, 1u).success);
        assert(!ddgi.setLocalLights(&light, renderer::kDdgiMaxLocalLights + 1u).success);
        light.range = std::numeric_limits<float>::quiet_NaN();
        assert(!ddgi.setLocalLights(&light, 1u).success);
        assert(ddgi.preparedHistoryToken().generation == receipt.generation);
        // Removing all lights also mutates both shared buffers.
        assert(ddgi.setLocalLights(nullptr, 0u).success);
        (reverse ? second : first)->signal();
        input.scrollOffsetFrame[3] = reverse ? 3.0f : 2.0f;
        assert(ddgi.inputSlotReady(static_cast<std::uint32_t>(input.scrollOffsetFrame[3])));
        assert(!ddgi.sharedInputsReady());
        wrapper.failBufferWrites("DdgiLocalLights");
        const auto busy = ddgi.prepareProbeUpdateSchedule(input, 0u);
        assert(!busy.success && busy.diagnostics == "DDGI-SHARED-LIGHT-INPUT-NOT-READY");
        assert(ddgi.preparedHistoryToken().generation == receipt.generation);
        (reverse ? first : second)->signal();
        assert(ddgi.sharedInputsReady());
        // Once writable, an actual upload failure invalidates the candidate.
        const auto failed = ddgi.prepareProbeUpdateSchedule(input, 0u);
        assert(!failed.success
            && failed.diagnostics.find("TEST-BUFFER-UPLOAD-FAILED") != std::string::npos);
        assert(ddgi.preparedHistoryToken().generation == 0u);
        wrapper.failBufferWrites("DdgiProbeLocalLightInfluences");
        const auto partial = ddgi.prepareProbeUpdateSchedule(input, 0u);
        assert(!partial.success
            && partial.diagnostics.find("TEST-BUFFER-UPLOAD-FAILED") != std::string::npos);
        assert(ddgi.preparedHistoryToken().generation == 0u);
        wrapper.failBufferWrites({});
        assert(ddgi.prepareProbeUpdateSchedule(input, 0u).success);
    }
}

void failed_shared_upload_invalidates_the_previous_cache_key()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    tests::CapabilityOverrideDevice wrapper(*device, nullptr);
    renderer::DdgiRenderer ddgi(wrapper);
    assert(
        ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                2u,
                2u,
                2u,
                4u,
                4u)
            .success);
    renderer::DdgiLocalLightDesc light;
    light.range = 4.0f;
    assert(ddgi.setLocalLights(&light, 1u).success);
    renderer::DdgiPushConstants input{};
    for (unsigned axis = 0u; axis < 3u; ++axis)
        input.probeGridSpacingStrength[axis] = 1.0f;
    input.updateParams[0] = 2.0f;
    assert(ddgi.prepareProbeUpdateSchedule(input, 0u).success);
    input.probeGridOriginHysteresis[0] = 32.0f;
    wrapper.failBufferWrites("DdgiProbeLocalLightInfluences", true);
    assert(!ddgi.prepareProbeUpdateSchedule(input, 0u).success);
    assert(ddgi.preparedHistoryToken().generation == 0u);
    input.probeGridOriginHysteresis[0] = 0.0f;
    wrapper.failBufferWrites("DdgiLocalLights");
    const auto oldKeyRetry = ddgi.prepareProbeUpdateSchedule(input, 0u);
    assert(!oldKeyRetry.success);
    assert(oldKeyRetry.diagnostics.find("TEST-BUFFER-UPLOAD-FAILED") != std::string::npos);
    assert(ddgi.preparedHistoryToken().generation == 0u);
    wrapper.failBufferWrites({});
    assert(ddgi.prepareProbeUpdateSchedule(input, 0u).success);
}

} // namespace

int main()
{
    diagnostic_owns_first_failure();
    all_recording_entries_reject_unready_renderer();
    partial_material_write_must_not_publish_cache_success();
    input_slots_wait_for_their_actual_consumer();
    shared_lights_wait_for_every_slot_reader();
    failed_shared_upload_invalidates_the_previous_cache_key();
    std::cout << "DDGI recording guards and owned first diagnostics passed\n";
}
