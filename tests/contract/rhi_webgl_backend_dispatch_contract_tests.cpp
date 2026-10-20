#include "2d/runtime_contract.h"
#include "core/diagnostic_error.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_original_query_binding.h"
#include "rhi/backends/webgpu/webgpu_original_query_record_reservation.h"
#include "rhi/backends/webgpu/webgpu_original_query_slots.h"
#include "rhi/backends/webgpu/webgpu_original_texture_upload_transaction.h"
#include "rhi/backends/webgpu/webgpu_readback_preparation.h"
#include "rhi/backends/webgpu/webgpu_readback_results.h"
#include "rhi/backends/webgpu/webgpu_texture_readback_cold_budget_lowering.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/bindless_recording_validation.h"
#include "rhi/frontend/general_initialized_buffer_content.h"
#include "rhi/frontend/pipeline_creation_validation.h"
#include "rhi/frontend/pipeline_depth_table_lowering_validation.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"
#include "rhi/semantics/bindless_completed_image_lowering.h"
#include "rhi/semantics/bindless_entry_access.h"
#include "rhi/semantics/bindless_graphics_execution_lowering.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "rhi/semantics/bindless_storage_image_lowering.h"
#include "rhi/semantics/command_lowering.h"
#include "rhi/semantics/original_artifact_compute_creation_lowering.h"
#include "rhi/semantics/original_artifact_graphics_creation_lowering.h"
#include "rhi/semantics/render_target_coordinates.h"
#include "tests/fixtures/rhi_webgl_dispatch_decision_assertions.h"
#include "tests/fixtures/webgpu_original_query_source_gate_fixture.h"

#include <array>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Assertions required
#endif
using namespace arp::rhi;

void decisions(bool snapshot)
{
    for (auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(254u),
             static_cast<Backend>(255u)}) {
        bool denied = backend == Backend::WebGL || static_cast<unsigned>(backend) >= 254u;
        if (snapshot && denied)
            continue;
        for (std::uint32_t version : {0u,
                 1u,
                 2u,
                 3u,
                 4u,
                 5u,
                 6u,
                 7u,
                 8u,
                 9u,
                 10u,
                 11u,
                 12u,
                 13u,
                 14u,
                 15u,
                 16u,
                 17u,
                 18u,
                 19u,
                 20u,
                 21u,
                 22u,
                 UINT32_MAX}) {
            for (bool flag : {false, true}) {
                if (snapshot)
                    std::printf("backend=%u version=%u flag=%u\n",
                        static_cast<unsigned>(backend),
                        version,
                        flag);
                dispatch_test::verify("semantics::OriginalBindlessGraphicsPassLowering::lowering",
                    semantics::OriginalBindlessGraphicsPassLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalBindlessRootNativeClosureLowering::lowering",
                    semantics::OriginalBindlessRootNativeClosureLowering::lowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalBindlessRootSourceLowering::lowering",
                    semantics::OriginalBindlessRootSourceLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalBindlessBufferSourceLowering::lowering",
                    semantics::OriginalBindlessBufferSourceLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalRetainedReadbackCorrespondenceLowering::lowering",
                    semantics::OriginalRetainedReadbackCorrespondenceLowering::lowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandTypedSourceLowering::lowering",
                    semantics::OriginalCommandTypedSourceLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandPipelineBindingLowering::lowering",
                    semantics::OriginalCommandPipelineBindingLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandDescriptorBindingLowering::tableLowering",
                    semantics::OriginalCommandDescriptorBindingLowering::tableLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandDescriptorBindingLowering::depthTableLowering",
                    semantics::OriginalCommandDescriptorBindingLowering::depthTableLowering(
                        backend, flag),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandStreamLowering::bindlessRootLowering",
                    semantics::OriginalCommandStreamLowering::bindlessRootLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandStreamLowering::lowering",
                    semantics::OriginalCommandStreamLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandStreamLowering::typedLowering",
                    semantics::OriginalCommandStreamLowering::typedLowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::attachmentReadbackLossBoundLowering",
                    semantics::OriginalCommandNativeClosureLowering::
                        attachmentReadbackLossBoundLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::finiteGraphicsLossBoundLowering",
                    semantics::OriginalCommandNativeClosureLowering::
                        finiteGraphicsLossBoundLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::generalResourcesLossBoundLowering",
                    semantics::OriginalCommandNativeClosureLowering::
                        generalResourcesLossBoundLowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::indexContentLossBoundLowering",
                    semantics::OriginalCommandNativeClosureLowering::indexContentLossBoundLowering(
                        backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::geometryContentLossBoundLowering",
                    semantics::OriginalCommandNativeClosureLowering::
                        geometryContentLossBoundLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::geometryReadbackLossBoundLowering",
                    semantics::OriginalCommandNativeClosureLowering::
                        geometryReadbackLossBoundLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::geometryPendingReadLowering",
                    semantics::OriginalCommandNativeClosureLowering::geometryPendingReadLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::generalPendingReadLowering",
                    semantics::OriginalCommandNativeClosureLowering::generalPendingReadLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::typedLowering",
                    semantics::OriginalCommandNativeClosureLowering::typedLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::typedLossBoundLowering",
                    semantics::OriginalCommandNativeClosureLowering::typedLossBoundLowering(
                        backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::tableLossBoundLowering",
                    semantics::OriginalCommandNativeClosureLowering::tableLossBoundLowering(
                        backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeClosureLowering::tableReadbackLossBoundLowering",
                    semantics::OriginalCommandNativeClosureLowering::tableReadbackLossBoundLowering(
                        backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandNativeClosureLowering::lowering",
                    semantics::OriginalCommandNativeClosureLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandDescriptorSourceLowering::lowering",
                    semantics::OriginalCommandDescriptorSourceLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandDescriptorSourceLowering::generalFiniteLowering",
                    semantics::OriginalCommandDescriptorSourceLowering::generalFiniteLowering(
                        backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandDescriptorSourceLowering::finiteLowering",
                    semantics::OriginalCommandDescriptorSourceLowering::finiteLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandPipelineSourceLowering::lowering",
                    semantics::OriginalCommandPipelineSourceLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandTextureUploadPackingSourceLowering::lowering",
                    semantics::OriginalCommandTextureUploadPackingSourceLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalTextureUploadStagingLowering::lowering",
                    semantics::OriginalTextureUploadStagingLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalBindlessRootCallbackColdLowering::lowering",
                    semantics::OriginalBindlessRootCallbackColdLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalBindlessRootColdCommandLowering::lowering",
                    semantics::OriginalBindlessRootColdCommandLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalBindlessRootCallbackCustodyLowering::lowering",
                    semantics::OriginalBindlessRootCallbackCustodyLowering::lowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalBindlessRootIssuerLowering::lowering",
                    semantics::OriginalBindlessRootIssuerLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering",
                    semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandNativeLifecycleLowering::lowering",
                    semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandNativeColdLowering::lowering",
                    semantics::OriginalCommandNativeColdLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeColdLowering::generalOutputLowering",
                    semantics::OriginalCommandNativeColdLowering::generalOutputLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandNativeColdLowering::typedLowering",
                    semantics::OriginalCommandNativeColdLowering::typedLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeColdLowering::uploadLowering",
                    semantics::OriginalCommandNativeColdLowering::uploadLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalGeneralOutputExecutionLowering::lowering",
                    semantics::OriginalGeneralOutputExecutionLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandNativeColdCommandLowering::lowering",
                    semantics::OriginalCommandNativeColdCommandLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandCallbackCustodyLowering::lowering",
                    semantics::OriginalCommandCallbackCustodyLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandIssuerPreparationLowering::lowering",
                    semantics::OriginalCommandIssuerPreparationLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandQueueSubmissionLowering::lowering",
                    semantics::OriginalCommandQueueSubmissionLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandScopedRecordingLowering::lowering",
                    semantics::OriginalCommandScopedRecordingLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandReadbackResultLowering::lowering",
                    semantics::OriginalCommandReadbackResultLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandMixedReadbackResultLowering::lowering",
                    semantics::OriginalCommandMixedReadbackResultLowering::lowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandOrderedCopyAttachmentLowering::lowering",
                    semantics::OriginalCommandOrderedCopyAttachmentLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandReadbackResultStorageLowering::lowering",
                    semantics::OriginalCommandReadbackResultStorageLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandReadbackResultStorageLowering::typedLowering",
                    semantics::OriginalCommandReadbackResultStorageLowering::typedLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandReadbackResultStorageLowering::bufferLowering",
                    semantics::OriginalCommandReadbackResultStorageLowering::bufferLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandReadbackMapLowering::lowering",
                    semantics::OriginalCommandReadbackMapLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandOrderedReadbackLowering::lowering",
                    semantics::OriginalCommandOrderedReadbackLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalTypedNativeColdLowering::admissionLowering",
                    semantics::OriginalTypedNativeColdLowering::admissionLowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalTypedNativeColdLowering::lowering",
                    semantics::OriginalTypedNativeColdLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalTypedUsageProjectionLowering::projectionLowering",
                    semantics::OriginalTypedUsageProjectionLowering::projectionLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalTypedUsageProjectionLowering::nativeLowering",
                    semantics::OriginalTypedUsageProjectionLowering::nativeLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalTypedStateBoundUsageLowering::lowering",
                    semantics::OriginalTypedStateBoundUsageLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalTypedDependencyProjectionLowering::lowering",
                    semantics::OriginalTypedDependencyProjectionLowering::lowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalTypedDependencyProjectionLowering::nativeLowering",
                    semantics::OriginalTypedDependencyProjectionLowering::nativeLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalTypedDependencyProjectionLowering::freezeLowering",
                    semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalTypedNativePayloadLowering::lowering",
                    semantics::OriginalTypedNativePayloadLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalTypedNativeColdCommandLowering::lowering",
                    semantics::OriginalTypedNativeColdCommandLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalTypedCallbackCustodyLowering::lowering",
                    semantics::OriginalTypedCallbackCustodyLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalTypedReadbackMapCustodyLowering::lowering",
                    semantics::OriginalTypedReadbackMapCustodyLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalTypedIssuerPreparationLowering::lowering",
                    semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalTypedNativeExecutionLowering::lowering",
                    semantics::OriginalTypedNativeExecutionLowering::lowering(
                        backend, version, version, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandUploadStagingLowering::lowering",
                    semantics::OriginalCommandUploadStagingLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandOrderedUploadLowering::lowering",
                    semantics::OriginalCommandOrderedUploadLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandReadbackJoinLowering::lowering",
                    semantics::OriginalCommandReadbackJoinLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandUploadJoinLowering::lowering",
                    semantics::OriginalCommandUploadJoinLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandReadbackMapDrainLowering::lowering",
                    semantics::OriginalCommandReadbackMapDrainLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandReadbackSnapshotPublicationLowering::lowering",
                    semantics::OriginalCommandReadbackSnapshotPublicationLowering::lowering(
                        backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandInitializedBufferStagingLowering::indexShaderReadLowering",
                    semantics::OriginalCommandInitializedBufferStagingLowering::
                        indexShaderReadLowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandInitializedBufferStagingLowering::lowering",
                    semantics::OriginalCommandInitializedBufferStagingLowering::lowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandCopyPassLowering::lowering",
                    semantics::OriginalCommandCopyPassLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalCommandCopyPassLowering::lowering",
                    semantics::OriginalCommandCopyPassLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandInitializedBufferUploadExecutionLowering::lowering",
                    semantics::OriginalCommandInitializedBufferUploadExecutionLowering::lowering(
                        backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCommandInitializedTextureUploadExecutionLowering::lowering",
                    semantics::OriginalCommandInitializedTextureUploadExecutionLowering::lowering(
                        backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalCompletedShaderTextureInputLowering::lowering",
                    semantics::OriginalCompletedShaderTextureInputLowering::lowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalGeneralOutputSnapshotLowering::completedAssociationLowering",
                    semantics::OriginalGeneralOutputSnapshotLowering::completedAssociationLowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalGeneralOutputSnapshotLowering::lowering",
                    semantics::OriginalGeneralOutputSnapshotLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "BindlessIndirectPacketStorageContract::closedConsumerLowering",
                    BindlessIndirectPacketStorageContract::closedConsumerLowering(
                        backend, version, flag),
                    denied,
                    snapshot);
                dispatch_test::verify("BindlessIndirectPacketStorageContract::coldLowering",
                    BindlessIndirectPacketStorageContract::coldLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("CanonicalZeroBaseIndirectConsumerContract::lowering",
                    CanonicalZeroBaseIndirectConsumerContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("CanonicalConsumerImageContentContract::lowering",
                    CanonicalConsumerImageContentContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "CanonicalZeroBaseIndirectConsumerGeometryContract::coldLowering",
                    CanonicalZeroBaseIndirectConsumerGeometryContract::coldLowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("CanonicalZeroBaseIndirectConsumerGeometryContract::lowering",
                    CanonicalZeroBaseIndirectConsumerGeometryContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("CanonicalExclusiveConsumerImagesContract::lowering",
                    CanonicalExclusiveConsumerImagesContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("CanonicalClosedConsumerTargetContract::lowering",
                    CanonicalClosedConsumerTargetContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "CanonicalZeroBaseIndirectConsumerColdBindingContract::lowering",
                    CanonicalZeroBaseIndirectConsumerColdBindingContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("CanonicalZeroBaseIndirectColdBindingContract::lowering",
                    CanonicalZeroBaseIndirectColdBindingContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "CanonicalZeroBaseIndirectProducerRecordingContract::lowering",
                    CanonicalZeroBaseIndirectProducerRecordingContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("CanonicalClosedConsumerRecordingContract::lowering",
                    CanonicalClosedConsumerRecordingContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("OriginalAcceptedGraphicsPipelineFactoryContract::lowering",
                    OriginalAcceptedGraphicsPipelineFactoryContract::lowering(backend, flag),
                    denied,
                    snapshot);
                dispatch_test::verify("OriginalAcceptedComputePipelineFactoryContract::lowering",
                    OriginalAcceptedComputePipelineFactoryContract::lowering(backend, flag),
                    denied,
                    snapshot);
                dispatch_test::verify("OriginalPipelineArtifactConsumptionContract::lowering",
                    OriginalPipelineArtifactConsumptionContract::lowering(backend, flag),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "OriginalDepthTableExpansionCreationContract::wrapperLowering",
                    OriginalDepthTableExpansionCreationContract::wrapperLowering(
                        backend, flag, flag),
                    denied,
                    snapshot);
                dispatch_test::verify("OriginalAcceptedTableExpansionPipelineContract::lowering",
                    OriginalAcceptedTableExpansionPipelineContract::lowering(backend, flag),
                    denied,
                    snapshot);
                dispatch_test::verify("OriginalGraphicsArtifactConsumptionContract::lowering",
                    OriginalGraphicsArtifactConsumptionContract::lowering(backend, flag),
                    denied,
                    snapshot);
                dispatch_test::verify("arp::rhi_webgpu::WebGpuOriginalQueryBinding::lowering",
                    arp::rhi_webgpu::WebGpuOriginalQueryBinding::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalQueryRecordReservation::lowering",
                    arp::rhi_webgpu::WebGpuOriginalQueryRecordReservation::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("arp::rhi_webgpu::WebGpuOriginalQuerySlots::lowering",
                    arp::rhi_webgpu::WebGpuOriginalQuerySlots::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalTextureUploadTransaction::lowering",
                    arp::rhi_webgpu::WebGpuOriginalTextureUploadTransaction::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalTextureReadbackSource::lowering",
                    arp::rhi_webgpu::WebGpuOriginalTextureReadbackSource::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalTextureReadbackDeviceStagingBudget::lowering",
                    arp::rhi_webgpu::WebGpuOriginalTextureReadbackDeviceStagingBudget::lowering(
                        backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalTextureReadbackDestination::lowering",
                    arp::rhi_webgpu::WebGpuOriginalTextureReadbackDestination::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalTextureReadbackPairProjection::lowering",
                    arp::rhi_webgpu::WebGpuOriginalTextureReadbackPairProjection::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalTextureReadbackMapping::lowering",
                    arp::rhi_webgpu::WebGpuOriginalTextureReadbackMapping::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalTextureReadbackStatistics::lowering",
                    arp::rhi_webgpu::WebGpuOriginalTextureReadbackStatistics::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalTextureReadbackPublication::lowering",
                    arp::rhi_webgpu::WebGpuOriginalTextureReadbackPublication::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuTextureReadbackColdBudgetLowering::lowering",
                    arp::rhi_webgpu::WebGpuTextureReadbackColdBudgetLowering::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuOriginalTextureUploadSource::lowering",
                    arp::rhi_webgpu::WebGpuOriginalTextureUploadSource::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "arp::rhi_webgpu::WebGpuCanonicalTextureContentUploadSource::lowering",
                    arp::rhi_webgpu::WebGpuCanonicalTextureContentUploadSource::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalBindlessEntryAccessContract::lowering",
                    semantics::OriginalBindlessEntryAccessContract::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "semantics::OriginalBindlessGraphicsExecutionLowering::lowering",
                    semantics::OriginalBindlessGraphicsExecutionLowering::lowering(
                        backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalBindlessExecutionLowering::lowering",
                    semantics::OriginalBindlessExecutionLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::OriginalBindlessPreparationLowering::lowering",
                    semantics::OriginalBindlessPreparationLowering::lowering(backend, version),
                    denied,
                    snapshot);
                dispatch_test::verify("semantics::BindlessPipelineInterfaceContract::lowering",
                    semantics::BindlessPipelineInterfaceContract::lowering(backend),
                    denied,
                    snapshot);
                dispatch_test::verify(
                    "OriginalGeneralFiniteInitializedBufferContentContract::lowering",
                    OriginalGeneralFiniteInitializedBufferContentContract::lowering(backend),
                    denied,
                    snapshot);
            }
        }
    }
}

template <class F> void coordinateFailure(Backend backend, F call)
{
    bool caught = false;
    try {
        call();
    } catch (const arp::core::DiagnosticError& error) {
        caught = true;
        assert(std::strcmp(error.what(),
                   backend == Backend::WebGL ? "RHI-WEBGL1-COORDINATE-LOWERING-UNAVAILABLE"
                                             : "RHI-COORDINATE-BACKEND-UNKNOWN")
            == 0);
    }
    assert(caught);
}

void coordinates()
{
    using namespace arp::rhi::semantics;
    static_assert(renderTargetOriginForBackend(Backend::Vulkan) == RenderTargetOrigin::BottomLeft);
    static_assert(projectionRequiresYFlip(Backend::Metal));
    static_assert(uiCoordinatesRequireYFlip(Backend::WebGPU));
    static_assert(
        cpuRasterTextureUvYTransformForBackend(Backend::Null) == TextureUvYTransform::Identity);
    for (auto backend : {Backend::WebGL, static_cast<Backend>(254u), static_cast<Backend>(255u)}) {
        coordinateFailure(backend, [&] {
            renderTargetOriginForBackend(backend);
        });
        coordinateFailure(backend, [&] {
            projectionRequiresYFlip(backend);
        });
        coordinateFailure(backend, [&] {
            uiCoordinatesRequireYFlip(backend);
        });
        coordinateFailure(backend, [&] {
            textureUvYTransformForBackend(backend);
        });
        coordinateFailure(backend, [&] {
            cpuRasterTextureUvYTransformForBackend(backend);
        });
        std::array<float, 16> matrix{};
        for (unsigned i = 0; i < 16u; ++i)
            matrix[i] = static_cast<float>(i + 1u);
        const auto original = matrix;
        for (auto apply : {applyProjectionYConvention,
                 applyViewProjectionYConvention,
                 applyRenderTargetSamplingYConvention}) {
            coordinateFailure(backend, [&] {
                apply(matrix.data(), backend);
            });
            assert(matrix == original);
            coordinateFailure(backend, [&] {
                apply(nullptr, backend);
            });
        }
        for (auto apply : {applyTextureUvYConvention, applyCpuRasterTextureUvYConvention}) {
            float origin = .3f, size = .1f;
            coordinateFailure(backend, [&] {
                apply(&origin, &size, backend);
            });
            assert(origin == .3f && size == .1f);
            coordinateFailure(backend, [&] {
                apply(nullptr, &size, backend);
            });
            coordinateFailure(backend, [&] {
                apply(&origin, nullptr, backend);
            });
            assert(origin == .3f && size == .1f);
        }
    }
}

void composedGates()
{
    using Producer = CanonicalZeroBaseIndirectProducerRecordingContract;
    using Consumer = CanonicalClosedConsumerRecordingContract;
    using Preflight = OriginalDepthTableExpansionPipelinePreflightContract;
    using Emission = OriginalDepthTableExpansionNativeEmissionContract;
    // Valid budget envelopes ensure the backend refusal is reachable rather than
    // hidden behind a malformed fixture. Authored facts still grant no execution.
    const Producer::Budget producerBudget{Producer::version,
        true,
        Producer::commandUnits,
        Producer::readbackBytes,
        Producer::referenceSlots};
    const Consumer::Budget consumerBudget{Consumer::version,
        true,
        Consumer::commandUnits,
        Consumer::readbackBytes,
        Consumer::referenceSlots};
    assert(!producerBudget.validate() && !consumerBudget.validate());
    const auto allocationCount = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (auto backend : {Backend::WebGL, static_cast<Backend>(254u), static_cast<Backend>(255u)}) {
        dispatch_test::verify("producer private recording",
            Producer::privateRecordingLowering(backend, producerBudget),
            true,
            false);
        assert(semantics::PipelineCreationLowering::artifactRole(backend)
            == semantics::PipelineCreationLowering::ArtifactRole::DiagnosticFailure);
        for (auto scope : {OriginalBindlessCompletedImageScope::ComputeColor8,
                 OriginalBindlessCompletedImageScope::SampledGraphics1,
                 static_cast<OriginalBindlessCompletedImageScope>(255u)}) {
            for (auto version : {0u, 1u, 2u, UINT32_MAX})
                dispatch_test::verify("completed image scope",
                    semantics::OriginalBindlessCompletedImageLowering::lowering(
                        backend, scope, version),
                    true,
                    false);
        }
        for (auto format :
            {TextureFormat::R32Float, TextureFormat::Rgba16Float, TextureFormat::Unknown}) {
            for (auto version : {0u, 1u, 2u, UINT32_MAX}) {
                const auto declaration =
                    semantics::BindlessStorageImageDeclarationLowering::lowering(
                        backend, format, version);
                dispatch_test::verify("storage image declaration", declaration, true, false);
                assert(!declaration.target);
            }
        }
        const ResourceBarrierDesc barrier{{7u},
            {},
            ResourceState::CopyDst,
            ResourceState::CopySrc,
            PipelineStage::Transfer,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite,
            ResourceAccess::TransferRead};
        for (auto version : {0u, 1u, 2u, UINT32_MAX}) {
            for (bool optIn : {false, true}) {
                const auto transfer = semantics::TransferDependencyLowering::transition(
                    backend, version, optIn, barrier, barrier.before, false);
                assert(transfer.outcome == CapabilityMode::DiagnosticFailure);
                assert(transfer.operation
                    == semantics::TransferDependencyLowering::Operation::DiagnosticFailure);
                assert(transfer.diagnostic && *transfer.diagnostic);
            }
        }
        dispatch_test::verify("consumer private recording",
            Consumer::privateRecordingLowering(backend, consumerBudget),
            true,
            false);
        for (std::uint32_t version : {1u, 2u, 3u}) {
            auto outputBarrier = barrier;
            const bool graphics = version == 3u;
            outputBarrier.before =
                graphics ? ResourceState::ColorAttachment : ResourceState::ShaderWrite;
            outputBarrier.sourceStages =
                graphics ? PipelineStage::ColorAttachmentOutput : PipelineStage::ComputeShader;
            outputBarrier.sourceAccess = graphics
                ? ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite
                : ResourceAccess::ShaderWrite;
            assert(!OriginalGeneralOutputReadbackDependencyContract::validate(
                outputBarrier, outputBarrier.before, {}, version));
            for (bool optIn : {false, true}) {
                const auto output =
                    semantics::OriginalGeneralOutputReadbackDependencyLowering::transition(
                        backend, version, optIn, outputBarrier, outputBarrier.before, {});
                assert(output.outcome == CapabilityMode::DiagnosticFailure);
                assert(output.operation
                    == semantics::TransferDependencyLowering::Operation::DiagnosticFailure);
                assert(output.diagnostic && *output.diagnostic);
            }
        }
        for (bool fact : {false, true}) {
            const semantics::OriginalArtifactCreationLowering::Facts facts{
                fact, fact, fact, fact, fact, fact, fact};
            dispatch_test::verify("graphics creation",
                semantics::OriginalArtifactGraphicsCreationLowering::decide(backend, facts),
                true,
                false);
            const semantics::OriginalArtifactComputeCreationLowering::Facts computeFacts{
                fact, fact, fact, fact, fact, fact, fact};
            dispatch_test::verify("compute creation",
                semantics::OriginalArtifactComputeCreationLowering::decide(backend, computeFacts),
                true,
                false);
            const auto preflight = Preflight::lowering(backend,
                Preflight::Requirements{},
                facts,
                arp::rhi_webgpu::WebGpuNativeDeviceLimits{});
            dispatch_test::verify("table preflight", preflight, true, false);
            Preflight::Decision forgedPreflight{
                CapabilityMode::Native, false, fact, fact, fact, nullptr};
            dispatch_test::verify(
                "table emission", Emission::lowering(backend, forgedPreflight), true, false);
            Emission::Decision forgedEmission{
                CapabilityMode::Native, false, fact, fact, fact, nullptr};
            dispatch_test::verify("table creation",
                OriginalDepthTableExpansionCreationContract::lowering(backend, forgedEmission),
                true,
                false);
            const auto scene2d = arp::scene2d::lowerExecution2D(backend, {fact, fact, fact, fact});
            assert(!scene2d.executable()
                && scene2d.mode == arp::scene2d::Execution2D::DiagnosticFailure);
            assert(std::strcmp(scene2d.diagnostic, "ARP-2D-BACKEND-UNSUPPORTED") == 0);
            for (bool other : {false, true}) {
                dispatch_test::verify("wrapper independent facts",
                    OriginalDepthTableExpansionCreationContract::wrapperLowering(
                        backend, fact, other),
                    true,
                    false);
                for (std::uint32_t version = 0; version <= 22u; ++version)
                    dispatch_test::verify("independent completion versions",
                        semantics::OriginalTypedNativeExecutionLowering::lowering(
                            backend, version, fact ? 1u : 0u, other ? 1u : 0u),
                        true,
                        false);
            }
        }
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocationCount);
    using QueryFacts = semantics::WebGpuTimestampCommandPointFacts;
    arp::test::WebGpuOriginalQuerySourceGateFixture::verify({&QueryFacts::deviceFeatureEnabled,
        &QueryFacts::encoderWrite,
        &QueryFacts::renderPassWrite,
        &QueryFacts::computePassWrite,
        &QueryFacts::resolve,
        &QueryFacts::bufferCopy,
        &QueryFacts::mapRead,
        &QueryFacts::mappedRead,
        &QueryFacts::unmap});
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "snapshot") == 0) {
        decisions(true);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "coordinates") == 0) {
        coordinates();
        return 0;
    }
    decisions(false);
    coordinates();
    composedGates();
    std::puts("WebGL and unknown dispatch denied; coordinates unchanged on failure");
}
