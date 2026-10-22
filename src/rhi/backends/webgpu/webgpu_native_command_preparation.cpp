#include "rhi/backends/webgpu/webgpu_native_command_preparation.h"

#include "core/runtime/completion_retention_pool.h"
#include "core/runtime/external_callback_gate.h"
#include "rhi/backends/webgpu/webgpu_attachment_operation_lowering.h"
#include "rhi/backends/webgpu/webgpu_bindless_preparation.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_cold_command.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_issuer_preparation.h"
#include "rhi/backends/webgpu/webgpu_canonical_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_canonical_callback_layout.h"
#include "rhi/backends/webgpu/webgpu_canonical_cold_command.h"
#include "rhi/backends/webgpu/webgpu_canonical_copy_pass_lowering.h"
#include "rhi/backends/webgpu/webgpu_canonical_issuer_preparation.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_joins.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h"
#include "rhi/backends/webgpu/webgpu_canonical_upload_joins.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_state.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_native_completion_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_error_scope_batch.h"
#include "rhi/backends/webgpu/webgpu_native_invocation.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_recording.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"
#include "rhi/backends/webgpu/webgpu_native_original_texture_readback_lowering.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#include "rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h"
#include "rhi/backends/webgpu/webgpu_original_observed_native_loss.h"
#include "rhi/backends/webgpu/webgpu_original_render_pass_payload.h"
#include "rhi/backends/webgpu/webgpu_original_typed_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_original_typed_cold_command.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "rhi/frontend/bindless_graphics_pass_preparation.h"
#include "rhi/frontend/bindless_root_execution.h"
#include "rhi/frontend/command_push_consumption.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/frontend/transfer_copy.h"
#include "rhi/semantics/bindless_scene_raster.h"
#include "rhi/semantics/command_lowering.h"
#include "rhi/semantics/viewport_scissor_validation.h"

#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace arp::rhi_webgpu {
const char* WebGpuNativeCommandPreparation::preflightBindlessRootPushUpload(
    const rhi::OriginalBindlessRootExecutionInputs& inputs,
    const rhi::OriginalBindlessRootExecutionBudget& budget) noexcept
{
    const auto& request = budget.command.program;
    if (request.pushUpload.empty())
        return nullptr;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)inputs;
    return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-RUNTIME";
#else
    using namespace rhi;
    using K = CommandUsageKind;
    if (!inputs.root || !inputs.readback || !inputs.inventory)
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-ROOT";
    const auto* prepared = std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
        &inputs.root->originalPrepared());
    if (!prepared || !*prepared || request.contractVersion != 2u)
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-ROOT";
    const auto& object = *(*prepared)->owners()->desc().pipeline;
    const auto pipeline = webGpuNativePayloadFor(object);
    if (!pipeline || !pipeline->issuedContract())
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-ROOT";
    auto limits = WGPU_LIMITS_INIT;
    if (wgpuDeviceGetLimits(
            static_cast<WGPUDevice>(pipeline->issuedContract()->nativeDevice()), &limits)
        != WGPUStatus_Success)
        return "RHI-WEBGPU-ROOT-PROGRAM-DEVICE-LIMITS";
    OriginalCommandPushUploadPlan upload(request.pushUpload,
        {limits.maxBufferSize,
            limits.maxUniformBufferBindingSize,
            limits.minUniformBufferOffsetAlignment});
    OriginalCommandPushConsumption consumer;
    const auto& program = inputs.readback->originalProgram();
    const auto& graphInputs = inputs.readback->originalGraph()->originalInputs();
    const auto& arguments = graphInputs->originalScalars()->originalArguments();
    const auto& usage = *graphInputs->originalScalars()->originalUsage();
    const auto& consumption = program->originalBudget().bindlessRoots.pushConsumption;
    if (consumption.diagnostic() || consumption.recordingContractVersion != 1u
        || !consumption.recordingOptIn)
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-CONSUMPTION";
    if (program->operations().size() > request.maxCommands)
        return "RHI-ORIGINAL-ROOT-PROGRAM-COMMAND-CAPACITY";
    for (const auto& op : program->operations()) {
        if (op.kind == K::BindPreparedGraphicsBinding || op.kind == K::EndRenderPass)
            consumer.onBind();
        else if (op.kind == K::PushConstants) {
            if (op.referenceCount != 1u
                || usage.references()[op.firstReference].sourceIndex
                    != inputs.readback->rootSourceIndex())
                return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-ROOT";
            PushConstantsDesc push;
            if (const auto* id = consumer.update(arguments,
                    op.commandIndex,
                    object.layout()->desc(),
                    object.shaderReflection(),
                    ShaderStage::Vertex | ShaderStage::Fragment,
                    consumption,
                    push))
                return id;
            OriginalCommandPushUploadPlan::Slice slice;
            if (const auto* id = upload.append(
                    push.sizeBytes, object.shaderReflection().pushConstantByteCount, slice))
                return id;
        } else if (op.kind == K::Draw)
            if (const auto* id = consumer.consume(object.shaderReflection(), consumption))
                return id;
    }
    return upload.validateComplete(WebGpuBindlessRootNativeProgram::pushNativeObjects,
        WebGpuBindlessRootNativeProgram::pushRetainedOwners);
#endif
}

const char* WebGpuNativeCommandPreparation::inspectBindlessRootColdCommand(
    const std::shared_ptr<const WebGpuBindlessRootColdAdmission>& admission,
    const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>& results,
    const rhi::OriginalBindlessRootColdCommandRequest& request) noexcept
{
    using C = rhi::OriginalBindlessRootColdCommandContract;
    if (const auto* id = C::validateIntent(request))
        return id;
    if (const auto* id = WebGpuBindlessRootColdAdmission::validateOriginal(admission))
        return id;
    if (const auto* id = rhi::OriginalBindlessRootPushUploadContract::validateCorrespondence(
            admission->originalBudget().pushUpload, request.program.pushUpload))
        return id;
    if (const auto* id = WebGpuCanonicalReadbackResultStorage::validateOriginal(results))
        return id;
    if (!core::SharedOwnerProjection::sameOwner(results->originalBufferAdmission(), admission))
        return "RHI-WEBGPU-ROOT-COLD-COMMAND-ORIGINAL-RESULTS";
    const auto& seal = admission->originalReservation()->originalSeal();
    const auto& root = seal->originalRoot();
    if (request.maxCpuMetadataBytes != root->originalMetadataQuota()->limitBytes())
        return "RHI-ORIGINAL-ROOT-COLD-COMMAND-QUOTA";
    const auto lowering = rhi::semantics::OriginalBindlessRootColdCommandLowering::lowering(
        rhi::Backend::WebGPU, request.contractVersion);
    if (!lowering.preparationAvailable || lowering.executionAvailable)
        return lowering.diagnostic;
    if (request.contractVersion == 2u) {
        const bool geometry = admission->originalBudget().initializedGeometry.contractVersion != 0u;
        if (geometry != bool(request.program.initializedGeometryContractVersion)
            || geometry != request.program.initializedGeometryOptIn)
            return "RHI-WEBGPU-ROOT-PROGRAM-GEOMETRY-CORRESPONDENCE";
        const auto programLowering = rhi::semantics::OriginalBindlessRootProgramLowering::lowering(
            rhi::Backend::WebGPU, request.program.contractVersion);
        if (!programLowering.preparationAvailable || programLowering.executionAvailable)
            return programLowering.diagnostic;
        if (const auto* id = rhi::OriginalBindlessRootProgramContract::validateCounts(
                admission->originalReadbackBinding()->originalProgram()->operations().size(),
                0u,
                request.program))
            return id;
    }
    std::uint32_t additionalOwners = 0u;
    if (!request.program.pushUpload.empty()) {
        using P = WebGpuBindlessRootNativeProgram;
        using U = rhi::OriginalBindlessRootPushUploadContract;
        const auto& required = admission->requirements();
        U::Footprint footprint;
        const auto native = std::uint64_t(admission->originalClosure()->nativeOwnerCount())
            + required.encoders + required.commandBuffers;
        if (native > UINT32_MAX)
            return "RHI-ORIGINAL-ROOT-PUSH-UPLOAD-NATIVE-CAPACITY";
        if (const auto* id = U::validateFootprint(request.program.pushUpload,
                {static_cast<std::uint32_t>(native),
                    results->retainedOwnerCount() + C::additionalOwners},
                {P::pushNativeObjects, P::pushRetainedOwners},
                admission->originalClosure()->originalInventory()->originalBudget(),
                seal->originalBudget(),
                request.maxRetainedOwners,
                footprint))
            return id;
        additionalOwners = P::pushRetainedOwners;
    }
    return C::validateOwners(results->retainedOwnerCount() + additionalOwners,
        request,
        admission->originalClosure()->originalInventory()->originalBudget(),
        seal->originalBudget());
}

std::shared_ptr<const WebGpuBindlessRootColdCommand>
WebGpuNativeCommandPreparation::prepareBindlessRoot(
    std::shared_ptr<const WebGpuBindlessRootColdAdmission> admission,
    std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> results,
    const rhi::OriginalBindlessRootColdCommandRequest& request,
    const char*& diagnostic,
    WebGpuRhiAllocator* allocator)
{
    if ((diagnostic = inspectBindlessRootColdCommand(admission, results, request)))
        return {};
    const auto& seal = admission->originalReservation()->originalSeal();
    if (seal->nativeColdCommandClaimed_) {
        diagnostic = "RHI-WEBGPU-ROOT-COLD-COMMAND-DUPLICATE";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-ROOT-COLD-COMMAND-SDK-UNAVAILABLE";
    return {};
#else
    const auto pipeline = admission->originalClosure()->originalPipelineOwnership();
    if (!pipeline || !pipeline->issuedContract()) {
        diagnostic = "RHI-WEBGPU-ROOT-COLD-COMMAND-PIPELINE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(admission->originalRoot()->originalMetadataQuota());
        auto command = allocate({});
        command->impl_->canonicalColdAdmission = admission;
        if (!connect(*command, pipeline->issuedContract()->nativeDevice(), diagnostic))
            return {};
        auto candidate = core::makeUnique<WebGpuBindlessRootColdCommand>(
            WebGpuBindlessRootColdCommand::ConstructionKey{},
            admission,
            std::move(results),
            command,
            request,
            command->impl_->device,
            command->impl_->queue,
            command->recordingIdentity());
        if ((diagnostic = prepareBindlessRootProgram(*candidate)))
            return {};
        if ((diagnostic = prepareBindlessRootPushUpload(*candidate, allocator)))
            return {};
        auto result = WebGpuBindlessRootColdCommand::shareOriginal(std::move(candidate));
        if ((diagnostic = validateBindlessRootUnclaimed(*result)))
            return {};
        seal->nativeColdCommandClaimed_ = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ROOT-COLD-COMMAND-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-ROOT-COLD-COMMAND-ALLOCATION";
    }
    return {};
#endif
}

const char* WebGpuNativeCommandPreparation::validateBindlessRootUnclaimed(
    const WebGpuBindlessRootColdCommand& original) noexcept
{
    if (const auto* id = inspectBindlessRootColdCommand(
            original.admission_, original.results_, original.request_))
        return id;
    if (const auto* id = validateBindlessRootProgram(original))
        return id;
    if (!core::SharedOwnerProjection::sameOwner(original.originalMetadataQuota(),
            original.admission_->originalRoot()->originalMetadataQuota()))
        return "RHI-WEBGPU-ROOT-COLD-COMMAND-QUOTA";
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ROOT-COLD-COMMAND-SDK-UNAVAILABLE";
#else
    const auto pipeline = original.admission_->originalClosure()->originalPipelineOwnership();
    if (!pipeline || !pipeline->issuedContract())
        return "RHI-WEBGPU-ROOT-COLD-COMMAND-PIPELINE";
    if (!original.command_ || !original.command_->impl_)
        return "RHI-WEBGPU-ROOT-COLD-COMMAND-SHELL-OWNER";
    const auto& native = *original.command_->impl_;
    if (native.encoder || native.commandBuffer || native.computePass || native.renderPass
        || native.finished || native.submitted)
        return "RHI-WEBGPU-ROOT-COLD-COMMAND-SHELL-OPEN";
    switch (inspectShellIdentity(original.command_,
        original.admission_,
        original.device_,
        original.queue_,
        pipeline->issuedContract()->nativeDevice(),
        original.identity_)) {
    case ShellIdentity::Complete:
        return nullptr;
    case ShellIdentity::Owner:
        return "RHI-WEBGPU-ROOT-COLD-COMMAND-SHELL-OWNER";
    case ShellIdentity::Mode:
        return "RHI-WEBGPU-ROOT-COLD-COMMAND-SHELL-MODE";
    case ShellIdentity::Native:
        return "RHI-WEBGPU-ROOT-COLD-COMMAND-SHELL-NATIVE";
    }
    return "RHI-WEBGPU-ROOT-COLD-COMMAND-SHELL-IDENTITY";
#endif
}

const char* WebGpuNativeCommandPreparation::validateBindlessRoot(
    const WebGpuBindlessRootColdCommand& original) noexcept
{
    if (const auto* id = validateBindlessRootUnclaimed(original))
        return id;
    return original.admission_->originalReservation()->originalSeal()->nativeColdCommandClaimed_
        ? nullptr
        : "RHI-WEBGPU-ROOT-COLD-COMMAND-CLAIM";
}

const char* WebGpuNativeCommandPreparation::prepareBindlessRootProgram(
    WebGpuBindlessRootColdCommand& original) noexcept
{
    if (original.request_.contractVersion == 1u)
        return original.program_ ? "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-PROFILE" : nullptr;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ROOT-PROGRAM-SDK-UNAVAILABLE";
#else
    if (!original.program_)
        return "RHI-WEBGPU-ROOT-PROGRAM-MISSING";
    auto limits = WGPU_LIMITS_INIT;
    if (wgpuDeviceGetLimits(static_cast<WGPUDevice>(original.device_), &limits)
        != WGPUStatus_Success)
        return "RHI-WEBGPU-ROOT-PROGRAM-DEVICE-LIMITS";
    auto& program = *original.program_;
    program.dispatchLimit_ = limits.maxComputeWorkgroupsPerDimension;
    program.pushLimits_ = {limits.maxBufferSize,
        limits.maxUniformBufferBindingSize,
        limits.minUniformBufferOffsetAlignment};
    const auto& prepared =
        original.admission_->originalClosure()->originalRoot()->originalPrepared();
    const char* captureDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-PREPARED";
    const auto captured = std::visit(
        [&](const auto& binding) {
            const auto pipeline = webGpuNativePayloadFor(*binding->owners()->desc().pipeline);
            const auto group = webGpuOriginalBindlessPhysicalGroup(binding);
            if (!pipeline || !group)
                return false;
            if constexpr (std::is_same_v<typename std::decay_t<decltype(binding)>::element_type,
                              const rhi::BindlessPreparedComputeBinding>)
                program.pipeline_ = pipeline->pipeline;
            else {
                program.renderPipeline_ = pipeline->pipeline;
                if (!original.request_.program.pushUpload.empty()) {
                    if (!pipeline->usesPushConstantGroup
                        || pipeline->pushConstantGroup == UINT32_MAX
                        || pipeline->pushConstantBinding == UINT32_MAX) {
                        captureDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-PUSH-INTERFACE";
                        return false;
                    }
                    program.pushGroupIndex_ = pipeline->pushConstantGroup;
                    program.pushBinding_ = pipeline->pushConstantBinding;
                    program.pushBindingBytes_ = binding->owners()
                                                    ->desc()
                                                    .pipeline->shaderReflection()
                                                    .pushConstantByteCount;
                }
            }
            program.group_ = group->group;
            const auto& sets = pipeline->issuedContract()->sets();
            const auto mapping = std::find_if(sets.begin(), sets.end(), [&](const auto& m) {
                return m.logicalSet == binding->owners()->desc().snapshot->setIndex();
            });
            if (mapping == sets.end())
                return false;
            program.groupIndex_ = mapping->webGpuGroup;
            return true;
        },
        prepared);
    if (!captured)
        return captureDiagnostic;
    return inspectBindlessRootProgram(original, &program);
#endif
}

const char* WebGpuNativeCommandPreparation::prepareBindlessRootPushUpload(
    WebGpuBindlessRootColdCommand& original, WebGpuRhiAllocator* allocator)
{
    if (original.request_.program.pushUpload.empty())
        return nullptr;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)allocator;
    return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-RUNTIME";
#else
    auto& program = *original.program_;
    if (!allocator || !program.pushResidentBytes_ || program.pushBuffer_ || program.pushGroup_)
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-ALLOCATION-INTENT";
    const auto allocation = allocator->allocate(
        {"Original Root push upload", program.pushResidentBytes_, rhi::MemoryUsage::CpuToGpu});
    if (!allocation.success || !allocation.releaseToken)
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-DEVICE-BUDGET";
    rhi::BufferDesc desc;
    desc.label = "Original Root push upload";
    desc.sizeBytes = program.pushResidentBytes_;
    desc.usage = rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst;
    desc.memory = rhi::MemoryUsage::CpuToGpu;
    desc.initialState = rhi::ResourceState::ShaderRead;
    const WebGpuNativeBufferInitializer initializer{
        &program, [](const void* context, void* mapped, std::uint64_t size) noexcept {
            const auto& frozen = *static_cast<const WebGpuBindlessRootNativeProgram*>(context);
            if (size != frozen.pushResidentBytes_)
                return false;
            std::memset(mapped, 0, static_cast<std::size_t>(size));
            for (const auto& record : frozen.records()) {
                if (record.original.kind != rhi::CommandUsageKind::PushConstants)
                    continue;
                const auto offset = record.pushSlice.dynamicOffset;
                if (!record.push.data || offset > size || record.push.sizeBytes > size - offset)
                    return false;
                std::memcpy(static_cast<std::uint8_t*>(mapped) + offset,
                    record.push.data,
                    record.push.sizeBytes);
            }
            return true;
        }};
    program.pushBuffer_ = createWebGpuNativeBufferPayload(desc, initializer);
    if (!program.pushBuffer_
        || program.pushBuffer_->ownerDevice != static_cast<WGPUDevice>(original.device_))
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-NATIVE-BUFFER";
    program.pushBuffer_->allocationReleaseToken = allocation.releaseToken;
    program.pushLayout_ =
        wgpuRenderPipelineGetBindGroupLayout(program.renderPipeline_, program.pushGroupIndex_);
    if (!program.pushLayout_)
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-NATIVE-LAYOUT";
    auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = program.pushBinding_;
    entry.buffer = program.pushBuffer_->buffer;
    entry.size = program.pushBindingBytes_;
    auto group = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group.label = webGpuNativeStringView("Original Root push upload");
    group.layout = program.pushLayout_;
    group.entryCount = 1u;
    group.entries = &entry;
    program.pushGroup_ =
        wgpuDeviceCreateBindGroup(static_cast<WGPUDevice>(original.device_), &group);
    return program.pushGroup_ ? nullptr : "RHI-WEBGPU-ROOT-PUSH-UPLOAD-NATIVE-GROUP";
#endif
}

const char* WebGpuNativeCommandPreparation::validateBindlessRootDepthPending(
    const WebGpuBindlessRootColdCommand& original) noexcept
{
    if (original.request_.program.contractVersion != 3u
        && original.request_.program.contractVersion != 5u
        && original.request_.program.contractVersion != 6u)
        return nullptr;
    if (!original.callbackCustodyClaimed_)
        return "RHI-WEBGPU-ROOT-PENDING-DEPTH-CUSTODY";
    const auto& admission = original.originalAdmission();
    const auto& seal = admission->originalReservation()->originalSeal();
    const auto& entries = admission->originalClosure()->entries_;
    bool found = false;
    for (std::size_t i = 0u; i < entries.size(); ++i) {
        const auto& input = entries[i].depthInput;
        if (!input.source)
            continue;
        bool duplicate = false;
        for (std::size_t j = 0u; j < i; ++j)
            if (entries[j].depthInput.source.get() == input.source.get()) {
                duplicate = true;
                break;
            }
        if (duplicate)
            continue;
        if (const auto* id = seal->validateCompletedDepthReadPending(
                input, original.request_.program.contractVersion == 6u ? 4u : 1u))
            return id;
        found = true;
    }
    if (!found)
        return "RHI-WEBGPU-ROOT-PENDING-DEPTH-MISSING";
    if (original.request_.program.contractVersion == 6u) {
        bool colorFound = false;
        // The same frozen publication already authenticated by the closure
        // must also match the original Pending journal and claimed custody.
        for (const auto& group : admission->originalClosure()->ordinaryGroups_)
            for (const auto& publication : group.descriptors->completedTexturePublications) {
                if (!publication.content)
                    continue;
                if (const auto* id = seal->validateCompletedTextureReadPending(
                        *publication.source->originalParentLease()->originalObject(),
                        *publication.content,
                        9u))
                    return id;
                colorFound = true;
            }
        if (!colorFound)
            return "RHI-WEBGPU-ROOT-PENDING-DEPTH-COLOR-MISSING";
    }
    return nullptr;
}

const char* WebGpuNativeCommandPreparation::validateBindlessRootProgram(
    const WebGpuBindlessRootColdCommand& original) noexcept
{
    if (original.request_.contractVersion == 1u)
        return original.program_ ? "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-PROFILE" : nullptr;
    return inspectBindlessRootProgram(original, nullptr);
}

const char* WebGpuNativeCommandPreparation::inspectBindlessRootProgram(
    const WebGpuBindlessRootColdCommand& original,
    WebGpuBindlessRootNativeProgram* destination) noexcept
{
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)original;
    (void)destination;
    return "RHI-WEBGPU-ROOT-PROGRAM-SDK-UNAVAILABLE";
#else
    using namespace rhi;
    using Contract = OriginalBindlessRootProgramContract;
    using K = CommandUsageKind;
    if (!original.program_)
        return "RHI-WEBGPU-ROOT-PROGRAM-MISSING";
    const auto& frozen = *original.program_;
    const auto& request = original.request_.program;
    const auto& binding = original.admission_->originalReadbackBinding();
    const auto& closure = *original.admission_->originalClosure();
    const auto& inputs = binding->originalGraph()->originalInputs();
    const auto& usage = *inputs->originalScalars()->originalUsage();
    const auto& operations = binding->originalProgram()->operations();
    if (const auto* id = Contract::validateCounts(operations.size(), 0u, request))
        return id;
    if (!destination && frozen.records().size() != operations.size())
        return "RHI-WEBGPU-ROOT-PROGRAM-COUNT";
    if (destination && !destination->records().empty())
        return "RHI-WEBGPU-ROOT-PROGRAM-IMMUTABLE";
    if (const auto* id = Contract::validateDispatch(0u, 0u, 0u, frozen.dispatchLimit_))
        return id;
    const bool produced = request.contractVersion == 7u;
    const bool scene = !original.admission_->originalSceneContent().empty();
    const bool fullPass = produced || scene;
    const bool graphics =
        request.contractVersion == 2u || request.contractVersion == 6u || produced;
    const bool geometry = request.initializedGeometryContractVersion == 1u || produced;
    if (!produced && geometry != closure.hasInitializedGeometry())
        return "RHI-WEBGPU-ROOT-PROGRAM-GEOMETRY-CORRESPONDENCE";
    const bool pushUpload = !request.pushUpload.empty();
    const auto& consumption =
        binding->originalProgram()->originalBudget().bindlessRoots.pushConsumption;
    if (pushUpload
        && (consumption.diagnostic() || consumption.recordingContractVersion != 1u
            || !consumption.recordingOptIn))
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-CONSUMPTION";
    OriginalCommandPushConsumption pushConsumer;
    OriginalCommandPushUploadPlan upload(request.pushUpload, frozen.pushLimits_);
    const RenderPipeline* graphicsObject = graphics && !produced
        ? std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
              closure.root_->originalPrepared())
              ->owners()
              ->desc()
              .pipeline.get()
        : nullptr;
    if (pushUpload && !destination
        && (!frozen.pushBuffer_ || !frozen.pushBuffer_->allocationReleaseToken || !frozen.pushGroup_
            || !frozen.pushLayout_
            || frozen.pushBuffer_->ownerDevice != static_cast<WGPUDevice>(original.device_)
            || frozen.pushBuffer_->sizeBytes != frozen.pushResidentBytes_))
        return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-CUSTODY";
    const bool mixed = request.contractVersion == 5u;
    const bool storage = request.contractVersion == 4u || mixed;
    const auto& nextClosure = original.admission_->nextClosure();
    const auto& finalClosure = original.admission_->finalClosure();
    if (mixed) {
        if (const auto* id = WebGpuBindlessRootNativeClosure::validateOriginal(nextClosure))
            return id;
        if (const auto* id = WebGpuBindlessRootNativeClosure::validateOriginal(finalClosure))
            return id;
        const auto visits = std::uint64_t(closure.root_->size())
            + nextClosure->originalRoot()->size() + finalClosure->originalRoot()->size();
        const auto flow = OriginalBindlessMixedEntryFlowContract::inspect(
            {closure.root_, nextClosure->originalRoot(), finalClosure->originalRoot()},
            closure.inventory_,
            {1u, true, visits});
        if (flow.diagnostic)
            return flow.diagnostic;
    } else if (finalClosure && !produced)
        return "RHI-WEBGPU-ROOT-PROGRAM-INPUT-SCHEMA";
    else if (storage) {
        if (const auto* id = WebGpuBindlessRootNativeClosure::validateOriginal(nextClosure))
            return id;
        if (const auto* id = OriginalBindlessStorageExecutionContract::validatePair(
                *closure.root_, *nextClosure->originalRoot()))
            return id;
    }
    const char* preparedDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-PREPARED";
    const bool preparedMatches = std::visit(
        [&](const auto& binding) {
            constexpr bool compute =
                std::is_same_v<typename std::decay_t<decltype(binding)>::element_type,
                    const BindlessPreparedComputeBinding>;
            if (compute == (graphics && !produced))
                return false;
            const auto pipeline = webGpuNativePayloadFor(*binding->owners()->desc().pipeline);
            const auto group = webGpuOriginalBindlessPhysicalGroup(binding);
            if (!pipeline || !group || !frozen.group_ || frozen.group_ != group->group)
                return false;
            const auto& sets = pipeline->issuedContract()->sets();
            const auto mapping = std::find_if(sets.begin(), sets.end(), [&](const auto& m) {
                return m.logicalSet == binding->owners()->desc().snapshot->setIndex();
            });
            if (mapping == sets.end() || mapping->webGpuGroup != frozen.groupIndex_)
                return false;
            if constexpr (compute)
                return frozen.pipeline_ && frozen.pipeline_ == pipeline->pipeline
                    && !frozen.renderPipeline_;
            else {
                const auto& compatibility = pipeline->issuedContract()->graphics();
                if (scene) {
                    const auto& object = *binding->owners()->desc().pipeline;
                    const auto* definition = webGpuRhiBindlessGraphicsPipelineState(object);
                    if (!geometry || !frozen.renderPipeline_
                        || frozen.renderPipeline_ != pipeline->pipeline || frozen.pipeline_) {
                        preparedDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-SCENE-PIPELINE";
                        return false;
                    }
                    if (!compatibility.matches(*pipeline) || !definition) {
                        preparedDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-SCENE-DEFINITION";
                        return false;
                    }
                    if (const auto* id = semantics::BindlessSceneRasterContract::inspect(
                            Backend::WebGPU, *definition)) {
                        preparedDiagnostic = id;
                        return false;
                    }
                    return true;
                }
                return frozen.renderPipeline_ && frozen.renderPipeline_ == pipeline->pipeline
                    && !frozen.pipeline_ && compatibility.matches(*pipeline)
                    && (geometry ? compatibility.vertexBufferCount != 0u
                                 : !compatibility.vertexBufferCount)
                    && compatibility.sampleCount == 1u
                    && compatibility.depthStencilFormat == TextureFormat::Unknown
                    && compatibility.colorFormats.size() == 1u
                    && compatibility.colorFormats[0] == TextureFormat::Rgba8Unorm;
            }
        },
        closure.root_->originalPrepared());
    if (!preparedMatches)
        return preparedDiagnostic;
    // One bounded traversal supplies both freezing and allocation-free revalidation.
    // The original graph already checks source provenance, all ranges and state
    // transitions. This consumer admits only native copy/compute usage scopes.
    std::uint64_t visits = 0u;
    const auto visit = [&]() noexcept {
        if (visits == request.maxOperationVisits)
            return false;
        ++visits;
        return true;
    };
    const char* lookupDiagnostic = nullptr;
    WebGpuBindlessRootNativeClosure::Entry readbackEntry;
    const auto entryAt =
        [&](std::size_t source) noexcept -> const WebGpuBindlessRootNativeClosure::Entry* {
        for (const auto& selected : original.admission_->originalRootSequence().closures()) {
            if (!visit()) {
                lookupDiagnostic = "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                return nullptr;
            }
            for (const auto& entry : selected->entries_) {
                if (!visit()) {
                    lookupDiagnostic = "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    return nullptr;
                }
                if (entry.sourceIndex == source)
                    return &entry;
            }
        }
        // Ordinary result destinations are not shader descriptors. Borrow
        // their exact native identity from the original result storage, which
        // already retains the payload through map drain and retirement.
        const auto& bindings = original.admission_->originalReadbackBindings();
        const auto& buffers = original.results_->buffers_.values();
        if (buffers.size() != bindings.size()) {
            lookupDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-READBACK-COUNT";
            return nullptr;
        }
        for (std::size_t i = 0u; i < bindings.size(); ++i) {
            if (!visit()) {
                lookupDiagnostic = "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                return nullptr;
            }
            const auto& lease = bindings[i]->originalReadbackSource();
            if (closure.inventory_->originalLeaseIndex(lease) != source)
                continue;
            const auto& retained = buffers[i];
            const auto payload = webGpuNativePayloadFor(*lease->originalObject());
            if (!payload || !core::SharedOwnerProjection::sameOwner(retained.buffer, payload)
                || !retained.handle || retained.handle != payload->buffer
                || retained.device != static_cast<WGPUDevice>(original.device_)
                || retained.device != payload->ownerDevice || retained.bytes != payload->sizeBytes
                || retained.bytes != lease->layout().sizeBytes) {
                lookupDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-READBACK-IDENTITY";
                return nullptr;
            }
            readbackEntry.sourceIndex = source;
            readbackEntry.resource = lease->resource();
            readbackEntry.generation =
                lease->originalObject()->originalResourceRegistration().generation();
            readbackEntry.bytes = retained.bytes;
            readbackEntry.handle = retained.handle;
            readbackEntry.device = retained.device;
            return &readbackEntry;
        }
        lookupDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-RESOURCE";
        return nullptr;
    };
    const auto attachmentAt =
        [&](std::size_t source) noexcept -> const WebGpuBindlessRootNativeClosure::Attachment* {
        for (const auto& selected : original.admission_->originalRootSequence().closures()) {
            if (!visit()) {
                lookupDiagnostic = "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                return nullptr;
            }
            for (const auto& entry : selected->attachments_) {
                if (!visit()) {
                    lookupDiagnostic = "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    return nullptr;
                }
                if (entry.sourceIndex == source || entry.parentSourceIndex == source)
                    return &entry;
            }
        }
        lookupDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-ATTACHMENT";
        return nullptr;
    };
    const auto resolveView = [&](GpuResourceId id) noexcept -> WGPUTextureView {
        for (const auto& selected : original.admission_->originalRootSequence().closures()) {
            if (!visit()) {
                lookupDiagnostic = "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                return nullptr;
            }
            for (const auto& entry : selected->attachments_) {
                if (!visit()) {
                    lookupDiagnostic = "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    return nullptr;
                }
                if (entry.resource == id)
                    return entry.handle;
            }
        }
        lookupDiagnostic = "RHI-WEBGPU-ROOT-PROGRAM-ATTACHMENT";
        return nullptr;
    };
    std::size_t passCount = 0u, computeBinds = 0u, computeDispatches = 0u;
    std::size_t graphicsRootSource = graphics && !produced ? binding->rootSourceIndex() : SIZE_MAX;
    std::uint64_t passDraws = 0u;
    const WebGpuBindlessRootNativeClosure::Attachment* currentAttachment = nullptr;
    const WebGpuBindlessRootNativeClosure* currentPassRoot = nullptr;
    bool viewportSet = false, scissorSet = false;
    semantics::CommandRecordingOrder order(
        binding->originalProgram()->originalBudget().stateSequence.maxDebugDepth);
    for (std::size_t i = 0u; i < operations.size(); ++i) {
        if (!visit())
            return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
        const auto& op = operations[i];
        if (const auto* id = Contract::validateOperation(op.kind, request))
            return id;
        if (const auto* id = order.append(op.kind))
            return id;
        WebGpuBindlessRootNativeProgram::Record record;
        record.original = op;
        for (std::size_t reference = 0u; reference < op.referenceCount; ++reference)
            if (!visit())
                return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
        switch (op.kind) {
        case K::RetainIndexedSnapshot:
            if (!geometry || produced || op.referenceCount != 1u
                || usage.references()[op.firstReference].sourceIndex != binding->rootSourceIndex())
                return "RHI-WEBGPU-ROOT-SNAPSHOT-CORRESPONDENCE";
            // The authentic admission strongly owns this Root and its snapshot.
            // This command adds lifetime intent, not a native resource binding.
            record.closure = &closure;
            break;
        case K::BufferBarrier: {
            if (op.referenceCount != 1u)
                return "RHI-WEBGPU-ROOT-PROGRAM-BARRIER";
            const auto* entry = entryAt(usage.references()[op.firstReference].sourceIndex);
            if (!entry)
                return lookupDiagnostic;
            record.destination = entry->handle;
            break;
        }
        case K::ClearSnapshotFault: {
            if (op.referenceCount != 1u)
                return "RHI-WEBGPU-ROOT-PROGRAM-ROOT";
            const auto source = usage.references()[op.firstReference].sourceIndex;
            const WebGpuBindlessRootNativeClosure* selected = nullptr;
            for (const auto& member : original.admission_->originalRootSequence().closures()) {
                if (!visit())
                    return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                if (closure.inventory_->originalLeaseIndex(member->originalRoot()) == source)
                    selected = member.get();
            }
            if (!selected)
                return "RHI-WEBGPU-ROOT-PROGRAM-ROOT";
            const auto& latch = selected->originalRoot()->originalSnapshot()->originalBufferSource(
                BindlessSnapshot::OriginalBufferRole::FaultLatch);
            const auto* entry = entryAt(closure.inventory_->originalLeaseIndex(latch));
            if (!entry)
                return lookupDiagnostic;
            if (entry->resource != latch->resource() || entry->bytes != sizeof(std::uint32_t))
                return "RHI-WEBGPU-ROOT-PROGRAM-FAULT-LATCH";
            record.destination = entry->handle;
            break;
        }
        case K::TextureBarrier: {
            if (op.referenceCount != 1u)
                return "RHI-WEBGPU-ROOT-PROGRAM-ATTACHMENT-BARRIER";
            const auto source = usage.references()[op.firstReference].sourceIndex;
            const auto* descriptor = inputs->descriptorAt(op.commandIndex);
            const auto* dependency =
                descriptor ? std::get_if<ResourceDependencyArguments>(descriptor) : nullptr;
            ResourceBarrierDesc barrier;
            if (!dependency || !dependency->write(K::TextureBarrier, barrier))
                return "RHI-WEBGPU-ROOT-PROGRAM-TEXTURE-BARRIER";
            if (produced && barrier.after == ResourceState::ShaderRead
                && barrier.textureSubresources.aspect == TextureAspect::Color) {
                bool found = false;
                for (const auto& entry : closure.entries_) {
                    if (!visit())
                        return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    if (!entry.viewOwner || entry.parentSourceIndex != source
                        || entry.parentResource != barrier.resource)
                        continue;
                    if (barrier.textureSubresources != entry.sampledRange
                        || barrier.destinationStages != entry.sampledStages
                        || barrier.destinationStages != PipelineStage::ComputeShader
                        || barrier.destinationAccess != ResourceAccess::ShaderRead
                        || (barrier.before != ResourceState::ShaderRead
                            && barrier.before != ResourceState::ShaderWrite
                            && barrier.before != ResourceState::ColorAttachment))
                        return "RHI-BINDLESS-PRODUCED-IMAGE-BARRIER";
                    found = true;
                }
                if (!found)
                    return "RHI-BINDLESS-PRODUCED-IMAGE-BARRIER";
                break;
            }
            if (request.contractVersion == 3u
                || (mixed && barrier.textureSubresources.aspect == TextureAspect::Depth)) {
                bool found = false;
                for (const auto& entry : closure.entries_) {
                    if (!visit())
                        return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    if (!entry.depthInput.source || entry.parentSourceIndex != source
                        || entry.parentResource != barrier.resource)
                        continue;
                    const auto& view =
                        entry.depthInput.source->originalViewSource()->originalObject()->desc();
                    const TextureSubresourceRange visible{view.baseMipLevel,
                        view.mipLevelCount,
                        view.baseArrayLayer,
                        view.arrayLayerCount,
                        view.aspect};
                    if ((barrier.before != ResourceState::DepthAttachment
                            && barrier.before != ResourceState::CopySrc
                            && barrier.before != ResourceState::ShaderRead)
                        || barrier.after != ResourceState::ShaderRead
                        || barrier.textureSubresources != visible
                        || barrier.destinationStages != PipelineStage::ComputeShader
                        || barrier.destinationAccess != ResourceAccess::ShaderRead)
                        return "RHI-WEBGPU-ROOT-PROGRAM-DEPTH-BARRIER";
                    found = true;
                    break;
                }
                if (!found)
                    return "RHI-WEBGPU-ROOT-PROGRAM-DEPTH-BARRIER";
                break;
            }
            if (storage) {
                bool found = false;
                for (const auto& entry : closure.entries_) {
                    if (!visit())
                        return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    if (!entry.viewOwner || entry.parentSourceIndex != source
                        || entry.parentResource != barrier.resource)
                        continue;
                    const auto& key = entry.viewOwner->creationKey;
                    const TextureSubresourceRange visible{key.baseMipLevel,
                        key.mipLevelCount,
                        key.baseArrayLayer,
                        key.arrayLayerCount,
                        TextureAspect::Color};
                    if (barrier.textureSubresources != visible)
                        continue;
                    const bool clear = (barrier.before == ResourceState::Undefined
                                           || barrier.before == ResourceState::ShaderRead)
                        && barrier.after == ResourceState::ColorAttachment;
                    const bool write = barrier.before == ResourceState::ColorAttachment
                        && barrier.after == ResourceState::ShaderWrite;
                    const bool read = barrier.before == ResourceState::ShaderWrite
                        && barrier.after == ResourceState::ShaderRead;
                    if (!clear && !write && !read)
                        return "RHI-WEBGPU-ROOT-PROGRAM-STORAGE-BARRIER";
                    found = true;
                    break;
                }
                if (!found)
                    return "RHI-WEBGPU-ROOT-PROGRAM-STORAGE-BARRIER";
                break;
            }
            if (request.contractVersion == 6u && barrier.after == ResourceState::ShaderRead
                && barrier.textureSubresources.aspect == TextureAspect::Depth) {
                bool found = false;
                for (const auto& entry : closure.entries_) {
                    if (!visit())
                        return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    if (!entry.depthInput.source || entry.parentSourceIndex != source
                        || entry.parentResource != barrier.resource)
                        continue;
                    const auto& view =
                        entry.depthInput.source->originalViewSource()->originalObject()->desc();
                    const TextureSubresourceRange visible{view.baseMipLevel,
                        view.mipLevelCount,
                        view.baseArrayLayer,
                        view.arrayLayerCount,
                        view.aspect};
                    if ((barrier.before != ResourceState::DepthAttachment
                            && barrier.before != ResourceState::CopySrc
                            && barrier.before != ResourceState::ShaderRead)
                        || barrier.textureSubresources != visible
                        || barrier.destinationStages != PipelineStage::FragmentShader
                        || barrier.destinationAccess != ResourceAccess::ShaderRead)
                        return "RHI-WEBGPU-ROOT-PROGRAM-CANONICAL-GRAPHICS-DEPTH-BARRIER";
                    found = true;
                    break;
                }
                if (!found)
                    return "RHI-WEBGPU-ROOT-PROGRAM-CANONICAL-GRAPHICS-DEPTH-BARRIER";
                break;
            }
            if ((closure.root_->originalSnapshot()->plan().originalExecutionContractVersion == 13u
                    || closure.root_->originalSnapshot()->plan().originalExecutionContractVersion
                        == 14u)
                && barrier.after == ResourceState::ShaderRead) {
                bool found = false;
                PipelineStage stages = PipelineStage::None;
                for (const auto& entry : closure.entries_) {
                    if (!visit())
                        return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    if (!entry.viewOwner || entry.parentSourceIndex != source
                        || entry.parentResource != barrier.resource)
                        continue;
                    if (barrier.textureSubresources != entry.sampledRange)
                        return "RHI-WEBGPU-ROOT-PROGRAM-ORDINARY-COLOR-RANGE";
                    stages = stages | entry.sampledStages;
                    found = true;
                }
                if (!found || barrier.destinationStages != stages
                    || barrier.destinationAccess != ResourceAccess::ShaderRead
                    || (barrier.before != ResourceState::CopyDst
                        && barrier.before != ResourceState::ShaderRead))
                    return "RHI-WEBGPU-ROOT-PROGRAM-ORDINARY-COLOR-BARRIER";
                break;
            }
            if ((closure.root_->originalSnapshot()->plan().originalExecutionContractVersion == 3u
                    || closure.root_->originalSnapshot()->plan().originalExecutionContractVersion
                        == 4u)
                && barrier.after == ResourceState::ShaderRead) {
                bool found = false;
                for (const auto& entry : closure.entries_) {
                    if (!visit())
                        return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    if (!entry.viewOwner || entry.parentSourceIndex != source
                        || entry.parentResource != barrier.resource)
                        continue;
                    if (barrier.before != ResourceState::CopyDst
                        && barrier.before != ResourceState::ShaderRead)
                        return "RHI-WEBGPU-ROOT-PROGRAM-SAMPLED-BARRIER";
                    found = true;
                    break;
                }
                if (!found)
                    return "RHI-WEBGPU-ROOT-PROGRAM-SAMPLED-BARRIER";
                break;
            }
            const auto* attachment =
                attachmentAt(usage.references()[op.firstReference].sourceIndex);
            if (!attachment)
                return lookupDiagnostic;
            if (barrier.resource != attachment->parentResource
                || barrier.after
                    != (fullPass && isDepthTextureFormat(attachment->parent->rhiFormat)
                            ? ResourceState::DepthAttachment
                            : ResourceState::ColorAttachment))
                return "RHI-WEBGPU-ROOT-PROGRAM-ATTACHMENT-BARRIER";
            if (fullPass) {
                const auto& view = attachment->owner->creationKey;
                const TextureSubresourceRange visible{view.baseMipLevel,
                    view.mipLevelCount,
                    view.baseArrayLayer,
                    view.arrayLayerCount,
                    isDepthTextureFormat(attachment->parent->rhiFormat) ? TextureAspect::Depth
                                                                        : TextureAspect::Color};
                if (barrier.textureSubresources != visible)
                    return "RHI-WEBGPU-ROOT-PROGRAM-ATTACHMENT-RANGE";
            }
            break;
        }
        case K::BeginRenderPass: {
            if (!op.referenceCount || (!fullPass && op.referenceCount != 1u) || currentAttachment)
                return "RHI-WEBGPU-ROOT-PROGRAM-PASS";
            const auto* descriptor = inputs->descriptorAt(op.commandIndex);
            const auto* pass = descriptor
                ? std::get_if<std::shared_ptr<const RenderPassArguments>>(descriptor)
                : nullptr;
            if (!pass || !*pass)
                return "RHI-WEBGPU-ROOT-PROGRAM-PASS";
            const auto& desc = (*pass)->desc();
            if (fullPass) {
                using Pass = OriginalBindlessGraphicsPassContract;
                Pass::RootSelection selection;
                if (const auto* id = Pass::selectRoot(operations,
                        usage.references(),
                        op.commandIndex,
                        request.maxOperationVisits - visits,
                        selection))
                    return id;
                visits += selection.visits;
                currentPassRoot = original.admission_->originalRootSequence().select(
                    closure.originalInventory(), selection.source);
                if (!currentPassRoot || currentPassRoot->budget_.producedAttachments.empty())
                    return "RHI-WEBGPU-ROOT-PROGRAM-PASS-ROOT";
                const auto* prepared =
                    std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                        &currentPassRoot->originalRoot()->originalPrepared());
                if (!prepared || !*prepared)
                    return "RHI-WEBGPU-ROOT-PROGRAM-PASS-ROOT";
                const auto& pipeline = (*prepared)->owners()->desc().pipeline;
                const auto native = webGpuNativePayloadFor(*pipeline);
                const auto* definition = scene ? webGpuRhiBindlessGraphicsPipelineState(*pipeline)
                                               : pipeline->originalRenderPipelineDefinition();
                if (!native || !native->issuedContract() || !definition
                    || !native->issuedContract()->graphics().matches(*native))
                    return "RHI-WEBGPU-ROOT-PROGRAM-PASS-PIPELINE";
                if (!scene)
                    if (const auto* id = pipeline->originalFullDefinitionDiagnostic())
                        return id;
                if (scene) {
                    if (const auto* id = semantics::BindlessSceneRasterContract::inspect(
                            Backend::WebGPU, *definition))
                        return id;
                }
                if (const auto* id = Pass::inspectPass(desc, *definition))
                    return id;
                const auto count =
                    desc.colorAttachments.size() + std::size_t(desc.hasDepthStencilAttachment);
                if (op.referenceCount != count || count > Pass::maxAttachments
                    || op.firstReference > usage.references().size()
                    || count > usage.references().size() - op.firstReference)
                    return "RHI-WEBGPU-ROOT-PROGRAM-PASS-REFERENCES";
                std::array<GpuResourceId, Pass::maxAttachments> parents{};
                for (std::size_t i = 0u; i < count; ++i) {
                    if (!visit())
                        return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    const auto* attachment =
                        attachmentAt(usage.references()[op.firstReference + i].sourceIndex);
                    if (!attachment)
                        return lookupDiagnostic;
                    const bool depth = i == desc.colorAttachments.size();
                    const auto target = depth ? desc.depthStencilAttachment.target
                                              : desc.colorAttachments[i].target;
                    const auto format = depth ? desc.depthStencilAttachment.format
                                              : desc.colorAttachments[i].format;
                    if (attachment->resource != target || attachment->parent->rhiFormat != format
                        || attachment->device != static_cast<WGPUDevice>(original.device_)
                        || attachment->parent->sampleCount != 1u
                        || (currentAttachment
                            && (currentAttachment->parent->extent.width
                                    != attachment->parent->extent.width
                                || currentAttachment->parent->extent.height
                                    != attachment->parent->extent.height)))
                        return "RHI-WEBGPU-ROOT-PROGRAM-PASS-ATTACHMENT";
                    for (std::size_t prior = 0u; prior < i; ++prior) {
                        if (!visit())
                            return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                        if (parents[prior] == attachment->parentResource)
                            return "RHI-WEBGPU-ROOT-PROGRAM-PASS-ATTACHMENT-ALIAS";
                    }
                    parents[i] = attachment->parentResource;
                    if (!currentAttachment)
                        currentAttachment = attachment;
                }
            } else {
                currentAttachment = attachmentAt(usage.references()[op.firstReference].sourceIndex);
                if (!currentAttachment)
                    return lookupDiagnostic;
                if (desc.colorAttachments.size() != 1u || desc.hasDepthStencilAttachment
                    || !desc.subpasses.empty() || !desc.subpassDependencies.empty()
                    || desc.layeredRendering
                    || desc.colorAttachments[0].target != currentAttachment->resource
                    || desc.colorAttachments[0].format
                        != (storage ? TextureFormat::Rgba16Float : TextureFormat::Rgba8Unorm)
                    || desc.colorAttachments[0].hasResolveTarget
                    || desc.colorAttachments[0].loadOp != LoadOp::Clear
                    || desc.colorAttachments[0].storeOp != StoreOp::Store)
                    return "RHI-WEBGPU-ROOT-PROGRAM-PASS-SCOPE";
            }
            record.passIndex = passCount;
            if (destination) {
                if (!destination->passes_.append(WebGpuOriginalRenderPassPayload(pass->get(), 0u)))
                    return "RHI-ORIGINAL-ROOT-PROGRAM-COMMAND-CAPACITY";
            } else if (passCount >= frozen.passes_.values().size()
                || frozen.passes_.values()[passCount].pass != pass->get())
                return "RHI-WEBGPU-ROOT-PROGRAM-PASS-OWNER";
            ++passCount;
            passDraws = 0u;
            viewportSet = scissorSet = false;
            break;
        }
        case K::SetViewport: {
            const auto* value = inputs->originalScalars()
                                    ->originalArguments()
                                    ->records()[op.commandIndex]
                                    .scalar.get<ViewportState>();
            if (!currentAttachment || !value)
                return "RHI-WEBGPU-ROOT-PROGRAM-VIEWPORT";
            if (const auto* id = semantics::viewportCommandDiagnostic(*value))
                return id;
            const auto& extent = currentAttachment->parent->extent;
            if (!std::isfinite(value->x) || !std::isfinite(value->y) || !std::isfinite(value->width)
                || !std::isfinite(value->height) || !std::isfinite(value->minDepth)
                || !std::isfinite(value->maxDepth) || value->minDepth < 0.0f
                || value->maxDepth > 1.0f || value->x < 0.0f || value->y < 0.0f
                || value->x + value->width > extent.width
                || value->y + value->height > extent.height)
                return "RHI-WEBGPU-ROOT-PROGRAM-VIEWPORT-BOUNDS";
            record.viewport = *value;
            viewportSet = true;
            break;
        }
        case K::SetScissor: {
            const auto* value = inputs->originalScalars()
                                    ->originalArguments()
                                    ->records()[op.commandIndex]
                                    .scalar.get<ScissorState>();
            if (!currentAttachment || !value)
                return "RHI-WEBGPU-ROOT-PROGRAM-SCISSOR";
            if (const auto* id = semantics::scissorCommandDiagnostic(*value))
                return id;
            const auto& extent = currentAttachment->parent->extent;
            if (value->x < 0 || value->y < 0
                || !core::containsUnsignedRange(std::uint64_t{0u},
                    std::uint64_t{extent.width},
                    static_cast<std::uint64_t>(value->x),
                    std::uint64_t{value->width})
                || !core::containsUnsignedRange(std::uint64_t{0u},
                    std::uint64_t{extent.height},
                    static_cast<std::uint64_t>(value->y),
                    std::uint64_t{value->height}))
                return "RHI-WEBGPU-ROOT-PROGRAM-SCISSOR-BOUNDS";
            record.scissor = *value;
            scissorSet = true;
            break;
        }
        case K::BindVertexBuffer:
        case K::BindIndexBuffer: {
            if (!geometry || !graphicsObject || !currentAttachment || op.referenceCount != 1u)
                return "RHI-WEBGPU-ROOT-PROGRAM-GEOMETRY-BINDING";
            const auto source = usage.references()[op.firstReference].sourceIndex;
            const auto* entry = entryAt(source);
            if (!entry || !entry->initializedGeometry || !entry->handle)
                return "RHI-WEBGPU-ROOT-PROGRAM-GEOMETRY-CUSTODY";
            const auto& scalar =
                inputs->originalScalars()->originalArguments()->records()[op.commandIndex].scalar;
            std::uint64_t offset = 0u;
            if (op.kind == K::BindVertexBuffer) {
                const auto* value = scalar.get<CommandScalarArguments::VertexBinding>();
                const auto* layouts = graphicsObject->bindlessVertexBuffers(1u);
                if (!value || !layouts)
                    return "RHI-WEBGPU-ROOT-PROGRAM-VERTEX-DEFINITION";
                record.vertex = *value;
                offset = value->offset;
                for (std::size_t slot = 0u; slot < layouts->size(); ++slot) {
                    if (!visit())
                        return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                    if ((*layouts)[slot].slot == value->slot) {
                        if (slot >= UINT32_MAX)
                            return "RHI-WEBGPU-ROOT-PROGRAM-VERTEX-SLOT";
                        record.physicalVertexSlot = static_cast<std::uint32_t>(slot);
                        break;
                    }
                }
                if (record.physicalVertexSlot == UINT32_MAX)
                    return "RHI-WEBGPU-ROOT-PROGRAM-VERTEX-SLOT";
            } else {
                const auto* value = scalar.get<CommandScalarArguments::IndexBinding>();
                if (!value
                    || (value->format != IndexFormat::Uint16
                        && value->format != IndexFormat::Uint32))
                    return "RHI-WEBGPU-ROOT-PROGRAM-INDEX-DEFINITION";
                record.index = *value;
                offset = value->offset;
            }
            if (offset >= entry->bytes)
                return "RHI-WEBGPU-ROOT-PROGRAM-GEOMETRY-RANGE";
            record.bufferBindingBytes = entry->bytes - offset;
            record.source = entry->handle;
            break;
        }
        case K::DrawIndexedIndirect: {
            const auto* value = inputs->originalScalars()
                                    ->originalArguments()
                                    ->records()[op.commandIndex]
                                    .scalar.get<CommandScalarArguments::DrawIndirect>();
            if (!geometry || !graphicsObject || !currentAttachment || !viewportSet || !scissorSet
                || !value || op.referenceCount != 1u
                || binding->originalProgram()->originalBudget().bindlessRoots.contractVersion != 6u)
                return "RHI-WEBGPU-ROOT-PROGRAM-PRODUCED-DRAW";
            const auto source = usage.references()[op.firstReference].sourceIndex;
            const auto* entry = entryAt(source);
            if (!entry || !entry->handle)
                return lookupDiagnostic ? lookupDiagnostic : "RHI-WEBGPU-ROOT-PROGRAM-RESOURCE";
            const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                &closure.inventory_->sources()[source]);
            std::uint64_t extent = 0u;
            if (!lease)
                return "RHI-WEBGPU-ROOT-PROGRAM-PRODUCED-LEASE";
            if (const auto* id = OriginalCommandIndirectAccesses::inspectRange(
                    (*lease)->originalObject()->desc(), *value, extent))
                return id;
            if (value->offset > entry->bytes || extent > entry->bytes - value->offset
                || value->count > UINT64_MAX - passDraws)
                return "RHI-WEBGPU-ROOT-PROGRAM-PRODUCED-RANGE";
            if (pushUpload)
                if (const auto* id =
                        pushConsumer.consume(graphicsObject->shaderReflection(), consumption))
                    return id;
            record.indirect = *value;
            record.source = entry->handle;
            passDraws += value->count;
            break;
        }
        case K::DrawIndexed: {
            const auto* value = inputs->originalScalars()
                                    ->originalArguments()
                                    ->records()[op.commandIndex]
                                    .scalar.get<CommandScalarArguments::DrawIndexed>();
            if (!geometry || !currentAttachment || !viewportSet || !scissorSet || !value
                || !value->indices || !value->instances)
                return "RHI-WEBGPU-ROOT-PROGRAM-INDEXED-DRAW";
            if (pushUpload)
                if (const auto* id =
                        pushConsumer.consume(graphicsObject->shaderReflection(), consumption))
                    return id;
            record.indexed = *value;
            ++passDraws;
            break;
        }
        case K::Draw: {
            if (pushUpload)
                if (const auto* id =
                        pushConsumer.consume(graphicsObject->shaderReflection(), consumption))
                    return id;
            const auto* value = inputs->originalScalars()
                                    ->originalArguments()
                                    ->records()[op.commandIndex]
                                    .scalar.get<CommandScalarArguments::Draw>();
            if (!currentAttachment || !viewportSet || !scissorSet || !value || !value->vertices
                || !value->instances)
                return "RHI-WEBGPU-ROOT-PROGRAM-DRAW";
            record.draw = *value;
            ++passDraws;
            break;
        }
        case K::PushConstants: {
            if (!graphicsObject || !currentAttachment || op.referenceCount != 1u
                || usage.references()[op.firstReference].sourceIndex != graphicsRootSource)
                return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-ROOT";
            if (const auto* id = pushConsumer.update(inputs->originalScalars()->originalArguments(),
                    op.commandIndex,
                    graphicsObject->layout()->desc(),
                    graphicsObject->shaderReflection(),
                    ShaderStage::Vertex | ShaderStage::Fragment,
                    consumption,
                    record.push))
                return id;
            if (const auto* id = upload.append(
                    record.push.sizeBytes, frozen.pushBindingBytes_, record.pushSlice))
                return id;
            break;
        }
        case K::EndRenderPass: {
            if (!currentAttachment || (storage ? bool(passDraws) : !passDraws))
                return "RHI-WEBGPU-ROOT-PROGRAM-PASS";
            if (destination) {
                const char* failure = nullptr;
                std::size_t index = 0u;
                destination->passes_.visitMutable([&](auto& pass) {
                    if (!visit()) {
                        failure = "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                        return;
                    }
                    if (index++ != passCount - 1u)
                        return;
                    pass.maxDraw.maxDrawCount = passDraws;
                    failure = pass.finalize(resolveView);
                });
                if (failure)
                    return failure;
            } else {
                // Account for the same traversal used during freezing.
                for (std::size_t index = 0u; index < passCount; ++index)
                    if (!visit())
                        return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                if (!frozen.passes_.values()[passCount - 1u].matches(resolveView, passDraws))
                    return "RHI-WEBGPU-ROOT-PROGRAM-PASS-FIELDS";
            }
            currentAttachment = nullptr;
            currentPassRoot = nullptr;
            break;
        }
        case K::BindPreparedGraphicsBinding:
        case K::BindPreparedComputeBinding: {
            pushConsumer.onBind();
            if (op.referenceCount != 1u || op.firstReference >= usage.references().size())
                return "RHI-WEBGPU-ROOT-PROGRAM-ROOT";
            const auto* selected = original.admission_->originalRootSequence().select(
                closure.originalInventory(), usage.references()[op.firstReference].sourceIndex);
            if (!selected)
                return "RHI-WEBGPU-ROOT-PROGRAM-UNRETAINED-ROOT";
            record.closure = selected;
            const auto source = closure.inventory_->originalLeaseIndex(selected->originalRoot());
            if (op.referenceCount != 1u
                || usage.references()[op.firstReference].sourceIndex != source
                || (storage
                    && (computeBinds >= (mixed ? 3u : 2u) || computeBinds != computeDispatches
                        || selected
                            != original.admission_->originalRootSequence()
                                .atOrEmpty(computeBinds)
                                .get())))
                return "RHI-WEBGPU-ROOT-PROGRAM-ROOT";
            if (op.kind == K::BindPreparedComputeBinding) {
                ++computeBinds;
                const auto& prepared =
                    std::get<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                        selected->originalRoot()->originalPrepared());
                const auto pipeline = webGpuNativePayloadFor(*prepared->owners()->desc().pipeline);
                const auto group = webGpuOriginalBindlessPhysicalGroup(prepared);
                if (!pipeline || !pipeline->issuedContract() || !pipeline->pipeline || !group
                    || !group->group)
                    return "RHI-WEBGPU-ROOT-PROGRAM-PREPARED";
                const auto& sets = pipeline->issuedContract()->sets();
                const auto mapping = std::find_if(sets.begin(), sets.end(), [&](const auto& m) {
                    return m.logicalSet == prepared->owners()->desc().snapshot->setIndex();
                });
                if (mapping == sets.end())
                    return "RHI-WEBGPU-ROOT-PROGRAM-PREPARED";
                record.computePipeline = pipeline->pipeline;
                record.group = group->group;
                record.groupIndex = mapping->webGpuGroup;
            } else {
                if (fullPass && selected != currentPassRoot)
                    return "RHI-WEBGPU-ROOT-PROGRAM-PASS-ROOT";
                const auto* prepared =
                    std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                        &selected->originalRoot()->originalPrepared());
                if (!prepared || !*prepared)
                    return "RHI-WEBGPU-ROOT-PROGRAM-GRAPHICS-STAGE";
                const auto pipeline =
                    webGpuNativePayloadFor(*(*prepared)->owners()->desc().pipeline);
                const auto group = webGpuOriginalBindlessPhysicalGroup(*prepared);
                if (!pipeline || !pipeline->issuedContract() || !pipeline->pipeline || !group
                    || !group->group)
                    return "RHI-WEBGPU-ROOT-PROGRAM-PREPARED";
                const auto& sets = pipeline->issuedContract()->sets();
                const auto mapping = std::find_if(sets.begin(), sets.end(), [&](const auto& m) {
                    return m.logicalSet == (*prepared)->owners()->desc().snapshot->setIndex();
                });
                if (mapping == sets.end())
                    return "RHI-WEBGPU-ROOT-PROGRAM-PREPARED";
                record.renderPipeline = pipeline->pipeline;
                record.group = group->group;
                record.groupIndex = mapping->webGpuGroup;
                graphicsObject = (*prepared)->owners()->desc().pipeline.get();
                graphicsRootSource = source;
            }
            break;
        }
        case K::Dispatch: {
            if (storage && computeBinds != computeDispatches + 1u)
                return "RHI-WEBGPU-ROOT-PROGRAM-ENTRY-ORDER";
            ++computeDispatches;
            const auto* dispatch = inputs->originalScalars()
                                       ->originalArguments()
                                       ->records()[op.commandIndex]
                                       .scalar.get<CommandScalarArguments::Dispatch>();
            if (!dispatch)
                return "RHI-WEBGPU-ROOT-PROGRAM-DISPATCH";
            record.dispatch = *dispatch;
            if (const auto* id = Contract::validateDispatch(
                    dispatch->x, dispatch->y, dispatch->z, frozen.dispatchLimit_))
                return id;
            break;
        }
        case K::BufferCopy: {
            if (op.referenceCount != 2u || op.elementCount != 1u)
                return "RHI-WEBGPU-ROOT-PROGRAM-COPY-SCOPE";
            const OriginalCommandBufferReadbackBinding* selectedBinding = nullptr;
            for (const auto& candidate : original.admission_->originalReadbackBindings()) {
                if (!visit())
                    return "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
                if (candidate->commandIndex() != op.commandIndex)
                    continue;
                if (selectedBinding)
                    return "RHI-WEBGPU-ROOT-PROGRAM-COPY-SCOPE";
                selectedBinding = candidate.get();
            }
            if (!selectedBinding)
                return "RHI-WEBGPU-ROOT-PROGRAM-COPY-SCOPE";
            const auto* descriptor = inputs->descriptorAt(op.commandIndex);
            const auto* copy =
                descriptor ? std::get_if<TransferCopyArguments>(descriptor) : nullptr;
            if (!copy || !copy->write(record.copy))
                return "RHI-WEBGPU-ROOT-PROGRAM-COPY";
            const auto sourceIndex = usage.references()[op.firstReference].sourceIndex;
            const auto targetIndex = usage.references()[op.firstReference + 1u].sourceIndex;
            const auto* sourceEntry = entryAt(sourceIndex);
            if (!sourceEntry)
                return lookupDiagnostic;
            // entryAt may project a retained readback destination into scratch
            // storage. Preserve the source value before another lookup.
            const auto source = *sourceEntry;
            const auto* target = entryAt(targetIndex);
            if (!target)
                return lookupDiagnostic;
            const auto& sourceLease = selectedBinding->originalCopySource();
            const auto& targetLease = selectedBinding->originalReadbackSource();
            const auto& readbackRequest = selectedBinding->originalRequest();
            if (sourceIndex != closure.inventory_->originalLeaseIndex(sourceLease)
                || targetIndex != closure.inventory_->originalLeaseIndex(targetLease)
                || source.resource != sourceLease->resource()
                || target->resource != targetLease->resource()
                || source.resource != record.copy.sourceBuffer
                || target->resource != record.copy.destinationBuffer
                || record.copy.destinationOffsetBytes != readbackRequest->offsetBytes()
                || record.copy.sizeBytes != readbackRequest->byteCapacity()
                || record.copy.sourceOffsetBytes > source.bytes
                || record.copy.sizeBytes > source.bytes - record.copy.sourceOffsetBytes
                || record.copy.destinationOffsetBytes > target->bytes
                || record.copy.sizeBytes > target->bytes - record.copy.destinationOffsetBytes
                || source.device != static_cast<WGPUDevice>(original.device_)
                || target->device != static_cast<WGPUDevice>(original.device_))
                return "RHI-WEBGPU-ROOT-PROGRAM-COPY-RANGE";
            if (selectedBinding->isOriginalSnapshotFaultReadback()
                && (record.copy.sourceOffsetBytes || record.copy.destinationOffsetBytes
                    || record.copy.sizeBytes != sizeof(std::uint32_t)))
                return "RHI-WEBGPU-ROOT-PROGRAM-FAULT-COPY-RANGE";
            record.source = source.handle;
            record.destination = target->handle;
            break;
        }
        case K::BeginComputePass:
        case K::EndComputePass:
        // WebGPU copies use encoder scope. The original transfer brackets
        // remain in the program and CommandRecordingOrder validates them.
        case K::BeginTransferPass:
        case K::EndTransferPass:
        case K::Finish:
            break;
        default:
            return "RHI-WEBGPU-ROOT-PROGRAM-OPERATION";
        }
        if (destination) {
            if (!destination->records_.append(record))
                return "RHI-ORIGINAL-ROOT-PROGRAM-COMMAND-CAPACITY";
        } else {
            const auto& retained = frozen.records()[i];
            if (!retained.original.matches(record.original) || retained.source != record.source
                || retained.destination != record.destination || retained.closure != record.closure
                || retained.renderPipeline != record.renderPipeline
                || retained.computePipeline != record.computePipeline
                || retained.group != record.group || retained.groupIndex != record.groupIndex
                || std::tie(
                       retained.indirect.offset, retained.indirect.count, retained.indirect.stride)
                    != std::tie(
                        record.indirect.offset, record.indirect.count, record.indirect.stride)
                || retained.physicalVertexSlot != record.physicalVertexSlot
                || retained.bufferBindingBytes != record.bufferBindingBytes
                || std::tie(retained.vertex.slot,
                       retained.vertex.offset,
                       retained.index.format,
                       retained.index.offset)
                    != std::tie(record.vertex.slot,
                        record.vertex.offset,
                        record.index.format,
                        record.index.offset)
                || std::tie(retained.indexed.indices,
                       retained.indexed.instances,
                       retained.indexed.firstIndex,
                       retained.indexed.vertexOffset,
                       retained.indexed.firstInstance)
                    != std::tie(record.indexed.indices,
                        record.indexed.instances,
                        record.indexed.firstIndex,
                        record.indexed.vertexOffset,
                        record.indexed.firstInstance)
                || retained.computePipeline != record.computePipeline
                || retained.group != record.group || retained.groupIndex != record.groupIndex
                || retained.push.stages != record.push.stages
                || retained.push.offsetBytes != record.push.offsetBytes
                || retained.push.sizeBytes != record.push.sizeBytes
                || retained.push.data != record.push.data
                || retained.pushSlice.dynamicOffset != record.pushSlice.dynamicOffset
                || retained.pushSlice.writeEnd != record.pushSlice.writeEnd
                || retained.pushSlice.bindingEnd != record.pushSlice.bindingEnd
                || std::tie(retained.dispatch.x, retained.dispatch.y, retained.dispatch.z)
                    != std::tie(record.dispatch.x, record.dispatch.y, record.dispatch.z)
                || retained.passIndex != record.passIndex
                || std::tie(retained.draw.vertices,
                       retained.draw.instances,
                       retained.draw.firstVertex,
                       retained.draw.firstInstance)
                    != std::tie(record.draw.vertices,
                        record.draw.instances,
                        record.draw.firstVertex,
                        record.draw.firstInstance)
                || !semantics::viewportStatesEqual(retained.viewport, record.viewport)
                || !semantics::scissorStatesEqual(retained.scissor, record.scissor)
                || std::tie(retained.copy.sourceBuffer,
                       retained.copy.destinationBuffer,
                       retained.copy.sourceOffsetBytes,
                       retained.copy.destinationOffsetBytes,
                       retained.copy.sizeBytes)
                    != std::tie(record.copy.sourceBuffer,
                        record.copy.destinationBuffer,
                        record.copy.sourceOffsetBytes,
                        record.copy.destinationOffsetBytes,
                        record.copy.sizeBytes))
                return "RHI-WEBGPU-ROOT-PROGRAM-FIELDS";
        }
    }
    if (!order.complete())
        return "RHI-COMMAND-ORDER-INCOMPLETE";
    if (pushUpload) {
        if (const auto* id =
                upload.validateComplete(WebGpuBindlessRootNativeProgram::pushNativeObjects,
                    WebGpuBindlessRootNativeProgram::pushRetainedOwners))
            return id;
        if (destination)
            destination->pushResidentBytes_ = upload.residentBytes();
        else if (frozen.pushResidentBytes_ != upload.residentBytes())
            return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-FROZEN-RANGE";
    }
    if (storage && (computeBinds != (mixed ? 3u : 2u) || computeDispatches != computeBinds))
        return "RHI-WEBGPU-ROOT-PROGRAM-STORAGE-ENTRIES";
    if (!destination && passCount != frozen.passes_.values().size())
        return "RHI-WEBGPU-ROOT-PROGRAM-PASS-COUNT";
    if (destination)
        destination->visits_ = visits;
    else if (frozen.visits_ != visits)
        return "RHI-WEBGPU-ROOT-PROGRAM-VISITS";
    return Contract::validateCounts(operations.size(), visits, request);
#endif
}

const char* WebGpuCanonicalReadbackResultStorage::validateBufferMapReceipt(
    const WebGpuNativeBufferMapReceipt& receipt, std::size_t index) const noexcept
{
    if (!bufferAdmission_ || index >= buffers_.values().size())
        return "RHI-WEBGPU-BUFFER-READBACK-STORAGE-PROFILE";
    if (const auto* id = validateBufferSnapshot())
        return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto& entry = buffers_.values()[index];
    return receipt.validateRetainedRange(entry.handle, entry.mapRange);
#else
    (void)receipt;
    return "RHI-WEBGPU-BUFFER-READBACK-STORAGE-NATIVE-UNAVAILABLE";
#endif
}

const char* WebGpuCanonicalReadbackResultStorage::readOriginalUnpublished(
    const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>& self,
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal,
    std::size_t index,
    WebGpuNativeBufferMapReceipt& receipt) noexcept
{
    if (self && self->bufferAdmission_) {
        if (index >= self->buffers_.values().size())
            return "RHI-WEBGPU-READBACK-STORAGE-MAP-INDEX";
        if (const auto* id = validateOriginalRetained(self, seal))
            return id;
        if (const auto* id = self->validateBufferMapReceipt(receipt, index))
            return id;
        const auto& snapshot = self->buffers_.values()[index].snapshot;
        if (snapshot->published_)
            return "RHI-WEBGPU-READBACK-STORAGE-SNAPSHOT-IMMUTABLE";
        return receipt.readAndUnmap(snapshot->bytes_.data(), snapshot->bytes_.size());
    }
    if (!self || index >= self->snapshots().size())
        return "RHI-WEBGPU-READBACK-STORAGE-MAP-INDEX";
    const auto& snapshot = self->snapshots()[index];
    const auto& request = snapshot->originalRequest();
    if (snapshot->published_)
        return "RHI-WEBGPU-READBACK-STORAGE-SNAPSHOT-IMMUTABLE";
    if (const auto* id = validateOriginalRetained(self, seal, &request, &receipt))
        return id;
    return receipt.readAndUnmap(snapshot->bytes_.data(), snapshot->bytes_.size());
}

const char* WebGpuCanonicalReadbackResultStorage::publishOriginalCommitted(
    const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>& self,
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal,
    const rhi::OriginalCommandReadbackMapDrainContract::Facts& drain,
    const rhi::OriginalCommandReadbackSnapshotPublicationContract::Intent& intent) noexcept
{
    using Publication = rhi::OriginalCommandReadbackSnapshotPublicationContract;
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-READBACK-STORAGE-OWNER";
    const auto& reservation = self->bufferAdmission_ ? self->bufferAdmission_->originalReservation()
        : self->typedAdmission_                      ? self->typedAdmission_->originalReservation()
                                                     : self->admission_->originalReservation();
    if (!seal || !seal->hasQuotaBoundSharedOwner(seal)
        || !core::SharedOwnerProjection::sameOwner(seal, reservation->originalSeal()))
        return "RHI-WEBGPU-READBACK-STORAGE-RETAINED-SEAL";
    if (self->bufferAdmission_
            ? !rhi::OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(
                  seal->originalBudget().contractVersion,
                  self->bufferAdmission_->originalBudget().contractVersion,
                  self->originalRoot()->originalContentPlanContractVersion())
            : self->typedAdmission_ ? !rhi::OriginalTypedNativeColdContract::readbackJoinVersion(
                                          self->typedAdmission_->originalRequest().contractVersion)
                                    : self->admission_->originalBudget().contractVersion != 5u)
        return "RHI-WEBGPU-READBACK-STORAGE-PUBLICATION-PROFILE";
    if (const auto* id = self->validateSnapshots())
        return id;
    Publication::BindingFacts bindings;
    bindings.originalSealAndRootCommitted =
        self->originalRoot()->status() == rhi::OriginalCommandStateTransaction::Status::Committed;
    bindings.originalJournalEmpty = self->originalRoot()->journalEmpty();
    bindings.validatedOriginalRequests = self->readbackRequirements().resultDeclarations;
    bindings.validatedIndependentSnapshots = self->snapshotCount();
    bindings.exactOriginalBindings = true;
    bindings.independentResultQuota = true;
    if (self->bufferAdmission_)
        for (const auto& entry : self->buffers_.values())
            if (!entry.snapshot->published_)
                ++bindings.unpublishedSnapshots;
    for (const auto& snapshot : self->snapshots())
        if (!snapshot->published_)
            ++bindings.unpublishedSnapshots;
    const auto decision =
        Publication::inspect(self->readbackMapRequirements(), drain, intent, bindings);
    if (decision.action != Publication::Action::FreezeIndependentSnapshots)
        return decision.diagnostic;
    // Complete set validated before any flag changes. No native calls, foreign
    // reentry, byte copying, allocation or authority creation during freeze.
    if (self->bufferAdmission_)
        for (const auto& entry : self->buffers_.values())
            entry.snapshot->published_ = true;
    for (const auto& snapshot : self->snapshots())
        snapshot->published_ = true;
    return nullptr;
}

const char* WebGpuCanonicalReadbackJoins::capture(const Root& root,
    const Input& input,
    Native& native,
    const ActiveMap* active,
    std::uint32_t version) noexcept
{
    if (const auto* id = rhi::OriginalCommandTextureReadbackSource::validateOriginal(input.source))
        return id;
    if (version != 4u && input.generalOutput)
        return "RHI-WEBGPU-READBACK-JOIN-GENERAL-OUTPUT-PROFILE";
    if ((version == 1u && input.source->contractVersion() != 1u)
        || ((version == 2u || version == 3u) && input.source->contractVersion() != 2u)
        || (version == 4u && input.source->contractVersion() != 2u)
        || (version != 1u && version != 2u && version != 3u && version != 4u))
        return "RHI-WEBGPU-READBACK-JOIN-SOURCE-PROFILE";
    const auto& usage = *root.originalGraph()->originalInputs()->originalScalars()->originalUsage();
    if (input.commandIndex >= usage.commands().size())
        return "RHI-WEBGPU-READBACK-JOIN-COMMAND";
    const auto& command = usage.commands()[input.commandIndex];
    if (command.kind != rhi::CommandUsageKind::TextureReadback)
        return "RHI-WEBGPU-READBACK-JOIN-COMMAND";
    const auto& original =
        std::get<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(
            usage.originalSources()
                ->sources()[usage.references()[command.firstReference + 1u].sourceIndex]);
    if (!core::SharedOwnerProjection::sameOwner(original, input.source))
        return "RHI-WEBGPU-READBACK-JOIN-ORIGINAL-SOURCE";
    if (version == 4u)
        if (const auto* id = validateGeneralOutputBinding(root, input))
            return id;
    if (version == 2u || version == 3u) {
        if (root.originalContentPlanContractVersion() != 3u || command.referenceCount != 2u)
            return "RHI-WEBGPU-READBACK-PROGRAM-JOIN-CONTENT-PROFILE";
        const auto* parent = std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
            &usage.originalSources()
                ->sources()[usage.references()[command.firstReference].sourceIndex]);
        if (!parent
            || !core::SharedOwnerProjection::sameOwner(
                *parent, original->originalSource()->originalLease()->originalTextureOwner()))
            return "RHI-WEBGPU-READBACK-PROGRAM-JOIN-PARENT";
    }
    if (const auto* id = rhi::OriginalCommandReadbackJoinContract::destinationIntent(
            original->originalDestinationLease()->layout()))
        return id;
    if (const auto* id =
            observeOriginalWebGpuResource(*original->originalSource()->originalObject(), native[0]))
        return id;
    if (const auto* id = observeOriginalWebGpuResource(
            *original->originalDestinationLease()->originalObject(), native[1]))
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-READBACK-JOIN-NATIVE-UNAVAILABLE";
#elif !defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
    return "RHI-WEBGPU-READBACK-JOIN-API-UNAVAILABLE";
#else
    const auto& texture = native[0].texture;
    const auto& buffer = native[1].buffer;
    if (!texture || !buffer || !texture->ownerDevice || texture->ownerDevice != buffer->ownerDevice)
        return "RHI-WEBGPU-READBACK-JOIN-NATIVE-DEVICE";
    const bool attachmentOutput =
        version == 4u && input.generalOutput->originalBudget().contractVersion == 3u;
    // Output3 has already authenticated the exact completed attachment above;
    // consume that declared role instead of asking for ordinary texture access.
    if (version == 1u || (version == 4u && !attachmentOutput)) {
        if (const auto* id = texture->contentAccessPolicy().ordinaryAccessDiagnostic())
            return id;
    } else if (!texture->contentAccessPolicy().originalAttachmentExclusive())
        return "RHI-WEBGPU-READBACK-PROGRAM-JOIN-NATIVE-CONTENT-PROFILE";
    if (!texture->texture || !buffer->buffer
        || wgpuBufferGetSize(buffer->buffer) != native[1].bufferDesc.sizeBytes
        || wgpuBufferGetUsage(buffer->buffer) != (WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead)
        || !(wgpuTextureGetUsage(texture->texture) & WGPUTextureUsage_CopySrc))
        return "RHI-WEBGPU-READBACK-JOIN-NATIVE-LAYOUT";
    if (active
        && core::SharedOwnerProjection::sameOwner(
            active->request, original->originalSource()->originalRequest())) {
        if (native[1].bufferDesc.sizeBytes > SIZE_MAX)
            return "RHI-WEBGPU-READBACK-JOIN-MAP-SIZE-RANGE";
        if (const auto* id = active->receipt.validateRetainedBinding(
                buffer->buffer, static_cast<std::size_t>(native[1].bufferDesc.sizeBytes)))
            return id;
    } else if (!WebGpuNativeBufferMapState::matches(buffer->buffer, WGPUBufferMapState_Unmapped))
        return "RHI-WEBGPU-READBACK-JOIN-NATIVE-MAP-STATE";
    const auto& layout = original->originalSource()->originalRequest()->originalLayout();
    for (const auto& mip : layout->mipCopies())
        if (const auto* id = WebGpuNativeTransferCopyLowering::textureDiagnostic(
                *texture, mip.range, mip.extent))
            return id;
    return nullptr;
#endif
}

const char* WebGpuCanonicalReadbackJoins::inspect(const std::shared_ptr<Root>& root,
    const Input* entries,
    std::size_t count,
    const Budget& budget,
    std::uint32_t& owners,
    std::uint64_t& sourceBytes,
    std::uint64_t& stagingBytes,
    std::size_t& copies,
    const rhi::OriginalCommandRecordingSeal* seal,
    const ActiveMap* active,
    const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program) noexcept
{
    owners = 0u;
    sourceBytes = stagingBytes = 0u;
    copies = 0u;
    if (const auto* id = (budget.contractVersion == 2u || budget.contractVersion == 3u)
            ? rhi::OriginalCommandReadbackJoinContract::validateProgramIntent(budget)
            : budget.contractVersion == 4u
            ? rhi::OriginalCommandReadbackJoinContract::validateGeneralOutputIntent(budget)
            : rhi::OriginalCommandReadbackJoinContract::validateIntent(budget))
        return id;
    if (const auto* id = Root::validateOriginal(root))
        return id;
    if (seal) {
        // Old recording/map consumers have no mixed typed payload authority.
        if (budget.contractVersion == 2u || budget.contractVersion == 3u
            || budget.contractVersion == 4u)
            return "RHI-WEBGPU-READBACK-PROGRAM-JOIN-EXECUTION-PROFILE";
        if (!root->hasOriginalRecordingSeal() || seal->originalRoot().get() != root.get()
            || root->status() != Root::Status::Pending || root->journalEmpty())
            return "RHI-WEBGPU-READBACK-JOIN-SEALED-PENDING-ROOT";
    } else if (root->status() != Root::Status::Prepared || !root->hasOriginalPrototypePlan()
        || !root->journalEmpty())
        return "RHI-WEBGPU-READBACK-JOIN-ROOT-STATUS";
    if (!entries || !count || count > budget.maxReadbacks || count > SIZE_MAX / 2u)
        return "RHI-WEBGPU-READBACK-JOIN-COUNT";
    if (budget.maxCpuMetadataBytes != root->originalMetadataQuota()->limitBytes())
        return "RHI-WEBGPU-READBACK-JOIN-QUOTA";
    const auto& inputs = root->originalGraph()->originalInputs();
    const auto requirements = (budget.contractVersion == 2u || budget.contractVersion == 3u)
        ? rhi::OriginalCommandReadbackJoinContract::inspectProgram(root, program, budget)
        : budget.contractVersion == 4u
        ? rhi::OriginalCommandReadbackJoinContract::inspectGeneralOutput(root, budget)
        : rhi::OriginalCommandReadbackResultContract::inspect(inputs, budget.stream, budget.result);
    if (requirements.diagnostic)
        return requirements.diagnostic;
    if (requirements.required.nativeCopyCommands > SIZE_MAX)
        return "RHI-WEBGPU-READBACK-JOIN-COPY-CAPACITY";
    copies = static_cast<std::size_t>(requirements.required.nativeCopyCommands);
    const auto& usage = *inputs->originalScalars()->originalUsage();
    const auto& authored = usage.originalSources()->originalBudget();
    core::MemoryBudgetLedger ownerBudget({budget.maxRetainedOwners, true});
    if (copies > budget.maxRetainedOwners)
        return "RHI-WEBGPU-READBACK-JOIN-OWNER-CAPACITY";
    const auto initial =
        std::uint64_t(root->retainedOwnerCount()) + 1u + copies + (program ? 1u : 0u);
    if (!ownerBudget.canAllocate(initial))
        return "RHI-WEBGPU-READBACK-JOIN-OWNER-CAPACITY";
    ownerBudget.recordAllocation(initial);
    std::size_t ordinal = 0u;
    for (std::size_t command = 0u; command < usage.commands().size(); ++command) {
        if (usage.commands()[command].kind != rhi::CommandUsageKind::TextureReadback)
            continue;
        if (ordinal >= count || entries[ordinal].commandIndex != command)
            return "RHI-WEBGPU-READBACK-JOIN-ORDER-COVERAGE";
        Native native;
        if (const auto* id =
                capture(*root, entries[ordinal], native, active, budget.contractVersion))
            return id;
        // The entry retains one original declaration in addition to the exact
        // native observations and each copy decision's layout reference.
        if (!ownerBudget.canAllocate(1u))
            return "RHI-WEBGPU-READBACK-JOIN-OWNER-CAPACITY";
        ownerBudget.recordAllocation(1u);
        if (budget.contractVersion == 4u) {
            // Conservatively charge the complete upstream association graph
            // and the frozen observation's registration ticket independently.
            const auto extra =
                std::uint64_t(entries[ordinal].generalOutput->retainedOwnerCount()) + 1u;
            if (!ownerBudget.canAllocate(extra))
                return "RHI-WEBGPU-READBACK-JOIN-OWNER-CAPACITY";
            ownerBudget.recordAllocation(extra);
        }
        for (const auto& endpoint : native) {
            if (!ownerBudget.canAllocate(endpoint.ownerCount()))
                return "RHI-WEBGPU-READBACK-JOIN-OWNER-CAPACITY";
            ownerBudget.recordAllocation(endpoint.ownerCount());
        }
        ++ordinal;
    }
    if (ordinal != count)
        return "RHI-WEBGPU-READBACK-JOIN-ORDER-COVERAGE";
    const char* failure = nullptr;
    const auto mapped = [&](std::size_t i) noexcept {
        Native value;
        if (!failure)
            failure = capture(*root, entries[i], value, active, budget.contractVersion);
        return value;
    };
    for (std::size_t endpoint = 0u; endpoint < 2u; ++endpoint) {
        const auto identities = core::SharedIdentityProjection::inspect(count,
            count,
            endpoint ? budget.maxNativeDestinations : budget.maxNativeSources,
            [&](std::size_t i) noexcept -> core::SharedIdentityProjection::Record {
                const auto native = mapped(i);
                const auto& resource = native[endpoint];
                return {resource.id.value,
                    endpoint ? 1u : 2u,
                    resource.allocationRecord().backing,
                    endpoint ? static_cast<const void*>(resource.buffer.get())
                             : resource.texture.get()};
            });
        if (failure)
            return failure;
        if (identities.status != core::SharedIdentityProjection::Status::Complete)
            return "RHI-WEBGPU-READBACK-JOIN-NATIVE-OBJECT-CAPACITY";
        const auto bytes = core::SharedAllocationProjection::inspect(count,
            count,
            endpoint ? budget.maxStagingResourceBytes : budget.maxSourceResourceBytes,
            [&](std::size_t i) noexcept {
                return mapped(i)[endpoint].allocationRecord();
            });
        if (failure)
            return failure;
        if (bytes.status != core::SharedAllocationProjection::Status::Complete)
            return "RHI-WEBGPU-READBACK-JOIN-NATIVE-BYTE-CAPACITY";
        if (endpoint)
            stagingBytes = bytes.bytes;
        else
            sourceBytes = bytes.bytes;
    }
    const auto payloads = core::SharedIdentityProjection::inspect(count * 2u,
        count * 2u,
        authored.maxNativePayloadObjects,
        [&](std::size_t i) noexcept -> core::SharedIdentityProjection::Record {
            const auto native = mapped(i / 2u);
            const auto& resource = native[i % 2u];
            return {resource.id.value,
                i % 2u ? 1u : 2u,
                resource.allocationRecord().backing,
                i % 2u ? static_cast<const void*>(resource.buffer.get()) : resource.texture.get()};
        });
    if (failure)
        return failure;
    if (payloads.status != core::SharedIdentityProjection::Status::Complete)
        return "RHI-WEBGPU-READBACK-JOIN-ORIGINAL-PAYLOAD-CAPACITY";
    if (stagingBytes > authored.maxNativeStagingBytes)
        return "RHI-WEBGPU-READBACK-JOIN-ORIGINAL-STAGING-CAPACITY";
    owners = static_cast<std::uint32_t>(ownerBudget.stats().allocatedBytes);
    return nullptr;
}

std::shared_ptr<const WebGpuCanonicalReadbackJoins> WebGpuCanonicalReadbackJoins::prepareOriginal(
    std::shared_ptr<Root> root,
    const rhi::GpuDevice& device,
    const Input* entries,
    std::size_t count,
    const Budget& budget,
    const char*& diagnostic)
{
    diagnostic = rhi::OriginalCommandReadbackJoinContract::validateIntent(budget);
    if (diagnostic)
        return {};
    return prepareOriginalImpl(std::move(root), {}, device, entries, count, budget, diagnostic);
}

std::shared_ptr<const WebGpuCanonicalReadbackJoins>
WebGpuCanonicalReadbackJoins::prepareOriginalProgram(std::shared_ptr<Root> root,
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> program,
    const rhi::GpuDevice& device,
    const Input* entries,
    std::size_t count,
    const Budget& budget,
    const char*& diagnostic)
{
    diagnostic = rhi::OriginalCommandReadbackJoinContract::validateProgramIntent(budget);
    if (diagnostic)
        return {};
    return prepareOriginalImpl(
        std::move(root), std::move(program), device, entries, count, budget, diagnostic);
}

std::shared_ptr<const WebGpuCanonicalReadbackJoins>
WebGpuCanonicalReadbackJoins::prepareOriginalGeneralOutput(std::shared_ptr<Root> root,
    const rhi::GpuDevice& device,
    const Input* entries,
    std::size_t count,
    const Budget& budget,
    const char*& diagnostic)
{
    diagnostic = rhi::OriginalCommandReadbackJoinContract::validateGeneralOutputIntent(budget);
    if (diagnostic)
        return {};
    return prepareOriginalImpl(std::move(root), {}, device, entries, count, budget, diagnostic);
}

std::shared_ptr<const WebGpuCanonicalReadbackJoins>
WebGpuCanonicalReadbackJoins::prepareOriginalImpl(std::shared_ptr<Root> root,
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> program,
    const rhi::GpuDevice& device,
    const Input* entries,
    std::size_t count,
    const Budget& budget,
    const char*& diagnostic)
{
    diagnostic = (budget.contractVersion == 2u || budget.contractVersion == 3u)
        ? rhi::OriginalCommandReadbackJoinContract::validateProgramIntent(budget)
        : budget.contractVersion == 4u
        ? rhi::OriginalCommandReadbackJoinContract::validateGeneralOutputIntent(budget)
        : rhi::OriginalCommandReadbackJoinContract::validateIntent(budget);
    if (diagnostic)
        return {};
    const auto decision = arp::rhi::semantics::OriginalCommandReadbackJoinLowering::lowering(
        device.backend(), budget.contractVersion);
    if (!decision.privateJoinAvailable) {
        diagnostic = decision.diagnostic;
        return {};
    }
    if ((diagnostic = Root::validateOriginal(root)))
        return {};
    if ((diagnostic = root->compareOriginalGraph(root->originalGraph(), device)))
        return {};
    if ((diagnostic = rhi::OriginalCommandResourceSources::validateRegisteredLifetime(
             root->originalGraph()
                 ->originalInputs()
                 ->originalScalars()
                 ->originalUsage()
                 ->originalSources(),
             device)))
        return {};
    std::uint32_t owners = 0u;
    std::uint64_t sourceBytes = 0u, stagingBytes = 0u;
    std::size_t copies = 0u;
    if ((diagnostic = inspect(root,
             entries,
             count,
             budget,
             owners,
             sourceBytes,
             stagingBytes,
             copies,
             nullptr,
             nullptr,
             program)))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(root->originalMetadataQuota());
        auto result = core::makeUnique<WebGpuCanonicalReadbackJoins>(ConstructionKey{},
            root,
            budget,
            count,
            copies,
            owners,
            sourceBytes,
            stagingBytes,
            program);
        for (std::size_t i = 0u; i < count; ++i) {
            Native native;
            if ((diagnostic = capture(*root, entries[i], native, nullptr, budget.contractVersion)))
                return {};
            result->entries_.append(entries[i]);
            result->native_.append(std::move(native));
            if (budget.contractVersion == 4u) {
                const auto observed = entries[i]
                                          .generalOutput->originalOutput()
                                          ->originalObject()
                                          ->observeOriginalCommittedState(
                                              entries[i].generalOutput->originalBudget().range);
                if (!observed || !result->generalOutputStates_.append(*observed)) {
                    diagnostic = "RHI-WEBGPU-GENERAL-OUTPUT-JOIN-STATE-CAPTURE";
                    return {};
                }
            }
            const auto& layout =
                entries[i].source->originalSource()->originalRequest()->originalLayout();
            for (std::size_t mip = 0u; mip < layout->mipCopies().size(); ++mip) {
                Lowering::Decision lowered;
                if ((diagnostic = Lowering::lowerOriginal(rhi::Backend::WebGPU,
                         1u,
                         true,
                         layout,
                         mip,
                         layout->mipCopies()[mip],
                         lowered)))
                    return {};
                result->copies_.append(Copy{entries[i].commandIndex, std::move(lowered)});
            }
        }
        auto shared = shareOriginal(std::move(result));
        if ((diagnostic = shared->validate()))
            return {};
        return shared;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-READBACK-JOIN-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-READBACK-JOIN-ALLOCATION";
        return {};
    }
}

const char* WebGpuCanonicalReadbackJoins::validate() const noexcept
{
    if (budget_.contractVersion == 4u)
        return validatePreparedGeneralOutput();
    return validateWithSeal(nullptr);
}

const char* WebGpuCanonicalReadbackJoins::validateGeneralOutputBinding(const Root& root,
    const Input& input,
    const rhi::GpuTexture::CommittedStateObservation* frozen) noexcept
{
    using Snapshot = WebGpuOriginalGeneralOutputSnapshot;
    if (const auto* id = Snapshot::validateCompletedOriginal(input.generalOutput))
        return id;
    const auto outputVersion = input.generalOutput->originalBudget().contractVersion;
    if (const auto* id = rhi::OriginalCommandReadbackJoinContract::validateGeneralOutputSource(
            input.source, outputVersion))
        return id;
    const auto& output = input.generalOutput->originalOutput();
    if (input.generalOutput->originalRoot().get() == &root
        || !core::SharedOwnerProjection::sameOwner(
            output, input.source->originalSource()->originalLease()->originalTextureOwner())
        || input.source->originalInput()->scalarFields().range()
            != input.generalOutput->originalBudget().range)
        return "RHI-WEBGPU-GENERAL-OUTPUT-JOIN-ASSOCIATION";
    const auto observed = output->originalObject()->observeOriginalCommittedState(
        input.generalOutput->originalBudget().range);
    if (!observed)
        return "RHI-WEBGPU-GENERAL-OUTPUT-JOIN-STATE-CAPTURE";
    if (frozen) {
        if (const auto* id = output->originalObject()->validateOriginalCommittedState(*frozen))
            return id;
        if (frozen->revision() != observed->revision() || frozen->range() != observed->range()
            || frozen->state() != observed->state()
            || frozen->resourceGeneration() != observed->resourceGeneration())
            return "RHI-WEBGPU-GENERAL-OUTPUT-JOIN-STATE-CHANGED";
    }
    const auto& graph = root.originalGraph();
    const auto& inputs = graph->originalInputs();
    const auto& usage = *inputs->originalScalars()->originalUsage();
    if (input.commandIndex >= usage.commands().size()
        || usage.commands()[input.commandIndex].kind != rhi::CommandUsageKind::TextureReadback)
        return "RHI-WEBGPU-READBACK-JOIN-COMMAND";
    const auto& command = usage.commands()[input.commandIndex];
    const auto parentIndex = usage.references()[command.firstReference].sourceIndex;
    const auto* parent = std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
        &usage.originalSources()->sources()[parentIndex]);
    if (!parent || !core::SharedOwnerProjection::sameOwner(*parent, output))
        return "RHI-WEBGPU-GENERAL-OUTPUT-JOIN-PARENT";
    const rhi::OriginalCommandResourceStates::TextureEntry* entry = nullptr;
    for (const auto& candidate : graph->textureEntries())
        if (candidate.point.commandIndex == input.commandIndex
            && candidate.point.sourceIndex == parentIndex
            && candidate.point.range == observed->range()) {
            entry = &candidate;
            break;
        }
    const auto expected =
        outputVersion == 3u ? rhi::ResourceState::ColorAttachment : rhi::ResourceState::ShaderWrite;
    if (!entry || entry->observation.revision() != observed->revision()
        || entry->observation.state() != expected
        || entry->observation.resourceGeneration() != observed->resourceGeneration())
        return "RHI-WEBGPU-GENERAL-OUTPUT-JOIN-GRAPH-STATE";
    return rhi::OriginalCommandReadbackJoinContract::validateGeneralOutputCommand(
        *inputs, input.commandIndex, *input.source, outputVersion);
}

const char* WebGpuCanonicalReadbackJoins::validatePreparedGeneralOutput() const noexcept
{
    if (const auto* id =
            rhi::OriginalCommandReadbackJoinContract::inspectGeneralOutput(root_, budget_)
                .diagnostic)
        return id;
    if (program_ || !originalMetadataQuota()
        || !core::SharedOwnerProjection::sameOwner(
            originalMetadataQuota(), root_->originalMetadataQuota())
        || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes
        || entries_.values().size() != generalOutputStates_.values().size()
        || entries_.values().size() != native_.values().size()
        || owners_ > budget_.maxRetainedOwners || sourceBytes_ > budget_.maxSourceResourceBytes
        || stagingBytes_ > budget_.maxStagingResourceBytes)
        return "RHI-WEBGPU-GENERAL-OUTPUT-JOIN-RETAINED-COVERAGE";
    for (std::size_t i = 0u; i < entries_.values().size(); ++i) {
        if (const auto* id = validateGeneralOutputBinding(
                *root_, entries_.values()[i], &generalOutputStates_.values()[i]))
            return id;
        const auto& snapshot = entries_.values()[i].generalOutput;
        if (const auto* id =
                rhi::OriginalGeneralOutputPairingContract::validatePair(budget_.generalOutput,
                    {snapshot->originalBudget().contractVersion,
                        snapshot->originalBudget().prototypeOptIn}))
            return id;
    }
    return validateFrozenEntries(nullptr, false);
}

const char* WebGpuCanonicalReadbackJoins::validateRetained(
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal) const noexcept
{
    if (budget_.contractVersion == 4u)
        return validateRetainedGeneralOutput(seal);
    if (budget_.contractVersion != 1u)
        return "RHI-WEBGPU-READBACK-PROGRAM-JOIN-EXECUTION-PROFILE";
    if (!seal || !seal->hasQuotaBoundSharedOwner(seal)
        || !core::SharedOwnerProjection::sameOwner(seal->originalRoot(), root_)
        || !core::SharedOwnerProjection::sameOwner(
            seal->originalMetadataQuota(), root_->originalMetadataQuota()))
        return "RHI-WEBGPU-READBACK-JOIN-SEAL-ROOT";
    return validateWithSeal(root_->status() == Root::Status::Prepared ? nullptr : seal.get());
}

const char* WebGpuCanonicalReadbackJoins::validateRetainedMapping(
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal,
    const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>& request,
    const WebGpuNativeBufferMapReceipt& receipt) const noexcept
{
    if (budget_.contractVersion != 1u && budget_.contractVersion != 2u
        && budget_.contractVersion != 3u && budget_.contractVersion != 4u)
        return "RHI-WEBGPU-READBACK-PROGRAM-JOIN-EXECUTION-PROFILE";
    if (!seal || !seal->hasQuotaBoundSharedOwner(seal)
        || !core::SharedOwnerProjection::sameOwner(seal->originalRoot(), root_)
        || !core::SharedOwnerProjection::sameOwner(
            seal->originalMetadataQuota(), root_->originalMetadataQuota()))
        return "RHI-WEBGPU-READBACK-JOIN-SEAL-ROOT";
    if (!request || !request->hasQuotaBoundSharedOwner(request))
        return "RHI-WEBGPU-READBACK-JOIN-MAP-REQUEST-OWNER";
    if (root_->status() != Root::Status::Pending || root_->journalEmpty())
        return "RHI-WEBGPU-READBACK-JOIN-MAP-PENDING-ROOT";
    if (!entries_.findIf([&](const auto& entry) {
            return core::SharedOwnerProjection::sameOwner(
                entry.source->originalSource()->originalRequest(), request);
        }))
        return "RHI-WEBGPU-READBACK-JOIN-MAP-REQUEST";
    const ActiveMap active{request, receipt};
    if (budget_.contractVersion == 4u)
        return validateRetainedGeneralOutput(seal, &active);
    if (budget_.contractVersion == 2u || budget_.contractVersion == 3u) {
        if (const auto* id = validateImmutableProgramBindings())
            return id;
        return validateFrozenEntries(&active, false);
    }
    return validateWithSeal(seal.get(), &active);
}

const char* WebGpuCanonicalReadbackJoins::validateRetainedGeneralOutput(
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal,
    const ActiveMap* active) const noexcept
{
    if (!seal || !seal->hasQuotaBoundSharedOwner(seal)
        || !core::SharedOwnerProjection::sameOwner(seal->originalRoot(), root_)
        || !core::SharedOwnerProjection::sameOwner(
            seal->originalMetadataQuota(), root_->originalMetadataQuota()))
        return "RHI-WEBGPU-READBACK-JOIN-SEAL-ROOT";
    if (!active && root_->status() == Root::Status::Prepared)
        return validatePreparedGeneralOutput();
    if (const auto* id =
            rhi::OriginalCommandReadbackJoinContract::validateGeneralOutputIntent(budget_))
        return id;
    if (budget_.contractVersion != 4u || program_ || !originalMetadataQuota()
        || !core::SharedOwnerProjection::sameOwner(
            originalMetadataQuota(), root_->originalMetadataQuota())
        || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes
        || entries_.values().empty() || entries_.values().size() > budget_.maxReadbacks
        || entries_.values().size() != generalOutputStates_.values().size()
        || entries_.values().size() != native_.values().size()
        || owners_ > budget_.maxRetainedOwners || sourceBytes_ > budget_.maxSourceResourceBytes
        || stagingBytes_ > budget_.maxStagingResourceBytes)
        return "RHI-WEBGPU-GENERAL-OUTPUT-JOIN-RETAINED-COVERAGE";
    for (std::size_t i = 0u; i < entries_.values().size(); ++i) {
        const auto& input = entries_.values()[i];
        if (const auto* id = rhi::OriginalCommandReadbackJoinContract::validateGeneralOutputSource(
                input.source, budget_.generalOutput.contractVersion))
            return id;
        if (!input.generalOutput
            || !core::SharedOwnerProjection::sameOwner(input.generalOutput->originalOutput(),
                input.source->originalSource()->originalLease()->originalTextureOwner())
            || input.source->originalInput()->scalarFields().range()
                != input.generalOutput->originalBudget().range)
            return "RHI-WEBGPU-GENERAL-OUTPUT-JOIN-ASSOCIATION";
        if (const auto* id =
                rhi::OriginalGeneralOutputPairingContract::validatePair(budget_.generalOutput,
                    {input.generalOutput->originalBudget().contractVersion,
                        input.generalOutput->originalBudget().prototypeOptIn}))
            return id;
        if (const auto* id = rhi::OriginalCommandReadbackJoinContract::validateGeneralOutputCommand(
                *root_->originalGraph()->originalInputs(),
                input.commandIndex,
                *input.source,
                budget_.generalOutput.contractVersion))
            return id;
        if (const auto* id = WebGpuCanonicalCallbackCustody::validateGeneralOutputPending(
                input.generalOutput, seal, generalOutputStates_.values()[i]))
            return id;
    }
    return validateFrozenEntries(active, false);
}

bool WebGpuCanonicalReadbackJoins::overlapsCpuOwner(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    if (!quota)
        return false;
    if (program_
        && (core::cpuAllocationOverlapsQuotaOwner(program_.get(), quota)
            || core::SharedOwnerProjection::sameControlBlock(
                program_->originalMetadataQuota(), quota)))
        return true;
    for (const auto& entry : entries_.values()) {
        const auto& source = entry.source;
        if (entry.generalOutput && entry.generalOutput->overlapsCpuOwner(quota))
            return true;
        const auto& request = source->originalSource()->originalRequest();
        for (const auto* allocation : std::array<const void*, 8>{source.get(),
                 source->originalSource().get(),
                 request.get(),
                 request->originalLayout().get(),
                 source->originalInput().get(),
                 source->originalInput()->labelOwner().get(),
                 source->originalSource()->originalObject(),
                 source->originalDestinationLease()->originalObject()})
            if (core::cpuAllocationOverlapsQuotaOwner(allocation, quota))
                return true;
    }
    for (const auto& native : native_.values())
        for (const auto& endpoint : native)
            for (const auto& owner : endpoint.owners())
                if (owner && core::cpuAllocationOverlapsQuotaOwner(owner.get(), quota))
                    return true;
    return false;
}

const char* WebGpuCanonicalReadbackJoins::validateWithSeal(
    const rhi::OriginalCommandRecordingSeal* seal, const ActiveMap* active) const noexcept
{
    if (!originalMetadataQuota()
        || !core::SharedOwnerProjection::sameOwner(
            originalMetadataQuota(), root_->originalMetadataQuota()))
        return "RHI-WEBGPU-READBACK-JOIN-QUOTA";
    std::uint32_t owners = 0u;
    std::uint64_t sourceBytes = 0u, stagingBytes = 0u;
    std::size_t copies = 0u;
    if (const auto* id = inspect(root_,
            entries_.values().data(),
            entries_.values().size(),
            budget_,
            owners,
            sourceBytes,
            stagingBytes,
            copies,
            seal,
            active,
            program_))
        return id;
    if (owners != owners_ || sourceBytes != sourceBytes_ || stagingBytes != stagingBytes_
        || copies != copies_.values().size() || native_.values().size() != entries_.values().size())
        return "RHI-WEBGPU-READBACK-JOIN-RETAINED-COVERAGE";
    return validateFrozenEntries(active);
}

const char* WebGpuCanonicalReadbackJoins::validateImmutableProgramBindings() const noexcept
{
    if ((budget_.contractVersion != 2u && budget_.contractVersion != 3u) || !program_
        || !program_->hasQuotaBoundSharedOwner(program_))
        return "RHI-WEBGPU-READBACK-PROGRAM-JOIN-EXECUTION-PROFILE";
    if (const auto* id = Root::validateOriginal(root_))
        return id;
    if (!root_->hasOriginalRecordingSeal() || root_->status() != Root::Status::Pending
        || root_->journalEmpty() || root_->originalContentPlanContractVersion() != 3u)
        return "RHI-WEBGPU-READBACK-JOIN-SEALED-PENDING-ROOT";
    if (!core::SharedOwnerProjection::sameOwner(
            originalMetadataQuota(), root_->originalMetadataQuota())
        || !core::SharedOwnerProjection::sameOwner(
            program_->originalInputs(), root_->originalGraph()->originalInputs())
        || program_->streamContractVersion() != budget_.stream.contractVersion
        || !rhi::OriginalCommandReadbackJoinContract::programProfileMatches(budget_,
            root_->originalGraph()->stateContractVersion(),
            program_->originalInputs()->originalBudget().contractVersion,
            root_->originalContentPlanContractVersion()))
        return "RHI-WEBGPU-READBACK-PROGRAM-JOIN-RETAINED-IDENTITY";
    // The original cold object is immutable. Its count/budget projections were
    // materialized before sealing; do not re-enter Prepared graph validators or
    // query native sizes/usages while consuming the same Pending journal.
    if (entries_.values().empty() || entries_.values().size() > budget_.maxReadbacks
        || entries_.values().size() != native_.values().size()
        || budget_.maxCpuMetadataBytes != root_->originalMetadataQuota()->limitBytes())
        return "RHI-WEBGPU-READBACK-JOIN-RETAINED-COVERAGE";
    return validateFrozenEntries(nullptr, false);
}

const char* WebGpuCanonicalReadbackJoins::validateFrozenEntries(
    const ActiveMap* active, bool queryNative) const noexcept
{
    std::size_t ordinal = 0u;
    for (std::size_t i = 0u; i < entries_.values().size(); ++i) {
        const auto& entry = entries_.values()[i];
        Native native;
        if (queryNative) {
            if (const auto* id = capture(*root_, entry, native, active, budget_.contractVersion))
                return id;
        } else {
            if (const auto* id =
                    rhi::OriginalCommandTextureReadbackSource::validateOriginal(entry.source))
                return id;
            if (const auto* id = observeOriginalWebGpuResource(
                    *entry.source->originalSource()->originalObject(), native[0]))
                return id;
            if (const auto* id = observeOriginalWebGpuResource(
                    *entry.source->originalDestinationLease()->originalObject(), native[1]))
                return id;
        }
        for (std::size_t endpoint = 0u; endpoint < 2u; ++endpoint)
            if (!native[endpoint].sameOwnersAndLayout(native_.values()[i][endpoint]))
                return "RHI-WEBGPU-READBACK-JOIN-NATIVE-CHANGED";
        if (!queryNative && active
            && core::SharedOwnerProjection::sameOwner(
                active->request, entry.source->originalSource()->originalRequest())) {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (!native[1].buffer || native[1].bufferDesc.sizeBytes > SIZE_MAX)
                return "RHI-WEBGPU-READBACK-JOIN-MAP-SIZE-RANGE";
            if (const auto* id = active->receipt.validateRetainedBinding(native[1].buffer->buffer,
                    static_cast<std::size_t>(native[1].bufferDesc.sizeBytes)))
                return id;
#else
            return "RHI-WEBGPU-READBACK-JOIN-NATIVE-UNAVAILABLE";
#endif
        }
        const auto& layout = entry.source->originalSource()->originalRequest()->originalLayout();
        for (std::size_t mip = 0u; mip < layout->mipCopies().size(); ++mip) {
            if (ordinal >= copies_.values().size())
                return "RHI-WEBGPU-READBACK-JOIN-RETAINED-COVERAGE";
            const auto& copy = copies_.values()[ordinal++];
            if (copy.commandIndex != entry.commandIndex)
                return "RHI-WEBGPU-READBACK-JOIN-COPY-ORDER";
            if (const auto* id = Lowering::validateOriginal(rhi::Backend::WebGPU,
                    1u,
                    true,
                    layout,
                    mip,
                    layout->mipCopies()[mip],
                    copy.decision))
                return id;
        }
    }
    return ordinal == copies_.values().size() ? nullptr
                                              : "RHI-WEBGPU-READBACK-JOIN-RETAINED-COVERAGE";
}

// Shared read-only provenance for a retained completed texture initializer.
// Completion may have retired its callback graph; staging/input ownership remains.
static bool overlapsTextureContentSourceOwner(
    const std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource>& proof,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
{
    if (!proof || !quota)
        return false;
    const auto& staging = proof->originalStaging();
    const auto& binding = staging->originalPackingSource();
    const auto contentQuota =
        binding->originalDestinationLease()->originalObject()->originalContentCpuQuota();
    if (staging->initializedBuffer()->overlapsCpuOwner(quota)
        || staging->overlapsCpuQuota(quota.get()) || quota.get() == contentQuota.get()
        || core::SharedOwnerProjection::sameControlBlock(quota, contentQuota))
        return true;
    for (const auto* allocation : std::array<const void*, 7>{proof.get(),
             staging.get(),
             binding.get(),
             binding->packing().get(),
             binding->packing()->packedBytes().data(),
             binding->originalInput().get(),
             binding->originalInput()->pixelBytes().data()})
        if (core::cpuAllocationOverlapsQuotaOwner(allocation, quota))
            return true;
    return false;
}

bool WebGpuCanonicalUploadJoins::overlapsCpuOwner(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    if (!quota)
        return false;
    for (const auto& input : entries_.values()) {
        const auto overlap = std::visit(
            [&](const auto& proof) noexcept {
                using T = typename std::decay_t<decltype(proof)>::element_type;
                if constexpr (std::is_same_v<T, const WebGpuCanonicalBufferUploadSource>)
                    return proof->overlapsCpuOwner(quota);
                else {
                    return overlapsTextureContentSourceOwner(proof, quota);
                }
            },
            input.source);
        if (overlap)
            return true;
    }
    return false;
}

const char* WebGpuCanonicalUploadJoins::capture(const Root& root,
    const Input& input,
    Native& native,
    const rhi::OriginalCommandRecordingSeal* seal) noexcept
{
    const auto& usage = *root.originalGraph()->originalInputs()->originalScalars()->originalUsage();
    if (input.commandIndex >= usage.commands().size())
        return "RHI-WEBGPU-UPLOAD-JOIN-COMMAND";
    const auto& command = usage.commands()[input.commandIndex];
    const auto& inventory = usage.originalSources()->sources();
    using K = rhi::CommandUsageKind;
    std::shared_ptr<const rhi::OriginalCommandBufferLease> sourceLease;
    const rhi::GpuBuffer* destinationBuffer = nullptr;
    const rhi::GpuTexture* destinationTexture = nullptr;
    if (command.kind == K::BufferUpload) {
        const auto* proof =
            std::get_if<std::shared_ptr<const WebGpuCanonicalBufferUploadSource>>(&input.source);
        if (!proof || !*proof || !(*proof)->hasQuotaBoundSharedOwner(*proof))
            return "RHI-WEBGPU-UPLOAD-JOIN-BUFFER-OWNER";
        const auto index = usage.references()[command.firstReference + 2u].sourceIndex;
        const auto& original =
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>>(
                inventory[index]);
        if (seal) {
            if (const auto* id = (*proof)->compareInitializedUploadFacts(original))
                return id;
            if (const auto* id = seal->validateInitializedBufferPending(
                    *(*proof)->state_->lease->originalObject(), *(*proof)->state_->committed))
                return id;
        } else if (const auto* id = (*proof)->compareOriginalUpload(original))
            return id;
        if ((*proof)->overlapsCpuQuota(root.originalMetadataQuota().get()))
            return "RHI-WEBGPU-UPLOAD-JOIN-QUOTA-DOMAIN";
        sourceLease = (*proof)->originalLease();
        destinationBuffer = original->originalDestinationLease()->originalObject();
        if (destinationBuffer->gpuTransferWritesSealed())
            return "RHI-BUFFER-GPU-WRITES-SEALED";
    } else if (command.kind == K::TextureUpload) {
        const auto* proof =
            std::get_if<std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource>>(
                &input.source);
        if (!proof)
            return "RHI-WEBGPU-UPLOAD-JOIN-TEXTURE-OWNER";
        if (!*proof || !(*proof)->hasQuotaBoundSharedOwner(*proof))
            return "RHI-CANONICAL-CONTENT-STAGING-OWNER";
        if (seal) {
            const auto& staging = (*proof)->originalStaging();
            const auto& buffer = staging->initializedBuffer();
            if (!(*proof)->originalMetadataQuota()
                || (*proof)->originalMetadataQuota()->limitBytes()
                    != (*proof)->budget_.maxCpuMetadataBytes)
                return "RHI-CANONICAL-CONTENT-STAGING-QUOTA";
            if (const auto* id = staging->stagingInitializedFacts())
                return id;
            if (const auto* id = seal->validateInitializedBufferPending(
                    *buffer->state_->lease->originalObject(), *buffer->state_->committed))
                return id;
            if (const auto* id = seal->validateInitializedTexturePending(input.commandIndex,
                    *staging->originalPackingSource()->originalDestinationLease()->originalObject(),
                    staging->destinationStateExpectation(),
                    (*proof)->destinationContentExpectation()))
                return id;
        } else if (const auto* id =
                       WebGpuCanonicalTextureContentUploadSource::validateOriginal(*proof))
            return id;
        const auto index = usage.references()[command.firstReference + 1u].sourceIndex;
        const auto& original =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureUploadSource>>(
                inventory[index]);
        const auto& staging = (*proof)->originalStaging();
        if (!core::SharedOwnerProjection::sameOwner(
                original, staging->originalPackingSource()->originalSource()))
            return "RHI-WEBGPU-UPLOAD-JOIN-ORIGINAL-TEXTURE";
        if (staging->overlapsCpuQuota(root.originalMetadataQuota().get())
            || (*proof)->originalMetadataQuota().get() == root.originalMetadataQuota().get()
            || core::SharedOwnerProjection::sameControlBlock(
                (*proof)->originalMetadataQuota(), root.originalMetadataQuota()))
            return "RHI-WEBGPU-UPLOAD-JOIN-QUOTA-DOMAIN";
        sourceLease = staging->initializedBuffer()->originalLease();
        destinationTexture = original->originalDestinationLease()->originalObject();
    } else
        return "RHI-WEBGPU-UPLOAD-JOIN-COMMAND";
    bool retained = false;
    for (const auto& entry : inventory)
        if (const auto* lease =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(&entry))
            if (core::SharedOwnerProjection::sameOwner(*lease, sourceLease))
                retained = true;
    if (!retained)
        return "RHI-WEBGPU-UPLOAD-JOIN-STAGING-NOT-IN-ORIGINAL-SOURCE";
    if (const auto* id = observeOriginalWebGpuResource(*sourceLease->originalObject(), native[0]))
        return id;
    if (const auto* id = destinationBuffer
            ? observeOriginalWebGpuResource(*destinationBuffer, native[1])
            : observeOriginalWebGpuResource(*destinationTexture, native[1]))
        return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!native[0].buffer || !native[0].buffer->ownerDevice || !native[1].allocationBytes
        || native[0].buffer->ownerDevice
            != (destinationBuffer ? native[1].buffer->ownerDevice : native[1].texture->ownerDevice))
        return "RHI-WEBGPU-UPLOAD-JOIN-NATIVE-DEVICE";
    return nullptr;
#else
    return "RHI-WEBGPU-UPLOAD-JOIN-NATIVE-UNAVAILABLE";
#endif
}

const char* WebGpuCanonicalUploadJoins::inspect(const Root& root,
    const Input* entries,
    std::size_t count,
    const Budget& budget,
    std::uint32_t& owners,
    std::uint64_t& stagingBytes,
    std::uint64_t& destinationBytes,
    const rhi::OriginalCommandRecordingSeal* seal) noexcept
{
    owners = 0u;
    stagingBytes = destinationBytes = 0u;
    if (const auto* id = rhi::OriginalCommandUploadJoinContract::validateIntent(budget))
        return id;
    if (const auto* id = root.validate())
        return id;
    if (seal) {
        if (seal->originalRoot().get() != &root)
            return "RHI-WEBGPU-UPLOAD-JOIN-PENDING-ROOT";
        if (root.status() != Root::Status::Pending || root.journalEmpty())
            return "RHI-WEBGPU-UPLOAD-JOIN-ROOT-STATUS";
    } else if (root.status() != Root::Status::Prepared || !root.hasOriginalPrototypePlan()
        || !root.journalEmpty())
        return "RHI-WEBGPU-UPLOAD-JOIN-ROOT-STATUS";
    if (!entries || !count || count > budget.maxUploads || count > SIZE_MAX / 2u)
        return "RHI-WEBGPU-UPLOAD-JOIN-COUNT";
    if (budget.maxCpuMetadataBytes != root.originalMetadataQuota()->limitBytes())
        return "RHI-WEBGPU-UPLOAD-JOIN-QUOTA";
    const auto& usage = *root.originalGraph()->originalInputs()->originalScalars()->originalUsage();
    const auto& authored = usage.originalSources()->originalBudget();
    core::MemoryBudgetLedger ownerBudget({budget.maxRetainedOwners, true});
    if (!ownerBudget.canAllocate(root.retainedOwnerCount() + std::uint64_t{1u}))
        return "RHI-WEBGPU-UPLOAD-JOIN-OWNER-CAPACITY";
    ownerBudget.recordAllocation(root.retainedOwnerCount() + std::uint64_t{1u});
    std::size_t ordinal = 0u;
    for (std::size_t command = 0u; command < usage.commands().size(); ++command) {
        const auto kind = usage.commands()[command].kind;
        if (kind != rhi::CommandUsageKind::BufferUpload
            && kind != rhi::CommandUsageKind::TextureUpload)
            continue;
        if (ordinal >= count || entries[ordinal].commandIndex != command)
            return "RHI-WEBGPU-UPLOAD-JOIN-ORDER-COVERAGE";
        Native native;
        if (const auto* id = capture(root, entries[ordinal], native, seal))
            return id;
        const auto closure = kind == rhi::CommandUsageKind::BufferUpload
            ? rhi::OriginalCommandInitializedBufferStagingContract::retainedOwnerBound
            : WebGpuCanonicalTextureContentUploadSource::retainedOwnerBound;
        if (!ownerBudget.canAllocate(closure))
            return "RHI-WEBGPU-UPLOAD-JOIN-OWNER-CAPACITY";
        ownerBudget.recordAllocation(closure);
        ++ordinal;
    }
    if (ordinal != count)
        return "RHI-WEBGPU-UPLOAD-JOIN-ORDER-COVERAGE";
    const char* failure = nullptr;
    const auto mapped = [&](std::size_t index) noexcept {
        Native native;
        if (!failure)
            failure = capture(root, entries[index], native, seal);
        return native;
    };
    for (std::size_t endpoint = 0u; endpoint < 2u; ++endpoint) {
        const auto bound = endpoint ? budget.maxNativeDestinations : budget.maxNativeSources;
        const auto identities = core::SharedIdentityProjection::inspect(count,
            count,
            bound,
            [&](std::size_t i) noexcept -> core::SharedIdentityProjection::Record {
                const auto native = mapped(i);
                const auto& resource = native[endpoint];
                return {resource.id.value,
                    resource.buffer ? 1u : 2u,
                    resource.allocationRecord().backing,
                    resource.buffer ? static_cast<const void*>(resource.buffer.get())
                                    : resource.texture.get()};
            });
        if (failure)
            return failure;
        if (identities.status != core::SharedIdentityProjection::Status::Complete)
            return "RHI-WEBGPU-UPLOAD-JOIN-NATIVE-OBJECT-CAPACITY";
        const auto allocation = core::SharedAllocationProjection::inspect(count,
            count,
            endpoint ? budget.maxDestinationResourceBytes : budget.maxStagingResourceBytes,
            [&](std::size_t i) noexcept {
                return mapped(i)[endpoint].allocationRecord();
            });
        if (failure)
            return failure;
        if (allocation.status != core::SharedAllocationProjection::Status::Complete)
            return "RHI-WEBGPU-UPLOAD-JOIN-NATIVE-BYTE-CAPACITY";
        if (endpoint)
            destinationBytes = allocation.bytes;
        else
            stagingBytes = allocation.bytes;
    }
    const auto all = core::SharedIdentityProjection::inspect(count * 2u,
        count * 2u,
        authored.maxNativePayloadObjects,
        [&](std::size_t i) noexcept -> core::SharedIdentityProjection::Record {
            const auto native = mapped(i / 2u);
            const auto& resource = native[i % 2u];
            return {resource.id.value,
                resource.buffer ? 1u : 2u,
                resource.allocationRecord().backing,
                resource.buffer ? static_cast<const void*>(resource.buffer.get())
                                : resource.texture.get()};
        });
    if (failure)
        return failure;
    if (all.status != core::SharedIdentityProjection::Status::Complete)
        return "RHI-WEBGPU-UPLOAD-JOIN-ORIGINAL-PAYLOAD-CAPACITY";
    if (stagingBytes > authored.maxNativeStagingBytes)
        return "RHI-WEBGPU-UPLOAD-JOIN-ORIGINAL-STAGING-CAPACITY";
    owners = static_cast<std::uint32_t>(ownerBudget.stats().allocatedBytes);
    return nullptr;
}

std::shared_ptr<const WebGpuCanonicalUploadJoins> WebGpuCanonicalUploadJoins::prepareOriginal(
    std::shared_ptr<Root> root,
    const rhi::GpuDevice& device,
    const Input* entries,
    std::size_t count,
    const Budget& budget,
    const char*& diagnostic)
{
    diagnostic = rhi::OriginalCommandUploadJoinContract::validateIntent(budget);
    if (diagnostic)
        return {};
    const auto decision =
        arp::rhi::semantics::OriginalCommandUploadJoinLowering::lowering(device.backend());
    if (!decision.privateJoinAvailable) {
        diagnostic = decision.diagnostic;
        return {};
    }
    if ((diagnostic = Root::validateOriginal(root)))
        return {};
    if ((diagnostic = root->compareOriginalGraph(root->originalGraph(), device)))
        return {};
    if ((diagnostic = rhi::OriginalCommandResourceSources::validateRegisteredLifetime(
             root->originalGraph()
                 ->originalInputs()
                 ->originalScalars()
                 ->originalUsage()
                 ->originalSources(),
             device)))
        return {};
    std::uint32_t owners = 0u;
    std::uint64_t staging = 0u, destination = 0u;
    if ((diagnostic = inspect(*root, entries, count, budget, owners, staging, destination)))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(root->originalMetadataQuota());
        auto result = core::makeUnique<WebGpuCanonicalUploadJoins>(
            ConstructionKey{}, root, budget, count, owners, staging, destination);
        for (std::size_t i = 0u; i < count; ++i) {
            Native native;
            if ((diagnostic = capture(*root, entries[i], native)))
                return {};
            result->entries_.append(entries[i]);
            result->native_.append(std::move(native));
        }
        auto shared = shareOriginal(std::move(result));
        if ((diagnostic = shared->validate()))
            return {};
        return shared;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-UPLOAD-JOIN-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-UPLOAD-JOIN-ALLOCATION";
        return {};
    }
}

const char* WebGpuCanonicalUploadJoins::validate() const noexcept
{
    return validateWithSeal(nullptr);
}

const char* WebGpuCanonicalUploadJoins::validateRetained(
    const rhi::OriginalCommandRecordingSeal& seal) const noexcept
{
    if (!core::SharedOwnerProjection::sameOwner(seal.originalRoot(), root_))
        return "RHI-WEBGPU-UPLOAD-JOIN-PENDING-ROOT";
    return validateWithSeal(root_->status() == Root::Status::Prepared ? nullptr : &seal);
}

const char* WebGpuCanonicalUploadJoins::validateWithSeal(
    const rhi::OriginalCommandRecordingSeal* seal) const noexcept
{
    if (!originalMetadataQuota()
        || !core::SharedOwnerProjection::sameOwner(
            originalMetadataQuota(), root_->originalMetadataQuota()))
        return "RHI-WEBGPU-UPLOAD-JOIN-QUOTA-DOMAIN";
    std::uint32_t owners = 0u;
    std::uint64_t staging = 0u, destination = 0u;
    if (const auto* id = inspect(*root_,
            entries_.values().data(),
            entries_.values().size(),
            budget_,
            owners,
            staging,
            destination,
            seal))
        return id;
    if (owners != owners_ || staging != stagingBytes_ || destination != destinationBytes_
        || native_.values().size() != entries_.values().size())
        return "RHI-WEBGPU-UPLOAD-JOIN-RETAINED-COVERAGE";
    for (std::size_t i = 0u; i < entries_.values().size(); ++i) {
        Native native;
        if (const auto* id = capture(*root_, entries_.values()[i], native, seal))
            return id;
        for (std::size_t endpoint = 0u; endpoint < 2u; ++endpoint)
            if (!native[endpoint].sameOwnersAndLayout(native_.values()[i][endpoint]))
                return "RHI-WEBGPU-UPLOAD-JOIN-NATIVE-CHANGED";
    }
    return nullptr;
}

std::shared_ptr<WebGpuNativeCommandPayload> WebGpuNativeCommandPreparation::allocate(
    std::shared_ptr<WebGpuNativeCommandStats> stats)
{
    auto impl = core::makeUnique<WebGpuNativeCommandPayload::Impl>();
    impl->stats = std::move(stats);
    return core::makeShared<WebGpuNativeCommandPayload>(std::move(impl));
}

bool WebGpuNativeCommandPreparation::connect(
    WebGpuNativeCommandPayload& payload, void* expectedDevice, const char*& diagnostic)
{
    diagnostic = nullptr;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    auto& state = *payload.impl_;
    if (state.device || state.queue || state.encoder || !expectedDevice
        || webGpuNativeDeviceLost()) {
        diagnostic = "RHI-WEBGPU-COMMAND-NATIVE-PREPARATION-STATE";
        return false;
    }
    state.device = emscripten_webgpu_get_device();
    if (!state.device || state.device != expectedDevice) {
        diagnostic = "RHI-WEBGPU-COMMAND-DEVICE-MISMATCH";
        return false;
    }
    state.queue = wgpuDeviceGetQueue(state.device);
    if (!state.queue) {
        diagnostic = "RHI-WEBGPU-COMMAND-NATIVE-PREPARATION-FAILED";
        return false;
    }
    return true;
#else
    (void)payload;
    (void)expectedDevice;
    diagnostic = "RHI-WEBGPU-COMMAND-NATIVE-UNAVAILABLE";
    return false;
#endif
}

bool WebGpuNativeCommandPreparation::open(WebGpuNativeCommandPayload& payload,
    void* expectedDevice,
    std::string_view label,
    const char*& diagnostic)
{
    diagnostic = nullptr;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    auto& state = *payload.impl_;
    if (state.canonicalColdAdmission) {
        diagnostic = "RHI-WEBGPU-CANONICAL-COLD-COMMAND-RECORDING-UNPROVEN";
        return false;
    }
    if (!state.device && !connect(payload, expectedDevice, diagnostic))
        return false;
    if (!state.device || state.device != expectedDevice || !state.queue || state.encoder
        || webGpuNativeDeviceLost()) {
        diagnostic = "RHI-WEBGPU-COMMAND-NATIVE-PREPARATION-STATE";
        return false;
    }
    auto descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    descriptor.label = webGpuNativeStringView(label);
    state.encoder = wgpuDeviceCreateCommandEncoder(state.device, &descriptor);
    if (!state.encoder) {
        diagnostic = "RHI-WEBGPU-COMMAND-NATIVE-PREPARATION-FAILED";
        return false;
    }
    if (state.stats)
        ++state.stats->nativeCommandEncoderCreateCount;
    return true;
#else
    (void)payload;
    (void)expectedDevice;
    (void)label;
    diagnostic = "RHI-WEBGPU-COMMAND-NATIVE-UNAVAILABLE";
    return false;
#endif
}

WebGpuNativeCommandPreparation::ShellIdentity WebGpuNativeCommandPreparation::inspectShellIdentity(
    const std::shared_ptr<WebGpuNativeCommandPayload>& command,
    const std::shared_ptr<const void>& admission,
    void* device,
    void* queue,
    void* closureDevice,
    std::uint64_t identity) noexcept
{
    if (!command || !command->sharedOwnerMatches(command) || !command->impl_)
        return ShellIdentity::Owner;
    const auto& state = *command->impl_;
    if (!core::SharedOwnerProjection::sameOwner(state.canonicalColdAdmission, admission)
        || state.ordinaryPrepared || state.ordinaryTransfer || state.ordinaryUnsupported
        || state.ordinaryOriginalStates || state.ordinaryOriginalStateTransaction
        || state.ordinaryOriginalTextureTransaction || state.ordinaryOriginalReadbackCopies
        || !state.recordingIdentity || state.recordingIdentity != identity)
        return ShellIdentity::Mode;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!state.device || !state.queue || state.device != device || state.queue != queue
        || state.device != closureDevice)
        return ShellIdentity::Native;
    return ShellIdentity::Complete;
#else
    (void)device;
    (void)queue;
    (void)closureDevice;
    return ShellIdentity::Native;
#endif
}

const char* WebGpuNativeCommandPreparation::inspectCanonicalColdCommand(
    const std::shared_ptr<const WebGpuCanonicalColdAdmission>& admission,
    const rhi::OriginalCommandNativeColdCommandBudget& budget) noexcept
{
    using Contract = rhi::OriginalCommandNativeColdCommandContract;
    if (const auto* id = Contract::validateIntent(budget))
        return id;
    if (const auto* id = WebGpuCanonicalColdAdmission::validateOriginal(admission))
        return id;
    return inspectCanonicalCommandOwnership(admission, budget);
}

const char* WebGpuNativeCommandPreparation::inspectCanonicalCommandOwnership(
    const std::shared_ptr<const WebGpuCanonicalColdAdmission>& admission,
    const rhi::OriginalCommandNativeColdCommandBudget& budget) noexcept
{
    using Contract = rhi::OriginalCommandNativeColdCommandContract;
    if (const auto* id = Contract::validateIntent(budget))
        return id;
    if (!admission || !admission->hasQuotaBoundSharedOwner(admission))
        return "RHI-WEBGPU-CANONICAL-COLD-OWNER";
    if (const auto* id = rhi::OriginalGeneralOutputPairingContract::validatePair(
            admission->originalBudget().generalOutput, budget.generalOutput))
        return id;
    if (const auto* id = admission->validateRetainedOwnership())
        return id;
    if (budget.maxCpuMetadataBytes
        != admission->originalRoot()->originalMetadataQuota()->limitBytes())
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-QUOTA";
    constexpr auto additional = Contract::additionalOwnerCount;
    if (admission->retainedOwnerCount() > UINT32_MAX - additional)
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-OWNER-CAPACITY";
    const auto owners = admission->retainedOwnerCount() + additional;
    const auto& lifecycle = admission->originalReservation()->originalSeal()->originalBudget();
    if (owners > budget.maxRetainedOwners || owners > admission->originalBudget().maxRetainedOwners
        || owners > lifecycle.maxRecordingOwners || owners > lifecycle.maxSubmissionOwners
        || owners > lifecycle.maxCallbackOwners)
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-OWNER-CAPACITY";
    return nullptr;
}

std::shared_ptr<const WebGpuCanonicalColdCommand> WebGpuNativeCommandPreparation::prepareCanonical(
    std::shared_ptr<const WebGpuCanonicalColdAdmission> admission,
    const rhi::OriginalCommandNativeColdCommandBudget& budget,
    const char*& diagnostic)
{
    diagnostic = inspectCanonicalColdCommand(admission, budget);
    if (diagnostic)
        return {};
    const auto& seal = admission->originalReservation()->originalSeal();
    if (seal->nativeColdCommandClaimed_) {
        diagnostic = "RHI-WEBGPU-CANONICAL-COLD-COMMAND-DUPLICATE";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-CANONICAL-COLD-COMMAND-NATIVE-UNAVAILABLE";
    return {};
#else
    try {
        core::CpuMemoryQuotaScope scope(admission->originalRoot()->originalMetadataQuota());
        auto command = allocate({});
        command->impl_->canonicalColdAdmission = admission;
        if (!connect(*command, admission->originalClosure()->nativeDevice(), diagnostic))
            return {};
        auto result =
            WebGpuCanonicalColdCommand::shareOriginal(core::makeUnique<WebGpuCanonicalColdCommand>(
                WebGpuCanonicalColdCommand::ConstructionKey{},
                admission,
                command,
                budget,
                command->impl_->device,
                command->impl_->queue,
                command->recordingIdentity()));
        // Every fallible C++ allocation and exact native connection precedes the
        // sticky claim. /1 cannot issue GPU work, including through raw commands.
        if ((diagnostic = validateCanonical(*result, false)))
            return {};
        seal->nativeColdCommandClaimed_ = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-CANONICAL-COLD-COMMAND-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-CANONICAL-COLD-COMMAND-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuNativeCommandPreparation::validateCanonical(
    const WebGpuCanonicalColdCommand& original, bool requireClaim) noexcept
{
    if (const auto* id = inspectCanonicalColdCommand(original.admission_, original.budget_))
        return id;
    if (const auto* id = validateCanonicalIdentity(original, requireClaim))
        return id;
    const auto& state = *original.command_->impl_;
    if (state.finished || state.submitted)
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-STATE";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (state.encoder || state.commandBuffer || state.renderPass || state.computePass)
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-NATIVE-STATE";
    return nullptr;
#else
    return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-NATIVE-UNAVAILABLE";
#endif
}

const char* WebGpuNativeCommandPreparation::validateCanonicalIdentity(
    const WebGpuCanonicalColdCommand& original, bool requireClaim) noexcept
{
    if (const auto* id = inspectCanonicalCommandOwnership(original.admission_, original.budget_))
        return id;
    if (!core::SharedOwnerProjection::sameOwner(original.originalMetadataQuota(),
            original.admission_->originalRoot()->originalMetadataQuota()))
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-QUOTA";
    if (requireClaim
        && !original.admission_->originalReservation()->originalSeal()->nativeColdCommandClaimed_)
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-CLAIM";
    switch (inspectShellIdentity(original.command_,
        original.admission_,
        original.device_,
        original.queue_,
        original.admission_->originalClosure()->nativeDevice(),
        original.identity_)) {
    case ShellIdentity::Owner:
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-NATIVE-OWNER";
    case ShellIdentity::Mode:
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-STATE";
    case ShellIdentity::Native:
        return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-NATIVE-STATE";
    case ShellIdentity::Complete:
        break;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return WebGpuNativeCompletionReceipt::inspect(original.device_, original.queue_);
#else
    return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-NATIVE-UNAVAILABLE";
#endif
}

const char* WebGpuNativeCommandPreparation::validateCanonicalLive(
    const WebGpuCanonicalColdCommand& original) noexcept
{
    if (const auto* id = validateCanonicalIdentity(original, true))
        return id;
    const auto& root = original.admission_->originalRoot();
    if (root->status() != rhi::OriginalCommandStateTransaction::Status::Pending
        || root->journalEmpty())
        return "RHI-WEBGPU-CANONICAL-LIVE-COMMAND-PENDING";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto& state = *original.command_->impl_;
    // Inspection at the post-record boundary: a finished native payload and
    // owned pending finals still prove neither scope nor queue success.
    if (!state.encoder || !state.finished || !state.commandBuffer || state.renderPass
        || state.computePass)
        return "RHI-WEBGPU-CANONICAL-LIVE-COMMAND-NATIVE-STATE";
    return nullptr;
#else
    return "RHI-WEBGPU-CANONICAL-COLD-COMMAND-NATIVE-UNAVAILABLE";
#endif
}

const char* WebGpuCanonicalColdCommand::validate() const noexcept
{
    return WebGpuNativeCommandPreparation::validateCanonical(*this);
}

const char* WebGpuNativeCommandPreparation::inspectTypedColdCommand(
    const std::shared_ptr<const WebGpuOriginalTypedColdAdmission>& admission,
    const rhi::OriginalTypedNativeColdCommandRequest& request) noexcept
{
    using C = rhi::OriginalTypedNativeColdCommandContract;
    if (const auto* id = C::validateIntent(request))
        return id;
    if (const auto* id = WebGpuOriginalTypedColdAdmission::validateOriginal(admission))
        return id;
    if (const auto* id = C::validateProfile(request, admission->originalRequest().contractVersion))
        return id;
    const auto lowering = rhi::semantics::OriginalTypedNativeColdCommandLowering::lowering(
        rhi::Backend::WebGPU, request.contractVersion);
    if (!lowering.connectionAvailable)
        return lowering.diagnostic;
    const auto& root = admission->originalRoot();
    if (request.maxCpuMetadataBytes != root->originalMetadataQuota()->limitBytes())
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-QUOTA";
    const auto& source = root->originalGraph()
                             ->originalInputs()
                             ->originalScalars()
                             ->originalUsage()
                             ->originalSources();
    return C::validateOwners(admission->retainedOwnerCount(),
        request,
        source->originalBudget(),
        admission->originalReservation()->originalSeal()->originalBudget(),
        admission->originalRequest().maxRetainedOwners,
        root->retainedOwnerCount());
}

std::shared_ptr<const WebGpuOriginalTypedColdCommand>
WebGpuNativeCommandPreparation::prepareTypedOriginal(
    std::shared_ptr<const WebGpuOriginalTypedColdAdmission> admission,
    const rhi::OriginalTypedNativeColdCommandRequest& request,
    const char*& diagnostic)
{
    if ((diagnostic = inspectTypedColdCommand(admission, request)))
        return {};
    const auto& seal = admission->originalReservation()->originalSeal();
    if (seal->nativeColdCommandClaimed_) {
        diagnostic = "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-TYPED-COLD-COMMAND-SDK-UNAVAILABLE";
    return {};
#else
    try {
        core::CpuMemoryQuotaScope scope(admission->originalRoot()->originalMetadataQuota());
        auto command = allocate({});
        command->impl_->canonicalColdAdmission = admission;
        const auto& closure = admission->originalPayload()->originalDecision()->originalClosure();
        if (!connect(*command, closure->nativeDevice(), diagnostic))
            return {};
        auto result = WebGpuOriginalTypedColdCommand::shareOriginal(
            core::makeUnique<WebGpuOriginalTypedColdCommand>(
                WebGpuOriginalTypedColdCommand::ConstructionKey{},
                admission,
                command,
                request,
                command->impl_->device,
                command->impl_->queue,
                command->recordingIdentity()));
        if ((diagnostic = validateTypedUnclaimed(*result)))
            return {};
        // This genuine original seal is the only claim authority. All fallible
        // allocations and exact device/queue checks precede its publication.
        seal->nativeColdCommandClaimed_ = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-TYPED-COLD-COMMAND-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-TYPED-COLD-COMMAND-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuNativeCommandPreparation::validateTypedUnclaimed(
    const WebGpuOriginalTypedColdCommand& original) noexcept
{
    if (const auto* id = inspectTypedColdCommand(original.admission_, original.request_))
        return id;
    if (!core::SharedOwnerProjection::sameOwner(original.originalMetadataQuota(),
            original.admission_->originalRoot()->originalMetadataQuota()))
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-QUOTA";
    const auto& closure =
        original.admission_->originalPayload()->originalDecision()->originalClosure();
    switch (inspectShellIdentity(original.command_,
        original.admission_,
        original.device_,
        original.queue_,
        closure->nativeDevice(),
        original.identity_)) {
    case ShellIdentity::Owner:
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-NATIVE-OWNER";
    case ShellIdentity::Mode:
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-STATE";
    case ShellIdentity::Native:
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-NATIVE-STATE";
    case ShellIdentity::Complete:
        break;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto& state = *original.command_->impl_;
    if (state.finished || state.submitted || state.encoder || state.commandBuffer
        || state.renderPass || state.computePass)
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-NATIVE-STATE";
    return WebGpuNativeCompletionReceipt::inspect(original.device_, original.queue_);
#else
    return "RHI-WEBGPU-TYPED-COLD-COMMAND-SDK-UNAVAILABLE";
#endif
}

const char* WebGpuNativeCommandPreparation::validateTypedAdmissionRetained(
    const std::shared_ptr<const WebGpuOriginalTypedColdAdmission>& admission) noexcept
{
    using A = WebGpuOriginalTypedColdAdmission;
    if (!admission || !admission->hasQuotaBoundSharedOwner(admission))
        return "RHI-WEBGPU-TYPED-COLD-OWNER";
    const auto& payload = admission->originalPayload();
    if (!payload || !payload->hasQuotaBoundSharedOwner(payload))
        return "RHI-WEBGPU-TYPED-PAYLOAD-OWNER";
    const auto& decision = payload->originalDecision();
    if (!decision || !decision->hasQuotaBoundSharedOwner(decision))
        return "RHI-WEBGPU-TYPED-DEPENDENCY-OWNER";
    const auto& closure = decision->originalClosure();
    if (!closure || !closure->hasQuotaBoundSharedOwner(closure)
        || !rhi::OriginalTypedNativePayloadContract::profileMatches(payload->originalRequest(),
            decision->originalRequest().contractVersion,
            closure->closureContractVersion()))
        return "RHI-WEBGPU-TYPED-DEPENDENCY-CLOSURE-VERSION";
    if (const auto* id = rhi::OriginalTypedNativeColdContract::validatePayloadProfile(
            admission->originalRequest(), payload->originalRequest().contractVersion))
        return id;
    const auto& root = closure->originalRoot();
    const auto& reservation = admission->originalReservation();
    if (!reservation || !reservation->hasQuotaBoundSharedOwner(reservation))
        return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER";
    const auto& seal = reservation->originalSeal();
    if (!core::SharedOwnerProjection::sameOwner(seal->originalRoot(), root))
        return "RHI-ORIGINAL-RECORDING-SEAL-ROOT";
    if (const auto* id = reservation->validateRetainedOwnership())
        return id;
    if (const auto* id = rhi::OriginalCommandStateTransaction::validateOriginal(root))
        return id;
    if (!seal->nativeColdCommandClaimed_ || !seal->typedCallbackCustodyClaimed_)
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-CLAIM";
    if (!core::SharedOwnerProjection::sameOwner(
            closure->originalStream()->originalInputs(), root->originalGraph()->originalInputs()))
        return "RHI-ORIGINAL-STREAM-STATE-IDENTITY";
    for (const auto& quota : std::array{admission->originalMetadataQuota(),
             payload->originalMetadataQuota(),
             decision->originalMetadataQuota(),
             closure->originalMetadataQuota()})
        if (!core::SharedOwnerProjection::sameOwner(quota, root->originalMetadataQuota()))
            return "RHI-WEBGPU-TYPED-COLD-COMMAND-QUOTA";
    A::Requirements required;
    if (root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared)
        return A::validateOriginal(admission);
    if (root->status() != rhi::OriginalCommandStateTransaction::Status::Pending
        || root->journalEmpty())
        return "RHI-WEBGPU-TYPED-COLD-RETAINED-PENDING";
    if (const auto* id = closure->nativeLossDiagnostic())
        return id;
    const auto geometryReady =
        [&](const WebGpuCanonicalBufferUploadSource& source) noexcept -> const char* {
        const auto lowering =
            rhi::semantics::OriginalCommandNativeClosureLowering::geometryPendingReadLowering(
                rhi::Backend::WebGPU);
        if (!lowering.resourceClosureAvailable)
            return lowering.diagnostic;
        if (const auto* id = source.initializedReadyFacts())
            return id;
        return seal->validateInitializedBufferPending(*source.state_->lease->originalObject(),
            *source.state_->committed,
            rhi::OriginalInitializedBufferPendingUse::ImmutableGeometryRead1);
    };
    if (closure->closureContractVersion() == 10u) {
        const auto lowering =
            rhi::semantics::OriginalCommandNativeClosureLowering::generalPendingReadLowering(
                rhi::Backend::WebGPU, 3u);
        if (!lowering.resourceClosureAvailable)
            return lowering.diagnostic;
        const auto graphicsBufferReady =
            [&](const WebGpuCanonicalBufferUploadSource& source) noexcept -> const char* {
            const auto type = source.state_->budget.shaderContent.type;
            if (type != rhi::DescriptorType::UniformBuffer
                && type != rhi::DescriptorType::ReadOnlyStorageBuffer)
                return "RHI-ORIGINAL-INITIALIZED-GRAPHICS-BUFFER-PENDING-TYPE";
            if (const auto* id = source.initializedReadyFacts())
                return id;
            return seal->validateInitializedBufferPending(*source.state_->lease->originalObject(),
                *source.state_->committed,
                rhi::OriginalInitializedBufferPendingUse::ImmutableGraphicsShaderBufferRead4,
                4u);
        };
        const auto graphicsTextureRead = [&](const auto& content,
                                             const auto& view) noexcept -> const char* {
            if (!content.completed)
                return "RHI-WEBGPU-GRAPHICS-TEXTURE-COMPLETED-INPUT";
            return seal->validateCompletedTextureReadPending(
                *view->originalParentLease()->originalObject(), *content.completed, 3u);
        };
        if (const auto* id =
                closure->validateImmutableBindings(graphicsBufferReady, graphicsTextureRead))
            return id;
    } else if (closure->closureContractVersion() == 8u || closure->closureContractVersion() == 9u) {
        const bool finite = closure->closureContractVersion() == 9u;
        const auto lowering =
            rhi::semantics::OriginalCommandNativeClosureLowering::generalPendingReadLowering(
                rhi::Backend::WebGPU, finite ? 2u : 1u);
        if (!lowering.resourceClosureAvailable)
            return lowering.diagnostic;
        const auto uniformReady =
            [&](const WebGpuCanonicalBufferUploadSource& source) noexcept -> const char* {
            const auto type = source.state_->budget.shaderContent.type;
            if (type != rhi::DescriptorType::UniformBuffer
                && (!finite || type != rhi::DescriptorType::ReadOnlyStorageBuffer))
                return "RHI-ORIGINAL-INITIALIZED-UNIFORM-PENDING-TYPE";
            if (const auto* id = source.initializedReadyFacts())
                return id;
            return seal->validateInitializedBufferPending(*source.state_->lease->originalObject(),
                *source.state_->committed,
                finite ? rhi::OriginalInitializedBufferPendingUse::ImmutableShaderBufferRead3
                       : rhi::OriginalInitializedBufferPendingUse::ImmutableUniformRead2,
                finite ? 3u : 2u);
        };
        const auto textureRead = [&](const auto& content,
                                     const auto& view) noexcept -> const char* {
            if (!content.completed)
                return "RHI-WEBGPU-GENERAL-TEXTURE-COMPLETED-INPUT";
            return seal->validateCompletedTextureReadPending(
                *view->originalParentLease()->originalObject(),
                *content.completed,
                finite ? 2u : 1u);
        };
        if (const auto* id = closure->validateImmutableBindings(uniformReady, textureRead))
            return id;
    } else if (const auto* id = closure->validateImmutableBindings(geometryReady))
        return id;
    if (const auto* id = decision->validateImmutableBindings())
        return id;
    if (const auto* id = payload->validateImmutableRecords())
        return id;
    if (const auto* id = A::deriveImmutableRequirements(
            payload, admission->request_, required, &admission->requirements()))
        return id;
    if (!required.matches(admission->requirements()))
        return "RHI-WEBGPU-TYPED-COLD-REQUIREMENTS";
    if (const auto* id =
            A::inspectImmutableBinding(payload, reservation, admission->request_, required))
        return id;
    return nullptr;
}

const char* WebGpuNativeCommandPreparation::validateTypedRetained(
    const WebGpuOriginalTypedColdCommand& original) noexcept
{
    const auto& admission = original.originalAdmission();
    if (const auto* id = validateTypedAdmissionRetained(admission))
        return id;
    if (const auto* id = rhi::OriginalTypedNativeColdCommandContract::validateProfile(
            original.originalRequest(), admission->originalRequest().contractVersion))
        return id;
    const auto& closure = admission->originalPayload()->originalDecision()->originalClosure();
    const auto& root = admission->originalRoot();
    const auto& seal = admission->originalReservation()->originalSeal();
    if (!core::SharedOwnerProjection::sameOwner(
            original.originalMetadataQuota(), root->originalMetadataQuota()))
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-QUOTA";
    if (const auto* id =
            rhi::OriginalTypedNativeColdCommandContract::validateIntent(original.request_))
        return id;
    const auto& source = root->originalGraph()
                             ->originalInputs()
                             ->originalScalars()
                             ->originalUsage()
                             ->originalSources();
    if (const auto* id = rhi::OriginalTypedNativeColdCommandContract::validateOwners(
            admission->retainedOwnerCount(),
            original.request_,
            source->originalBudget(),
            seal->originalBudget(),
            admission->originalRequest().maxRetainedOwners,
            root->retainedOwnerCount()))
        return id;
    if (original.request_.maxCpuMetadataBytes != root->originalMetadataQuota()->limitBytes())
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-QUOTA";
    if (const auto* id = closure->nativeLossDiagnostic())
        return id;
    switch (inspectShellIdentity(original.command_,
        admission,
        original.device_,
        original.queue_,
        closure->nativeDevice(),
        original.identity_)) {
    case ShellIdentity::Owner:
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-NATIVE-OWNER";
    case ShellIdentity::Mode:
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-STATE";
    case ShellIdentity::Native:
        return "RHI-WEBGPU-TYPED-COLD-COMMAND-NATIVE-STATE";
    case ShellIdentity::Complete:
        break;
    }
    return WebGpuNativeCompletionReceipt::inspect(original.device_, original.queue_);
}

const char* WebGpuNativeCommandPreparation::validateTypedOriginal(
    const WebGpuOriginalTypedColdCommand& original) noexcept
{
    if (const auto* id = validateTypedUnclaimed(original))
        return id;
    return original.admission_->originalReservation()->originalSeal()->nativeColdCommandClaimed_
        ? nullptr
        : "RHI-WEBGPU-TYPED-COLD-COMMAND-CLAIM";
}

class WebGpuCanonicalCopyPassLowering::Payload final {
public:
    using Closure = WebGpuCanonicalNativeClosure;
    using Stream = rhi::OriginalCommandStreamProgram;
    using Copy = std::variant<rhi::BufferCopyDesc,
        rhi::BufferToTextureCopyDesc,
        rhi::TextureToBufferCopyDesc,
        rhi::TextureToTextureCopyDesc>;

    class Operation final {
    public:
        template <class Desc>
        Operation(const Closure::Entry* source, const Closure::Entry* destination, const Desc& desc)
            : source(source), destination(destination), copy(desc)
        {}

        const Closure::Entry* source;
        const Closure::Entry* destination;
        Copy copy;

        bool matches(const Closure::Entry* s,
            const Closure::Entry* d,
            const rhi::TransferCopyArguments& fields) const noexcept
        {
            return source == s && destination == d
                && std::visit(
                    [&](const auto& value) noexcept {
                        return fields.matches(
                            rhi::TransferCopyArguments::captureScalarFields(value));
                    },
                    copy);
        }
    };

    struct OrderedOperation {
        Stream::Operation original;
        std::size_t passIndex = SIZE_MAX;
        std::size_t uploadFirst = SIZE_MAX, uploadCount = 0u, joinIndex = SIZE_MAX;
        std::size_t readbackFirst = SIZE_MAX, readbackCount = 0u, readbackJoin = SIZE_MAX;
    };

    class IndexedCopyOperation final {
    public:
        template <class Desc>
        IndexedCopyOperation(std::size_t command,
            std::size_t join,
            const Closure::Entry* s,
            const Closure::Entry* d,
            const Desc& desc)
            : command(command), join(join), copy(s, d, desc)
        {}

        const std::size_t command, join;
        Operation copy;
    };

    using UploadOperation = IndexedCopyOperation;
    using ReadbackOperation = IndexedCopyOperation;
    using Pass = WebGpuOriginalRenderPassPayload;

    Payload(const Closure& closure,
        std::size_t copyCount,
        std::size_t passCount,
        std::size_t commandCount,
        std::shared_ptr<const WebGpuCanonicalUploadJoins> joins = {},
        std::size_t uploadCount = 0u,
        std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbackJoins = {},
        std::size_t readbackCount = 0u)
        : closure(&closure), copies(copyCount), passes(passCount), operations(commandCount),
          joins(std::move(joins)), uploads(uploadCount), readbackJoins(std::move(readbackJoins)),
          readbacks(readbackCount)
    {}

    static const Closure::Entry* find(const Closure& closure, rhi::GpuResourceId id) noexcept
    {
        return closure.entries_.findIf([&](const auto& entry) noexcept {
            return entry.object.id() == id;
        });
    }

    template <class Visitor>
    static const char* visitInputs(
        const Closure& closure, const Budget& budget, Visitor&& visitor) noexcept
    {
        const auto& inputs = closure.originalRoot()->originalGraph()->originalInputs();
        const auto& source =
            inputs->originalScalars()->originalUsage()->originalSources()->originalBudget();
        rhi::OriginalCommandStreamBudget stream;
        stream.contractVersion = arp::rhi::semantics::OriginalCommandStreamLowering::version;
        stream.prototypeOptIn = budget.prototypeOptIn;
        stream.maxCommands = source.maxCommands;
        stream.maxDescriptors = source.maxInputDescriptors;
        stream.maxReferences = source.maxReferences;
        stream.maxCopyElements = budget.maxCopyOperations;
        stream.maxRenderPasses = budget.contractVersion == 1u
            ? WebGpuCanonicalColdAdmission::selectedRequirements().renderPasses
            : budget.maxRenderPasses;
        stream.maxAttachmentReferences = source.maxPassAttachmentReferences;
        stream.maxRetainedOwners = budget.maxRetainedOwners;
        stream.maxCpuMetadataBytes = budget.maxCpuMetadataBytes;
        if (budget.contractVersion == 3u) {
            stream.maxBufferUploads = budget.maxBufferUploadCommands;
            stream.maxTextureUploadMips = budget.maxTextureUploadMips;
            stream.maxBufferUploadDeclaredBytes = budget.maxBufferUploadDeclaredBytes;
            stream.maxTextureUploadPixelBytes = budget.maxTextureUploadPixelBytes;
            const auto inspection = rhi::OriginalCommandOrderedUploadContract::inspect(
                inputs, stream, budget.uploadStaging, budget.uploadWork);
            if (inspection.diagnostic)
                return inspection.diagnostic;
        }
        if ((budget.contractVersion == 4u || budget.contractVersion == 5u)) {
            stream.maxTextureReadbackMips = budget.readback.maxNativeCopyCommands;
            stream.maxReadbackDeclaredBytes = budget.readback.maxDeclaredReadbackBytes;
            const auto inspection = rhi::OriginalCommandOrderedReadbackContract::inspect(
                inputs, stream, budget.readback);
            if (inspection.diagnostic)
                return inspection.diagnostic;
        }
        const auto* diagnostic =
            Stream::visitOriginal(inputs, stream, std::forward<Visitor>(visitor));
        // Keep the selected native limit's versioned diagnostic at its public
        // boundary; no fallback or partially visited projection is published.
        if (diagnostic && std::string_view(diagnostic) == "RHI-ORIGINAL-STREAM-COPY-CAPACITY")
            return "RHI-WEBGPU-CANONICAL-LOWERING-COPY-CAPACITY";
        return diagnostic;
    }

    template <class Visitor>
    static const char* inspectCopies(const Closure& closure,
        const Budget& budget,
        Visitor&& visitor,
        std::uint32_t& count) noexcept
    {
        count = 0u;
        using K = rhi::CommandUsageKind;
        const auto& inputs = *closure.originalRoot()->originalGraph()->originalInputs();
        const auto* streamDiagnostic = visitInputs(
            closure, budget, [&](const Stream::Operation& operation) noexcept -> const char* {
                if (operation.family != Stream::Family::Copy)
                    return nullptr;
                const auto kind = operation.kind;
                if (operation.firstElement != count)
                    return "RHI-WEBGPU-CANONICAL-LOWERING-COPY-ORDINAL";
                const auto* descriptor = &inputs.descriptors()[operation.descriptorIndex].arguments;
                const auto* fields = std::get_if<rhi::TransferCopyArguments>(descriptor);
                const rhi::TransferCopyInputSnapshot* full = nullptr;
                if (const auto* snapshot =
                        std::get_if<std::shared_ptr<const rhi::TransferCopyInputSnapshot>>(
                            descriptor)) {
                    if (const auto* id =
                            rhi::TransferCopyInputSnapshot::validateOriginal(*snapshot))
                        return id;
                    full = snapshot->get();
                    fields = &full->scalarFields();
                }
                if (!fields || fields->kind() != kind)
                    return "RHI-WEBGPU-CANONICAL-LOWERING-COPY-INPUT";
                const auto* source = find(closure, fields->source());
                const auto* destination = find(closure, fields->destination());
                if (!source || !destination || source == destination)
                    return "RHI-WEBGPU-CANONICAL-LOWERING-COPY-BINDING";
                const auto& s = source->backing();
                const auto& d = destination->backing();
                if (d.writeSeal && d.writeSeal->transferWritesSealed())
                    return "RHI-BUFFER-GPU-WRITES-SEALED";
                const auto inspect = [&](const auto& desc) noexcept -> const char* {
                    if (count >= budget.maxCopyOperations)
                        return "RHI-WEBGPU-CANONICAL-LOWERING-COPY-CAPACITY";
                    if (const auto* id = WebGpuTransferCopyValidation::validate(s, d, desc))
                        return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
                    using Desc = std::decay_t<decltype(desc)>;
                    if constexpr (std::is_same_v<Desc, rhi::BufferToTextureCopyDesc>
                        || std::is_same_v<Desc, rhi::TextureToBufferCopyDesc>) {
                        const auto* texture = std::is_same_v<Desc, rhi::BufferToTextureCopyDesc>
                            ? d.texture.get()
                            : s.texture.get();
                        if (!texture)
                            return "RHI-WEBGPU-CANONICAL-LOWERING-COPY-BINDING";
                        if (const auto* id = WebGpuNativeTransferCopyLowering::textureDiagnostic(
                                *texture, desc.textureSubresources, desc.extent))
                            return id;
                    } else if constexpr (std::is_same_v<Desc, rhi::TextureToTextureCopyDesc>) {
                        if (const auto* id = WebGpuNativeTransferCopyLowering::textureDiagnostic(
                                *s.texture, desc.sourceSubresources, desc.extent))
                            return id;
                        if (const auto* id = WebGpuNativeTransferCopyLowering::textureDiagnostic(
                                *d.texture, desc.destinationSubresources, desc.extent))
                            return id;
                    }
#else
                return "RHI-WEBGPU-CANONICAL-LOWERING-NATIVE-UNAVAILABLE";
#endif
                    if (const auto* id = visitor(source, destination, desc, count))
                        return id;
                    ++count;
                    return nullptr;
                };
                const char* diagnostic = nullptr;
                switch (kind) {
                case K::BufferCopy:
                    diagnostic = rhi::TransferCopyProjection::visit<rhi::BufferCopyDesc>(
                        *fields, full, inspect);
                    break;
                case K::BufferToTextureCopy:
                    diagnostic = rhi::TransferCopyProjection::visit<rhi::BufferToTextureCopyDesc>(
                        *fields, full, inspect);
                    break;
                case K::TextureToBufferCopy:
                    diagnostic = rhi::TransferCopyProjection::visit<rhi::TextureToBufferCopyDesc>(
                        *fields, full, inspect);
                    break;
                case K::TextureToTextureCopy:
                    diagnostic = rhi::TransferCopyProjection::visit<rhi::TextureToTextureCopyDesc>(
                        *fields, full, inspect);
                    break;
                default:
                    return "RHI-WEBGPU-CANONICAL-LOWERING-COPY-INPUT";
                }
                if (diagnostic)
                    return diagnostic;
                return count - operation.firstElement == operation.elementCount
                    ? nullptr
                    : "RHI-WEBGPU-CANONICAL-LOWERING-COPY-EXPANSION";
            });
        if (streamDiagnostic)
            return streamDiagnostic;
        return count || budget.contractVersion >= 2u ? nullptr
                                                     : "RHI-WEBGPU-CANONICAL-LOWERING-COPY-INPUT";
    }

    template <class Visitor>
    static const char* inspectUploads(const Closure& closure,
        const Budget& budget,
        const std::shared_ptr<const WebGpuCanonicalUploadJoins>& joins,
        Visitor&& visitor,
        std::uint32_t& count) noexcept
    {
        count = 0u;
        if (budget.contractVersion != 3u)
            return joins ? "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-PROFILE" : nullptr;
        if (const auto* id = WebGpuCanonicalUploadJoins::validateOriginal(joins))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(joins->originalRoot(), closure.originalRoot()))
            return "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-ROOT";
        const auto& inputs = *closure.originalRoot()->originalGraph()->originalInputs();
        std::size_t ordinal = 0u;
        const auto* diagnostic = visitInputs(
            closure, budget, [&](const Stream::Operation& operation) noexcept -> const char* {
                if (operation.family != Stream::Family::BufferUpload
                    && operation.family != Stream::Family::TextureUpload)
                    return nullptr;
                if (ordinal >= joins->entries_.values().size()
                    || joins->entries_.values()[ordinal].commandIndex != operation.commandIndex)
                    return "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-ORDINAL";
                const auto& join = joins->entries_.values()[ordinal];
                const auto& native = joins->native_.values()[ordinal];
                const auto* source = find(closure, native[0].id);
                const auto* destination = find(closure, native[1].id);
                if (!source || !destination || source == destination
                    || !source->backing().sameOwnersAndLayout(native[0])
                    || !destination->backing().sameOwnersAndLayout(native[1]))
                    return "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-BINDING";
                const auto first = count;
                const auto inspect = [&](const auto& desc) noexcept -> const char* {
                    if (count == budget.maxUploadCopyOperations)
                        return "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-COPY-CAPACITY";
                    if constexpr (std::is_same_v<std::decay_t<decltype(desc)>,
                                      rhi::BufferCopyDesc>) {
                        if (const auto* id =
                                WebGpuTransferCopyValidation::validate(native[0], native[1], desc))
                            return id;
                        if (destination->object.buffer->gpuTransferWritesSealed())
                            return "RHI-BUFFER-GPU-WRITES-SEALED";
                    } else {
                        if (!destination->object.texture->originalContentExclusive()
                            || !native[1].texture->contentAccessPolicy().originalUploadExclusive())
                            return "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-CONTENT-POLICY";
                        if (const auto* id =
                                WebGpuTransferCopyValidation::inspectBufferTextureLayout(
                                    native[0], native[1], desc))
                            return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
                        if (const auto* id = WebGpuNativeTransferCopyLowering::textureDiagnostic(
                                *native[1].texture, desc.textureSubresources, desc.extent))
                            return id;
#else
                    return "RHI-WEBGPU-CANONICAL-LOWERING-NATIVE-UNAVAILABLE";
#endif
                    }
                    if (const auto* id = visitor(
                            operation.commandIndex, ordinal, source, destination, desc, count))
                        return id;
                    ++count;
                    return nullptr;
                };
                if (operation.family == Stream::Family::BufferUpload) {
                    rhi::BufferCopyDesc desc;
                    if (!std::get<rhi::BufferUploadArguments>(
                            inputs.descriptors()[operation.descriptorIndex].arguments)
                            .copyArguments()
                            .write(desc))
                        return "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-INPUT";
                    if (const auto* id = inspect(desc))
                        return id;
                } else {
                    const auto* proof = std::get_if<
                        std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource>>(
                        &join.source);
                    if (!proof || !*proof)
                        return "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-INPUT";
                    const auto& staging = (*proof)->originalStaging();
                    for (std::uint64_t mip = 0u; mip < operation.elementCount; ++mip) {
                        rhi::semantics::TextureUploadPackedCopy mapped;
                        if (const auto* id =
                                WebGpuCanonicalTextureUploadSource::mapPackedCopyOriginal(staging,
                                    staging->originalPackingSource(),
                                    static_cast<std::size_t>(mip),
                                    mapped))
                            return id;
                        rhi::BufferToTextureCopyDesc desc;
                        desc.sourceBuffer = mapped.sourceBuffer;
                        desc.destinationTexture = mapped.destinationTexture;
                        desc.bufferOffsetBytes = mapped.bufferOffsetBytes;
                        desc.bytesPerRow = mapped.bytesPerRow;
                        desc.rowsPerImage = mapped.rowsPerImage;
                        desc.textureSubresources = mapped.subresources;
                        desc.extent = mapped.extent;
                        if (const auto* id = inspect(desc))
                            return id;
                    }
                }
                if (count - first != operation.elementCount)
                    return "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-EXPANSION";
                ++ordinal;
                return nullptr;
            });
        if (diagnostic)
            return diagnostic;
        return ordinal == joins->uploadCount() ? nullptr
                                               : "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-COVERAGE";
    }

    template <class Visitor>
    static const char* inspectReadbacks(const Closure& closure,
        const Budget& budget,
        const std::shared_ptr<const WebGpuCanonicalReadbackJoins>& joins,
        Visitor&& visitor,
        std::uint32_t& count,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal = {}) noexcept
    {
        count = 0u;
        if (budget.contractVersion != 4u && budget.contractVersion != 5u)
            return joins ? "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-PROFILE" : nullptr;
        if (!joins || !joins->hasQuotaBoundSharedOwner(joins))
            return "RHI-WEBGPU-READBACK-JOIN-OWNER";
        if (const auto* id = seal ? joins->validateRetained(seal) : joins->validate())
            return id;
        if (joins->projectedCopyCount() > UINT32_MAX)
            return "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-COPY-CAPACITY";
        if (!core::SharedOwnerProjection::sameOwner(joins->originalRoot(), closure.originalRoot()))
            return "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-ROOT";
        if (const auto* id = rhi::OriginalGeneralOutputPairingContract::validatePair(
                budget.generalOutput, joins->originalGeneralOutputIntent()))
            return id;
        const bool attachmentReadback = budget.generalOutput.contractVersion == 3u;
        if (attachmentReadback != closure.hasAttachmentReadbackProfile())
            return "RHI-WEBGPU-CANONICAL-LOWERING-ATTACHMENT-READBACK-PROFILE";
        std::size_t ordinal = 0u;
        const auto* diagnostic = visitInputs(
            closure, budget, [&](const Stream::Operation& operation) noexcept -> const char* {
                if (operation.family != Stream::Family::TextureReadback)
                    return nullptr;
                if (ordinal >= joins->entries_.values().size()
                    || joins->entries_.values()[ordinal].commandIndex != operation.commandIndex
                    || operation.firstElement != count)
                    return "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-ORDINAL";
                const auto& join = joins->entries_.values()[ordinal];
                const auto& native = joins->native_.values()[ordinal];
                const auto* source = find(closure, native[0].id);
                const auto* destination = find(closure, native[1].id);
                if (!source || !destination || source == destination
                    || !source->backing().sameOwnersAndLayout(native[0])
                    || !destination->backing().sameOwnersAndLayout(native[1]))
                    return "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-BINDING";
                const auto& layout =
                    join.source->originalSource()->originalRequest()->originalLayout();
                if (operation.elementCount != layout->mipCopies().size())
                    return "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-EXPANSION";
                for (std::size_t mip = 0u; mip < layout->mipCopies().size(); ++mip) {
                    if (count >= budget.readback.maxNativeCopyCommands
                        || count >= joins->copies_.values().size())
                        return "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-COPY-CAPACITY";
                    const auto& frozen = joins->copies_.values()[count];
                    if (frozen.commandIndex != operation.commandIndex)
                        return "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-ORDINAL";
                    if (const auto* id =
                            rhi::semantics::TextureReadbackCopyLowering::validateOriginal(
                                rhi::Backend::WebGPU,
                                1u,
                                true,
                                layout,
                                mip,
                                layout->mipCopies()[mip],
                                frozen.decision))
                        return id;
                    const auto& original = frozen.decision.original;
                    rhi::TextureToBufferCopyDesc desc;
                    desc.sourceTexture = native[0].id;
                    desc.destinationBuffer = native[1].id;
                    desc.textureSubresources = original.range;
                    desc.extent = original.extent;
                    desc.bytesPerRow = original.bytesPerRow;
                    desc.rowsPerImage = original.rowsPerImage;
                    desc.bufferOffsetBytes = original.bufferOffsetBytes;
                    if (const auto* id = attachmentReadback
                            ? WebGpuTransferCopyValidation::inspectTextureBufferLayout(
                                  native[0], native[1], desc)
                            : WebGpuTransferCopyValidation::validate(native[0], native[1], desc))
                        return id;
                    if (const auto* id = visitor(
                            operation.commandIndex, ordinal, source, destination, desc, count))
                        return id;
                    ++count;
                }
                ++ordinal;
                return nullptr;
            });
        if (diagnostic)
            return diagnostic;
        return ordinal == joins->readbackCount() && count == joins->projectedCopyCount()
            ? nullptr
            : "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-COVERAGE";
    }

    static const char* validatePass(
        const Closure& closure, const Budget& budget, const rhi::RenderPassArguments* pass) noexcept
    {
        const auto& desc = pass->desc();
        if (const auto* id =
                rhi::OriginalCommandCopyPassLoweringContract::validatePassShape(desc, budget))
            return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto view = [&](rhi::GpuResourceId id,
                              rhi::TextureFormat format,
                              bool depth) noexcept -> const char* {
            const auto* entry = find(closure, id);
            if (!entry || !entry->object.view)
                return "RHI-WEBGPU-CANONICAL-LOWERING-PASS-BINDING";
            const auto& observation = std::get<WebGpuOrdinaryResourceObservation>(entry->native);
            if (!Pass::validViewBinding(observation, format, depth))
                return "RHI-WEBGPU-CANONICAL-LOWERING-PASS-FORMAT";
            return nullptr;
        };
        for (const auto& color : desc.colorAttachments) {
            if (const auto* id = view(color.target, color.format, false))
                return id;
            if (color.hasResolveTarget)
                if (const auto* id = view(color.resolveTarget, color.format, false))
                    return id;
        }
        if (desc.hasDepthStencilAttachment)
            if (const auto* id = view(
                    desc.depthStencilAttachment.target, desc.depthStencilAttachment.format, true))
                return id;
#else
        return "RHI-WEBGPU-CANONICAL-LOWERING-NATIVE-UNAVAILABLE";
#endif
        return nullptr;
    }

    template <class Visitor>
    static const char* inspectPass(const Closure& closure,
        const Budget& budget,
        Visitor&& visitor,
        std::uint32_t& count) noexcept
    {
        count = 0u;
        using Return = rhi::OriginalCompletedDepthWriteReturnContract;
        const bool depthReturn = Return::selected(budget.depthWriteReturn);
        if (depthReturn) {
            if (const auto* id =
                    rhi::OriginalCommandCopyPassLoweringContract::validateIntent(budget))
                return id;
            const auto decision =
                rhi::semantics::OriginalCompletedDepthWriteReturnLowering::lowering(
                    rhi::Backend::WebGPU, budget.depthWriteReturn.contractVersion);
            if (!decision.transitionAvailable)
                return decision.diagnostic;
        }
        std::uint32_t returned = 0u;
        std::uint64_t colors = 0u;
        bool barrier = false;
        const auto& inputs = *closure.originalRoot()->originalGraph()->originalInputs();
        const auto* diagnostic = visitInputs(
            closure, budget, [&](const Stream::Operation& operation) noexcept -> const char* {
                if (operation.kind == rhi::CommandUsageKind::BufferBarrier) {
                    const auto& descriptor =
                        inputs.descriptors()[operation.descriptorIndex].arguments;
                    rhi::ResourceBarrierDesc desc;
                    const auto* fields = std::get_if<rhi::ResourceDependencyArguments>(&descriptor);
                    if ((budget.contractVersion != 3u && budget.contractVersion != 4u
                            && budget.contractVersion != 5u)
                        || !fields || !fields->write(operation.kind, desc))
                        return "RHI-WEBGPU-CANONICAL-LOWERING-BARRIER-INPUT";
                    if (const auto* id =
                            (budget.contractVersion == 4u || budget.contractVersion == 5u)
                            ? arp::rhi::semantics::OriginalCommandOrderedReadbackLowering::
                                  transition(desc, false)
                            : rhi::OriginalCommandOrderedUploadContract::bufferTransition(desc))
                        return id;
                    const auto* buffer = find(closure, desc.resource);
                    if (!buffer || !buffer->object.buffer)
                        return "RHI-WEBGPU-CANONICAL-LOWERING-BARRIER-BINDING";
                    if (buffer->object.buffer->gpuTransferWritesSealed())
                        return "RHI-WEBGPU-CANONICAL-LOWERING-INITIALIZED-STAGING-TRANSITION";
                }
                if (operation.kind == rhi::CommandUsageKind::TextureBarrier) {
                    const auto& descriptor =
                        inputs.descriptors()[operation.descriptorIndex].arguments;
                    rhi::ResourceBarrierDesc desc;
                    const auto* fields = std::get_if<rhi::ResourceDependencyArguments>(&descriptor);
                    if (!fields || !fields->write(operation.kind, desc))
                        return "RHI-WEBGPU-CANONICAL-LOWERING-BARRIER-INPUT";
                    const auto* texture = find(closure, desc.resource);
                    if (!texture || !texture->object.texture)
                        return "RHI-WEBGPU-CANONICAL-LOWERING-BARRIER-BINDING";
                    const auto whole =
                        rhi::defaultTextureSubresourceRange(texture->object.texture->desc());
                    const bool returningDepth =
                        depthReturn && desc.before == rhi::ResourceState::ShaderRead;
                    if (returningDepth && returned == budget.depthWriteReturn.maxTransitions)
                        return "RHI-DEPTH-WRITE-RETURN-CAPACITY";
                    const auto* id = returningDepth
                        ? Return::transition(
                              desc, texture->object.texture->desc(), budget.depthWriteReturn)
                        : rhi::OriginalGeneralOutputPairingContract::selected(budget.generalOutput)
                        ? rhi::semantics::OriginalGeneralOutputReadbackDependencyLowering::
                              transition(rhi::Backend::WebGPU,
                                  budget.generalOutput.contractVersion,
                                  budget.generalOutput.prototypeOptIn,
                                  desc,
                                  budget.generalOutput.contractVersion == 3u
                                      ? rhi::ResourceState::ColorAttachment
                                      : rhi::ResourceState::ShaderWrite,
                                  whole)
                                  .diagnostic
                        : (budget.contractVersion == 4u || budget.contractVersion == 5u)
                        ? arp::rhi::semantics::OriginalCommandOrderedReadbackLowering::transition(
                              desc, true, whole)
                        : budget.contractVersion == 1u
                        ? rhi::OriginalCommandCopyPassLoweringContract::attachmentTransition(
                              desc, whole)
                        : rhi::OriginalCommandOrderedCopyAttachmentContract::transition(
                              desc, whole);
                    if (id)
                        return id;
                    if (returningDepth)
                        ++returned;
                    barrier = true;
                } else if (operation.kind == rhi::CommandUsageKind::BeginRenderPass) {
                    const auto& descriptor =
                        inputs.descriptors()[operation.descriptorIndex].arguments;
                    const auto* fields =
                        std::get_if<std::shared_ptr<const rhi::RenderPassArguments>>(&descriptor);
                    if (!fields || !*fields)
                        return "RHI-WEBGPU-CANONICAL-LOWERING-PASS-INPUT";
                    if (const auto* id = validatePass(closure, budget, fields->get()))
                        return id;
                    const auto added = (*fields)->desc().colorAttachments.size();
                    if (added
                        && !core::containsUnsignedRange<std::uint64_t>(
                            0u, budget.maxColorAttachments, colors, added))
                        return "RHI-ORIGINAL-COPY-PASS-LOWERING-COLOR-CAPACITY";
                    colors += added;
                    if (const auto* id = visitor(fields->get(), count))
                        return id;
                    ++count;
                }
                return nullptr;
            });
        if (diagnostic)
            return diagnostic;
        if (depthReturn && !returned)
            return "RHI-DEPTH-WRITE-RETURN-MISSING";
        return budget.contractVersion == 1u && (!barrier || count != 1u)
            ? "RHI-WEBGPU-CANONICAL-LOWERING-PASS-INPUT"
            : nullptr;
    }

    const char* populate(const Budget& budget)
    {
        std::uint32_t count = 0u;
        if (const auto *id = inspectCopies(
                *closure,
                budget,
                [&](const auto* s, const auto* d, const auto& desc, std::uint32_t) noexcept
                    -> const char* {
                    return copies.append(Operation(s, d, desc))
                        ? nullptr
                        : "RHI-WEBGPU-CANONICAL-LOWERING-COPY-CAPACITY";
                },
                count))
            return id;
        if (const auto *id = inspectUploads(
                *closure,
                budget,
                joins,
                [&](std::size_t command,
                    std::size_t join,
                    const auto* s,
                    const auto* d,
                    const auto& desc,
                    std::uint32_t) noexcept -> const char* {
                    return uploads.append(UploadOperation(command, join, s, d, desc))
                        ? nullptr
                        : "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-COPY-CAPACITY";
                },
                count))
            return id;
        if (const auto *id = inspectReadbacks(
                *closure,
                budget,
                readbackJoins,
                [&](std::size_t command,
                    std::size_t join,
                    const auto* s,
                    const auto* d,
                    const auto& desc,
                    std::uint32_t) noexcept -> const char* {
                    return readbacks.append(ReadbackOperation(command, join, s, d, desc))
                        ? nullptr
                        : "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-COPY-CAPACITY";
                },
                count))
            return id;
        std::size_t readbackFirst = 0u, readbackJoin = 0u;
        std::size_t passIndex = 0u, uploadFirst = 0u, joinIndex = 0u;
        if (const auto* id = visitInputs(
                *closure, budget, [&](const Stream::Operation& operation) noexcept -> const char* {
                    OrderedOperation ordered{operation, SIZE_MAX};
                    if (operation.kind == rhi::CommandUsageKind::BeginRenderPass)
                        ordered.passIndex = passIndex++;
                    if (operation.family == Stream::Family::BufferUpload
                        || operation.family == Stream::Family::TextureUpload) {
                        ordered.uploadFirst = uploadFirst;
                        ordered.uploadCount = static_cast<std::size_t>(operation.elementCount);
                        ordered.joinIndex = joinIndex++;
                        uploadFirst += ordered.uploadCount;
                    }
                    if (operation.family == Stream::Family::TextureReadback) {
                        ordered.readbackFirst = readbackFirst;
                        ordered.readbackCount = static_cast<std::size_t>(operation.elementCount);
                        ordered.readbackJoin = readbackJoin++;
                        readbackFirst += ordered.readbackCount;
                    }
                    return operations.append(ordered)
                        ? nullptr
                        : "RHI-WEBGPU-CANONICAL-LOWERING-OPERATION-CAPACITY";
                }))
            return id;
        const auto& inputs = *closure->originalRoot()->originalGraph()->originalInputs();
        for (const auto& operation : operations.values())
            if (operation.original.kind == rhi::CommandUsageKind::BeginRenderPass) {
                const auto& original = std::get<std::shared_ptr<const rhi::RenderPassArguments>>(
                    inputs.descriptors()[operation.original.descriptorIndex].arguments);
                if (!passes.append(Pass(original.get(), 0u)))
                    return "RHI-WEBGPU-CANONICAL-LOWERING-PASS-CAPACITY";
            }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const char* failure = nullptr;
        // Finalize native pointers only after all Pass values occupy their final
        // bounded addresses; no pass is moved/appended during record or submit.
        passes.visitMutable([&](Pass& pass) {
            if (!failure)
                failure = pass.finalize([&](rhi::GpuResourceId id) noexcept {
                    return std::get<WebGpuOrdinaryResourceObservation>(find(*closure, id)->native)
                        .view()
                        ->view;
                });
        });
        if (failure)
            return failure;
#endif
        return nullptr;
    }

    const char* validate(const Budget& budget,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal = {}) const noexcept
    {
        std::uint32_t count = 0u;
        if (const auto *id = inspectCopies(
                *closure,
                budget,
                [&](const auto* s, const auto* d, const auto& desc, std::uint32_t i) noexcept
                    -> const char* {
                    return i < copies.values().size()
                            && copies.values()[i].matches(
                                s, d, rhi::TransferCopyArguments::captureScalarFields(desc))
                        ? nullptr
                        : "RHI-WEBGPU-CANONICAL-LOWERING-COPY-CHANGED";
                },
                count))
            return id;
        if (count != copies.values().size())
            return "RHI-WEBGPU-CANONICAL-LOWERING-COPY-CHANGED";
        if (const auto *id = inspectUploads(
                *closure,
                budget,
                joins,
                [&](std::size_t command,
                    std::size_t join,
                    const auto* s,
                    const auto* d,
                    const auto& desc,
                    std::uint32_t i) noexcept -> const char* {
                    return i < uploads.values().size() && uploads.values()[i].command == command
                            && uploads.values()[i].join == join
                            && uploads.values()[i].copy.matches(
                                s, d, rhi::TransferCopyArguments::captureScalarFields(desc))
                        ? nullptr
                        : "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-CHANGED";
                },
                count))
            return id;
        if (count != uploads.values().size())
            return "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-CHANGED";
        if (const auto *id = inspectReadbacks(
                *closure,
                budget,
                readbackJoins,
                [&](std::size_t command,
                    std::size_t join,
                    const auto* s,
                    const auto* d,
                    const auto& desc,
                    std::uint32_t i) noexcept -> const char* {
                    return i < readbacks.values().size() && readbacks.values()[i].command == command
                            && readbacks.values()[i].join == join
                            && readbacks.values()[i].copy.matches(
                                s, d, rhi::TransferCopyArguments::captureScalarFields(desc))
                        ? nullptr
                        : "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-CHANGED";
                },
                count,
                seal))
            return id;
        if (count != readbacks.values().size())
            return "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-CHANGED";
        if (const auto *id = inspectPass(
                *closure,
                budget,
                [&](const rhi::RenderPassArguments* pass, std::uint32_t i) noexcept -> const char* {
                    return i < passes.values().size() && passes.values()[i].pass == pass
                        ? nullptr
                        : "RHI-WEBGPU-CANONICAL-LOWERING-PASS-CHANGED";
                },
                count))
            return id;
        if (count != passes.values().size())
            return "RHI-WEBGPU-CANONICAL-LOWERING-PASS-CHANGED";
        std::size_t command = 0u, pass = 0u, uploadFirst = 0u, joinIndex = 0u, readbackFirst = 0u,
                    readbackJoin = 0u;
        if (const auto* id = visitInputs(
                *closure, budget, [&](const Stream::Operation& operation) noexcept -> const char* {
                    if (command >= operations.values().size())
                        return "RHI-WEBGPU-CANONICAL-LOWERING-OPERATION-CHANGED";
                    const auto& stored = operations.values()[command++];
                    const auto index = operation.kind == rhi::CommandUsageKind::BeginRenderPass
                        ? pass++
                        : SIZE_MAX;
                    const bool upload = operation.family == Stream::Family::BufferUpload
                        || operation.family == Stream::Family::TextureUpload;
                    const auto first = upload ? uploadFirst : SIZE_MAX,
                               join = upload ? joinIndex++ : SIZE_MAX;
                    const auto added =
                        upload ? static_cast<std::size_t>(operation.elementCount) : 0u;
                    uploadFirst += added;
                    const bool readback = operation.family == Stream::Family::TextureReadback;
                    const auto readFirst = readback ? readbackFirst : SIZE_MAX,
                               readJoin = readback ? readbackJoin++ : SIZE_MAX;
                    const auto readCount =
                        readback ? static_cast<std::size_t>(operation.elementCount) : 0u;
                    readbackFirst += readCount;
                    return stored.original.matches(operation) && stored.passIndex == index
                            && stored.uploadFirst == first && stored.uploadCount == added
                            && stored.joinIndex == join && stored.readbackFirst == readFirst
                            && stored.readbackCount == readCount && stored.readbackJoin == readJoin
                        ? nullptr
                        : "RHI-WEBGPU-CANONICAL-LOWERING-OPERATION-CHANGED";
                }))
            return id;
        return command == operations.values().size()
            ? nullptr
            : "RHI-WEBGPU-CANONICAL-LOWERING-OPERATION-CHANGED";
    }

    const Closure* const closure;
    core::BoundedSequence<Operation> copies;
    core::BoundedSequence<Pass> passes;
    core::BoundedSequence<OrderedOperation> operations;
    const std::shared_ptr<const WebGpuCanonicalUploadJoins> joins;
    core::BoundedSequence<UploadOperation> uploads;
    const std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbackJoins;
    core::BoundedSequence<ReadbackOperation> readbacks;
};

template <class Command> struct WebGpuTypedLossObserverStorage {};

template <> struct WebGpuTypedLossObserverStorage<WebGpuOriginalTypedColdCommand> {
    std::shared_ptr<const WebGpuOriginalObservedNativeLoss> observedLoss;
    core::runtime::BoundedTerminalObservers<rhi::OriginalDeviceNativeLossTerminal>::Ticket
        observerTicket;
};

template <class Command> struct WebGpuOriginalRootFaultStorage {};

template <> struct WebGpuOriginalRootFaultStorage<WebGpuBindlessRootColdCommand> {
    mutable rhi::BindlessFaultSubmissionLease fault;
    mutable rhi::OriginalBindlessRootFaultCustody additionalFaults;
};

template <class Command>
class WebGpuCanonicalCallbackCustody::RuntimeState final
    : public core::SharedObjectIdentity,
      public WebGpuTypedLossObserverStorage<Command> {
public:
    static constexpr bool typed = std::is_same_v<Command, WebGpuOriginalTypedColdCommand>;
    static constexpr bool bindlessRoot = std::is_same_v<Command, WebGpuBindlessRootColdCommand>;
    enum class Role {
        RecordingScope,
        SubmissionScope,
        QueueCompletion,
        NativeLoss,
        MapCompletion,
        MapScope
    };

    class Graph final : public WebGpuOriginalRootFaultStorage<Command> {
    public:
        explicit Graph(std::shared_ptr<const Command> original) noexcept
            : command(std::move(original))
        {}

        const std::shared_ptr<const Command> command;
    };

    using Pool = core::runtime::CompletionRetentionPool<Graph>;

    class Slot final {
    public:
        Slot(Role originalRole,
            std::size_t originalIndex,
            typename Pool::Ticket originalTicket) noexcept
            : role(originalRole), index(originalIndex), ticket(originalTicket)
        {}

        ~Slot()
        {
            releaseWebGpuNativeDeviceLossRegistration(nativeState);
            WebGpuNativeCompletionReceipt::release(nativeReceipt);
        }

        const Role role;
        const std::size_t index;
        const typename Pool::Ticket ticket;
        core::runtime::ExternalCallbackGate gate;
        std::shared_ptr<RuntimeState> owner;
        std::uintptr_t nativeState = 0u, nativeReceipt = 0u;
        WebGpuNativeBufferMapReceipt map;
        bool mapIssued = false, copied = false, scopeIssued = false, scopeSucceeded = false;
    };

    static constexpr std::size_t recordingFirst = WebGpuCanonicalCallbackLayout::recordingFirst;
    static constexpr std::size_t submissionFirst = WebGpuCanonicalCallbackLayout::submissionFirst;
    static constexpr std::size_t queueIndex = WebGpuCanonicalCallbackLayout::queueIndex;
    static constexpr std::size_t lossIndex = WebGpuCanonicalCallbackLayout::lossIndex;
    static constexpr std::size_t slotCount = WebGpuCanonicalCallbackLayout::slotCount;
    static_assert(WebGpuOrdinaryRecording::scopeCount == submissionFirst - recordingFirst,
        "Canonical recording evidence must match its original scope slots");
    static_assert(WebGpuOrdinarySubmission::callbackCount == lossIndex - submissionFirst,
        "Canonical submission evidence must match original scope and queue slots");

    RuntimeState(std::shared_ptr<core::CpuMemoryQuota> submission,
        std::shared_ptr<core::CpuMemoryQuota> callback,
        std::uint32_t owners,
        std::uint32_t callbacks = slotCount,
        std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> results = {},
        std::optional<PublicationIntent> publication = std::nullopt)
        : submissionQuota(std::move(submission)), callbackQuota(std::move(callback)),
          admittedOwners(owners), pool(1u), results(std::move(results)), publication(publication)
    {
        slots.resize(callbacks);
    }

    bool overlapsCpuOwner(const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (!quota)
            return false;
        if (core::SharedOwnerProjection::overlapsOwner(quota, submissionQuota)
            || core::SharedOwnerProjection::overlapsOwner(quota, callbackQuota)
            || core::cpuAllocationOverlapsQuotaOwner(this, quota))
            return true;
        if (results
            && (core::SharedOwnerProjection::overlapsOwner(quota, results->originalResultQuota())
                || core::SharedOwnerProjection::overlapsOwner(
                    quota, results->originalMetadataQuota())))
            return true;
        const auto* graph = pool.get(ticket);
        if (!graph || !graph->command)
            return false;
        const auto& admission = graph->command->originalAdmission();
        const auto& root = admission->originalRoot();
        if (root->overlapsCpuOwner(quota))
            return true;
        if constexpr (typed) {
            const auto& closure =
                admission->originalPayload()->originalDecision()->originalClosure();
            return closure->overlapsNativeOwner(quota)
                || core::SharedOwnerProjection::overlapsOwner(
                    quota, closure->originalStream()->originalMetadataQuota());
        } else if constexpr (bindlessRoot) {
            return core::SharedOwnerProjection::overlapsOwner(
                       quota, admission->originalClosure()->originalMetadataQuota())
                || core::SharedOwnerProjection::overlapsOwner(quota,
                    admission->originalReadbackBinding()
                        ->originalProgram()
                        ->originalMetadataQuota());
        } else {
            if (admission->originalClosure()->overlapsNativeOwner(quota))
                return true;
            if (const auto& upload = admission->originalUploadJoins();
                upload && upload->overlapsCpuOwner(quota))
                return true;
            if (const auto& readback = admission->originalReadbackJoins();
                readback && readback->overlapsCpuOwner(quota))
                return true;
            return false;
        }
    }

    static Role roleAt(std::size_t index) noexcept
    {
        return index < submissionFirst ? Role::RecordingScope
            : index < queueIndex       ? Role::SubmissionScope
            : index == queueIndex      ? Role::QueueCompletion
            : index == lossIndex       ? Role::NativeLoss
            : (index - slotCount) % rhi::OriginalCommandReadbackMapContract::participantsPerMap
                == 0u
            ? Role::MapCompletion
            : Role::MapScope;
    }

    template <class Joins, class Visitor>
    const char* visitOriginalMapBindings(const Joins& joins, Visitor&& visitor) const noexcept
    {
        if (!results)
            return nullptr;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        (void)joins;
        (void)visitor;
        return "RHI-WEBGPU-CANONICAL-MAP-RECEIPT-UNAVAILABLE";
#else
        if (slots.size()
            != slotCount
                + results->snapshotCount()
                    * rhi::OriginalCommandReadbackMapContract::participantsPerMap)
            return "RHI-WEBGPU-CANONICAL-MAP-SLOT-COUNT";
        if constexpr (bindlessRoot) {
            if (!core::SharedOwnerProjection::sameOwner(results->originalBufferAdmission(), joins))
                return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-RESULT-ADMISSION";
            for (std::size_t i = 0u; i < results->buffers_.values().size(); ++i) {
                const auto& entry = results->buffers_.values()[i];
                const auto first =
                    slotCount + i * rhi::OriginalCommandReadbackMapContract::participantsPerMap;
                if (const auto* id = visitor(first, entry.handle, entry.mapRange))
                    return id;
            }
            return nullptr;
        } else {
            for (std::size_t request = 0u; request < results->snapshots().size(); ++request) {
                const auto& snapshot = results->snapshots()[request];
                const auto* entry = joins.entries_.findIf([&](const auto& e) {
                    return core::SharedOwnerProjection::sameOwner(
                        e.source->originalSource()->originalRequest(), snapshot->originalRequest());
                });
                if (!entry)
                    return "RHI-WEBGPU-CANONICAL-MAP-REQUEST-JOIN";
                const auto ordinal =
                    static_cast<std::size_t>(entry - joins.entries_.values().data());
                const auto first = slotCount
                    + request * rhi::OriginalCommandReadbackMapContract::participantsPerMap;
                const auto& destination = joins.native_.values()[ordinal][1].buffer;
                if (!destination)
                    return "RHI-WEBGPU-CANONICAL-MAP-DESTINATION";
                const WebGpuNativeBufferMapReceipt::Range range{
                    snapshot->byteCapacity(), 0u, snapshot->byteCapacity()};
                if (const auto* id = visitor(first, destination->buffer, range))
                    return id;
            }
            return nullptr;
        }
#endif
    }

    template <class Joins>
    const char* prepareOriginalMapReceipts(const Joins& joins, void* device, void* queue) noexcept
    {
        return visitOriginalMapBindings(joins,
            [&](std::size_t first,
                void* buffer,
                const WebGpuNativeBufferMapReceipt::Range& range) noexcept -> const char* {
                auto& map = *slots[first];
                if (const auto* id =
                        map.map.prepareColdRange(buffer, range, &map, &RuntimeState::consumeMap))
                    return id;
                for (std::size_t i = first + 1u;
                    i < first + rhi::OriginalCommandReadbackMapContract::participantsPerMap;
                    ++i) {
                    auto receipt = WebGpuNativeCompletionReceipt::prepare(device,
                        queue,
                        WebGpuNativeCompletionReceipt::Kind::Scope,
                        slots[i].get(),
                        &RuntimeState::consumeMapScope);
                    if (!receipt)
                        return "RHI-WEBGPU-CANONICAL-MAP-SCOPE-PREPARATION";
                    slots[i]->nativeReceipt = receipt.release();
                }
                return nullptr;
            });
    }

    template <class Joins>
    const char* validateOriginalMapReceipts(
        const Joins& joins, void* device, void* queue) const noexcept
    {
        return visitOriginalMapBindings(joins,
            [&](std::size_t first,
                void* buffer,
                const WebGpuNativeBufferMapReceipt::Range& range) noexcept -> const char* {
                const auto& map = *slots[first];
                if (const auto* id = map.map.validateCold())
                    return id;
                if (const auto* id = map.map.validateRetainedRange(buffer, range))
                    return id;
                for (std::size_t i = first + 1u;
                    i < first + rhi::OriginalCommandReadbackMapContract::participantsPerMap;
                    ++i)
                    if (!WebGpuNativeCompletionReceipt::matchesPrepared(slots[i]->nativeReceipt,
                            device,
                            queue,
                            WebGpuNativeCompletionReceipt::Kind::Scope,
                            slots[i].get(),
                            &RuntimeState::consumeMapScope))
                        return "RHI-WEBGPU-CANONICAL-MAP-SCOPE-PREPARATION";
                return nullptr;
            });
    }

    template <class Joins>
    const char* validateRetainedMapReceipts(
        const Joins& joins, void* device, void* queue) const noexcept
    {
        return visitOriginalMapBindings(joins,
            [&](std::size_t first,
                void* buffer,
                const WebGpuNativeBufferMapReceipt::Range& range) noexcept -> const char* {
                if (const auto* id = slots[first]->map.validateRetainedRange(buffer, range))
                    return id;
                for (std::size_t i = first + 1u;
                    i < first + rhi::OriginalCommandReadbackMapContract::participantsPerMap;
                    ++i) {
                    const auto& slot = *slots[i];
                    const bool prepared =
                        WebGpuNativeCompletionReceipt::matchesPrepared(slot.nativeReceipt,
                            device,
                            queue,
                            WebGpuNativeCompletionReceipt::Kind::Scope,
                            slots[i].get(),
                            &RuntimeState::consumeMapScope);
                    const bool issued =
                        WebGpuNativeCompletionReceipt::matchesIssued(slot.nativeReceipt,
                            device,
                            queue,
                            WebGpuNativeCompletionReceipt::Kind::Scope,
                            slots[i].get(),
                            &RuntimeState::consumeMapScope);
                    if (slot.scopeIssued ? !(issued || (mapIssuanceActive && prepared)) : !prepared)
                        return "RHI-WEBGPU-CANONICAL-MAP-SCOPE-PREPARATION";
                }
                return nullptr;
            });
    }

    bool foreignCallbacksDrained() const noexcept
    {
        using Phase = core::runtime::ExternalCallbackGate::Phase;
        // Detached is meaningful only after the native adapter has proven
        // suppression. No loss handler detaches scope/queue userdata itself.
        for (const auto& slot : slots)
            if (slot
                && (slot->gate.phase() == Phase::Armed || slot->gate.phase() == Phase::Delivering))
                return false;
        return true;
    }

    bool foreignCallbacksDrainedExceptLoss() const noexcept
    {
        using Phase = core::runtime::ExternalCallbackGate::Phase;
        for (std::size_t i = 0u; i < slots.size(); ++i) {
            if (i == lossIndex)
                continue;
            const auto& slot = slots[i];
            if (!slot || slot->owner || slot->gate.phase() == Phase::Armed
                || slot->gate.phase() == Phase::Delivering)
                return false;
        }
        return true;
    }

    bool recordingCallbacksDrained() const noexcept
    {
        using Phase = core::runtime::ExternalCallbackGate::Phase;
        for (std::size_t i = recordingFirst; i < submissionFirst; ++i)
            if (!slots[i] || slots[i]->owner || slots[i]->gate.phase() != Phase::Delivered)
                return false;
        return true;
    }

    bool detachLoss() noexcept
    {
        auto& loss = *slots[lossIndex];
        if (loss.gate.phase() == core::runtime::ExternalCallbackGate::Phase::Delivered)
            return true;
        // Completion may already have proved suppression before a later
        // commit/publication failure enters terminal drain. Retire that same
        // detached registration without requesting suppression a second time.
        if (loss.gate.phase() == core::runtime::ExternalCallbackGate::Phase::Detached) {
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
            ++postSuppressionProbe.detachedDrainRevisits;
#endif
            if constexpr (typed)
                return !loss.owner && !this->observerTicket;
            else
                return !loss.owner && !loss.nativeState;
        }
        if constexpr (typed) {
            const auto owner = loss.owner;
            if (!this->observedLoss || !this->observerTicket)
                return false;
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
            ++postSuppressionProbe.suppressionAttempts;
#endif
            const auto status = this->observedLoss->observers_.suppress(
                this->observerTicket, [&](const void* userdata) noexcept {
                    return userdata == &loss && loss.gate.detach();
                });
            if (status
                != core::runtime::BoundedTerminalObservers<
                    rhi::OriginalDeviceNativeLossTerminal>::Status::Complete)
                return false;
            this->observerTicket = {};
            loss.owner.reset();
            this->observedLoss->finishDeferredTerminal();
            return true;
        } else {
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
            ++postSuppressionProbe.suppressionAttempts;
#endif
            if (!loss.nativeState || !detachWebGpuNativeDeviceLossRegistration(loss.nativeState))
                return false;
            loss.gate.detach();
            releaseWebGpuNativeDeviceLossRegistration(loss.nativeState);
            loss.nativeState = 0u;
            loss.owner.reset();
            return true;
        }
    }

    void releaseTerminalIfDrained() noexcept
    {
        if (mapIssuanceActive || mapDrainActive)
            return;
        if (results && pool.retainedInvalidated(ticket)) {
            drainTerminalMaps();
            const auto decision = mapDrainDecision();
            if (decision.action
                != rhi::OriginalCommandReadbackMapDrainContract::Action::RetireTerminal)
                return;
            if (mapInvocationUncertain
                && slots[lossIndex]->gate.phase()
                    != core::runtime::ExternalCallbackGate::Phase::Delivered)
                return;
        }
        if (!pool.retainedInvalidated(ticket) || !foreignCallbacksDrainedExceptLoss())
            return;
        const bool lost =
            slots[lossIndex]->gate.phase() == core::runtime::ExternalCallbackGate::Phase::Delivered;
        // Suppressed queue userdata is never GPU lifetime evidence.
        if (maySubmit && !queueNativeReceiptReceived && !lost)
            return;
        if (recording
            && recording->status_ == WebGpuOrdinaryRecording::Status::NativeInvocationUncertain
            && !lost)
            return;
        if (!lost && !detachLoss())
            return;
        if (!foreignCallbacksDrained())
            return;
        submission.reset();
        recording.reset();
        lowering.reset();
        pool.releaseInvalidated(ticket);
    }

    rhi::OriginalCommandReadbackMapDrainContract::Facts mapDrainFacts() const noexcept
    {
        using Contract = rhi::OriginalCommandReadbackMapDrainContract;
        Contract::Facts facts;
        facts.contractVersion = 1u;
        facts.prototypeOptIn = true;
        for (std::size_t i = slotCount; i < slots.size(); ++i) {
            const auto& slot = *slots[i];
            if (slot.role == Role::MapCompletion && slot.mapIssued) {
                ++facts.mapsIssued;
                if (slot.map.phase() == WebGpuNativeBufferMapReceipt::Phase::Delivered)
                    ++facts.mapCallbacks;
                if (slot.copied)
                    ++facts.copiedMaps;
                if (slot.map.nativeUnmapProven())
                    ++facts.safeUnmaps;
            } else if (slot.role == Role::MapScope && slot.scopeIssued) {
                using Phase = core::runtime::ExternalCallbackGate::Phase;
                if (slot.gate.phase() == Phase::Delivered || slot.gate.phase() == Phase::Detached)
                    ++facts.scopeCallbacksDrained;
                if (slot.scopeSucceeded)
                    ++facts.successfulScopes;
            }
        }
        facts.maySubmit = maySubmit;
        facts.submissionProven = submissionProven;
        facts.queueLifetimeProven = queueNativeReceiptReceived;
        facts.originalForeignCallbacksDrained = foreignCallbacksDrainedExceptLoss();
        facts.cancellation = cancellationRequested;
        facts.nativeError = terminalDiagnostic != nullptr;
        facts.invocationUncertain = mapInvocationUncertain;
        facts.nativeLoss =
            slots[lossIndex]->gate.phase() == core::runtime::ExternalCallbackGate::Phase::Delivered;
        return facts;
    }

    rhi::OriginalCommandReadbackMapDrainContract::Decision mapDrainDecision() const noexcept
    {
        return rhi::OriginalCommandReadbackMapDrainContract::inspect(
            results->readbackMapRequirements(), mapDrainFacts());
    }

    const char* validateRetainedResults(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal) const noexcept
    {
        return WebGpuCanonicalReadbackResultStorage::validateOriginalRetained(results, seal);
    }

    const char* publishCommittedResults(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal) const noexcept
    {
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
        using Probe = arp::test::WebGpuPostSuppressionFailureProbeFixture;
        if (postSuppressionProbe.failure == Probe::Failure::Publication) {
            assert(publication && results && postSuppressionProbe.suppressionAttempts == 1u
                && slots[lossIndex]->gate.phase()
                    == core::runtime::ExternalCallbackGate::Phase::Detached
                && !slots[lossIndex]->owner && queueNativeReceiptReceived
                && foreignCallbacksDrainedExceptLoss());
            postSuppressionProbe.injectedAfterSuppression = true;
            postSuppressionProbe.publicationAttempted = true;
            postSuppressionProbe.committedBeforePublicationFailure =
                results->originalRoot()->status()
                == rhi::OriginalCommandStateTransaction::Status::Committed;
            assert(postSuppressionProbe.committedBeforePublicationFailure
                && results->originalRoot()->journalEmpty());
            auto forbidden = *publication;
            forbidden.prototypeOptIn = false;
            return WebGpuCanonicalReadbackResultStorage::publishOriginalCommitted(
                results, seal, mapDrainFacts(), forbidden);
        }
#endif
        return publication ? WebGpuCanonicalReadbackResultStorage::publishOriginalCommitted(
                                 results, seal, mapDrainFacts(), *publication)
                           : nullptr;
    }

    void drainTerminalMaps() noexcept
    {
        // Unmap/destroy may synchronously deliver the C callback. Keep the
        // original State owner at the caller and prevent recursive retirement.
        mapDrainActive = true;
        const bool lost =
            slots[lossIndex]->gate.phase() == core::runtime::ExternalCallbackGate::Phase::Delivered;
        for (std::size_t i = slotCount; i < slots.size();
            i += rhi::OriginalCommandReadbackMapContract::participantsPerMap) {
            auto& slot = *slots[i];
            if (!slot.mapIssued || slot.map.nativeUnmapProven())
                continue;
            slot.map.abortAndDrain();
            if (lost && !slot.map.nativeUnmapProven())
                slot.map.destroyAndDrainAfterNativeLoss();
        }
        mapDrainActive = false;
    }

    void advanceMaps() noexcept
    {
        if (mapIssuanceActive || mapDrainActive)
            return;
        if (terminalDiagnostic) {
            releaseTerminalIfDrained();
            return;
        }
        if (!results || !submissionProven || !foreignCallbacksDrainedExceptLoss())
            return;
        const auto* graph = pool.get(ticket);
        if (!graph) {
            quarantine("RHI-WEBGPU-CANONICAL-MAP-TICKET");
            return;
        }
        const auto seal =
            graph->command->originalAdmission()->originalReservation()->originalSeal();
        if (const auto* id =
                WebGpuCanonicalReadbackResultStorage::validateOriginalRetained(results, seal)) {
            quarantine(id);
            return;
        }
        if (nextMap == results->snapshotCount()) {
            const auto issuer = submission;
            if (issuer)
                issuer->finishCanonicalSubmission();
            return;
        }
        const auto first =
            slotCount + nextMap * rhi::OriginalCommandReadbackMapContract::participantsPerMap;
        auto& map = *slots[first];
        if (const auto* id = map.map.validateCold()) {
            quarantine(id);
            return;
        }
        for (std::size_t i = first + 1u;
            i < first + rhi::OriginalCommandReadbackMapContract::participantsPerMap;
            ++i)
            if (!WebGpuNativeCompletionReceipt::prepared(slots[i]->nativeReceipt)) {
                quarantine("RHI-WEBGPU-CANONICAL-MAP-SCOPE-UNPREPARED");
                return;
            }
        // No hot owned object or callback adapter is created. The deliberate
        // loss cycle already owns this exact State before native issuance.
        mapIssuanceActive = true;
        const auto owner = slots[lossIndex]->owner;
        for (std::size_t i = first;
            i < first + rhi::OriginalCommandReadbackMapContract::participantsPerMap;
            ++i) {
            slots[i]->owner = owner;
            slots[i]->gate.arm();
        }
        const bool pushed = WebGpuNativeCompletionReceipt::pushScopes(graph->command->device_);
        if (pushed) {
            map.mapIssued = true;
            ++nextMap;
            if (const auto* id = map.map.start()) {
                mapInvocationUncertain = true;
                quarantine(id);
            }
            for (std::size_t i = first + 1u;
                i < first + rhi::OriginalCommandReadbackMapContract::participantsPerMap;
                ++i) {
                auto& scope = *slots[i];
                scope.scopeIssued = true;
                if (!WebGpuNativeCompletionReceipt::bind(scope.nativeReceipt)) {
                    mapInvocationUncertain = true;
                    scope.gate.detach();
                    scope.owner.reset();
                    quarantine("RHI-WEBGPU-CANONICAL-MAP-SCOPE-REGISTRATION-FAILED");
                }
            }
        } else {
            mapInvocationUncertain = true;
            for (std::size_t i = first;
                i < first + rhi::OriginalCommandReadbackMapContract::participantsPerMap;
                ++i) {
                slots[i]->gate.detach();
                slots[i]->owner.reset();
            }
            quarantine("RHI-WEBGPU-CANONICAL-MAP-SCOPE-PUSH-UNCERTAIN");
        }
        mapIssuanceActive = false;
        if (terminalDiagnostic)
            releaseTerminalIfDrained();
        else
            advanceMaps();
    }

    void quarantine(const char* reason) noexcept
    {
        if (!terminalDiagnostic)
            terminalDiagnostic = reason;
        pool.invalidateAndRetain([](Graph& graph) noexcept {
            WebGpuCanonicalCallbackCustody::invalidateOriginalOwnership(*graph.command);
        });
        releaseTerminalIfDrained();
    }

    static void consumeOriginalPhysicalLoss(
        const void* userdata, rhi::OriginalDeviceNativeLossTerminal terminal) noexcept
    {
        static_assert(typed, "Local physical receiver is only available to typed custody");
        const auto* borrowed = static_cast<const Slot*>(userdata);
        const auto owner = borrowed ? borrowed->owner : nullptr;
        if (!owner || owner->slots[lossIndex].get() != borrowed)
            return;
        if (terminal != rhi::OriginalDeviceNativeLossTerminal::NativeLost) {
            owner->quarantine("RHI-WEBGPU-TYPED-CALLBACK-PHYSICAL-LOSS-REQUIRED");
            return;
        }
        consumeNativeLoss(owner->slots[lossIndex].get());
    }

    static void consumeNativeLoss(void* userdata) noexcept
    {
        auto* slot = static_cast<Slot*>(userdata);
        if (!slot || !slot->gate.beginDelivery())
            return;
        // The existing bridge suppresses userdata before delivery. Keep the
        // exact state alive while breaking the deliberate native cycle.
        auto owner = slot->owner;
        if (owner && owner->slots[lossIndex].get() == slot) {
            using Phase = core::runtime::ExternalCallbackGate::Phase;
            for (std::size_t i = recordingFirst; i < submissionFirst; ++i)
                if (owner->slots[i]->gate.phase() == Phase::Armed
                    || owner->slots[i]->gate.phase() == Phase::Delivering)
                    ++owner->recordingScopesAtLoss;
            for (std::size_t i = submissionFirst; i < lossIndex; ++i)
                if (owner->slots[i]->gate.phase() == Phase::Armed
                    || owner->slots[i]->gate.phase() == Phase::Delivering)
                    ++owner->submissionSlotsAtLoss;
            owner->terminalDiagnostic = "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-LOST";
            owner->pool.invalidateAndRetain([](Graph& graph) noexcept {
                WebGpuCanonicalCallbackCustody::invalidateOriginalOwnership(*graph.command);
            }); // Close controls now; foreign userdata may still borrow owners.
        }
        slot->gate.finishDelivery();
        releaseWebGpuNativeDeviceLossRegistration(slot->nativeState);
        slot->nativeState = 0u;
        slot->owner.reset();
        if (owner)
            owner->releaseTerminalIfDrained();
    }

    static void consumeMap(void* userdata, WebGpuNativeBufferMapReceipt::Outcome outcome) noexcept
    {
        auto* slot = static_cast<Slot*>(userdata);
        if (!slot || slot->role != Role::MapCompletion || !slot->gate.beginDelivery())
            return;
        const auto owner = slot->owner;
        if (owner && owner->slots[slot->index].get() == slot && slot->mapIssued
            && owner->submissionProven) {
            const auto* graph = owner->pool.get(owner->ticket);
            const char* failure = nullptr;
            if (!owner->terminalDiagnostic
                && outcome == WebGpuNativeBufferMapReceipt::Outcome::Success && graph) {
                failure =
                    WebGpuCanonicalReadbackResultStorage::readOriginalUnpublished(owner->results,
                        graph->command->originalAdmission()->originalReservation()->originalSeal(),
                        (slot->index - slotCount)
                            / rhi::OriginalCommandReadbackMapContract::participantsPerMap,
                        slot->map);
                slot->copied = !failure;
            } else
                failure = "RHI-WEBGPU-CANONICAL-MAP-NATIVE-ERROR";
            if (!slot->map.nativeUnmapProven())
                if (const auto* id = slot->map.abortAndDrain())
                    failure = id;
            if (failure)
                owner->quarantine(failure);
        } else if (owner)
            owner->quarantine("RHI-WEBGPU-CANONICAL-MAP-ORIGINAL-BINDING");
        slot->gate.finishDelivery();
        slot->owner.reset();
        if (owner)
            owner->advanceMaps();
    }

    static void consumeMapScope(
        void* userdata, WebGpuNativeCompletionReceipt::Outcome outcome) noexcept
    {
        auto* slot = static_cast<Slot*>(userdata);
        if (!slot || slot->role != Role::MapScope || !slot->gate.beginDelivery())
            return;
        const auto owner = slot->owner;
        if (owner && owner->slots[slot->index].get() == slot && slot->scopeIssued
            && owner->submissionProven) {
            slot->scopeSucceeded = outcome == WebGpuNativeCompletionReceipt::Outcome::Success;
            if (!slot->scopeSucceeded)
                owner->quarantine("RHI-WEBGPU-CANONICAL-MAP-SCOPE-NATIVE-ERROR");
        } else if (owner)
            owner->quarantine("RHI-WEBGPU-CANONICAL-MAP-SCOPE-ORIGINAL-BINDING");
        slot->gate.finishDelivery();
        slot->owner.reset();
        if (owner)
            owner->advanceMaps();
    }

    const char* cancel(const char* reason = nullptr) noexcept
    {
        auto& loss = *slots[lossIndex];
        using Phase = core::runtime::ExternalCallbackGate::Phase;
        if (loss.gate.phase() == Phase::Delivered || loss.gate.phase() == Phase::Detached)
            return "RHI-WEBGPU-CANONICAL-CALLBACK-RETIRED";
        if (maySubmit) {
            cancellationRequested = true;
            quarantine(reason ? reason : "RHI-WEBGPU-CANONICAL-SUBMISSION-CANCELLED");
            return "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED";
        }
        // Cancellation cannot free userdata still owned by the foreign runtime.
        if (pool.failed())
            return "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED";
        for (std::size_t i = 0u; i < slots.size(); ++i) {
            if (i == lossIndex)
                continue;
            const auto phase = slots[i]->gate.phase();
            if (phase == Phase::Armed || phase == Phase::Delivering) {
                cancellationRequested = true;
                return "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-CALLBACKS-PENDING";
            }
        }
        if (!pool.canCancel(ticket))
            return "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-UNPROVEN";
        if constexpr (typed) {
            if (!detachLoss()) {
                terminalDiagnostic = "RHI-WEBGPU-TYPED-CALLBACK-LOCAL-SUPPRESSION";
                return terminalDiagnostic;
            }
        } else {
            if (loss.nativeState && !detachWebGpuNativeDeviceLossRegistration(loss.nativeState)) {
                terminalDiagnostic = "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-DETACH";
                return terminalDiagnostic; // Bounded graph and native cycle stay live.
            }
            loss.gate.detach();
        }
        if (recording)
            recording->rollbackCanonicalRecording();
        submission.reset();
        recording.reset();
        lowering.reset();
        if (pool.get(ticket) && !pool.cancel(ticket)) {
            terminalDiagnostic = "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-UNPROVEN";
            return terminalDiagnostic;
        }
        pool.invalidate();
        releaseWebGpuNativeDeviceLossRegistration(loss.nativeState);
        loss.nativeState = 0u;
        loss.owner.reset();
        terminalDiagnostic = reason ? reason : "RHI-WEBGPU-CANONICAL-CALLBACK-CANCELLED";
        return nullptr;
    }

    const std::shared_ptr<core::CpuMemoryQuota> submissionQuota, callbackQuota;
    const std::uint32_t admittedOwners;
    Pool pool;
    typename Pool::Ticket ticket;
    core::pmr::Vector<core::UniquePtr<Slot>> slots{&core::defaultCpuMemoryResource()};
    const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> results;
    const std::optional<PublicationIntent> publication;
    core::UniquePtr<WebGpuCanonicalCopyPassLowering::Payload> lowering;
    std::shared_ptr<WebGpuOrdinaryRecording> recording;
    std::shared_ptr<WebGpuOrdinarySubmission> submission;
    bool loweringClaimed = false;
    bool issuersClaimed = false;
    // UINT32_MAX denotes unrecorded. The record entry freezes the independent
    // completion choice; submit cannot opt into a different execution profile.
    std::uint32_t recordedReadbackCompletionVersion = UINT32_MAX;
    std::uint32_t recordedGeneralSubmissionVersion = UINT32_MAX;
    std::uint32_t recordedGeneralOutputVersion = UINT32_MAX;
    bool cancellationRequested = false;
    bool maySubmit = false, queueNativeReceiptReceived = false, submissionCompleted = false;
    bool submissionProven = false, mapIssuanceActive = false, mapDrainActive = false,
         mapInvocationUncertain = false;
    std::size_t nextMap = 0u;
    std::uint32_t recordingScopesAtLoss = 0u, submissionSlotsAtLoss = 0u;
    const char* terminalDiagnostic = nullptr;
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
    mutable arp::test::WebGpuPostSuppressionFailureProbeFixture::Facts postSuppressionProbe;
#endif
};

void WebGpuCanonicalCallbackCustody::invalidateOriginalOwnership(
    const WebGpuBindlessRootColdCommand& command) noexcept
{
    command.originalAdmission()
        ->originalReservation()
        ->originalSeal()
        ->invalidateOriginalOwnership();
}

const char* WebGpuCanonicalCallbackCustody::inspectRootOriginal(
    const std::shared_ptr<const WebGpuBindlessRootColdCommand>& command,
    const rhi::OriginalBindlessRootCallbackCustodyRequest& request) noexcept
{
    using Contract = rhi::OriginalBindlessRootCallbackCustodyContract;
    if (const auto* id = Contract::validateIntent(request))
        return id;
    if (const auto* id = WebGpuBindlessRootColdCommand::validateOriginal(command))
        return id;
    const auto& admission = command->originalAdmission();
    const auto& seal = admission->originalReservation()->originalSeal();
    if (request.maxCpuSubmissionBytes != seal->originalSubmissionQuota()->limitBytes()
        || request.maxCpuCallbackBytes != seal->originalCallbackQuota()->limitBytes())
        return "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-QUOTA";
    const auto lowering = rhi::semantics::OriginalBindlessRootCallbackCustodyLowering::lowering(
        rhi::Backend::WebGPU, request.contractVersion);
    if (!lowering.preparationAvailable || lowering.executionAvailable)
        return lowering.diagnostic;
    return Contract::validateRequirements(command->retainedOwnerCount(),
        admission->requirements().capacity,
        admission->originalClosure()->originalInventory()->originalBudget(),
        seal->originalBudget(),
        request);
}

std::shared_ptr<const WebGpuBindlessRootCallbackCustody>
WebGpuCanonicalCallbackCustody::prepareRootOriginal(
    std::shared_ptr<const WebGpuBindlessRootColdCommand> command,
    const rhi::OriginalBindlessRootCallbackCustodyRequest& request,
    const char*& diagnostic)
{
    if ((diagnostic = inspectRootOriginal(command, request)))
        return {};
    if (command->callbackCustodyClaimed_) {
        diagnostic = "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-DUPLICATE";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-NATIVE-UNAVAILABLE";
    return {};
#else
    using RootState = RuntimeState<WebGpuBindlessRootColdCommand>;
    using Contract = rhi::OriginalBindlessRootCallbackCustodyContract;
    const auto& admission = command->originalAdmission();
    const auto& seal = admission->originalReservation()->originalSeal();
    try {
        core::CpuMemoryQuotaScope submissionScope(seal->originalSubmissionQuota());
        auto state = core::makeShared<RootState>(seal->originalSubmissionQuota(),
            seal->originalCallbackQuota(),
            command->retainedOwnerCount() + Contract::additionalOwners(request),
            request.maxCallbackSlots,
            command->originalResultStorage(),
            (seal->originalBudget().contractVersion == 11u
                || seal->originalBudget().contractVersion == 12u
                || seal->originalBudget().contractVersion == 13u
                || seal->originalBudget().contractVersion == 16u
                || seal->originalBudget().contractVersion == 18u
                || seal->originalBudget().contractVersion == 20u
                || seal->originalBudget().contractVersion == 22u)
                ? std::optional<PublicationIntent>{{1u, true}}
                : std::nullopt);
        auto graph = core::makeUnique<RootState::Graph>(command);
        if ((diagnostic = graph->additionalFaults.prepare(admission->originalReadbackSequence())))
            return {};
        state->ticket = state->pool.reserve(graph);
        if (!state->ticket) {
            diagnostic = "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-CAPACITY";
            return {};
        }
        {
            core::CpuMemoryQuotaScope callbackScope(seal->originalCallbackQuota());
            for (std::size_t i = 0u; i < state->slots.size(); ++i)
                state->slots[i] =
                    core::makeUnique<RootState::Slot>(RootState::roleAt(i), i, state->ticket);
            if ((diagnostic = state->prepareOriginalMapReceipts(
                     admission, command->device_, command->queue_)))
                return {};
        }
        // Allocate every fallible owner/receipt before exposing native userdata.
        auto result = WebGpuBindlessRootCallbackCustody::shareOriginal(
            core::makeUnique<WebGpuBindlessRootCallbackCustody>(
                WebGpuBindlessRootCallbackCustody::ConstructionKey{}, state, request));
        auto& loss = *state->slots[RootState::lossIndex];
        loss.gate.arm();
        loss.nativeState = prepareWebGpuNativeDeviceLossRegistration(
            command->device_, &loss, &RootState::consumeNativeLoss);
        if (!loss.nativeState) {
            loss.gate.detach();
            diagnostic = "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-NATIVE-BINDING";
            return {};
        }
        loss.owner = state;
        command->callbackCustodyClaimed_ = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-ALLOCATION";
    }
    return {};
#endif
}

const char* WebGpuCanonicalCallbackCustody::validateRootOriginal(
    const WebGpuBindlessRootCallbackCustody& self) noexcept
{
    using RootState = RuntimeState<WebGpuBindlessRootColdCommand>;
    const auto& state = self.state_;
    if (!state || !state->sharedOwnerMatches(state))
        return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-STATE-OWNER";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || state->pool.pending() != 1u || !state->pool.accepting())
        return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-CAPACITY";
    if (const auto* id = inspectRootOriginal(graph->command, self.request_))
        return id;
    const auto& command = graph->command;
    const auto& admission = command->originalAdmission();
    const auto& seal = admission->originalReservation()->originalSeal();
    if (!core::SharedOwnerProjection::sameOwner(
            self.originalMetadataQuota(), seal->originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->submissionQuota, seal->originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->callbackQuota, seal->originalCallbackQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->results, command->originalResultStorage()))
        return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-QUOTA";
    if (!command->callbackCustodyClaimed_)
        return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-CLAIM";
    if (state->slots.size() != self.request_.maxCallbackSlots
        || state->admittedOwners
            != command->retainedOwnerCount()
                + rhi::OriginalBindlessRootCallbackCustodyContract::additionalOwners(self.request_))
        return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-CAPACITY";
    for (std::size_t i = 0u; i < state->slots.size(); ++i) {
        const auto& slot = state->slots[i];
        if (!slot || slot->index != i || slot->role != RootState::roleAt(i)
            || (i != RootState::lossIndex
                && (slot->gate.phase() != core::runtime::ExternalCallbackGate::Phase::Unarmed
                    || slot->owner || slot->nativeState || slot->mapIssued || slot->scopeIssued
                    || slot->copied)))
            return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-SLOT";
    }
    if (const auto* id =
            state->validateOriginalMapReceipts(admission, command->device_, command->queue_))
        return id;
    const auto& loss = *state->slots[RootState::lossIndex];
    return loss.nativeState && core::SharedOwnerProjection::sameOwner(loss.owner, state)
            && loss.gate.phase() == core::runtime::ExternalCallbackGate::Phase::Armed
        ? nullptr
        : "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-LOSS-UNBOUND";
}

WebGpuBindlessRootCallbackCustody::WebGpuBindlessRootCallbackCustody(
    ConstructionKey, std::shared_ptr<State> state, Request request) noexcept
    : state_(std::move(state)), request_(request)
{}

WebGpuBindlessRootCallbackCustody::~WebGpuBindlessRootCallbackCustody()
{
    if (state_ && state_->slots[State::lossIndex])
        state_->cancel();
}

std::shared_ptr<const WebGpuBindlessRootCallbackCustody>
WebGpuBindlessRootCallbackCustody::prepareOriginal(
    std::shared_ptr<const WebGpuBindlessRootColdCommand> command,
    const Request& request,
    const char*& diagnostic)
{
    return WebGpuCanonicalCallbackCustody::prepareRootOriginal(
        std::move(command), request, diagnostic);
}

const char* WebGpuBindlessRootCallbackCustody::validate() const noexcept
{
    return WebGpuCanonicalCallbackCustody::validateRootOriginal(*this);
}

const char* WebGpuBindlessRootCallbackCustody::cancelOriginal() const noexcept
{
    return state_->cancel();
}

bool WebGpuBindlessRootCallbackCustody::originalNativeLossArmed() const noexcept
{
    return state_->slots[State::lossIndex]->gate.phase()
        == core::runtime::ExternalCallbackGate::Phase::Armed;
}

std::uint32_t WebGpuBindlessRootCallbackCustody::pendingOriginalRecords() const noexcept
{
    return state_->pool.pending();
}

std::uint32_t WebGpuBindlessRootCallbackCustody::retainedOwnerCount() const noexcept
{
    return state_->admittedOwners;
}

WebGpuCanonicalCallbackCustody::WebGpuCanonicalCallbackCustody(
    ConstructionKey, std::shared_ptr<State> state, Budget budget) noexcept
    : state_(std::move(state)), budget_(budget)
{}

WebGpuCanonicalCallbackCustody::~WebGpuCanonicalCallbackCustody()
{
    if (state_ && state_->slots[State::lossIndex])
        state_->cancel();
}

void WebGpuCanonicalCallbackCustody::invalidateOriginalOwnership(
    const WebGpuCanonicalColdCommand& command) noexcept
{
    command.originalAdmission()
        ->originalReservation()
        ->originalSeal()
        ->invalidateOriginalOwnership();
}

const char* WebGpuCanonicalCallbackCustody::inspect(
    const std::shared_ptr<const WebGpuCanonicalColdCommand>& command,
    const Budget& budget,
    const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>& results,
    const std::optional<PublicationIntent>& publication) noexcept
{
    using Contract = rhi::OriginalCommandCallbackCustodyContract;
    if (const auto* id = Contract::validateIntent(budget))
        return id;
    if (const auto* id = WebGpuCanonicalColdCommand::validateOriginal(command))
        return id;
    const auto& admission = *command->originalAdmission();
    if (const auto* id = rhi::OriginalGeneralOutputPairingContract::validatePair(
            admission.originalBudget().generalOutput, budget.generalOutput))
        return id;
    if (budget.maxCpuSubmissionBytes != admission.originalSubmissionQuota()->limitBytes()
        || budget.maxCpuCallbackBytes != admission.originalCallbackQuota()->limitBytes())
        return "RHI-WEBGPU-CANONICAL-CALLBACK-QUOTA";
    const auto requirements = admission.requirements();
    if (requirements.capacity.pendingSubmissions != budget.maxPendingRecords
        || requirements.capacity.callbackTickets != budget.maxCallbackSlots
        || requirements.capacity.callbackRegistrations != budget.maxCallbackSlots)
        return "RHI-WEBGPU-CANONICAL-CALLBACK-REQUIREMENTS";
    const bool mapping = admission.originalBudget().contractVersion == 5u;
    if (publication) {
        if (!mapping)
            return "RHI-WEBGPU-CANONICAL-CALLBACK-PUBLICATION-PROFILE";
        using Publication = rhi::OriginalCommandReadbackSnapshotPublicationContract;
        if (const auto* id = Publication::validateIntent(*publication))
            return id;
        const auto decision =
            arp::rhi::semantics::OriginalCommandReadbackSnapshotPublicationLowering::lowering(
                rhi::Backend::WebGPU);
        if (!decision.publicationAvailable)
            return decision.diagnostic;
    }
    if (mapping != (budget.contractVersion == 2u))
        return "RHI-WEBGPU-CANONICAL-CALLBACK-MAP-PROFILE";
    std::uint64_t extra = Contract::additionalOwners(budget);
    if (!extra)
        return "RHI-WEBGPU-CANONICAL-CALLBACK-OWNER-CAPACITY";
    if (mapping) {
        if (const auto* id = WebGpuCanonicalReadbackResultStorage::validateOriginal(results))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                results->originalAdmission(), command->originalAdmission()))
            return "RHI-WEBGPU-CANONICAL-CALLBACK-RESULT-ADMISSION";
        if (results->snapshots().size() != requirements.readbackMap.maps
            || requirements.readbackMap.maps != requirements.readback.resultDeclarations)
            return "RHI-WEBGPU-CANONICAL-CALLBACK-MAP-COVERAGE";
        extra += results->retainedOwnerCount() - admission.retainedOwnerCount();
    } else if (results)
        return "RHI-WEBGPU-CANONICAL-CALLBACK-MAP-PROFILE";
    if (extra > UINT32_MAX || command->retainedOwnerCount() > UINT32_MAX - extra)
        return "RHI-WEBGPU-CANONICAL-CALLBACK-OWNER-CAPACITY";
    const auto owners = command->retainedOwnerCount() + static_cast<std::uint32_t>(extra);
    const auto& lifecycle = admission.originalReservation()->originalSeal()->originalBudget();
    if (owners > budget.maxRetainedOwners || owners > admission.originalBudget().maxRetainedOwners
        || owners > lifecycle.maxRecordingOwners || owners > lifecycle.maxSubmissionOwners
        || owners > lifecycle.maxCallbackOwners)
        return "RHI-WEBGPU-CANONICAL-CALLBACK-OWNER-CAPACITY";
    return nullptr;
}

std::shared_ptr<const WebGpuCanonicalCallbackCustody>
WebGpuCanonicalCallbackCustody::prepareOriginal(
    std::shared_ptr<const WebGpuCanonicalColdCommand> command,
    const Budget& budget,
    const char*& diagnostic,
    std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> results,
    std::optional<PublicationIntent> publication)
{
    diagnostic = inspect(command, budget, results, publication);
    if (diagnostic)
        return {};
    if (command->callbackCustodyClaimed_) {
        diagnostic = "RHI-WEBGPU-CANONICAL-CALLBACK-DUPLICATE";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-UNAVAILABLE";
    return {};
#else
    try {
        const auto& admission = *command->originalAdmission();
        core::CpuMemoryQuotaScope submissionScope(admission.originalSubmissionQuota());
        auto state = core::makeShared<State>(admission.originalSubmissionQuota(),
            admission.originalCallbackQuota(),
            command->retainedOwnerCount()
                + rhi::OriginalCommandCallbackCustodyContract::additionalOwners(budget)
                + (results ? results->retainedOwnerCount() - admission.retainedOwnerCount() : 0u),
            budget.maxCallbackSlots,
            results,
            publication);
        auto graph = core::makeUnique<State::Graph>(command);
        state->ticket = state->pool.reserve(graph);
        if (!state->ticket) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CALLBACK-CAPACITY";
            return {};
        }
        {
            core::CpuMemoryQuotaScope callbackScope(admission.originalCallbackQuota());
            for (std::size_t i = 0u; i < state->slots.size(); ++i) {
                state->slots[i] = core::makeUnique<State::Slot>(State::roleAt(i), i, state->ticket);
            }
        }
        if (results) {
            core::CpuMemoryQuotaScope callbackScope(admission.originalCallbackQuota());
            if ((diagnostic = state->prepareOriginalMapReceipts(
                     *admission.originalReadbackJoins(), command->device_, command->queue_)))
                return {};
        }
        // Allocate the opaque shared wrapper before publishing any native userdata.
        auto result = shareOriginal(
            core::makeUnique<WebGpuCanonicalCallbackCustody>(ConstructionKey{}, state, budget));
        auto& loss = *state->slots[State::lossIndex];
        loss.gate.arm();
        loss.nativeState = prepareWebGpuNativeDeviceLossRegistration(
            command->device_, &loss, &State::consumeNativeLoss);
        if (!loss.nativeState) {
            loss.gate.detach();
            diagnostic = "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-BINDING";
            return {};
        }
        // Reservation already owns the exact Source-authored capacity. Consume
        // its eighth registration for loss (also in /5),
        // never recharge the original facade loss subscription or Source counts.
        loss.owner = state;
        command->callbackCustodyClaimed_ = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-CANONICAL-CALLBACK-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-CANONICAL-CALLBACK-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuCanonicalCallbackCustody::validate() const noexcept
{
    if (!state_ || !state_->sharedOwnerMatches(state_))
        return "RHI-WEBGPU-CANONICAL-CALLBACK-STATE-OWNER";
    if (state_->terminalDiagnostic)
        return state_->terminalDiagnostic;
    auto* graph = state_->pool.get(state_->ticket);
    if (!graph || state_->pool.pending() != 1u || !state_->pool.accepting())
        return "RHI-WEBGPU-CANONICAL-CALLBACK-CAPACITY";
    if (const auto* id = inspect(graph->command, budget_, state_->results, state_->publication))
        return id;
    const auto& admission = *graph->command->originalAdmission();
    if (!core::SharedOwnerProjection::sameOwner(
            originalMetadataQuota(), admission.originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state_->submissionQuota, admission.originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state_->callbackQuota, admission.originalCallbackQuota()))
        return "RHI-WEBGPU-CANONICAL-CALLBACK-QUOTA";
    if (!graph->command->callbackCustodyClaimed_)
        return "RHI-WEBGPU-CANONICAL-CALLBACK-CLAIM";
    if (state_->slots.size() != budget_.maxCallbackSlots)
        return "RHI-WEBGPU-CANONICAL-CALLBACK-REQUIREMENTS";
    for (std::size_t i = 0u; i < state_->slots.size(); ++i) {
        const auto& slot = state_->slots[i];
        if (!slot
            || (i != State::lossIndex
                && (slot->gate.phase() != core::runtime::ExternalCallbackGate::Phase::Unarmed
                    || slot->nativeState || slot->owner)))
            return "RHI-WEBGPU-CANONICAL-CALLBACK-SLOT";
        if (slot->role == State::Role::MapCompletion)
            if (const auto* id = slot->map.validateCold())
                return id;
        if (slot->role == State::Role::MapScope
            && !WebGpuNativeCompletionReceipt::prepared(slot->nativeReceipt))
            return "RHI-WEBGPU-CANONICAL-MAP-SCOPE-PREPARATION";
    }
    const auto& loss = *state_->slots[State::lossIndex];
    return loss.nativeState && core::SharedOwnerProjection::sameOwner(loss.owner, state_)
            && loss.gate.phase() == core::runtime::ExternalCallbackGate::Phase::Armed
        ? nullptr
        : "RHI-WEBGPU-CANONICAL-CALLBACK-LOSS-UNBOUND";
}

const char* WebGpuCanonicalCallbackCustody::cancelOriginal() const noexcept
{
    return state_->cancel();
}

bool WebGpuCanonicalCallbackCustody::originalNativeLossArmed() const noexcept
{
    return state_->slots[State::lossIndex]->gate.phase()
        == core::runtime::ExternalCallbackGate::Phase::Armed;
}

bool WebGpuCanonicalCallbackCustody::originalNativeLossDelivered() const noexcept
{
    return state_->slots[State::lossIndex]->gate.phase()
        == core::runtime::ExternalCallbackGate::Phase::Delivered;
}

std::uint32_t WebGpuCanonicalCallbackCustody::pendingOriginalRecords() const noexcept
{
    return static_cast<std::uint32_t>(state_->pool.pending());
}

std::uintptr_t WebGpuCanonicalCallbackCustody::originalLossStateForInspection() const noexcept
{
    return state_->slots[State::lossIndex]->nativeState;
}

void WebGpuCanonicalCallbackCustody::invalidateOriginalOwnership(
    const WebGpuOriginalTypedColdCommand& command) noexcept
{
    command.originalAdmission()
        ->originalReservation()
        ->originalSeal()
        ->invalidateOriginalOwnership();
}

const char* WebGpuCanonicalCallbackCustody::inspectTypedOriginal(
    const std::shared_ptr<const WebGpuOriginalTypedColdCommand>& command,
    const rhi::OriginalTypedCallbackCustodyRequest& request,
    const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>& results,
    const std::optional<PublicationIntent>& publication) noexcept
{
    using C = rhi::OriginalTypedCallbackCustodyContract;
    if (const auto* id = WebGpuOriginalTypedColdCommand::validateOriginal(command))
        return id;
    const auto& admission = *command->originalAdmission();
    C::Participants participants;
    if (const auto* id = C::inspect(admission.requirements(), request, participants))
        return id;
    std::uint32_t resultOwners = 0u;
    if (request.readbackMap.contractVersion) {
        const auto decision = rhi::semantics::OriginalTypedReadbackMapCustodyLowering::lowering(
            rhi::Backend::WebGPU, request.readbackMap.contractVersion);
        if (!decision.preparationAvailable)
            return decision.diagnostic;
        if (const auto* id = WebGpuCanonicalReadbackResultStorage::validateOriginal(results))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                results->originalTypedAdmission(), command->originalAdmission())
            || results->originalAdmission())
            return "RHI-WEBGPU-TYPED-CALLBACK-RESULT-BINDING";
        if (results->snapshots().size() != admission.requirements().readbackMap.maps)
            return "RHI-WEBGPU-TYPED-CALLBACK-RESULT-COVERAGE";
        resultOwners = results->retainedOwnerCount() - admission.retainedOwnerCount();
    } else if (results)
        return "RHI-WEBGPU-TYPED-CALLBACK-RESULT-PROFILE";
    if (publication) {
        if (!results || !request.readbackMap.contractVersion)
            return "RHI-WEBGPU-TYPED-CALLBACK-PUBLICATION-PROFILE";
        if (const auto* id =
                rhi::OriginalCommandReadbackSnapshotPublicationContract::validateIntent(
                    *publication))
            return id;
        const auto decision =
            rhi::semantics::OriginalCommandReadbackSnapshotPublicationLowering::lowering(
                rhi::Backend::WebGPU);
        if (!decision.publicationAvailable)
            return decision.diagnostic;
    }
    const auto& seal = admission.originalReservation()->originalSeal();
    if (request.maxCpuSubmissionBytes != seal->originalSubmissionQuota()->limitBytes()
        || request.maxCpuCallbackBytes != seal->originalCallbackQuota()->limitBytes())
        return "RHI-WEBGPU-TYPED-CALLBACK-QUOTA";
    const auto& source = admission.originalRoot()
                             ->originalGraph()
                             ->originalInputs()
                             ->originalScalars()
                             ->originalUsage()
                             ->originalSources()
                             ->originalBudget();
    const auto& lifecycle = seal->originalBudget();
    if (const auto* id = C::validateOwners(command->retainedOwnerCount(),
            resultOwners,
            request,
            command->originalRequest().maxRetainedOwners,
            admission.originalRequest().maxRetainedOwners,
            source,
            lifecycle,
            admission.originalRoot()->retainedOwnerCount()))
        return id;
    const auto& closure = admission.originalPayload()->originalDecision()->originalClosure();
    const auto* observation = closure->originalNativeLossObservation();
    if (!observation)
        return "RHI-WEBGPU-TYPED-CALLBACK-LOSS-UNBOUND";
    const auto binding =
        std::dynamic_pointer_cast<const WebGpuOriginalObservedNativeLoss>(observation->binding_);
    if (!binding || !binding->hasQuotaBoundSharedOwner(binding))
        return "RHI-WEBGPU-TYPED-CALLBACK-OBSERVED-PROFILE";
    if (const auto* id = binding->diagnostic())
        return id;
    return nullptr;
}

std::shared_ptr<const WebGpuOriginalTypedCallbackCustody>
WebGpuCanonicalCallbackCustody::prepareTypedOriginal(
    std::shared_ptr<const WebGpuOriginalTypedColdCommand> command,
    const rhi::OriginalTypedCallbackCustodyRequest& request,
    const char*& diagnostic,
    std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> results,
    std::optional<PublicationIntent> publication)
{
    const auto observerDecision =
        rhi::OriginalDeviceNativeLossObserverContract::lowering(rhi::Backend::WebGPU);
    const auto custodyDecision =
        arp::rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(
            rhi::Backend::WebGPU, request.contractVersion);
    if (!observerDecision.typedColdObserverPreparationAvailable
        || !custodyDecision.observerPreparationAvailable) {
        diagnostic = custodyDecision.diagnostic;
        return {};
    }
    if ((diagnostic = inspectTypedOriginal(command, request, results, publication)))
        return {};
    const auto& admission = *command->originalAdmission();
    const auto& seal = admission.originalReservation()->originalSeal();
    if (seal->typedCallbackCustodyClaimed_) {
        diagnostic = "RHI-WEBGPU-TYPED-CALLBACK-DUPLICATE";
        return {};
    }
    const auto* observation = command->originalAdmission()
                                  ->originalPayload()
                                  ->originalDecision()
                                  ->originalClosure()
                                  ->originalNativeLossObservation();
    const auto binding =
        std::dynamic_pointer_cast<const WebGpuOriginalObservedNativeLoss>(observation->binding_);
    if (!binding->observers_.accepting()) {
        diagnostic = "RHI-WEBGPU-TYPED-CALLBACK-OBSERVER-CLOSED";
        return {};
    }
    if (binding->observers_.pending() >= binding->observers_.capacity()) {
        diagnostic = "RHI-WEBGPU-TYPED-CALLBACK-OBSERVER-CAPACITY";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-TYPED-CALLBACK-NATIVE-UNAVAILABLE";
    return {};
#else
    using S = RuntimeState<WebGpuOriginalTypedColdCommand>;
    using Wrapper = WebGpuOriginalTypedCallbackCustody;
    try {
        core::CpuMemoryQuotaScope submission(seal->originalSubmissionQuota());
        auto state = core::makeShared<S>(seal->originalSubmissionQuota(),
            seal->originalCallbackQuota(),
            command->retainedOwnerCount()
                + rhi::OriginalTypedCallbackCustodyContract::additionalOwners(request)
                + (results ? results->retainedOwnerCount() - admission.retainedOwnerCount() : 0u),
            request.maxCallbackSlots,
            results,
            publication);
        auto graph = core::makeUnique<S::Graph>(command);
        state->ticket = state->pool.reserve(graph);
        if (!state->ticket) {
            diagnostic = "RHI-WEBGPU-TYPED-CALLBACK-CAPACITY";
            return {};
        }
        {
            core::CpuMemoryQuotaScope callback(seal->originalCallbackQuota());
            for (std::size_t i = 0u; i < state->slots.size(); ++i) {
                state->slots[i] = core::makeUnique<S::Slot>(S::roleAt(i), i, state->ticket);
            }
        }
        if (results) {
            const auto& joins = admission.originalPayload()
                                    ->originalDecision()
                                    ->originalClosure()
                                    ->originalReadbackJoins();
            core::CpuMemoryQuotaScope callback(seal->originalCallbackQuota());
            if ((diagnostic = state->prepareOriginalMapReceipts(
                     *joins, command->device_, command->queue_)))
                return {};
        }
        const auto* observation = admission.originalPayload()
                                      ->originalDecision()
                                      ->originalClosure()
                                      ->originalNativeLossObservation();
        state->observedLoss = std::dynamic_pointer_cast<const WebGpuOriginalObservedNativeLoss>(
            observation->binding_);
        auto result =
            shareOriginal(core::makeUnique<Wrapper>(Wrapper::ConstructionKey{}, state, request));
        // Final bounded publication. No encoder, extra native registration,
        // replacement pool or owned receiver object is created.
        auto& loss = *state->slots[S::lossIndex];
        loss.gate.arm();
        loss.owner = state;
        const auto status =
            state->observedLoss->observers_.attach(std::shared_ptr<const void>(state, &loss),
                &S::consumeOriginalPhysicalLoss,
                state->observerTicket);
        if (status
            != core::runtime::BoundedTerminalObservers<
                rhi::OriginalDeviceNativeLossTerminal>::Status::Complete) {
            loss.gate.detach();
            loss.owner.reset();
            diagnostic = "RHI-WEBGPU-TYPED-CALLBACK-OBSERVER-ATTACH";
            return {};
        }
        seal->typedCallbackCustodyClaimed_ = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-TYPED-CALLBACK-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-TYPED-CALLBACK-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuCanonicalCallbackCustody::validateTypedOriginal(
    const WebGpuOriginalTypedCallbackCustody& original) noexcept
{
    using S = RuntimeState<WebGpuOriginalTypedColdCommand>;
    const auto& state = original.state_;
    if (!state || !state->sharedOwnerMatches(state))
        return "RHI-WEBGPU-TYPED-CALLBACK-STATE-OWNER";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || state->pool.pending() != 1u || !state->pool.accepting())
        return "RHI-WEBGPU-TYPED-CALLBACK-CAPACITY";
    if (const auto* id = inspectTypedOriginal(
            graph->command, original.request_, state->results, state->publication))
        return id;
    const auto& seal = graph->command->originalAdmission()->originalReservation()->originalSeal();
    if (!seal->typedCallbackCustodyClaimed_)
        return "RHI-WEBGPU-TYPED-CALLBACK-CLAIM";
    if (!core::SharedOwnerProjection::sameOwner(
            original.originalMetadataQuota(), seal->originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->submissionQuota, seal->originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->callbackQuota, seal->originalCallbackQuota()))
        return "RHI-WEBGPU-TYPED-CALLBACK-QUOTA";
    const auto* observation = graph->command->originalAdmission()
                                  ->originalPayload()
                                  ->originalDecision()
                                  ->originalClosure()
                                  ->originalNativeLossObservation();
    if (!state->observedLoss
        || !core::SharedOwnerProjection::sameOwner(state->observedLoss, observation->binding_)
        || state->observedLoss->observers_.inspect(state->observerTicket)
            != core::runtime::BoundedTerminalObservers<
                rhi::OriginalDeviceNativeLossTerminal>::Status::Complete)
        return "RHI-WEBGPU-TYPED-CALLBACK-LOSS-UNBOUND";
    if (state->slots.size() != original.request_.maxCallbackSlots)
        return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
    for (std::size_t i = 0u; i < state->slots.size(); ++i) {
        const auto& slot = state->slots[i];
        if (!slot || slot->nativeState || slot->role != S::roleAt(i))
            return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
        if (i != S::lossIndex) {
            if (slot->owner
                || slot->gate.phase() != core::runtime::ExternalCallbackGate::Phase::Unarmed)
                return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
            if (i >= S::slotCount) {
                if (slot->role == S::Role::MapCompletion && slot->nativeReceipt)
                    return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
            } else if (state->issuersClaimed
                    ? !WebGpuNativeCompletionReceipt::prepared(slot->nativeReceipt)
                    : slot->nativeReceipt != 0u)
                return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
        } else if (slot->nativeReceipt)
            return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
    }
    if (state->results) {
        const auto& joins = graph->command->originalAdmission()
                                ->originalPayload()
                                ->originalDecision()
                                ->originalClosure()
                                ->originalReadbackJoins();
        if (const auto* id = state->validateOriginalMapReceipts(
                *joins, graph->command->device_, graph->command->queue_))
            return id;
    }
    const auto& loss = *state->slots[S::lossIndex];
    return core::SharedOwnerProjection::sameOwner(loss.owner, state)
            && loss.gate.phase() == core::runtime::ExternalCallbackGate::Phase::Armed
        ? nullptr
        : "RHI-WEBGPU-TYPED-CALLBACK-LOSS-UNBOUND";
}

WebGpuOriginalTypedCallbackCustody::WebGpuOriginalTypedCallbackCustody(
    ConstructionKey, std::shared_ptr<State> state, Request request) noexcept
    : state_(std::move(state)), request_(request)
{}

WebGpuOriginalTypedCallbackCustody::~WebGpuOriginalTypedCallbackCustody()
{
    if (state_ && state_->slots[State::lossIndex])
        state_->cancel();
}

std::shared_ptr<const WebGpuOriginalTypedCallbackCustody>
WebGpuOriginalTypedCallbackCustody::prepareOriginal(
    std::shared_ptr<const WebGpuOriginalTypedColdCommand> command,
    const Request& request,
    const char*& diagnostic,
    std::shared_ptr<const WebGpuCanonicalReadbackResultStorage> results,
    std::optional<WebGpuCanonicalCallbackCustody::PublicationIntent> publication)
{
    return WebGpuCanonicalCallbackCustody::prepareTypedOriginal(
        std::move(command), request, diagnostic, std::move(results), publication);
}

const char* WebGpuOriginalTypedCallbackCustody::validate() const noexcept
{
    return WebGpuCanonicalCallbackCustody::validateTypedOriginal(*this);
}

const char* WebGpuOriginalTypedCallbackCustody::cancelOriginal() const noexcept
{
    return state_->cancel();
}

std::uint32_t WebGpuOriginalTypedCallbackCustody::pendingOriginalRecords() const noexcept
{
    return static_cast<std::uint32_t>(state_->pool.pending());
}

bool WebGpuOriginalTypedCallbackCustody::physicalLossArmed() const noexcept
{
    return state_->slots[State::lossIndex]->gate.phase()
        == core::runtime::ExternalCallbackGate::Phase::Armed;
}

bool WebGpuOriginalTypedCallbackCustody::physicalLossDelivered() const noexcept
{
    return state_->slots[State::lossIndex]->gate.phase()
        == core::runtime::ExternalCallbackGate::Phase::Delivered;
}

std::shared_ptr<const WebGpuCanonicalCopyPassLowering>
WebGpuCanonicalCopyPassLowering::prepareOriginal(
    std::shared_ptr<const WebGpuCanonicalCallbackCustody> custody,
    const Budget& budget,
    const char*& diagnostic)
{
    return WebGpuCanonicalCallbackCustody::prepareOriginalLowering(
        std::move(custody), budget, diagnostic);
}

std::shared_ptr<const WebGpuCanonicalCopyPassLowering>
WebGpuCanonicalCallbackCustody::prepareOriginalLowering(
    std::shared_ptr<const WebGpuCanonicalCallbackCustody> custody,
    const rhi::OriginalCommandCopyPassLoweringBudget& budget,
    const char*& diagnostic)
{
    using Lowering = WebGpuCanonicalCopyPassLowering;
    using Payload = Lowering::Payload;
    using Contract = rhi::OriginalCommandCopyPassLoweringContract;
    diagnostic = Contract::validateIntent(budget);
    if (diagnostic)
        return {};
    if ((diagnostic = validateOriginal(custody)))
        return {};
    auto& state = *custody->state_;
    if (state.loweringClaimed) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-DUPLICATE";
        return {};
    }
    const auto* graph = state.pool.get(state.ticket);
    const auto& admission = *graph->command->originalAdmission();
    if ((diagnostic = rhi::OriginalGeneralOutputPairingContract::validatePair(
             admission.originalBudget().generalOutput, budget.generalOutput)))
        return {};
    const auto& lifecycle = admission.originalReservation()->originalSeal()->originalBudget();
    if (budget.contractVersion != admission.originalBudget().contractVersion) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-PROFILE-VERSION";
        return {};
    }
    if (budget.contractVersion == 5u) {
        const auto r = admission.requirements();
        const auto original = std::uint64_t(r.recordingScopeCallbacks) + r.submissionScopeCallbacks
            + r.queueCallbacks + r.runtimeLossCallbacks;
        if ((diagnostic = rhi::OriginalCommandReadbackMapContract::validateRequirements(
                 r.readback, original, original, budget.readbackMap, r.readbackMap)))
            return {};
    }
    if (budget.maxCpuMetadataBytes
        != admission.originalRoot()->originalMetadataQuota()->limitBytes()) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-QUOTA";
        return {};
    }
    if (state.admittedOwners > UINT32_MAX - Contract::additionalOwnerCount) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-OWNER-CAPACITY";
        return {};
    }
    const auto owners = state.admittedOwners + Contract::additionalOwnerCount;
    if (owners > budget.maxRetainedOwners || owners > admission.originalBudget().maxRetainedOwners
        || owners > lifecycle.maxRecordingOwners || owners > lifecycle.maxSubmissionOwners
        || owners > lifecycle.maxCallbackOwners) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-OWNER-CAPACITY";
        return {};
    }
    const auto& closure = *admission.originalClosure();
    std::uint32_t count = 0u, passCount = 0u, uploadCount = 0u, readbackCount = 0u;
    if ((diagnostic = Payload::inspectCopies(
             closure,
             budget,
             [](const auto*, const auto*, const auto&, std::uint32_t) noexcept -> const char* {
                 return nullptr;
             },
             count)))
        return {};
    if ((diagnostic = Payload::inspectPass(
             closure,
             budget,
             [](const rhi::RenderPassArguments*, std::uint32_t) noexcept -> const char* {
                 return nullptr;
             },
             passCount)))
        return {};
    if ((diagnostic = Payload::inspectUploads(
             closure,
             budget,
             admission.originalUploadJoins(),
             [](std::size_t,
                 std::size_t,
                 const auto*,
                 const auto*,
                 const auto&,
                 std::uint32_t) noexcept -> const char* {
                 return nullptr;
             },
             uploadCount)))
        return {};
    if ((diagnostic = Payload::inspectReadbacks(
             closure,
             budget,
             admission.originalReadbackJoins(),
             [](std::size_t,
                 std::size_t,
                 const auto*,
                 const auto*,
                 const auto&,
                 std::uint32_t) noexcept -> const char* {
                 return nullptr;
             },
             readbackCount)))
        return {};
    if ((budget.contractVersion == 4u || budget.contractVersion == 5u)
        && readbackCount != admission.requirements().readback.nativeCopyCommands) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-READBACK-REQUIREMENTS";
        return {};
    }
    if (passCount != admission.requirements().renderPasses) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-PASS-REQUIREMENTS";
        return {};
    }
    if (budget.contractVersion == 3u
        && std::uint64_t(count) + uploadCount != admission.requirements().nativeCopyCommands) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-UPLOAD-REQUIREMENTS";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-NATIVE-UNAVAILABLE";
    return {};
#else
    try {
        core::CpuMemoryQuotaScope scope(admission.originalRoot()->originalMetadataQuota());
        auto payload = core::makeUnique<Payload>(closure,
            count,
            passCount,
            admission.requirements().originalCommands,
            admission.originalUploadJoins(),
            uploadCount,
            admission.originalReadbackJoins(),
            readbackCount);
        if ((diagnostic = payload->populate(budget)))
            return {};
        if ((diagnostic = payload->validate(budget)))
            return {};
        auto result = Lowering::shareOriginal(core::makeUnique<Lowering>(
            Lowering::ConstructionKey{}, custody, budget, count, owners));
        // No encoder or callback registration is issued here. Every fallible
        // allocation/preflight precedes one sticky publication into exact state.
        state.lowering = std::move(payload);
        state.loweringClaimed = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-CANONICAL-LOWERING-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuCanonicalCallbackCustody::validateOriginalLowering(
    const WebGpuCanonicalCopyPassLowering& original) const noexcept
{
    using Contract = rhi::OriginalCommandCopyPassLoweringContract;
    if (const auto* id = validate())
        return id;
    if (!state_->loweringClaimed || !state_->lowering)
        return "RHI-WEBGPU-CANONICAL-LOWERING-UNPREPARED";
    const auto* graph = state_->pool.get(state_->ticket);
    const auto& admission = *graph->command->originalAdmission();
    if (const auto* id = rhi::OriginalGeneralOutputPairingContract::validatePair(
            admission.originalBudget().generalOutput, original.budget_.generalOutput))
        return id;
    const auto& lifecycle = admission.originalReservation()->originalSeal()->originalBudget();
    if (state_->lowering->closure != admission.originalClosure().get()
        || original.copies_ != state_->lowering->copies.values().size()
        || original.owners_ != state_->admittedOwners + Contract::additionalOwnerCount)
        return "RHI-WEBGPU-CANONICAL-LOWERING-ORIGINAL-BINDING";
    if (original.budget_.maxCpuMetadataBytes
            != admission.originalRoot()->originalMetadataQuota()->limitBytes()
        || !core::SharedOwnerProjection::sameOwner(
            original.originalMetadataQuota(), admission.originalRoot()->originalMetadataQuota()))
        return "RHI-WEBGPU-CANONICAL-LOWERING-QUOTA";
    if (original.owners_ > original.budget_.maxRetainedOwners
        || original.owners_ > admission.originalBudget().maxRetainedOwners
        || original.owners_ > lifecycle.maxRecordingOwners
        || original.owners_ > lifecycle.maxSubmissionOwners
        || original.owners_ > lifecycle.maxCallbackOwners)
        return "RHI-WEBGPU-CANONICAL-LOWERING-OWNER-CAPACITY";
    return state_->lowering->validate(
        original.budget_, admission.originalReservation()->originalSeal());
}

const char* WebGpuCanonicalCopyPassLowering::validate() const noexcept
{
    if (const auto* id = WebGpuCanonicalCallbackCustody::validateOriginal(custody_))
        return id;
    return custody_->validateOriginalLowering(*this);
}

std::uint32_t WebGpuCanonicalCopyPassLowering::projectedUploadCopyCount() const noexcept
{
    return custody_->state_->lowering
        ? static_cast<std::uint32_t>(custody_->state_->lowering->uploads.values().size())
        : 0u;
}

std::uint32_t WebGpuCanonicalCopyPassLowering::projectedReadbackCopyCount() const noexcept
{
    return custody_->state_->lowering
        ? static_cast<std::uint32_t>(custody_->state_->lowering->readbacks.values().size())
        : 0u;
}

bool WebGpuCanonicalCopyPassLowering::matchesProjectedReadbackForInspection(std::size_t index,
    std::size_t command,
    std::size_t join,
    const rhi::TransferCopyArguments& fields) const noexcept
{
    if (validate())
        return false;
    const auto& readbacks = custody_->state_->lowering->readbacks.values();
    return index < readbacks.size() && readbacks[index].command == command
        && readbacks[index].join == join
        && readbacks[index].copy.matches(
            readbacks[index].copy.source, readbacks[index].copy.destination, fields);
}

bool WebGpuCanonicalCopyPassLowering::matchesProjectedUploadForInspection(std::size_t index,
    std::size_t command,
    std::size_t join,
    const rhi::TransferCopyArguments& fields) const noexcept
{
    if (validate())
        return false;
    const auto& uploads = custody_->state_->lowering->uploads.values();
    return index < uploads.size() && uploads[index].command == command
        && uploads[index].join == join
        && uploads[index].copy.matches(
            uploads[index].copy.source, uploads[index].copy.destination, fields);
}

bool WebGpuCanonicalCopyPassLowering::matchesProjectedCopyForInspection(
    std::size_t index, const rhi::TransferCopyArguments& fields) const noexcept
{
    if (validate())
        return false;
    const auto& copies = custody_->state_->lowering->copies.values();
    return index < copies.size()
        && copies[index].matches(copies[index].source, copies[index].destination, fields);
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::allocateCanonical(
    std::shared_ptr<WebGpuNativeCommandPayload> command,
    std::weak_ptr<WebGpuCanonicalCallbackCustody::State> state)
{
    return shareOriginal(core::makeUnique<WebGpuOrdinaryRecording>(
        ConstructionKey{}, std::move(command), std::move(state)));
}

std::shared_ptr<WebGpuOrdinarySubmission> WebGpuOrdinarySubmission::allocateCanonical(
    std::shared_ptr<const WebGpuRecordedCommandProof> recording,
    std::weak_ptr<WebGpuCanonicalCallbackCustody::State> state)
{
    return shareOriginal(core::makeUnique<WebGpuOrdinarySubmission>(
        ConstructionKey{}, std::move(recording), std::move(state)));
}

WebGpuOrdinaryRecording::Status WebGpuOrdinaryRecording::canonicalStatus() const noexcept
{
    if (rootCanonicalMode_) {
        const auto state = rootCanonicalState();
        if (!state)
            return Status::Rejected;
        if (state->slots[RootState::lossIndex]->gate.phase()
            == core::runtime::ExternalCallbackGate::Phase::Delivered)
            return Status::DeviceLost;
        return state->terminalDiagnostic ? Status::Rejected : status_;
    }
    if (typedCanonicalMode_) {
        const auto state = typedCanonicalState();
        if (!state)
            return Status::Rejected;
        if (state->slots[TypedState::lossIndex]->gate.phase()
            == core::runtime::ExternalCallbackGate::Phase::Delivered)
            return Status::DeviceLost;
        return state->terminalDiagnostic ? Status::Rejected : status_;
    }
    const auto state = legacyCanonicalState();
    if (!state)
        return Status::Rejected;
    if (state->slots[WebGpuCanonicalCallbackCustody::State::lossIndex]->gate.phase()
        == core::runtime::ExternalCallbackGate::Phase::Delivered)
        return Status::DeviceLost;
    if (status_ == Status::Recorded && validateCanonicalLive())
        return Status::Rejected;
    return state->terminalDiagnostic ? Status::Rejected : status_;
}

const char* WebGpuOrdinaryRecording::validateCanonicalCold() const noexcept
{
    const auto state = legacyCanonicalState();
    if (!state)
        return "RHI-WEBGPU-CANONICAL-ISSUER-RETIRED";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || state->recording.get() != this
        || !core::SharedOwnerProjection::sameOwner(graph->command->command_, command_)
        || identity_ != graph->command->identity_ || completion_ || originalStates_
        || originalTransaction_ || originalTextureTransaction_)
        return "RHI-WEBGPU-CANONICAL-ISSUER-ORIGINAL-RECORDING";
    if (!core::SharedOwnerProjection::sameOwner(originalMetadataQuota(),
            graph->command->originalAdmission()->originalRoot()->originalMetadataQuota()))
        return "RHI-WEBGPU-CANONICAL-ISSUER-QUOTA";
    return WebGpuNativeCommandPreparation::validateCanonical(*graph->command);
}

const char* WebGpuOrdinaryRecording::canonicalDiagnostic() const noexcept
{
    if (rootCanonicalMode_)
        return failure_                   ? failure_
            : status_ == Status::Recorded ? validateRootLive()
                                          : rootColdDiagnostic();
    if (typedCanonicalMode_) {
        if (failure_)
            return failure_;
        if (status_ == Status::RecordingPending)
            return "RHI-WEBGPU-TYPED-RECORDING-PENDING";
        if (status_ == Status::Recorded)
            return validateTypedLive();
        return typedColdDiagnostic();
    }
    if (failure_)
        return failure_;
    if (status_ == Status::RecordingPending)
        return "RHI-WEBGPU-CANONICAL-RECORDING-PENDING";
    if (status_ == Status::Recorded)
        return validateCanonicalLive();
    if (const auto* id = validateCanonicalCold())
        return id;
    return "RHI-WEBGPU-CANONICAL-ISSUER-RECORDING-UNPROVEN";
}

WebGpuOrdinarySubmission::Status WebGpuOrdinarySubmission::canonicalStatus() const noexcept
{
    if (rootCanonicalMode_) {
        const auto state = rootCanonicalState();
        if (!state)
            return Status::Unproven;
        if (state->slots[RootState::lossIndex]->gate.phase()
            == core::runtime::ExternalCallbackGate::Phase::Delivered)
            return Status::DeviceLost;
        return state->terminalDiagnostic ? Status::Unproven : status_;
    }
    if (typedCanonicalMode_) {
        const auto state = typedCanonicalState();
        if (!state)
            return Status::Unproven;
        if (state->slots[TypedState::lossIndex]->gate.phase()
            == core::runtime::ExternalCallbackGate::Phase::Delivered)
            return Status::DeviceLost;
        return state->terminalDiagnostic ? Status::Unproven : status_;
    }
    const auto state = legacyCanonicalState();
    if (!state)
        return Status::Unproven;
    if (state->slots[WebGpuCanonicalCallbackCustody::State::lossIndex]->gate.phase()
        == core::runtime::ExternalCallbackGate::Phase::Delivered)
        return Status::DeviceLost;
    return state->terminalDiagnostic ? Status::Unproven : status_;
}

const char* WebGpuOrdinarySubmission::canonicalDiagnostic() const noexcept
{
    if (rootCanonicalMode_) {
        const auto state = rootCanonicalState();
        return !state                      ? "RHI-WEBGPU-ROOT-ISSUER-RETIRED"
            : state->terminalDiagnostic    ? state->terminalDiagnostic
            : status_ == Status::Completed ? nullptr
                                           : rootColdDiagnostic();
    }
    if (typedCanonicalMode_) {
        const auto state = typedCanonicalState();
        if (!state)
            return "RHI-WEBGPU-TYPED-ISSUER-RETIRED";
        if (state->terminalDiagnostic)
            return state->terminalDiagnostic;
        if (status_ == Status::Pending)
            return "RHI-WEBGPU-TYPED-SUBMISSION-PENDING";
        if (status_ == Status::Completed)
            return nullptr;
        return typedColdDiagnostic();
    }
    const auto state = legacyCanonicalState();
    if (!state)
        return "RHI-WEBGPU-CANONICAL-ISSUER-RETIRED";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    if (!state->pool.get(state->ticket) || state->submission.get() != this
        || !core::SharedOwnerProjection::sameOwner(recording_, state->recording))
        return "RHI-WEBGPU-CANONICAL-ISSUER-ORIGINAL-SUBMISSION";
    if (status_ == Status::Pending)
        return "RHI-WEBGPU-CANONICAL-SUBMISSION-PENDING";
    if (status_ == Status::Completed)
        return nullptr;
    if (state->recording->status_ == WebGpuOrdinaryRecording::Status::Prepared)
        if (const auto* id = state->recording->validateCanonicalCold())
            return id;
    return "RHI-WEBGPU-CANONICAL-ISSUER-SUBMISSION-UNPROVEN";
}

std::shared_ptr<const WebGpuCanonicalIssuerPreparation>
WebGpuCanonicalIssuerPreparation::prepareOriginal(
    std::shared_ptr<const WebGpuCanonicalCopyPassLowering> lowering,
    const Budget& budget,
    const char*& diagnostic)
{
    return WebGpuCanonicalCallbackCustody::prepareOriginalIssuers(
        std::move(lowering), budget, diagnostic);
}

std::shared_ptr<const WebGpuCanonicalIssuerPreparation>
WebGpuCanonicalCallbackCustody::prepareOriginalIssuers(
    std::shared_ptr<const WebGpuCanonicalCopyPassLowering> lowering,
    const rhi::OriginalCommandIssuerPreparationBudget& budget,
    const char*& diagnostic)
{
    using Wrapper = WebGpuCanonicalIssuerPreparation;
    using Contract = rhi::OriginalCommandIssuerPreparationContract;
    diagnostic = Contract::validateIntent(budget);
    if (diagnostic)
        return {};
    if ((diagnostic = WebGpuCanonicalCopyPassLowering::validateOriginal(lowering)))
        return {};
    if (lowering->budget_.contractVersion == 5u) {
        const auto decision =
            arp::rhi::semantics::OriginalCommandReadbackMapLowering::lowering(rhi::Backend::WebGPU);
        if (!decision.mapAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
    }
    if (lowering->budget_.contractVersion == 3u) {
        const auto& required = lowering->originalCustody()
                                   ->state_->pool.get(lowering->originalCustody()->state_->ticket)
                                   ->command->originalAdmission()
                                   ->requirements();
        using BufferContract = rhi::OriginalCommandInitializedBufferUploadExecutionContract;
        using TextureContract = rhi::OriginalCommandInitializedTextureUploadExecutionContract;
        diagnostic = required.textureUploads
            ? TextureContract::validateWork(required.textureUploads)
            : BufferContract::validateWork(required.bufferUploads, required.textureUploads);
        if (diagnostic)
            return {};
        const auto decision = required.textureUploads
            ? arp::rhi::semantics::OriginalCommandInitializedTextureUploadExecutionLowering::
                  lowering(rhi::Backend::WebGPU)
            : arp::rhi::semantics::OriginalCommandInitializedBufferUploadExecutionLowering::
                  lowering(rhi::Backend::WebGPU);
        if (!decision.recordingAvailable || !decision.executionAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
    }
    if (lowering->budget_.contractVersion == 4u) {
        const auto decision = arp::rhi::semantics::OriginalCommandOrderedReadbackLowering::lowering(
            rhi::Backend::WebGPU);
        if (!decision.recordingAvailable || !decision.executionAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
    }
    const auto& custody = lowering->originalCustody();
    auto& state = *custody->state_;
    if (state.issuersClaimed) {
        diagnostic = "RHI-WEBGPU-CANONICAL-ISSUER-DUPLICATE";
        return {};
    }
    const auto* graph = state.pool.get(state.ticket);
    const auto& admission = *graph->command->originalAdmission();
    if ((diagnostic = rhi::OriginalGeneralOutputPairingContract::validatePair(
             admission.originalBudget().generalOutput, budget.generalOutput)))
        return {};
    const auto& lifecycle = admission.originalReservation()->originalSeal()->originalBudget();
    if (budget.maxRecordingCpuBytes
            != admission.originalRoot()->originalMetadataQuota()->limitBytes()
        || budget.maxSubmissionCpuBytes != admission.originalSubmissionQuota()->limitBytes()) {
        diagnostic = "RHI-WEBGPU-CANONICAL-ISSUER-QUOTA";
        return {};
    }
    if (lowering->retainedOwnerCount() > UINT32_MAX - Contract::additionalOwnerCount) {
        diagnostic = "RHI-WEBGPU-CANONICAL-ISSUER-OWNER-CAPACITY";
        return {};
    }
    const auto owners = lowering->retainedOwnerCount() + Contract::additionalOwnerCount;
    if (owners > budget.maxRetainedOwners || owners > admission.originalBudget().maxRetainedOwners
        || owners > lifecycle.maxRecordingOwners || owners > lifecycle.maxSubmissionOwners
        || owners > lifecycle.maxCallbackOwners) {
        diagnostic = "RHI-WEBGPU-CANONICAL-ISSUER-OWNER-CAPACITY";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-CANONICAL-ISSUER-NATIVE-UNAVAILABLE";
    return {};
#else
    try {
        std::shared_ptr<WebGpuOrdinaryRecording> recording;
        {
            core::CpuMemoryQuotaScope scope(admission.originalRoot()->originalMetadataQuota());
            recording = WebGpuOrdinaryRecording::allocateCanonical(
                graph->command->command_, custody->state_);
        }
        core::CpuMemoryQuotaScope scope(admission.originalSubmissionQuota());
        auto submission = WebGpuOrdinarySubmission::allocateCanonical(recording, custody->state_);
        auto result = Wrapper::shareOriginal(core::makeUnique<Wrapper>(
            Wrapper::ConstructionKey{}, lowering, budget, owners, admission.originalRoot()));
        // All C++ allocations complete before sticky publication. The two real
        // issuers consume the existing command/slots; no ordinary reservation,
        // second state graph, native event or hot share-owner creation follows.
        std::array<WebGpuNativeCompletionReceipt::Prepared, WebGpuOrdinarySubmission::callbackCount>
            receipts;
        for (std::size_t i = State::submissionFirst; i < State::lossIndex; ++i) {
            auto prepared = WebGpuNativeCompletionReceipt::prepare(graph->command->device_,
                graph->command->queue_,
                i == State::queueIndex ? WebGpuNativeCompletionReceipt::Kind::Queue
                                       : WebGpuNativeCompletionReceipt::Kind::Scope,
                state.slots[i].get(),
                &WebGpuOrdinarySubmission::completeCanonical);
            if (!prepared) {
                diagnostic = "RHI-WEBGPU-CANONICAL-SUBMISSION-RECEIPT-PREPARATION";
                return {};
            }
            receipts[i - State::submissionFirst] = std::move(prepared);
        }
        for (std::size_t i = State::submissionFirst; i < State::lossIndex; ++i)
            state.slots[i]->nativeReceipt = receipts[i - State::submissionFirst].release();
        state.recording = std::move(recording);
        state.submission = std::move(submission);
        state.issuersClaimed = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-CANONICAL-ISSUER-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-CANONICAL-ISSUER-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuCanonicalCallbackCustody::validateOriginalIssuers(
    const WebGpuCanonicalIssuerPreparation& original) const noexcept
{
    using Contract = rhi::OriginalCommandIssuerPreparationContract;
    if (const auto* id = Contract::validateIntent(original.budget_))
        return id;
    if (const auto* id = WebGpuCanonicalCopyPassLowering::validateOriginal(original.lowering_))
        return id;
    if (!core::SharedOwnerProjection::sameOwner(
            original.lowering_->originalCustody()->state_, state_))
        return "RHI-WEBGPU-CANONICAL-ISSUER-CUSTODY";
    if (!state_->issuersClaimed || !state_->recording || !state_->submission)
        return "RHI-WEBGPU-CANONICAL-ISSUER-UNPREPARED";
    const auto* graph = state_->pool.get(state_->ticket);
    const auto& admission = *graph->command->originalAdmission();
    if (const auto* id = rhi::OriginalGeneralOutputPairingContract::validatePair(
            admission.originalBudget().generalOutput, original.budget_.generalOutput))
        return id;
    const auto& lifecycle = admission.originalReservation()->originalSeal()->originalBudget();
    const auto& recording = *state_->recording;
    const auto& submission = *state_->submission;
    if (!recording.hasQuotaBoundSharedOwner(state_->recording)
        || !submission.hasQuotaBoundSharedOwner(state_->submission) || !recording.canonicalMode_
        || !submission.canonicalMode_
        || !core::SharedOwnerProjection::sameOwner(recording.legacyCanonicalState(), state_)
        || !core::SharedOwnerProjection::sameOwner(submission.legacyCanonicalState(), state_)
        || !core::SharedOwnerProjection::sameOwner(submission.recording_, state_->recording)
        || !core::SharedOwnerProjection::sameOwner(recording.command_, graph->command->command_)
        || recording.identity_ != graph->command->identity_ || recording.completion_
        || recording.originalStates_ || recording.originalTransaction_
        || recording.originalTextureTransaction_ || recording.originalReadbackCopies_
        || recording.range_ || recording.attachments_ || recording.originalGraphicsRange_
        || recording.canonicalEvidence_.allReceived() || submission.evidence_.allReceived()
        || submission.callbacksIssued_)
        return "RHI-WEBGPU-CANONICAL-ISSUER-ORIGINAL-BINDING";
    for (const auto& callback : recording.callbacks_)
        if (callback)
            return "RHI-WEBGPU-CANONICAL-ISSUER-EXTRA-CALLBACK";
    for (const auto& callback : submission.callbacks_)
        if (callback)
            return "RHI-WEBGPU-CANONICAL-ISSUER-EXTRA-CALLBACK";
    if (original.owners_
            != original.lowering_->retainedOwnerCount() + Contract::additionalOwnerCount
        || original.owners_ > original.budget_.maxRetainedOwners
        || original.owners_ > admission.originalBudget().maxRetainedOwners
        || original.owners_ > lifecycle.maxRecordingOwners
        || original.owners_ > lifecycle.maxSubmissionOwners
        || original.owners_ > lifecycle.maxCallbackOwners)
        return "RHI-WEBGPU-CANONICAL-ISSUER-OWNER-CAPACITY";
    if (original.budget_.maxRecordingCpuBytes
            != admission.originalRoot()->originalMetadataQuota()->limitBytes()
        || original.budget_.maxSubmissionCpuBytes
            != admission.originalSubmissionQuota()->limitBytes()
        || !core::SharedOwnerProjection::sameOwner(
            recording.originalMetadataQuota(), admission.originalRoot()->originalMetadataQuota())
        || !core::SharedOwnerProjection::sameOwner(
            submission.originalMetadataQuota(), admission.originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            original.originalMetadataQuota(), admission.originalSubmissionQuota()))
        return "RHI-WEBGPU-CANONICAL-ISSUER-QUOTA";
    return nullptr;
}

const char* WebGpuCanonicalIssuerPreparation::validate() const noexcept
{
    return lowering_->originalCustody()->validateOriginalIssuers(*this);
}

bool WebGpuCanonicalIssuerPreparation::issuersClosedForInspection() const noexcept
{
    if (validate())
        return false;
    const auto& state = *lowering_->originalCustody()->state_;
    const auto& recording = state.recording;
    const auto& submission = state.submission;
    if (recording->status() != WebGpuOrdinaryRecording::Status::Prepared
        || recording->hasRecordedCommand() || recording->originalCompletionOwner()
        || recording->readbackRecordingCallbacksIssued()
        || submission->status() != WebGpuOrdinarySubmission::Status::Ready
        || submission->callbacksIssued() || submission->callbacksDrained()
        || submission->queueCompletionObserved())
        return false;
    const char* diagnostic = nullptr;
    const auto ordinary = WebGpuOrdinarySubmission::prepare(recording,
        {1u, true, 1u, WebGpuOrdinarySubmission::callbackCount, budget_.maxSubmissionCpuBytes},
        submission->originalMetadataQuota(),
        diagnostic);
    const auto* recordDiagnostic = WebGpuOrdinaryRecording::record(recording, 1u, 1u, 1u);
    const auto* submitDiagnostic = WebGpuOrdinarySubmission::submit(submission);
    return !ordinary && diagnostic
        && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED"
        && recordDiagnostic
        && std::string_view(recordDiagnostic) == "RHI-WEBGPU-CANONICAL-ISSUER-RECORDING-UNPROVEN"
        && submitDiagnostic
        && std::string_view(submitDiagnostic) == "RHI-WEBGPU-CANONICAL-ISSUER-SUBMISSION-UNPROVEN";
}

const char* WebGpuCanonicalIssuerPreparation::recordOriginal(
    const std::shared_ptr<const WebGpuCanonicalIssuerPreparation>& self,
    const rhi::OriginalCommandScopedRecordingIntent& intent) noexcept
{
    using Contract = rhi::OriginalCommandScopedRecordingContract;
    if (const auto* id = Contract::validateIntent(intent))
        return id;
    const auto decision =
        arp::rhi::semantics::OriginalCommandScopedRecordingLowering::lowering(rhi::Backend::WebGPU);
    if (!decision.recordingAvailable)
        return decision.diagnostic;
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-CANONICAL-ISSUER-OWNER";
    if (const auto* id = rhi::OriginalGeneralOutputPairingContract::validatePair(
            self->budget_.generalOutput, intent.generalOutput))
        return id;
    if (rhi::OriginalGeneralOutputPairingContract::selected(intent.generalOutput)) {
        const auto output = rhi::semantics::OriginalGeneralOutputExecutionLowering::lowering(
            rhi::Backend::WebGPU, intent.generalOutput.contractVersion);
        if (!output.recordingAvailable)
            return output.diagnostic;
    }
    const auto state = self->lowering_->originalCustody()->state_;
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    if (!state->recording)
        return "RHI-WEBGPU-CANONICAL-ISSUER-UNPREPARED";
    if (state->recording->status_ != WebGpuOrdinaryRecording::Status::Prepared)
        return "RHI-WEBGPU-CANONICAL-RECORDING-ONE-SHOT";
    if (const auto* id = self->validate())
        return id;
    state->recordedGeneralOutputVersion = intent.generalOutput.contractVersion;
    return WebGpuOrdinaryRecording::recordCanonical(state->recording, *self);
}

bool WebGpuCanonicalIssuerPreparation::hasOriginalRecordedCommand() const noexcept
{
    const auto& state = *lowering_->originalCustody()->state_;
    return state.recording && state.recording->hasRecordedCommand()
        && !state.recording->diagnostic();
}

const char* WebGpuCanonicalIssuerPreparation::recordingDiagnostic() const noexcept
{
    const auto& state = *lowering_->originalCustody()->state_;
    if (state.terminalDiagnostic)
        return state.terminalDiagnostic;
    return state.recording ? state.recording->canonicalDiagnostic()
                           : "RHI-WEBGPU-CANONICAL-ISSUER-RETIRED";
}

bool WebGpuCanonicalIssuerPreparation::recordingSlotsDrainedForInspection() const noexcept
{
    using S = WebGpuCanonicalCallbackCustody::State;
    const auto& state = *lowering_->originalCustody()->state_;
    for (std::size_t i = S::recordingFirst; i < S::submissionFirst; ++i)
        if (state.slots[i]->gate.phase() != core::runtime::ExternalCallbackGate::Phase::Delivered
            || state.slots[i]->owner)
            return false;
    return true;
}

std::uint32_t WebGpuCanonicalIssuerPreparation::recordingScopesAtLossForInspection() const noexcept
{
    return lowering_->originalCustody()->state_->recordingScopesAtLoss;
}

const char* WebGpuOrdinaryRecording::validateCanonicalLive() const noexcept
{
    if (rootCanonicalMode_)
        return validateRootLive();
    if (typedCanonicalMode_)
        return validateTypedLive();
    using S = WebGpuCanonicalCallbackCustody::State;
    const auto state = legacyCanonicalState();
    if (!state)
        return "RHI-WEBGPU-CANONICAL-ISSUER-RETIRED";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    const auto* graph = state->pool.get(state->ticket);
    if (!canonicalMode_ || !graph || state->recording.get() != this
        || !hasQuotaBoundSharedOwner(state->recording)
        || !core::SharedOwnerProjection::sameOwner(graph->command->command_, command_)
        || identity_ != graph->command->identity_ || completion_ || !state->issuersClaimed
        || !state->loweringClaimed || !state->lowering
        || state->lowering->closure != graph->command->originalAdmission()->originalClosure().get())
        return "RHI-WEBGPU-CANONICAL-ISSUER-ORIGINAL-RECORDING";
    if (!core::SharedOwnerProjection::sameOwner(originalMetadataQuota(),
            graph->command->originalAdmission()->originalRoot()->originalMetadataQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->submissionQuota, graph->command->originalAdmission()->originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->callbackQuota, graph->command->originalAdmission()->originalCallbackQuota()))
        return "RHI-WEBGPU-CANONICAL-ISSUER-QUOTA";
    const auto& loss = *state->slots[S::lossIndex];
    if (loss.gate.phase() != core::runtime::ExternalCallbackGate::Phase::Armed || !loss.nativeState
        || !core::SharedOwnerProjection::sameOwner(loss.owner, state))
        return "RHI-WEBGPU-CANONICAL-RECORDING-LOSS-UNBOUND";
    if (!canonicalIssuanceComplete_ || !canonicalEvidence_.proven()
        || !state->recordingCallbacksDrained())
        return "RHI-WEBGPU-CANONICAL-RECORDING-SCOPES-UNPROVEN";
    if (const auto* id = graph->command->originalAdmission()->originalRoot()->validate())
        return id;
    return WebGpuNativeCommandPreparation::validateCanonicalLive(*graph->command);
}

void WebGpuOrdinaryRecording::rollbackCanonicalRecording() noexcept
{
    if (status_ == Status::Prepared)
        return; // Cold cancellation has issued no plan.
    if (rootCanonicalMode_) {
        const auto state = rootCanonicalState();
        if (!state || !state->foreignCallbacksDrainedExceptLoss()
            || status_ == Status::NativeInvocationUncertain)
            return;
        const auto* graph = state->pool.get(state->ticket);
        if (!graph || state->recording.get() != this || identity_ != graph->command->identity_)
            return;
        graph->command->originalAdmission()
            ->originalReservation()
            ->originalSeal()
            ->rollbackOriginalRecordingPlan();
        if (status_ != Status::DeviceLost)
            status_ = Status::Rejected;
        return;
    }
    if (typedCanonicalMode_) {
        const auto state = typedCanonicalState();
        if (!state || !state->foreignCallbacksDrainedExceptLoss()
            || status_ == Status::NativeInvocationUncertain)
            return;
        const auto* graph = state->pool.get(state->ticket);
        if (!graph || state->recording.get() != this || identity_ != graph->command->identity_)
            return;
        graph->command->originalAdmission()
            ->originalReservation()
            ->originalSeal()
            ->rollbackOriginalRecordingPlan();
        if (status_ != Status::DeviceLost)
            status_ = Status::Rejected;
        return;
    }
    const auto state = legacyCanonicalState();
    if (!state || !state->foreignCallbacksDrainedExceptLoss())
        return;
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || state->recording.get() != this || identity_ != graph->command->identity_)
        return;
    graph->command->originalAdmission()
        ->originalReservation()
        ->originalSeal()
        ->rollbackOriginalRecordingPlan();
    if (status_ != Status::DeviceLost)
        status_ = Status::Rejected;
}

void WebGpuOrdinaryRecording::finishCanonicalRecording() noexcept
{
    const auto run = [&](const auto& state) noexcept {
        if (!state || !canonicalIssuanceComplete_ || !canonicalEvidence_.allReceived())
            return;
        if (state->pool.retainedInvalidated(state->ticket)) {
            if (status_ != Status::NativeInvocationUncertain)
                status_ = Status::DeviceLost;
            state->releaseTerminalIfDrained();
            return;
        }
        if (status_ != Status::RecordingPending)
            return;
        if (state->cancellationRequested && !failure_)
            failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-CANCELLED";
        if (!failure_)
            failure_ = validateCanonicalLive();
        if (failure_) {
            // State::cancel suppresses only loss userdata; all issued scopes have
            // already cleared ownership and entered Delivered before rollback.
            status_ = Status::Rejected;
            if (const auto* id = state->cancel(failure_))
                state->terminalDiagnostic = id;
            return;
        }
        status_ = Status::Recorded;
    };
    if (rootCanonicalMode_)
        run(rootCanonicalState());
    else if (typedCanonicalMode_)
        run(typedCanonicalState());
    else
        run(legacyCanonicalState());
}

const char* WebGpuOrdinaryRecording::recordCanonical(std::shared_ptr<WebGpuOrdinaryRecording> self,
    const WebGpuCanonicalIssuerPreparation& original) noexcept
{
    using S = WebGpuCanonicalCallbackCustody::State;
    const auto state = self->legacyCanonicalState();
    if (!state || state->recording != self)
        return "RHI-WEBGPU-CANONICAL-ISSUER-ORIGINAL-RECORDING";
    if (const auto* id = self->validateCanonicalCold())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    (void)original;
    return "RHI-WEBGPU-CANONICAL-RECORDING-NATIVE-UNAVAILABLE";
#else
    const auto cold = state->pool.get(state->ticket)->command;
    const auto& seal = cold->originalAdmission()->originalReservation()->originalSeal();
    auto& native = *self->command_->impl_;
    const auto& projection = *state->lowering;
    if (const auto* id = projection.validate(original.lowering_->budget_))
        return id;
    if (const auto* id = seal->applyOriginalRecordingPlan())
        return id;
    self->status_ = Status::RecordingPending;
    if (projection.joins)
        if (const auto* id = projection.joins->validateRetained(*seal)) {
            self->failure_ = id;
            seal->rollbackOriginalRecordingPlan();
            self->status_ = Status::Rejected;
            return id;
        }
    if (projection.readbackJoins)
        if (const auto* id = projection.readbackJoins->validateRetained(seal)) {
            self->failure_ = id;
            seal->rollbackOriginalRecordingPlan();
            self->status_ = Status::Rejected;
            return id;
        }
    // Slots are armed before any pop can deliver synchronously. Native loss
    // does not detach them. The same non-recycled State ticket retains the root.
    for (std::size_t i = S::recordingFirst; i < S::submissionFirst; ++i) {
        auto& slot = *state->slots[i];
        slot.owner = state;
        slot.gate.arm();
    }
    WebGpuNativeErrorScopeBatch::push(native.device);
    auto encoder = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    encoder.label = webGpuNativeStringView(original.lowering_->budget_.contractVersion == 1u
            ? "RHI canonical CopyPass/1 encoder"
            : original.lowering_->budget_.contractVersion == 2u
            ? "RHI canonical OrderedCopyAttachment/1 encoder"
            : original.lowering_->budget_.contractVersion == 3u
            ? "RHI canonical OrderedOriginalUpload/1 encoder"
            : "RHI canonical OrderedOriginalReadback/1 encoder");
    native.encoder = wgpuDeviceCreateCommandEncoder(native.device, &encoder);
    if (!native.encoder)
        self->failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-ENCODER";
    for (const auto& ordered : projection.operations.values()) {
        if (self->failure_)
            break;
        const auto& originalOperation = ordered.original;
        using K = rhi::CommandUsageKind;
        switch (originalOperation.kind) {
        case K::BufferCopy:
        case K::BufferToTextureCopy:
        case K::TextureToBufferCopy:
        case K::TextureToTextureCopy:
            for (std::uint64_t i = 0u; i < originalOperation.elementCount && !self->failure_; ++i) {
                const auto& operation =
                    projection.copies
                        .values()[static_cast<std::size_t>(originalOperation.firstElement + i)];
                const auto& source = operation.source->backing();
                const auto& destination = operation.destination->backing();
                const char* diagnostic = nullptr;
                const auto success = std::visit(
                    [&](const auto& desc) noexcept {
                        using D = std::decay_t<decltype(desc)>;
                        if constexpr (std::is_same_v<D, rhi::BufferCopyDesc>)
                            return WebGpuNativeTransferCopyLowering::emit(native.encoder,
                                *source.buffer,
                                *destination.buffer,
                                desc,
                                diagnostic);
                        else if constexpr (std::is_same_v<D, rhi::BufferToTextureCopyDesc>)
                            return WebGpuNativeTransferCopyLowering::emit(native.encoder,
                                *source.buffer,
                                *destination.texture,
                                desc,
                                diagnostic);
                        else if constexpr (std::is_same_v<D, rhi::TextureToBufferCopyDesc>)
                            return WebGpuNativeTransferCopyLowering::emit(native.encoder,
                                *source.texture,
                                *destination.buffer,
                                desc,
                                diagnostic);
                        else
                            return WebGpuNativeTransferCopyLowering::emit(native.encoder,
                                *source.texture,
                                *destination.texture,
                                desc,
                                diagnostic);
                    },
                    operation.copy);
                if (!success)
                    self->failure_ =
                        diagnostic ? diagnostic : "RHI-WEBGPU-CANONICAL-RECORDING-COPY";
            }
            break;
        case K::BufferUpload:
        case K::TextureUpload:
            for (std::size_t i = 0u; i < ordered.uploadCount && !self->failure_; ++i) {
                const auto& upload = projection.uploads.values()[ordered.uploadFirst + i];
                if (upload.command != originalOperation.commandIndex
                    || upload.join != ordered.joinIndex) {
                    self->failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-UPLOAD-ORDINAL";
                    break;
                }
                const auto& source = upload.copy.source->backing();
                const auto& destination = upload.copy.destination->backing();
                const char* diagnostic = nullptr;
                const bool emitted = std::visit(
                    [&](const auto& desc) noexcept {
                        using D = std::decay_t<decltype(desc)>;
                        if constexpr (std::is_same_v<D, rhi::BufferCopyDesc>)
                            return WebGpuNativeTransferCopyLowering::emit(native.encoder,
                                *source.buffer,
                                *destination.buffer,
                                desc,
                                diagnostic);
                        else if constexpr (std::is_same_v<D, rhi::BufferToTextureCopyDesc>)
                            return WebGpuNativeTransferCopyLowering::emitOriginalContentUpload(
                                native.encoder,
                                *source.buffer,
                                *destination.texture,
                                desc,
                                diagnostic);
                        else {
                            diagnostic = "RHI-WEBGPU-CANONICAL-RECORDING-UPLOAD-KIND";
                            return false;
                        }
                    },
                    upload.copy.copy);
                if (!emitted)
                    self->failure_ =
                        diagnostic ? diagnostic : "RHI-WEBGPU-CANONICAL-RECORDING-UPLOAD";
            }
            break;
        case K::TextureReadback:
            for (std::size_t i = 0u; i < ordered.readbackCount && !self->failure_; ++i) {
                const auto& readback = projection.readbacks.values()[ordered.readbackFirst + i];
                if (readback.command != originalOperation.commandIndex
                    || readback.join != ordered.readbackJoin) {
                    self->failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-READBACK-ORDINAL";
                    break;
                }
                const auto* desc = std::get_if<rhi::TextureToBufferCopyDesc>(&readback.copy.copy);
                if (!desc) {
                    self->failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-READBACK-KIND";
                    break;
                }
                const auto& source = readback.copy.source->backing();
                const auto& destination = readback.copy.destination->backing();
                if (original.lowering_->budget_.generalOutput.contractVersion == 3u) {
                    const auto index = ordered.readbackFirst + i;
                    const auto& joins = projection.readbackJoins;
                    if (!projection.closure->hasAttachmentReadbackProfile() || !joins
                        || joins->joinContractVersion() != 4u
                        || index >= joins->copies_.values().size()
                        || joins->copies_.values()[index].commandIndex
                            != originalOperation.commandIndex) {
                        self->failure_ =
                            "RHI-WEBGPU-CANONICAL-RECORDING-ATTACHMENT-READBACK-IDENTITY";
                        break;
                    }
                    self->failure_ =
                        WebGpuNativeOriginalTextureReadbackLowering::emitFrozen(native.encoder,
                            *source.texture,
                            *destination.buffer,
                            joins->copies_.values()[index].decision);
                    continue;
                }
                const char* diagnostic = nullptr;
                if (!WebGpuNativeTransferCopyLowering::emit(
                        native.encoder, *source.texture, *destination.buffer, *desc, diagnostic))
                    self->failure_ =
                        diagnostic ? diagnostic : "RHI-WEBGPU-CANONICAL-RECORDING-READBACK";
            }
            break;
        case K::BeginRenderPass:
            native.renderPass = wgpuCommandEncoderBeginRenderPass(
                native.encoder, &projection.passes.values()[ordered.passIndex].renderPass);
            if (!native.renderPass)
                self->failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-PASS";
            break;
        case K::EndRenderPass:
            if (!native.renderPass)
                self->failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-PASS-ORDER";
            else {
                wgpuRenderPassEncoderEnd(native.renderPass);
                wgpuRenderPassEncoderRelease(native.renderPass);
                native.renderPass = nullptr;
            }
            break;
        case K::Finish: {
            auto buffer = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            buffer.label = webGpuNativeStringView("RHI canonical copy/attachment command buffer");
            native.commandBuffer = wgpuCommandEncoderFinish(native.encoder, &buffer);
            native.finished = true;
            if (!native.commandBuffer)
                self->failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-FINISH";
            break;
        }
        case K::BeginTransferPass:
        case K::EndTransferPass:
        case K::TextureBarrier:
        case K::BufferBarrier:
            break; // Versioned grouping / normative automatic ordered usage.
        default:
            self->failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-OPERATION-UNPROVEN";
            break;
        }
    }
    if (native.renderPass) {
        wgpuRenderPassEncoderEnd(native.renderPass);
        wgpuRenderPassEncoderRelease(native.renderPass);
        native.renderPass = nullptr;
    }
    for (std::size_t i = S::recordingFirst; i < S::submissionFirst; ++i) {
        auto callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.callback = &completeCanonical;
        callback.userdata1 = state->slots[i].get();
        (void)wgpuDevicePopErrorScope(native.device, callback);
    }
    self->canonicalIssuanceComplete_ = true;
    self->finishCanonicalRecording();
    return self->failure_; // Pending success still grants no queue authority.
#endif
}
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
void WebGpuOrdinaryRecording::completeCanonical(
    WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView, void* data, void*) noexcept
{
    using S = WebGpuCanonicalCallbackCustody::State;
    auto* slot = static_cast<S::Slot*>(data);
    if (!slot || slot->role != S::Role::RecordingScope || slot->index >= S::submissionFirst
        || !slot->gate.beginDelivery())
        return;
    const auto state = slot->owner;
    const auto self = state ? state->recording : nullptr;
    if (state && self && state->slots[slot->index].get() == slot
        && slot->ticket.index == state->ticket.index
        && slot->ticket.generation == state->ticket.generation
        && core::SharedOwnerProjection::sameOwner(self->legacyCanonicalState(), state)) {
        const auto* graph = state->pool.get(slot->ticket);
        const auto* identityDiagnostic = graph
            ? WebGpuNativeCommandPreparation::validateCanonicalIdentity(*graph->command, true)
            : "RHI-WEBGPU-CANONICAL-RECORDING-TICKET";
        if (identityDiagnostic && !self->failure_)
            self->failure_ = identityDiagnostic;
        const bool success = !identityDiagnostic && status == WGPUPopErrorScopeStatus_Success
            && type == WGPUErrorType_NoError;
        if (!self->canonicalEvidence_.observe(
                static_cast<std::uint32_t>(slot->index - S::recordingFirst), success))
            self->failure_ = "RHI-WEBGPU-CANONICAL-RECORDING-SCOPE-DUPLICATE";
        else if (!success && !self->failure_)
            self->failure_ = status == WGPUPopErrorScopeStatus_Success
                ? "RHI-WEBGPU-CANONICAL-RECORDING-NATIVE-ERROR"
                : "RHI-WEBGPU-CANONICAL-RECORDING-SCOPE-FAILED";
    }
    slot->gate.finishDelivery();
    slot->owner.reset();
    if (self)
        self->finishCanonicalRecording();
    if (state)
        state->releaseTerminalIfDrained();
}
#endif

const char* WebGpuCanonicalIssuerPreparation::submitOriginal(
    const std::shared_ptr<const WebGpuCanonicalIssuerPreparation>& self,
    const rhi::OriginalCommandQueueSubmissionIntent& intent) noexcept
{
    using Contract = rhi::OriginalCommandQueueSubmissionContract;
    if (const auto* id = Contract::validateIntent(intent))
        return id;
    const auto decision =
        arp::rhi::semantics::OriginalCommandQueueSubmissionLowering::lowering(rhi::Backend::WebGPU);
    if (!decision.submissionAvailable || !decision.completionCommitAvailable)
        return decision.diagnostic;
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-CANONICAL-ISSUER-OWNER";
    const auto state = self->lowering_->originalCustody()->state_;
    if (rhi::OriginalGeneralOutputPairingContract::selected(self->budget_.generalOutput)
        || rhi::OriginalGeneralOutputPairingContract::selected(intent.generalOutput)
        || state->recordedGeneralOutputVersion != UINT32_MAX)
        if (const auto* id = rhi::OriginalGeneralOutputPairingContract::validateRecorded(
                self->budget_.generalOutput,
                state->recordedGeneralOutputVersion,
                intent.generalOutput))
            return id;
    if (rhi::OriginalGeneralOutputPairingContract::selected(intent.generalOutput)) {
        const auto output = rhi::semantics::OriginalGeneralOutputExecutionLowering::lowering(
            rhi::Backend::WebGPU, intent.generalOutput.contractVersion);
        if (!output.submissionAvailable)
            return output.diagnostic;
    }
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    if (state->maySubmit || state->submissionCompleted)
        return "RHI-WEBGPU-CANONICAL-SUBMISSION-ONE-SHOT";
    if (!state->submission)
        return "RHI-WEBGPU-CANONICAL-ISSUER-UNPREPARED";
    return WebGpuOrdinarySubmission::submitCanonical(state->submission);
}

bool WebGpuCanonicalIssuerPreparation::hasOriginalCompletedSubmission() const noexcept
{
    return lowering_->originalCustody()->state_->submissionCompleted;
}

const char* WebGpuCanonicalIssuerPreparation::validateOriginalCompletedShaderTextureInput(
    const std::shared_ptr<const WebGpuCanonicalIssuerPreparation>& self,
    const std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource>& source,
    const rhi::GpuTexture::ContentExpectation& completed,
    const rhi::OriginalCompletedShaderTextureInputContract::Budget& budget,
    const std::shared_ptr<const rhi::OriginalCommandStateTransaction>& originalRoot) noexcept
{
    using Contract = rhi::OriginalCompletedShaderTextureInputContract;
    if (const auto* id = Contract::validateIntent(budget))
        return id;
    const auto decision =
        rhi::semantics::OriginalCompletedShaderTextureInputLowering::lowering(rhi::Backend::WebGPU);
    if (!decision.privateInputValidationAvailable)
        return decision.diagnostic;
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-COMPLETED-SHADER-TEXTURE-ISSUER-OWNER";
    if (!self->hasOriginalCompletedSubmission() || self->submissionDiagnostic()
        || !self->lowering_->originalCustody()->state_->foreignCallbacksDrainedExceptLoss())
        return "RHI-COMPLETED-SHADER-TEXTURE-ISSUER-PENDING";
    const auto root = self->originalRoot_.lock();
    if (!root || root->status() != rhi::OriginalCommandStateTransaction::Status::Committed
        || !root->journalEmpty())
        return "RHI-COMPLETED-SHADER-TEXTURE-ORIGINAL-ROOT";
    if (originalRoot && !core::SharedOwnerProjection::sameOwner(originalRoot, root))
        return "RHI-COMPLETED-SHADER-TEXTURE-ORIGINAL-ROOT";
    if (const auto* id = WebGpuCanonicalTextureContentUploadSource::validateCompletedInputFacts(
            source, completed))
        return id;
    const auto& staging = *source->originalStaging();
    const auto& packing = staging.originalPackingSource();
    const auto& target = *packing->originalDestinationLease()->originalObject();
    const auto& usage =
        *root->originalGraph()->originalInputs()->originalScalars()->originalUsage();
    if (usage.commands().size() > budget.maxJoinVisits)
        return "RHI-COMPLETED-SHADER-TEXTURE-CAPACITY";
    const auto& inputs = usage.originalSources()->sources();
    std::uint64_t uploads = 0u;
    for (const auto& command : usage.commands()) {
        if (command.kind != rhi::CommandUsageKind::TextureUpload)
            continue;
        if (command.referenceCount != 2u || command.firstReference >= usage.references().size()
            || usage.references().size() - command.firstReference < 2u)
            return "RHI-COMPLETED-SHADER-TEXTURE-ORIGINAL-JOIN";
        const auto parentIndex = usage.references()[command.firstReference].sourceIndex;
        const auto uploadIndex = usage.references()[command.firstReference + 1u].sourceIndex;
        if (parentIndex >= inputs.size() || uploadIndex >= inputs.size())
            return "RHI-COMPLETED-SHADER-TEXTURE-ORIGINAL-JOIN";
        const auto* parent = std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
            &inputs[parentIndex]);
        const auto* upload =
            std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureUploadSource>>(
                &inputs[uploadIndex]);
        if (!parent || !upload
            || !core::SharedOwnerProjection::sameOwner(*parent, packing->originalDestinationLease())
            || !core::SharedOwnerProjection::sameOwner(*upload, packing->originalSource()))
            return "RHI-COMPLETED-SHADER-TEXTURE-ORIGINAL-JOIN";
        ++uploads;
    }
    if (uploads != root->plannedContentUploadCount())
        return "RHI-COMPLETED-SHADER-TEXTURE-ORIGINAL-JOIN";
    if (target.desc().mipLevelCount > budget.maxMipLevels)
        return "RHI-COMPLETED-SHADER-TEXTURE-CAPACITY";
    std::uint64_t texelBytes = 0u;
    if (const auto* id = rhi::semantics::TextureUploadDestinationValidation::footprint(
            target.desc(), texelBytes))
        return id;
    return Contract::inspect(target.desc(),
        budget,
        uploads,
        usage.commands().size(),
        texelBytes,
        source->destinationContentExpectation().contentGeneration(),
        source->destinationContentExpectation().revision(),
        completed.contentGeneration(),
        completed.revision(),
        true);
}

std::uint64_t WebGpuCanonicalNativeClosure::completedTextureOwnerBound(
    const ShaderTextureContentInput& input) noexcept
{
    return input.issuer && input.issuer->hasQuotaBoundSharedOwner(input.issuer)
        ? std::uint64_t{input.issuer->retainedOwnerCount()}
            + WebGpuCanonicalTextureContentUploadSource::retainedOwnerBound + 1u
        : 0u;
}

const char* WebGpuCanonicalNativeClosure::validateCompletedDepthTextureInput(
    const rhi::OriginalCompletedDepthReadInputContract::Input& input,
    const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& view,
    const rhi::OriginalCompletedDepthReadInputContract::Budget& budget) noexcept
{
    const auto inspected =
        rhi::OriginalCompletedDepthReadInputContract::inspect(input, view, budget);
    if (inspected.diagnostic)
        return inspected.diagnostic;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-COMPLETED-DEPTH-READ-INPUT-NATIVE-UNAVAILABLE";
#else
    if (!webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost())
        return "RHI-COMPLETED-DEPTH-READ-INPUT-NATIVE-LOST";
    const auto& object = *view->originalObject();
    const auto& texture = *view->originalParentLease()->originalObject();
    WebGpuOrdinaryResourceObservation registered;
    if (const auto* id = observeOriginalWebGpuResource(object, registered))
        return id;
    const auto nativeView = webGpuNativePayloadFor(object);
    const auto parent = webGpuNativePayloadFor(texture);
    if (!nativeView || !parent || !nativeView->view || !parent->texture
        || !core::SharedOwnerProjection::sameOwner(nativeView, registered.view())
        || !core::SharedOwnerProjection::sameOwner(parent, registered.backing().texture)
        || !core::SharedOwnerProjection::sameOwner(nativeView->parentTexture, parent)
        || !nativeView->matchesDescription(object.desc())
        || !parent->contentAccessPolicy().originalAttachmentExclusive())
        return "RHI-COMPLETED-DEPTH-READ-INPUT-NATIVE-IDENTITY";
    return WebGpuNativeTextureFactory::validateOriginalAttachment(*parent, texture.desc());
#endif
}

const char* WebGpuCanonicalNativeClosure::validateCompletedTextureInput(
    const ShaderTextureContentInput& input,
    const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& view,
    const Budget::ShaderTextureContentCustody& budget) noexcept
{
    if (const auto* id = budget.diagnostic())
        return id;
    if (!input.uploadRoot || !input.completed)
        return "RHI-WEBGPU-GENERAL-TEXTURE-COMPLETED-INPUT";
    const rhi::OriginalCompletedShaderTextureInputContract::Budget completed{1u,
        true,
        budget.maxUploads,
        budget.maxMipLevels,
        budget.maxInitializedBytes,
        budget.maxJoinVisits};
    if (const auto* id =
            WebGpuCanonicalIssuerPreparation::validateOriginalCompletedShaderTextureInput(
                input.issuer, input.source, *input.completed, completed, input.uploadRoot))
        return id;
    if (const auto* id = rhi::OriginalCommandTextureViewSource::validateOriginal(view))
        return id;
    const auto& staging = *input.source->originalStaging();
    const auto& packing = *staging.originalPackingSource();
    if (!core::SharedOwnerProjection::sameOwner(
            view->originalParentLease(), packing.originalDestinationLease()))
        return "RHI-WEBGPU-GENERAL-TEXTURE-VIEW-PARENT";
    const auto& usage =
        *input.uploadRoot->originalGraph()->originalInputs()->originalScalars()->originalUsage();
    const auto& sources = usage.originalSources()->sources();
    // Exactly the initializer's target, upload source and initialized staging
    // buffer are retained. Unrelated initializer work cannot hide allocations.
    if (sources.size() != 3u
        || usage.commands().size() != input.uploadRoot->plannedContentUploadCount() + 3u)
        return "RHI-WEBGPU-GENERAL-TEXTURE-UPLOAD-DOMAIN";
    const auto* parent =
        std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(&sources[0]);
    const auto* upload =
        std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureUploadSource>>(&sources[1]);
    const auto* buffer =
        std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(&sources[2]);
    if (!parent || !upload || !buffer
        || !core::SharedOwnerProjection::sameOwner(*parent, packing.originalDestinationLease())
        || !core::SharedOwnerProjection::sameOwner(*upload, packing.originalSource())
        || !core::SharedOwnerProjection::sameOwner(
            *buffer, staging.initializedBuffer()->originalLease()))
        return "RHI-WEBGPU-GENERAL-TEXTURE-UPLOAD-DOMAIN";
    if (usage.commands()[0u].kind != rhi::CommandUsageKind::BeginTransferPass
        || usage.commands()[usage.commands().size() - 2u].kind
            != rhi::CommandUsageKind::EndTransferPass
        || usage.commands()[usage.commands().size() - 1u].kind != rhi::CommandUsageKind::Finish)
        return "RHI-WEBGPU-GENERAL-TEXTURE-UPLOAD-DOMAIN";
    const auto owners = completedTextureOwnerBound(input);
    if (!owners || owners > budget.maxRetainedOwners)
        return "RHI-WEBGPU-GENERAL-TEXTURE-OWNER-CAPACITY";
    return rhi::OriginalCompletedShaderTextureInputContract::inspectViewBinding(
        view->originalParentLease()->originalObject()->desc(),
        view->originalObject()->desc(),
        input.shaderState,
        input.stages,
        input.access);
}

const char* WebGpuCanonicalIssuerPreparation::submissionDiagnostic() const noexcept
{
    const auto& state = *lowering_->originalCustody()->state_;
    if (state.submissionCompleted)
        return nullptr;
    if (state.terminalDiagnostic)
        return state.terminalDiagnostic;
    return state.submission ? state.submission->canonicalDiagnostic()
                            : "RHI-WEBGPU-CANONICAL-ISSUER-RETIRED";
}

std::uint32_t WebGpuCanonicalIssuerPreparation::submissionSlotsAtLossForInspection() const noexcept
{
    return lowering_->originalCustody()->state_->submissionSlotsAtLoss;
}

bool WebGpuCanonicalIssuerPreparation::submissionSlotsDrainedForInspection() const noexcept
{
    return lowering_->originalCustody()->state_->foreignCallbacksDrainedExceptLoss();
}

const char* WebGpuOrdinarySubmission::submitCanonical(
    std::shared_ptr<WebGpuOrdinarySubmission> self) noexcept
{
    const auto run = [&](const auto& state) noexcept -> const char* {
        using S = typename std::decay_t<decltype(state)>::element_type;
        if (!state || state->submission != self || !self->hasQuotaBoundSharedOwner(self)
            || !self->canonicalMode_
            || !core::SharedOwnerProjection::sameOwner(self->recording_, state->recording))
            return "RHI-WEBGPU-CANONICAL-ISSUER-ORIGINAL-SUBMISSION";
        if (self->status_ != Status::Ready || self->callbacksIssued_ || state->maySubmit)
            return "RHI-WEBGPU-CANONICAL-SUBMISSION-ONE-SHOT";
        if (!state->recording->hasRecordedCommand())
            return "RHI-WEBGPU-CANONICAL-SUBMISSION-UNRECORDED";
        if (const auto* id = state->recording->validateCanonicalLive())
            return id;
        const auto* graph = state->pool.get(state->ticket);
        if (!graph)
            return "RHI-WEBGPU-CANONICAL-SUBMISSION-TICKET";
        const auto cold = graph->command;
        if (!core::SharedOwnerProjection::sameOwner(self->originalMetadataQuota(),
                cold->originalAdmission()
                    ->originalReservation()
                    ->originalSeal()
                    ->originalSubmissionQuota()))
            return "RHI-WEBGPU-CANONICAL-ISSUER-QUOTA";
        for (std::size_t i = S::submissionFirst; i < S::lossIndex; ++i) {
            const auto& slot = *state->slots[i];
            if (slot.index != i || slot.ticket.index != state->ticket.index
                || slot.ticket.generation != state->ticket.generation
                || slot.role
                    != (i == S::queueIndex ? S::Role::QueueCompletion : S::Role::SubmissionScope)
                || slot.owner
                || slot.gate.phase() != core::runtime::ExternalCallbackGate::Phase::Unarmed
                || !WebGpuNativeCompletionReceipt::prepared(slot.nativeReceipt))
                return "RHI-WEBGPU-CANONICAL-SUBMISSION-SLOT";
        }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-WEBGPU-CANONICAL-SUBMISSION-NATIVE-UNAVAILABLE";
#else
        auto& native = *cold->command_->impl_;
        if (native.submitted)
            return "RHI-WEBGPU-CANONICAL-SUBMISSION-ONE-SHOT";
        if (!state->pool.submit(state->ticket))
            return "RHI-WEBGPU-CANONICAL-SUBMISSION-TICKET";
        // MaySubmit is published BEFORE crossing any potentially throwing native API.
        state->maySubmit = true;
        native.submitted = true;
        self->status_ = Status::Pending;
        self->callbacksIssued_ = true;
        for (std::size_t i = S::submissionFirst; i < S::lossIndex; ++i) {
            auto& slot = *state->slots[i];
            slot.owner = state;
            slot.gate.arm();
        }
        const bool pushed = WebGpuNativeCompletionReceipt::pushScopes(native.device);
        if (pushed)
            self->canonicalInvocationProven_ =
                WebGpuNativeCompletionReceipt::invoke(native.queue, native.commandBuffer);
        if (!self->canonicalInvocationProven_)
            state->quarantine(pushed ? "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN"
                                     : "RHI-WEBGPU-CANONICAL-SUBMISSION-SCOPE-PUSH-UNCERTAIN");
        for (std::size_t i = S::submissionFirst; i < S::lossIndex; ++i) {
            auto& slot = *state->slots[i];
            // A partial push leaves stack effects unknown. Pop no guessed scopes;
            // physical loss is the only release proof for that uncertain boundary.
            const bool bound = pushed && WebGpuNativeCompletionReceipt::bind(slot.nativeReceipt);
            if (!bound) {
                WebGpuNativeCompletionReceipt::release(slot.nativeReceipt);
                slot.nativeReceipt = 0u;
                slot.gate.detach();
                slot.owner.reset();
                const auto participant = i == S::queueIndex
                    ? 0u
                    : static_cast<std::uint32_t>(i - S::submissionFirst + 1u);
                self->evidence_.observe(participant, false);
                state->quarantine("RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
            }
        }
        self->canonicalIssuanceComplete_ = true;
        self->finishCanonicalSubmission();
        return state->terminalDiagnostic;
#endif
    };
    return self->rootCanonicalMode_ ? run(self->rootCanonicalState())
        : self->typedCanonicalMode_ ? run(self->typedCanonicalState())
                                    : run(self->legacyCanonicalState());
}

void WebGpuOrdinarySubmission::completeCanonical(
    void* userdata, WebGpuNativeCompletionReceipt::Outcome outcome) noexcept
{
    using S = WebGpuCanonicalCallbackCustody::State;
    auto* slot = static_cast<S::Slot*>(userdata);
    if (!slot || slot->index < S::submissionFirst || slot->index >= S::lossIndex
        || !slot->gate.beginDelivery())
        return;
    const auto state = slot->owner;
    const auto self = state ? state->submission : nullptr;
    if (state && self && state->slots[slot->index].get() == slot
        && slot->ticket.index == state->ticket.index
        && slot->ticket.generation == state->ticket.generation
        && core::SharedOwnerProjection::sameOwner(self->legacyCanonicalState(), state)) {
        const bool queue = slot->index == S::queueIndex;
        if (queue)
            state->queueNativeReceiptReceived = true; // Genuine delivery only.
        const auto participant =
            queue ? 0u : static_cast<std::uint32_t>(slot->index - S::submissionFirst + 1u);
        // Logical Shutdown closes original lifetime tickets synchronously;
        // the first private issuer observation invalidates the pending journal
        // while every outstanding foreign receipt keeps the shell retained.
        if (!state->terminalDiagnostic)
            if (const auto* id = state->recording->validateCanonicalLive())
                state->quarantine(id);
        const bool success = outcome == WebGpuNativeCompletionReceipt::Outcome::Success;
        if (!self->evidence_.observe(participant, success))
            state->quarantine("RHI-WEBGPU-CANONICAL-SUBMISSION-RECEIPT-DUPLICATE");
        else if (!success)
            state->quarantine("RHI-WEBGPU-CANONICAL-SUBMISSION-NATIVE-ERROR");
    }
    slot->gate.finishDelivery();
    slot->owner.reset();
    if (self)
        self->finishCanonicalSubmission();
    if (state)
        state->releaseTerminalIfDrained();
}

void WebGpuOrdinarySubmission::finishCanonicalSubmission() noexcept
{
    const auto run = [&](const auto& state) noexcept {
        using S = typename std::decay_t<decltype(state)>::element_type;
        if (!state || state->mapIssuanceActive || state->mapDrainActive
            || !canonicalIssuanceComplete_ || !evidence_.allReceived()
            || !state->foreignCallbacksDrainedExceptLoss())
            return;
        if (state->pool.retainedInvalidated(state->ticket)) {
            status_ = Status::Unproven;
            state->releaseTerminalIfDrained();
            return;
        }
        if (status_ != Status::Pending)
            return;
        if (!canonicalInvocationProven_ || !evidence_.proven()
            || !state->queueNativeReceiptReceived) {
            state->quarantine("RHI-WEBGPU-CANONICAL-SUBMISSION-EVIDENCE-UNPROVEN");
            return;
        }
        const auto* graph = state->pool.get(state->ticket);
        if (!state->submissionProven)
            if (const auto* id = state->recording->validateCanonicalLive()) {
                state->quarantine(id);
                return;
            }
        if (!graph || !state->maySubmit) {
            state->quarantine("RHI-WEBGPU-CANONICAL-SUBMISSION-TICKET");
            return;
        }
        // Keep the exact root/seal until commit and loss userdata suppression finish.
        const auto cold = graph->command;
        const auto seal = cold->originalAdmission()->originalReservation()->originalSeal();
        if (state->results) {
            if (!state->submissionProven) {
                state->submissionProven = true;
                state->advanceMaps();
                return;
            }
            const auto decision = state->mapDrainDecision();
            using Action = rhi::OriginalCommandReadbackMapDrainContract::Action;
            if (decision.action == Action::RetainOriginal)
                return;
            if (decision.action != Action::RetireUnpublished) {
                state->quarantine(decision.diagnostic ? decision.diagnostic
                                                      : "RHI-WEBGPU-CANONICAL-MAP-DRAIN-UNPROVEN");
                return;
            }
            if (const auto* id = state->validateRetainedResults(seal)) {
                state->quarantine(id);
                return;
            }
        }
        if (!state->detachLoss()) {
            state->quarantine("RHI-WEBGPU-CANONICAL-SUBMISSION-LOSS-DETACH");
            return;
        }
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
        using Probe = arp::test::WebGpuPostSuppressionFailureProbeFixture;
        if (state->postSuppressionProbe.failure != Probe::Failure::None) {
            assert(state->postSuppressionProbe.suppressionAttempts == 1u
                && state->slots[S::lossIndex]->gate.phase()
                    == core::runtime::ExternalCallbackGate::Phase::Detached
                && !state->slots[S::lossIndex]->owner && state->queueNativeReceiptReceived
                && state->foreignCallbacksDrainedExceptLoss());
            state->postSuppressionProbe.commitAttempted = true;
            if constexpr (S::typed) {
                if (state->postSuppressionProbe.failure == Probe::Failure::Commit) {
                    assert(!state->results && state->recordedGeneralSubmissionVersion == 2u);
                    state->postSuppressionProbe.injectedAfterSuppression = true;
                    state->recordedGeneralSubmissionVersion = 1u;
                }
            }
        }
#endif
        const auto commit = [&]() noexcept -> const char* {
            if constexpr (S::bindlessRoot) {
                if (!state->results
                    || state->recordedReadbackCompletionVersion
                        != graph->command->originalAdmission()
                            ->originalClosure()
                            ->originalRoot()
                            ->originalSnapshot()
                            ->plan()
                            .originalExecutionContractVersion)
                    return "RHI-ORIGINAL-ROOT-EXECUTION-COMPLETION";
                return seal->template commitOriginalRootReadbackSubmission<
                    rhi::OriginalCommandReadbackMapDrainContract>(
                    state->results->readbackMapRequirements(), state->mapDrainFacts());
            }
            if constexpr (S::typed) {
                if (state->results) {
                    if (state->recordedReadbackCompletionVersion != 1u)
                        return "RHI-TYPED-NATIVE-READBACK-COMPLETION-REQUIRED";
                    return seal->template commitOriginalReadbackSubmission<
                        rhi::OriginalCommandReadbackMapDrainContract>(
                        state->results->readbackMapRequirements(), state->mapDrainFacts());
                }
                return seal->commitOriginalSubmission(state->recordedGeneralSubmissionVersion);
            }
            // Output3 keeps attachment content in ContentPlan3. Only the
            // authenticated map/unmap drain may commit that reader journal.
            // Legacy storage-output and ordinary submission profiles retain
            // their existing completion boundary.
            if (state->results && state->recordedGeneralOutputVersion == 3u)
                return seal->template commitOriginalReadbackSubmission<
                    rhi::OriginalCommandReadbackMapDrainContract>(
                    state->results->readbackMapRequirements(), state->mapDrainFacts());
            return seal->commitOriginalSubmission();
        };
        if (const auto* id = commit()) {
            state->quarantine(id);
            return;
        }
        if (state->results)
            if (const auto* id = state->publishCommittedResults(seal)) {
                state->quarantine(id);
                return;
            }
        status_ = Status::Completed;
        state->submissionCompleted = true;
        state->submission.reset();
        state->recording.reset();
        state->lowering.reset();
        state->pool.resolve(state->ticket, true);
    };
    if (rootCanonicalMode_)
        run(rootCanonicalState());
    else if (typedCanonicalMode_)
        run(typedCanonicalState());
    else
        run(legacyCanonicalState());
}

std::shared_ptr<WebGpuCanonicalCallbackCustody::State>
WebGpuOrdinaryRecording::legacyCanonicalState() const noexcept
{
    return !typedCanonicalMode_ && !rootCanonicalMode_
        ? std::static_pointer_cast<WebGpuCanonicalCallbackCustody::State>(canonicalState_.lock())
        : nullptr;
}

std::shared_ptr<WebGpuOrdinaryRecording::TypedState>
WebGpuOrdinaryRecording::typedCanonicalState() const noexcept
{
    return typedCanonicalMode_ ? std::static_pointer_cast<TypedState>(canonicalState_.lock())
                               : nullptr;
}

std::shared_ptr<WebGpuCanonicalCallbackCustody::State>
WebGpuOrdinarySubmission::legacyCanonicalState() const noexcept
{
    return !typedCanonicalMode_ && !rootCanonicalMode_
        ? std::static_pointer_cast<WebGpuCanonicalCallbackCustody::State>(canonicalState_.lock())
        : nullptr;
}

std::shared_ptr<WebGpuOrdinarySubmission::TypedState>
WebGpuOrdinarySubmission::typedCanonicalState() const noexcept
{
    return typedCanonicalMode_ ? std::static_pointer_cast<TypedState>(canonicalState_.lock())
                               : nullptr;
}

std::shared_ptr<WebGpuOrdinaryRecording::RootState>
WebGpuOrdinaryRecording::rootCanonicalState() const noexcept
{
    return rootCanonicalMode_ ? std::static_pointer_cast<RootState>(canonicalState_.lock())
                              : nullptr;
}

std::shared_ptr<WebGpuOrdinarySubmission::RootState>
WebGpuOrdinarySubmission::rootCanonicalState() const noexcept
{
    return rootCanonicalMode_ ? std::static_pointer_cast<RootState>(canonicalState_.lock())
                              : nullptr;
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::allocateRoot(
    std::shared_ptr<WebGpuNativeCommandPayload> command, std::weak_ptr<RootState> state)
{
    return shareOriginal(core::makeUnique<WebGpuOrdinaryRecording>(
        ConstructionKey{}, std::move(command), std::move(state)));
}

std::shared_ptr<WebGpuOrdinarySubmission> WebGpuOrdinarySubmission::allocateRoot(
    std::shared_ptr<const WebGpuRecordedCommandProof> recording, std::weak_ptr<RootState> state)
{
    return shareOriginal(core::makeUnique<WebGpuOrdinarySubmission>(
        ConstructionKey{}, std::move(recording), std::move(state)));
}

const char* WebGpuOrdinaryRecording::rootColdDiagnostic() const noexcept
{
    const auto state = rootCanonicalState();
    if (!state)
        return "RHI-WEBGPU-ROOT-ISSUER-RETIRED";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || state->recording.get() != this || !state->issuersClaimed
        || !core::SharedOwnerProjection::sameOwner(graph->command->command_, command_)
        || identity_ != graph->command->identity_
        || !core::SharedOwnerProjection::sameOwner(originalMetadataQuota(),
            graph->command->originalAdmission()->originalRoot()->originalMetadataQuota()))
        return "RHI-WEBGPU-ROOT-ISSUER-ORIGINAL-RECORDING";
    if (const auto* id = graph->command->validate())
        return id;
    return "RHI-WEBGPU-ROOT-ISSUER-RECORDING-UNPROVEN";
}

const char* WebGpuOrdinarySubmission::rootColdDiagnostic() const noexcept
{
    const auto state = rootCanonicalState();
    if (!state)
        return "RHI-WEBGPU-ROOT-ISSUER-RETIRED";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    if (!state->pool.get(state->ticket) || state->submission.get() != this || !state->issuersClaimed
        || !core::SharedOwnerProjection::sameOwner(recording_, state->recording)
        || !core::SharedOwnerProjection::sameOwner(originalMetadataQuota(), state->submissionQuota))
        return "RHI-WEBGPU-ROOT-ISSUER-ORIGINAL-SUBMISSION";
    return "RHI-WEBGPU-ROOT-ISSUER-SUBMISSION-UNPROVEN";
}

std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::allocateTyped(
    std::shared_ptr<WebGpuNativeCommandPayload> command, std::weak_ptr<TypedState> state)
{
    return shareOriginal(core::makeUnique<WebGpuOrdinaryRecording>(
        ConstructionKey{}, std::move(command), std::move(state)));
}

std::shared_ptr<WebGpuOrdinarySubmission> WebGpuOrdinarySubmission::allocateTyped(
    std::shared_ptr<const WebGpuRecordedCommandProof> recording, std::weak_ptr<TypedState> state)
{
    return shareOriginal(core::makeUnique<WebGpuOrdinarySubmission>(
        ConstructionKey{}, std::move(recording), std::move(state)));
}

const char* WebGpuOrdinaryRecording::typedColdDiagnostic() const noexcept
{
    const auto state = typedCanonicalState();
    if (!state)
        return "RHI-WEBGPU-TYPED-ISSUER-RETIRED";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || state->recording.get() != this || !state->issuersClaimed
        || !core::SharedOwnerProjection::sameOwner(graph->command->command_, command_)
        || identity_ != graph->command->identity_
        || !core::SharedOwnerProjection::sameOwner(originalMetadataQuota(),
            graph->command->originalAdmission()->originalRoot()->originalMetadataQuota()))
        return "RHI-WEBGPU-TYPED-ISSUER-ORIGINAL-RECORDING";
    if (const auto* id = graph->command->validate())
        return id;
    return "RHI-WEBGPU-TYPED-ISSUER-RECORDING-UNPROVEN";
}

const char* WebGpuOrdinarySubmission::typedColdDiagnostic() const noexcept
{
    const auto state = typedCanonicalState();
    if (!state)
        return "RHI-WEBGPU-TYPED-ISSUER-RETIRED";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    if (!state->pool.get(state->ticket) || state->submission.get() != this || !state->issuersClaimed
        || !core::SharedOwnerProjection::sameOwner(recording_, state->recording)
        || !core::SharedOwnerProjection::sameOwner(originalMetadataQuota(), state->submissionQuota))
        return "RHI-WEBGPU-TYPED-ISSUER-ORIGINAL-SUBMISSION";
    return "RHI-WEBGPU-TYPED-ISSUER-SUBMISSION-UNPROVEN";
}

// The native Promise adapter clears userdata before entering this delivery
// window. Cold/arbitrary callbacks cannot advance the same original gate.
template <class S>
void WebGpuOrdinaryRecording::completePrepared(void* userdata,
    WebGpuNativeCompletionReceipt::Outcome outcome,
    WebGpuNativeCompletionReceipt::Callback receiver) noexcept
{
    auto* slot = static_cast<typename S::Slot*>(userdata);
    const auto state = slot ? slot->owner : nullptr;
    if (!state || slot->role != S::Role::RecordingScope || slot->index >= S::submissionFirst
        || state->slots[slot->index].get() != slot || slot->ticket.index != state->ticket.index
        || slot->ticket.generation != state->ticket.generation)
        return;
    const auto self = state->recording;
    const auto* graph = state->pool.get(state->ticket);
    if (!self || !graph
        || !core::SharedOwnerProjection::sameOwner(([&] {
               if constexpr (S::bindlessRoot)
                   return self->rootCanonicalState();
               else
                   return self->typedCanonicalState();
           })(),
            state)
        || !WebGpuNativeCompletionReceipt::matchesDelivery(slot->nativeReceipt,
            graph->command->device_,
            graph->command->queue_,
            WebGpuNativeCompletionReceipt::Kind::Scope,
            slot,
            receiver,
            outcome)) {
        state->quarantine("RHI-WEBGPU-TYPED-RECORDING-DELIVERY-UNPROVEN");
        return;
    }
    if (!slot->gate.beginDelivery()) {
        state->quarantine("RHI-WEBGPU-TYPED-RECORDING-SCOPE-DUPLICATE");
        return;
    }
    const bool success = outcome == WebGpuNativeCompletionReceipt::Outcome::Success;
    if (!self->canonicalEvidence_.observe(
            static_cast<std::uint32_t>(slot->index - S::recordingFirst), success))
        self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-SCOPE-DUPLICATE";
    else if (!success && !self->failure_)
        self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-NATIVE-ERROR";
    slot->gate.finishDelivery();
    slot->owner.reset();
    self->finishCanonicalRecording();
    state->releaseTerminalIfDrained();
}

void WebGpuOrdinaryRecording::completeTyped(
    void* userdata, WebGpuNativeCompletionReceipt::Outcome outcome) noexcept
{
    completePrepared<TypedState>(userdata, outcome, &completeTyped);
}

void WebGpuOrdinaryRecording::completeRoot(
    void* userdata, WebGpuNativeCompletionReceipt::Outcome outcome) noexcept
{
    completePrepared<RootState>(userdata, outcome, &completeRoot);
}

template <class S>
void WebGpuOrdinarySubmission::completePrepared(void* userdata,
    WebGpuNativeCompletionReceipt::Outcome outcome,
    WebGpuNativeCompletionReceipt::Callback receiver) noexcept
{
    auto* slot = static_cast<typename S::Slot*>(userdata);
    const auto state = slot ? slot->owner : nullptr;
    if (!state || slot->index < S::submissionFirst || slot->index >= S::lossIndex
        || state->slots[slot->index].get() != slot || slot->ticket.index != state->ticket.index
        || slot->ticket.generation != state->ticket.generation)
        return;
    const auto self = state->submission;
    const auto* graph = state->pool.get(state->ticket);
    const bool queue = slot->index == S::queueIndex;
    if (!self || !graph
        || !core::SharedOwnerProjection::sameOwner(([&] {
               if constexpr (S::bindlessRoot)
                   return self->rootCanonicalState();
               else
                   return self->typedCanonicalState();
           })(),
            state)
        || !WebGpuNativeCompletionReceipt::matchesDelivery(slot->nativeReceipt,
            graph->command->device_,
            graph->command->queue_,
            queue ? WebGpuNativeCompletionReceipt::Kind::Queue
                  : WebGpuNativeCompletionReceipt::Kind::Scope,
            slot,
            receiver,
            outcome)) {
        state->quarantine("RHI-WEBGPU-TYPED-SUBMISSION-DELIVERY-UNPROVEN");
        return;
    }
    if (!slot->gate.beginDelivery()) {
        state->quarantine("RHI-WEBGPU-TYPED-SUBMISSION-RECEIPT-DUPLICATE");
        return;
    }
    if (queue)
        state->queueNativeReceiptReceived = true;
    if (!state->terminalDiagnostic)
        if (const auto* id = state->recording->validateCanonicalLive())
            state->quarantine(id);
    const auto participant =
        queue ? 0u : static_cast<std::uint32_t>(slot->index - S::submissionFirst + 1u);
    if (!self->evidence_.observe(
            participant, outcome == WebGpuNativeCompletionReceipt::Outcome::Success))
        state->quarantine("RHI-WEBGPU-TYPED-SUBMISSION-RECEIPT-DUPLICATE");
    else if (outcome != WebGpuNativeCompletionReceipt::Outcome::Success)
        state->quarantine("RHI-WEBGPU-TYPED-SUBMISSION-NATIVE-ERROR");
    slot->gate.finishDelivery();
    slot->owner.reset();
    self->finishCanonicalSubmission();
    state->releaseTerminalIfDrained();
}

void WebGpuOrdinarySubmission::completeTyped(
    void* userdata, WebGpuNativeCompletionReceipt::Outcome outcome) noexcept
{
    completePrepared<TypedState>(userdata, outcome, &completeTyped);
}

void WebGpuOrdinarySubmission::completeRoot(
    void* userdata, WebGpuNativeCompletionReceipt::Outcome outcome) noexcept
{
    completePrepared<RootState>(userdata, outcome, &completeRoot);
}

const char* WebGpuOrdinarySubmission::submitTyped(
    std::shared_ptr<WebGpuOrdinarySubmission> self) noexcept
{
    if (!self || !self->typedCanonicalMode_)
        return "RHI-WEBGPU-TYPED-ISSUER-ORIGINAL-SUBMISSION";
    return submitCanonical(std::move(self));
}

const char* WebGpuCanonicalCallbackCustody::inspectRootIssuers(
    const std::shared_ptr<const WebGpuBindlessRootCallbackCustody>& custody,
    const rhi::OriginalBindlessRootIssuerRequest& request,
    std::uint32_t& owners) noexcept
{
    using C = rhi::OriginalBindlessRootIssuerContract;
    owners = 0u;
    if (const auto* id = C::validateIntent(request))
        return id;
    if (const auto* id = WebGpuBindlessRootCallbackCustody::validateOriginal(custody))
        return id;
    const auto& state = custody->state_;
    const auto* graph = state->pool.get(state->ticket);
    const auto& admission = graph->command->originalAdmission();
    const auto& seal = admission->originalReservation()->originalSeal();
    if (request.maxRecordingCpuBytes
            != admission->originalRoot()->originalMetadataQuota()->limitBytes()
        || request.maxSubmissionCpuBytes != state->submissionQuota->limitBytes()
        || request.maxCallbackCpuBytes != state->callbackQuota->limitBytes())
        return "RHI-ORIGINAL-ROOT-ISSUER-QUOTA";
    if (const auto* id = C::validateRequirements(state->admittedOwners,
            admission->originalClosure()->originalInventory()->originalBudget(),
            seal->originalBudget(),
            request))
        return id;
    owners = state->admittedOwners + C::additionalOwners;
    return nullptr;
}

std::shared_ptr<const WebGpuBindlessRootIssuerPreparation>
WebGpuCanonicalCallbackCustody::prepareRootIssuers(
    std::shared_ptr<const WebGpuBindlessRootCallbackCustody> custody,
    const rhi::OriginalBindlessRootIssuerRequest& request,
    const char*& diagnostic)
{
    using S = RuntimeState<WebGpuBindlessRootColdCommand>;
    using Wrapper = WebGpuBindlessRootIssuerPreparation;
    const auto decision = rhi::semantics::OriginalBindlessRootIssuerLowering::lowering(
        rhi::Backend::WebGPU, request.contractVersion);
    if (!decision.preparationAvailable) {
        diagnostic = decision.diagnostic;
        return {};
    }
    std::uint32_t owners = 0u;
    if ((diagnostic = inspectRootIssuers(custody, request, owners)))
        return {};
    auto& state = *custody->state_;
    if (state.issuersClaimed) {
        diagnostic = "RHI-WEBGPU-ROOT-ISSUER-DUPLICATE";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-ROOT-ISSUER-NATIVE-UNAVAILABLE";
    return {};
#else
    const auto* graph = state.pool.get(state.ticket);
    const auto& command = *graph->command;
    try {
        std::shared_ptr<WebGpuOrdinaryRecording> recording;
        {
            core::CpuMemoryQuotaScope scope(
                command.originalAdmission()->originalRoot()->originalMetadataQuota());
            recording = WebGpuOrdinaryRecording::allocateRoot(command.command_, custody->state_);
        }
        core::CpuMemoryQuotaScope scope(state.submissionQuota);
        auto submission = WebGpuOrdinarySubmission::allocateRoot(recording, custody->state_);
        auto result = Wrapper::shareOriginal(
            core::makeUnique<Wrapper>(Wrapper::ConstructionKey{}, custody, request, owners));
        std::array<WebGpuNativeCompletionReceipt::Prepared, S::lossIndex> receipts;
        for (std::size_t i = 0u; i < S::lossIndex; ++i) {
            auto receipt = WebGpuNativeCompletionReceipt::prepare(command.device_,
                command.queue_,
                i == S::queueIndex ? WebGpuNativeCompletionReceipt::Kind::Queue
                                   : WebGpuNativeCompletionReceipt::Kind::Scope,
                state.slots[i].get(),
                i < S::submissionFirst ? &WebGpuOrdinaryRecording::completeRoot
                                       : &WebGpuOrdinarySubmission::completeRoot);
            if (!receipt
                || !receipt.matches(command.device_,
                    command.queue_,
                    i == S::queueIndex ? WebGpuNativeCompletionReceipt::Kind::Queue
                                       : WebGpuNativeCompletionReceipt::Kind::Scope,
                    state.slots[i].get(),
                    i < S::submissionFirst ? &WebGpuOrdinaryRecording::completeRoot
                                           : &WebGpuOrdinarySubmission::completeRoot)) {
                diagnostic = "RHI-WEBGPU-ROOT-ISSUER-RECEIPT-PREPARATION";
                return {};
            }
            receipts[i] = std::move(receipt);
        }
        // Publish all seven listeners and both existing issuers atomically.
        for (std::size_t i = 0u; i < S::lossIndex; ++i)
            state.slots[i]->nativeReceipt = receipts[i].release();
        state.recording = std::move(recording);
        state.submission = std::move(submission);
        state.issuersClaimed = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ROOT-ISSUER-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-ROOT-ISSUER-ALLOCATION";
    }
    return {};
#endif
}

const char* WebGpuCanonicalCallbackCustody::validateRootIssuers(
    const WebGpuBindlessRootIssuerPreparation& original) noexcept
{
    using S = RuntimeState<WebGpuBindlessRootColdCommand>;
    const auto& custody = original.custody_;
    if (const auto* id = WebGpuBindlessRootCallbackCustody::validateOriginal(custody))
        return id;
    const auto& state = custody->state_;
    const auto* graph = state->pool.get(state->ticket);
    if (!state->issuersClaimed || !state->recording || !state->submission)
        return "RHI-WEBGPU-ROOT-ISSUER-UNPREPARED";
    const auto& recording = *state->recording;
    const auto& submission = *state->submission;
    if (!recording.hasQuotaBoundSharedOwner(state->recording)
        || !submission.hasQuotaBoundSharedOwner(state->submission) || !recording.rootCanonicalMode_
        || !submission.rootCanonicalMode_ || !recording.canonicalMode_ || !submission.canonicalMode_
        || recording.typedCanonicalMode_ || submission.typedCanonicalMode_
        || recording.legacyCanonicalState() || submission.legacyCanonicalState()
        || !core::SharedOwnerProjection::sameOwner(recording.rootCanonicalState(), state)
        || !core::SharedOwnerProjection::sameOwner(submission.rootCanonicalState(), state)
        || !core::SharedOwnerProjection::sameOwner(submission.recording_, state->recording)
        || !core::SharedOwnerProjection::sameOwner(recording.command_, graph->command->command_)
        || recording.identity_ != graph->command->identity_ || recording.completion_
        || recording.range_ || recording.attachments_ || recording.originalGraphicsRange_
        || recording.originalStates_ || recording.originalTransaction_
        || recording.originalTextureTransaction_ || recording.originalReadbackCopies_
        || recording.status_ != WebGpuOrdinaryRecording::Status::Prepared
        || submission.status_ != WebGpuOrdinarySubmission::Status::Ready
        || recording.canonicalIssuanceComplete_ || submission.canonicalIssuanceComplete_
        || submission.callbacksIssued_ || recording.canonicalEvidence_.allReceived()
        || submission.evidence_.allReceived())
        return "RHI-WEBGPU-ROOT-ISSUER-ORIGINAL-BINDING";
    for (const auto& callback : recording.callbacks_)
        if (callback)
            return "RHI-WEBGPU-ROOT-ISSUER-EXTRA-CALLBACK";
    for (const auto& callback : submission.callbacks_)
        if (callback)
            return "RHI-WEBGPU-ROOT-ISSUER-EXTRA-CALLBACK";
    if (!core::SharedOwnerProjection::sameOwner(
            original.originalMetadataQuota(), state->submissionQuota)
        || !core::SharedOwnerProjection::sameOwner(
            submission.originalMetadataQuota(), state->submissionQuota)
        || !core::SharedOwnerProjection::sameOwner(recording.originalMetadataQuota(),
            graph->command->originalAdmission()->originalRoot()->originalMetadataQuota()))
        return "RHI-ORIGINAL-ROOT-ISSUER-QUOTA";
    const auto& command = *graph->command;
    for (std::size_t i = 0u; i < S::lossIndex; ++i)
        if (!WebGpuNativeCompletionReceipt::matchesPrepared(state->slots[i]->nativeReceipt,
                command.device_,
                command.queue_,
                i == S::queueIndex ? WebGpuNativeCompletionReceipt::Kind::Queue
                                   : WebGpuNativeCompletionReceipt::Kind::Scope,
                state->slots[i].get(),
                i < S::submissionFirst ? &WebGpuOrdinaryRecording::completeRoot
                                       : &WebGpuOrdinarySubmission::completeRoot))
            return "RHI-WEBGPU-ROOT-ISSUER-RECEIPT";
    std::uint32_t owners = 0u;
    if (const auto* id = inspectRootIssuers(custody, original.request_, owners))
        return id;
    return owners == original.owners_ ? nullptr : "RHI-WEBGPU-ROOT-ISSUER-OWNER-CAPACITY";
}

bool WebGpuCanonicalCallbackCustody::rootIssuersClosed(
    const WebGpuBindlessRootIssuerPreparation& original) noexcept
{
    if (validateRootIssuers(original))
        return false;
    const auto& state = original.custody_->state_;
    const auto& recording = state->recording;
    const auto& submission = state->submission;
    const char* diagnostic = nullptr;
    auto ordinary = WebGpuOrdinarySubmission::prepare(recording,
        {1u,
            true,
            1u,
            WebGpuOrdinarySubmission::callbackCount,
            original.request_.maxSubmissionCpuBytes},
        state->submissionQuota,
        diagnostic);
    return !ordinary && diagnostic && !recording->hasRecordedCommand()
        && !submission->callbacksIssued() && !submission->queueCompletionObserved()
        && core::StringView(recording->diagnostic()) == "RHI-WEBGPU-ROOT-ISSUER-RECORDING-UNPROVEN"
        && core::StringView(submission->diagnostic())
        == "RHI-WEBGPU-ROOT-ISSUER-SUBMISSION-UNPROVEN"
        && core::StringView(WebGpuOrdinaryRecording::record(recording, 1u, 1u, 1u))
        == "RHI-WEBGPU-ROOT-ISSUER-RECORDING-UNPROVEN"
        && core::StringView(WebGpuOrdinarySubmission::submit(submission))
        == "RHI-WEBGPU-CANONICAL-ISSUER-SUBMISSION-UNPROVEN";
}

const char* WebGpuCanonicalCallbackCustody::inspectTypedIssuers(
    const std::shared_ptr<const WebGpuOriginalTypedCallbackCustody>& custody,
    const rhi::OriginalTypedIssuerPreparationRequest& request,
    std::uint32_t& owners) noexcept
{
    using C = rhi::OriginalTypedIssuerPreparationContract;
    owners = 0u;
    if (const auto* id = C::validateIntent(request))
        return id;
    if (const auto* id = WebGpuOriginalTypedCallbackCustody::validateOriginal(custody))
        return id;
    if (const auto* id = C::validateProfile(request, custody->request_.contractVersion))
        return id;
    auto& state = *custody->state_;
    const auto* graph = state.pool.get(state.ticket);
    const auto& admission = *graph->command->originalAdmission();
    const auto& seal = admission.originalReservation()->originalSeal();
    if (request.maxRecordingCpuBytes
            != admission.originalRoot()->originalMetadataQuota()->limitBytes()
        || request.maxSubmissionCpuBytes != seal->originalSubmissionQuota()->limitBytes()
        || request.maxCallbackCpuBytes != seal->originalCallbackQuota()->limitBytes())
        return "RHI-WEBGPU-TYPED-ISSUER-QUOTA";
    const auto& lifecycle = seal->originalBudget();
    const auto& source = admission.originalRoot()
                             ->originalGraph()
                             ->originalInputs()
                             ->originalScalars()
                             ->originalUsage()
                             ->originalSources()
                             ->originalBudget();
    if (const auto* id = C::validateOwners(state.admittedOwners,
            request,
            custody->request_.maxRetainedOwners,
            graph->command->originalRequest().maxRetainedOwners,
            admission.originalRequest().maxRetainedOwners,
            source,
            lifecycle,
            admission.originalRoot()->retainedOwnerCount()))
        return id;
    owners = state.admittedOwners + C::additionalOwners;
    return nullptr;
}

std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>
WebGpuCanonicalCallbackCustody::prepareTypedIssuers(
    std::shared_ptr<const WebGpuOriginalTypedCallbackCustody> custody,
    const rhi::OriginalTypedIssuerPreparationRequest& request,
    const char*& diagnostic)
{
    using C = rhi::OriginalTypedIssuerPreparationContract;
    using S = RuntimeState<WebGpuOriginalTypedColdCommand>;
    using Wrapper = WebGpuOriginalTypedIssuerPreparation;
    std::uint32_t owners = 0u;
    const auto decision = arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(
        rhi::Backend::WebGPU, request.contractVersion);
    if (!decision.preparationAvailable) {
        diagnostic = decision.diagnostic;
        return {};
    }
    if ((diagnostic = inspectTypedIssuers(custody, request, owners)))
        return {};
    auto& state = *custody->state_;
    if (state.issuersClaimed) {
        diagnostic = "RHI-WEBGPU-TYPED-ISSUER-DUPLICATE";
        return {};
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    diagnostic = "RHI-WEBGPU-TYPED-ISSUER-NATIVE-UNAVAILABLE";
    return {};
#else
    const auto* graph = state.pool.get(state.ticket);
    const auto& command = *graph->command;
    const auto& admission = *command.originalAdmission();
    try {
        std::shared_ptr<WebGpuOrdinaryRecording> recording;
        {
            core::CpuMemoryQuotaScope scope(admission.originalRoot()->originalMetadataQuota());
            recording = WebGpuOrdinaryRecording::allocateTyped(command.command_, custody->state_);
        }
        core::CpuMemoryQuotaScope scope(state.submissionQuota);
        auto submission = WebGpuOrdinarySubmission::allocateTyped(recording, custody->state_);
        auto result = Wrapper::shareOriginal(core::makeUnique<Wrapper>(
            Wrapper::ConstructionKey{}, custody, request, owners, admission.originalRoot()));
        std::array<WebGpuNativeCompletionReceipt::Prepared, S::lossIndex> receipts;
        for (std::size_t i = 0u; i < S::lossIndex; ++i) {
            auto receipt = WebGpuNativeCompletionReceipt::prepare(command.device_,
                command.queue_,
                i == S::queueIndex ? WebGpuNativeCompletionReceipt::Kind::Queue
                                   : WebGpuNativeCompletionReceipt::Kind::Scope,
                state.slots[i].get(),
                i < S::submissionFirst ? &WebGpuOrdinaryRecording::completeTyped
                                       : &WebGpuOrdinarySubmission::completeTyped);
            if (!receipt
                || !receipt.matches(command.device_,
                    command.queue_,
                    i == S::queueIndex ? WebGpuNativeCompletionReceipt::Kind::Queue
                                       : WebGpuNativeCompletionReceipt::Kind::Scope,
                    state.slots[i].get(),
                    i < S::submissionFirst ? &WebGpuOrdinaryRecording::completeTyped
                                           : &WebGpuOrdinarySubmission::completeTyped)) {
                diagnostic = "RHI-WEBGPU-TYPED-ISSUER-RECEIPT-PREPARATION";
                return {};
            }
            receipts[i] = std::move(receipt);
        }
        // No fallible operation follows publication. This same State owns all
        // seven prepared native listeners and both actual existing issuers.
        for (std::size_t i = 0u; i < S::lossIndex; ++i)
            state.slots[i]->nativeReceipt = receipts[i].release();
        state.recording = std::move(recording);
        state.submission = std::move(submission);
        state.issuersClaimed = true;
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-TYPED-ISSUER-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-TYPED-ISSUER-ALLOCATION";
        return {};
    }
#endif
}

const char* WebGpuCanonicalCallbackCustody::validateTypedIssuers(
    const WebGpuOriginalTypedIssuerPreparation& original) noexcept
{
    using C = rhi::OriginalTypedIssuerPreparationContract;
    using S = RuntimeState<WebGpuOriginalTypedColdCommand>;
    if (const auto* id = C::validateIntent(original.request_))
        return id;
    if (!original.custody_ || !original.custody_->hasQuotaBoundSharedOwner(original.custody_))
        return "RHI-WEBGPU-TYPED-CALLBACK-OWNER";
    if (const auto* id =
            C::validateProfile(original.request_, original.custody_->request_.contractVersion))
        return id;
    const auto& state = original.custody_->state_;
    if (!state || !state->sharedOwnerMatches(state))
        return "RHI-WEBGPU-TYPED-CALLBACK-STATE-OWNER";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || !graph->command || !graph->command->hasQuotaBoundSharedOwner(graph->command))
        return "RHI-WEBGPU-TYPED-CALLBACK-CAPACITY";
    if (!state->issuersClaimed || !state->recording || !state->submission)
        return "RHI-WEBGPU-TYPED-ISSUER-UNPREPARED";
    const auto& recording = *state->recording;
    const auto& submission = *state->submission;
    if (!recording.hasQuotaBoundSharedOwner(state->recording)
        || !submission.hasQuotaBoundSharedOwner(state->submission) || !recording.typedCanonicalMode_
        || !submission.typedCanonicalMode_ || !recording.canonicalMode_
        || !submission.canonicalMode_ || recording.legacyCanonicalState()
        || submission.legacyCanonicalState()
        || !core::SharedOwnerProjection::sameOwner(recording.typedCanonicalState(), state)
        || !core::SharedOwnerProjection::sameOwner(submission.typedCanonicalState(), state)
        || !core::SharedOwnerProjection::sameOwner(submission.recording_, state->recording)
        || !core::SharedOwnerProjection::sameOwner(recording.command_, graph->command->command_)
        || recording.identity_ != graph->command->identity_ || recording.completion_
        || recording.range_ || recording.attachments_ || recording.originalGraphicsRange_
        || recording.originalStates_ || recording.originalTransaction_
        || recording.originalTextureTransaction_ || recording.originalReadbackCopies_
        || recording.status_ != WebGpuOrdinaryRecording::Status::Prepared
        || submission.status_ != WebGpuOrdinarySubmission::Status::Ready
        || recording.canonicalIssuanceComplete_ || submission.canonicalIssuanceComplete_
        || submission.callbacksIssued_ || recording.canonicalEvidence_.allReceived()
        || submission.evidence_.allReceived())
        return "RHI-WEBGPU-TYPED-ISSUER-ORIGINAL-BINDING";
    for (const auto& callback : recording.callbacks_)
        if (callback)
            return "RHI-WEBGPU-TYPED-ISSUER-EXTRA-CALLBACK";
    for (const auto& callback : submission.callbacks_)
        if (callback)
            return "RHI-WEBGPU-TYPED-ISSUER-EXTRA-CALLBACK";
    if (!core::SharedOwnerProjection::sameOwner(
            original.originalMetadataQuota(), state->submissionQuota)
        || !core::SharedOwnerProjection::sameOwner(
            submission.originalMetadataQuota(), state->submissionQuota))
        return "RHI-WEBGPU-TYPED-ISSUER-QUOTA";
    const auto& command = *graph->command;
    for (std::size_t i = 0u; i < S::lossIndex; ++i)
        if (!state->slots[i]
            || !WebGpuNativeCompletionReceipt::matchesPrepared(state->slots[i]->nativeReceipt,
                command.device_,
                command.queue_,
                i == S::queueIndex ? WebGpuNativeCompletionReceipt::Kind::Queue
                                   : WebGpuNativeCompletionReceipt::Kind::Scope,
                state->slots[i].get(),
                i < S::submissionFirst ? &WebGpuOrdinaryRecording::completeTyped
                                       : &WebGpuOrdinarySubmission::completeTyped))
            return "RHI-WEBGPU-TYPED-ISSUER-RECEIPT";
    // Reject a changed native listener after exact retained issuer/slot owner
    // checks, before the expensive public cold projection replay. Success still
    // consumes the entire original contract; no validation bypass is granted.
    std::uint32_t owners = 0u;
    if (const auto* id = inspectTypedIssuers(original.custody_, original.request_, owners))
        return id;
    return owners == original.owners_
            && core::SharedOwnerProjection::sameOwner(recording.originalMetadataQuota(),
                command.originalAdmission()->originalRoot()->originalMetadataQuota())
        ? nullptr
        : "RHI-WEBGPU-TYPED-ISSUER-QUOTA";
}

bool WebGpuCanonicalCallbackCustody::typedIssuersClosed(
    const WebGpuOriginalTypedIssuerPreparation& original) noexcept
{
    if (validateTypedIssuers(original))
        return false;
    const auto& state = *original.custody_->state_;
    const auto& recording = state.recording;
    const auto& submission = state.submission;
    const char* id = nullptr;
    auto ordinary = WebGpuOrdinarySubmission::prepare(recording,
        {1u,
            true,
            1u,
            WebGpuOrdinarySubmission::callbackCount,
            original.request_.maxSubmissionCpuBytes},
        state.submissionQuota,
        id);
    return !ordinary && id && core::StringView(id) == "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED"
        && !recording->hasRecordedCommand() && !submission->callbacksIssued()
        && !submission->queueCompletionObserved()
        && core::StringView(recording->diagnostic()) == "RHI-WEBGPU-TYPED-ISSUER-RECORDING-UNPROVEN"
        && core::StringView(submission->diagnostic())
        == "RHI-WEBGPU-TYPED-ISSUER-SUBMISSION-UNPROVEN"
        && core::StringView(WebGpuOrdinaryRecording::record(recording, 1u, 1u, 1u))
        == "RHI-WEBGPU-TYPED-ISSUER-RECORDING-UNPROVEN"
        && core::StringView(WebGpuOrdinarySubmission::submit(submission))
        == "RHI-WEBGPU-CANONICAL-ISSUER-SUBMISSION-UNPROVEN";
}

std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>
WebGpuOriginalTypedIssuerPreparation::prepareOriginal(
    std::shared_ptr<const WebGpuOriginalTypedCallbackCustody> custody,
    const Request& request,
    const char*& diagnostic)
{
    return WebGpuCanonicalCallbackCustody::prepareTypedIssuers(
        std::move(custody), request, diagnostic);
}

const char* WebGpuOriginalTypedIssuerPreparation::validate() const noexcept
{
    return WebGpuCanonicalCallbackCustody::validateTypedIssuers(*this);
}

bool WebGpuOriginalTypedIssuerPreparation::issuersClosedForInspection() const noexcept
{
    return WebGpuCanonicalCallbackCustody::typedIssuersClosed(*this);
}

const char* WebGpuCanonicalCallbackCustody::validateTypedRetainedState(
    const std::shared_ptr<RuntimeState<WebGpuOriginalTypedColdCommand>>& state) noexcept
{
    using S = RuntimeState<WebGpuOriginalTypedColdCommand>;
    using Phase = core::runtime::ExternalCallbackGate::Phase;
    if (!state || !state->sharedOwnerMatches(state))
        return "RHI-WEBGPU-TYPED-CALLBACK-STATE-OWNER";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || state->pool.pending() != 1u || !state->pool.accepting() || !graph->command
        || !graph->command->hasQuotaBoundSharedOwner(graph->command))
        return "RHI-WEBGPU-TYPED-CALLBACK-CAPACITY";
    if (!state->issuersClaimed || !state->recording || !state->submission || state->lowering
        || !state->recording->canonicalMode_ || !state->submission->canonicalMode_
        || !state->recording->hasQuotaBoundSharedOwner(state->recording)
        || !state->submission->hasQuotaBoundSharedOwner(state->submission)
        || !core::SharedOwnerProjection::sameOwner(state->recording->typedCanonicalState(), state)
        || !core::SharedOwnerProjection::sameOwner(state->submission->typedCanonicalState(), state)
        || !core::SharedOwnerProjection::sameOwner(state->submission->recording_, state->recording)
        || !core::SharedOwnerProjection::sameOwner(
            state->recording->command_, graph->command->command_)
        || state->recording->identity_ != graph->command->identity_ || state->recording->completion_
        || state->submission->readbackDrainConsumer_)
        return "RHI-WEBGPU-TYPED-ISSUER-ORIGINAL-BINDING";
    const auto& admission = *graph->command->originalAdmission();
    const auto& seal = admission.originalReservation()->originalSeal();
    if (!core::SharedOwnerProjection::sameOwner(
            state->submissionQuota, seal->originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->callbackQuota, seal->originalCallbackQuota())
        || !core::SharedOwnerProjection::sameOwner(state->recording->originalMetadataQuota(),
            admission.originalRoot()->originalMetadataQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->submission->originalMetadataQuota(), state->submissionQuota))
        return "RHI-WEBGPU-TYPED-ISSUER-QUOTA";
    const auto& budget = seal->originalBudget();
    if (state->admittedOwners
            > UINT32_MAX - rhi::OriginalTypedIssuerPreparationContract::additionalOwners
        || state->admittedOwners + rhi::OriginalTypedIssuerPreparationContract::additionalOwners
            > budget.maxRecordingOwners
        || state->admittedOwners + rhi::OriginalTypedIssuerPreparationContract::additionalOwners
            > budget.maxSubmissionOwners
        || state->admittedOwners + rhi::OriginalTypedIssuerPreparationContract::additionalOwners
            > budget.maxCallbackOwners)
        return "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY";
    if (const auto* id = WebGpuNativeCommandPreparation::validateTypedRetained(*graph->command))
        return id;
    const auto& payload = admission.originalPayload();
    const auto& decision = payload->originalDecision();
    const auto& closure = decision->originalClosure();
    const auto* observation = closure->originalNativeLossObservation();
    if (!observation || !state->observedLoss
        || !core::SharedOwnerProjection::sameOwner(state->observedLoss, observation->binding_)
        || state->observedLoss->observers_.inspect(state->observerTicket)
            != core::runtime::BoundedTerminalObservers<
                rhi::OriginalDeviceNativeLossTerminal>::Status::Complete)
        return "RHI-WEBGPU-TYPED-CALLBACK-LOSS-UNBOUND";
    if (state->slots.size() != admission.requirements().capacity.callbackTickets)
        return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
    for (std::size_t i = 0u; i < S::slotCount; ++i) {
        const auto& slot = state->slots[i];
        const auto expected = i < S::submissionFirst ? S::Role::RecordingScope
            : i < S::queueIndex                      ? S::Role::SubmissionScope
            : i == S::queueIndex                     ? S::Role::QueueCompletion
                                                     : S::Role::NativeLoss;
        if (!slot || slot->index != i || slot->role != expected
            || slot->ticket.index != state->ticket.index
            || slot->ticket.generation != state->ticket.generation || slot->nativeState)
            return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
        if (i == S::lossIndex) {
            if (slot->nativeReceipt || slot->gate.phase() != Phase::Armed
                || !core::SharedOwnerProjection::sameOwner(slot->owner, state))
                return "RHI-WEBGPU-TYPED-CALLBACK-LOSS-UNBOUND";
            continue;
        }
        const auto callback = i < S::submissionFirst ? &WebGpuOrdinaryRecording::completeTyped
                                                     : &WebGpuOrdinarySubmission::completeTyped;
        const auto kind = i == S::queueIndex ? WebGpuNativeCompletionReceipt::Kind::Queue
                                             : WebGpuNativeCompletionReceipt::Kind::Scope;
        const bool unarmed = slot->gate.phase() == Phase::Unarmed;
        if (unarmed ? slot->owner != nullptr
                : slot->gate.phase() == Phase::Delivered
                ? slot->owner != nullptr
                : !core::SharedOwnerProjection::sameOwner(slot->owner, state))
            return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
        const bool issuing = i < S::submissionFirst ? !state->recording->canonicalIssuanceComplete_
                && state->recording->status_ == WebGpuOrdinaryRecording::Status::RecordingPending
                                                    : !state->submission->canonicalIssuanceComplete_
                && state->submission->status_ == WebGpuOrdinarySubmission::Status::Pending;
        if (!(unarmed ? WebGpuNativeCompletionReceipt::matchesPrepared(slot->nativeReceipt,
                            graph->command->device_,
                            graph->command->queue_,
                            kind,
                            slot.get(),
                            callback)
                      : WebGpuNativeCompletionReceipt::matchesIssued(slot->nativeReceipt,
                            graph->command->device_,
                            graph->command->queue_,
                            kind,
                            slot.get(),
                            callback)
                        || (issuing && slot->gate.phase() == Phase::Armed
                            && WebGpuNativeCompletionReceipt::matchesPrepared(slot->nativeReceipt,
                                graph->command->device_,
                                graph->command->queue_,
                                kind,
                                slot.get(),
                                callback))))
            return "RHI-WEBGPU-TYPED-ISSUER-RECEIPT";
    }
    if (state->results) {
        if (!core::SharedOwnerProjection::sameOwner(
                state->results->originalTypedAdmission(), graph->command->originalAdmission())
            || state->results->originalAdmission())
            return "RHI-WEBGPU-TYPED-CALLBACK-RESULT-BINDING";
        if (const auto* id = admission.originalRoot()->status()
                    == rhi::OriginalCommandStateTransaction::Status::Prepared
                ? WebGpuCanonicalReadbackResultStorage::validateOriginal(state->results)
                : state->validateRetainedResults(seal))
            return id;
        for (std::size_t i = S::slotCount; i < state->slots.size(); ++i) {
            const auto& slot = state->slots[i];
            if (!slot || slot->index != i || slot->role != S::roleAt(i) || slot->nativeState
                || slot->ticket.index != state->ticket.index
                || slot->ticket.generation != state->ticket.generation)
                return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
            const auto phase = slot->gate.phase();
            if (phase == Phase::Unarmed || phase == Phase::Delivered) {
                if (slot->owner)
                    return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
            } else if (!core::SharedOwnerProjection::sameOwner(slot->owner, state))
                return "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
        }
        return state->validateRetainedMapReceipts(
            *closure->originalReadbackJoins(), graph->command->device_, graph->command->queue_);
    }
    return state->slots.size() == S::slotCount ? nullptr : "RHI-WEBGPU-TYPED-CALLBACK-SLOTS";
}

const char* WebGpuCanonicalCallbackCustody::validateRootRetainedState(
    const std::shared_ptr<RuntimeState<WebGpuBindlessRootColdCommand>>& state) noexcept
{
    using S = RuntimeState<WebGpuBindlessRootColdCommand>;
    using Phase = core::runtime::ExternalCallbackGate::Phase;
    if (!state || !state->sharedOwnerMatches(state))
        return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-STATE-OWNER";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || !state->issuersClaimed || !state->recording || !state->submission
        || !state->results)
        return "RHI-WEBGPU-ROOT-ISSUER-UNPREPARED";
    const auto& command = graph->command;
    const auto& admission = command->originalAdmission();
    const auto& seal = admission->originalReservation()->originalSeal();
    if (!command->hasQuotaBoundSharedOwner(command)
        || !admission->hasQuotaBoundSharedOwner(admission) || !seal->hasQuotaBoundSharedOwner(seal)
        || command->request_.contractVersion != 2u
        || !core::SharedOwnerProjection::sameOwner(state->results, command->originalResultStorage())
        || !core::SharedOwnerProjection::sameOwner(
            state->submissionQuota, seal->originalSubmissionQuota())
        || !core::SharedOwnerProjection::sameOwner(
            state->callbackQuota, seal->originalCallbackQuota())
        || !state->recording->hasQuotaBoundSharedOwner(state->recording)
        || !state->submission->hasQuotaBoundSharedOwner(state->submission)
        || !core::SharedOwnerProjection::sameOwner(state->recording->rootCanonicalState(), state)
        || !core::SharedOwnerProjection::sameOwner(state->submission->rootCanonicalState(), state)
        || !core::SharedOwnerProjection::sameOwner(state->recording->command_, command->command_)
        || state->recording->identity_ != command->identity_
        || !core::SharedOwnerProjection::sameOwner(state->submission->recording_, state->recording))
        return "RHI-WEBGPU-ROOT-ISSUER-ORIGINAL-BINDING";
    if (const auto* id = rhi::OriginalBindlessRootNativeExecutionContract::validateProfile(
            {admission->originalClosure()
                    ->originalRoot()
                    ->originalSnapshot()
                    ->plan()
                    .originalExecutionContractVersion,
                true},
            admission->originalClosure()->originalRoot()->originalSnapshot()->plan(),
            seal->originalBudget()))
        return id;
    for (const auto& selected : admission->originalRootSequence().closures())
        if (const auto* id =
                WebGpuBindlessRootNativeClosure::validateOriginalPending(selected, seal))
            return id;
    const auto pipeline = admission->originalClosure()->originalPipelineOwnership();
    if (!pipeline || !pipeline->issuedContract()
        || WebGpuNativeCommandPreparation::inspectShellIdentity(command->command_,
               admission,
               command->device_,
               command->queue_,
               pipeline->issuedContract()->nativeDevice(),
               command->identity_)
            != WebGpuNativeCommandPreparation::ShellIdentity::Complete)
        return "RHI-WEBGPU-ROOT-ISSUER-SHELL-IDENTITY";
    if (const auto* id = WebGpuNativeCommandPreparation::validateBindlessRootProgram(*command))
        return id;
    if (const auto* id = WebGpuNativeCommandPreparation::validateBindlessRootDepthPending(*command))
        return id;
    if (const auto* id = graph->additionalFaults.validatePending(
            admission->originalReadbackSequence(), admission->originalRoot(), graph->fault))
        return id;
    if (const auto* id = state->validateRetainedResults(seal))
        return id;
    if (state->slots.size() != admission->requirements().capacity.callbackTickets)
        return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-CAPACITY";
    for (std::size_t i = 0u; i < state->slots.size(); ++i) {
        const auto& slot = state->slots[i];
        if (!slot || slot->index != i || slot->role != S::roleAt(i)
            || slot->ticket.index != state->ticket.index
            || slot->ticket.generation != state->ticket.generation)
            return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-SLOT";
        const auto phase = slot->gate.phase();
        if (phase == Phase::Unarmed || phase == Phase::Delivered) {
            if (slot->owner)
                return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-SLOT";
        } else if (!core::SharedOwnerProjection::sameOwner(slot->owner, state))
            return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-SLOT";
        if (i == S::lossIndex) {
            if (phase != Phase::Armed || !slot->nativeState)
                return "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-LOSS-UNBOUND";
        } else if (i < S::lossIndex) {
            const auto kind = i == S::queueIndex ? WebGpuNativeCompletionReceipt::Kind::Queue
                                                 : WebGpuNativeCompletionReceipt::Kind::Scope;
            const auto callback = i < S::submissionFirst ? &WebGpuOrdinaryRecording::completeRoot
                                                         : &WebGpuOrdinarySubmission::completeRoot;
            const bool issuing = i < S::submissionFirst
                ? !state->recording->canonicalIssuanceComplete_
                    && state->recording->status_
                        == WebGpuOrdinaryRecording::Status::RecordingPending
                : !state->submission->canonicalIssuanceComplete_
                    && state->submission->status_ == WebGpuOrdinarySubmission::Status::Pending;
            if (!(phase == Phase::Unarmed
                        ? WebGpuNativeCompletionReceipt::matchesPrepared(slot->nativeReceipt,
                              command->device_,
                              command->queue_,
                              kind,
                              slot.get(),
                              callback)
                        : WebGpuNativeCompletionReceipt::matchesIssued(slot->nativeReceipt,
                              command->device_,
                              command->queue_,
                              kind,
                              slot.get(),
                              callback)
                            || (issuing && phase == Phase::Armed
                                && WebGpuNativeCompletionReceipt::matchesPrepared(
                                    slot->nativeReceipt,
                                    command->device_,
                                    command->queue_,
                                    kind,
                                    slot.get(),
                                    callback))))
                return "RHI-WEBGPU-ROOT-ISSUER-RECEIPT";
        }
    }
    return state->validateRetainedMapReceipts(admission, command->device_, command->queue_);
}

const char* WebGpuOrdinaryRecording::validateRootLive() const noexcept
{
    const auto state = rootCanonicalState();
    if (const auto* id = WebGpuCanonicalCallbackCustody::validateRootRetainedState(state))
        return id;
    if (state->recording.get() != this || !canonicalIssuanceComplete_
        || !canonicalEvidence_.proven() || !state->recordingCallbacksDrained())
        return "RHI-WEBGPU-ROOT-RECORDING-SCOPES-UNPROVEN";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto& native = *command_->impl_;
    return native.encoder && native.finished && native.commandBuffer && !native.renderPass
            && !native.computePass
        ? nullptr
        : "RHI-WEBGPU-ROOT-RECORDING-NATIVE-STATE";
#else
    return "RHI-WEBGPU-ROOT-RECORDING-NATIVE-UNAVAILABLE";
#endif
}

void WebGpuOrdinaryRecording::emitRoot(void* userdata) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto& context = *static_cast<RootEmission*>(userdata);
    auto& self = *context.self;
    auto& native = *self.command_->impl_;
    const auto& program = *context.program;
    auto encoder = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    encoder.label = webGpuNativeStringView("RHI original BindlessRoot/1 encoder");
    native.encoder = wgpuDeviceCreateCommandEncoder(native.device, &encoder);
    if (!native.encoder) {
        self.failure_ = "RHI-WEBGPU-ROOT-RECORDING-ENCODER";
        return;
    }
    using K = rhi::CommandUsageKind;
    for (const auto& record : program.records()) {
        if (self.failure_)
            break;
        switch (record.original.kind) {
        case K::ClearSnapshotFault:
            wgpuCommandEncoderClearBuffer(
                native.encoder, record.destination, 0u, sizeof(std::uint32_t));
            break;
        case K::BeginComputePass: {
            auto pass = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            pass.label = webGpuNativeStringView("RHI original BindlessRoot/1 compute");
            native.computePass = wgpuCommandEncoderBeginComputePass(native.encoder, &pass);
            if (!native.computePass)
                self.failure_ = "RHI-WEBGPU-ROOT-RECORDING-COMPUTE-PASS";
            break;
        }
        case K::BeginRenderPass:
            native.renderPass = wgpuCommandEncoderBeginRenderPass(
                native.encoder, &program.passes_.values()[record.passIndex].renderPass);
            if (!native.renderPass)
                self.failure_ = "RHI-WEBGPU-ROOT-RECORDING-RENDER-PASS";
            break;
        case K::BindPreparedGraphicsBinding:
            wgpuRenderPassEncoderSetPipeline(native.renderPass, record.renderPipeline);
            wgpuRenderPassEncoderSetBindGroup(
                native.renderPass, record.groupIndex, record.group, 0u, nullptr);
            for (const auto& group : record.closure->ordinaryGroups_)
                if (group.group)
                    wgpuRenderPassEncoderSetBindGroup(
                        native.renderPass, group.groupIndex, group.group, 0u, nullptr);
            break;
        case K::SetViewport:
            wgpuRenderPassEncoderSetViewport(native.renderPass,
                record.viewport.x,
                record.viewport.y,
                record.viewport.width,
                record.viewport.height,
                record.viewport.minDepth,
                record.viewport.maxDepth);
            break;
        case K::PushConstants:
            wgpuRenderPassEncoderSetBindGroup(native.renderPass,
                program.pushGroupIndex_,
                program.pushGroup_,
                1u,
                &record.pushSlice.dynamicOffset);
            break;
        case K::SetScissor:
            wgpuRenderPassEncoderSetScissorRect(native.renderPass,
                record.scissor.x,
                record.scissor.y,
                record.scissor.width,
                record.scissor.height);
            break;
        case K::BindVertexBuffer:
            wgpuRenderPassEncoderSetVertexBuffer(native.renderPass,
                record.physicalVertexSlot,
                record.source,
                record.vertex.offset,
                record.bufferBindingBytes);
            break;
        case K::BindIndexBuffer:
            wgpuRenderPassEncoderSetIndexBuffer(native.renderPass,
                record.source,
                record.index.format == rhi::IndexFormat::Uint16 ? WGPUIndexFormat_Uint16
                                                                : WGPUIndexFormat_Uint32,
                record.index.offset,
                record.bufferBindingBytes);
            break;
        case K::DrawIndexedIndirect:
            for (std::uint32_t i = 0u; i < record.indirect.count; ++i)
                wgpuRenderPassEncoderDrawIndexedIndirect(native.renderPass,
                    record.source,
                    record.indirect.offset + std::uint64_t(i) * record.indirect.stride);
            break;
        case K::DrawIndexed:
            wgpuRenderPassEncoderDrawIndexed(native.renderPass,
                record.indexed.indices,
                record.indexed.instances,
                record.indexed.firstIndex,
                record.indexed.vertexOffset,
                record.indexed.firstInstance);
            break;
        case K::Draw:
            wgpuRenderPassEncoderDraw(native.renderPass,
                record.draw.vertices,
                record.draw.instances,
                record.draw.firstVertex,
                record.draw.firstInstance);
            break;
        case K::EndRenderPass:
            wgpuRenderPassEncoderEnd(native.renderPass);
            wgpuRenderPassEncoderRelease(native.renderPass);
            native.renderPass = nullptr;
            break;
        case K::BindPreparedComputeBinding:
            wgpuComputePassEncoderSetPipeline(native.computePass, record.computePipeline);
            wgpuComputePassEncoderSetBindGroup(
                native.computePass, record.groupIndex, record.group, 0u, nullptr);
            for (const auto& group : record.closure->ordinaryGroups_)
                if (group.group)
                    wgpuComputePassEncoderSetBindGroup(
                        native.computePass, group.groupIndex, group.group, 0u, nullptr);
            break;
        case K::Dispatch:
            wgpuComputePassEncoderDispatchWorkgroups(
                native.computePass, record.dispatch.x, record.dispatch.y, record.dispatch.z);
            break;
        case K::EndComputePass:
            wgpuComputePassEncoderEnd(native.computePass);
            wgpuComputePassEncoderRelease(native.computePass);
            native.computePass = nullptr;
            break;
        case K::BufferCopy:
            wgpuCommandEncoderCopyBufferToBuffer(native.encoder,
                record.source,
                record.copy.sourceOffsetBytes,
                record.destination,
                record.copy.destinationOffsetBytes,
                record.copy.sizeBytes);
            break;
        case K::TextureBarrier:
        case K::BufferBarrier:
        case K::RetainIndexedSnapshot:
        case K::BeginTransferPass:
        case K::EndTransferPass:
            // Frozen RootState/4 barriers are outside passes. Encoder command
            // order supplies the declared clear/compute/copy scope visibility.
            break;
        case K::Finish: {
            auto desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            desc.label = webGpuNativeStringView("RHI original BindlessRoot/1 command buffer");
            native.commandBuffer = wgpuCommandEncoderFinish(native.encoder, &desc);
            native.finished = true;
            if (!native.commandBuffer)
                self.failure_ = "RHI-WEBGPU-ROOT-RECORDING-FINISH";
            break;
        }
        default:
            self.failure_ = "RHI-WEBGPU-ROOT-RECORDING-OPERATION";
            break;
        }
    }
#else
    (void)userdata;
#endif
}

const char* WebGpuOrdinaryRecording::recordRoot(
    std::shared_ptr<WebGpuOrdinaryRecording> self) noexcept
{
    using S = RootState;
    const auto state = self ? self->rootCanonicalState() : nullptr;
    if (!state || state->recording != self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ROOT-ISSUER-ORIGINAL-RECORDING";
    if (self->status_ != Status::Prepared)
        return "RHI-WEBGPU-ROOT-RECORDING-ONE-SHOT";
    const auto* graph = state->pool.get(state->ticket);
    if (!graph)
        return "RHI-WEBGPU-ROOT-ISSUER-RETIRED";
    const auto cold = graph->command;
    if (const auto* id = cold->validate())
        return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-ROOT-RECORDING-NATIVE-UNAVAILABLE";
#else
    const auto& seal = cold->originalAdmission()->originalReservation()->originalSeal();
    core::pmr::String faultDiagnostic{&core::defaultCpuMemoryResource()};
    try {
        graph->fault = rhi::OriginalCommandBufferReadbackBinding::acquireOriginalFaultCustody(
            cold->originalAdmission()->originalReadbackBinding(), faultDiagnostic);
    } catch (const std::bad_alloc&) {
        return "RHI-WEBGPU-ROOT-RECORDING-FAULT-ALLOCATION";
    }
    if (!graph->fault.valid())
        return "RHI-WEBGPU-ROOT-RECORDING-FAULT-CUSTODY";
    if (const auto* id = graph->additionalFaults.acquire(
            cold->originalAdmission()->originalReadbackSequence(), graph->fault, faultDiagnostic)) {
        graph->fault.release();
        return id;
    }
    if (const auto* id = seal->applyOriginalRecordingPlan()) {
        graph->additionalFaults.release();
        graph->fault = {};
        return id;
    }
    if (const auto* id = WebGpuCanonicalCallbackCustody::validateRootRetainedState(state)) {
        seal->rollbackOriginalRecordingPlan();
        state->cancel(id);
        return id;
    }
    self->status_ = Status::RecordingPending;
    for (std::size_t i = S::recordingFirst; i < S::submissionFirst; ++i) {
        state->slots[i]->owner = state;
        state->slots[i]->gate.arm();
    }
    const bool pushed = WebGpuNativeCompletionReceipt::pushScopes(cold->device_);
    if (pushed) {
        RootEmission context{self.get(),
            cold->originalNativeProgram(),
            cold->originalAdmission()->originalClosure().get()};
        if (!WebGpuNativeInvocation::guarded(&emitRoot, &context)) {
            self->status_ = Status::NativeInvocationUncertain;
            self->failure_ = "RHI-WEBGPU-ROOT-RECORDING-INVOCATION-UNCERTAIN";
            state->quarantine(self->failure_);
        }
    } else {
        self->status_ = Status::NativeInvocationUncertain;
        self->failure_ = "RHI-WEBGPU-ROOT-RECORDING-SCOPE-PUSH-UNCERTAIN";
        state->quarantine(self->failure_);
    }
    for (std::size_t i = S::recordingFirst; i < S::submissionFirst; ++i) {
        auto& slot = *state->slots[i];
        if (!pushed || !WebGpuNativeCompletionReceipt::bind(slot.nativeReceipt)) {
            WebGpuNativeCompletionReceipt::release(slot.nativeReceipt);
            slot.nativeReceipt = 0u;
            slot.gate.detach();
            slot.owner.reset();
            self->canonicalEvidence_.observe(i - S::recordingFirst, false);
            if (!self->failure_)
                self->failure_ = "RHI-WEBGPU-ROOT-RECORDING-REGISTRATION-FAILED";
        }
    }
    self->canonicalIssuanceComplete_ = true;
    self->finishCanonicalRecording();
    state->releaseTerminalIfDrained();
    return self->failure_;
#endif
}

const char* WebGpuBindlessRootIssuerPreparation::recordOriginal(
    const std::shared_ptr<const WebGpuBindlessRootIssuerPreparation>& self,
    const rhi::BindlessRequirements::OriginalExecution& request) noexcept
{
    using C = rhi::OriginalBindlessRootNativeExecutionContract;
    if (const auto* id = C::validateIntent(request))
        return id;
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ROOT-ISSUER-OWNER";
    return self->recordExecution(request);
}

const char* WebGpuBindlessRootIssuerPreparation::recordExecution(
    const rhi::BindlessRequirements::OriginalExecution& request) const noexcept
{
    using C = rhi::OriginalBindlessRootNativeExecutionContract;
    if (const auto* id = validate())
        return id;
    const auto state = custody_->state_;
    const auto* graph = state->pool.get(state->ticket);
    if (const auto* id = C::validateProfile(request,
            graph->command->originalAdmission()
                ->originalClosure()
                ->originalRoot()
                ->originalSnapshot()
                ->plan(),
            graph->command->originalAdmission()
                ->originalReservation()
                ->originalSeal()
                ->originalBudget()))
        return id;
    state->recordedReadbackCompletionVersion = request.contractVersion;
    return WebGpuOrdinaryRecording::recordRoot(state->recording);
}

const char* WebGpuBindlessRootIssuerPreparation::submitOriginal(
    const std::shared_ptr<const WebGpuBindlessRootIssuerPreparation>& self,
    const rhi::BindlessRequirements::OriginalExecution& request) noexcept
{
    if (const auto* id = rhi::OriginalBindlessRootNativeExecutionContract::validateIntent(request))
        return id;
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-ROOT-ISSUER-OWNER";
    return self->submitExecution(request);
}

const char* WebGpuBindlessRootIssuerPreparation::submitExecution(
    const rhi::BindlessRequirements::OriginalExecution& request) const noexcept
{
    const auto state = custody_->state_;
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    if (state->recordedReadbackCompletionVersion != request.contractVersion || !state->submission)
        return "RHI-ORIGINAL-ROOT-EXECUTION-COMPLETION";
    return WebGpuOrdinarySubmission::submitCanonical(state->submission);
}

bool WebGpuBindlessRootIssuerPreparation::recordingProven() const noexcept
{
    return custody_->state_->recording && custody_->state_->recording->hasRecordedCommand();
}

bool WebGpuBindlessRootIssuerPreparation::submissionCompleted() const noexcept
{
    return custody_->state_->submissionCompleted;
}

const char* WebGpuBindlessRootIssuerPreparation::executionDiagnostic() const noexcept
{
    return custody_->state_->terminalDiagnostic;
}

const char* WebGpuOrdinaryRecording::validateTypedLive() const noexcept
{
    const auto state = typedCanonicalState();
    if (const auto* id = WebGpuCanonicalCallbackCustody::validateTypedRetainedState(state))
        return id;
    const auto* graph = state->pool.get(state->ticket);
    if (state->recording.get() != this || !canonicalIssuanceComplete_
        || !canonicalEvidence_.proven() || !state->recordingCallbacksDrained())
        return "RHI-WEBGPU-TYPED-RECORDING-SCOPES-UNPROVEN";
    const auto& root = graph->command->originalAdmission()->originalRoot();
    if (root->status() != rhi::OriginalCommandStateTransaction::Status::Pending
        || root->journalEmpty())
        return "RHI-WEBGPU-TYPED-RECORDING-PENDING-OWNERSHIP";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto& native = *command_->impl_;
    return native.encoder && native.finished && native.commandBuffer && !native.renderPass
            && !native.computePass
        ? nullptr
        : "RHI-WEBGPU-TYPED-RECORDING-NATIVE-STATE";
#else
    return "RHI-WEBGPU-TYPED-RECORDING-NATIVE-UNAVAILABLE";
#endif
}

void WebGpuOrdinaryRecording::emitTyped(void* userdata) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto* context = static_cast<TypedEmission*>(userdata);
    auto* self = context->self;
    auto* native = self->command_->impl_.get();
    const auto* payload = context->payload;
    auto encoder = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    encoder.label = webGpuNativeStringView("RHI original typed Prototype/1 encoder");
    native->encoder = wgpuDeviceCreateCommandEncoder(native->device, &encoder);
    if (!native->encoder) {
        self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-ENCODER";
        return;
    }
    using K = rhi::CommandUsageKind;
    using A = rhi::CommandScalarArguments;
    for (const auto& record : payload->records()) {
        if (self->failure_)
            break;
        switch (record.original.kind) {
        case K::BeginComputePass: {
            auto pass = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            pass.label = webGpuNativeStringView(record.text);
            native->computePass = wgpuCommandEncoderBeginComputePass(native->encoder, &pass);
            if (!native->computePass)
                self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-COMPUTE-PASS";
            break;
        }
        case K::EndComputePass:
            wgpuComputePassEncoderEnd(native->computePass);
            wgpuComputePassEncoderRelease(native->computePass);
            native->computePass = nullptr;
            break;
        case K::BindComputePipeline:
            wgpuComputePassEncoderSetPipeline(native->computePass, record.compute);
            break;
        case K::BindGraphicsPipeline:
            wgpuRenderPassEncoderSetPipeline(native->renderPass, record.graphics);
            break;
        case K::BindComputeDescriptor:
        case K::BindRenderDescriptor:
        case K::BindComputeDescriptorTable:
        case K::BindRenderDescriptorTable: {
            std::uint32_t group = UINT32_MAX;
            if ((self->failure_ =
                        WebGpuOriginalTypedNativePayload::originalPhysicalGroup(record, group)))
                break;
            if (record.original.kind == K::BindComputeDescriptor
                || record.original.kind == K::BindComputeDescriptorTable)
                wgpuComputePassEncoderSetBindGroup(
                    native->computePass, group, record.group, record.offsetCount, record.offsets);
            else
                wgpuRenderPassEncoderSetBindGroup(
                    native->renderPass, group, record.group, record.offsetCount, record.offsets);
            break;
        }
        case K::Dispatch: {
            const auto* d = record.scalar.template get<A::Dispatch>();
            wgpuComputePassEncoderDispatchWorkgroups(native->computePass, d->x, d->y, d->z);
            break;
        }
        case K::BeginRenderPass:
            native->renderPass = wgpuCommandEncoderBeginRenderPass(
                native->encoder, &payload->passes()[record.passIndex].renderPass);
            if (!native->renderPass)
                self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-RENDER-PASS";
            break;
        case K::EndRenderPass:
            wgpuRenderPassEncoderEnd(native->renderPass);
            wgpuRenderPassEncoderRelease(native->renderPass);
            native->renderPass = nullptr;
            break;
        case K::SetViewport: {
            const auto* v = record.scalar.template get<rhi::ViewportState>();
            wgpuRenderPassEncoderSetViewport(
                native->renderPass, v->x, v->y, v->width, v->height, v->minDepth, v->maxDepth);
            break;
        }
        case K::SetScissor: {
            const auto* v = record.scalar.template get<rhi::ScissorState>();
            wgpuRenderPassEncoderSetScissorRect(
                native->renderPass, v->x, v->y, v->width, v->height);
            break;
        }
        case K::BindVertexBuffer: {
            const auto* b = record.scalar.template get<A::VertexBinding>();
            wgpuRenderPassEncoderSetVertexBuffer(native->renderPass,
                record.physicalVertexSlot,
                record.buffer,
                b->offset,
                record.bufferBindingBytes);
            break;
        }
        case K::BindIndexBuffer: {
            const auto* b = record.scalar.template get<A::IndexBinding>();
            const auto format = b->format == rhi::IndexFormat::Uint16 ? WGPUIndexFormat_Uint16
                                                                      : WGPUIndexFormat_Uint32;
            wgpuRenderPassEncoderSetIndexBuffer(
                native->renderPass, record.buffer, format, b->offset, record.bufferBindingBytes);
            break;
        }
        case K::DrawIndexed: {
            const auto* d = record.scalar.template get<A::DrawIndexed>();
            wgpuRenderPassEncoderDrawIndexed(native->renderPass,
                d->indices,
                d->instances,
                d->firstIndex,
                d->vertexOffset,
                d->firstInstance);
            break;
        }
        case K::Draw: {
            const auto* d = record.scalar.template get<A::Draw>();
            wgpuRenderPassEncoderDraw(
                native->renderPass, d->vertices, d->instances, d->firstVertex, d->firstInstance);
            break;
        }
        case K::BeginTransferPass:
        case K::EndTransferPass:
            // Frozen grouping is explicit in the original program; WebGPU
            // texture copies belong directly to the same command encoder.
            break;
        case K::TextureReadback:
            for (std::size_t mip = 0u; mip < record.readbackCopyCount && !self->failure_; ++mip) {
                const auto& copy = record.readbackCopies[mip];
                if (copy.commandIndex != record.original.commandIndex) {
                    self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-READBACK-ORDINAL";
                    break;
                }
                self->failure_ =
                    WebGpuNativeOriginalTextureReadbackLowering::emitFrozen(native->encoder,
                        *record.binding->backing().texture,
                        *record.destination->backing().buffer,
                        copy.decision);
            }
            break;
        case K::BufferBarrier:
        case K::TextureBarrier:
        case K::BufferMemoryBarrier:
        case K::TextureMemoryBarrier:
            // Executable frozen dependency operation selected before Pending.
            // Distinct usage scopes synchronize natively; initial/final clauses
            // are exclusively the authenticated original CPU state journal.
            switch (record.dependency->candidate) {
            case WebGpuOriginalTypedDependencyDecision::CandidateOperation::InitialStateLedger:
            case WebGpuOriginalTypedDependencyDecision::CandidateOperation::FinalStateLedger:
            case WebGpuOriginalTypedDependencyDecision::CandidateOperation::
                InterScopeCommandListBoundary:
                break;
            default:
                self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-DEPENDENCY-LOWERING";
                break;
            }
            break;
        case K::Finish: {
            auto desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            desc.label = webGpuNativeStringView("RHI original typed Prototype/1 command buffer");
            native->commandBuffer = wgpuCommandEncoderFinish(native->encoder, &desc);
            native->finished = true;
            if (!native->commandBuffer)
                self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-FINISH";
            break;
        }
        default:
            self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-OPERATION-UNPROVEN";
            break;
        }
    }
#else
    (void)userdata;
#endif
}

const char* WebGpuOrdinaryRecording::recordTyped(
    std::shared_ptr<WebGpuOrdinaryRecording> self) noexcept
{
    using S = TypedState;
    const auto state = self ? self->typedCanonicalState() : nullptr;
    if (!state || state->recording != self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-TYPED-ISSUER-ORIGINAL-RECORDING";
    if (self->status_ != Status::Prepared)
        return "RHI-WEBGPU-TYPED-RECORDING-ONE-SHOT";
    if (const auto* id = WebGpuCanonicalCallbackCustody::validateTypedRetainedState(state))
        return id;
    const auto cold = state->pool.get(state->ticket)->command;
    if (const auto* id = cold->validate())
        return id;
    const auto& payload = cold->originalAdmission()->originalPayload();
    for (const auto& record : payload->records()) {
        if (const auto* d = record.scalar.template get<rhi::CommandScalarArguments::Dispatch>())
            if (const auto* id =
                    rhi::OriginalTypedNativeExecutionContract::validateDispatch(d->x, d->y, d->z))
                return id;
        if (record.original.kind == rhi::CommandUsageKind::BindComputeDescriptor
            || record.original.kind == rhi::CommandUsageKind::BindRenderDescriptor
            || record.original.kind == rhi::CommandUsageKind::BindComputeDescriptorTable
            || record.original.kind == rhi::CommandUsageKind::BindRenderDescriptorTable) {
            std::uint32_t group = UINT32_MAX;
            if (const auto* id =
                    WebGpuOriginalTypedNativePayload::originalPhysicalGroup(record, group))
                return id;
        }
    }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return "RHI-WEBGPU-TYPED-RECORDING-NATIVE-UNAVAILABLE";
#else
    const auto& seal = cold->originalAdmission()->originalReservation()->originalSeal();
    if (const auto* id = seal->applyOriginalRecordingPlan())
        return id;
    // Revalidate initialized contents against the newly enlisted original
    // journal before creating an encoder or issuing native error scopes.
    if (const auto* id = WebGpuCanonicalCallbackCustody::validateTypedRetainedState(state)) {
        seal->rollbackOriginalRecordingPlan();
        state->cancel();
        return id;
    }
    self->status_ = Status::RecordingPending;
    for (std::size_t i = S::recordingFirst; i < S::submissionFirst; ++i) {
        state->slots[i]->owner = state;
        state->slots[i]->gate.arm();
    }
    const bool pushed = WebGpuNativeCompletionReceipt::pushScopes(cold->device_);
    if (pushed) {
        TypedEmission context{self.get(), payload.get()};
        if (!WebGpuNativeInvocation::guarded(&emitTyped, &context)) {
            self->status_ = Status::NativeInvocationUncertain;
            self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-INVOCATION-UNCERTAIN";
            state->quarantine(self->failure_);
        }
    } else {
        self->status_ = Status::NativeInvocationUncertain;
        self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-SCOPE-PUSH-UNCERTAIN";
        state->quarantine(self->failure_);
    }
    for (std::size_t i = S::recordingFirst; i < S::submissionFirst; ++i) {
        auto& slot = *state->slots[i];
        if (!pushed || !WebGpuNativeCompletionReceipt::bind(slot.nativeReceipt)) {
            WebGpuNativeCompletionReceipt::release(slot.nativeReceipt);
            slot.nativeReceipt = 0u;
            slot.gate.detach();
            slot.owner.reset();
            self->canonicalEvidence_.observe(
                static_cast<std::uint32_t>(i - S::recordingFirst), false);
            if (!self->failure_)
                self->failure_ = "RHI-WEBGPU-TYPED-RECORDING-REGISTRATION-FAILED";
        }
    }
    self->canonicalIssuanceComplete_ = true;
    self->finishCanonicalRecording();
    state->releaseTerminalIfDrained();
    return self->failure_;
#endif
}

const char* WebGpuOriginalTypedIssuerPreparation::recordOriginal(
    const std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>& self,
    const rhi::OriginalTypedNativeExecutionRequest& request) noexcept
{
    using C = rhi::OriginalTypedNativeExecutionContract;
    if (const auto* id = C::validateIntent(request))
        return id;
    const auto decision =
        arp::rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(rhi::Backend::WebGPU,
            request.contractVersion,
            request.readbackCompletion.contractVersion,
            request.generalSubmission.contractVersion);
    if (!decision.recordingAvailable)
        return decision.diagnostic;
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-TYPED-ISSUER-OWNER";
    if (const auto* id = C::validateProfile(request, self->request_.contractVersion))
        return id;
    auto& state = *self->custody_->state_;
    if (state.terminalDiagnostic)
        return state.terminalDiagnostic;
    if (!state.recording || state.recording->status_ != WebGpuOrdinaryRecording::Status::Prepared
        || state.maySubmit || state.submissionCompleted)
        return "RHI-WEBGPU-TYPED-RECORDING-ONE-SHOT";
    if (const auto* id = self->validate())
        return id;
    if (request.readbackCompletion.contractVersion) {
        const auto* graph = state.pool.get(state.ticket);
        if (const auto* id = C::validateReadbackProfile(request,
                graph->command->originalAdmission()->requirements(),
                self->custody_->request_))
            return id;
        if (!state.results)
            return "RHI-TYPED-NATIVE-READBACK-COMPLETION-BINDING";
    } else if (state.results)
        return "RHI-TYPED-NATIVE-READBACK-COMPLETION-REQUIRED";
    state.recordedReadbackCompletionVersion = request.readbackCompletion.contractVersion;
    state.recordedGeneralSubmissionVersion = request.generalSubmission.contractVersion;
    return WebGpuOrdinaryRecording::recordTyped(self->custody_->state_->recording);
}

const char* WebGpuOriginalTypedIssuerPreparation::submitOriginal(
    const std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>& self,
    const rhi::OriginalTypedNativeExecutionRequest& request) noexcept
{
    using C = rhi::OriginalTypedNativeExecutionContract;
    if (const auto* id = C::validateIntent(request))
        return id;
    const auto decision =
        arp::rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(rhi::Backend::WebGPU,
            request.contractVersion,
            request.readbackCompletion.contractVersion,
            request.generalSubmission.contractVersion);
    if (!decision.submissionAvailable)
        return decision.diagnostic;
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-WEBGPU-TYPED-ISSUER-OWNER";
    if (const auto* id = C::validateProfile(request, self->request_.contractVersion))
        return id;
    const auto state = self->custody_->state_;
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    if (request.contractVersion == 6u || request.contractVersion == 7u
        || request.contractVersion == 8u)
        if (const auto* id = C::validateRecordedGeneralSubmission(
                request, state->recordedGeneralSubmissionVersion))
            return id;
    if (state->recordedReadbackCompletionVersion != UINT32_MAX
        && state->recordedReadbackCompletionVersion != request.readbackCompletion.contractVersion)
        return "RHI-TYPED-NATIVE-READBACK-COMPLETION-RECORDED-PROFILE";
    if (request.readbackCompletion.contractVersion) {
        const auto* graph = state->pool.get(state->ticket);
        if (!graph)
            return "RHI-WEBGPU-TYPED-CALLBACK-CAPACITY";
        if (const auto* id = C::validateReadbackProfile(request,
                graph->command->originalAdmission()->requirements(),
                self->custody_->request_))
            return id;
        if (!state->results)
            return "RHI-TYPED-NATIVE-READBACK-COMPLETION-BINDING";
    }
    if (!state->submission || state->maySubmit || state->submissionCompleted)
        return "RHI-WEBGPU-TYPED-SUBMISSION-ONE-SHOT";
    return WebGpuOrdinarySubmission::submitTyped(state->submission);
}

bool WebGpuOriginalTypedIssuerPreparation::hasOriginalRecordedCommand() const noexcept
{
    const auto& state = *custody_->state_;
    return state.recording && state.recording->hasRecordedCommand();
}

bool WebGpuOriginalTypedIssuerPreparation::hasOriginalCompletedSubmission() const noexcept
{
    return custody_->state_->submissionCompleted;
}

const char* WebGpuOriginalTypedIssuerPreparation::validateGeneralOutputIssuer(
    const std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>& self,
    const std::shared_ptr<const rhi::OriginalCommandStateTransaction>& root,
    bool completed,
    std::uint32_t outputVersion) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-GENERAL-OUTPUT-ISSUER-OWNER";
    if (!root || !root->hasQuotaBoundSharedOwner(root)
        || !core::SharedOwnerProjection::sameOwner(root, self->originalRoot_.lock()))
        return "RHI-GENERAL-OUTPUT-ISSUER-ROOT";
    if (const auto* id =
            rhi::OriginalGeneralOutputSnapshotContract::validateUpstreamProfile(outputVersion,
                self->request_.contractVersion,
                root->hasOriginalGeneralShaderResourceProfile(),
                root->hasOriginalFiniteGeneralShaderResourceProfile(),
                root->originalContentPlanContractVersion()))
        return id;
    if (!completed) {
        if (root->status() != rhi::OriginalCommandStateTransaction::Status::Prepared
            || !root->journalEmpty())
            return "RHI-GENERAL-OUTPUT-ISSUER-PREPARED";
        return validateOriginal(self);
    }
    if (const auto* id = validateGeneralOutputCompletionEvidence(self, root, outputVersion))
        return id;
    // Public completion always validates every final observation.
    return root->validate();
}

const char* WebGpuOriginalTypedIssuerPreparation::validateGeneralOutputCompletionEvidence(
    const std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>& self,
    const std::shared_ptr<const rhi::OriginalCommandStateTransaction>& root,
    std::uint32_t outputVersion) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-GENERAL-OUTPUT-ISSUER-OWNER";
    if (!root || !root->hasQuotaBoundSharedOwner(root)
        || !core::SharedOwnerProjection::sameOwner(root, self->originalRoot_.lock()))
        return "RHI-GENERAL-OUTPUT-ISSUER-ROOT";
    if (const auto* id =
            rhi::OriginalGeneralOutputSnapshotContract::validateUpstreamProfile(outputVersion,
                self->request_.contractVersion,
                root->hasOriginalGeneralShaderResourceProfile(),
                root->hasOriginalFiniteGeneralShaderResourceProfile(),
                root->originalContentPlanContractVersion()))
        return id;
    const auto& state = self->custody_->state_;
    if (!state || !state->sharedOwnerMatches(state))
        return "RHI-GENERAL-OUTPUT-ISSUER-STATE";
    if (state->terminalDiagnostic)
        return state->terminalDiagnostic;
    if (!state->submissionCompleted || !state->queueNativeReceiptReceived
        || state->recordedGeneralSubmissionVersion != outputVersion
        || !state->foreignCallbacksDrainedExceptLoss())
        return "RHI-GENERAL-OUTPUT-ISSUER-INCOMPLETE";
    if (root->status() != rhi::OriginalCommandStateTransaction::Status::Committed
        || !root->journalEmpty())
        return "RHI-GENERAL-OUTPUT-ISSUER-COMMITTED";
    return nullptr;
}

const char* WebGpuCanonicalCallbackCustody::validateGeneralOutputPending(
    const std::shared_ptr<const WebGpuOriginalGeneralOutputSnapshot>& snapshot,
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal,
    const rhi::GpuTexture::CommittedStateObservation& frozen) noexcept
{
    if (const auto* id = WebGpuOriginalGeneralOutputSnapshot::validateOriginal(snapshot))
        return id;
    if (const auto* id =
            WebGpuOriginalTypedIssuerPreparation::validateGeneralOutputCompletionEvidence(
                snapshot->originalIssuer(),
                snapshot->originalRoot(),
                snapshot->originalBudget().contractVersion))
        return id;
    if (!seal)
        return "RHI-GENERAL-OUTPUT-PENDING-SEAL-OWNER";
    if (frozen.range() != snapshot->originalBudget().range)
        return "RHI-GENERAL-OUTPUT-PENDING-SNAPSHOT-RANGE";
    return seal->validateGeneralOutputReadPending(seal,
        snapshot->originalRoot(),
        *snapshot->originalOutput()->originalObject(),
        frozen,
        snapshot->originalBudget().contractVersion);
}

std::shared_ptr<const WebGpuOriginalTypedNativePayload>
WebGpuOriginalTypedIssuerPreparation::preparedGeneralOutputPayload() const noexcept
{
    const auto& state = custody_->state_;
    const auto* graph = state->pool.get(state->ticket);
    return graph && graph->command ? graph->command->originalAdmission()->originalPayload()
                                   : nullptr;
}

std::shared_ptr<const WebGpuOriginalGeneralOutputSnapshot>
WebGpuOriginalGeneralOutputSnapshot::prepareOriginal(std::shared_ptr<const Issuer> issuer,
    std::shared_ptr<const Root> root,
    std::shared_ptr<const rhi::OriginalCommandTextureLease> output,
    const Budget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    using C = rhi::OriginalGeneralOutputSnapshotContract;
    if ((diagnostic = C::validateIntent(budget)))
        return {};
    const auto lowering = rhi::semantics::OriginalGeneralOutputSnapshotLowering::lowering(
        rhi::Backend::WebGPU, budget.contractVersion);
    if (!lowering.privateAssociationAvailable) {
        diagnostic = lowering.diagnostic;
        return {};
    }
    if ((diagnostic =
                Issuer::validateGeneralOutputIssuer(issuer, root, false, budget.contractVersion)))
        return {};
    if ((diagnostic = rhi::OriginalCommandTextureLease::validateOriginal(output)))
        return {};
    const auto& graph = root->originalGraph();
    const auto& usage = *graph->originalInputs()->originalScalars()->originalUsage();
    const auto& sources = usage.originalSources()->sources();
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes
        || !core::SharedOwnerProjection::sameOwner(quota, output->originalMetadataQuota())
        || !core::SharedOwnerProjection::sameOwner(
            quota, usage.originalSources()->originalMetadataQuota())) {
        diagnostic = "RHI-GENERAL-OUTPUT-SNAPSHOT-QUOTA";
        return {};
    }
    WebGpuTransferResourceObservation native;
    if ((diagnostic = observeOriginalWebGpuResource(*output->originalObject(), native)))
        return {};
    if ((diagnostic = C::inspect(output->originalObject()->desc(),
             budget,
             issuer->owners_,
             usage.commands().size(),
             native.allocationBytes)))
        return {};
    Capture captured{Access{}};
    std::uint64_t visits = usage.commands().size(), matches = 0u;
    using Projection = rhi::common::OriginalTypedUsageProjection;
    const auto payload = issuer->preparedGeneralOutputPayload();
    if (!payload) {
        diagnostic = "RHI-GENERAL-OUTPUT-SNAPSHOT-PAYLOAD";
        return {};
    }
    const auto& decision = payload->originalDecision();
    if (budget.contractVersion == 3u) {
        Attachment attachment{};
        if ((diagnostic = C::inspectPreparedGraphicsAttachment<Projection>(root,
                 decision->originalClosure()->originalStream(),
                 output,
                 budget,
                 decision->originalRequest().projection.usages,
                 issuer->owners_,
                 native.allocationBytes,
                 attachment)))
            return {};
        captured = attachment;
    } else {
        const auto inspected =
            Projection::visitStateBound(decision->originalClosure()->originalStream(),
                graph,
                decision->originalRequest().projection.usages,
                [&](const Projection::Point& point) noexcept -> const char* {
                    if (visits == budget.maxJoinVisits)
                        return "RHI-GENERAL-OUTPUT-SNAPSHOT-CAPACITY";
                    ++visits;
                    const auto* texture = std::get_if<Projection::TexturePoint>(&point);
                    if (!texture)
                        return nullptr;
                    const auto& access = texture->original;
                    if (access.kind != Access::Kind::TextureView
                        || access.state != rhi::ResourceState::ShaderWrite
                        || access.textureParentSourceIndex >= sources.size()
                        || access.commandIndex >= usage.commands().size()
                        || usage.commands()[access.commandIndex].kind
                            != rhi::CommandUsageKind::Dispatch)
                        return nullptr;
                    const auto* parent =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                            &sources[access.textureParentSourceIndex]);
                    if (!parent || !core::SharedOwnerProjection::sameOwner(*parent, output))
                        return nullptr;
                    if (access.stages != rhi::PipelineStage::ComputeShader
                        || access.access != rhi::ResourceAccess::ShaderWrite
                        || access.textureRange != budget.range)
                        return "RHI-GENERAL-OUTPUT-SNAPSHOT-WRITE-PROFILE";
                    captured = access;
                    ++matches;
                    return nullptr;
                });
        if ((diagnostic = inspected.diagnostic))
            return {};
        if ((diagnostic = C::inspect(output->originalObject()->desc(),
                 budget,
                 issuer->owners_,
                 visits,
                 native.allocationBytes)))
            return {};
        if (matches != 1u) {
            diagnostic = "RHI-GENERAL-OUTPUT-SNAPSHOT-OUTPUT-BINDING";
            return {};
        }
    }
    // Root/output/native payloads overlap the existing issuer graph. Only this
    // wrapper is a new unique owner; its metadata uses the original source quota.
    if (budget.maxRetainedOwners > issuer->request_.maxRetainedOwners) {
        diagnostic = "RHI-GENERAL-OUTPUT-SNAPSHOT-OWNER-CEILING";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result =
            shareOriginal(core::makeUnique<WebGpuOriginalGeneralOutputSnapshot>(ConstructionKey{},
                std::move(issuer),
                std::move(root),
                std::move(output),
                budget,
                captured,
                std::move(native),
                decision->originalRequest().projection.usages));
        if ((diagnostic = result->validateIdentity()))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-GENERAL-OUTPUT-SNAPSHOT-ALLOCATION";
        return {};
    }
}

const char* WebGpuOriginalGeneralOutputSnapshot::validateIdentity() const noexcept
{
    using C = rhi::OriginalGeneralOutputSnapshotContract;
    if (const auto* id = C::validateIntent(budget_))
        return id;
    if (!issuer_ || !issuer_->hasQuotaBoundSharedOwner(issuer_) || !root_
        || !root_->hasQuotaBoundSharedOwner(root_)
        || !core::SharedOwnerProjection::sameOwner(root_, issuer_->originalRoot_.lock()))
        return "RHI-GENERAL-OUTPUT-SNAPSHOT-ORIGINAL-ROOT";
    if (const auto* id = C::validateUpstreamProfile(budget_.contractVersion,
            issuer_->request_.contractVersion,
            root_->hasOriginalGeneralShaderResourceProfile(),
            root_->hasOriginalFiniteGeneralShaderResourceProfile(),
            root_->originalContentPlanContractVersion()))
        return id;
    if (const auto* id = rhi::OriginalCommandTextureLease::validateOriginal(output_))
        return id;
    const auto& retainedSources = root_->originalGraph()
                                      ->originalInputs()
                                      ->originalScalars()
                                      ->originalUsage()
                                      ->originalSources();
    if (const auto* id = rhi::OriginalCommandResourceSources::validateOriginal(retainedSources))
        return id;
    if (!native_.matches() || native_.id != output_->resource())
        return "RHI-GENERAL-OUTPUT-SNAPSHOT-NATIVE";
    const auto& usage =
        *root_->originalGraph()->originalInputs()->originalScalars()->originalUsage();
    const auto& sources = usage.originalSources()->sources();
    if (!originalMetadataQuota()
        || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes
        || !core::SharedOwnerProjection::sameOwner(
            originalMetadataQuota(), output_->originalMetadataQuota()))
        return "RHI-GENERAL-OUTPUT-SNAPSHOT-QUOTA";
    if (const auto* id = C::inspect(output_->originalObject()->desc(),
            budget_,
            issuer_->owners_,
            usage.commands().size(),
            native_.allocationBytes))
        return id;
    if (budget_.maxRetainedOwners > issuer_->request_.maxRetainedOwners)
        return "RHI-GENERAL-OUTPUT-SNAPSHOT-OWNER-CEILING";
    if (budget_.contractVersion == 3u) {
        const auto* captured = std::get_if<Attachment>(&capture_);
        if (!captured)
            return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-CAPTURE-ROLE";
        Attachment current{};
        if (root_->status() == Root::Status::Prepared) {
            if (const auto* id = Issuer::validateGeneralOutputIssuer(issuer_, root_, false, 3u))
                return id;
            const auto payload = issuer_->preparedGeneralOutputPayload();
            if (!payload)
                return "RHI-GENERAL-OUTPUT-SNAPSHOT-PAYLOAD";
            const auto& decision = payload->originalDecision();
            if (const auto* id =
                    C::inspectPreparedGraphicsAttachment<rhi::common::OriginalTypedUsageProjection>(
                        root_,
                        decision->originalClosure()->originalStream(),
                        output_,
                        budget_,
                        usageRequest_,
                        issuer_->owners_,
                        native_.allocationBytes,
                        current))
                return id;
        } else {
            const auto lowering =
                rhi::semantics::OriginalGeneralOutputSnapshotLowering::completedAssociationLowering(
                    rhi::Backend::WebGPU, 3u);
            if (!lowering.privateAssociationAvailable)
                return lowering.diagnostic;
            // The original command slot can be retired after completion. The
            // root retains the stream; replay only after authentic receipt drain.
            if (const auto* id =
                    Issuer::validateGeneralOutputCompletionEvidence(issuer_, root_, 3u))
                return id;
            if (const auto* id = C::inspectCompletedGraphicsAttachment<
                    rhi::common::OriginalTypedUsageProjection>(root_,
                    output_,
                    budget_,
                    usageRequest_,
                    issuer_->owners_,
                    native_.allocationBytes,
                    current))
                return id;
        }
        return captured->original.matches(current.original)
                && captured->resource == current.resource && captured->scope.matches(current.scope)
                && captured->access.state == current.access.state
                && captured->access.stages == current.access.stages
                && captured->access.access == current.access.access
                && captured->registrationGeneration == current.registrationGeneration
            ? nullptr
            : "RHI-GRAPHICS-OUTPUT-SNAPSHOT-CAPTURE-MISMATCH";
    }
    const auto* descriptorCapture = std::get_if<Access>(&capture_);
    if (!descriptorCapture)
        return "RHI-GENERAL-OUTPUT-SNAPSHOT-CAPTURE-ROLE";
    const auto& access = *descriptorCapture;
    if (access.descriptorSourceIndex >= sources.size()
        || access.textureViewSourceIndex >= sources.size()
        || access.textureParentSourceIndex >= sources.size())
        return "RHI-GENERAL-OUTPUT-SNAPSHOT-SOURCE";
    const auto* descriptor =
        std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
            &sources[access.descriptorSourceIndex]);
    const auto* view = std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
        &sources[access.textureViewSourceIndex]);
    const auto* parent = std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
        &sources[access.textureParentSourceIndex]);
    if (!descriptor || !view || !parent || !core::SharedOwnerProjection::sameOwner(*parent, output_)
        || !core::SharedOwnerProjection::sameOwner((*view)->originalParentLease(), output_))
        return "RHI-GENERAL-OUTPUT-SNAPSHOT-OUTPUT-BINDING";
    if (const auto* id = rhi::OriginalCommandTextureViewSource::validateOriginal(*view))
        return id;
    if (const auto* id = rhi::OriginalCommandDescriptorSetSource::validateOriginal(*descriptor))
        return id;
    if ((*descriptor)->frozenContentRevision() != access.descriptorContentRevision
        || output_->originalObject()->originalResourceRegistration().generation()
            != access.textureParentRegistrationGeneration)
        return "RHI-GENERAL-OUTPUT-SNAPSHOT-GENERATION";
    return nullptr;
}

const char* WebGpuOriginalGeneralOutputSnapshot::validateCompletedOriginal(
    const std::shared_ptr<const WebGpuOriginalGeneralOutputSnapshot>& self) noexcept
{
    if (const auto* id = validateOriginal(self))
        return id;
    const auto association =
        rhi::semantics::OriginalGeneralOutputSnapshotLowering::completedAssociationLowering(
            rhi::Backend::WebGPU, self->budget_.contractVersion);
    if (!association.privateAssociationAvailable)
        return association.diagnostic;
    if (const auto* id = Issuer::validateGeneralOutputIssuer(
            self->issuer_, self->root_, true, self->budget_.contractVersion))
        return id;
    const auto state =
        self->output_->originalObject()->observeOriginalCommittedState(self->budget_.range);
    const auto expected = self->budget_.contractVersion == 3u ? rhi::ResourceState::ColorAttachment
                                                              : rhi::ResourceState::ShaderWrite;
    return state && state->state() == expected ? nullptr
                                               : "RHI-GENERAL-OUTPUT-SNAPSHOT-COMMITTED-STATE";
}

bool WebGpuCanonicalCallbackCustody::overlapsCpuOwner(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return quota
        && (core::SharedOwnerProjection::overlapsOwner(quota, originalMetadataQuota())
            || core::cpuAllocationOverlapsQuotaOwner(this, quota)
            || state_->overlapsCpuOwner(quota));
}

bool WebGpuOriginalTypedCallbackCustody::overlapsCpuOwner(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return quota
        && (core::SharedOwnerProjection::overlapsOwner(quota, originalMetadataQuota())
            || core::cpuAllocationOverlapsQuotaOwner(this, quota)
            || state_->overlapsCpuOwner(quota));
}

bool WebGpuCanonicalNativeClosure::overlapsCompletedTextureInputOwner(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    for (const auto& entry : entries_.values())
        if (entry.textureContent) {
            const auto& input = *entry.textureContent;
            const auto& custody = input.issuer->originalLowering()->originalCustody();
            if (input.uploadRoot->overlapsCpuOwner(quota) || custody->overlapsCpuOwner(quota)
                || core::SharedOwnerProjection::overlapsOwner(
                    quota, input.source->originalMetadataQuota())
                || overlapsTextureContentSourceOwner(input.source, quota))
                return true;
        }
    return false;
}

bool WebGpuOriginalGeneralOutputSnapshot::overlapsCpuOwner(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return quota
        && (core::SharedOwnerProjection::overlapsOwner(quota, originalMetadataQuota())
            || core::cpuAllocationOverlapsQuotaOwner(this, quota) || root_->overlapsCpuOwner(quota)
            || issuer_->custody_->overlapsCpuOwner(quota));
}

const char* WebGpuOriginalTypedIssuerPreparation::recordingDiagnostic() const noexcept
{
    const auto& state = *custody_->state_;
    return state.terminalDiagnostic ? state.terminalDiagnostic
        : state.recording           ? state.recording->canonicalDiagnostic()
                                    : "RHI-WEBGPU-TYPED-ISSUER-RETIRED";
}

const char* WebGpuOriginalTypedIssuerPreparation::submissionDiagnostic() const noexcept
{
    const auto& state = *custody_->state_;
    return state.submissionCompleted ? nullptr
        : state.terminalDiagnostic   ? state.terminalDiagnostic
        : state.submission           ? state.submission->canonicalDiagnostic()
                                     : "RHI-WEBGPU-TYPED-ISSUER-RETIRED";
}
} // namespace arp::rhi_webgpu

#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
namespace arp::test {
template <class State>
const char* WebGpuPostSuppressionFailureProbeFixture::armState(
    const std::shared_ptr<State>& state, Failure failure) noexcept
{
    using Phase = core::runtime::ExternalCallbackGate::Phase;
    if (!state || failure == Failure::None || state->postSuppressionProbe.failure != Failure::None)
        return "RHI-TEST-POST-SUPPRESSION-CHOICE";
    const auto* graph = state->pool.get(state->ticket);
    if (!graph || state->pool.pending() != 1u || !state->maySubmit || !state->recording
        || state->terminalDiagnostic || state->submissionCompleted
        || state->slots[State::lossIndex]->gate.phase() != Phase::Armed
        || state->foreignCallbacksDrainedExceptLoss()
        || state->postSuppressionProbe.suppressionAttempts)
        return "RHI-TEST-POST-SUPPRESSION-NATIVE-PENDING";
    if (!state->slots[State::lossIndex]->owner)
        return "RHI-TEST-POST-SUPPRESSION-LOSS-REGISTRATION";
    if constexpr (State::typed) {
        if (!state->observerTicket || !state->observedLoss)
            return "RHI-TEST-POST-SUPPRESSION-LOSS-REGISTRATION";
    } else if (!state->slots[State::lossIndex]->nativeState)
        return "RHI-TEST-POST-SUPPRESSION-LOSS-REGISTRATION";
    const auto& root =
        graph->command->originalAdmission()->originalReservation()->originalSeal()->originalRoot();
    if (!root || root->status() != rhi::OriginalCommandStateTransaction::Status::Pending
        || root->journalEmpty())
        return "RHI-TEST-POST-SUPPRESSION-ORIGINAL-ROOT";
    if (failure == Failure::Commit) {
        if constexpr (!State::typed)
            return "RHI-TEST-POST-SUPPRESSION-COMMIT-PROFILE";
        else if (state->results || state->recordedGeneralSubmissionVersion != 2u
            || !root->hasOriginalFiniteGeneralShaderResourceProfile())
            return "RHI-TEST-POST-SUPPRESSION-COMMIT-PROFILE";
    } else if (failure != Failure::Publication || !state->results || !state->publication
        || state->recordedGeneralOutputVersion != 2u)
        return "RHI-TEST-POST-SUPPRESSION-PUBLICATION-PROFILE";
    state->postSuppressionProbe.failure = failure;
    return nullptr;
}

template <class State>
WebGpuPostSuppressionFailureProbeFixture::Facts
WebGpuPostSuppressionFailureProbeFixture::inspectState(const std::shared_ptr<State>& state) noexcept
{
    if (!state)
        return {};
    auto facts = state->postSuppressionProbe;
    const auto& loss = *state->slots[State::lossIndex];
    using Phase = core::runtime::ExternalCallbackGate::Phase;
    facts.nativeQueueReceipt = state->queueNativeReceiptReceived;
    facts.foreignCallbacksDrained = state->foreignCallbacksDrainedExceptLoss();
    facts.lossDetached = loss.gate.phase() == Phase::Detached;
    facts.physicalLossDelivered = loss.gate.phase() == Phase::Delivered;
    facts.lossUserdataCleared = !loss.owner;
    if constexpr (State::typed)
        facts.lossUserdataCleared = facts.lossUserdataCleared && !state->observerTicket;
    else
        facts.lossUserdataCleared = facts.lossUserdataCleared && !loss.nativeState;
    facts.originalRecordReleased = !state->pool.pending() && !state->pool.get(state->ticket)
        && !state->pool.retainedInvalidated(state->ticket);
    return facts;
}

const char* WebGpuPostSuppressionFailureProbeFixture::arm(
    const std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedCallbackCustody>& custody,
    Failure failure) noexcept
{
    return custody && custody->hasQuotaBoundSharedOwner(custody)
        ? armState(custody->state_, failure)
        : "RHI-TEST-POST-SUPPRESSION-OWNER";
}

const char* WebGpuPostSuppressionFailureProbeFixture::arm(
    const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalCallbackCustody>& custody,
    Failure failure) noexcept
{
    return custody && custody->hasQuotaBoundSharedOwner(custody)
        ? armState(custody->state_, failure)
        : "RHI-TEST-POST-SUPPRESSION-OWNER";
}

WebGpuPostSuppressionFailureProbeFixture::Facts WebGpuPostSuppressionFailureProbeFixture::inspect(
    const std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedCallbackCustody>& custody) noexcept
{
    return custody && custody->hasQuotaBoundSharedOwner(custody) ? inspectState(custody->state_)
                                                                 : Facts{};
}

WebGpuPostSuppressionFailureProbeFixture::Facts WebGpuPostSuppressionFailureProbeFixture::inspect(
    const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalCallbackCustody>& custody) noexcept
{
    return custody && custody->hasQuotaBoundSharedOwner(custody) ? inspectState(custody->state_)
                                                                 : Facts{};
}
} // namespace arp::test
#endif
