#include "core/math/half_float.h"
#include "renderer/volumetric_cloud/cloud_gpu_contract.h"
#include "renderer/volumetric_cloud/cloud_reconstruction.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_graphics_pass.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_original_compute_pipeline.h"
#include "rhi/backends/vulkan/vulkan_original_graphics_binding.h"
#include "rhi/backends/vulkan/vulkan_original_graphics_pipeline.h"
#include "rhi/backends/vulkan/vulkan_render_pass_storage.h"
#include "rhi/frontend/command_recorded_state.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/descriptor_general_write_validation.h"
#include "rhi/frontend/original_prepared_command_device.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_core_capability_query_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
#include "renderer/render_graph/original_frame_graph_execution.h"
#include "renderer/volumetric_cloud/cloud_prepared_descriptors.h"
#include "renderer/volumetric_cloud/cloud_prepared_feature.h"
#include "renderer/volumetric_cloud/cloud_prepared_history.h"
#include "renderer/volumetric_cloud/cloud_prepared_pipelines.h"
#include "renderer/volumetric_cloud/cloud_reference.h"
#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"
#include "rhi/semantics/render_target_coordinates.h"
#include "tests/fixtures/cloud/cloud_fixture.h"
#include "tests/fixtures/cloud/cloud_quality_oracle.h"
#include "tests/fixtures/cloud/cloud_sequence_oracle.h"
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <thread>
#include <utility>

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_vulkan;
using Contract = OriginalSynchronousGraphicsCreationContract;
using Receipt = VulkanOriginalArtifactGraphicsCreation;
using Source = OriginalCommandGraphicsPipelineSource;
constexpr std::uint64_t bytes = 2u * 1024u * 1024u;

// Every successful creation delegates to the actual driver. Counters prove
// complete destruction on every native failure prefix and CPU allocation edge.
class NativeProbe final {
public:
    static inline VkSemaphore submissionGate = VK_NULL_HANDLE;
    static inline unsigned gatedSubmissions = 0u;

    static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(
        VkQueue queue, std::uint32_t count, const VkSubmitInfo* submits, VkFence completion)
    {
        if (submissionGate) {
            const auto gate = std::exchange(submissionGate, VK_NULL_HANDLE);
            const std::uint64_t value = 1u;
            const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkTimelineSemaphoreSubmitInfo timeline{
                VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
            timeline.waitSemaphoreValueCount = 1u;
            timeline.pWaitSemaphoreValues = &value;
            VkSubmitInfo wait{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            wait.pNext = &timeline;
            wait.waitSemaphoreCount = 1u;
            wait.pWaitSemaphores = &gate;
            wait.pWaitDstStageMask = &stage;
            const auto result = vkQueueSubmit(queue, 1u, &wait, VK_NULL_HANDLE);
            if (result != VK_SUCCESS)
                return result;
            ++gatedSubmissions;
        }
        return vkQueueSubmit(queue, count, submits, completion);
    }

    static inline const char* missing = nullptr;
    static inline unsigned ordinal = 0u, failAt = 0u;
    static inline VkResult failure = VK_ERROR_OUT_OF_HOST_MEMORY;
    static inline bool partialPipelineFailure = false;
    static inline std::array<unsigned, 5u> live{};
    static inline unsigned viewAttempts = 0u, viewFailAt = 0u, liveViews = 0u,
                           descriptorUpdates = 0u;

    static VKAPI_ATTR VkResult VKAPI_CALL CreateImageView(VkDevice device,
        const VkImageViewCreateInfo* info,
        const VkAllocationCallbacks* allocator,
        VkImageView* output)
    {
        if (++viewAttempts == viewFailAt)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        const auto result = vkCreateImageView(device, info, allocator, output);
        if (*output)
            ++liveViews;
        return result;
    }

    static VKAPI_ATTR void VKAPI_CALL DestroyImageView(
        VkDevice device, VkImageView view, const VkAllocationCallbacks* allocator)
    {
        assert(view && liveViews);
        --liveViews;
        vkDestroyImageView(device, view, allocator);
    }

    static VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSets(VkDevice device,
        std::uint32_t count,
        const VkWriteDescriptorSet* writes,
        std::uint32_t copyCount,
        const VkCopyDescriptorSet* copies)
    {
        ++descriptorUpdates;
        vkUpdateDescriptorSets(device, count, writes, copyCount, copies);
    }

    static bool reject() { return ++ordinal == failAt; }

#define ARP_NATIVE_CREATE(name, Handle, Info, index)                                               \
    static VKAPI_ATTR VkResult VKAPI_CALL name(                                                    \
        VkDevice device, const Info* info, const VkAllocationCallbacks* allocator, Handle* output) \
    {                                                                                              \
        if (reject())                                                                              \
            return failure;                                                                        \
        const auto result = vk##name(device, info, allocator, output);                             \
        if (*output)                                                                               \
            ++live[index];                                                                         \
        return result;                                                                             \
    }
    ARP_NATIVE_CREATE(CreateShaderModule, VkShaderModule, VkShaderModuleCreateInfo, 0u)
    ARP_NATIVE_CREATE(
        CreateDescriptorSetLayout, VkDescriptorSetLayout, VkDescriptorSetLayoutCreateInfo, 1u)
    ARP_NATIVE_CREATE(CreatePipelineLayout, VkPipelineLayout, VkPipelineLayoutCreateInfo, 2u)
    ARP_NATIVE_CREATE(CreateRenderPass, VkRenderPass, VkRenderPassCreateInfo, 3u)
#undef ARP_NATIVE_CREATE
#define ARP_NATIVE_DESTROY(name, Handle, index)                                                    \
    static VKAPI_ATTR void VKAPI_CALL name(                                                        \
        VkDevice device, Handle handle, const VkAllocationCallbacks* allocator)                    \
    {                                                                                              \
        assert(handle && live[index]);                                                             \
        --live[index];                                                                             \
        vk##name(device, handle, allocator);                                                       \
    }
    ARP_NATIVE_DESTROY(DestroyShaderModule, VkShaderModule, 0u)
    ARP_NATIVE_DESTROY(DestroyDescriptorSetLayout, VkDescriptorSetLayout, 1u)
    ARP_NATIVE_DESTROY(DestroyPipelineLayout, VkPipelineLayout, 2u)
    ARP_NATIVE_DESTROY(DestroyRenderPass, VkRenderPass, 3u)
    ARP_NATIVE_DESTROY(DestroyPipeline, VkPipeline, 4u)
#undef ARP_NATIVE_DESTROY

    static VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device,
        VkPipelineCache cache,
        std::uint32_t count,
        const VkGraphicsPipelineCreateInfo* info,
        const VkAllocationCallbacks* allocator,
        VkPipeline* output)
    {
        if (reject())
            return failure;
        const auto result =
            vkCreateGraphicsPipelines(device, cache, count, info, allocator, output);
        for (std::uint32_t i = 0u; i < count; ++i)
            if (output[i])
                ++live[4];
        return result == VK_SUCCESS && partialPipelineFailure ? VK_ERROR_OUT_OF_HOST_MEMORY
                                                              : result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL CreateComputePipelines(VkDevice device,
        VkPipelineCache cache,
        std::uint32_t count,
        const VkComputePipelineCreateInfo* info,
        const VkAllocationCallbacks* allocator,
        VkPipeline* output)
    {
        if (reject())
            return failure;
        const auto result = vkCreateComputePipelines(device, cache, count, info, allocator, output);
        for (std::uint32_t i = 0u; i < count; ++i)
            if (output[i])
                ++live[4];
        return result == VK_SUCCESS && partialPipelineFailure ? VK_ERROR_OUT_OF_HOST_MEMORY
                                                              : result;
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (missing && !std::strcmp(missing, name))
            return nullptr;
#define ARP_NATIVE_RESOLVE(method)                                                                 \
    if (!std::strcmp(name, "vk" #method))                                                          \
    return reinterpret_cast<PFN_vkVoidFunction>(&method)
        ARP_NATIVE_RESOLVE(CreateShaderModule);
        ARP_NATIVE_RESOLVE(CreateDescriptorSetLayout);
        ARP_NATIVE_RESOLVE(CreatePipelineLayout);
        ARP_NATIVE_RESOLVE(CreateRenderPass);
        ARP_NATIVE_RESOLVE(CreateGraphicsPipelines);
        ARP_NATIVE_RESOLVE(CreateComputePipelines);
        ARP_NATIVE_RESOLVE(DestroyShaderModule);
        ARP_NATIVE_RESOLVE(DestroyDescriptorSetLayout);
        ARP_NATIVE_RESOLVE(DestroyPipelineLayout);
        ARP_NATIVE_RESOLVE(DestroyRenderPass);
        ARP_NATIVE_RESOLVE(DestroyPipeline);
        ARP_NATIVE_RESOLVE(CreateImageView);
        ARP_NATIVE_RESOLVE(DestroyImageView);
        ARP_NATIVE_RESOLVE(UpdateDescriptorSets);
        ARP_NATIVE_RESOLVE(QueueSubmit);
#undef ARP_NATIVE_RESOLVE
        return vkGetDeviceProcAddr(device, name);
    }
};

void verifyContract()
{
    {
        OriginalResourceRegistrationBudget registration;
        registration.contractVersion = 10u;
        registration.prototypeOptIn = true;
        registration.maxPipelineLayouts = registration.maxGraphicsPipelines = 1u;
        registration.maxRetainedOwners = 11u;
        registration.maxCpuMetadataBytes = bytes;
        using Join = OriginalTypedNativeLossJoinContract;
        assert(!Join::validate({3u, true}, registration));
        assert(Join::validate({3u, false}, registration));
        assert(Join::validate({1u, true}, registration));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto d = Join::lowering(backend, 3u);
            assert(
                d.registrationAvailable == (backend == Backend::Vulkan || backend == Backend::Metal)
                && d.acceptedPipelineJoinAvailable
                    == (backend == Backend::Vulkan || backend == Backend::Metal)
                && d.validatorOnly == (backend == Backend::Null) && !d.scopedGroupJoinAvailable
                && !d.recordingAvailable && !d.executionAvailable);
            assert(OriginalResourceRegistrationContract::lowering(backend, 10u).diagnostic);
        }
        auto invalid = registration;
        invalid.maxDescriptorTables = 1u;
        assert(Join::validate({3u, true}, invalid));
        auto null = createNullDevice();
        assert(null->prepareOriginalResourceRegistration(
            registration, core::makeShared<core::CpuMemoryQuota>(bytes)));
    }
    {
        const OriginalRenderPassStorageContract::Budget storage{
            1u, true, 2u, 1u, 3u, {1u, true, 2u, 0u, 0u, 0u, 0u, bytes}};
        OriginalRenderPassPreparationRequest request{5u,
            true,
            storage,
            7u,
            2u,
            OriginalRenderPassContentBudget{2u, true, 3u, 1u, 32u, 128u, 128u},
            OriginalRenderPassRecordingBudget{2u, true, 128u, 512u}};
        assert(!OriginalRenderPassPreparationContract::validate(request));
        assert(OriginalPreparedCommandDeviceExtension::validateRequest(request));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto d = OriginalRenderPassPreparationContract::lowering(backend, request);
            assert(d.validatorOnly == (backend == Backend::Null)
                && d.privatePreparationAvailable == (backend == Backend::Vulkan)
                && d.recordingAvailable == (backend == Backend::Vulkan)
                && bool(d.diagnostic) == (backend != Backend::Vulkan));
            const auto consumption =
                semantics::OriginalCommandDescriptorConsumptionLowering::lowering(backend, 4u);
            const auto binding = OriginalRenderDescriptorUsageContract::lowering(backend, 4u);
            assert(!consumption.executionAvailable && !binding.executionAvailable);
            assert(consumption.projectionAvailable
                == (backend == Backend::Vulkan || backend == Backend::Metal
                    || backend == Backend::WebGPU));
            assert(binding.projectionAvailable == consumption.projectionAvailable);
        }
        for (unsigned field = 0u; field < 4u; ++field) {
            auto bad = request;
            if (!field)
                bad.content->contractVersion = 1u;
            if (field == 1u)
                bad.recording->contractVersion = 1u;
            if (field == 2u)
                bad.contractVersion = 4u;
            if (field == 3u)
                bad.prototypeOptIn = false;
            assert(OriginalRenderPassPreparationContract::validate(bad));
        }
        for (unsigned version = 1u; version < 4u; ++version)
            assert(!semantics::OriginalCommandDescriptorConsumptionLowering::lowering(
                Backend::Vulkan, version)
                    .projectionAvailable);
        std::puts(
            "ORDINARY_CLOUD_DRAW_CONTRACT_OK Prototype5 opt-in exact-pair content2 recording2 four-backends plus-WebGL public-execution-closed");
    }
    {
        const OriginalRenderPassStorageContract::Budget storage{
            1u, true, 2u, 1u, 3u, {1u, true, 2u, 0u, 0u, 0u, 0u, bytes}};
        OriginalRenderPassPreparationRequest request{6u,
            true,
            storage,
            7u,
            2u,
            OriginalRenderPassContentBudget{3u, true, 3u, 1u, 64u, 512u, 512u},
            OriginalRenderPassRecordingBudget{3u, true, 512u, 1024u}};
        request.recording->opaqueDepthGraphicsVersion = 1u;
        request.recording->opaqueDepthGraphicsOptIn = true;
        assert(!OriginalRenderPassPreparationContract::validate(request));
        assert(!OriginalPreparedCommandDeviceExtension::validateRequest(request));
        auto nullDevice = createNullDevice();
        const char* extensionDiagnostic = nullptr;
        assert(!OriginalPreparedCommandDeviceExtension::query(
            *nullDevice, request, extensionDiagnostic));
        assert(extensionDiagnostic
            && !std::strcmp(extensionDiagnostic, "RHI-RENDER-PASS-PREPARATION-NULL-VALIDATOR"));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto d = OriginalRenderPassPreparationContract::lowering(backend, request);
            assert(d.validatorOnly == (backend == Backend::Null)
                && d.recordingAvailable == (backend == Backend::Vulkan)
                && d.privatePreparationAvailable == (backend == Backend::Vulkan)
                && bool(d.diagnostic) == (backend != Backend::Vulkan));
        }
        for (unsigned field = 0u; field < 4u; ++field) {
            auto bad = request;
            if (!field)
                bad.content->contractVersion = 2u;
            if (field == 1u)
                bad.recording->contractVersion = 2u;
            if (field == 2u)
                bad.contractVersion = 5u;
            if (field == 3u)
                bad.prototypeOptIn = false;
            assert(OriginalRenderPassPreparationContract::validate(bad));
        }
        assert(OriginalCommandDescriptorBindingContract::validateSetProfile(6u, 7u));
        assert(!OriginalCommandDescriptorBindingContract::validateSetProfile(10u, 7u));
        assert(!OriginalCommandDescriptorBindingContract::validateSetProfile(10u, 5u));
        std::puts(
            "ORDINARY_MIXED_CONTRACT_OK Preparation6 Content3 Recording3 Inputs10 four-backends public-gates-closed");
    }
    using Write = OriginalGeneralDescriptorWriteContract;
    OriginalGeneralDescriptorWriteRequest synchronous;
    synchronous.contractVersion = 3u;
    synchronous.prototypeOptIn = true;
    synchronous.maxBindings = 4u;
    synchronous.maxRetainedOwnerSlots = Write::ownerSlots(4u, synchronous.contractVersion);
    synchronous.maxCpuBytes.fill(bytes);
    synchronous.maxRetainedAllocationBytes = bytes;
    assert(!Write::validate(synchronous));
    synchronous.maxCallbacks = 3u;
    assert(Write::validate(synchronous));
    synchronous.maxCallbacks = 0u;
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto decision = Write::synchronousGraphicsLowering(backend);
        assert(decision.preparationAvailable
            == (backend == Backend::Vulkan || backend == Backend::Metal));
        assert(!decision.executionAvailable);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(
            bool(decision.diagnostic) == (backend != Backend::Vulkan && backend != Backend::Metal));
    }

    using Inspection = OriginalGraphicsBindingInspectionContract;
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto decision = Inspection::lowering(backend, {1u, true});
        assert(!decision.executionAvailable);
        assert(decision.inspectionAvailable == (backend == Backend::Vulkan));
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(bool(decision.diagnostic) != decision.inspectionAvailable);
        assert(!Inspection::lowering(backend, {0u, true}).inspectionAvailable);
        assert(!Inspection::lowering(backend, {1u, false}).inspectionAvailable);
    }
    std::uint64_t count = UINT64_MAX;
    assert(!Inspection::validateBufferExtent(368u, 368u, false, 0u, 0u, 0u, count) && !count);
    assert(Inspection::validateBufferExtent(367u, 368u, false, 0u, 0u, 0u, count) && !count);
    assert(!Inspection::validateBufferExtent(64u, 0u, true, 0u, 32u, 32u, count) && count == 2u);
    for (const auto n : {0u, 1u, 31u, 33u, 63u})
        assert(Inspection::validateBufferExtent(n, 0u, true, 0u, 32u, 32u, count) && !count);
    assert(!Inspection::validateBufferExtent(80u, 16u, true, 16u, 32u, 24u, count) && count == 2u);
    assert(Inspection::validateBufferExtent(16u, 16u, true, 16u, 32u, 24u, count) && !count);
    assert(Inspection::validateBufferExtent(UINT64_MAX, 16u, true, UINT64_MAX, 32u, 24u, count)
        && !count);
    assert(Inspection::validateBufferExtent(64u, 0u, true, 0u, 32u, 33u, count) && !count);
    assert(Inspection::validateBufferExtent(64u, 0u, true, 0u, 0u, 1u, count) && !count);
    assert(Inspection::validateBufferExtent(64u, 0u, false, 0u, 32u, 0u, count) && !count);
    assert(Inspection::validateBufferExtent(64u, 0u, false, 0u, 0u, 0u, count) && !count);
    TextureDesc texture;
    texture.extent = {4u, 4u, 1u};
    texture.format = TextureFormat::Rgba16Float;
    TextureViewDesc view;
    view.format = texture.format;
    assert(!Inspection::validateTexture(texture, view, false));
    texture.sampleCount = 4u;
    assert(Inspection::validateTexture(texture, view, false));
    texture.sampleCount = 1u;
    texture.format = TextureFormat::Rgba8Unorm;
    assert(Inspection::validateTexture(texture, view, false));
    view.format = texture.format;
    assert(!Inspection::validateTexture(texture, view, false));
    assert(Inspection::validateTexture(texture, view, true));
    texture.format = view.format = TextureFormat::D32Float;
    view.aspect = TextureAspect::Depth;
    assert(!Inspection::validateTexture(texture, view, true));
    assert(Inspection::validateTexture(texture, view, false));
    view.arrayLayerCount = 2u;
    assert(Inspection::validateTexture(texture, view, true));
    DescriptorBindingDesc scalar;
    scalar.type = DescriptorType::UniformBuffer;
    scalar.stages = ShaderStage::Fragment;
    OriginalCommandDescriptorSetElement element;
    element.write.type = scalar.type;
    element.write.resource = {1u};
    element.write.resourceKind = DescriptorResourceKind::Buffer;
    assert(!Inspection::validateShape(scalar, element));
    for (unsigned field = 0u; field < 6u; ++field) {
        auto invalid = scalar;
        if (field == 0u)
            invalid.stages = ShaderStage::Compute;
        if (field == 1u)
            invalid.arrayCount = 2u;
        if (field == 2u)
            invalid.tableBacked = true;
        if (field == 3u)
            invalid.dynamicOffset = true;
        if (field == 4u)
            invalid.type = DescriptorType::StorageBuffer;
        if (field == 5u)
            invalid.type = DescriptorType::StorageTexture;
        assert(Inspection::validateShape(invalid, element));
    }
    Contract::Budget budget{1u, true, bytes, 256u, bytes, bytes};
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = Contract::lowering(backend, budget, {true, true});
        assert(!decision.executionAvailable);
        assert(
            decision.creationEligible == (backend == Backend::Vulkan || backend == Backend::Metal));
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(bool(decision.diagnostic) == !decision.creationEligible);
        assert(!OriginalAcceptedGraphicsPipelineFactoryContract::lowering(backend, true)
                .executionAvailable);
        const auto accepted =
            OriginalAcceptedGraphicsPipelineFactoryContract::lowering(backend, true, 2u);
        assert(accepted.factoryEligible == (backend == Backend::Vulkan || backend == Backend::Metal)
            && !accepted.executionAvailable);
        const auto source =
            semantics::OriginalCommandPipelineSourceLowering::lowering(backend, 2u, true);
        assert(source.custodyAvailable == (backend == Backend::Vulkan || backend == Backend::Metal)
            && !source.positionalAdapterAvailable && !source.executionAvailable);
    }
    assert(!Contract::lowering(Backend::Vulkan, budget, {false, true}).creationEligible);
    assert(!Contract::lowering(Backend::Vulkan, budget, {true, false}).creationEligible);
    for (unsigned field = 0u; field < 6u; ++field) {
        auto invalid = budget;
        if (field == 0u)
            invalid.contractVersion = 0u;
        if (field == 1u)
            invalid.prototypeOptIn = false;
        if (field == 2u)
            invalid.maxTargetBytes = 0u;
        if (field == 3u)
            invalid.maxLabelBytes = 0u;
        if (field == 4u)
            invalid.maxCpuOwnerBytes = 0u;
        if (field == 5u)
            invalid.maxCpuNativeBytes = 0u;
        assert(Contract::validateBudget(invalid));
        assert(!Contract::lowering(Backend::Vulkan, invalid, {true, true}).creationEligible);
    }
    assert(!semantics::OriginalCommandPipelineSourceLowering::lowering(Backend::Vulkan)
            .custodyAvailable);
    assert(semantics::OriginalCommandPipelineSourceLowering::lowering(Backend::Vulkan, 2u, false)
            .custodyAvailable);
    assert(!OriginalAcceptedGraphicsPipelineFactoryContract::lowering(Backend::Vulkan, true)
            .factoryEligible);
    assert(!OriginalAcceptedComputePipelineFactoryContract::lowering(Backend::Vulkan, true)
            .factoryEligible);
    assert(OriginalAcceptedGraphicsPipelineFactoryContract::validateBudget({3u, true, bytes}));
    using Lowering = semantics::PipelineCreationLowering;
    assert(!Lowering::synchronousVulkanGraphicsShaderCapability(1u, false));
    assert(!Lowering::synchronousVulkanGraphicsShaderCapability(4427u, true));
    assert(Lowering::synchronousVulkanGraphicsShaderCapability(4427u, false));
    for (const auto capability : {0u, 2u, 9u, 10u, 11u, 22u, 28u, UINT32_MAX})
        assert(Lowering::synchronousVulkanGraphicsShaderCapability(capability, true));
    for (unsigned unavailable = 0u; unavailable < 6u; ++unavailable) {
        tests::VulkanCoreCapabilityQueryFixture query;
        query.propertiesQueryAvailable = unavailable != 1u;
        query.featuresQueryAvailable = unavailable != 2u;
        query.shaderDrawParametersSupported = unavailable != 3u;
        auto desc = query.descriptor();
        desc.shaderDrawParametersEnablementReported = unavailable != 4u;
        desc.shaderDrawParametersEnabled = unavailable != 5u;
        const auto runtime = createVulkanRhiRuntime(desc);
        assert(runtime && runtime->shaderDrawParametersEnabled() == (unavailable == 0u));
    }
    auto validator = createNullDevice();
    {
        using Depth = semantics::OpaqueDepthGraphicsContract;
        assert(!Depth::validateIntent(0u, false));
        assert(!Depth::validateIntent(1u, true));
        assert(Depth::validateIntent(0u, true));
        assert(Depth::validateIntent(1u, false));
        assert(Depth::validateIntent(2u, true));
        RenderPipelineDesc pipeline;
        RenderPassDesc pass;
        assert(!Depth::validateAttachments(pipeline, pass, false));
        pipeline.depthFormat = TextureFormat::D32Float;
        pipeline.depthStencil.depthTestEnabled = pipeline.depthStencil.depthWriteEnabled = true;
        pipeline.depthStencil.depthCompare = CompareOp::Less;
        pass.hasDepthStencilAttachment = true;
        auto& attachment = pass.depthStencilAttachment;
        attachment.format = TextureFormat::D32Float;
        attachment.depthLoadOp = LoadOp::Clear;
        attachment.depthStoreOp = StoreOp::Store;
        attachment.stencilLoadOp = LoadOp::DontCare;
        attachment.stencilStoreOp = StoreOp::DontCare;
        assert(Depth::validateAttachments(pipeline, pass, false));
        assert(!Depth::validateAttachments(pipeline, pass, true));
        for (unsigned mutation = 0u; mutation < 9u; ++mutation) {
            auto badPipeline = pipeline;
            auto badPass = pass;
            if (mutation == 0u)
                badPipeline.depthFormat = TextureFormat::Unknown;
            if (mutation == 1u)
                badPass.hasDepthStencilAttachment = false;
            if (mutation == 2u)
                badPass.depthStencilAttachment.depthReadOnly = true;
            if (mutation == 3u)
                badPass.depthStencilAttachment.depthLoadOp = LoadOp::Load;
            if (mutation == 4u)
                badPass.depthStencilAttachment.depthStoreOp = StoreOp::DontCare;
            if (mutation == 5u)
                badPipeline.depthStencil.depthWriteEnabled = false;
            if (mutation == 6u)
                badPipeline.depthStencil.depthCompare = CompareOp::Always;
            if (mutation == 7u)
                badPipeline.depthStencil.stencilEnabled = true;
            if (mutation == 8u)
                badPipeline.depthStencil.depthBiasEnabled = true;
            assert(Depth::validateAttachments(badPipeline, badPass, true));
        }
        const OriginalRenderPassRecordingBudget wrongRecording{2u, true, 1u, 1u, 1u, true};
        assert(OriginalRenderPassRecordingContract::validate(wrongRecording));
    }
    ShaderPackageDesc package;
    auto layout = validator->createPipelineLayout({});
    assert(layout);
    RenderPipelineDesc profile;
    profile.layout = layout.get();
    profile.shaderPackage = &package;
    profile.colorTargets = {{TextureFormat::Rgba16Float}};
    assert(!Contract::validateProfile(profile));
    profile.multisample.sampleCount = 2u;
    assert(Contract::validateProfile(profile));
    profile.multisample.sampleCount = 1u;
    profile.colorTargets.clear();
    assert(Contract::validateProfile(profile));
    profile.colorTargets = {{TextureFormat::Rgba16Float}};
    for (const auto type : {DescriptorType::SampledTexture,
             DescriptorType::Sampler,
             DescriptorType::UniformBuffer,
             DescriptorType::ReadOnlyStorageBuffer,
             DescriptorType::StorageBuffer,
             DescriptorType::StorageTexture,
             DescriptorType::InputAttachment}) {
        PipelineLayoutDesc ld;
        DescriptorLayoutDesc descriptors;
        descriptors.name = "CreationProfile";
        DescriptorBindingDesc binding;
        binding.type = type;
        binding.stages = ShaderStage::Fragment;
        descriptors.bindings.push_back(binding);
        ld.descriptorLayouts.push_back(std::move(descriptors));
        ld.descriptorSets.push_back({0u, "CreationProfile"});
        auto candidate = validator->createPipelineLayout(ld);
        assert(candidate);
        profile.layout = candidate.get();
        const bool unsupported = type == DescriptorType::StorageBuffer
            || type == DescriptorType::StorageTexture || type == DescriptorType::InputAttachment;
        assert(bool(Contract::validateProfile(profile)) == unsupported);
    }
    std::puts(
        "Synchronous original graphics contract PASS; four explicit backend dispositions, creation/custody only");
}

class Fixture final {
public:
    std::shared_ptr<VulkanRhiRuntime> runtime;
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 10u> quotas;
    core::UniquePtr<GpuDevice> device;
    std::shared_ptr<const OriginalGraphicsPipelineInput> input;
    std::shared_ptr<PipelineLayout> layout;
    RenderPipelineDesc authored;
    Contract::Budget budget{1u, true, 0u, 256u, bytes, bytes};
    const char* diagnostic = nullptr;

    explicit Fixture(const std::shared_ptr<VulkanRhiRuntime>& runtime,
        bool bindingResources = false,
        bool mixed = false,
        bool foreground = false,
        unsigned historySlots = 0u,
        bool spatial = false,
        bool loadPipeline = true)
    {
        this->runtime = runtime;
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        VulkanRhiDeviceDesc desc;
        desc.runtime = runtime;
        desc.capabilityContract.backend = Backend::Vulkan;
        desc.memoryBudgetBytes = 1024u * 1024u;
        desc.directObjectBudget = {1u,
            "OriginalGraphicsCreationFixture/1",
            mixed ? (foreground ? 8u : 6u) : 2u,
            mixed                  ? 12u + std::uint32_t(foreground) + (spatial ? 3u : 0u)
                : bindingResources ? 4u
                                   : 1u};
        device = createVulkanRhiDevice(desc);
        assert(
            device && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quotas[0]));
        if (bindingResources)
            assert(!device->prepareOriginalDeviceNativeResultLoss(
                {1u, true, 1u, 2u, bytes}, quotas[8]));
        OriginalResourceRegistrationBudget registry;
        registry.contractVersion = bindingResources ? 10u : 6u;
        registry.prototypeOptIn = true;
        registry.maxPipelineLayouts = registry.maxGraphicsPipelines = 1u;
        registry.maxRetainedOwners = 11u;
        if (bindingResources) {
            registry.maxBuffers = 2u;
            registry.maxTextures = 3u;
            registry.maxTextureViews = 5u;
            registry.maxDescriptorArenas = registry.maxDescriptorSets = 1u;
            registry.maxRetainedOwners +=
                11u * OriginalResourceRegistrationContract::perResourceOwnerCount
                + OriginalResourceRegistrationContract::perDescriptorOwnerCount;
        }
        if (mixed) {
            registry.maxBuffers = 6u;
            registry.maxComputePipelines = 2u;
            registry.maxPipelineLayouts = 3u;
            registry.maxDescriptorSets = 3u;
            registry.maxRetainedOwners +=
                6u * OriginalResourceRegistrationContract::perResourceOwnerCount
                + 2u * OriginalResourceRegistrationContract::perDescriptorOwnerCount
                + 2u
                    * OriginalResourceRegistrationContract::ownerCount(
                        OriginalRegisteredResourceKind::ComputePipeline, registry.contractVersion);
        }
        if (foreground) {
            ++registry.maxBuffers;
            ++registry.maxGraphicsPipelines;
            ++registry.maxPipelineLayouts;
            ++registry.maxDescriptorSets;
            registry.maxRetainedOwners +=
                2u * OriginalResourceRegistrationContract::perResourceOwnerCount
                + OriginalResourceRegistrationContract::perDescriptorOwnerCount
                + OriginalResourceRegistrationContract::ownerCount(
                    OriginalRegisteredResourceKind::GraphicsPipeline, registry.contractVersion);
        }
        if (historySlots) {
            // Persistent raw/resolved slots, one current density/readback pair
            // and two immutable constants generations during cold replacement.
            const auto historyBuffers =
                2u * historySlots + 2u + 2u + unsigned(foreground) + (spatial ? 2u : 0u);
            registry.maxRetainedOwners += (historyBuffers - registry.maxBuffers)
                * OriginalResourceRegistrationContract::perResourceOwnerCount;
            registry.maxBuffers = historyBuffers;
        }
        registry.maxCpuMetadataBytes = bytes;
        if (bindingResources) {
            const auto loss = device->observeOriginalDeviceNativeLoss(diagnostic);
            assert(loss && !diagnostic);
            assert(!device->prepareOriginalTypedNativeLossRegistration(
                {3u, true}, registry, quotas[1], *loss));
        } else
            assert(!device->prepareOriginalResourceRegistration(registry, quotas[1]));
        if (!loadPipeline)
            return;
        core::pmr::String text{&core::defaultCpuMemoryResource()};
        shader::GraphicsShaderModuleLoadBudget load{bytes, 8u * bytes, bytes, 4u, 4u, 256u, 2u};
        auto artifact = shader::GraphicsShaderModuleArtifact::load(
            spatial ? ARP_ORIGINAL_CLOUD_SPATIAL_MODULE : ARP_ORIGINAL_CLOUD_MODULE,
            load,
            quotas[2],
            text);
        if (!artifact)
            std::fprintf(stderr, "cloud original module: %s\n", text.c_str());
        assert(artifact);
        input = OriginalGraphicsPipelineInput::prepare(artifact, {1u, bytes}, quotas[3], text);
        assert(input);
        const auto target = OriginalGraphicsPipelineInput::selectTarget(input, Backend::Vulkan);
        assert(!target.diagnostic);
        budget.maxTargetBytes = target.sources[0]->size() + target.sources[1]->size();
        DescriptorLayoutDesc bindings;
        bindings.name = "Cloud.OriginalComposite";
        for (const auto& reflected : input->package().reflection.descriptorBindings) {
            DescriptorBindingDesc binding;
            binding.binding = reflected.binding;
            binding.type = reflected.type;
            binding.stages = reflected.stages;
            binding.arrayCount = reflected.arrayCount;
            binding.depthTexture = reflected.depthTexture;
            // CloudComposite/1 declares exact unfilterable 2D Load inputs.
            if (binding.type == DescriptorType::SampledTexture) {
                binding.textureViewDimensionConstrained = true;
                binding.textureViewDimension = TextureViewDimension::Texture2D;
                binding.sampledTextureFilterable = false;
            }
            bindings.bindings.push_back(binding);
        }
        PipelineLayoutDesc ld;
        ld.descriptorLayouts.push_back(std::move(bindings));
        ld.descriptorSets.push_back({0u, "Cloud.OriginalComposite"});
        layout = core::shareUnique(device->createOriginalPipelineLayout(ld));
        if (!layout)
            std::fprintf(stderr,
                "original cloud layout: %s\n",
                device->lastOriginalResourceRegistrationDiagnostic().data());
        assert(layout);
        authored.label.assign(128u, 'p');
        authored.layout = layout.get();
        authored.shaderPackage = &input->package();
        authored.vertexShader = input->package().entrypoints[0];
        authored.fragmentShader = input->package().entrypoints[1];
        authored.colorFormat = TextureFormat::Rgba16Float;
        authored.colorTargets = {{TextureFormat::Rgba16Float}};
        authored.cullMode = CullMode::None;
        authored.depthStencil.depthTestEnabled = authored.depthStencil.depthWriteEnabled = false;
        authored.dynamicViewportScissor = true;
    }

    auto create()
    {
        return createVulkanOriginalGraphicsCreation(
            *device, input, layout, authored, budget, quotas[4], quotas[5], diagnostic);
    }
};

void verifyNative(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    Fixture f(runtime);
    const auto none = NativeProbe::live;
    auto factory = [&] {
        return f.create();
    };
    const auto refusal = [&](const auto& input,
                             const auto& layout,
                             const auto& definition,
                             auto budget,
                             const auto& owner,
                             const auto& native) {
        const auto ownerBytes = f.quotas[4]->liveBytes(), nativeBytes = f.quotas[5]->liveBytes();
        NativeProbe::ordinal = 0u;
        assert(!createVulkanOriginalGraphicsCreation(
            *f.device, input, layout, definition, budget, owner, native, f.diagnostic));
        assert(f.diagnostic && !NativeProbe::ordinal && NativeProbe::live == none
            && f.quotas[4]->liveBytes() == ownerBytes && f.quotas[5]->liveBytes() == nativeBytes);
    };
    auto invalidBudget = f.budget;
    --invalidBudget.maxTargetBytes;
    refusal(f.input, f.layout, f.authored, invalidBudget, f.quotas[4], f.quotas[5]);
    invalidBudget = f.budget;
    invalidBudget.maxLabelBytes = f.authored.label.size() - 1u;
    refusal(f.input, f.layout, f.authored, invalidBudget, f.quotas[4], f.quotas[5]);
    refusal(f.input, f.layout, f.authored, f.budget, f.quotas[5], f.quotas[5]);
    refusal(std::shared_ptr<const OriginalGraphicsPipelineInput>(f.quotas[3], f.input.get()),
        f.layout,
        f.authored,
        f.budget,
        f.quotas[4],
        f.quotas[5]);
    refusal(f.input,
        std::shared_ptr<PipelineLayout>(f.quotas[1], f.layout.get()),
        f.authored,
        f.budget,
        f.quotas[4],
        f.quotas[5]);
    auto changed = RenderPipelineStateCopy::copy(f.authored);
    for (unsigned field = 0u; field < 8u; ++field) {
        changed = RenderPipelineStateCopy::copy(f.authored);
        if (field == 0u)
            changed.subpassIndex = 1u;
        if (field == 1u)
            changed.patchControlPointCount = 3u;
        if (field == 2u)
            changed.topology = PrimitiveTopology::PatchList;
        if (field == 3u)
            changed.tessellationControlShader.entryPoint = "other";
        if (field == 4u)
            changed.vertexBuffers.emplace_back();
        if (field == 5u)
            changed.colorTargets.push_back(changed.colorTargets.front());
        if (field == 6u)
            changed.multisample.sampleCount = 4u;
        if (field == 7u)
            changed.depthStencil.depthBiasClamp = 1.0f;
        assert(Contract::validateProfile(changed));
        refusal(f.input, f.layout, changed, f.budget, f.quotas[4], f.quotas[5]);
    }
    changed = RenderPipelineStateCopy::copy(f.authored);
    changed.fragmentShader.entryPoint = "missingFragment";
    refusal(f.input, f.layout, changed, f.budget, f.quotas[4], f.quotas[5]);
    auto copiedPackage = ShaderPackageStateCopy::copy(f.input->package());
    changed = RenderPipelineStateCopy::copy(f.authored);
    changed.shaderPackage = &copiedPackage;
    refusal(f.input, f.layout, changed, f.budget, f.quotas[4], f.quotas[5]);
    Fixture foreign(runtime);
    changed = RenderPipelineStateCopy::copy(f.authored);
    changed.layout = foreign.layout.get();
    refusal(f.input, foreign.layout, changed, f.budget, f.quotas[4], f.quotas[5]);
    test::CpuQuotaFactoryFixture owner(f.quotas[4]), native(f.quotas[5]);
    const auto ownerPeak = owner.measure(factory);
    const auto nativePeak = native.measure(factory);
    owner.verify(ownerPeak, factory);
    native.verify(nativePeak, factory);
    assert(NativeProbe::live == none);
    NativeProbe::ordinal = 0u;
    auto receipt = factory();
    if (!receipt)
        std::fprintf(stderr, "original creation: %s\n", f.diagnostic);
    assert(receipt && !f.diagnostic);
    const auto nativeCalls = NativeProbe::ordinal;
    receipt.reset();
    assert(NativeProbe::live == none);
    for (unsigned prefix = 1u; prefix <= nativeCalls; ++prefix) {
        NativeProbe::ordinal = 0u;
        NativeProbe::failAt = prefix;
        assert(!factory() && f.diagnostic && NativeProbe::live == none
            && NativeProbe::ordinal == prefix);
        assert(!f.quotas[4]->liveBytes() && !f.quotas[5]->liveBytes());
    }
    auto* failurePrevious = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    for (unsigned prefix = 1u; prefix <= nativeCalls; ++prefix) {
        NativeProbe::ordinal = 0u;
        NativeProbe::failAt = prefix;
        assert(!factory() && f.diagnostic && NativeProbe::live == none
            && NativeProbe::ordinal == prefix);
    }
    std::pmr::set_default_resource(failurePrevious);
    NativeProbe::failAt = 0u;
    NativeProbe::partialPipelineFailure = true;
    assert(!factory() && NativeProbe::live == none);
    NativeProbe::partialPipelineFailure = false;
    for (const char* method : {"vkCreateShaderModule",
             "vkDestroyShaderModule",
             "vkCreateDescriptorSetLayout",
             "vkDestroyDescriptorSetLayout",
             "vkCreatePipelineLayout",
             "vkDestroyPipelineLayout",
             "vkCreateRenderPass",
             "vkDestroyRenderPass",
             "vkCreateGraphicsPipelines",
             "vkDestroyPipeline"}) {
        NativeProbe::ordinal = 0u;
        NativeProbe::missing = method;
        assert(!factory() && NativeProbe::ordinal == 0u && NativeProbe::live == none);
    }
    NativeProbe::missing = nullptr;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    receipt = factory();
    std::pmr::set_default_resource(previous);
    assert(receipt && !f.diagnostic);
    auto accepted = [&] {
        return f.device->createOriginalArtifactGraphicsPipeline(
            receipt, {2u, true, bytes}, f.quotas[6]);
    };
    assert(PipelineDefinitionIdentity::matches(receipt->definition(), f.authored));
    f.authored.viewport.x = -37.0f; // Inactive fields still participate in identity.
    assert(!PipelineDefinitionIdentity::matches(receipt->definition(), f.authored));
    assert(!VulkanAcceptedOriginalGraphicsPipelineFactory::create(
        f.authored, receipt, f.quotas[6], runtime.get(), f.diagnostic));
    assert(f.diagnostic);
    f.authored.viewport.x = receipt->definition().viewport.x;
    assert(!VulkanAcceptedOriginalGraphicsPipelineFactory::create(
        receipt->definition(), receipt, f.quotas[5], runtime.get(), f.diagnostic));
    test::CpuQuotaFactoryFixture wrapper(f.quotas[6]);
    const auto wrapperPeak = wrapper.measure(accepted);
    wrapper.verify(wrapperPeak, accepted);
    assert(
        !f.device->createOriginalArtifactGraphicsPipeline(receipt, {1u, true, bytes}, f.quotas[6]));
    previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto pipeline = OriginalRegisteredPipeline::shareAcceptedOriginal(accepted(), f.diagnostic);
    std::pmr::set_default_resource(previous);
    assert(pipeline && !f.diagnostic && !pipeline->originalArtifactGraphicsDiagnostic());
    const OriginalCommandPipelineSourceBudget sb{2u,
        true,
        OriginalRegisteredResourceKind::GraphicsPipeline,
        pipeline->originalResourceRegistration().generation(),
        bytes,
        Source::exclusiveOwnerCount};
    auto sourceFactory = [&] {
        return Source::prepareOriginal(pipeline, sb, f.quotas[7], *f.device, f.diagnostic);
    };
    test::CpuQuotaFactoryFixture sourceQuota(f.quotas[7]);
    const auto sourcePeak = sourceQuota.measure(sourceFactory);
    sourceQuota.verify(sourcePeak, sourceFactory);
    auto source = sourceFactory();
    assert(source && !f.diagnostic && !source->nativeExecutionProven());
    auto forged = std::shared_ptr<const RenderPipeline>(f.quotas[6], pipeline.get());
    assert(!Source::prepareOriginal(forged, sb, f.quotas[7], *f.device, f.diagnostic));
    assert(!Source::prepareOriginal(pipeline, sb, f.quotas[7], *foreign.device, f.diagnostic));
    auto legacy = sb;
    legacy.contractVersion = 1u;
    assert(!Source::prepareOriginal(pipeline, legacy, f.quotas[7], *f.device, f.diagnostic));
    for (const auto index : {2u, 3u, 4u, 5u, 6u, 7u}) {
        f.quotas[index]->setLimitBytes(bytes - 1u);
        assert(Source::validateOriginal(source, *f.device));
        f.quotas[index]->setLimitBytes(bytes);
    }
    forged.reset();
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(f.quotas, [&]() noexcept {
        return Source::validateOriginal(source, *f.device);
    }));
    auto payload = vulkanRhiNativePayload(*pipeline);
    assert(payload && payload->backendVulkanDeviceHandle() == runtime->logicalDevice()
        && payload->backendVulkanRenderPipelineHandle()
        && payload->backendVulkanRenderPipelineLayoutHandle());
    assert(!f.create()
        && f.diagnostic); // The same direct-object budget is held by the receipt/payload.
    std::weak_ptr<const Receipt> receiptObserver = receipt;
    receipt.reset();
    pipeline.reset();
    assert(!receiptObserver.expired() && !Source::validateOriginal(source, *f.device));
    source.reset();
    assert(receiptObserver.expired()
        && NativeProbe::live[4] == 1u); // payload still holds native lease
    payload.reset();
    receiptObserver.reset();
    assert(NativeProbe::live == none);
    receipt = factory();
    assert(receipt);
    pipeline = OriginalRegisteredPipeline::shareAcceptedOriginal(accepted(), f.diagnostic);
    assert(pipeline);
    auto nextSourceBudget = sb;
    nextSourceBudget.registrationGeneration = pipeline->originalResourceRegistration().generation();
    source =
        Source::prepareOriginal(pipeline, nextSourceBudget, f.quotas[7], *f.device, f.diagnostic);
    assert(source);
    f.input.reset();
    f.layout.reset();
    receipt.reset();
    pipeline.reset();
    assert(!Source::validateOriginal(source));
    f.device.reset();
    assert(Source::validateOriginal(source) && NativeProbe::live[4] == 1u);
    source.reset();
    assert(NativeProbe::live == none);
    std::printf(
        "Original Vulkan cloud-composite creation PASS native-calls=%u owner=%llu native-cpu=%llu wrapper=%llu source=%llu; recording closed\n",
        nativeCalls,
        static_cast<unsigned long long>(ownerPeak),
        static_cast<unsigned long long>(nativePeak),
        static_cast<unsigned long long>(wrapperPeak),
        static_cast<unsigned long long>(sourcePeak));
}

struct MixedCloudProgramData {
    std::array<std::size_t, 2u> pipelines{}, sets{};
    GpuBuffer *raw = nullptr, *resolved = nullptr, *readback = nullptr;
    std::array<std::uint64_t, 2u> sampleOffsets{};
    std::uint32_t rowBytes = 0u;
};

void verifyOrdinaryCloudDraw(Fixture& f,
    const std::shared_ptr<VulkanRhiRuntime>& runtime,
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const std::array<GpuTexture*, 3u>& textureObjects,
    std::size_t pipelineIndex,
    std::size_t descriptorIndex,
    unsigned mode,
    const MixedCloudProgramData* mixed = nullptr,
    bool measureBudget = true,
    bool deviceIssuer = false)
{
    using K = CommandUsageKind;
    using Sources = OriginalCommandResourceSources;
    const auto require = [](const char* id) {
        if (id)
            std::fprintf(stderr, "ordinary cloud Draw: %s\n", id);
        assert(!id);
    };
    const auto& sources = inventory->sources();
    const auto parent = [&](std::size_t index) -> GpuTexture& {
        const auto* canonical =
            std::get<std::shared_ptr<const OriginalCommandTextureLease>>(sources[index])
                ->originalObject();
        const auto ordinal = index == 8u ? 2u : (index - 2u) / 2u;
        assert(ordinal < textureObjects.size() && canonical == textureObjects[ordinal]);
        return *textureObjects[ordinal];
    };
    const auto view = [&](std::size_t index) -> const GpuTextureView& {
        return *std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(sources[index])
                    ->originalObject();
    };
    core::pmr::Vector<CommandUsage> commands;
    core::pmr::Vector<OriginalCommandUsage::Reference> references;
    core::pmr::Vector<CommandVariableArguments::Input> arguments;
    core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
    std::array<ResourceBarrierDesc, 6u> barriers;
    std::array<ResourceBarrierDesc, 2u> bufferBarriers;
    TextureToBufferCopyDesc copy;
    const auto command =
        [&](K kind, std::initializer_list<std::size_t> operands, CommandScalarArguments scalar) {
            const auto ordinal = commands.size();
            commands.push_back({kind,
                static_cast<std::uint32_t>(references.size()),
                static_cast<std::uint32_t>(operands.size())});
            for (const auto index : operands)
                references.push_back({static_cast<std::uint32_t>(index)});
            arguments.push_back({scalar});
            return ordinal;
        };
    const auto none = [](K k) {
        return *CommandScalarArguments::capture(k, CommandScalarArguments::None{});
    };
    const auto barrier = [&](std::size_t number,
                             std::size_t source,
                             ResourceState before,
                             ResourceState after,
                             PipelineStage sourceStage,
                             ResourceAccess sourceAccess,
                             PipelineStage destinationStage,
                             ResourceAccess destinationAccess) {
        auto& b = barriers[number];
        b.resource = parent(source).id();
        b.textureSubresources = {
            0u, 1u, 0u, 1u, source == 4u ? TextureAspect::Depth : TextureAspect::Color};
        b.before = before;
        b.after = after;
        b.sourceStages = sourceStage;
        b.sourceAccess = sourceAccess;
        b.destinationStages = destinationStage;
        b.destinationAccess = destinationAccess;
        const auto ordinal = command(K::TextureBarrier, {source}, none(K::TextureBarrier));
        descriptors.push_back(
            {ordinal, *ResourceDependencyArguments::capture(K::TextureBarrier, b)});
    };
    const auto computePass = [&](std::size_t ordinal) {
        const auto begin = command(K::BeginComputePass, {}, none(K::BeginComputePass));
        const auto pipelineIndex = mixed->pipelines[ordinal];
        const auto descriptorIndex = mixed->sets[ordinal];
        const auto& pipeline =
            *std::get<std::shared_ptr<const OriginalCommandComputePipelineSource>>(
                sources[pipelineIndex])
                 ->originalObject();
        const auto& set = *std::get<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
            sources[descriptorIndex])
                               ->originalObject();
        descriptors.push_back(
            {command(K::BindComputePipeline, {pipelineIndex}, none(K::BindComputePipeline)),
                OriginalCommandPipelineBinding{K::BindComputePipeline,
                    pipelineIndex,
                    begin,
                    pipeline.originalResourceRegistration().generation()}});
        descriptors.push_back({command(K::BindComputeDescriptor,
                                   {descriptorIndex},
                                   *CommandScalarArguments::capture(K::BindComputeDescriptor,
                                       CommandScalarArguments::DescriptorBinding{0u})),
            OriginalCommandDescriptorBinding{K::BindComputeDescriptor,
                descriptorIndex,
                pipelineIndex,
                begin,
                set.originalResourceRegistration().generation(),
                set.contentRevision()}});
        command(K::Dispatch,
            {},
            *CommandScalarArguments::capture(
                K::Dispatch, CommandScalarArguments::Dispatch{1u, 1u, 1u}));
        command(K::EndComputePass, {}, none(K::EndComputePass));
        auto& b = bufferBarriers[ordinal];
        b.resource = ordinal ? mixed->resolved->id() : mixed->raw->id();
        b.before = ResourceState::ShaderWrite;
        b.after = ResourceState::ShaderRead;
        b.sourceStages = PipelineStage::ComputeShader;
        b.sourceAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        b.destinationStages =
            ordinal ? PipelineStage::FragmentShader : PipelineStage::ComputeShader;
        b.destinationAccess = ResourceAccess::ShaderRead;
        descriptors.push_back(
            {command(K::BufferBarrier, {ordinal ? 1u : 11u}, none(K::BufferBarrier)),
                *ResourceDependencyArguments::capture(K::BufferBarrier, b)});
    };
    const auto graphQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
    const auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
    const auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
    const OriginalRenderPassStorageContract::Budget storage{
        1u, true, 2u, 1u, 3u, {1u, true, 2u, 0u, 0u, 0u, 0u, bytes}};
    RenderPassDesc initialization;
    RenderPassColorAttachmentDesc color;
    color.target = view(6u).id();
    color.format = TextureFormat::Rgba16Float;
    color.loadOp = LoadOp::Clear;
    color.storeOp = StoreOp::Store;
    color.clearColor = {.125f, .375f, .75f, .625f};
    initialization.colorAttachments.push_back(color);
    initialization.hasDepthStencilAttachment = true;
    auto& depth = initialization.depthStencilAttachment;
    depth.target = view(7u).id();
    depth.format = TextureFormat::D32Float;
    depth.depthLoadOp = LoadOp::Clear;
    depth.depthStoreOp = StoreOp::Store;
    depth.depthClearValue = 1.f;
    depth.stencilLoadOp = LoadOp::DontCare;
    depth.stencilStoreOp = StoreOp::DontCare;
    const char* id = nullptr;
    auto initialPass =
        RenderPassArguments::prepareOriginal(initialization, storage.arguments, graphQuota, id);
    require(id);
    RenderPassDesc composite;
    color.target = view(9u).id();
    color.clearColor = {0.f, 0.f, 0.f, 0.f};
    composite.colorAttachments.push_back(color);
    auto compositePass =
        RenderPassArguments::prepareOriginal(composite, storage.arguments, graphQuota, id);
    require(id);
    const auto colorAccess =
        ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite;
    const auto depthAccess = ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite;
    barrier(0u,
        2u,
        ResourceState::Undefined,
        ResourceState::ColorAttachment,
        PipelineStage::TopOfPipe,
        ResourceAccess::None,
        PipelineStage::ColorAttachmentOutput,
        colorAccess);
    barrier(1u,
        4u,
        ResourceState::Undefined,
        ResourceState::DepthAttachment,
        PipelineStage::TopOfPipe,
        ResourceAccess::None,
        PipelineStage::DepthStencil,
        depthAccess);
    descriptors.push_back(
        {command(K::BeginRenderPass, {6u, 7u}, none(K::BeginRenderPass)), initialPass});
    command(K::EndRenderPass, {}, none(K::EndRenderPass));
    barrier(2u,
        2u,
        ResourceState::ColorAttachment,
        ResourceState::ShaderRead,
        PipelineStage::ColorAttachmentOutput,
        colorAccess,
        PipelineStage::FragmentShader,
        ResourceAccess::ShaderRead);
    barrier(3u,
        4u,
        ResourceState::DepthAttachment,
        ResourceState::ShaderRead,
        PipelineStage::DepthStencil,
        depthAccess,
        mixed ? PipelineStage::ComputeShader | PipelineStage::FragmentShader
              : PipelineStage::FragmentShader,
        ResourceAccess::ShaderRead);
    if (mixed) {
        computePass(0u);
        computePass(1u);
    }
    barrier(4u,
        8u,
        ResourceState::Undefined,
        ResourceState::ColorAttachment,
        PipelineStage::TopOfPipe,
        ResourceAccess::None,
        PipelineStage::ColorAttachmentOutput,
        colorAccess);
    const auto begin = command(K::BeginRenderPass, {9u}, none(K::BeginRenderPass));
    descriptors.push_back({begin, compositePass});
    const auto& pipeline =
        *std::get<std::shared_ptr<const Source>>(sources[pipelineIndex])->originalObject();
    descriptors.push_back(
        {command(K::BindGraphicsPipeline, {pipelineIndex}, none(K::BindGraphicsPipeline)),
            OriginalCommandPipelineBinding{K::BindGraphicsPipeline,
                pipelineIndex,
                begin,
                pipeline.originalResourceRegistration().generation()}});
    const ViewportState viewport{0.f, 0.f, 4.f, 4.f, 0.f, 1.f};
    const ScissorState scissor{0, 0, 4u, 4u};
    command(K::SetViewport, {}, *CommandScalarArguments::capture(K::SetViewport, viewport));
    command(K::SetScissor, {}, *CommandScalarArguments::capture(K::SetScissor, scissor));
    const auto& set = *std::get<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
        sources[descriptorIndex])
                           ->originalObject();
    descriptors.push_back({command(K::BindRenderDescriptor,
                               {descriptorIndex},
                               *CommandScalarArguments::capture(K::BindRenderDescriptor,
                                   CommandScalarArguments::DescriptorBinding{0u})),
        OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
            descriptorIndex,
            pipelineIndex,
            begin,
            set.originalResourceRegistration().generation(),
            set.contentRevision()}});
    command(K::Draw,
        {},
        *CommandScalarArguments::capture(K::Draw, CommandScalarArguments::Draw{3u, 1u, 0u, 0u}));
    command(K::EndRenderPass, {}, none(K::EndRenderPass));
    if (mixed) {
        barrier(5u,
            8u,
            ResourceState::ColorAttachment,
            ResourceState::CopySrc,
            PipelineStage::ColorAttachmentOutput,
            colorAccess,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead);
        command(K::BeginTransferPass, {}, none(K::BeginTransferPass));
        copy.sourceTexture = parent(8u).id();
        copy.destinationBuffer = mixed->readback->id();
        copy.extent = {4u, 4u, 1u};
        copy.textureSubresources = {0u, 1u, 0u, 1u, TextureAspect::Color};
        copy.bytesPerRow = mixed->rowBytes;
        copy.rowsPerImage = 4u;
        descriptors.push_back(
            {command(K::TextureToBufferCopy, {8u, 13u}, none(K::TextureToBufferCopy)),
                *TransferCopyArguments::capture(copy)});
        command(K::EndTransferPass, {}, none(K::EndTransferPass));
    }
    command(K::Finish, {}, none(K::Finish));
    auto usage = OriginalCommandUsage::prepareOriginal(inventory,
        commands.data(),
        commands.size(),
        references.data(),
        references.size(),
        graphQuota,
        id);
    require(id);
    auto store = CommandVariableArguments::prepareOriginal(arguments.data(),
        arguments.size(),
        {1u, true, static_cast<std::uint32_t>(commands.size()), 0u, 0u, 0u, bytes},
        graphQuota,
        id);
    require(id);
    auto scalars = OriginalCommandScalars::prepareOriginal(usage, store, graphQuota, id);
    require(id);
    if (mixed) {
        assert(!OriginalCommandInputs::prepareOriginal(
            scalars, descriptors.data(), descriptors.size(), {6u, true, 3u, 3u}, graphQuota, id));
        assert(id);
    }
    auto inputs = OriginalCommandInputs::prepareOriginal(scalars,
        descriptors.data(),
        descriptors.size(),
        {mixed ? 10u : 6u, true, mixed ? 3u : 1u, mixed ? 3u : 1u},
        graphQuota,
        id);
    require(id);
    // The registration union includes the nested pipeline layout and arena.
    OriginalCommandStateAdmissionBudget state{5u,
        true,
        1u,
        static_cast<std::uint32_t>(sources.size() + (mixed ? 4u : 2u)),
        32u,
        32u,
        inputs->retainedOwnerCount() + 128u,
        bytes};
    state.descriptorConsumption = {4u, true, 1u, 1u, true, true, mixed ? 3u : 1u, mixed ? 12u : 4u};
    state.descriptorConsumption.maxTextureAccesses = mixed ? 3u : 2u;
    state.descriptorConsumption.generalShaderResourcesVersion = 2u;
    state.descriptorConsumption.generalShaderResourcesOptIn = true;
    state.renderDescriptorUsage = {4u, true, 1u, 1u, true, true, commands.size(), 1u, 4u};
    state.renderDescriptorUsage.maxTextureAccesses = 2u;
    state.renderDescriptorUsage.generalShaderResourcesVersion = 2u;
    state.renderDescriptorUsage.generalShaderResourcesOptIn = true;
    auto graph =
        OriginalCommandResourceStates::prepareOriginal(inputs, *f.device, state, stateQuota, id);
    require(id);
    OriginalCommandStreamBudget stream;
    stream.contractVersion = 5u;
    stream.prototypeOptIn = true;
    stream.maxCommands = commands.size();
    stream.maxDescriptors = descriptors.size();
    stream.maxReferences = references.size();
    stream.maxCopyElements = mixed ? 1u : 0u;
    stream.maxRenderPasses = 2u;
    stream.maxAttachmentReferences = 3u;
    stream.maxPipelineBinds = stream.maxDescriptorBinds = stream.maxConsumptions = mixed ? 3u : 1u;
    stream.maxImplicitAccesses = mixed ? 16u : 8u;
    stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
    stream.maxCpuMetadataBytes = bytes;
    stream.descriptorConsumption = state.descriptorConsumption;
    stream.renderDescriptorUsage = state.renderDescriptorUsage;
    stream.stateSequence = {
        5u, true, static_cast<std::uint32_t>(commands.size()), 0u, 32u, 64u, mixed ? 16u : 8u};
    CommandRecordingBudget recording{1u, true, commands.size()};
    for (const auto& op : commands)
        recording.limits.append(op.kind);
    recording.limits.resourceReferences = references.size();
    recording.limits.descriptorElements = mixed ? 12u : 4u;
    auto program = OriginalCommandStreamProgram::prepareRecordingBoundOriginalTyped(
        graph, stream, recording, recordingQuota, id);
    require(id);
    OriginalRenderPassPreparationRequest request{mixed ? 6u : 5u,
        true,
        storage,
        7u,
        2u,
        OriginalRenderPassContentBudget{mixed ? 3u : 2u, true, 3u, 1u, 64u, 512u, 512u},
        OriginalRenderPassRecordingBudget{mixed ? 3u : 2u, true, 512u, 1024u}};
    const VulkanRhiNativeResourceAllocator resources(runtime);
    semantics::NativeBackendCapabilityContract capabilities;
    capabilities.backend = Backend::Vulkan;
    core::pmr::String diagnostic;
    const auto factory = [&] {
        if (deviceIssuer) {
            const char* extensionDiagnostic = nullptr;
            auto* extension = OriginalPreparedCommandDeviceExtension::query(
                *f.device, request, extensionDiagnostic);
            require(extensionDiagnostic);
            assert(extension);
            return extension->prepare(program, request, diagnostic);
        }
        return createVulkanRhiCommandEncoderWithColdRenderPasses(
            resources, {}, nullptr, capabilities, program, *f.device, request, diagnostic);
    };
    if (!mode) {
        const auto checked = OriginalRenderPassRecordingContract::inspect(
            program, storage, *request.content, *request.recording);
        require(checked.diagnostic);
        for (unsigned field = 0u; field < 2u; ++field) {
            auto shortRequest = request;
            if (!field)
                shortRequest.recording->maxCommandVisits = checked.commandVisits - 1u;
            else
                shortRequest.recording->maxAttachmentVisits = checked.attachmentVisits - 1u;
            assert(!createVulkanRhiCommandEncoderWithColdRenderPasses(resources,
                {},
                nullptr,
                capabilities,
                program,
                *f.device,
                shortRequest,
                diagnostic));
            assert(!resources.sharedStats()->nativeDrawRecordCount
                && !resources.sharedStats()->nativeQueueSubmitCount);
        }
        for (const auto* missing : {"vkCmdBindPipeline",
                 "vkCmdBindDescriptorSets",
                 "vkCmdSetViewport",
                 "vkCmdSetScissor",
                 "vkCmdDraw"}) {
            NativeProbe::missing = missing;
            assert(!factory() && diagnostic == "RHI-ORDINARY-GRAPHICS-RECORDING-ENTRYPOINT");
            NativeProbe::missing = nullptr;
        }
        if (mixed) {
            for (const auto* missing : {"vkCmdDispatch", "vkCmdCopyImageToBuffer"}) {
                NativeProbe::missing = missing;
                assert(!factory() && diagnostic == "RHI-ORDINARY-MIXED-RECORDING-ENTRYPOINT");
                NativeProbe::missing = nullptr;
            }
        }
        {
            auto probe = factory();
            if (!probe)
                std::fprintf(
                    stderr, "ordinary cloud full-budget factory: %s\n", diagnostic.c_str());
            assert(probe);
        }
        if (measureBudget) {
            test::CpuQuotaFactoryFixture quota(recordingQuota);
            unsigned quotaProbe = 0u;
            const auto required = quota.measureSingleUse([] {},
                [&] {
                    if (mixed)
                        std::printf("ORDINARY_MIXED_QUOTA_PROBE %u\n", ++quotaProbe);
                    return factory();
                });
            quota.verifySingleUse(required, [] {}, factory);
            std::printf(mixed ? "ORDINARY_MIXED_COLD_CPU_EXACT_BYTES %llu\n"
                              : "ORDINARY_CLOUD_DRAW_COLD_CPU_EXACT_BYTES %llu\n",
                static_cast<unsigned long long>(required));
        }
    }
    auto encoder = factory();
    if (!encoder)
        std::fprintf(stderr, "ordinary cloud factory: %s\n", diagnostic.c_str());
    assert(encoder);
    std::optional<tests::RenderDocCaptureScope> capture;
    if (mode == 3u || (mixed && mode == 4u))
        capture.emplace();
    const auto quotas = std::array{graphQuota, stateQuota, recordingQuota};
    core::UniquePtr<CommandBuffer> finished;
    assert(
        !test::CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
            const auto firstTransition = encoder->transition(parent(2u), barriers[0]);
            if (!firstTransition.success)
                std::fprintf(stderr, "ordinary cloud first transition mode=%u\n", mode);
            assert(firstTransition.success);
            if (!mode)
                return nullptr;
            assert(encoder->transition(parent(4u), barriers[1]).success);
            auto initial = encoder->beginRenderPass(initialization);
            assert(initial && initial->end().success);
            assert(encoder->transition(parent(2u), barriers[2]).success);
            assert(encoder->transition(parent(4u), barriers[3]).success);
            if (mixed) {
                for (std::size_t i = 0u; i < 2u; ++i) {
                    auto compute = encoder->beginComputePass({});
                    const auto& pipeline =
                        *std::get<std::shared_ptr<const OriginalCommandComputePipelineSource>>(
                            sources[mixed->pipelines[i]])
                             ->originalObject();
                    const auto& set =
                        *std::get<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
                            sources[mixed->sets[i]])
                             ->originalObject();
                    assert(compute && compute->setPipeline(pipeline).success
                        && compute->bindDescriptorSet(0u, set, nullptr, 0u).success);
                    if (mode == 5u) {
                        assert(!compute->dispatch(2u, 1u, 1u).success
                            && !resources.sharedStats()->nativeExecutableDispatchRecordCount
                            && !compute->end().success && !encoder->finish());
                        return nullptr;
                    }
                    assert(compute->dispatch(1u, 1u, 1u).success && compute->end().success);
                    assert(
                        encoder->transition(i ? *mixed->resolved : *mixed->raw, bufferBarriers[i])
                            .success);
                }
            }
            assert(encoder->transition(parent(8u), barriers[4]).success);
            auto pass = encoder->beginRenderPass(composite);
            assert(pass && pass->setPipeline(pipeline).success
                && pass->setViewport(viewport).success && pass->setScissor(scissor).success
                && pass->bindDescriptorSet(0u, set, nullptr, 0u).success);
            if (mode == 2u) {
                assert(!pass->draw(4u, 1u, 0u, 0u).success && !pass->end().success
                    && !encoder->finish());
                return nullptr;
            }
            assert(pass->draw(3u, 1u, 0u, 0u).success && pass->end().success);
            if (mixed) {
                assert(encoder->transition(parent(8u), barriers[5]).success);
                auto transfer = encoder->beginTransferPass({});
                assert(transfer && transfer->copyTextureToBuffer(copy).success
                    && transfer->end().success);
            }
            finished = encoder->finish();
            assert(finished);
            return nullptr;
        }));
    if (mode < 3u || mode == 5u) {
        finished.reset();
        encoder.reset();
        assert(parent(2u).observeOriginalCommittedState(barriers[0].textureSubresources)->state()
            == ResourceState::Undefined);
        for (std::size_t i = 0u; i < 2u; ++i) {
            const auto& buffer =
                *std::get<std::shared_ptr<const OriginalCommandBufferLease>>(sources[i])
                     ->originalObject();
            const auto native = vulkanRhiNativePayload(buffer);
            assert(mixed ? native->originalHostDefinedBytesMatch(buffer)
                         : native->originalHostInitializationMatches(buffer));
        }
        assert(!resources.sharedStats()->nativeQueueSubmitCount
            && (mode != 2u || !resources.sharedStats()->nativeDrawRecordCount));
        return;
    }
    auto native = vulkanRhiCommandPayload(*finished);
    auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
    assert(native && gate);
    assert(
        !test::CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
            assert(native->submit({gate, 1u}, diagnostic)
                && acceptRecordedCommandOwnership(*finished));
            assert(!native->submittedFenceCompleted()
                && !parent(8u).observeOriginalContent(barriers[4].textureSubresources));
            finished.reset();
            encoder.reset();
            return nullptr;
        }));
    assert(gate->hostSignal(1u) == VK_SUCCESS);
    assert(
        !test::CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
            assert(native->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
            assert(native->submittedFenceCompleted());
            retireCompletedVulkanRhiSubmittedCommands();
            return nullptr;
        }));
    capture->end();
    assert(parent(8u).observeOriginalContent({0u, 1u, 0u, 1u, TextureAspect::Color})->state()
        == common::OriginalTextureContentControl::State::Completed);
    assert(deviceIssuer || resources.sharedStats()->nativeDrawRecordCount == 1u);
    if (mixed) {
        assert(deviceIssuer
            || (resources.sharedStats()->nativeExecutableDispatchRecordCount == 2u
                && resources.sharedStats()->nativeImageToBufferCopyRecordCount == 1u));
        const auto readback = vulkanRhiNativePayload(*mixed->readback);
        assert(readback
            && readback->backendInvalidateMappedRead(
                0u, mixed->readback->desc().sizeBytes, diagnostic));
        const auto* data = static_cast<const std::byte*>(readback->backendMappedData());
        assert(data);
        for (std::uint32_t y = 0u; y < 4u; ++y)
            for (std::uint32_t x = 0u; x < 4u; ++x) {
                std::array<std::uint16_t, 4u> pixel;
                std::memcpy(
                    pixel.data(), data + y * mixed->rowBytes + x * sizeof(pixel), sizeof(pixel));
                const std::array expected{.125f, .375f, .75f, .625f};
                for (std::size_t c = 0u; c < pixel.size(); ++c)
                    assert(core::halfToFloat(pixel[c]) == expected[c]);
            }
        for (std::size_t i = 0u; i < 2u; ++i) {
            const auto* object = i ? mixed->resolved : mixed->raw;
            const auto payload = vulkanRhiNativePayload(*object);
            assert(payload->backendInvalidateMappedRead(0u, object->desc().sizeBytes, diagnostic));
            renderer::cloud::CloudGpuSample sample;
            std::memcpy(&sample,
                static_cast<const std::byte*>(payload->backendMappedData())
                    + mixed->sampleOffsets[i],
                sizeof(sample));
            assert(sample.radianceTransmittance.x == 0.f && sample.radianceTransmittance.y == 0.f
                && sample.radianceTransmittance.z == 0.f && sample.radianceTransmittance.w == 1.f
                && sample.metadata.x == 1000.f && sample.metadata.y == 1000.f);
        }
        std::puts(
            "ORDINARY_MIXED_CLOUD_OK Inputs10 State5 Stream5 Preparation6 same-owner same-meter same-journal Dispatch2 Draw1 Copy1 actual-vacuum-oracle16 hot0 timeline-gated-real-fence");
    }
    if (!mixed)
        std::puts(
            "ORDINARY_CLOUD_DRAW_OK original-Inputs6-State5-Stream5 same-wrapper same-journal Source5 four-bindings nativeDraw1 hot0 cancel-reuse foreign-draw-refused caller-drop timeline-gated-real-fence");
}

void verifyBindingInspection(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    using Inspection = VulkanOriginalGraphicsBindingInspection;
    using Sources = OriginalCommandResourceSources;
    Fixture f(runtime, true);
    // Native resource wrappers and content controls have independent ceilings.
    core::CpuMemoryQuotaScope nativeOwnerScope(f.quotas[9]);
    auto receipt = f.create();
    assert(receipt);
    auto pipeline = OriginalRegisteredPipeline::shareAcceptedOriginal(
        f.device->createOriginalArtifactGraphicsPipeline(receipt, {2u, true, bytes}, f.quotas[6]),
        f.diagnostic);
    assert(pipeline);
    auto source = Source::prepareOriginal(pipeline,
        {2u,
            true,
            Source::kind,
            pipeline->originalResourceRegistration().generation(),
            bytes,
            Source::exclusiveOwnerCount},
        f.quotas[7],
        *f.device,
        f.diagnostic);
    assert(source);
    std::array<Sources::Source, 10u> prefix;
    std::array<GpuTexture*, 3u> ordinaryTextureObjects{};
    std::array<OriginalCommandDescriptorSetElement, 4u> elements;
    const auto& layout = f.layout->desc().descriptorLayouts.front();
    std::array<std::uint64_t, 2u> sizes{}, offsets{};
    for (std::size_t i = 0u; i < 2u; ++i) {
        shader::SpirvBufferDeclarationLayout declaration;
        assert(!shader::SingleEntryShaderArtifact::originalBufferDeclarationLayout(
            f.input->artifact()->fragment(), 0u, static_cast<std::uint32_t>(i), declaration));
        offsets[i] = i ? runtime->descriptorIndexing().limits.minStorageBufferOffsetAlignment
                       : runtime->descriptorIndexing().limits.minUniformBufferOffsetAlignment;
        if (declaration.hasRuntimeArray)
            offsets[i] = std::lcm(offsets[i], declaration.runtimeArrayStrideBytes);
        sizes[i] = declaration.hasRuntimeArray
            ? declaration.runtimeArrayOffsetBytes + 2u * declaration.runtimeArrayStrideBytes
            : declaration.staticOccupiedExtentBytes;
        assert(offsets[i] && sizes[i]);
        BufferDesc desc;
        desc.label = "original graphics borrowed binding";
        desc.sizeBytes = offsets[i] + sizes[i];
        desc.usage = i ? ResourceUsage::StorageBuffer : ResourceUsage::UniformBuffer;
        desc.memory = MemoryUsage::CpuToGpu;
        desc.initialState = ResourceState::ShaderRead;
        {
            auto partial = f.device->createBuffer(desc);
            assert(partial);
            core::pmr::Vector<std::byte> data(desc.sizeBytes);
            assert(f.device->writeBuffer(*partial, data.data(), data.size() - 1u).success);
            partial->sealWrites();
            assert(!vulkanRhiNativePayload(*partial)->originalHostInitializationMatches(*partial));
        }
        auto buffer = f.device->createBuffer(desc);
        assert(buffer);
        core::pmr::Vector<std::byte> initialized(desc.sizeBytes);
        if (!i) {
            renderer::cloud::CloudGpuConstants constants{};
            constants.inverseViewProjection = core::Mat4::identity();
            constants.cameraRadial = {0.f, 100.f, 0.f, 0.f};
            constants.shell = {1.f, 5.f, 100.f, .01f};
            constants.dimensions = {1u, 1u, 4u, 4u};
            constants.temporal = {0.f, 1.f, 1000.f, 0.f};
            std::memcpy(initialized.data() + offsets[i], &constants, sizeof(constants));
        } else {
            renderer::cloud::CloudGpuSample sample{};
            sample.radianceTransmittance = {.25f, .5f, .75f, .5f};
            sample.metadata.y = 1000.f;
            std::memcpy(initialized.data() + offsets[i], &sample, sizeof(sample));
        }
        const auto native = vulkanRhiNativePayload(*buffer);
        assert(native && !native->originalHostInitializationMatches(*buffer));
        assert(!f.device->writeBuffer(*buffer, initialized.data(), 0u).success);
        assert(!f.device->writeBuffer(*buffer, initialized.data(), 1u, UINT64_MAX).success);
        assert(f.device->writeBuffer(*buffer, initialized.data(), initialized.size() - 1u).success);
        assert(!native->originalHostInitializationMatches(*buffer));
        assert(f.device->writeBuffer(*buffer, initialized.data(), initialized.size()).success);
        assert(!native->originalHostInitializationMatches(*buffer));
        buffer->sealWrites();
        assert(native->originalHostInitializationMatches(*buffer));
        assert(!f.device->writeBuffer(*buffer, initialized.data(), initialized.size()).success);
        prefix[i] = OriginalCommandBufferLease::prepareOriginal(
            buffer, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
        const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(prefix[i]);
        assert(lease && !f.diagnostic);
        auto& element = elements[i];
        element.sourceIndex = i;
        element.registrationGeneration =
            lease->originalObject()->originalResourceRegistration().generation();
        element.write.binding = static_cast<std::uint32_t>(i);
        element.write.type = layout.bindings[i].type;
        element.write.resource = lease->resource();
        element.write.resourceKind = DescriptorResourceKind::Buffer;
        element.write.resourceState = ResourceState::ShaderRead;
        element.write.bufferBindingRange = {2u, offsets[i], sizes[i]};
        element.bufferAccessOffset = offsets[i];
        element.bufferAccessBytes = sizes[i];
        element.stages = PipelineStage::FragmentShader;
        element.access = ResourceAccess::ShaderRead;
    }
    for (std::size_t i = 0u; i < 2u; ++i) {
        TextureDesc td;
        td.extent = {4u, 4u, 1u};
        td.format = i ? TextureFormat::D32Float : TextureFormat::Rgba16Float;
        td.usage = TextureUsage::Sampled | TextureUsage::RenderAttachment;
        const char* contentId = nullptr;
        const auto allocation = f.device->queryOriginalContentTextureAllocation(td, contentId, 4u);
        std::uint64_t footprint = 0u;
        assert(allocation && !contentId
            && !semantics::TextureUploadDestinationValidation::footprint(td, footprint));
        OriginalTextureContentCreationBudget contentBudget{
            4u, true, 1u, 1u, footprint, 4u, bytes, 1u, allocation};
        auto texture =
            f.device->createOriginalContentTexture(td, contentBudget, f.quotas[7], contentId);
        if (!texture)
            std::fprintf(stderr,
                "ordinary cloud texture %zu: %s; registration: %s\n",
                i,
                contentId ? contentId : "missing diagnostic",
                f.device->lastOriginalResourceRegistrationDiagnostic().data());
        assert(texture);
        ordinaryTextureObjects[i] = texture.get();
        TextureViewDesc vd;
        vd.format = td.format;
        vd.usage = TextureUsage::Sampled;
        vd.aspect = i ? TextureAspect::Depth : TextureAspect::Color;
        auto view = f.device->createTextureView(*texture, vd);
        assert(view);
        auto parent = OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
        auto lease = OriginalCommandTextureViewLease::prepareOriginal(
            view, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
        auto sampled = OriginalCommandTextureViewSource::prepareOriginal(
            lease, parent, {1u, true, 5u, bytes}, f.quotas[7], f.diagnostic);
        assert(sampled && !f.diagnostic);
        const auto index = 2u + 2u * i;
        prefix[index] = parent;
        prefix[index + 1u] = sampled;
        vd.usage = TextureUsage::RenderAttachment;
        auto attachment = f.device->createTextureView(*parent->originalObject(), vd);
        auto attachmentLease = OriginalCommandTextureViewLease::prepareOriginal(
            attachment, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
        prefix[6u + i] = OriginalCommandTextureViewSource::prepareOriginal(
            attachmentLease, parent, {1u, true, 5u, bytes}, f.quotas[7], f.diagnostic);
        assert(!f.diagnostic);
        auto& element = elements[i + 2u];
        element.sourceIndex = index + 1u;
        element.registrationGeneration =
            sampled->originalObject()->originalResourceRegistration().generation();
        element.write.binding = static_cast<std::uint32_t>(i + 2u);
        element.write.type = DescriptorType::SampledTexture;
        element.write.resource = sampled->resource();
        element.write.resourceKind = DescriptorResourceKind::TextureView;
        element.write.resourceState = ResourceState::ShaderRead;
        element.stages = PipelineStage::FragmentShader;
        element.access = ResourceAccess::ShaderRead;
    }
    {
        TextureDesc td;
        td.extent = {4u, 4u, 1u};
        td.format = TextureFormat::Rgba16Float;
        td.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
        const char* id = nullptr;
        const auto allocation = f.device->queryOriginalContentTextureAllocation(td, id, 4u);
        std::uint64_t footprint = 0u;
        assert(allocation && !id
            && !semantics::TextureUploadDestinationValidation::footprint(td, footprint));
        auto object = f.device->createOriginalContentTexture(
            td, {4u, true, 1u, 1u, footprint, 4u, bytes, 1u, allocation}, f.quotas[7], id);
        assert(object && !id);
        ordinaryTextureObjects[2u] = object.get();
        auto parent = OriginalCommandTextureLease::prepareOriginal(
            object, {1u, true, 2u, bytes}, f.quotas[7], id);
        TextureViewDesc vd;
        vd.format = td.format;
        vd.usage = TextureUsage::RenderAttachment;
        auto view = f.device->createTextureView(*parent->originalObject(), vd);
        auto lease = OriginalCommandTextureViewLease::prepareOriginal(
            view, {1u, true, 2u, bytes}, f.quotas[7], id);
        prefix[8u] = parent;
        prefix[9u] = OriginalCommandTextureViewSource::prepareOriginal(
            lease, parent, {1u, true, 5u, bytes}, f.quotas[7], id);
        assert(!id);
    }
    OriginalCommandAdmissionBudget budget;
    budget.contractVersion = 1u;
    budget.prototypeOptIn = true;
    budget.maxCommands = 32u;
    budget.maxReferences = 32u;
    budget.maxInputDescriptors = 16u;
    budget.maxStateObservations = budget.maxTextureRanges = 32u;
    budget.maxPassColorAttachments = 2u;
    budget.maxPassAttachmentReferences = 3u;
    budget.maxGraphOwners = Source::cpuOwnerSlots
        + 6u * OriginalCommandBufferLease::retainedOwnerCount
        + 2u * OriginalCommandTextureViewSource::completeRetainedOwnerCount;
    budget.maxCpuBytes.fill(bytes);
    for (const auto domain : {CommandReferenceDomain::Buffer,
             CommandReferenceDomain::Texture,
             CommandReferenceDomain::TextureView})
        budget.maxSourceOwners[static_cast<std::size_t>(domain)] = 2u;
    budget.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Pipeline)] = 1u;
    budget.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)] = 1u;
    budget.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] = 3u;
    budget.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::TextureView)] = 5u;
    budget.maxGraphOwners +=
        OriginalCommandDescriptorSetSource::finiteExclusiveRetainedOwnerCount + 64u;
    auto inventory = Sources::prepareColdOriginalTyped(
        prefix.data(), prefix.size(), budget, {5u, true, 2u}, f.quotas[7], f.diagnostic);
    assert(inventory && !f.diagnostic);
    assert(!Sources::appendOriginalTyped(inventory, source, *f.device));
    const auto pipelineIndex = prefix.size();
    Inspection::Observation output;
    const auto inspect = [&](std::size_t element,
                             const auto& selected,
                             std::size_t position,
                             std::uint32_t set = 0u) noexcept {
        return Inspection::inspect(
            {1u, true}, *f.device, selected, position, set, elements[element], output);
    };
    for (std::size_t i = 0u; i < elements.size(); ++i) {
        const auto* id = inspect(i, inventory, pipelineIndex);
        if (id)
            std::fprintf(stderr, "original graphics binding %zu: %s\n", i, id);
        assert(!id && output.available && !output.nativeExecutionProven());
        assert(output.device == runtime->logicalDevice() && output.pipeline && output.layout);
        assert(output.binding == i && output.set == 0u);
        if (i < 2u) {
            assert(output.buffer.buffer && output.buffer.offset == offsets[i]
                && output.buffer.range == sizes[i]);
            assert(output.type
                == (i ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER));
            assert(!i ? output.staticOccupiedBytes == sizes[0] : output.runtimeElementCount == 2u);
        } else {
            assert(output.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && output.imageView.image
                && output.imageView.format
                    == (i == 2u ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_D32_SFLOAT));
            assert(output.imageView.subresourceRange.aspectMask
                == (i == 2u ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT));
            assert(output.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VkImageView nativeView = VK_NULL_HANDLE;
            assert(
                NativeProbe::CreateImageView(output.device, &output.imageView, nullptr, &nativeView)
                    == VK_SUCCESS
                && nativeView);
            NativeProbe::DestroyImageView(output.device, nativeView, nullptr);
        }
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(f.quotas, [&]() noexcept {
            return inspect(i, inventory, pipelineIndex);
        }));
        const auto original = elements[i];
        const auto refuse = [&] {
            assert(inspect(i, inventory, pipelineIndex));
            assert(!output.available && !output.pipeline && !output.buffer.buffer
                && !output.imageView.image && !output.sampler);
            elements[i] = original;
        };
        ++elements[i].registrationGeneration;
        refuse();
        elements[i].sourceIndex = pipelineIndex;
        refuse();
        elements[i].stages = PipelineStage::ComputeShader;
        refuse();
        elements[i].access = ResourceAccess::ShaderWrite;
        refuse();
        elements[i].write.resourceState = ResourceState::CopySrc;
        refuse();
        elements[i].write.arrayElement = 1u;
        refuse();
        elements[i].dynamicOffset = 1u;
        refuse();
        if (i < 2u) {
            const auto& lease =
                std::get<std::shared_ptr<const OriginalCommandBufferLease>>(prefix[i]);
            elements[i].write.bufferBindingRange = {};
            elements[i].bufferAccessOffset = 0u;
            elements[i].bufferAccessBytes = lease->layout().sizeBytes;
            assert(!inspect(i, inventory, pipelineIndex) && output.buffer.offset == 0u
                && output.buffer.range == lease->layout().sizeBytes);
            elements[i] = original;
            --elements[i].bufferAccessBytes;
            refuse();
            --elements[i].write.bufferBindingRange.sizeBytes;
            --elements[i].bufferAccessBytes;
            refuse();
            elements[i].write.bufferBindingRange.offsetBytes = UINT64_MAX;
            refuse();
            if (offsets[i] > 1u) {
                --elements[i].write.bufferBindingRange.offsetBytes;
                --elements[i].bufferAccessOffset;
                refuse();
            }
        }
    }
    assert(inspect(0u, inventory, 0u) && !output.available);
    assert(inspect(0u, inventory, SIZE_MAX) && !output.available);
    assert(inspect(0u, inventory, pipelineIndex, 1u) && !output.available);
    assert(inspect(0u, std::shared_ptr<const Sources>{}, pipelineIndex) && !output.available);
    Fixture foreign(runtime);
    assert(Inspection::inspect(
               {1u, true}, *foreign.device, inventory, pipelineIndex, 0u, elements[0], output)
        && !output.available);
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> publicationQuotas;
    for (auto& quota : publicationQuotas)
        quota = core::makeShared<core::CpuMemoryQuota>(bytes);
    std::shared_ptr<DescriptorArena> arena;
    core::UniquePtr<DescriptorSet> descriptor;
    {
        core::CpuMemoryQuotaScope scope(publicationQuotas[0]);
        arena =
            core::shareUnique(f.device->createOriginalDescriptorArena({"Cloud.Original", 1u, 4u}));
        assert(arena);
        descriptor = f.device->allocateOriginalDescriptorSet(arena, {layout.name, layout});
        if (!descriptor)
            std::fprintf(stderr,
                "ordinary cloud descriptor allocation: %s\n",
                f.device->lastOriginalResourceRegistrationDiagnostic().data());
        assert(descriptor);
    }
    const std::array writerQuotas{publicationQuotas[1], publicationQuotas[2], publicationQuotas[3]};
    OriginalGeneralDescriptorWriteRequest writeRequest;
    writeRequest.contractVersion = 3u;
    writeRequest.prototypeOptIn = true;
    writeRequest.maxBindings = elements.size();
    writeRequest.maxRetainedOwnerSlots = OriginalGeneralDescriptorWriteContract::ownerSlots(
        elements.size(), writeRequest.contractVersion);
    writeRequest.maxCpuBytes.fill(bytes);
    writeRequest.maxRetainedAllocationBytes = bytes;
    const auto publish = [&] {
        const auto* id = f.device->beginOriginalGeneralDescriptorWrite(*descriptor,
            pipeline,
            inventory,
            0u,
            elements.data(),
            elements.size(),
            writeRequest,
            writerQuotas);
        return id;
    };
    const auto revision = descriptor->contentRevision();
    auto validElement = elements.back();
    elements.back().write.binding = elements.front().write.binding;
    assert(publish() && descriptor->contentRevision() == revision
        && !descriptor->stats().writtenBindings);
    elements.back() = validElement;
    const auto viewsBefore = NativeProbe::liveViews;
    const auto updatesBefore = NativeProbe::descriptorUpdates;
    const auto refused = [&] {
        assert(publish() && descriptor->contentRevision() == revision
            && !descriptor->stats().writtenBindings && NativeProbe::liveViews == viewsBefore
            && NativeProbe::descriptorUpdates == updatesBefore);
        for (const auto& quota : writerQuotas)
            assert(!quota->liveBytes());
    };
    for (const auto* name : {"vkUpdateDescriptorSets", "vkCreateImageView", "vkDestroyImageView"}) {
        NativeProbe::missing = name;
        refused();
        NativeProbe::missing = nullptr;
    }
    for (unsigned prefix = 1u; prefix <= 2u; ++prefix) {
        NativeProbe::viewAttempts = 0u;
        NativeProbe::viewFailAt = prefix;
        refused();
        assert(NativeProbe::viewAttempts == prefix);
    }
    NativeProbe::viewFailAt = 0u;
    for (const auto& quota : writerQuotas) {
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
        assert(pressure.charged());
        assert(publish() && !descriptor->stats().writtenBindings
            && NativeProbe::liveViews == viewsBefore);
    }
    if (const auto* id = publish()) {
        std::fprintf(stderr, "ordinary cloud descriptor publication: %s\n", id);
        assert(false);
    }
    assert(descriptor->originalScopedContentDiagnostic());
    assert(descriptor->contentRevision() == revision && !descriptor->stats().writtenBindings);
    std::array<std::uint64_t, 3u> exactCpu;
    for (std::size_t i = 0u; i < writerQuotas.size(); ++i)
        exactCpu[i] = writerQuotas[i]->liveBytes();
    assert(!descriptor->cancelOriginalGeneralWrite());
    for (const auto& quota : writerQuotas)
        assert(!quota->liveBytes());
    for (std::size_t i = 0u; i < writerQuotas.size(); ++i) {
        assert(exactCpu[i]);
        for (const auto available : {exactCpu[i], exactCpu[i] - 1u}) {
            core::CpuMemoryQuotaLease pressure(writerQuotas[i], bytes - available);
            assert(pressure.charged());
            const auto* id = publish();
            assert(bool(id) == (available != exactCpu[i]));
            if (!id)
                assert(!descriptor->cancelOriginalGeneralWrite());
            assert(NativeProbe::liveViews == viewsBefore
                && NativeProbe::descriptorUpdates == updatesBefore);
        }
        assert(!writerQuotas[i]->liveBytes());
    }
    assert(!publish());
    writerQuotas[1]->setLimitBytes(bytes - 1u);
    assert(descriptor->finishOriginalGeneralWrite() && descriptor->contentRevision() == revision
        && !descriptor->stats().writtenBindings && NativeProbe::descriptorUpdates == updatesBefore);
    writerQuotas[1]->setLimitBytes(bytes);
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(writerQuotas, [&]() noexcept {
        return descriptor->finishOriginalGeneralWrite();
    }));
    assert(descriptor->contentRevision() == revision + 1u
        && descriptor->originalGeneralContentContractVersion() == 3u
        && descriptor->stats().writtenBindings == elements.size());
    assert(!descriptor->originalScopedContentDiagnostic());
    assert(NativeProbe::descriptorUpdates == updatesBefore + 1u
        && NativeProbe::liveViews == viewsBefore + 2u && !vulkanRhiNativePayload(*descriptor));
    assert(!descriptor->write(elements[0].write).success);
    assert(descriptor->finishOriginalGeneralWrite());
    OriginalCommandDescriptorSourceBudget sourceBudget{5u,
        true,
        4u,
        4u,
        OriginalCommandDescriptorSetSource::finiteExclusiveRetainedOwnerCount,
        bytes};
    sourceBudget.finitePipelineSourceIndex = pipelineIndex;
    sourceBudget.finiteFootprintContractVersion = 2u;
    sourceBudget.finiteFullBoundViewOptIn = true;
    auto descriptorSource = OriginalCommandDescriptorSetSource::prepareOriginal(descriptor,
        arena,
        inventory,
        elements.data(),
        elements.size(),
        sourceBudget,
        f.quotas[7],
        *f.device,
        f.diagnostic);
    if (f.diagnostic)
        std::fprintf(stderr, "ordinary cloud descriptor Source5: %s\n", f.diagnostic);
    assert(descriptorSource && !f.diagnostic && !descriptor);
    assert(!Sources::appendOriginalTyped(inventory, descriptorSource, *f.device));
    assert(!OriginalCommandDescriptorSetSource::validateOriginal(descriptorSource, *f.device));
    assert(vulkanOriginalGraphicsDescriptorPayload(descriptorSource));
    for (unsigned mode = 0u; mode < 4u; ++mode)
        verifyOrdinaryCloudDraw(
            f, runtime, inventory, ordinaryTextureObjects, pipelineIndex, pipelineIndex + 1u, mode);
    descriptorSource.reset();
    arena.reset();
    const auto live = f.quotas[7]->liveBytes();
    auto forged = std::shared_ptr<const Sources>(f.quotas[6], inventory.get());
    assert(inspect(0u, forged, pipelineIndex) && !output.available);
    forged.reset();
    f.input.reset();
    f.layout.reset();
    pipeline.reset();
    receipt.reset();
    source.reset();
    for (auto& owner : prefix)
        owner = std::shared_ptr<const OriginalCommandBufferLease>{};
    assert(!inspect(0u, inventory, pipelineIndex) && f.quotas[7]->liveBytes() == live);
    f.device.reset();
    // No dangling device reference is used: the original Sources can diagnose
    // lifetime retirement independently of any native handle observation.
    assert(Sources::validateOriginal(inventory));
    inventory.reset();
    std::puts(
        "Original Graphics binding inspection and atomic publication PASS four cloud bindings, binary extents, finite offsets, native view-parent identity, hot0, negatives, caller-drop; Source5 published, recording closed");
}

void verifyLossPrefixes()
{
    // Six calls in this exact fixture: one descriptor layout, one pipeline
    // layout, two shader modules, one render pass and one graphics pipeline.
    for (unsigned scenario = 0u; scenario < 12u; ++scenario) {
        const bool joinedLoss = scenario >= 6u;
        const unsigned prefix = scenario % 6u + 1u;
        tests::VulkanHeadlessRuntime bootstrap(false, NativeProbe::resolve);
        assert(bootstrap.runtime() && bootstrap.runtime()->valid());
        Fixture f(bootstrap.runtime(), joinedLoss);
        const auto lifetime = f.device->originalDeviceLifetimeTicket();
        const auto layout = f.layout->originalResourceRegistration();
        assert(!lifetime.diagnostic() && !layout.diagnostic());
        NativeProbe::ordinal = 0u;
        NativeProbe::failAt = prefix;
        NativeProbe::failure = VK_ERROR_DEVICE_LOST;
        assert(!f.create() && f.diagnostic && bootstrap.runtime()->coreQueueCapabilityDiagnostic()
            && NativeProbe::ordinal == prefix);
        assert(!bootstrap.runtime()->shaderDrawParametersEnabled());
        if (joinedLoss)
            assert(lifetime.diagnostic() && layout.diagnostic());
        assert((NativeProbe::live == std::array<unsigned, 5u>{}));
        NativeProbe::failAt = 0u;
        NativeProbe::ordinal = 0u;
        NativeProbe::failure = VK_ERROR_OUT_OF_HOST_MEMORY;
        assert(!f.create() && f.diagnostic && !NativeProbe::ordinal);
    }
    std::puts(
        "Original graphics injected device-loss prefixes PASS unjoined and registry10 joined; native admission closes, joined lifetime and registrations revoked, all created objects reclaimed");
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL withoutFeatureQuery(VkInstance instance, const char* name)
{
    return !std::strcmp(name, "vkGetPhysicalDeviceFeatures2")
        ? nullptr
        : vkGetInstanceProcAddr(instance, name);
}

void verifyComputeContract()
{
    OriginalGeneralDescriptorWriteRequest write;
    write.contractVersion = 4u;
    write.prototypeOptIn = true;
    write.maxBindings = 4u;
    write.maxRetainedOwnerSlots =
        OriginalGeneralDescriptorWriteContract::ownerSlots(4u, write.contractVersion);
    write.maxCpuBytes.fill(bytes);
    write.maxRetainedAllocationBytes = bytes;
    assert(!OriginalGeneralDescriptorWriteContract::validate(write));
    write.maxCallbacks = 1u;
    assert(OriginalGeneralDescriptorWriteContract::validate(write));
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto d = OriginalGeneralDescriptorWriteContract::synchronousComputeLowering(backend);
        assert(d.preparationAvailable == (backend == Backend::Vulkan || backend == Backend::Metal)
            && !d.executionAvailable && d.validatorOnly == (backend == Backend::Null)
            && bool(d.diagnostic) != d.preparationAvailable);
        const auto inspection =
            OriginalGraphicsBindingInspectionContract::lowering(backend, {2u, true});
        assert(inspection.inspectionAvailable == (backend == Backend::Vulkan)
            && !inspection.executionAvailable);
    }
    assert(OriginalCommandResourceSources::allowsDescriptorSourceVersion(8u, 7u));
    assert(!OriginalCommandResourceSources::allowsDescriptorSourceVersion(5u, 7u));
    DescriptorBindingDesc binding;
    binding.type = DescriptorType::StorageBuffer;
    binding.stages = ShaderStage::Compute;
    OriginalCommandDescriptorSetElement element;
    element.write.type = binding.type;
    element.write.resourceKind = DescriptorResourceKind::Buffer;
    element.write.resource = {1u};
    assert(!OriginalGraphicsBindingInspectionContract::validateShape(binding, element, 2u));
    assert(OriginalGraphicsBindingInspectionContract::validateShape(binding, element, 1u));
    binding.stages = ShaderStage::Fragment;
    assert(OriginalGraphicsBindingInspectionContract::validateShape(binding, element, 2u));
    using C = OriginalSynchronousComputeCreationContract;
    const C::Budget budget{1u, true, bytes, 256u, bytes, bytes};
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto d = C::lowering(backend, budget, {true, true});
        assert(d.creationEligible == (backend == Backend::Vulkan || backend == Backend::Metal)
            && !d.executionAvailable && d.validatorOnly == (backend == Backend::Null)
            && bool(d.diagnostic) != d.creationEligible);
        const auto accepted =
            OriginalAcceptedComputePipelineFactoryContract::lowering(backend, true, 2u);
        const auto source =
            semantics::OriginalCommandPipelineSourceLowering::lowering(backend, 2u, false);
        assert(accepted.factoryEligible == (backend == Backend::Vulkan || backend == Backend::Metal)
            && !accepted.executionAvailable);
        assert(source.custodyAvailable == accepted.factoryEligible
            && !source.positionalAdapterAvailable && !source.executionAvailable);
        assert(!OriginalAcceptedComputePipelineFactoryContract::lowering(backend, false, 2u)
                .factoryEligible);
    }
    assert(!C::lowering(Backend::Vulkan, budget, {false, true}).creationEligible);
    assert(!C::lowering(Backend::Vulkan, budget, {true, false}).creationEligible);
    for (unsigned field = 0u; field < 6u; ++field) {
        auto bad = budget;
        if (!field)
            bad.contractVersion = 0u;
        if (field == 1u)
            bad.prototypeOptIn = false;
        if (field == 2u)
            bad.maxTargetBytes = 0u;
        if (field == 3u)
            bad.maxLabelBytes = 0u;
        if (field == 4u)
            bad.maxCpuOwnerBytes = 0u;
        if (field == 5u)
            bad.maxCpuNativeBytes = 0u;
        assert(C::validateBudget(bad));
    }
    auto null = createNullDevice();
    const char* id = nullptr;
    assert(!null->createOriginalSynchronousComputeCreation({}, {}, {}, budget, {}, {}, id) && id);
    std::puts(
        "ORIGINAL_CLOUD_COMPUTE_CONTRACT_OK synchronous1 accepted2 Source2 explicit-backends execution-closed Null-validator");
}

class ComputeFixture final {
public:
    using C = OriginalSynchronousComputeCreationContract;
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 10u> quotas;
    core::UniquePtr<GpuDevice> device;
    GpuDevice* selectedDevice = nullptr;
    std::shared_ptr<const OriginalSingleEntryPipelineInput> input;
    std::shared_ptr<PipelineLayout> layout;
    ComputePipelineDesc authored;
    C::Budget budget{1u, true, 0u, 256u, bytes, bytes};
    const char* diagnostic = nullptr;

    ComputeFixture(const std::shared_ptr<VulkanRhiRuntime>& runtime,
        const char* publication,
        bool bindingResources = false,
        GpuDevice* externalDevice = nullptr)
    {
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(bytes);
        core::CpuMemoryQuotaScope scope(quotas[9]);
        if (!externalDevice) {
            VulkanRhiDeviceDesc desc;
            desc.runtime = runtime;
            desc.capabilityContract.backend = Backend::Vulkan;
            desc.memoryBudgetBytes = 1024u * 1024u;
            // The layout and pipeline each hold one direct-object lease.
            desc.directObjectBudget = {
                1u, "OriginalCloudComputeCreation/1", 2u, bindingResources ? 4u : 1u};
            device = createVulkanRhiDevice(desc);
            assert(device
                && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quotas[0]));
            assert(!device->prepareOriginalDeviceNativeResultLoss(
                {1u, true, 1u, 2u, bytes}, quotas[8]));
            OriginalResourceRegistrationBudget registration;
            registration.contractVersion = 10u;
            registration.prototypeOptIn = true;
            registration.maxPipelineLayouts = registration.maxComputePipelines = 1u;
            registration.maxRetainedOwners = 11u;
            if (bindingResources) {
                registration.maxBuffers = 4u;
                registration.maxTextures = registration.maxTextureViews = 1u;
                registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
                registration.maxRetainedOwners +=
                    7u * OriginalResourceRegistrationContract::perResourceOwnerCount
                    + OriginalResourceRegistrationContract::perDescriptorOwnerCount;
            }
            registration.maxCpuMetadataBytes = bytes;
            const auto loss = device->observeOriginalDeviceNativeLoss(diagnostic);
            assert(loss && !diagnostic
                && !device->prepareOriginalTypedNativeLossRegistration(
                    {3u, true}, registration, quotas[1], *loss));
        }
        selectedDevice = externalDevice ? externalDevice : device.get();
        core::pmr::String text;
        const auto artifact = shader::SingleEntryShaderArtifact::load(
            publication, {bytes, 8u * bytes, bytes, 7u}, quotas[2], text);
        if (!artifact)
            std::fprintf(stderr, "original compute artifact: %s\n", text.c_str());
        assert(artifact);
        input = OriginalSingleEntryPipelineInput::prepare(artifact, {1u, bytes}, quotas[3], text);
        assert(input);
        const auto selected =
            OriginalSingleEntryPipelineInput::selectTarget(input, Backend::Vulkan);
        assert(selected.bytes && !selected.diagnostic);
        budget.maxTargetBytes = selected.bytes->size();
        DescriptorLayoutDesc bindings;
        bindings.name = "Cloud.OriginalCompute";
        for (const auto& reflected : input->package().reflection.descriptorBindings) {
            DescriptorBindingDesc binding;
            binding.binding = reflected.binding;
            binding.type = reflected.type;
            binding.stages = reflected.stages;
            binding.arrayCount = reflected.arrayCount;
            binding.depthTexture = reflected.depthTexture;
            if (binding.type == DescriptorType::SampledTexture) {
                binding.textureViewDimensionConstrained = true;
                binding.textureViewDimension = TextureViewDimension::Texture2D;
                binding.sampledTextureFilterable = false;
            }
            bindings.bindings.push_back(binding);
        }
        PipelineLayoutDesc ld;
        ld.descriptorLayouts.push_back(std::move(bindings));
        ld.descriptorSets.push_back({0u, "Cloud.OriginalCompute"});
        layout = core::shareUnique(selectedDevice->createOriginalPipelineLayout(ld));
        assert(layout);
        authored.label.assign(128u, 'c');
        authored.layout = layout.get();
        authored.shaderPackage = &input->package();
        authored.computeShader = input->package().entrypoints.front();
    }

    auto create()
    {
        return selectedDevice->createOriginalSynchronousComputeCreation(
            input, layout, authored, budget, quotas[4], quotas[5], diagnostic);
    }
};

#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
// The oracle's open half-planes are evaluated at pixel centers. At 32x24
// no center lies on the triangle edges, so no API fill-rule tolerance is needed.
bool foregroundPixel(unsigned x, unsigned y, unsigned width, unsigned height)
{
    return tests::cloud::ForegroundFixture{true, 0, 0, float(width), float(height)}.covers(x, y);
}

constexpr auto foregroundColor = tests::cloud::ForegroundFixture::color;

struct ForegroundGraphSources {
    std::size_t pipeline = SIZE_MAX, descriptors = SIZE_MAX;
    ViewportState viewport;
};

std::shared_ptr<RenderPipeline> prepareForegroundPipeline(
    Fixture& f, OriginalPreparedCommandDeviceExtension& extension)
{
    core::pmr::String text;
    const auto artifact = shader::GraphicsShaderModuleArtifact::load(ARP_CLOUD_FOREGROUND_MODULE,
        {bytes, 8u * bytes, bytes, 4u, 4u, 256u, 2u},
        f.quotas[2],
        text);
    if (!artifact)
        std::fprintf(stderr, "foreground artifact: %s\n", text.c_str());
    assert(artifact);
    const auto input =
        OriginalGraphicsPipelineInput::prepare(artifact, {1u, bytes}, f.quotas[3], text);
    assert(input);
    const auto target = OriginalGraphicsPipelineInput::selectTarget(input, Backend::Vulkan);
    assert(!target.diagnostic);
    DescriptorLayoutDesc bindings;
    bindings.name = "Cloud.Foreground";
    bindings.bindings.push_back({0u, DescriptorType::UniformBuffer, ShaderStage::Fragment});
    PipelineLayoutDesc ld;
    ld.descriptorLayouts.push_back(std::move(bindings));
    ld.descriptorSets.push_back({0u, "Cloud.Foreground"});
    auto layout = core::shareUnique(f.device->createOriginalPipelineLayout(ld));
    assert(layout);
    RenderPipelineDesc authored;
    authored.label = "Cloud.Foreground";
    authored.layout = layout.get();
    authored.shaderPackage = &input->package();
    authored.vertexShader = input->package().entrypoints[0];
    authored.fragmentShader = input->package().entrypoints[1];
    authored.colorFormat = TextureFormat::Rgba16Float;
    authored.colorTargets = {{TextureFormat::Rgba16Float}};
    authored.cullMode = CullMode::None;
    authored.depthFormat = TextureFormat::D32Float;
    authored.depthStencil.depthTestEnabled = authored.depthStencil.depthWriteEnabled = true;
    authored.depthStencil.depthCompare = CompareOp::Less;
    authored.dynamicViewportScissor = true;
    const Contract::Budget budget{1u,
        true,
        target.sources[0]->size() + target.sources[1]->size(),
        authored.label.size(),
        bytes,
        bytes};
    const auto creation = extension.createGraphics(
        input, layout, authored, budget, f.quotas[4], f.quotas[5], f.diagnostic);
    if (f.diagnostic)
        std::fprintf(stderr, "foreground creation: %s\n", f.diagnostic);
    assert(creation && !f.diagnostic);
    auto result = OriginalRegisteredPipeline::shareAcceptedOriginal(
        f.device->createOriginalArtifactGraphicsPipeline(creation, {2u, true, bytes}, f.quotas[6]),
        f.diagnostic);
    assert(result && !f.diagnostic);
    return result;
}

void verifyCloudSequenceNumeric(const tests::cloud::SequenceOracle& oracle,
    const std::array<GpuBuffer*, 16u>& buffers,
    const MixedCloudProgramData& mixed,
    unsigned frame,
    tests::cloud::QualityOracle* quality)
{
    using namespace renderer::cloud;
    core::pmr::String id;
    float worstSample = 0.f, worstHdr = 0.f, historyDifference = 0.f;
    const CloudGpuSample *actualRaw = nullptr, *actualResolved = nullptr;
    for (unsigned pass = 0; pass < 2; ++pass) {
        const auto& expected = pass ? oracle.resolved : oracle.raw;
        const auto* object = pass ? mixed.resolved : mixed.raw;
        const auto native = vulkanRhiNativePayload(*object);
        assert(native->backendInvalidateMappedRead(0, object->desc().sizeBytes, id));
        const auto* actual = reinterpret_cast<const CloudGpuSample*>(
            static_cast<const std::byte*>(native->backendMappedData()) + mixed.sampleOffsets[pass]);
        if (pass)
            actualResolved = actual;
        else
            actualRaw = actual;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const auto a =
                tests::cloud::SequenceOracle::components(actual[i].radianceTransmittance);
            const auto e =
                tests::cloud::SequenceOracle::components(expected[i].radianceTransmittance);
            const auto raw = tests::cloud::SequenceOracle::components(
                (pass ? oracle.current[i] : oracle.raw[i]).radianceTransmittance);
            assert(std::fabs(actual[i].metadata.y - expected[i].metadata.y) < .002f);
            assert(std::isfinite(actual[i].metadata.x));
            for (unsigned c = 0; c < 4; ++c) {
                assert(std::isfinite(a[c]));
                worstSample = std::max(worstSample, std::fabs(a[c] - e[c]));
                if (pass)
                    historyDifference = std::max(historyDifference, std::fabs(e[c] - raw[c]));
            }
        }
    }
    const auto native = vulkanRhiNativePayload(*buffers[13]);
    assert(native->backendInvalidateMappedRead(0, buffers[13]->desc().sizeBytes, id));
    const auto* actual = static_cast<const std::byte*>(native->backendMappedData());
    for (unsigned y = 0; y < oracle.plan.extent.height; ++y) {
        for (unsigned x = 0; x < oracle.plan.extent.width; ++x) {
            std::array<std::uint16_t, 4> pixel;
            std::memcpy(
                pixel.data(), actual + y * mixed.rowBytes + x * sizeof(pixel), sizeof(pixel));
            const auto expected = oracle.hdr(x, y);
            for (unsigned c = 0; c < 4; ++c)
                worstHdr = std::max(worstHdr, std::fabs(core::halfToFloat(pixel[c]) - expected[c]));
        }
        for (auto b = oracle.plan.extent.width * 8u; b < mixed.rowBytes; ++b)
            assert(actual[y * mixed.rowBytes + b] == std::byte{});
    }
    std::printf(
        "ORDINARY_HISTORY frame=%u history=%u accepted=%u difference=%.9g sampleError=%.9g hdrError=%.9g extent=%ux%u samples=%ux%u\n",
        frame,
        unsigned(oracle.parameters.previousCamera.w > 0),
        oracle.accepted,
        double(historyDifference),
        double(worstSample),
        double(worstHdr),
        oracle.plan.extent.width,
        oracle.plan.extent.height,
        oracle.plan.sampleExtent.width,
        oracle.plan.sampleExtent.height);
    std::fflush(stdout);
    assert(worstSample < .002f && worstHdr < .002f);
    if (oracle.parameters.previousCamera.w > 0)
        assert(oracle.accepted > 0 && historyDifference > 1.e-6f);
    if (oracle.foreground.enabled && oracle.parameters.previousCamera.w > 0
        && oracle.requireOcclusionChange && !oracle.fullResolved) {
        assert(oracle.opaqueRejected > 0 && oracle.disoccluded > 0 && oracle.newlyOccluded > 0);
        std::printf(
            "ORDINARY_MOVING_OCCLUSION frame=%u orthographic=%u opaqueRejected=%u disoccluded=%u newlyOccluded=%u\n",
            frame,
            unsigned(oracle.parameters.jitter.w > 0),
            oracle.opaqueRejected,
            oracle.disoccluded,
            oracle.newlyOccluded);
    }
    if (quality) {
        quality->evaluate(oracle, actualRaw, actualResolved, actual, mixed.rowBytes);
        if (oracle.foreground.enabled && oracle.parameters.cameraTime.y > oracle.layer.baseAltitude
            && oracle.parameters.cameraTime.y < oracle.layer.baseAltitude + oracle.layer.thickness)
            assert(quality->coveredCloudPixels > 0);
        if (oracle.fullResolved) {
            assert(oracle.eligible && oracle.refined && oracle.refined <= oracle.eligible);
            if (oracle.parameters.previousCamera.w > 0 && oracle.requireOcclusionChange)
                assert(quality->exposedPixels > 0);
            std::printf("ORDINARY_RECONSTRUCTION frame=%u eligible=%u refined=%u resolved=%zu\n",
                frame,
                oracle.eligible,
                oracle.refined,
                oracle.resolved.size());
        }
        std::printf(
            "ORDINARY_QUALITY frame=%u passed=%u Lrms=%.9g Trms=%.9g hdrRms=%.9g rawLrms=%.9g rawTrms=%.9g exposed=%u exposedHdrRms=%.9g convergenceRms=%.9g convergenceMax=%.9g\n",
            frame,
            unsigned(quality->passed()),
            quality->radiance.rms(),
            quality->transmittance.rms(),
            quality->hdr.rms(),
            quality->rawRadiance.rms(),
            quality->rawTransmittance.rms(),
            quality->exposedPixels,
            quality->exposedHdr.rms(),
            quality->convergence.rms(),
            quality->convergence.maximum);
        std::fflush(stdout);
    }
    if (const auto* base = std::getenv("ARP_CLOUD_ORACLE_PATH")) {
        const auto path = core::pmr::String(base) + ".frame-"
            + core::pmr::String(std::to_string(frame).c_str()) + ".json";
        auto* output = std::fopen(path.c_str(), "wb");
        assert(output);
        std::fprintf(output,
            "{\"width\":%u,\"height\":%u,\"sampleWidth\":%u,\"sampleHeight\":%u,\"rowBytes\":%llu,\"tolerance\":0.002,\"frame\":%u,\"historyUsed\":%s,\"historyAccepted\":%u",
            oracle.plan.extent.width,
            oracle.plan.extent.height,
            oracle.plan.sampleExtent.width,
            oracle.plan.sampleExtent.height,
            static_cast<unsigned long long>(mixed.rowBytes),
            frame,
            oracle.parameters.previousCamera.w > 0 ? "true" : "false",
            oracle.accepted);
        std::fprintf(output,
            ",\"resolvedWidth\":%u,\"resolvedHeight\":%u,\"reconstructionVersion\":%u,\"reconstructionRanges\":[%.9g,%.9g],\"refinementEligible\":%u,\"refinedPixels\":%u",
            oracle.fullResolved ? oracle.plan.extent.width : oracle.plan.sampleExtent.width,
            oracle.fullResolved ? oracle.plan.extent.height : oracle.plan.sampleExtent.height,
            oracle.reconstruction.contractVersion,
            double(oracle.reconstruction.maxRadianceRange),
            double(oracle.reconstruction.maxTransmittanceRange),
            oracle.eligible,
            oracle.refined);
        for (unsigned array = 0; array < 4; ++array) {
            const auto& rows = array == 0 ? oracle.raw
                : array == 1              ? oracle.resolved
                : array == 2              ? oracle.previous
                                          : oracle.current;
            std::fprintf(output,
                ",\"%s\":[",
                array == 0       ? "rawExpected"
                    : array == 1 ? "resolvedExpected"
                    : array == 2 ? "previousExpected"
                                 : "currentExpected");
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const auto& v = rows[i].radianceTransmittance;
                std::fprintf(output,
                    "%s[%.9g,%.9g,%.9g,%.9g]",
                    i ? "," : "",
                    double(v.x),
                    double(v.y),
                    double(v.z),
                    double(v.w));
            }
            std::fprintf(output, "]");
        }
        std::fprintf(output,
            ",\"foreground\":%s,\"orthographic\":%s,\"opaqueRejected\":%u,\"disoccluded\":%u,\"newlyOccluded\":%u,\"foregroundViewport\":[%.9g,%.9g,%.9g,%.9g],\"foregroundMask\":[",
            oracle.foreground.enabled ? "true" : "false",
            oracle.parameters.jitter.w > 0 ? "true" : "false",
            oracle.opaqueRejected,
            oracle.disoccluded,
            oracle.newlyOccluded,
            double(oracle.foreground.x),
            double(oracle.foreground.y),
            double(oracle.foreground.width),
            double(oracle.foreground.height));
        for (unsigned y = 0; y < oracle.plan.extent.height; ++y)
            for (unsigned x = 0; x < oracle.plan.extent.width; ++x)
                std::fprintf(
                    output, "%s%u", x || y ? "," : "", unsigned(oracle.foreground.covers(x, y)));
        std::fprintf(output, "],\"opaqueDistances\":[");
        for (std::size_t i = 0; i < oracle.raw.size(); ++i)
            std::fprintf(output, "%s%.9g", i ? "," : "", double(oracle.raw[i].metadata.y));
        std::fprintf(output, "],\"resolvedOpaqueDistances\":[");
        for (std::size_t i = 0; i < oracle.resolved.size(); ++i)
            std::fprintf(output, "%s%.9g", i ? "," : "", double(oracle.resolved[i].metadata.y));
        std::fprintf(output, "],\"expectedHdr\":[");
        for (unsigned y = 0; y < oracle.plan.extent.height; ++y)
            for (unsigned x = 0; x < oracle.plan.extent.width; ++x) {
                const auto v = oracle.hdr(x, y);
                std::fprintf(output,
                    "%s[%.9g,%.9g,%.9g,%.9g]",
                    x || y ? "," : "",
                    double(v[0]),
                    double(v[1]),
                    double(v[2]),
                    double(v[3]));
            }
        std::fprintf(output,
            "],\"requireOcclusionChange\":%s,\"viewSteps\":%u,\"lightSteps\":%u",
            oracle.requireOcclusionChange ? "true" : "false",
            oracle.parameters.sampling[0],
            oracle.parameters.sampling[1]);
        if (quality)
            quality->write(output, oracle);
        std::fprintf(output, "}\n");
        std::fclose(output);
    }
    assert(!quality || quality->passed());
}

void verifyPreparedCloudGraph(Fixture& f,
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const std::array<GpuTexture*, 3u>& textures,
    const std::array<GpuBuffer*, 16u>& buffers,
    const MixedCloudProgramData& mixed,
    std::size_t graphics,
    std::size_t graphicsSet,
    const renderer::cloud::CloudGpuConstants& parameters,
    const assets::cloud::CloudAsset* asset,
    const ForegroundGraphSources& foreground,
    tests::cloud::SequenceOracle* sequence = nullptr,
    renderer::cloud::CloudPreparedHistory* history = nullptr,
    unsigned sequenceFrame = 0u,
    bool exerciseReaderGate = false,
    tests::cloud::QualityOracle* quality = nullptr,
    const char* densityDiagnostic = nullptr,
    renderer::cloud::VolumetricCloudFeature* feature = nullptr,
    const renderer::cloud::CloudPreparedResourceCreateContext* featureResources = nullptr)
{
    using namespace render_graph;
    using namespace renderer::cloud;
    const auto require = [](const char* id) {
        if (id)
            std::fprintf(stderr, "prepared cloud graph: %s\n", id);
        assert(!id);
    };
    const auto intentQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
    const auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
    const auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
    FrameGraph graph(*f.device);
    OriginalFrameGraphExecution execution(graph, inventory, intentQuota);
    std::array<TextureHandle, 3u> th;
    std::array<BufferHandle, 16u> bh{};
    for (std::size_t i = 0u; i < th.size(); ++i) {
        const auto source = i == 2u ? 8u : 2u + 2u * i;
        const auto& lease = std::get<std::shared_ptr<const OriginalCommandTextureLease>>(
            inventory->sources()[source]);
        th[i] = graph.importTextureRef({lease, textures[i]});
        require(execution.bindAttachment(th[i], i == 2u ? 9u : 6u + i));
    }
    for (const auto i : {0u, 1u, 10u, 11u, 12u, 13u}) {
        const auto& lease =
            std::get<std::shared_ptr<const OriginalCommandBufferLease>>(inventory->sources()[i]);
        bh[i] = graph.importBufferRef({lease, buffers[i]});
    }
    const bool hasForeground = foreground.pipeline != SIZE_MAX;
    const bool spatial = sequence && sequence->fullResolved;
    if (spatial) {
        const auto& lease =
            std::get<std::shared_ptr<const OriginalCommandBufferLease>>(inventory->sources()[15u]);
        bh[15u] = graph.importBufferRef({lease, buffers[15u]});
    }
    if (hasForeground) {
        const auto& lease =
            std::get<std::shared_ptr<const OriginalCommandBufferLease>>(inventory->sources()[14u]);
        bh[14u] = graph.importBufferRef({lease, buffers[14u]});
    }
    auto producer = hasForeground
        ? execution.graphics("Cloud.SceneProducer",
              foreground.pipeline,
              foreground.descriptors,
              foreground.viewport,
              {0u, 0u, textures[0]->desc().extent.width, textures[0]->desc().extent.height},
              {3u, 1u, 0u, 0u})
        : execution.clear("Cloud.SceneProducer");
    producer
        .writeAttachment(th[0],
            ResourceState::ColorAttachment,
            LoadOp::Clear,
            StoreOp::Store,
            {},
            {.125f, .375f, .75f, .625f})
        .writeAttachment(th[1], ResourceState::DepthAttachment, LoadOp::Clear, StoreOp::Store);
    if (hasForeground)
        producer.read(bh[14u], ResourceState::ShaderRead);
    CloudExecutionPlan plan;
    plan.extent = {parameters.dimensions[2], parameters.dimensions[3]};
    plan.sampleExtent = {parameters.dimensions[0], parameters.dimensions[1]};
    plan.sampleCount = std::uint64_t(plan.sampleExtent.width) * plan.sampleExtent.height;
    plan.sampleBufferBytes = plan.sampleCount * sizeof(CloudGpuSample);
    plan.dispatchGroups = {1u + (plan.sampleExtent.width - 1u) / kCloudWorkgroupWidth,
        1u + (plan.sampleExtent.height - 1u) / kCloudWorkgroupHeight,
        1u};
    if (sequence)
        plan = sequence->plan;
    CloudPreparedGraphContext context{1u,
        true,
        execution,
        plan,
        th[0],
        th[1],
        th[2],
        bh[0],
        bh[10],
        bh[11],
        history && !spatial && parameters.previousCamera.w <= 0 ? bh[11] : bh[12],
        bh[1],
        {mixed.pipelines[0], mixed.pipelines[1], graphics},
        {mixed.sets[0], mixed.sets[1], graphicsSet}};
    if (history) {
        context.contractVersion = spatial ? 3u : 2u;
        context.history = history;
        context.historyGeneration = history->token().generation;
        if (spatial) {
            context.reconstruction = &history->reconstructionPlan();
            context.reconstructionConstants = bh[15u];
        }
    }
    CloudOutputs output;
    auto invalid = context;
    invalid.descriptors[0] = SIZE_MAX;
    assert(!VolumetricCloudFeature::contributePreparedGraph(invalid, output).success);
    if (history) {
        auto stale = context;
        ++stale.historyGeneration;
        assert(!VolumetricCloudFeature::contributePreparedGraph(stale, output).success);
        auto foreign = context;
        foreign.constants = bh[10u];
        assert(!VolumetricCloudFeature::contributePreparedGraph(foreign, output).success);
        auto version = context;
        version.contractVersion = 1u;
        assert(!VolumetricCloudFeature::contributePreparedGraph(version, output).success);
        if (spatial) {
            auto wrongPolicy = context;
            wrongPolicy.reconstructionConstants = bh[0u];
            assert(!VolumetricCloudFeature::contributePreparedGraph(wrongPolicy, output).success);
            auto shape = history->reconstructionPlan();
            --shape.resolvedBufferBytes;
            auto wrongShape = context;
            wrongShape.reconstruction = &shape;
            assert(!VolumetricCloudFeature::contributePreparedGraph(wrongShape, output).success);
        }
    }
    CloudResult contribution;
    if (feature) {
        CloudPreparedFeatureGraphContext owned{execution};
        owned.contractVersion = 1u;
        owned.prototypeOptIn = true;
        owned.frameToken = feature->preparedToken();
        owned.color = th[0];
        owned.depth = th[1];
        owned.output = th[2];
        owned.pipelines = context.pipelines;
        owned.descriptors = context.descriptors;
        auto stale = owned;
        ++stale.frameToken.generation;
        assert(!feature->contributeGraph(stale, output).success);
        auto foreign = owned;
        foreign.pipelines[0] = foreground.pipeline;
        assert(!feature->contributeGraph(foreign, output).success);
        contribution = feature->contributeGraph(owned, output);
        if (contribution.success) {
            CloudOutputs duplicate;
            assert(!feature->contributeGraph(owned, duplicate).success && !duplicate.color.id);
        }
    } else
        contribution = VolumetricCloudFeature::contributePreparedGraph(context, output);
    if (densityDiagnostic) {
        assert(!contribution.success && contribution.diagnostic);
        assert(std::string_view(contribution.diagnostic) == densityDiagnostic);
        assert(!execution.program());
        std::printf("ORDINARY_DENSITY_RANGE_REJECTED %s before prepare/Dispatch\n",
            contribution.diagnostic);
        return;
    }
    assert(contribution.success);

    struct Observer final : PassSubmissionObserver {
        std::shared_ptr<const GpuFence> fence;

        const char* validateSubmission(PassSubmissionToken) const noexcept override
        {
            return nullptr;
        }

        void onSubmitted(
            PassSubmissionToken, const std::shared_ptr<const GpuFence>& value) noexcept override
        {
            fence = value;
        }
    } observer;

    TextureToBufferCopyDesc copy;
    copy.sourceTexture = textures[2]->id();
    copy.destinationBuffer = buffers[13]->id();
    copy.extent = {plan.extent.width, plan.extent.height, 1u};
    copy.textureSubresources = {0u, 1u, 0u, 1u, TextureAspect::Color};
    copy.bytesPerRow = mixed.rowBytes;
    copy.rowsPerImage = plan.extent.height;
    execution.copy("Cloud.Readback", copy)
        .read(th[2], ResourceState::CopySrc)
        .write(bh[13], ResourceState::CopyDst)
        .onSubmitted(observer);
    // Authored finite acceptance limits, independent of device/OS defaults.
    constexpr std::uint32_t commandCapacity = 64u, ownerCapacity = 2048u;
    OriginalFrameGraphBudget budget;
    budget.contractVersion = 1u;
    budget.prototypeOptIn = true;
    budget.maxCpuIntentBytes = bytes;
    const auto pipelineCount = hasForeground ? 4u : 3u;
    budget.inputs = {10u, true, pipelineCount, pipelineCount};
    budget.states = {5u,
        true,
        1u,
        static_cast<std::uint32_t>(inventory->sources().size() + pipelineCount + 1u),
        32u,
        32u,
        ownerCapacity,
        bytes};
    auto& consumption = budget.states.descriptorConsumption;
    const auto shaderAccesses = (hasForeground ? 13u : 12u) + (spatial ? 3u : 0u);
    const auto renderAccesses = hasForeground ? 5u : 4u;
    // Every consumed shader resource and every literal render binding is
    // independently projected by the original state sequence.
    const auto implicitAccesses = shaderAccesses + renderAccesses;
    consumption = {4u, true, 1u, 1u, true, true, pipelineCount, shaderAccesses};
    consumption.maxTextureAccesses = spatial ? 4u : 3u;
    consumption.generalShaderResourcesVersion = 2u;
    consumption.generalShaderResourcesOptIn = true;
    auto& render = budget.states.renderDescriptorUsage;
    render = {
        4u, true, 1u, 1u, true, true, commandCapacity, hasForeground ? 2u : 1u, renderAccesses};
    render.maxTextureAccesses = 2u;
    render.generalShaderResourcesVersion = 2u;
    render.generalShaderResourcesOptIn = true;
    auto& stream = budget.stream;
    stream.contractVersion = 5u;
    stream.prototypeOptIn = true;
    stream.maxCommands = stream.maxDescriptors = stream.maxReferences = commandCapacity;
    stream.maxCopyElements = 1u;
    stream.maxRenderPasses = 2u;
    stream.maxAttachmentReferences = 3u;
    stream.maxPipelineBinds = stream.maxDescriptorBinds = stream.maxConsumptions = pipelineCount;
    stream.maxImplicitAccesses = implicitAccesses;
    stream.maxRetainedOwners = ownerCapacity;
    stream.maxCpuMetadataBytes = bytes;
    stream.descriptorConsumption = consumption;
    stream.renderDescriptorUsage = render;
    stream.stateSequence = {5u, true, commandCapacity, 0u, 32u, 64u, implicitAccesses};
    const auto textBytes = std::strlen("Cloud.SceneProducer") + std::strlen("Cloud.Composite")
        + 2u * (textures[0]->desc().label.size() + textures[2]->desc().label.size())
        + textures[1]->desc().label.size();
    budget.preparation = {6u,
        true,
        {1u, true, 2u, 1u, 3u, {1u, true, 2u, 0u, 0u, 0u, textBytes, bytes}},
        7u,
        2u,
        OriginalRenderPassContentBudget{3u, true, 3u, 1u, 64u, 512u, 512u},
        OriginalRenderPassRecordingBudget{3u, true, 512u, 1024u}};
    if (hasForeground) {
        budget.preparation.recording->opaqueDepthGraphicsVersion = 1u;
        budget.preparation.recording->opaqueDepthGraphicsOptIn = true;
        auto oldProfile = budget;
        oldProfile.preparation.recording->opaqueDepthGraphicsVersion = 0u;
        oldProfile.preparation.recording->opaqueDepthGraphicsOptIn = false;
        assert(execution.prepare(oldProfile, intentQuota, stateQuota, recordingQuota));
        assert(!execution.program());
    }
    auto wrong = budget;
    wrong.prototypeOptIn = false;
    assert(execution.prepare(wrong, intentQuota, stateQuota, recordingQuota));
    auto insufficient = budget;
    insufficient.states.maxStateObservations = 0u;
    assert(execution.prepare(insufficient, intentQuota, stateQuota, recordingQuota));
    assert(!execution.program());
    require(execution.prepare(budget, intentQuota, stateQuota, recordingQuota));
    require(execution.bindAttachment(th[0], 6u));
    bool mutationRefused = false;
    try {
        execution.execute();
    } catch (const std::exception& e) {
        mutationRefused = std::string_view(e.what()) == "RG-ORIGINAL-GRAPH-CHANGED";
    }
    assert(mutationRefused);
    require(execution.cancel());
    // Cold preparation begins the native command buffer. Capture its entire
    // recording lifetime so replay also observes vkBeginCommandBuffer.
    core::pmr::String capturePath;
    if (sequence)
        if (const auto* base = std::getenv("ARP_RENDERDOC_CAPTURE_PATH")) {
            capturePath = base;
            capturePath += "-frame-";
            capturePath += core::pmr::String(std::to_string(sequenceFrame).c_str());
        }
    tests::RenderDocCaptureScope capture(capturePath.empty() ? nullptr : capturePath.c_str());
    require(execution.prepare(budget, intentQuota, stateQuota, recordingQuota));
    assert(execution.program());
    assert(execution.prepare(budget, intentQuota, stateQuota, recordingQuota));
    std::shared_ptr<VulkanRhiTimelineSemaphorePayload> readerGate;
    if (history && sequenceFrame == 1u && exerciseReaderGate) {
        core::pmr::String id;
        readerGate = VulkanRhiTimelineSemaphorePayload::create(f.runtime, 0u, id);
        assert(readerGate);
        NativeProbe::submissionGate = readerGate->nativeHandle();
    }
    const auto submittedToken = feature ? feature->preparedToken() : PassSubmissionToken{};
    const std::array hotQuotas{intentQuota, stateQuota, recordingQuota};
    require(test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        hotQuotas, [&]() noexcept -> const char* {
            try {
                execution.execute();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "graph execute: %s\n", e.what());
                std::abort();
            }
            return nullptr;
        }));
    assert(observer.fence && graph.lastExecutionSubmitted());
    if (feature) {
        assert(!feature->preparedToken().ownerId);
        assert(feature->validateSubmission(submittedToken));
    }
    if (readerGate) {
        assert(NativeProbe::gatedSubmissions == 1u && !observer.fence->isSignaled());
        assert(!history->idle() && !history->releaseIdle().success);
        if (feature) {
            assert(featureResources && !feature->preparedHistory()->idle());
            const auto charged = featureResources->retainedLogicalBytes->used();
            const auto rejected = feature->resize(*featureResources);
            assert(!rejected.success
                && std::string_view(rejected.diagnostic) == "CLOUD-RESIZE-IN-FLIGHT");
            assert(feature->preparedHistory() == history
                && featureResources->retainedLogicalBytes->used() == charged);
        }
        CloudGraphContext next{graph};
        next.layer = sequence->layer;
        next.cameraPosition = {39.f, 10.f, -57.f};
        next.view =
            core::lookAt(next.cameraPosition, next.cameraPosition + core::Vec3{.25f, .6f, 1.f});
        next.projection = core::perspective(57.f, 4.f / 3.f, 1.f, 20000.f);
        semantics::applyProjectionYConvention(next.projection.m.data(), f.device->backend());
        next.sunDirection = {
            parameters.sunDirection.x, parameters.sunDirection.y, parameters.sunDirection.z};
        next.sunRadiance = {2, 1.8f, 1.6f};
        next.ambientRadiance = {.1f, .2f, .3f};
        next.time = parameters.cameraTime.w + 1.f / 60.f;
        CloudPreparedFrameBuffers nextBuffers;
        assert((feature ? feature->prepareFrame(next, nextBuffers) : history->prepareFrame(next))
                .success);
        // The old writer is already complete, but this gated frame is still
        // reading that slot. Only the third slot may be selected for writing.
        assert(history->raw()->originalObject() != buffers[11u]);
        assert(history->resolved()->originalObject() != buffers[12u]);
        const auto cancelledToken = feature ? feature->preparedToken() : PassSubmissionToken{};
        assert((feature ? feature->cancelPendingFrame() : history->cancelPendingFrame()).success);
        if (feature)
            assert(
                feature->validateSubmission(cancelledToken) && !feature->preparedToken().ownerId);
        assert(readerGate->hostSignal(1u) == VK_SUCCESS);
        std::puts(
            "ORDINARY_HISTORY_READER_RETIREMENT real writer completed, gated reader retains previous slot; idle resize rejected");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!observer.fence->isSignaled() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    assert(observer.fence->isSignaled());
    retireCompletedVulkanRhiSubmittedCommands();
    capture.end();
    if (sequence) {
        verifyCloudSequenceNumeric(*sequence, buffers, mixed, sequenceFrame, quality);
        assert(history && history->idle());
        return;
    }
    const auto native = vulkanRhiNativePayload(*buffers[13]);
    core::pmr::String diagnostic;
    assert(native->backendInvalidateMappedRead(0u, buffers[13]->desc().sizeBytes, diagnostic));
    const auto* data = static_cast<const std::byte*>(native->backendMappedData());
    const std::array background{.125f, .375f, .75f, .625f};
    CloudReference reference;
    if (asset)
        assert(reference.configure(*asset, tests::cloud::layer()).success);
    std::array<const CloudGpuSample*, 2u> samples{};
    for (unsigned i = 0u; i < 2u; ++i) {
        auto* buffer = i ? mixed.resolved : mixed.raw;
        const auto payload = vulkanRhiNativePayload(*buffer);
        assert(payload->backendInvalidateMappedRead(0u, buffer->desc().sizeBytes, diagnostic));
        samples[i] = reinterpret_cast<const CloudGpuSample*>(
            static_cast<const std::byte*>(payload->backendMappedData()) + mixed.sampleOffsets[i]);
    }
    std::FILE* oracle = nullptr;
    if (const auto* path = std::getenv("ARP_CLOUD_ORACLE_PATH")) {
        oracle = std::fopen(path, "wb");
        assert(oracle);
        std::fprintf(oracle,
            "{\"width\":%u,\"height\":%u,\"rowBytes\":%llu,\"tolerance\":0.002,\"expected\":[",
            plan.extent.width,
            plan.extent.height,
            static_cast<unsigned long long>(mixed.rowBytes));
    }
    core::pmr::Vector<float> expectedOpaqueDistances;
    expectedOpaqueDistances.reserve(plan.sampleCount);
    float worstSample = 0.f, worstColor = 0.f;
    unsigned cloudy = 0u, foregroundPixels = 0u, backgroundCloudy = 0u;
    float worstOpaqueDistance = 0.f;
    for (unsigned y = 0u; y < plan.extent.height; ++y)
        for (unsigned x = 0u; x < plan.extent.width; ++x) {
            renderer::volume::MediumIntegral integral;
            const bool covered =
                hasForeground && foregroundPixel(x, y, plan.extent.width, plan.extent.height);
            foregroundPixels += covered;
            float opaqueDistance = parameters.temporal.z;
            if (asset) {
                const float u = (x + .5f) / plan.extent.width, v = (y + .5f) / plan.extent.height;
                const auto nearPoint = core::transformPoint(
                    parameters.inverseViewProjection, {u * 2 - 1, v * 2 - 1, 0});
                const auto farPoint = core::transformPoint(
                    parameters.inverseViewProjection, {u * 2 - 1, v * 2 - 1, 1});
                const core::Vec3 camera{
                    parameters.cameraTime.x, parameters.cameraTime.y, parameters.cameraTime.z};
                const auto origin = parameters.jitter.w > 0 ? nearPoint : camera;
                const auto direction = core::normalize(farPoint - nearPoint);
                if (covered) {
                    const auto surface = core::transformPoint(
                        parameters.inverseViewProjection, {u * 2 - 1, v * 2 - 1, .5f});
                    opaqueDistance = std::max(0.f, core::dot(surface - origin, direction));
                }
                assert(reference
                        .integrate({origin, direction, opaqueDistance},
                            parameters.sampling[0],
                            parameters.sampling[1],
                            {parameters.sunDirection.x,
                                parameters.sunDirection.y,
                                parameters.sunDirection.z},
                            {parameters.sunRadiance.x,
                                parameters.sunRadiance.y,
                                parameters.sunRadiance.z},
                            {parameters.ambientRadiance.x,
                                parameters.ambientRadiance.y,
                                parameters.ambientRadiance.z},
                            parameters.cameraTime.w,
                            integral)
                        .success);
            }
            const std::array expected{integral.radiance.x,
                integral.radiance.y,
                integral.radiance.z,
                integral.transmittance};
            expectedOpaqueDistances.push_back(opaqueDistance);
            const auto pixelIndex = y * plan.extent.width + x;
            const auto sampleIndex = asset ? pixelIndex : 0u;
            for (const auto* values : samples) {
                const auto value = values[sampleIndex].radianceTransmittance;
                const std::array actual{value.x, value.y, value.z, value.w};
                for (unsigned c = 0u; c < 4u; ++c) {
                    assert(std::isfinite(actual[c]));
                    worstSample = std::max(worstSample, std::fabs(actual[c] - expected[c]));
                }
                worstOpaqueDistance = std::max(worstOpaqueDistance,
                    std::fabs(values[sampleIndex].metadata.y - opaqueDistance));
                if (covered && parameters.cameraTime.y < tests::cloud::layer().baseAltitude)
                    assert(actual[0] == 0.f && actual[1] == 0.f && actual[2] == 0.f
                        && actual[3] == 1.f);
            }
            cloudy += samples[1][sampleIndex].radianceTransmittance.w < .999f;
            backgroundCloudy += !covered && samples[1][sampleIndex].radianceTransmittance.w < .999f;
            std::array<std::uint16_t, 4u> pixel;
            std::memcpy(pixel.data(), data + y * mixed.rowBytes + x * sizeof(pixel), sizeof(pixel));
            for (unsigned c = 0u; c < 4u; ++c) {
                const auto& scene = covered ? foregroundColor : background;
                const float color = c == 3u ? scene[c] : expected[c] + expected[3] * scene[c];
                worstColor = std::max(worstColor, std::fabs(core::halfToFloat(pixel[c]) - color));
            }
            if (oracle)
                std::fprintf(oracle,
                    "%s[%.9g,%.9g,%.9g,%.9g]",
                    pixelIndex ? "," : "",
                    double(expected[0]),
                    double(expected[1]),
                    double(expected[2]),
                    double(expected[3]));
        }
    if (oracle) {
        std::fprintf(
            oracle, "],\"foreground\":%s,\"opaqueDistances\":[", hasForeground ? "true" : "false");
        for (std::size_t i = 0u; i < expectedOpaqueDistances.size(); ++i)
            std::fprintf(oracle, "%s%.9g", i ? "," : "", double(expectedOpaqueDistances[i]));
        std::fprintf(oracle, "]}\n");
        std::fclose(oracle);
    }
    std::printf(
        "ORDINARY_FEATURE_GRAPH_NUMERIC texels=%u cloudy=%u sampleMaxError=%.9g hdrMaxError=%.9g\n",
        plan.extent.width * plan.extent.height,
        cloudy,
        double(worstSample),
        double(worstColor));
    assert(worstSample < .002f && worstColor < .002f);
    assert(worstOpaqueDistance < .002f);
    assert(!hasForeground || (foregroundPixels > 0u && foregroundPixels < plan.sampleCount));
    std::printf("ORDINARY_FEATURE_FOREGROUND covered=%u metadataMaxError=%.9g\n",
        foregroundPixels,
        double(worstOpaqueDistance));
    assert(!asset || cloudy > 0u);
    assert(!hasForeground || backgroundCloudy > 0u);
    assert(graph.executionTrace().size() == 5u);
    bool refused = false;
    try {
        execution.execute();
    } catch (...) {
        refused = true;
    }
    assert(refused);
    std::printf(
        "ORDINARY_FEATURE_FRAME_GRAPH_PASS dispatch2/draw%u/copy1 CPU oracle; hot allocations=0\n",
        hasForeground ? 2u : 1u);
}
#endif

enum class CloudQualityFixture {
    Disabled,
    Reference,
    High,
    Medium,
    MediumFullProbe,
    MediumSpatial
};

enum class CloudDensityProbe { Valid, RayShort, ResolveShort, ResolveOffset };

void verifyOrdinaryMixedCloud(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    bool measureBudget = true,
    bool deviceIssuer = false,
    bool featureGraph = false,
    unsigned cloudCase = 0u,
    bool foreground = false,
    bool historySequence = false,
    bool exerciseReaderGate = false,
    bool orthographic = false,
    CloudQualityFixture quality = CloudQualityFixture::Disabled,
    CloudDensityProbe densityProbe = CloudDensityProbe::Valid,
    bool spatialMotion = false,
    bool rendererPipelines = false,
    bool rendererSequence = false)
{
    using Sources = OriginalCommandResourceSources;
    using SetSource = OriginalCommandDescriptorSetSource;
    constexpr std::array lifecycleModes{0u, 1u, 2u, 5u, 4u};
    constexpr unsigned historySlots = 3u, sequenceFrames = 11u, resizeFrame = 6u;
    const bool qualityRun = quality != CloudQualityFixture::Disabled;
    const bool integrationProbe = quality == CloudQualityFixture::MediumFullProbe;
    const bool spatial = quality == CloudQualityFixture::MediumSpatial;
    const renderer::cloud::CloudReconstructionConfig reconstruction{1u, .025f, .025f};
    const bool rejectDensity = densityProbe != CloudDensityProbe::Valid;
    assert(
        !rendererSequence || (rendererPipelines && featureGraph && historySequence && foreground));
    const unsigned frameCount = rendererPipelines && !rendererSequence ? 1u
        : rejectDensity                                                ? 1u
        : spatialMotion                                                ? sequenceFrames
        : qualityRun ? (quality == CloudQualityFixture::Reference || integrationProbe ? 1u : 16u)
        : historySequence ? sequenceFrames
                          : 1u;
    Fixture f(runtime,
        true,
        true,
        foreground,
        historySequence ? historySlots : 0u,
        spatial,
        !rendererPipelines);
    core::CpuMemoryQuotaScope scope(f.quotas[9]);
    const auto require = [](const char* id) {
        if (id)
            std::fprintf(stderr, "ordinary mixed cloud: %s\n", id);
        assert(!id);
    };
    auto* extension = static_cast<OriginalPreparedCommandDeviceExtension*>(
        f.device->queryInterface(OriginalPreparedCommandDeviceExtension::id));
    assert(extension && &extension->originalDevice() == f.device.get());
    std::shared_ptr<RenderPipeline> graphics;
    std::shared_ptr<ComputePipeline> rayPipeline, temporalPipeline;
    core::UniquePtr<ComputeFixture> ray, temporal;
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
    renderer::cloud::CloudPreparedPipelines preparedPipelines;
    renderer::cloud::VolumetricCloudFeature preparedFeature;
    std::optional<assets::cloud::CloudAsset> preparedAsset;
    std::optional<renderer::cloud::CloudPreparedResourceCreateContext> preparedResources;
    if (rendererPipelines) {
        using namespace renderer::cloud;
        CloudPreparedPipelineCreateContext c{*f.device};
        c.resources.config = tests::cloud::config();
        if (spatial)
            assert(CloudQuality::apply(1u, CloudQualityTier::Medium, c.resources.config).success);
        c.resources.extent = spatial ? Extent2D{64u, 48u} : Extent2D{32u, 24u};
        c.resources.assetBytes = tests::cloud::asset().cells.size() * sizeof(core::Vec4);
        c.resources.maxDescriptorSets = 9u;
        c.resources.maxDescriptorElements = spatial ? 45u : 36u;
        c.selection = {1u,
            spatial ? CloudPreparedResolve::Reconstruction1 : CloudPreparedResolve::Temporal1,
            spatial ? reconstruction : CloudReconstructionConfig{}};
        c.publications = {ARP_ORIGINAL_CLOUD_RAYMARCH,
            ARP_ORIGINAL_CLOUD_TEMPORAL,
            ARP_ORIGINAL_CLOUD_MODULE,
            ARP_ORIGINAL_CLOUD_SPATIAL_RESOLVE,
            ARP_ORIGINAL_CLOUD_SPATIAL_MODULE};
        c.preparation = {6u,
            true,
            {1u, true, 2u, 1u, 3u, {1u, true, 2u, 0u, 0u, 0u, 256u, bytes}},
            7u,
            2u,
            OriginalRenderPassContentBudget{3u, true, 3u, 1u, 64u, 512u, 512u},
            OriginalRenderPassRecordingBudget{3u, true, 512u, 1024u}};
        auto& b = c.budget;
        b.maxCpuOrchestrationBytes = bytes;
        c.orchestrationQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        b.computeLoad = {bytes, 8u * bytes, bytes, 7u};
        b.graphicsLoad = {bytes, 8u * bytes, bytes, 4u, 4u, 256u, 2u};
        b.computeInput = {1u, bytes};
        b.graphicsInput = {1u, bytes};
        b.computeCreation = {1u, true, bytes, 256u, bytes, bytes};
        b.graphicsCreation = {1u, true, bytes, 256u, bytes, bytes};
        b.computeWrapper = b.graphicsWrapper = {2u, true, bytes};
        for (auto& q : c.quotas) {
            q.artifact = core::makeShared<core::CpuMemoryQuota>(bytes);
            q.input = core::makeShared<core::CpuMemoryQuota>(bytes);
            q.owner = core::makeShared<core::CpuMemoryQuota>(bytes);
            q.native = core::makeShared<core::CpuMemoryQuota>(bytes);
            q.wrapper = core::makeShared<core::CpuMemoryQuota>(bytes);
        }
        core::pmr::String text;
        const auto liveBefore = NativeProbe::live;
        const auto assertReleased = [&] {
            std::printf(
                "COLD_PREFIX_RELEASE native=%u/%u/%u/%u/%u orchestration=%llu diagnosticCapacity=%zu\n",
                NativeProbe::live[0],
                NativeProbe::live[1],
                NativeProbe::live[2],
                NativeProbe::live[3],
                NativeProbe::live[4],
                static_cast<unsigned long long>(c.orchestrationQuota->liveBytes()),
                text.capacity());
            // The returned owning diagnostic is caller-visible output; destroy
            // its capacity before testing complete CPU quota reclamation.
            core::pmr::String{}.swap(text);
            assert(!preparedPipelines.created() && !preparedPipelines.raymarch()
                && !preparedPipelines.resolve());
            assert(NativeProbe::live == liveBefore && !c.orchestrationQuota->liveBytes());
            for (const auto& q : c.quotas)
                assert(!q.artifact->liveBytes() && !q.input->liveBytes() && !q.owner->liveBytes()
                    && !q.native->liveBytes() && !q.wrapper->liveBytes());
        };
        if (spatial) {
            const auto selected = c.publications.spatialResolve;
            c.publications.spatialResolve = c.publications.temporal;
            const auto calls = NativeProbe::ordinal;
            const auto rejected = preparedPipelines.create(c, text);
            assert(!rejected.success && text == "CLOUD-PREPARED-PIPELINE-RESOLVE-ABI"
                && NativeProbe::ordinal == calls);
            assertReleased();
            c.publications.spatialResolve = selected;
        }
        for (std::size_t i = 0u; i < c.quotas.size(); ++i) {
            c.quotas[i].owner->setLimitBytes(bytes - 1u);
            assert(!preparedPipelines.create(c, text).success && !text.empty());
            assertReleased();
            c.quotas[i].owner->setLimitBytes(bytes);
        }
        NativeProbe::ordinal = 0u;
        const auto result = preparedPipelines.create(c, text);
        if (!result.success)
            std::fprintf(
                stderr, "renderer cold pipelines: %s (%s)\n", result.diagnostic, text.c_str());
        assert(result.success && text.empty() && preparedPipelines.created());
        const auto creationCalls = NativeProbe::ordinal;
        assert(creationCalls);
        preparedPipelines.release();
        assertReleased();
        for (unsigned at = 1u; at <= creationCalls; ++at) {
            NativeProbe::ordinal = 0u;
            NativeProbe::failAt = at;
            assert(!preparedPipelines.create(c, text).success && !text.empty());
            assertReleased();
            NativeProbe::failAt = 0u;
        }
        preparedAsset = tests::cloud::asset();
        preparedResources.emplace(CloudPreparedResourceCreateContext{*f.device, *preparedAsset});
        auto& resources = *preparedResources;
        resources.contractVersion = 1u;
        resources.prototypeOptIn = true;
        resources.cameraId = 1u;
        resources.selection = c.selection;
        resources.resources = c.resources;
        resources.sampleMemory = MemoryUsage::CpuToGpu;
        resources.maxCpuOwnerBytes = resources.maxCpuInitializationBytes = bytes;
        resources.ownerQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        resources.initializationQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        resources.sourceQuota = f.quotas[7];
        resources.historyQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        resources.retainedLogicalBytes =
            core::makeShared<core::AtomicCapacityBudget>(c.resources.config.gpuBudgetBytes);
        CloudPreparedFeatureCreateContext featureContext{c, resources};
        featureContext.contractVersion = 1u;
        featureContext.prototypeOptIn = true;
        featureContext.maxCpuStateBytes = bytes;
        featureContext.stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto finalResult = preparedFeature.create(featureContext, text);
        if (!finalResult.success)
            std::fprintf(stderr,
                "renderer cold pipelines final: %s (%s)\n",
                finalResult.diagnostic,
                text.c_str());
        assert(finalResult.success && text.empty());
        graphics = preparedFeature.preparedPipelines()->composite();
        rayPipeline = preparedFeature.preparedPipelines()->raymarch();
        temporalPipeline = preparedFeature.preparedPipelines()->resolve();
        assert(!preparedFeature.create(featureContext, text).success);
        std::printf(
            "CLOUD_RENDERER_COLD_PIPELINES_PASS resolve=%s native-failure-prefixes=%u quota-prefixes=3\n",
            spatial ? "Reconstruction/1" : "Temporal/1",
            creationCalls);
    } else
#endif
    {
        ray = core::makeUnique<ComputeFixture>(
            runtime, ARP_ORIGINAL_CLOUD_RAYMARCH, false, f.device.get());
        temporal = core::makeUnique<ComputeFixture>(runtime,
            spatial ? ARP_ORIGINAL_CLOUD_SPATIAL_RESOLVE : ARP_ORIGINAL_CLOUD_TEMPORAL,
            false,
            f.device.get());
        std::shared_ptr<const OriginalGraphicsPipelineCreation> creation = deviceIssuer
            ? extension->createGraphics(
                  f.input, f.layout, f.authored, f.budget, f.quotas[4], f.quotas[5], f.diagnostic)
            : f.create();
        assert(creation);
        graphics = OriginalRegisteredPipeline::shareAcceptedOriginal(
            f.device->createOriginalArtifactGraphicsPipeline(
                creation, {2u, true, bytes}, f.quotas[6]),
            f.diagnostic);
        auto rayCreation = ray->create(), temporalCreation = temporal->create();
        assert(rayCreation && temporalCreation);
        rayPipeline = OriginalRegisteredPipeline::shareAcceptedOriginal(
            f.device->createOriginalArtifactComputePipeline(
                rayCreation, {2u, true, bytes}, ray->quotas[6]),
            f.diagnostic);
        temporalPipeline = OriginalRegisteredPipeline::shareAcceptedOriginal(
            f.device->createOriginalArtifactComputePipeline(
                temporalCreation, {2u, true, bytes}, temporal->quotas[6]),
            f.diagnostic);
    }
    assert(graphics && rayPipeline && temporalPipeline);
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
    renderer::cloud::CloudPreparedHistory fixtureHistory;
    auto* history = &fixtureHistory;
    std::array<renderer::cloud::CloudPreparedHistorySlot, historySlots> slots;
    tests::cloud::SequenceOracle sequence;
    tests::cloud::QualityOracle qualityOracle;
    render_graph::PassSubmissionToken cancelledFeatureToken{}, previousFeatureToken{};
    std::shared_ptr<RenderPipeline> foregroundPipeline;
    if (foreground)
        foregroundPipeline = prepareForegroundPipeline(f, *extension);
#endif
    for (unsigned frame = 0; frame < frameCount; ++frame) {
        core::pmr::Vector<Sources::Source> prefix(spatial ? 16u : foreground ? 15u : 14u);
        std::array<GpuTexture*, 3u> textures{};
        std::array<GpuBuffer*, 16u> mutableBuffers{};
        MixedCloudProgramData mixed;
        std::array<std::uint64_t, 16u> offsets{}, lengths{};
        const auto buffer = [&](std::size_t index,
                                std::uint64_t count,
                                ResourceUsage usage,
                                ResourceState state,
                                bool writable,
                                const void* data,
                                std::uint64_t dataBytes) {
            const auto alignment = usage == ResourceUsage::UniformBuffer
                ? runtime->descriptorIndexing().limits.minUniformBufferOffsetAlignment
                : runtime->descriptorIndexing().limits.minStorageBufferOffsetAlignment;
            offsets[index] = (usage & ResourceUsage::StorageBuffer) != ResourceUsage::None
                ? std::lcm<std::uint64_t>(alignment, sizeof(renderer::cloud::CloudGpuSample))
                : usage == ResourceUsage::UniformBuffer ? alignment
                                                        : 0u;
            lengths[index] = count;
            BufferDesc desc;
            desc.sizeBytes = offsets[index] + count;
            desc.usage = usage;
            desc.memory = MemoryUsage::CpuToGpu;
            desc.initialState = state;
            auto object = f.device->createBuffer(desc);
            if (!object)
                std::fprintf(stderr,
                    "ordinary buffer index=%zu bytes=%llu registration=%s\n",
                    index,
                    static_cast<unsigned long long>(desc.sizeBytes),
                    f.device->lastOriginalResourceRegistrationDiagnostic().data());
            assert(object);
            core::pmr::Vector<std::byte> initialized(
                desc.sizeBytes, &core::defaultCpuMemoryResource());
            assert(dataBytes <= count);
            if (dataBytes)
                std::memcpy(initialized.data() + offsets[index], data, dataBytes);
            assert(f.device->writeBuffer(*object, initialized.data(), initialized.size()).success);
            if (writable)
                object->sealHostWrites();
            else
                object->sealWrites();
            if (index == 1u)
                mixed.resolved = object.get();
            if (index == 11u)
                mixed.raw = object.get();
            if (index == 13u)
                mixed.readback = object.get();
            mutableBuffers[index] = object.get();
            prefix[index] = OriginalCommandBufferLease::prepareOriginal(
                object, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
            require(f.diagnostic);
        };
        const Extent2D frameExtent = spatialMotion
            ? (frame >= resizeFrame ? Extent2D{63u, 47u} : Extent2D{64u, 48u})
            : qualityRun ? Extent2D{64u, 48u}
            : historySequence && (!foreground || rendererSequence) && frame >= resizeFrame
            ? Extent2D{31u, 23u}
            : cloudCase ? Extent2D{32u, 24u}
                        : Extent2D{4u, 4u};
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
        std::optional<assets::cloud::CloudAsset> cloudAsset;
#endif
        renderer::cloud::CloudGpuConstants constants{};
        constants.inverseViewProjection = core::Mat4::identity();
        constants.previousViewProjection = core::Mat4::identity();
        constants.cameraRadial = {0.f, 100.f, 0.f, 0.f};
        constants.shell = {1.f, 5.f, 100.f, .01f};
        constants.dimensions = {1u, 1u, 4u, 4u};
        constants.sampling = {2u, 1u, 1u, 1u};
        constants.assetLayout = {1u, 0u, 4u, 5u};
        constants.periodsCoverage = {1.f, 1.f, 1.f, 0.f};
        constants.sunDirection = {0.f, 1.f, 0.f, 0.f};
        constants.temporal = {0.f, 1.f, 1000.f, 0.f};
        constants.jitter = {.5f, .5f, .5f, 0.f};
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
        if (cloudCase) {
            cloudAsset = tests::cloud::asset();
            if (cloudCase <= 3u) {
                const auto profiles = assets::cloud::kCloudHeightProfileCount;
                const auto shape = profiles
                    + cloudAsset->source.weatherDimension * cloudAsset->source.weatherDimension;
                const auto detail = shape
                    + cloudAsset->source.shapeDimension * cloudAsset->source.shapeDimension
                        * cloudAsset->source.shapeDimension;
                for (unsigned i = 0u; i < profiles; ++i) {
                    cloudAsset->cells[i] = {0, .001f, .999f, 1};
                    cloudAsset->source.heightProfiles[i] = cloudAsset->cells[i];
                }
                for (unsigned i = profiles; i < shape; ++i)
                    cloudAsset->cells[i] = {1, 0, 1, 0};
                for (unsigned i = shape; i < detail; ++i)
                    cloudAsset->cells[i] = {1, 0, 0, 0};
                for (std::size_t i = detail; i < cloudAsset->cells.size(); ++i)
                    cloudAsset->cells[i] = {};
                assert(assets::cloud::CloudAssetCooker::validate(*cloudAsset).success);
            }
            const auto layer = tests::cloud::layer();
            const auto altitude = (cloudCase - 1u) % 3u;
            const core::Vec3 camera{39.f,
                altitude == 0u       ? 10.f
                    : altitude == 1u ? 1600.f
                                     : 4000.f,
                -57.f};
            const auto view =
                core::lookAt(camera, camera + core::Vec3{.25f, altitude == 2u ? -.6f : .6f, 1.f});
            auto projection = core::perspective(
                57.f, float(frameExtent.width) / frameExtent.height, 1.f, 20000.f);
            semantics::applyProjectionYConvention(projection.m.data(), f.device->backend());
            auto vp = core::multiply(projection, view);
            semantics::applyRenderTargetSamplingYConvention(vp.m.data(), f.device->backend());
            constants.inverseViewProjection = core::inverse(vp);
            constants.previousViewProjection = vp;
            constants.cameraTime = {camera.x, camera.y, camera.z, 0.f};
            const double rx = double(camera.x) - layer.planetCenter.x,
                         ry = double(camera.y) - layer.planetCenter.y,
                         rz = double(camera.z) - layer.planetCenter.z;
            constants.cameraRadial = {float(rx),
                float(ry),
                float(rz),
                float(
                    rx * rx + ry * ry + rz * rz - double(layer.planetRadius) * layer.planetRadius)};
            constants.shell = {
                layer.baseAltitude, layer.thickness, layer.planetRadius, layer.extinction};
            constants.albedoAnisotropy = {layer.scatteringAlbedo.x,
                layer.scatteringAlbedo.y,
                layer.scatteringAlbedo.z,
                layer.anisotropy};
            constants.periodsCoverage = {
                layer.weatherPeriod, layer.shapePeriod, layer.detailPeriod, layer.coverage};
            constants.windErosion = {
                layer.windVelocity.x, layer.windVelocity.y, layer.windVelocity.z, layer.erosion};
            const auto sun = core::normalize(core::Vec3{1, 1, -.2f});
            constants.sunDirection = {sun.x, sun.y, sun.z, 0};
            constants.sunRadiance = {2, 1.8f, 1.6f, 0};
            constants.ambientRadiance = {.1f, .2f, .3f, 0};
            constants.dimensions = {
                frameExtent.width, frameExtent.height, frameExtent.width, frameExtent.height};
            constants.sampling = {
                24u, 4u, cloudAsset->source.weatherDimension, cloudAsset->source.shapeDimension};
            const auto shape = assets::cloud::kCloudHeightProfileCount
                + constants.sampling[2] * constants.sampling[2];
            constants.assetLayout = {cloudAsset->source.detailDimension,
                0u,
                shape,
                shape + constants.sampling[3] * constants.sampling[3] * constants.sampling[3]};
            constants.temporal = {0.f, 10.f, 10000.f, 0.f};
        }
#endif
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
        if (historySequence) {
            auto config = tests::cloud::config();
            if (qualityRun) {
                const auto tier = quality == CloudQualityFixture::Reference
                    ? renderer::cloud::CloudQualityTier::Reference
                    : quality == CloudQualityFixture::High
                    ? renderer::cloud::CloudQualityTier::High
                    : renderer::cloud::CloudQualityTier::Medium;
                assert(renderer::cloud::CloudQuality::apply(1u, tier, config).success);
                if (integrationProbe) {
                    // Test-only integration isolation, not a renamed Medium
                    // quality profile: explicit full resolution, no history.
                    config.resolutionDivisor = 1u;
                    config.temporal = false;
                    std::printf(
                        "ORDINARY_QUALITY_INTEGRATION_PROBE full-resolution 64/8; not Medium acceptance\n");
                }
            } else
                config.resolutionDivisor = frame >= resizeFrame ? 2u : 1u;
            auto result = renderer::cloud::CloudExecutionPlanner::describeResources(config,
                frameExtent,
                cloudAsset->cells.size() * sizeof(core::Vec4),
                sizeof(constants),
                sequence.plan);
            require(result.success ? nullptr : result.diagnostic);
            renderer::cloud::CloudReconstructionResources spatialResources;
            if (spatial) {
                result = renderer::cloud::CloudReconstruction::describeResources(reconstruction,
                    config,
                    frameExtent,
                    cloudAsset->cells.size() * sizeof(core::Vec4),
                    sizeof(constants),
                    spatialResources);
                require(result.success ? nullptr : result.diagnostic);
                sequence.reconstruction = reconstruction;
            }
            if (frame == 0u || ((!qualityRun || spatialMotion) && frame == resizeFrame)) {
                if (rendererPipelines) {
                    assert(preparedResources);
                    if (frame) {
                        assert(preparedFeature.preparedHistory()->idle());
                        // The caller releases its borrowed slot leases before
                        // replacement; the same authored budget remains live.
                        slots = {};
                        preparedResources->resources.config = config;
                        preparedResources->resources.extent = frameExtent;
                        const auto pipelineCalls = NativeProbe::ordinal;
                        result = preparedFeature.resize(*preparedResources);
                        require(result.success ? nullptr : result.diagnostic);
                        assert(NativeProbe::ordinal == pipelineCalls);
                        assert(preparedFeature.validateSubmission(previousFeatureToken));
                        assert(preparedFeature.preparedPipelines()->raymarch() == rayPipeline
                            && preparedFeature.preparedPipelines()->resolve() == temporalPipeline
                            && preparedFeature.preparedPipelines()->composite() == graphics);
                        std::printf(
                            "CLOUD_OWNED_IDLE_RESIZE frame=%u extent=%ux%u pipelines=retained\n",
                            frame,
                            frameExtent.width,
                            frameExtent.height);
                    }
                    history = preparedFeature.preparedHistory();
                    const auto owners = preparedFeature.preparedResources()->historySlots();
                    assert(owners.size() == slots.size());
                    std::copy(owners.begin(), owners.end(), slots.begin());
                    const auto plannedBytes = spatial ? spatialResources.retainedGpuBytes
                                                      : sequence.plan.retainedGpuBytes;
                    assert(preparedResources->retainedLogicalBytes->used() == plannedBytes);
                } else {
                    result = history->releaseIdle();
                    require(result.success ? nullptr : result.diagnostic);
                    for (auto& slot : slots)
                        slot = {};
                    for (auto& slot : slots)
                        for (auto* lease : {&slot.raw, &slot.resolved}) {
                            BufferDesc desc;
                            desc.sizeBytes = spatial && lease == &slot.resolved
                                ? spatialResources.resolvedBufferBytes
                                : sequence.plan.sampleBufferBytes;
                            desc.usage = ResourceUsage::StorageBuffer;
                            desc.memory = MemoryUsage::CpuToGpu;
                            desc.initialState = ResourceState::ShaderWrite;
                            auto object = f.device->createBuffer(desc);
                            assert(object);
                            core::pmr::Vector<std::byte> zero(desc.sizeBytes);
                            assert(
                                f.device->writeBuffer(*object, zero.data(), zero.size()).success);
                            object->sealHostWrites();
                            *lease = OriginalCommandBufferLease::prepareOriginal(
                                object, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
                            require(f.diagnostic);
                        }
                    result = history->create(*f.device,
                        1u,
                        config,
                        sequence.plan,
                        cloudAsset->source,
                        slots,
                        f.quotas[7],
                        spatial ? &reconstruction : nullptr);
                    require(result.success ? nullptr : result.diagnostic);
                }
            }
            if ((!qualityRun || spatialMotion) && frame == 9u) {
                result =
                    rendererPipelines ? preparedFeature.resetHistory() : history->resetHistory();
                require(result.success ? nullptr : result.diagnostic);
            }
            render_graph::FrameGraph metadata(*f.device);
            renderer::cloud::CloudGraphContext c{metadata};
            c.layer = tests::cloud::layer();
            if (qualityRun)
                c.layer.windVelocity = {};
            else if (frame >= 8u)
                c.layer.windVelocity.x += 2.f;
            const auto altitude = (cloudCase - 1u) % 3u;
            c.cameraPosition = {39.f
                    + (spatialMotion && frame >= 2u      ? 64.f
                            : !qualityRun && frame >= 2u ? .75f
                                                         : 0.f),
                orthographic                       ? 300.f
                    : qualityRun && altitude == 1u ? 1600.f
                    : qualityRun && altitude == 2u ? 4000.f
                                                   : 10.f,
                -57.f};
            c.view = core::lookAt(c.cameraPosition,
                c.cameraPosition
                    + core::Vec3{.25f, qualityRun && altitude == 2u ? -.6f : .6f, 1.f});
            c.orthographic = orthographic;
            c.projection = orthographic
                ? core::orthographic(frame >= 5u ? 110.f : 100.f, 75.f, 1.f, 800.f)
                : core::perspective((!qualityRun || spatialMotion) && frame >= 5u ? 60.f : 57.f,
                      float(frameExtent.width) / frameExtent.height,
                      1.f,
                      20000.f);
            semantics::applyProjectionYConvention(c.projection.m.data(), f.device->backend());
            c.sunDirection = core::normalize(core::Vec3{1, 1, -.2f});
            c.sunRadiance = {2, 1.8f, 1.6f};
            c.ambientRadiance = {.1f, .2f, .3f};
            if (spatialMotion && frame >= 8u)
                c.ambientRadiance.x += .025f;
            c.time = float(frame) / 60.f;
            c.cameraCut = (!qualityRun || spatialMotion) && frame == 3u;
            c.sceneRevision = (!qualityRun || spatialMotion) && frame >= 4u ? 1u : 0u;
            renderer::cloud::CloudPreparedFrameBuffers preparedFrame;
            if (rendererSequence && (frame == 0u || frame == resizeFrame)) {
                assert(preparedFeature.frameReady());
                result = preparedFeature.prepareFrame(c, preparedFrame);
                require(result.success ? nullptr : result.diagnostic);
                cancelledFeatureToken = preparedFeature.preparedToken();
                const auto rejected = preparedFeature.resize(*preparedResources);
                assert(!rejected.success
                    && std::string_view(rejected.diagnostic) == "CLOUD-RESIZE-IN-FLIGHT");
                assert(
                    preparedFeature.preparedToken().generation == cancelledFeatureToken.generation);
                assert(preparedFeature.cancelPendingFrame().success);
                preparedFrame = {};
                assert(preparedFeature.validateSubmission(cancelledFeatureToken));
            }
            result = rendererPipelines ? preparedFeature.prepareFrame(c, preparedFrame)
                                       : history->prepareFrame(c);
            require(result.success ? nullptr : result.diagnostic);
            if (rendererSequence) {
                previousFeatureToken = preparedFeature.preparedToken();
                assert(previousFeatureToken.ownerId
                    && previousFeatureToken.generation != cancelledFeatureToken.generation);
                assert(preparedFeature.validateSubmission(cancelledFeatureToken));
                renderer::cloud::CloudPreparedFrameBuffers duplicate;
                assert(!preparedFeature.prepareFrame(c, duplicate).success && !duplicate.constants);
            }
            constants = history->pendingFrame().parameters;
            const bool expectedHistory = qualityRun && !spatialMotion
                ? frame > 0u && config.temporal
                : frame == 1u || frame == 2u || frame == 7u || frame == 10u;
            assert(history->pendingFrame().historyUsed == expectedHistory);
            sequence.parameters = constants;
            sequence.layer = c.layer;
            sequence.foreground = spatialMotion ? tests::cloud::ForegroundFixture{foreground,
                                                      float(frame % 2u) * 16.f,
                                                      0.f,
                                                      48.f,
                                                      48.f}
                : qualityRun                    ? tests::cloud::ForegroundFixture{foreground,
                                   frame >= 8u ? 16.f : 0.f,
                                   0.f,
                                   48.f,
                                   48.f}
                                                : tests::cloud::ForegroundFixture{
                                   foreground, float(frame % 2u) * 8.f, 0.f, 24.f, 24.f};
            sequence.requireOcclusionChange = !qualityRun || (!spatialMotion && frame == 8u);
            if (rejectDensity)
                sequence.fullResolved = true;
            else {
                sequence.build(*cloudAsset);
                if (qualityRun)
                    qualityOracle.prepare(sequence, *cloudAsset);
            }
            const std::array selected{
                history->constants(), history->resolved(), history->raw(), history->previous()};
            const std::array indices{0u, 1u, 11u, 12u};
            for (unsigned i = 0; i < selected.size(); ++i) {
                const auto index = indices[i];
                prefix[index] = selected[i];
                mutableBuffers[index] = const_cast<GpuBuffer*>(selected[i]->originalObject());
                lengths[index] = i == 0u      ? sizeof(constants)
                    : spatial && index != 11u ? spatialResources.resolvedBufferBytes
                                              : sequence.plan.sampleBufferBytes;
            }
            if (spatial) {
                prefix[15u] = history->reconstructionConstants();
                mutableBuffers[15u] =
                    const_cast<GpuBuffer*>(history->reconstructionConstants()->originalObject());
                lengths[15u] = sizeof(renderer::cloud::CloudReconstructionGpuConstants);
            }
            // Original inventories contain each owner once. In reset frames,
            // both temporal inputs bind the same real current raw source. The
            // spare slot is declared here but never used as fabricated history.
            if (!spatial && constants.previousCamera.w <= 0) {
                prefix[12u] = slots.back().resolved;
                mutableBuffers[12u] =
                    const_cast<GpuBuffer*>(slots.back().resolved->originalObject());
            }
            mixed.raw = mutableBuffers[11u];
            mixed.resolved = mutableBuffers[1u];
        } else
#endif
            buffer(0u,
                sizeof(constants),
                ResourceUsage::UniformBuffer,
                ResourceState::ShaderRead,
                false,
                &constants,
                sizeof(constants));
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
        if (foreground)
            buffer(14u,
                sizeof(foregroundColor),
                ResourceUsage::UniformBuffer,
                ResourceState::ShaderRead,
                false,
                foregroundColor.data(),
                sizeof(foregroundColor));
#endif
        const auto sampleBytes =
            std::uint64_t(cloudCase ? constants.dimensions[0] * constants.dimensions[1] : 2u)
            * sizeof(renderer::cloud::CloudGpuSample);
        if (!historySequence)
            buffer(1u,
                sampleBytes,
                ResourceUsage::StorageBuffer,
                ResourceState::ShaderWrite,
                true,
                nullptr,
                0u);
        std::array<core::Vec4, 6u> cells{};
        const auto* densityData = cells.data();
        std::uint64_t densityBytes = sizeof(cells);
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
        if (cloudAsset) {
            densityData = cloudAsset->cells.data();
            densityBytes = cloudAsset->cells.size() * sizeof(core::Vec4);
        }
#endif
        if (rendererPipelines) {
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
            renderer::cloud::CloudPreparedFrameBuffers selected;
            const auto result = preparedFeature.preparedResources()->pendingBuffers(selected);
            require(result.success ? nullptr : result.diagnostic);
            prefix[10u] = selected.density;
            mutableBuffers[10u] = const_cast<GpuBuffer*>(selected.density->originalObject());
            lengths[10u] = densityBytes;
#endif
        } else
            buffer(10u,
                densityProbe == CloudDensityProbe::ResolveOffset ? 2u * densityBytes : densityBytes,
                ResourceUsage::StorageBuffer,
                ResourceState::ShaderRead,
                false,
                densityData,
                densityBytes);
        if (densityProbe == CloudDensityProbe::ResolveOffset)
            lengths[10u] = densityBytes;
        if (!historySequence) {
            buffer(11u,
                sampleBytes,
                ResourceUsage::StorageBuffer,
                ResourceState::ShaderWrite,
                true,
                nullptr,
                0u);
            buffer(12u,
                sampleBytes,
                ResourceUsage::StorageBuffer,
                ResourceState::ShaderRead,
                false,
                nullptr,
                0u);
        }
        // Keep the explicitly portable 256-byte pitch, including outputs
        // wider than the old 32-pixel fixture. Do not truncate a texel row.
        const auto pitch =
            core::alignCpuAllocationOffsetUp(semantics::TextureTransferValidation::copyRowBytes(
                                                 TextureFormat::Rgba16Float, frameExtent.width),
                256u);
        assert(pitch.success && pitch.alignedValue <= UINT32_MAX);
        mixed.rowBytes = static_cast<std::uint32_t>(pitch.alignedValue);
        buffer(13u,
            mixed.rowBytes * frameExtent.height,
            ResourceUsage::CopyDst,
            ResourceState::CopyDst,
            true,
            nullptr,
            0u);
        for (std::size_t i = 0u; i < textures.size(); ++i) {
            const auto parentIndex = i == 2u ? 8u : 2u + 2u * i;
            TextureDesc desc;
            desc.extent = {frameExtent.width, frameExtent.height, 1u};
            desc.format = i == 1u ? TextureFormat::D32Float : TextureFormat::Rgba16Float;
            desc.usage = TextureUsage::RenderAttachment
                | (i == 2u ? TextureUsage::CopySrc : TextureUsage::Sampled);
            const auto allocation =
                f.device->queryOriginalContentTextureAllocation(desc, f.diagnostic, 4u);
            std::uint64_t footprint = 0u;
            assert(allocation && !f.diagnostic
                && !semantics::TextureUploadDestinationValidation::footprint(desc, footprint));
            auto object = f.device->createOriginalContentTexture(desc,
                {4u, true, 1u, 1u, footprint, lifecycleModes.size(), bytes, 1u, allocation},
                f.quotas[7],
                f.diagnostic);
            assert(object);
            textures[i] = object.get();
            auto parent = OriginalCommandTextureLease::prepareOriginal(
                object, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
            prefix[parentIndex] = parent;
            const auto makeView = [&](std::size_t index, TextureUsage usage) {
                TextureViewDesc vd;
                vd.format = desc.format;
                vd.aspect = i == 1u ? TextureAspect::Depth : TextureAspect::Color;
                vd.usage = usage;
                auto view = f.device->createTextureView(*parent->originalObject(), vd);
                auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                    view, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
                prefix[index] = OriginalCommandTextureViewSource::prepareOriginal(
                    lease, parent, {1u, true, 5u, bytes}, f.quotas[7], f.diagnostic);
                require(f.diagnostic);
            };
            if (i != 2u)
                makeView(parentIndex + 1u, TextureUsage::Sampled);
            makeView(i == 2u ? 9u : 6u + i, TextureUsage::RenderAttachment);
        }
        OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = admission.maxReferences = admission.maxInputDescriptors = 64u;
        admission.maxStateObservations = admission.maxTextureRanges = 64u;
        admission.maxPassColorAttachments = 2u;
        admission.maxPassAttachmentReferences = 3u;
        if (featureGraph)
            admission.maxLabelBytes = std::strlen("Cloud.SceneProducer")
                + std::strlen("Cloud.Composite")
                + 2u * (textures[0]->desc().label.size() + textures[2]->desc().label.size())
                + textures[1]->desc().label.size();
        const auto pipelineCount = foreground ? 4u : 3u;
        const auto bufferCount = (foreground ? 7u : 6u) + (spatial ? 1u : 0u);
        admission.maxGraphOwners = pipelineCount * Source::cpuOwnerSlots
            + bufferCount * OriginalCommandBufferLease::retainedOwnerCount
            + 3u * OriginalCommandTextureLease::retainedOwnerCount
            + 5u * OriginalCommandTextureViewSource::completeRetainedOwnerCount
            + pipelineCount * SetSource::finiteExclusiveRetainedOwnerCount;
        if (rendererPipelines)
            admission.maxGraphOwners += 4u; // Shared logical and uniform-generation custody.
        admission.maxCpuBytes.fill(bytes);
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Buffer)] =
            bufferCount;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] = 3u;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::TextureView)] =
            5u;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Pipeline)] =
            pipelineCount;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)] =
            pipelineCount;
        auto inventory = Sources::prepareColdOriginalTyped(prefix.data(),
            prefix.size(),
            admission,
            {8u, true, 2u * pipelineCount},
            f.quotas[7],
            f.diagnostic);
        require(f.diagnostic);
        const auto appendPipeline = [&](const auto& pipeline) {
            using Object =
                std::remove_const_t<typename std::decay_t<decltype(pipeline)>::element_type>;
            using PipelineSource = std::conditional_t<std::is_same_v<Object, RenderPipeline>,
                Source,
                OriginalCommandComputePipelineSource>;
            auto source = PipelineSource::prepareOriginal(pipeline,
                {2u,
                    true,
                    PipelineSource::kind,
                    pipeline->originalResourceRegistration().generation(),
                    bytes,
                    PipelineSource::exclusiveOwnerCount},
                f.quotas[7],
                *f.device,
                f.diagnostic);
            require(f.diagnostic);
            const auto index = inventory->sources().size();
            require(Sources::appendOriginalTyped(inventory, source, *f.device));
            return index;
        };
        const auto graphicsIndex = appendPipeline(graphics);
        mixed.pipelines = {appendPipeline(rayPipeline), appendPipeline(temporalPipeline)};
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
        ForegroundGraphSources foregroundSources;
        if (foreground) {
            foregroundSources.pipeline = appendPipeline(foregroundPipeline);
            foregroundSources.viewport = historySequence
                ? ViewportState{sequence.foreground.x,
                      sequence.foreground.y,
                      sequence.foreground.width,
                      sequence.foreground.height,
                      0.f,
                      1.f}
                : ViewportState{
                      0.f, 0.f, float(frameExtent.width), float(frameExtent.height), 0.f, 1.f};
        }
#endif
        auto arenaCandidate = f.device->createOriginalDescriptorArena({"Original.Mixed.Cloud",
            pipelineCount,
            (foreground ? 13u : 12u) + (spatial ? 3u : 0u)});
        assert(arenaCandidate);
        std::shared_ptr<DescriptorArena> arena;
        std::size_t graphicsSet = SIZE_MAX;
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
        if (rendererPipelines) {
            using namespace renderer::cloud;
            CloudPreparedDescriptorPublishContext c{*f.device,
                *preparedFeature.preparedResources(),
                *preparedFeature.preparedPipelines(),
                inventory,
                arenaCandidate};
            c.contractVersion = 1u;
            c.prototypeOptIn = true;
            c.locations.constants = 0u;
            c.locations.density = 10u;
            c.locations.raw = 11u;
            c.locations.previous = !spatial && constants.previousCamera.w <= 0.f ? 11u : 12u;
            c.locations.resolved = 1u;
            c.locations.reconstructionConstants = spatial ? 15u : SIZE_MAX;
            c.locations.color = 3u;
            c.locations.depth = 5u;
            c.pipelineSources = {mixed.pipelines[0], mixed.pipelines[1], graphicsIndex};
            c.maxCpuOwnerBytes = bytes;
            c.ownerQuota = f.quotas[9];
            for (std::size_t pass = 0u; pass < kCloudPipelineCount; ++pass) {
                auto& request = c.requests[pass];
                request.contractVersion = pass == 2u ? 3u : 4u;
                request.prototypeOptIn = true;
                request.maxBindings = spatial && pass == 1u ? 7u : 4u;
                request.maxRetainedOwnerSlots = OriginalGeneralDescriptorWriteContract::ownerSlots(
                    request.maxBindings, request.contractVersion);
                request.maxCpuBytes.fill(bytes);
                request.maxRetainedAllocationBytes = bytes;
                for (auto& quota : c.quotas[pass])
                    quota = core::makeShared<core::CpuMemoryQuota>(bytes);
            }
            CloudPreparedDescriptorOutput output;
            const auto result = CloudPreparedDescriptors::publish(c, output);
            require(result.success ? nullptr : result.diagnostic);
            assert(inventory && !arenaCandidate
                && output.historyGeneration == history->token().generation);
            arena = std::move(output.arena);
            mixed.sets = {output.descriptors[0], output.descriptors[1]};
            graphicsSet = output.descriptors[2];
        } else
#endif
            arena = core::shareUnique(std::move(arenaCandidate));
        const auto publish = [&](const auto& pipeline,
                                 std::size_t pipelineIndex,
                                 std::initializer_list<std::size_t> resources,
                                 bool compute) {
            const auto& layout = pipeline->originalLayoutOwner()->desc().descriptorLayouts.front();
            auto set = f.device->allocateOriginalDescriptorSet(arena, {layout.name, layout});
            if (!set)
                std::fprintf(stderr,
                    "original descriptor allocation (%zu): %s\n",
                    pipelineIndex,
                    f.device->lastOriginalResourceRegistrationDiagnostic().data());
            assert(set);
            core::pmr::Vector<OriginalCommandDescriptorSetElement> elements(resources.size());
            for (std::size_t i = 0u; i < elements.size(); ++i) {
                auto& e = elements[i];
                e.sourceIndex = resources.begin()[i];
                e.write.binding = layout.bindings[i].binding;
                e.write.type = layout.bindings[i].type;
                e.stages = compute ? PipelineStage::ComputeShader : PipelineStage::FragmentShader;
                e.access = e.write.type == DescriptorType::StorageBuffer
                    ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
                    : ResourceAccess::ShaderRead;
                e.write.resourceState = e.write.type == DescriptorType::StorageBuffer
                    ? ResourceState::ShaderWrite
                    : ResourceState::ShaderRead;
                if (e.write.type == DescriptorType::SampledTexture) {
                    const auto& source =
                        std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            inventory->sources()[e.sourceIndex]);
                    e.write.resource = source->resource();
                    e.write.resourceKind = DescriptorResourceKind::TextureView;
                    e.registrationGeneration =
                        source->originalObject()->originalResourceRegistration().generation();
                } else {
                    const auto& source =
                        std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                            inventory->sources()[e.sourceIndex]);
                    e.write.resource = source->resource();
                    e.write.resourceKind = DescriptorResourceKind::Buffer;
                    e.registrationGeneration =
                        source->originalObject()->originalResourceRegistration().generation();
                    e.write.bufferBindingRange = {
                        2u, offsets[e.sourceIndex], lengths[e.sourceIndex]};
                    e.bufferAccessOffset = offsets[e.sourceIndex];
                    e.bufferAccessBytes = lengths[e.sourceIndex];
                    if (e.sourceIndex == 10u && compute) {
                        if ((densityProbe == CloudDensityProbe::RayShort
                                && pipelineIndex == mixed.pipelines[0])
                            || (densityProbe == CloudDensityProbe::ResolveShort
                                && pipelineIndex == mixed.pipelines[1])) {
                            e.write.bufferBindingRange.sizeBytes = e.bufferAccessBytes =
                                sizeof(core::Vec4);
                        }
                        if (densityProbe == CloudDensityProbe::ResolveOffset
                            && pipelineIndex == mixed.pipelines[1]) {
                            e.write.bufferBindingRange.offsetBytes += offsets[10u];
                            e.bufferAccessOffset += offsets[10u];
                        }
                    }
                }
            }
            OriginalGeneralDescriptorWriteRequest request;
            request.contractVersion = compute ? 4u : 3u;
            request.prototypeOptIn = true;
            request.maxBindings = elements.size();
            request.maxRetainedOwnerSlots = OriginalGeneralDescriptorWriteContract::ownerSlots(
                elements.size(), request.contractVersion);
            request.maxCpuBytes.fill(bytes);
            request.maxRetainedAllocationBytes = bytes;
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> quotas;
            for (auto& quota : quotas)
                quota = core::makeShared<core::CpuMemoryQuota>(bytes);
            require(f.device->beginOriginalGeneralDescriptorWrite(
                *set, pipeline, inventory, 0u, elements.data(), elements.size(), request, quotas));
            require(set->finishOriginalGeneralWrite());
            OriginalCommandDescriptorSourceBudget budget{compute ? 7u : 5u,
                true,
                static_cast<std::uint32_t>(elements.size()),
                static_cast<std::uint32_t>(elements.size()),
                SetSource::finiteExclusiveRetainedOwnerCount,
                bytes};
            budget.finitePipelineSourceIndex = pipelineIndex;
            budget.finiteFootprintContractVersion = 2u;
            budget.finiteFullBoundViewOptIn = true;
            auto source = SetSource::prepareOriginal(set,
                arena,
                inventory,
                elements.data(),
                elements.size(),
                budget,
                f.quotas[7],
                *f.device,
                f.diagnostic);
            require(f.diagnostic);
            const auto index = inventory->sources().size();
            require(Sources::appendOriginalTyped(inventory, source, *f.device));
            return index;
        };
        if (!rendererPipelines) {
            graphicsSet = publish(graphics, graphicsIndex, {0u, 1u, 3u, 5u}, false);
            mixed.sets = {publish(rayPipeline, mixed.pipelines[0], {0u, 10u, 5u, 11u}, true),
                spatial ? publish(temporalPipeline,
                              mixed.pipelines[1],
                              {0u, 11u, 12u, 1u, 10u, 5u, 15u},
                              true)
                        : publish(temporalPipeline,
                              mixed.pipelines[1],
                              {0u,
                                  11u,
                                  historySequence && constants.previousCamera.w <= 0 ? 11u : 12u,
                                  1u},
                              true)};
        }
        mixed.sampleOffsets = {offsets[11u], offsets[1u]};
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
        if (foreground)
            foregroundSources.descriptors =
                publish(foregroundPipeline, foregroundSources.pipeline, {14u}, false);
        if (featureGraph) {
            verifyPreparedCloudGraph(f,
                inventory,
                textures,
                mutableBuffers,
                mixed,
                graphicsIndex,
                graphicsSet,
                constants,
                cloudAsset ? &*cloudAsset : nullptr,
                foregroundSources,
                historySequence ? &sequence : nullptr,
                historySequence ? history : nullptr,
                frame,
                exerciseReaderGate,
                qualityRun ? &qualityOracle : nullptr,
                rejectDensity ? densityProbe == CloudDensityProbe::RayShort
                        ? "CLOUD-PREPARED-GRAPH-RESOURCE-BINDING"
                        : "CLOUD-RECONSTRUCTION-GRAPH-RESOURCE-BINDING"
                              : nullptr,
                rendererPipelines ? &preparedFeature : nullptr,
                rendererPipelines ? &*preparedResources : nullptr);
            if (rejectDensity) {
                require(
                    history->cancelPendingFrame().success ? nullptr : "CLOUD-DENSITY-PROBE-CANCEL");
                require(history->releaseIdle().success ? nullptr : "CLOUD-DENSITY-PROBE-RELEASE");
                return;
            }
            if (!historySequence)
                return;
            continue;
        }
#endif
        for (const auto mode : lifecycleModes) {
            std::printf("ORDINARY_MIXED_MODE_BEGIN %u\n", mode);
            verifyOrdinaryCloudDraw(f,
                runtime,
                inventory,
                textures,
                graphicsIndex,
                graphicsSet,
                mode,
                &mixed,
                measureBudget,
                deviceIssuer);
        }
    }
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
    if (rendererPipelines) {
        assert(preparedFeature.preparedHistory()->idle());
        slots = {};
        preparedFeature.release();
        assert(!preparedFeature.created() && !preparedResources->retainedLogicalBytes->used());
        assert(!preparedResources->ownerQuota->liveBytes()
            && !preparedResources->historyQuota->liveBytes()
            && !preparedResources->initializationQuota->liveBytes());
        if (rendererSequence)
            std::printf(
                "CLOUD_OWNED_HISTORY_PASS resolve=%s orthographic=%u frames=%u readerGate=%u\n",
                spatial ? "Reconstruction/1" : "Temporal/1",
                unsigned(orthographic),
                frameCount,
                unsigned(exerciseReaderGate));
    } else if (historySequence)
        assert(history->releaseIdle().success);
#endif
}

void verifyComputePublication(
    const std::shared_ptr<VulkanRhiRuntime>& runtime, const char* publication)
{
    using Sources = OriginalCommandResourceSources;
    using S = OriginalCommandComputePipelineSource;
    using DescriptorSource = OriginalCommandDescriptorSetSource;
    using Inspection = VulkanOriginalGraphicsBindingInspection;
    ComputeFixture f(runtime, publication, true);
    core::CpuMemoryQuotaScope scope(f.quotas[9]);
    const auto require = [](const char* id) {
        if (id)
            std::fprintf(stderr, "original Compute publication: %s\n", id);
        assert(!id);
    };
    auto receipt = f.create();
    assert(receipt);
    auto pipeline = OriginalRegisteredPipeline::shareAcceptedOriginal(
        f.device->createOriginalArtifactComputePipeline(receipt, {2u, true, bytes}, f.quotas[6]),
        f.diagnostic);
    assert(pipeline);
    auto pipelineSource = S::prepareOriginal(pipeline,
        {2u,
            true,
            S::kind,
            pipeline->originalResourceRegistration().generation(),
            bytes,
            S::exclusiveOwnerCount},
        f.quotas[7],
        *f.device,
        f.diagnostic);
    assert(pipelineSource);
    const auto& layout = f.layout->desc().descriptorLayouts.front();
    std::array<OriginalCommandDescriptorSetElement, 4u> elements;
    core::pmr::Vector<Sources::Source> prefix{&core::defaultCpuMemoryResource()};
    GpuBuffer* writable = nullptr;
    for (std::size_t i = 0u; i < elements.size(); ++i) {
        auto& e = elements[i];
        const auto& b = layout.bindings[i];
        e.write.binding = b.binding;
        e.write.type = b.type;
        e.write.resourceState = b.type == DescriptorType::StorageBuffer ? ResourceState::ShaderWrite
                                                                        : ResourceState::ShaderRead;
        e.stages = PipelineStage::ComputeShader;
        e.access = b.type == DescriptorType::StorageBuffer
            ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
            : ResourceAccess::ShaderRead;
        if (b.type == DescriptorType::SampledTexture) {
            TextureDesc td;
            td.extent = {4u, 4u, 1u};
            td.format = TextureFormat::D32Float;
            td.usage = TextureUsage::Sampled | TextureUsage::RenderAttachment;
            auto texture = f.device->createTexture(td);
            assert(texture);
            TextureViewDesc vd;
            vd.format = td.format;
            vd.usage = TextureUsage::Sampled;
            vd.aspect = TextureAspect::Depth;
            auto view = f.device->createTextureView(*texture, vd);
            assert(view);
            auto parent = OriginalCommandTextureLease::prepareOriginal(
                texture, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
            auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                view, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
            auto source = OriginalCommandTextureViewSource::prepareOriginal(
                lease, parent, {1u, true, 5u, bytes}, f.quotas[7], f.diagnostic);
            assert(source);
            prefix.push_back(parent);
            e.sourceIndex = prefix.size();
            prefix.push_back(source);
            e.write.resource = source->resource();
            e.write.resourceKind = DescriptorResourceKind::TextureView;
            e.registrationGeneration =
                source->originalObject()->originalResourceRegistration().generation();
        } else {
            shader::SpirvBufferDeclarationLayout declaration;
            require(shader::SingleEntryShaderArtifact::originalBufferDeclarationLayout(
                f.input->artifact(), 0u, b.binding, declaration));
            auto offset = b.type == DescriptorType::UniformBuffer
                ? runtime->descriptorIndexing().limits.minUniformBufferOffsetAlignment
                : runtime->descriptorIndexing().limits.minStorageBufferOffsetAlignment;
            if (declaration.hasRuntimeArray)
                offset = std::lcm(offset, declaration.runtimeArrayStrideBytes);
            const auto size = declaration.hasRuntimeArray
                ? declaration.runtimeArrayOffsetBytes + 2u * declaration.runtimeArrayStrideBytes
                : declaration.staticOccupiedExtentBytes;
            BufferDesc bd;
            bd.sizeBytes = offset + size;
            bd.usage = b.type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                               : ResourceUsage::StorageBuffer;
            bd.memory = MemoryUsage::CpuToGpu;
            bd.initialState = e.write.resourceState;
            auto buffer = f.device->createBuffer(bd);
            assert(buffer);
            core::pmr::Vector<std::byte> initialized(
                bd.sizeBytes, &core::defaultCpuMemoryResource());
            assert(f.device->writeBuffer(*buffer, initialized.data(), initialized.size()).success);
            if (b.type == DescriptorType::StorageBuffer)
                writable = buffer.get();
            else
                buffer->sealWrites();
            auto source = OriginalCommandBufferLease::prepareOriginal(
                buffer, {1u, true, 2u, bytes}, f.quotas[7], f.diagnostic);
            assert(source);
            e.sourceIndex = prefix.size();
            prefix.push_back(source);
            e.write.resource = source->resource();
            e.write.resourceKind = DescriptorResourceKind::Buffer;
            e.registrationGeneration =
                source->originalObject()->originalResourceRegistration().generation();
            e.write.bufferBindingRange = {2u, offset, size};
            e.bufferAccessOffset = offset;
            e.bufferAccessBytes = size;
        }
    }
    OriginalCommandAdmissionBudget budget;
    budget.contractVersion = 1u;
    budget.prototypeOptIn = true;
    budget.maxCommands = budget.maxReferences = budget.maxInputDescriptors = 16u;
    budget.maxStateObservations = budget.maxTextureRanges = 16u;
    budget.maxGraphOwners = S::cpuOwnerSlots + 4u * OriginalCommandBufferLease::retainedOwnerCount
        + OriginalCommandTextureLease::retainedOwnerCount
        + OriginalCommandTextureViewSource::completeRetainedOwnerCount
        + DescriptorSource::finiteExclusiveRetainedOwnerCount;
    budget.maxCpuBytes.fill(bytes);
    budget.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Buffer)] = 4u;
    for (const auto d : {CommandReferenceDomain::Texture,
             CommandReferenceDomain::TextureView,
             CommandReferenceDomain::Pipeline,
             CommandReferenceDomain::Descriptor})
        budget.maxSourceOwners[static_cast<std::size_t>(d)] = 1u;
    auto inventory = Sources::prepareColdOriginalTyped(
        prefix.data(), prefix.size(), budget, {8u, true, 2u}, f.quotas[7], f.diagnostic);
    require(f.diagnostic);
    assert(inventory);
    const auto pipelineIndex = prefix.size();
    require(Sources::appendOriginalTyped(inventory, pipelineSource, *f.device));
    Inspection::Observation observed;
    for (const auto& element : elements) {
        require(Inspection::inspect(
            {2u, true}, *f.device, inventory, pipelineIndex, 0u, element, observed));
        assert(observed.available && observed.pipeline && observed.layout
            && !observed.nativeExecutionProven());
        assert(Inspection::inspect(
                   {1u, true}, *f.device, inventory, pipelineIndex, 0u, element, observed)
            && !observed.available);
    }
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> quotas;
    for (auto& q : quotas)
        q = core::makeShared<core::CpuMemoryQuota>(bytes);
    std::shared_ptr<DescriptorArena> arena;
    core::UniquePtr<DescriptorSet> descriptor;
    {
        core::CpuMemoryQuotaScope allocation(quotas[0]);
        arena = core::shareUnique(
            f.device->createOriginalDescriptorArena({"Cloud.Compute.Atomic", 1u, 4u}));
        assert(arena);
        descriptor = f.device->allocateOriginalDescriptorSet(arena, {layout.name, layout});
        assert(descriptor);
    }
    const std::array writerQuotas{quotas[1], quotas[2], quotas[3]};
    OriginalGeneralDescriptorWriteRequest request;
    request.contractVersion = 4u;
    request.prototypeOptIn = true;
    request.maxBindings = elements.size();
    request.maxRetainedOwnerSlots = OriginalGeneralDescriptorWriteContract::ownerSlots(
        elements.size(), request.contractVersion);
    request.maxCpuBytes.fill(bytes);
    request.maxRetainedAllocationBytes = bytes;
    const auto publish = [&] {
        return f.device->beginOriginalGeneralDescriptorWrite(*descriptor,
            pipeline,
            inventory,
            0u,
            elements.data(),
            elements.size(),
            request,
            writerQuotas);
    };
    const auto revision = descriptor->contentRevision();
    const auto updates = NativeProbe::descriptorUpdates, views = NativeProbe::liveViews;
    const auto refused = [&] {
        std::array<std::uint64_t, 3u> baseline{};
        for (std::size_t i = 0u; i < writerQuotas.size(); ++i)
            baseline[i] = writerQuotas[i]->liveBytes();
        assert(publish() && descriptor->contentRevision() == revision
            && !descriptor->stats().writtenBindings && NativeProbe::descriptorUpdates == updates
            && NativeProbe::liveViews == views);
        for (std::size_t i = 0u; i < writerQuotas.size(); ++i)
            assert(writerQuotas[i]->liveBytes() == baseline[i]);
    };
    request.contractVersion = 3u;
    refused();
    request.contractVersion = 4u;
    const auto original = elements.back();
    elements.back().access = ResourceAccess::ShaderRead;
    refused();
    elements.back() = original;
    --elements.back().write.bufferBindingRange.sizeBytes;
    --elements.back().bufferAccessBytes;
    refused();
    elements.back() = original;
    elements.back().sourceIndex = elements.front().sourceIndex;
    refused();
    elements.back() = original;
    for (const auto& q : writerQuotas) {
        core::CpuMemoryQuotaLease pressure(q, bytes);
        assert(pressure.charged());
        refused();
    }
    auto* previousResource = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto* publicationDiagnostic = publish();
    std::pmr::set_default_resource(previousResource);
    require(publicationDiagnostic);
    std::array<std::uint64_t, 3u> exact;
    for (std::size_t i = 0u; i < exact.size(); ++i)
        exact[i] = writerQuotas[i]->liveBytes();
    require(descriptor->cancelOriginalGeneralWrite());
    for (std::size_t i = 0u; i < exact.size(); ++i)
        for (const auto available : {exact[i], exact[i] - 1u}) {
            core::CpuMemoryQuotaLease pressure(writerQuotas[i], bytes - available);
            assert(pressure.charged());
            const auto* id = publish();
            assert(bool(id) == (available != exact[i]));
            if (!id)
                require(descriptor->cancelOriginalGeneralWrite());
        }
    require(publish());
    require(test::CpuQuotaFactoryFixture::verifyHotNoAllocation(writerQuotas, [&]() noexcept {
        return descriptor->finishOriginalGeneralWrite();
    }));
    assert(descriptor->contentRevision() == revision + 1u
        && descriptor->originalGeneralContentContractVersion() == 4u
        && descriptor->stats().writtenBindings == elements.size()
        && NativeProbe::descriptorUpdates == updates + 1u);
    OriginalCommandDescriptorSourceBudget sb{
        7u, true, 4u, 4u, DescriptorSource::finiteExclusiveRetainedOwnerCount, bytes};
    sb.finitePipelineSourceIndex = pipelineIndex;
    sb.finiteFootprintContractVersion = 2u;
    sb.finiteFullBoundViewOptIn = true;
    auto legacy = sb;
    legacy.contractVersion = 5u;
    assert(!DescriptorSource::prepareOriginal(descriptor,
               arena,
               inventory,
               elements.data(),
               elements.size(),
               legacy,
               f.quotas[7],
               *f.device,
               f.diagnostic)
        && descriptor);
    auto ds = DescriptorSource::prepareOriginal(descriptor,
        arena,
        inventory,
        elements.data(),
        elements.size(),
        sb,
        f.quotas[7],
        *f.device,
        f.diagnostic);
    require(f.diagnostic);
    assert(ds && !descriptor);
    require(Sources::appendOriginalTyped(inventory, ds, *f.device));
    require(DescriptorSource::validateOriginal(ds, *f.device));
    receipt.reset();
    pipeline.reset();
    pipelineSource.reset();
    f.input.reset();
    f.layout.reset();
    for (auto& owner : prefix)
        owner = std::shared_ptr<const OriginalCommandBufferLease>{};
    require(DescriptorSource::validateOriginal(ds, *f.device));
    assert(writable);
    writable->sealWrites();
    assert(DescriptorSource::validateOriginal(ds, *f.device));
    std::printf(
        "ORIGINAL_CLOUD_COMPUTE_PUBLICATION_OK %s atomic4 Source7 assembly8 independent-quotas=%llu/%llu/%llu no-dispatch-claim\n",
        publication,
        static_cast<unsigned long long>(exact[0]),
        static_cast<unsigned long long>(exact[1]),
        static_cast<unsigned long long>(exact[2]));
}

void verifyCompute(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    using S = OriginalCommandComputePipelineSource;
    for (const auto* publication : {ARP_ORIGINAL_CLOUD_RAYMARCH,
             ARP_ORIGINAL_CLOUD_TEMPORAL,
             ARP_ORIGINAL_CLOUD_SPATIAL_RESOLVE}) {
        ComputeFixture f(runtime, publication);
        const std::array<unsigned, 5u> none{};
        const auto factory = [&] {
            return f.create();
        };
        assert(!OriginalSynchronousComputeCreationContract::validateProfile(f.authored));
        const auto targetBudget = f.budget.maxTargetBytes;
        --f.budget.maxTargetBytes;
        NativeProbe::ordinal = 0u;
        assert(!factory() && !NativeProbe::ordinal && NativeProbe::live == none);
        f.budget.maxTargetBytes = targetBudget;
        ++f.authored.computeShader.computeWorkgroupSizeX;
        assert(!factory() && !NativeProbe::ordinal);
        --f.authored.computeShader.computeWorkgroupSizeX;
        assert(!f.device->createOriginalSynchronousComputeCreation(
            f.input, f.layout, f.authored, f.budget, f.quotas[4], f.quotas[4], f.diagnostic));
        for (const auto* missing : {"vkCreateShaderModule",
                 "vkDestroyShaderModule",
                 "vkCreateDescriptorSetLayout",
                 "vkDestroyDescriptorSetLayout",
                 "vkCreatePipelineLayout",
                 "vkDestroyPipelineLayout",
                 "vkCreateComputePipelines",
                 "vkDestroyPipeline"}) {
            NativeProbe::missing = missing;
            NativeProbe::ordinal = 0u;
            assert(
                !factory() && f.diagnostic && !NativeProbe::ordinal && NativeProbe::live == none);
        }
        NativeProbe::missing = nullptr;
        test::CpuQuotaFactoryFixture owner(f.quotas[4]), native(f.quotas[5]);
        const auto ownerBytes = owner.measure(factory), nativeBytes = native.measure(factory);
        owner.verify(ownerBytes, factory);
        native.verify(nativeBytes, factory);
        NativeProbe::ordinal = 0u;
        auto receipt = factory();
        if (!receipt)
            std::fprintf(stderr, "original compute creation: %s\n", f.diagnostic);
        assert(receipt && !f.diagnostic && NativeProbe::ordinal == 4u);
        receipt.reset();
        assert(NativeProbe::live == none);
        for (unsigned prefix = 1u; prefix <= 4u; ++prefix) {
            NativeProbe::failAt = prefix;
            NativeProbe::ordinal = 0u;
            assert(!factory() && f.diagnostic && NativeProbe::ordinal == prefix
                && NativeProbe::live == none);
            assert(!f.quotas[4]->liveBytes() && !f.quotas[5]->liveBytes());
        }
        NativeProbe::failAt = 0u;
        NativeProbe::partialPipelineFailure = true;
        assert(!factory() && NativeProbe::live == none);
        NativeProbe::partialPipelineFailure = false;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        receipt = factory();
        std::pmr::set_default_resource(previous);
        assert(receipt);
        const auto accepted = [&] {
            return f.device->createOriginalArtifactComputePipeline(
                receipt, {2u, true, bytes}, f.quotas[6]);
        };
        test::CpuQuotaFactoryFixture wrapper(f.quotas[6]);
        const auto wrapperBytes = wrapper.measure(accepted);
        wrapper.verify(wrapperBytes, accepted);
        assert(!f.device->createOriginalArtifactComputePipeline(
            receipt, {1u, true, bytes}, f.quotas[6]));
        auto pipeline = OriginalRegisteredPipeline::shareAcceptedOriginal(accepted(), f.diagnostic);
        assert(pipeline && !pipeline->originalArtifactComputeDiagnostic());
        const OriginalCommandPipelineSourceBudget sourceBudget{2u,
            true,
            S::kind,
            pipeline->originalResourceRegistration().generation(),
            bytes,
            S::exclusiveOwnerCount};
        const auto sourceFactory = [&] {
            return S::prepareOriginal(pipeline, sourceBudget, f.quotas[7], *f.device, f.diagnostic);
        };
        test::CpuQuotaFactoryFixture sourceQuota(f.quotas[7]);
        const auto sourceBytes = sourceQuota.measure(sourceFactory);
        sourceQuota.verify(sourceBytes, sourceFactory);
        auto source = sourceFactory();
        assert(source && !source->nativeExecutionProven());
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(f.quotas, [&]() noexcept {
            return S::validateOriginal(source, *f.device);
        }));
        f.input.reset();
        f.layout.reset();
        pipeline.reset();
        receipt.reset();
        assert(!S::validateOriginal(source, *f.device));
        const auto payload = vulkanRhiNativePayload(*source->originalObject());
        assert(payload && payload->backendVulkanComputePipelineHandle()
            && payload->backendVulkanComputePipelineLayoutHandle());
        std::printf(
            "ORIGINAL_CLOUD_COMPUTE_CREATION_OK %s owner=%llu native=%llu wrapper=%llu source=%llu actual-native-payload no-dispatch-claim\n",
            publication,
            static_cast<unsigned long long>(ownerBytes),
            static_cast<unsigned long long>(nativeBytes),
            static_cast<unsigned long long>(wrapperBytes),
            static_cast<unsigned long long>(sourceBytes));
        f.device->retireOriginalDeviceLifetime();
        assert(S::validateOriginal(source, *f.device));
    }
    for (const auto* publication : {ARP_ORIGINAL_CLOUD_RAYMARCH, ARP_ORIGINAL_CLOUD_TEMPORAL})
        verifyComputePublication(runtime, publication);
    assert((NativeProbe::live == std::array<unsigned, 5u>{}));
    for (unsigned prefix = 1u; prefix <= 4u; ++prefix) {
        tests::VulkanHeadlessRuntime bootstrap(false, NativeProbe::resolve);
        assert(bootstrap.runtime() && bootstrap.runtime()->valid());
        ComputeFixture f(bootstrap.runtime(), ARP_ORIGINAL_CLOUD_RAYMARCH);
        const auto lifetime = f.device->originalDeviceLifetimeTicket();
        const auto registration = f.layout->originalResourceRegistration();
        NativeProbe::ordinal = 0u;
        NativeProbe::failAt = prefix;
        NativeProbe::failure = VK_ERROR_DEVICE_LOST;
        assert(!f.create() && f.diagnostic && NativeProbe::ordinal == prefix
            && lifetime.diagnostic() && registration.diagnostic()
            && (NativeProbe::live == std::array<unsigned, 5u>{}));
        NativeProbe::failAt = NativeProbe::ordinal = 0u;
        NativeProbe::failure = VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    std::puts(
        "ORIGINAL_CLOUD_COMPUTE_LOSS_PREFIXES_OK injected-native-results registry10 revocation cleanup");
}

void verifyFeatureAdmission()
{
    for (unsigned unavailable = 0u; unavailable < 3u; ++unavailable) {
        // Actual device enablement changes in case 0; case 1 withholds its
        // receipt; case 2 removes the runtime's real support query.
        tests::VulkanHeadlessRuntime bootstrap(false,
            NativeProbe::resolve,
            false,
            true,
            unavailable != 0u,
            unavailable != 1u,
            unavailable == 2u ? withoutFeatureQuery : vkGetInstanceProcAddr);
        assert(bootstrap.runtime() && bootstrap.runtime()->valid()
            && !bootstrap.runtime()->shaderDrawParametersEnabled());
        Fixture f(bootstrap.runtime());
        NativeProbe::ordinal = 0u;
        const auto before = NativeProbe::live;
        assert(!f.create() && f.diagnostic
            && !std::strcmp(f.diagnostic, "RHI-SYNC-GRAPHICS-CREATION-DRAW-PARAMETERS-UNAVAILABLE")
            && !NativeProbe::ordinal && NativeProbe::live == before);
    }
    std::puts(
        "Original graphics DrawParameters disabled/unreported/unqueried refusal PASS; zero native creates");
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0u);
    if (argc == 2
        && (!std::strcmp(argv[1], "--compute") || !std::strcmp(argv[1], "--compute-contract")
            || !std::strcmp(argv[1], "--compute-null-hidden-frame"))) {
        verifyComputeContract();
        if (!std::strcmp(argv[1], "--compute-contract"))
            return 0;
        if (!std::strcmp(argv[1], "--compute-null-hidden-frame")) {
            auto device = createNullDevice();
            NullHiddenFrameSmokeRunner runner(*device);
            const auto result = runner.run({});
            assert(result.success && result.submittedFrameCount == 2u);
            return 0;
        }
        tests::VulkanHeadlessRuntime bootstrap(false, NativeProbe::resolve);
        if (!bootstrap.runtime() || !bootstrap.runtime()->valid())
            return 77;
        verifyCompute(bootstrap.runtime());
        return 0;
    }
    verifyContract();
    if (argc == 2 && !std::strcmp(argv[1], "--contract"))
        return 0;
    if (argc == 2 && !std::strcmp(argv[1], "--null-hidden-frame")) {
        auto device = createNullDevice();
        NullHiddenFrameSmokeRunner runner(*device);
        const auto result = runner.run({});
        assert(result.success && result.submittedFrameCount == 2u);
        std::puts(
            "Original graphics creation Null hidden-frame PASS; no native creation/execution claim");
        return 0;
    }
    tests::VulkanHeadlessRuntime bootstrap(false, NativeProbe::resolve);
    if (!bootstrap.runtime() || !bootstrap.runtime()->valid())
        return 77;
    if (!bootstrap.runtime()->shaderDrawParametersEnabled()) {
        Fixture unavailable(bootstrap.runtime());
        NativeProbe::ordinal = 0u;
        assert(!unavailable.create() && unavailable.diagnostic && !NativeProbe::ordinal);
        std::puts(
            "Original graphics native success probe unavailable: DrawParameters not enabled; refusal verified");
        return 77;
    }
#if defined(ARP_CLOUD_FRAME_GRAPH_TEST)
    if (argc == 3
        && (!std::strcmp(argv[1], "--feature-owned-history")
            || !std::strcmp(argv[1], "--feature-owned-history-ortho")
            || !std::strcmp(argv[1], "--feature-owned-history-capture")
            || !std::strcmp(argv[1], "--feature-owned-history-ortho-capture"))) {
        const bool spatial = !std::strcmp(argv[2], "reconstruction");
        assert(spatial || !std::strcmp(argv[2], "temporal"));
        const bool ortho = std::strstr(argv[1], "-ortho") != nullptr;
        // Capture the same cloud work without the injected queue delay:
        // RenderDoc may drain the queue when collecting initial contents.
        const bool readerGate = std::strstr(argv[1], "-capture") == nullptr;
        verifyOrdinaryMixedCloud(bootstrap.runtime(),
            false,
            true,
            true,
            4u,
            true,
            true,
            readerGate,
            ortho,
            spatial ? CloudQualityFixture::MediumSpatial : CloudQualityFixture::Disabled,
            CloudDensityProbe::Valid,
            spatial,
            true,
            true);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "--feature-cold-pipelines")) {
        const bool spatial = !std::strcmp(argv[2], "reconstruction");
        assert(spatial || !std::strcmp(argv[2], "temporal"));
        verifyOrdinaryMixedCloud(bootstrap.runtime(),
            false,
            true,
            true,
            4u,
            true,
            true,
            false,
            false,
            spatial ? CloudQualityFixture::MediumSpatial : CloudQualityFixture::Disabled,
            CloudDensityProbe::Valid,
            false,
            true);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "--feature-density-range")) {
        const auto probe = !std::strcmp(argv[2], "ray-short") ? CloudDensityProbe::RayShort
            : !std::strcmp(argv[2], "resolve-short")          ? CloudDensityProbe::ResolveShort
            : !std::strcmp(argv[2], "resolve-offset")         ? CloudDensityProbe::ResolveOffset
                                                              : CloudDensityProbe::Valid;
        assert(probe != CloudDensityProbe::Valid);
        verifyOrdinaryMixedCloud(bootstrap.runtime(),
            false,
            true,
            true,
            4u,
            true,
            true,
            false,
            false,
            CloudQualityFixture::MediumSpatial,
            probe);
        return 0;
    }
    if (argc == 3
        && (!std::strcmp(argv[1], "--feature-spatial-quality")
            || !std::strcmp(argv[1], "--feature-spatial-motion"))) {
        const bool motion = !std::strcmp(argv[1], "--feature-spatial-motion");
        const bool ortho = motion && !std::strcmp(argv[2], "orthographic");
        const auto scene = !std::strcmp(argv[2], "inside") ? 5u
            : !std::strcmp(argv[2], "above")               ? 6u
                                                           : 4u;
        assert(motion ? (ortho || !std::strcmp(argv[2], "perspective"))
                      : (scene != 4u || !std::strcmp(argv[2], "below")));
        verifyOrdinaryMixedCloud(bootstrap.runtime(),
            false,
            true,
            true,
            scene,
            true,
            true,
            false,
            ortho,
            CloudQualityFixture::MediumSpatial,
            CloudDensityProbe::Valid,
            motion);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "--feature-quality")) {
        const auto quality = !std::strcmp(argv[2], "reference") ? CloudQualityFixture::Reference
            : !std::strcmp(argv[2], "high")                     ? CloudQualityFixture::High
            : !std::strcmp(argv[2], "medium-full-probe") ? CloudQualityFixture::MediumFullProbe
            : !std::strcmp(argv[2], "medium-spatial")    ? CloudQualityFixture::MediumSpatial
                                                         : CloudQualityFixture::Medium;
        assert(quality != CloudQualityFixture::Medium || !std::strcmp(argv[2], "medium"));
        verifyOrdinaryMixedCloud(
            bootstrap.runtime(), false, true, true, 4u, true, true, false, false, quality);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "--feature-moving-occlusion")) {
        const bool orthographic = !std::strcmp(argv[2], "orthographic");
        assert(orthographic || !std::strcmp(argv[2], "perspective"));
        verifyOrdinaryMixedCloud(
            bootstrap.runtime(), false, true, true, 4u, true, true, false, orthographic);
        return 0;
    }
    if (argc == 2
        && (!std::strcmp(argv[1], "--feature-history")
            || !std::strcmp(argv[1], "--feature-history-capture"))) {
        // RenderDoc can synchronously drain submission while capturing initial
        // contents. Keep the explicit GPU-delay probe in the native test mode;
        // capture mode exercises identical renderer work without that fixture
        // delay. Both modes remain independently verified and named in evidence.
        const bool readerGate = !std::strcmp(argv[1], "--feature-history");
        std::printf("ORDINARY_HISTORY_MODE readerGate=%u\n", unsigned(readerGate));
        verifyOrdinaryMixedCloud(
            bootstrap.runtime(), false, true, true, 4u, false, true, readerGate);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "--feature-foreground")) {
        const auto cloudCase = unsigned(std::strtoul(argv[2], nullptr, 10));
        assert(cloudCase >= 4u && cloudCase <= 6u);
        verifyOrdinaryMixedCloud(bootstrap.runtime(), false, true, true, cloudCase, true);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "--feature-cloud")) {
        const auto cloudCase = unsigned(std::strtoul(argv[2], nullptr, 10));
        assert(cloudCase >= 1u && cloudCase <= 6u);
        verifyOrdinaryMixedCloud(bootstrap.runtime(), false, true, true, cloudCase);
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--feature-graph")) {
        verifyOrdinaryMixedCloud(bootstrap.runtime(), false, true, true);
        return 0;
    }
#endif
    if (argc == 2 && !std::strcmp(argv[1], "--mixed-device")) {
        verifyOrdinaryMixedCloud(bootstrap.runtime(), false, true);
        return 0;
    }
    if (argc == 2
        && (!std::strcmp(argv[1], "--mixed") || !std::strcmp(argv[1], "--mixed-recording"))) {
        verifyOrdinaryMixedCloud(bootstrap.runtime(), !std::strcmp(argv[1], "--mixed"));
        return 0;
    }
    verifyNative(bootstrap.runtime());
    verifyBindingInspection(bootstrap.runtime());
    verifyFeatureAdmission();
    verifyLossPrefixes();
    assert((NativeProbe::live == std::array<unsigned, 5u>{}));
}
