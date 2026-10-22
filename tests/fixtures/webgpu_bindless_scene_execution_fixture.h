#pragma once
#include "bindless_native_scene_execution_fixture.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_native_program.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_completed_scene_attachment_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"
#include "webgpu_original_completed_content_fixture.h"

namespace arp::test {
using namespace renderer;

// The callback backend uses the same scene bytes, immutable public actors and
// original graph stream as Native. Its callback/map budgets are independent.
struct WebGpuBindlessSceneDataFixture
    : BindlessNativeSceneDataFixture<WebGpuOriginalCompletedContentRequest> {
    using Base = BindlessNativeSceneDataFixture<WebGpuOriginalCompletedContentRequest>;
    static constexpr bool canonicalCompletion = true;
    std::uint64_t nativeStagingCeiling = 0u;

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        request.originalPreparation = {};
        request.originalExecution = {terrain() ? 4u : 3u, true};
    }

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& b) const
    {
        using namespace rhi;
        using Cold = rhi_webgpu::WebGpuBindlessRootColdAdmission;
        Base::configureAdmission(b);
        const auto maps = vertex->entry.find("Color") != core::pmr::String::npos ? 2u : 1u;
        b.maxCallbackTickets = Cold::baseCallbackTickets
            + maps * OriginalCommandReadbackMapContract::participantsPerMap;
        b.maxCallbackRegistrations = Cold::baseCallbackRegistrations
            + maps * OriginalCommandReadbackMapContract::participantsPerMap;
        b.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Callback)] = originalRootCpuBytes;
        std::uint32_t sources = 0u;
        for (const auto count : b.maxSourceOwners)
            sources += count;
        b.maxNativePayloadObjects =
            2u * sources + 2u + rhi_webgpu::WebGpuBindlessRootNativeProgram::pushNativeObjects;
        // Preserve the original graph union, then reserve independently held
        // closure, initializer, map and issuer custody from typed source bounds.
        const auto views =
            b.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::TextureView)];
        const OriginalBindlessRootCallbackCustodyRequest custody{
            1u, true, 1u, b.maxCallbackTickets};
        b.maxGraphOwners += views
                * (OriginalCommandTextureViewSource::completeRetainedOwnerCount
                    + 2u * OriginalCommandGraphicsPipelineSource::cpuOwnerSlots)
            + 2u * sources
            + 2u * (rhi_webgpu::WebGpuOriginalGeometryContent::exclusiveCpuQuotaSlots + 1u)
            + OriginalCommandRecordingSeal::additionalOwnerCount
            + OriginalCommandLifecycleReservation::additionalOwnerCount + 6u
            + maps
                * (OriginalCommandBufferReadbackBinding::retainedOwnerCount
                    + OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners + 2u)
            + OriginalBindlessRootColdCommandContract::additionalOwners
            + OriginalBindlessRootCallbackCustodyContract::additionalOwners(custody)
            + OriginalBindlessRootIssuerContract::additionalOwners;
    }
};

class WebGpuBindlessSceneExecutionFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Public WebGPU scene: %s\n", id);
        assert(!id);
    }

public:
    enum class Scenario {
        Success,
        RecordedCancel,
        ColdCancel,
        RecordingError,
        SubmissionError,
        Reverse,
        PendingCancel,
        CallerDrop,
        ThrowAfter,
        ThrowBefore,
        RetiredLifetime,
        MaterialRangeRejected
    };
    static constexpr std::array<const char*, 12u> scenarioNames{"success",
        "recorded-cancel",
        "cold-cancel",
        "recording-error",
        "submission-error",
        "reverse",
        "pending-cancel",
        "caller-drop",
        "throw-after",
        "throw-before",
        "retired-lifetime",
        "material-range-rejected"};
    Scenario scenario = Scenario::Success;
    WebGpuBindlessSceneDataFixture data;
    static constexpr bool graphicsPush = true, sceneStream = true, completedTextures = true;
    static constexpr auto geometryBufferCount = WebGpuBindlessSceneDataFixture::geometryBufferCount;
    static constexpr auto resultBytes = WebGpuBindlessSceneDataFixture::resultBytes;
    const rhi::BindlessShaderArtifact* artifact;
    std::shared_ptr<WebGpuBindlessSceneDataFixture::Geometry> geometry;
    renderer::BindlessPbrGeometryDraw16 draw;

    explicit WebGpuBindlessSceneExecutionFixture(WebGpuBindlessSceneDataFixture value)
        : data(std::move(value)), artifact(data.artifact), geometry(data.geometry), draw(data.draw)
    {
        scenario = data.cancelBeforeSubmit ? Scenario::RecordedCancel : Scenario::Success;
    }

    bool terrain() const { return data.terrain(); }

    template <class... Args> void preparedSceneInputs(Args&&...) const
    {
        assert(false && "WebGPU scene graph admission unexpectedly failed");
    }

    template <class Root, class Inventory>
    void operator()(const Root& root, const Inventory& inventory, rhi::GpuDevice* device) const
    {
        if (!device) {
            data.releaseSceneInputs();
            return;
        }
        OriginalBindlessGraphicsStreamFixture::execute(root, inventory, *device, *this);
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root& root,
        const Inventory& inventory,
        const Binding& binding,
        const Transaction& tx,
        rhi::GpuDevice& device,
        const rhi::OriginalBindlessRootReadbackSequence::Input& results) const
    {
        using namespace rhi;
        using Cold = rhi_webgpu::WebGpuBindlessRootColdAdmission;
        using Program = rhi_webgpu::WebGpuBindlessRootNativeProgram;
        using Issuer = OriginalBindlessRootExecution;
        const auto cpu = tx->originalMetadataQuota()->limitBytes();
        const auto owners = inventory->originalBudget().maxGraphOwners;
        const auto count = static_cast<std::uint32_t>(inventory->sources().size());
        const auto operations = binding->originalProgram()->operations().size();
        const BindlessSceneConformanceInputs content{1u, true, data.family, 4096u, 4096u, 4096u};
        const auto* contentId = BindlessSceneConformanceContract::inspectInitialized<
            rhi_webgpu::WebGpuOriginalGeometryContent>(root,
            inventory,
            binding->originalProgram(),
            data.state->shaderActors.data(),
            data.state->shaderActors.size(),
            content);
        if (scenario == Scenario::MaterialRangeRejected) {
            assert(contentId && core::StringView(contentId) == "RHI-BINDLESS-PBR-GEOMETRY-SELECTION"
                && tx->journalEmpty());
            for (std::size_t i = 0u; i < results.count; ++i)
                assert(!results.bindings[i]->originalResult()->publishedBytes());
            std::printf(
                "BINDLESS_WEBGPU_SCENE_FAILURE_LIFECYCLE entry=%s scenario=material-range-rejected outcome=unpublished caller-conformance results=%u\n",
                data.vertex->entry.c_str(),
                unsigned(results.count));
            return;
        }
        require(contentId);
        std::uint64_t visits = count + operations;
        for (const auto& op : binding->originalProgram()->operations())
            visits += op.referenceCount + 2u * count;
        auto native = core::makeShared<core::CpuMemoryQuota>(cpu);
        auto submission = core::makeShared<core::CpuMemoryQuota>(cpu);
        auto callback = core::makeShared<core::CpuMemoryQuota>(cpu);
        assert(results.count == 1u || results.count == 2u);
        const auto maps = static_cast<std::uint32_t>(results.count);
        const auto tickets = Cold::baseCallbackTickets
            + maps * OriginalCommandReadbackMapContract::participantsPerMap;
        const auto registrations = Cold::baseCallbackRegistrations
            + maps * OriginalCommandReadbackMapContract::participantsPerMap;
        const auto bytes = sizeof(std::uint32_t) + (maps == 2u ? resultBytes : 0u);
        OriginalBindlessRootExecutionBudget b;
        b.intent = {terrain() ? 4u : 3u, true};
        b.closure = {terrain() ? 6u : 5u, true, count, owners, owners, cpu};
        b.closure.initializedGeometryContractVersion = 1u;
        b.closure.initializedGeometryOptIn = true;
        b.closure.maxInitializedGeometryActors = geometry->actors.size();
        for (const auto& actor : geometry->actors)
            b.closure.maxInitializedGeometryBytes += actor->initializedBytes();
        b.closure.initializedShader = {1u,
            true,
            static_cast<std::uint32_t>(data.state->shaderActors.size()),
            0u,
            count * (1u + data.state->shaderActors.size())};
        for (const auto& actor : data.state->shaderActors)
            b.closure.initializedShader.maxBytes += actor->initializedBytes();
        b.closure.producedAttachments = {1u, true};
        b.closure.maxAttachmentViews = 1u;
        b.admission = {2u,
            true,
            operations,
            visits,
            owners,
            cpu,
            {1u, true, maps, maps, maps, bytes, bytes, cpu},
            {1u,
                true,
                maps,
                maps,
                maps * OriginalCommandReadbackMapContract::scopeCallbacksPerMap,
                tickets,
                registrations}};
        b.admission.initializedGeometry = {2u, true, operations, 1u, 1u, operations * 2u, 3u};
        b.admission.pushUpload = {1u,
            true,
            1u,
            sizeof(draw),
            sizeof(draw),
            Program::pushNativeObjects,
            Program::pushRetainedOwners};
        assert(tickets <= UINT32_MAX && registrations <= UINT32_MAX);
        b.lifecycle = {12u,
            true,
            1u,
            static_cast<std::uint32_t>(tickets),
            static_cast<std::uint32_t>(registrations),
            owners,
            owners,
            owners,
            cpu,
            cpu,
            cpu};
        b.result = {3u, true, maps, bytes, cpu, owners};
        b.command = {2u, true, owners, cpu, {2u, true, operations, visits}};
        b.command.program.pushUpload = b.admission.pushUpload;
        b.command.program.initializedGeometryContractVersion = 1u;
        b.command.program.initializedGeometryOptIn = true;
        b.custody = {1u, true, 1u, static_cast<std::uint32_t>(tickets), owners, cpu, cpu};
        b.issuers = {1u, true, 1u, 1u, owners, cpu, cpu, cpu};
        b.requireWorkloadRetention = true;
        require(b.validate());
        OriginalBindlessRootExecutionInputs inputs{
            root, inventory, tx, binding, native, submission, callback};
        inputs.geometry = geometry->actors.data();
        inputs.geometryCount = geometry->actors.size();
        inputs.initializedShader = data.state->shaderActors.data();
        inputs.initializedShaderCount = data.state->shaderActors.size();
        inputs.readbacks = results;
        inputs.initializedGraphics = data.sceneInputs(root, operations);
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension);
        core::pmr::String diagnostic;
        auto missing = inputs;
        missing.initializedGraphics = {};
        assert(!extension->prepareOriginalRootExecution(device, missing, b, diagnostic)
            && diagnostic == "RHI-BINDLESS-INITIALIZED-GRAPHICS-INPUTS-REQUIRED");
        assert(!native->liveBytes() && !submission->liveBytes() && !callback->liveBytes()
            && tx->journalEmpty());
        auto issuer = extension->prepareOriginalRootExecution(device, inputs, b, diagnostic);
        require(issuer ? nullptr : diagnostic.c_str());
        assert(issuer && !binding->originalResult()->publishedBytes());
        using Probe = WebGpuNativeSubmissionProbeFixture;
        const auto pipeline = std::visit(
            [](const auto& prepared) -> std::shared_ptr<rhi_webgpu::WebGpuNativePipelineOwnership> {
                return rhi_webgpu::webGpuNativePayloadFor(*prepared->owners()->desc().pipeline);
            },
            root->originalPrepared());
        assert(pipeline && pipeline->issuedContract());
        const auto nativeDevice =
            static_cast<WGPUDevice>(pipeline->issuedContract()->nativeDevice());
        const auto queue = wgpuDeviceGetQueue(nativeDevice);
        assert(queue);
        const bool recordingProbe =
            scenario == Scenario::RecordingError || scenario == Scenario::Reverse;
        const bool submissionProbe = scenario == Scenario::SubmissionError
            || scenario == Scenario::Reverse || scenario == Scenario::ThrowAfter
            || scenario == Scenario::ThrowBefore;
        if (scenario == Scenario::ColdCancel) {
            require(issuer->cancelExecution());
            assert(!issuer->pendingRecords() && tx->journalEmpty());
            // No recording object exists yet. Release the original cold seal,
            // whose destructor performs the same rollback as the Native path.
            issuer.reset();
        } else {
            if (recordingProbe)
                Probe::instrumentRecording(nativeDevice, scenario == Scenario::Reverse);
            require(Issuer::record(issuer, b.intent));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return issuer->recordingProven() || issuer->executionDiagnostic();
            }));
            if (recordingProbe)
                Probe::restoreRecording();
            for (std::size_t i = 0u; i < maps; ++i)
                assert(!results.bindings[i]->originalResult()->publishedBytes());
            if (scenario == Scenario::RecordingError) {
                assert(issuer->executionDiagnostic() && !issuer->pendingRecords());
            } else {
                require(issuer->executionDiagnostic());
                assert(issuer->recordingProven());
                if (scenario == Scenario::RecordedCancel) {
                    require(issuer->cancelExecution());
                } else {
                    if (submissionProbe)
                        Probe::instrument(nativeDevice,
                            queue,
                            scenario == Scenario::SubmissionError   ? Probe::Probe::NativeError
                                : scenario == Scenario::Reverse     ? Probe::Probe::Reverse
                                : scenario == Scenario::ThrowBefore ? Probe::Probe::ThrowBefore
                                                                    : Probe::Probe::ThrowAfter,
                            nullptr,
                            [](void*) noexcept {});
                    const auto* submitId = Issuer::submit(issuer, b.intent);
                    if (scenario == Scenario::ThrowAfter || scenario == Scenario::ThrowBefore)
                        assert(submitId);
                    else
                        require(submitId);
                    assert(root->originalSnapshot()->originalSubmissionBudgetOwner()->used() == 1u);
                    for (std::size_t i = 0u; i < maps; ++i)
                        assert(!results.bindings[i]->originalResult()->publishedBytes());
                    if (scenario == Scenario::PendingCancel)
                        assert(issuer->cancelExecution());
                    if (scenario == Scenario::RetiredLifetime)
                        device.retireOriginalDeviceLifetime();
                    if (scenario == Scenario::CallerDrop)
                        issuer.reset();
                }
            }
        }
        const auto ready = [&] {
            return !root->originalSnapshot()->originalSubmissionBudgetOwner()->used();
        };
        const auto browserDeadline = EM_ASM_DOUBLE({
            // clang-format off
            return Module.tableBrowserCompletionDeadline || 0;
            // clang-format on
        });
        const bool drained = browserDeadline > 0.0
            ? WebGpuAsyncWaitFixture::untilDeadline(ready, browserDeadline)
            : WebGpuAsyncWaitFixture::until(ready);
        if (!drained)
            std::fprintf(stderr,
                "WebGPU scene drain: entry=%s scenario=%s execution=%s pending=%u transaction=%u submission-used=%llu native-bytes=%llu submission-bytes=%llu callback-bytes=%llu tickets=%llu registrations=%llu\n",
                data.vertex->entry.c_str(),
                scenarioNames[static_cast<std::size_t>(scenario)],
                issuer && issuer->executionDiagnostic() ? issuer->executionDiagnostic() : "none",
                issuer ? issuer->pendingRecords() : 0u,
                static_cast<unsigned>(tx->status()),
                static_cast<unsigned long long>(
                    root->originalSnapshot()->originalSubmissionBudgetOwner()->used()),
                static_cast<unsigned long long>(native->liveBytes()),
                static_cast<unsigned long long>(submission->liveBytes()),
                static_cast<unsigned long long>(callback->liveBytes()),
                static_cast<unsigned long long>(inventory->originalReservedCallbackTicketCount()),
                static_cast<unsigned long long>(
                    inventory->originalReservedCallbackRegistrationCount()));
        assert(drained);
        if (submissionProbe)
            Probe::restore();

        const bool success = scenario == Scenario::Success || scenario == Scenario::Reverse;
        if (scenario == Scenario::RecordedCancel || scenario == Scenario::ColdCancel
            || scenario == Scenario::RecordingError) {
            // A rejected recording has never invoked native submission. Its
            // genuine scope drain rolls back; only uncertain submitted work
            // invalidates the original journal.
            assert(tx->status() == OriginalCommandStateTransaction::Status::Abandoned);
        } else if (success) {
            require(issuer->executionDiagnostic());
            assert(issuer->submissionCompleted()
                && tx->status() == OriginalCommandStateTransaction::Status::Committed);
            std::uint32_t fault = ~0u;
            std::memcpy(&fault, binding->originalResult()->publishedBytes(), sizeof(fault));
            assert(!fault);
            std::printf("BINDLESS_SCENE_FAULT_WORD entry=%s scenario=%s word=%u\n",
                data.vertex->entry.c_str(),
                scenarioNames[static_cast<std::size_t>(scenario)],
                fault);
            for (std::size_t i = 0u; i < maps; ++i)
                assert(results.bindings[i]->originalResult()->publishedBytes());
            if (maps == 2u) {
                const auto* pixels = results.bindings[1u]->originalResult()->publishedBytes();
                require(scenario == Scenario::Success
                        ? BindlessSceneGeometryReadbackFixture::inspectAndReport(
                              data.vertex->entry.c_str(), pixels, resultBytes, terrain())
                        : BindlessSceneGeometryReadbackFixture::inspect(
                              pixels, resultBytes, terrain()));
            }
            if (scenario == Scenario::Success)
                WebGpuCompletedSceneAttachmentFixture::observe(
                    BindlessSceneAttachmentFixture::completedTarget(*inventory),
                    data.vertex->entry.c_str(),
                    terrain(),
                    maps == 2u ? results.bindings[1u]->originalResult()->publishedBytes() : nullptr,
                    false);
        } else {
            assert(tx->status() == OriginalCommandStateTransaction::Status::Invalidated);
        }
        for (std::size_t i = 0u; i < maps; ++i)
            assert(bool(results.bindings[i]->originalResult()->publishedBytes()) == success);
        wgpuQueueRelease(queue);
        issuer.reset();
        assert(!native->liveBytes() && !submission->liveBytes() && !callback->liveBytes()
            && !inventory->originalReservedSubmissionCount()
            && !inventory->originalReservedCallbackTicketCount()
            && !inventory->originalReservedCallbackRegistrationCount());
        if (scenario == Scenario::Success || scenario == Scenario::RecordedCancel)
            std::printf("BINDLESS_WEBGPU_SCENE_PUBLIC_LIFECYCLE entry=%s outcome=%s results=%u\n",
                data.vertex->entry.c_str(),
                success ? "completed" : "cancelled-unpublished",
                maps);
        else
            std::printf(
                "BINDLESS_WEBGPU_SCENE_FAILURE_LIFECYCLE entry=%s scenario=%s outcome=%s results=%u\n",
                data.vertex->entry.c_str(),
                scenarioNames[static_cast<std::size_t>(scenario)],
                success ? "completed" : "unpublished",
                maps);
    }
};
} // namespace arp::test
