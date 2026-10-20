#include "rhi/backends/webgpu/webgpu_device.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "rhi/backends/null/dummy_sync.h"
#include "rhi/backends/webgpu/webgpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_bindless_preparation.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_issuer_preparation.h"
#include "rhi/backends/webgpu/webgpu_conformance_clear.h"
#include "rhi/backends/webgpu/webgpu_native_indirect_processor.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_original_geometry_content.h"
#include "rhi/backends/webgpu/webgpu_original_observed_native_loss.h"
#include "rhi/backends/webgpu/webgpu_owned_texture_readback_context.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/backends/webgpu/webgpu_query_pool.h"
#include "rhi/backends/webgpu/webgpu_staging_upload_pool.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/bindless_indirect_packet.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_recorded_state.h"
#include "rhi/frontend/device_validation.h"
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "rhi/semantics/fallback_plan.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_engine_fallback_bridge.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"
#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"

#include <iostream>
#include <limits>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {

namespace {

WebGpuPushConstantBudget resolvePushConstantBudget(const WebGpuRhiDeviceDesc& desc)
{
    WebGpuPushConstantBudget budget;
    budget.declaredWorkloadByteBudget = desc.pushConstantDeclaredWorkloadByteBudget != 0u
        ? desc.pushConstantDeclaredWorkloadByteBudget
        : desc.pushConstantUploadRingByteCapacity;
    budget.uniformOffsetAlignment = desc.runtimeProfile.limits.uniformBufferOffsetAlignment;
    if (budget.declaredWorkloadByteBudget == 0u || budget.uniformOffsetAlignment == 0u
        || (budget.uniformOffsetAlignment & (budget.uniformOffsetAlignment - 1u)) != 0u) {
        return budget;
    }
    const auto alignment = static_cast<std::uint64_t>(budget.uniformOffsetAlignment);
    const auto requested = static_cast<std::uint64_t>(budget.declaredWorkloadByteBudget);
    const auto rounded = (requested + alignment - 1u) & ~(alignment - 1u);
    if (rounded > std::numeric_limits<std::uint32_t>::max()) {
        budget.ringByteCapacity = 0u;
        return budget;
    }
    budget.ringByteCapacity = static_cast<std::uint32_t>(rounded);
    return budget;
}

bool isRuntimePromotedFromProbe(const WebGpuRhiDeviceDesc& desc)
{
    return desc.runtimeProfile.status == rhi::WebGpuRuntimeProfileStatus::PlatformValidated
        && desc.capabilityContract.queriedFromNativeDevice
        && desc.capabilityContract.queriedExtendedFeatures;
}

bool hasFeature(const rhi::semantics::NativeBackendCapabilityContract& contract,
    rhi::semantics::NativeBackendFeature feature)
{
    return rhi::semantics::hasNativeFeature(contract, feature);
}

bool hasResourceUsage(rhi::ResourceUsage usage, rhi::ResourceUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

bool hasTextureUsage(rhi::TextureUsage usage, rhi::TextureUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

bool validateWebGpuBufferCapability(const rhi::BufferDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    std::pmr::string& diagnostic)
{
    if (hasResourceUsage(desc.usage, rhi::ResourceUsage::StorageBuffer)
        && !hasFeature(contract, rhi::semantics::NativeBackendFeature::StorageBufferBinding)) {
        diagnostic = "WebGPU RHI storage-buffer creation requires "
                     "StorageBufferBinding capability";
        return false;
    }
    return true;
}

bool validateWebGpuTextureCapability(const rhi::TextureDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    std::pmr::string& diagnostic)
{
    const bool blockCompressed = desc.format == rhi::TextureFormat::Bc1Unorm
        || desc.format == rhi::TextureFormat::Bc3Unorm
        || desc.format == rhi::TextureFormat::Bc7Unorm
        || desc.format == rhi::TextureFormat::Bc7UnormSrgb;
    if (blockCompressed && hasTextureUsage(desc.usage, rhi::TextureUsage::Storage)) {
        diagnostic = "WebGPU RHI block-compressed textures cannot use "
                     "Storage usage";
        return false;
    }
    if (blockCompressed
        && !hasFeature(contract, rhi::semantics::NativeBackendFeature::TextureCompressionBc)) {
        diagnostic = "WebGPU RHI BC texture creation requires "
                     "TextureCompressionBc capability";
        return false;
    }
    if (!hasTextureUsage(desc.usage, rhi::TextureUsage::Storage)) {
        return true;
    }
    if (!hasFeature(contract, rhi::semantics::NativeBackendFeature::StorageTextureBinding)) {
        diagnostic = "WebGPU RHI storage-texture creation requires "
                     "StorageTextureBinding capability";
        return false;
    }
    if (desc.format == rhi::TextureFormat::Rg16Float
        && !hasFeature(contract, rhi::semantics::NativeBackendFeature::Rg16FloatStorageTexture)) {
        diagnostic = "WebGPU RHI Rg16Float storage-texture creation requires "
                     "Rg16FloatStorageTexture capability";
        return false;
    }
    if (desc.format == rhi::TextureFormat::Rgba16Float
        && !hasFeature(contract, rhi::semantics::NativeBackendFeature::Rgba16FloatStorageTexture)) {
        diagnostic = "WebGPU RHI Rgba16Float storage-texture creation requires "
                     "Rgba16FloatStorageTexture capability";
        return false;
    }
    return true;
}

bool validateWebGpuCapabilityContract(const WebGpuRhiDeviceDesc& desc, std::pmr::string& diagnostic)
{
    const auto& profile = desc.runtimeProfile;
    const auto& contract = desc.capabilityContract;
    const bool platformValidated =
        profile.status == rhi::WebGpuRuntimeProfileStatus::PlatformValidated;

    const auto expected = [platformValidated](bool profileFact) {
        return platformValidated && profileFact;
    };
    const auto requireExact =
        [&](rhi::semantics::NativeBackendFeature feature, bool profileFact, const char* label) {
            const bool contractFact = hasFeature(contract, feature);
            if (contractFact != expected(profileFact)) {
                diagnostic = "WebGPU RHI capability contract mismatch for ";
                diagnostic += label;
                return false;
            }
            return true;
        };

    if (!requireExact(rhi::semantics::NativeBackendFeature::ComputeDispatch,
            profile.optionalFeatures.computeDispatch,
            "ComputeDispatch")
        || !requireExact(rhi::semantics::NativeBackendFeature::StorageTextureBinding,
            profile.optionalFeatures.storageTextureBinding,
            "StorageTextureBinding")
        || !requireExact(rhi::semantics::NativeBackendFeature::Rg16FloatStorageTexture,
            profile.optionalFeatures.rg16FloatStorage,
            "Rg16FloatStorageTexture")
        || !requireExact(rhi::semantics::NativeBackendFeature::Rgba16FloatStorageTexture,
            profile.optionalFeatures.rgba16FloatStorage,
            "Rgba16FloatStorageTexture")
        || !requireExact(rhi::semantics::NativeBackendFeature::StorageBufferBinding,
            profile.optionalFeatures.storageBuffer,
            "StorageBufferBinding")
        || !requireExact(rhi::semantics::NativeBackendFeature::TextureCompressionBc,
            profile.optionalFeatures.textureCompressionBc,
            "TextureCompressionBc")
        || !requireExact(rhi::semantics::NativeBackendFeature::SceneGeometryRayQuery,
            profile.optionalFeatures.sceneGeometryRayQuery,
            "SceneGeometryRayQuery")
        || !requireExact(rhi::semantics::NativeBackendFeature::TimestampQueries,
            profile.optionalFeatures.timestampQuery,
            "TimestampQueries")
        || !requireExact(rhi::semantics::NativeBackendFeature::IndirectDraw, true, "IndirectDraw")
        || !requireExact(
            rhi::semantics::NativeBackendFeature::DescriptorIndexing, false, "DescriptorIndexing")
        || !requireExact(rhi::semantics::NativeBackendFeature::SeparatePassSubpassInputAttachment,
            true,
            "SeparatePassSubpassInputAttachment")
        || !requireExact(
            rhi::semantics::NativeBackendFeature::RenderBundles, true, "RenderBundles")) {
        return false;
    }

    for (const auto feature : contract.optionalFeatures) {
        if (feature != rhi::semantics::NativeBackendFeature::TimestampQueries
            || !hasFeature(contract, feature)) {
            diagnostic = "WebGPU RHI capability contract contains an invalid "
                         "optional feature declaration";
            return false;
        }
    }

    for (const auto feature : contract.features) {
        const bool allowed = feature == rhi::semantics::NativeBackendFeature::RuntimeLimits
            || feature == rhi::semantics::NativeBackendFeature::OptionalRuntimeFeatures
            || feature == rhi::semantics::NativeBackendFeature::ComputeDispatch
            || feature == rhi::semantics::NativeBackendFeature::StorageTextureBinding
            || feature == rhi::semantics::NativeBackendFeature::Rg16FloatStorageTexture
            || feature == rhi::semantics::NativeBackendFeature::Rgba16FloatStorageTexture
            || feature == rhi::semantics::NativeBackendFeature::StorageBufferBinding
            || feature == rhi::semantics::NativeBackendFeature::TextureCompressionBc
            || feature == rhi::semantics::NativeBackendFeature::SceneGeometryRayQuery
            || feature == rhi::semantics::NativeBackendFeature::IndirectDraw
            || feature == rhi::semantics::NativeBackendFeature::DescriptorIndexing
            || feature == rhi::semantics::NativeBackendFeature::SeparatePassSubpassInputAttachment
            || feature == rhi::semantics::NativeBackendFeature::TimestampQueries
            || feature == rhi::semantics::NativeBackendFeature::RenderBundles;
        if (!allowed) {
            diagnostic = "WebGPU RHI capability contract contains unsupported "
                         "native feature";
            return false;
        }
    }
    return true;
}

void publishWebGpuDeviceDiagnostic(const char* kind, const char* detail)
{
#if defined(__EMSCRIPTEN__)
    EM_ASM(
        {
            // clang-format off
            if (typeof document !== 'undefined' && document.body) {
                document.body.dataset.woccWebgpuResourceDiagnostic =
                    UTF8ToString($0) + ':' + UTF8ToString($1);
            }
            // clang-format on
        },
        kind,
        detail);
#else
    (void)kind;
    (void)detail;
#endif
}

WebGpuRhiStagingUploadPoolDesc uploadStagingPoolDescFor(const WebGpuRhiDeviceDesc& desc)
{
    // Transfer staging has its own workload-owned bounded budget and must not
    // reuse the push-constant ring capacity. Zero remains zero: the backend
    // must not invent a transfer budget from policy defaults.
    return {
        desc.label.empty() ? "WebGpuUploadStaging" : desc.label + "UploadStaging",
        desc.uploadStagingByteCapacity,
        // Upload row pitches and buffer offsets use the WebGPU 256-byte
        // alignment contract.
        256,
        true,
    };
}

platform::PlatformLifecycleEvent platformEventFor(WebGpuRhiCanvasEvent event)
{
    switch (event) {
    case WebGpuRhiCanvasEvent::Resize:
        return platform::PlatformLifecycleEvent::Resize;
    case WebGpuRhiCanvasEvent::SurfaceLost:
        return platform::PlatformLifecycleEvent::SurfaceLost;
    case WebGpuRhiCanvasEvent::HiddenTab:
        return platform::PlatformLifecycleEvent::EnterBackground;
    case WebGpuRhiCanvasEvent::VisibleTab:
        return platform::PlatformLifecycleEvent::EnterForeground;
    }
    return platform::PlatformLifecycleEvent::Resize;
}

class WebGpuRhiQueue final : public rhi::GpuQueue {

public:
    rhi::GpuQueueRole role() const override { return rhi::GpuQueueRole::Graphics; }

    explicit WebGpuRhiQueue(std::shared_ptr<WebGpuNativeCommandStats> commandStats)
        : commandStats_(std::move(commandStats))
    {}

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& commandBuffer) override
    {
        return submit(commandBuffer, nullptr, 0u, nullptr, 0u, nullptr);
    }

    rhi::QueueSubmitResult submit(
        const rhi::CommandBuffer& commandBuffer, rhi::GpuFence* signalFence) override
    {
        return submit(commandBuffer, nullptr, 0u, nullptr, 0u, signalFence);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& commandBuffer,
        rhi::GpuTimelineSemaphore* waitSemaphore,
        uint64_t waitValue,
        rhi::GpuTimelineSemaphore* signalSemaphore,
        uint64_t signalValue,
        rhi::GpuFence* signalFence) override
    {
        if (signalFence)
            return {false, "RHI-WEBGPU-PUBLIC-FENCE-RESOURCE-GRAPH-UNAVAILABLE", 0};
        // WebGPU core has no queue-side timeline wait/signal primitive. A
        // callback completion state cannot satisfy the L2 ordering contract;
        // reject both timeline directions before native submission.
        if (waitSemaphore || signalSemaphore) {
            (void)waitValue;
            (void)signalValue;
            return {false, "WebGPU RHI timeline synchronization is unsupported by WebGPU core", 0};
        }
        // No original general command graph/envelope exists yet. Reject before
        // fence reset, native work or recorded ownership/counter mutation.
        return {
            false, webGpuGenericCompletionResourceGraphDiagnostic, commandBuffer.commandCount()};
    }

    std::uint32_t submitCount() const override { return submitCount_; }

private:
    std::shared_ptr<WebGpuNativeCommandStats> commandStats_;
    std::uint32_t submitCount_ = 0;
};

class WebGpuTextureReadbackExtensionImpl final
    : public rhi::RetiredTextureReadbackExtension<rhi::Backend::WebGPU> {};

class WebGpuBindlessCrossExtension final : public rhi::BindlessCrossExtension {
public:
    explicit WebGpuBindlessCrossExtension(const WebGpuNativeDeviceLimits& limits,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        std::uint32_t pendingSubmissionBudget,
        WebGpuRhiAllocator& allocator)
        : registry_(std::move(registry)), allocator_(allocator)
    {
        caps_.deviceIdentity = rhi::allocateGpuResourceId().value;
        const bool completeLimits = limits.maxDescriptorBindings != 0u && limits.maxBindGroups != 0u
            && limits.maxSampledTexturesPerShaderStage != 0u
            && limits.maxSamplersPerShaderStage != 0u
            && limits.maxStorageTexturesPerShaderStage != 0u
            && limits.maxUniformBuffersPerShaderStage != 0u
            && limits.maxStorageBuffersPerShaderStage != 0u
            && limits.maxUniformBufferBindingSize != 0u && limits.maxStorageBufferBindingSize != 0u;
        if (!completeLimits)
            return;

        caps_.limitsQueried = true;
        caps_.indirectFirstInstance = webGpuNativeDeviceSupportsIndirectFirstInstance();
        // Queried limits are planning facts. Public execution also needs the
        // complete generic command ownership/completion graph, which is absent.
        caps_.execution = rhi::BindlessExecution::DiagnosticFailure;
        caps_.executablePayload = false;
        // Independent buffer Compute execution consumes OriginalRootProgram/1
        // through Lifecycle11. The full scene execution gate stays closed.
        if (webGpuNativeDeviceAvailable() && !webGpuNativeDeviceLost()) {
            if (limits.uniformBufferOffsetAlignment && limits.maxUniformBufferBindingSize
                && limits.maxUniformBuffersPerShaderStage)
                caps_.originalGraphicsPushContractVersion = 1u;
            caps_.originalPreparationContractVersion = 1u;
            caps_.originalGraphicsPreparationContractVersion = 2u;
            caps_.originalSampledGraphicsPreparationContractVersion = 3u;
            // Snapshot ownership only; /4 cannot authorize a depth pipeline.
            caps_.originalDepthSnapshotPreparationContractVersion = 4u;
            caps_.originalDepthRootPreparationContractVersion = 5u;
            // WebGPU core rgba16float is sampled, renderable and WriteOnly
            // storage-capable. This publishes cold metadata only; the queried
            // complete limits above still constrain every physical binding.
            caps_.originalStoragePreparationContractVersion = 7u;
            caps_.originalStorageRootPreparationContractVersion = 8u;
            caps_.originalMixedImageSnapshotPreparationContractVersion = 9u;
            caps_.originalMixedImageRootPreparationContractVersion = 10u;
            caps_.originalOrdinaryColorGraphicsPreparationContractVersion = 11u;
            // WebGPU core depth2D/comparison declarations use queried limits.
            // Cold public pipeline preparation grants no Root or execution.
            caps_.originalOrdinaryDepthGraphicsPreparationContractVersion = 12u;
            caps_.preparedGraphicsBindingContractVersion = 1u;
            if (pendingSubmissionBudget) {
                caps_.originalExecutionContractVersion = 1u;
                caps_.originalFragmentGraphicsExecutionContractVersion = 2u;
                caps_.originalSampledFragmentGraphicsExecutionContractVersion = 3u;
                caps_.originalArrayFragmentGraphicsExecutionContractVersion = 4u;
                // WebGPU core vertex/index draws compose only when the
                // caller supplies authentic initialized actors and Geometry/1.
                caps_.originalInitializedGeometryExecutionContractVersion = 1u;
                // Independent canonical whole-upload ordinary color profile.
                // It retains generic ordinary-image refusal and requires the
                // original Graphics callback completion pool and Closure18.
                caps_.originalOrdinaryColorGraphicsExecutionContractVersion = 13u;
                caps_.originalOrdinaryDepthGraphicsExecutionContractVersion = 14u;
                caps_.originalDepthComputeExecutionContractVersion = 5u;
                caps_.originalEmulatedStorageComputeExecutionContractVersion = 10u;
                if (caps_.originalMixedImageRootPreparationContractVersion == 10u)
                    caps_.originalEmulatedMixedComputeExecutionContractVersion = 12u;
                caps_.faultReadbackContractVersion = 1u;
            }
            caps_.preparedComputeBindingContractVersion = 1u;
            // Scalar buffer domains use queried stage/binding limits and the
            // original Root/2 closure. No ordinary image execution is granted.
            caps_.ordinaryDescriptorDomains[3u] = true;
            caps_.ordinaryDescriptorDomains[4u] = true;
            caps_.ordinaryDescriptorDomains[5u] = true;
        }
        (void)pendingSubmissionBudget;
        caps_.maxBindingsPerGroup = limits.maxDescriptorBindings;
        caps_.maxBindGroups = limits.maxBindGroups;
        caps_.maxUniformBufferBindingSize = limits.maxUniformBufferBindingSize;
        caps_.maxStorageBufferBindingSize = limits.maxStorageBufferBindingSize;
        caps_.uniformBufferOffsetAlignment = limits.uniformBufferOffsetAlignment;
        caps_.storageBufferOffsetAlignment = limits.storageBufferOffsetAlignment;
        caps_.workloadResourceLimits = {1u,
            limits.maxBufferSize,
            limits.maxUniformBufferBindingSize,
            limits.maxStorageBufferBindingSize,
            limits.uniformBufferOffsetAlignment,
            limits.storageBufferOffsetAlignment,
            limits.maxTexture2DSize,
            limits.maxTexture3DSize,
            limits.maxTextureArrayLayers};
        for (auto& stage : caps_.stageLimits) {
            stage.sampledTextures = limits.maxSampledTexturesPerShaderStage;
            stage.samplers = limits.maxSamplersPerShaderStage;
            stage.storageTextures = limits.maxStorageTexturesPerShaderStage;
            stage.uniformBuffers = limits.maxUniformBuffersPerShaderStage;
            stage.storageBuffers = limits.maxStorageBuffersPerShaderStage;
        }

        // Publish only operations exercised through the public snapshot,
        // authorization, pass, queue completion and readback contracts.
        using rhi::BindlessOperation;
        caps_.support[2][0] = {
            BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel,
            true};
        caps_.support[2][1] = {BindlessOperation::None, true};
        caps_.support[2][2] = {
            BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::Store, true};
        caps_.support[2][3] = {BindlessOperation::Load, true};
        caps_.support[2][4] = {BindlessOperation::Load | BindlessOperation::Query, true};
        caps_.support[2][5] = {BindlessOperation::Load | BindlessOperation::Query
                | BindlessOperation::Store | BindlessOperation::Atomic,
            true};
        caps_.support[1][0] = {BindlessOperation::Load | BindlessOperation::Query
                | BindlessOperation::SampleLevel | BindlessOperation::SampleGrad,
            true};
        caps_.support[1][1] = {BindlessOperation::None, true};
        caps_.support[1][3] = {BindlessOperation::Load, true};
        caps_.support[1][4] = {BindlessOperation::Load | BindlessOperation::Query, true};
        caps_.support[1][5] = {BindlessOperation::Load | BindlessOperation::Query
                | BindlessOperation::Store | BindlessOperation::Atomic,
            true};
    }

    const rhi::BindlessCapabilities& capabilities() const noexcept override { return caps_; }

    std::shared_ptr<const rhi::OriginalInitializedGeometrySource>
    prepareOriginalInitializedBufferPayload(rhi::GpuDevice& device,
        const rhi::OriginalInitializedBufferRequest& request,
        const char*& diagnostic) const override
    {
        return WebGpuOriginalGeometryContent::prepareOriginal(device,
            request.desc,
            request.bytes,
            request.byteCount,
            request.budget,
            request.quotas,
            diagnostic);
    }

    std::shared_ptr<const rhi::OriginalBindlessRootExecution> prepareOriginalRootExecutionPayload(
        rhi::GpuDevice& device,
        const rhi::OriginalBindlessRootExecutionInputs& inputs,
        const rhi::OriginalBindlessRootExecutionBudget& budget,
        core::pmr::String& diagnostic) const override
    {
        const char* id = nullptr;
        auto fail = [&]() -> std::shared_ptr<const rhi::OriginalBindlessRootExecution> {
            diagnostic = id;
            return {};
        };
        // Join the same inventory/transaction/result. Every native object stays
        // behind this public extension and the original completion pool.
        const auto prepareClosure = [&](const auto& root, const char*& failure) {
            core::CpuMemoryQuotaScope nativeScope(inputs.nativeQuota);
            const auto& limits = inputs.roots.closureBudget(root, budget.closure);
            const auto selected = inputs.roots.initializedSources(root,
                {inputs.geometry,
                    inputs.geometryCount,
                    inputs.initializedShader,
                    inputs.initializedShaderCount});
            core::pmr::Vector<std::shared_ptr<const WebGpuOriginalGeometryContent>> geometry{
                &core::defaultCpuMemoryResource()};
            core::pmr::Vector<std::shared_ptr<const WebGpuOriginalGeometryContent>> shader{
                &core::defaultCpuMemoryResource()};
            if ((failure = rhi::OriginalBindlessInitializedSourcesContract::project(
                     limits, selected, geometry, shader)))
                return std::shared_ptr<const WebGpuBindlessRootNativeClosure>{};
            return WebGpuBindlessRootNativeClosure::prepareOriginal(root,
                inputs.inventory,
                device,
                limits,
                inputs.nativeQuota,
                failure,
                {geometry.data(), geometry.size(), shader.data(), shader.size()});
        };
        using Sequence = rhi::OriginalBindlessRootNativeSequence<WebGpuBindlessRootNativeClosure>;
        std::array<Sequence::Owner, 3u> historical;
        try {
            if ((id = Sequence::prepareHistoricalInput(
                     {inputs.root, inputs.nextRoot, inputs.finalRoot}, historical, prepareClosure)))
                return fail();
        } catch (const std::bad_alloc&) {
            id = "RHI-WEBGPU-ROOT-GEOMETRY-ADAPTER-CAPACITY";
            return fail();
        } catch (const std::length_error&) {
            id = "RHI-WEBGPU-ROOT-GEOMETRY-ADAPTER-CAPACITY";
            return fail();
        }
        const auto& closure = historical[0u];
        const auto& next = historical[1u];
        const auto& final = historical[2u];
        core::pmr::Vector<Sequence::Owner> closures{&core::defaultCpuMemoryResource()};
        try {
            core::CpuMemoryQuotaScope scope(inputs.nativeQuota);
            if ((id = Sequence::preparePublicInput(inputs.roots,
                     {closure, next, final},
                     inputs.inventory,
                     inputs.readback->originalProgram(),
                     closures,
                     prepareClosure)))
                return fail();
        } catch (const std::bad_alloc&) {
            id = "RHI-WEBGPU-ROOT-SEQUENCE-ADAPTER-CAPACITY";
            return fail();
        } catch (const std::length_error&) {
            id = "RHI-WEBGPU-ROOT-SEQUENCE-ADAPTER-CAPACITY";
            return fail();
        }
        const auto nativeRoots = inputs.roots.empty()
            ? Sequence::Input{}
            : Sequence::Input{closures.data(), closures.size(), inputs.roots.budget};
        if ((id = inputs.producedGeometry.inspectNative(closures.data(),
                 closures.size(),
                 inputs.transaction->originalGraph(),
                 inputs.readback->originalProgram(),
                 inputs.readback,
                 inputs.readbacks,
                 webGpuNativeDeviceLimits().maxComputeWorkgroupsPerDimension,
                 capabilities().indirectFirstInstance)))
            return fail();
        if (inputs.transaction->hasOriginalBindlessProducedGeometryProfile()) {
            const auto program = rhi::OriginalBindlessProducedProgramContract::inspectNative<
                WebGpuBindlessRootNativeClosure>(inputs.readback->originalProgram(),
                nativeRoots,
                inputs.readback,
                inputs.readbacks,
                inputs.producedGeometry.commands);
            if ((id = program.diagnostic))
                return fail();
        }
        WebGpuBindlessRootColdAdmission::Requirements required;
        if ((id = WebGpuBindlessRootColdAdmission::deriveRequirements(closure,
                 inputs.transaction,
                 inputs.readback,
                 budget.admission,
                 required,
                 next,
                 final,
                 inputs.readbacks,
                 nativeRoots)))
            return fail();
        if ((id = WebGpuNativeCommandPreparation::preflightBindlessRootPushUpload(inputs, budget)))
            return fail();
        auto seal = rhi::OriginalCommandRecordingSeal::prepareOriginal(inputs.transaction,
            device,
            budget.lifecycle,
            inputs.submissionQuota,
            inputs.callbackQuota,
            id);
        if (!seal)
            return fail();
        auto reservation =
            rhi::OriginalCommandLifecycleReservation::prepareOriginal(seal, required.capacity, id);
        if (!reservation)
            return fail();
        auto admission = WebGpuBindlessRootColdAdmission::prepareOriginal(closure,
            reservation,
            inputs.readback,
            device,
            budget.admission,
            id,
            next,
            final,
            inputs.readbacks,
            nativeRoots,
            inputs.producedGeometry,
            inputs.scene);
        if (!admission)
            return fail();
        auto storage = WebGpuCanonicalReadbackResultStorage::prepareOriginalBuffer(
            admission, device, budget.result, id);
        if (!storage)
            return fail();
        auto command = WebGpuBindlessRootColdCommand::prepareOriginal(
            admission, storage, budget.command, id, &allocator_);
        if (!command)
            return fail();
        auto custody =
            WebGpuBindlessRootCallbackCustody::prepareOriginal(command, budget.custody, id);
        if (!custody)
            return fail();
        auto issuers =
            WebGpuBindlessRootIssuerPreparation::prepareOriginal(custody, budget.issuers, id);
        if (!issuers)
            return fail();
        return issuers;
    }

    const std::shared_ptr<WebGpuProducerCompletionOwner>& processorCompletion() const noexcept
    {
        return processorCompletion_;
    }

    core::UniquePtr<rhi::BindlessIndirectProcessorPool> createIndirectProcessorPool(
        rhi::GpuDevice& device,
        const rhi::BindlessIndirectProcessorBudget& budget,
        core::pmr::String& diagnostic) const override
    {
        diagnostic.clear();
        if (device.queryInterface(rhi::kExtensionBindlessCross) != this) {
            diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-DEVICE-MISMATCH";
            return {};
        }
        if (caps_.zeroBaseIndirectProducerContractVersion != 1u || !processorCompletion_
            || !webGpuNativeDeviceAvailable()) {
            diagnostic = "RHI-BINDLESS-INDIRECT-PROCESSOR-EXECUTION-UNAVAILABLE";
            return {};
        }
        if (!budget.maxProcessors || !budget.maxBytesPerProcessor || !budget.maxResidentBytes
            || budget.maxBytesPerProcessor > budget.maxResidentBytes) {
            diagnostic = "RHI-WEBGPU-INDIRECT-PROCESSOR-BUDGET";
            return {};
        }
        return core::makeUnique<WebGpuRhiIndirectProcessorPool>(device, budget);
    }

    core::UniquePtr<rhi::ComputePipeline> createComputePipeline(
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        std::string_view entry,
        core::pmr::String& diagnostic) const override
    {
        diagnostic.clear();
        if (!authorization || !authorization->issuedBy(this, authorization)
            || authorization->plan().execution != rhi::BindlessExecution::Emulated
            || (!authorization->plan().executable
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization->plan(), 1u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization->plan(), 5u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization->plan(), 8u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageResourcePlan(
                    authorization->plan()))
            || authorization->plan().deviceIdentity != caps_.deviceIdentity
            || authorization->plan().shaderPackageIdentity
                != authorization->package().artifact().shaderPackageIdentity) {
            diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
            return {};
        }
        return createWebGpuRhiAuthorizedBindlessComputePipeline(
            std::move(authorization), entry, diagnostic);
    }

    core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        const rhi::RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        core::pmr::String& diagnostic) const override
    {
        return createRenderPipelineImpl(
            std::move(authorization), state, vertexEntry, fragmentEntry, diagnostic, nullptr);
    }

    core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        const rhi::RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        const rhi::BindlessGraphicsPushRequest& push,
        core::pmr::String& diagnostic) const override
    {
        if (const auto* id = push.diagnostic()) {
            diagnostic = id;
            return {};
        }
        if (caps_.originalGraphicsPushContractVersion != 1u) {
            diagnostic = "RHI-WEBGPU-BINDLESS-GRAPHICS-PUSH-UNAVAILABLE";
            return {};
        }
        return createRenderPipelineImpl(
            std::move(authorization), state, vertexEntry, fragmentEntry, diagnostic, &push);
    }

private:
    core::UniquePtr<rhi::RenderPipeline> createRenderPipelineImpl(
        std::shared_ptr<const rhi::BindlessAuthorization> authorization,
        const rhi::RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        core::pmr::String& diagnostic,
        const rhi::BindlessGraphicsPushRequest* push) const
    {
        diagnostic.clear();
        if (!authorization || !authorization->issuedBy(this, authorization)
            || authorization->plan().execution != rhi::BindlessExecution::Emulated
            || (!authorization->plan().executable
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization->plan(), 2u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization->plan(), 3u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization->plan(), 5u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization->plan(), 11u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization->plan(), 12u))
            || authorization->plan().deviceIdentity != caps_.deviceIdentity
            || authorization->plan().shaderPackageIdentity
                != authorization->package().artifact().shaderPackageIdentity) {
            diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
            return {};
        }
        return createWebGpuRhiAuthorizedBindlessRenderPipeline(
            std::move(authorization), state, vertexEntry, fragmentEntry, diagnostic, push);
    }

protected:
    std::uint64_t queryOriginalSnapshotBufferAllocation(
        const rhi::BufferDesc& desc, const char*& diagnostic) const override
    {
        diagnostic = "RHI-WEBGPU-BINDLESS-SNAPSHOT-ALLOCATION-RUNTIME";
        if (!registry_ || !webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost())
            return 0u;
        // WebGPU's dedicated-buffer accounting uses the validated native size.
        diagnostic = nullptr;
        return desc.sizeBytes;
    }

    std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> prepareGraphicsBinding(
        std::shared_ptr<const rhi::BindlessGraphicsBindingOwners> owners,
        const rhi::BindlessGraphicsBindingBudget&,
        core::pmr::String& diagnostic) const override
    {
        return prepareWebGpuOriginalBindlessGraphics(*this, std::move(owners), diagnostic);
    }

    std::shared_ptr<const rhi::BindlessPreparedComputeBinding> prepareComputeBinding(
        std::shared_ptr<const rhi::BindlessComputeBindingOwners> owners,
        const rhi::BindlessComputeBindingBudget&,
        core::pmr::String& diagnostic) const override
    {
        return prepareWebGpuOriginalBindlessCompute(*this, std::move(owners), diagnostic);
    }

    rhi::ValidationResult validateIndirectProcessorSource(
        const rhi::GpuBuffer& source) const override
    {
        if (!registry_ || caps_.indirectPacketStorageContractVersion != 1u
            || !webGpuNativeDeviceAvailable())
            return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-PREPARATION-UNAVAILABLE"};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto registered = registry_->find(source.id());
        const auto native = webGpuNativePayloadFor(source);
        if (!registered || !native || !native->buffer || registered->buffer != native)
            return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-SOURCE-DEVICE-MISMATCH"};
        if (native->sizeBytes != source.desc().sizeBytes
            || wgpuBufferGetSize(native->buffer) != native->sizeBytes
            || (wgpuBufferGetUsage(native->buffer) & WGPUBufferUsage_Storage) == 0u)
            return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-SOURCE-PAYLOAD-INVALID"};
        return {true, {}};
#else
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-PREPARATION-UNAVAILABLE"};
#endif
    }

    rhi::ValidationResult validateIndirectPacketCreation(
        const rhi::BindlessLoweringPlan& plan) const override
    {
        if (caps_.indirectPacketStorageContractVersion != 1u || !registry_)
            return {false, "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-UNAVAILABLE"};
        return validateSnapshotCreation(plan);
    }

    rhi::ValidationResult validateIndirectPacketPayload(
        const rhi::BindlessIndirectPacket& packet) const override
    {
        const auto creation = validateIndirectPacketCreation(packet.consumerPlan());
        if (!creation.success)
            return creation;
        const auto live = packet.validate();
        if (!live.success)
            return live;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const std::array<const rhi::GpuBuffer*, 3> buffers{
            packet.physicalArgs(), packet.instanceMappings(), packet.producerParameters()};
        const std::array<std::uint64_t, 3> sizes{sizeof(rhi::DrawIndexedIndirectCommand),
            sizeof(rhi::IndirectInstanceBase),
            sizeof(rhi::ZeroBaseIndirectProducerParams)};
        const std::array<WGPUBufferUsage, 3> usages{WGPUBufferUsage_Storage
                | WGPUBufferUsage_Indirect | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
            WGPUBufferUsage_Uniform | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
        for (std::size_t index = 0u; index < buffers.size(); ++index) {
            const auto& buffer = *buffers[index];
            const auto registered = registry_->find(buffer.id());
            const auto native = webGpuNativePayloadFor(buffer);
            if (!registered || registered->buffer != native || !native || !native->buffer)
                return {false, "RHI-WEBGPU-INDIRECT-PACKET-REGISTRY-MISMATCH"};
            if (buffer.desc().sizeBytes != sizes[index] || native->sizeBytes != sizes[index]
                || wgpuBufferGetSize(native->buffer) != sizes[index]
                || (wgpuBufferGetUsage(native->buffer) & usages[index]) != usages[index]
                || !buffer.hostWritesSealed() || !registered->gpuWritesSealed
                || !registered->gpuTransferWritesSealed)
                return {false, "RHI-WEBGPU-INDIRECT-PACKET-PAYLOAD-INVALID"};
        }
        return {true, {}};
#else
        return {false, "RHI-WEBGPU-INDIRECT-PACKET-NATIVE-UNAVAILABLE"};
#endif
    }

    rhi::ValidationResult validateSnapshotCreation(
        const rhi::BindlessLoweringPlan& plan) const override
    {
        if (plan.originalExecutionContractVersion) {
            const auto lowering = rhi::semantics::OriginalBindlessExecutionLowering::lowering(
                rhi::Backend::WebGPU, plan.originalExecutionContractVersion);
            // Only the independent buffer Compute profile has a live
            // queue/fault/map completion contract. Preparation cannot promote.
            if (!lowering.executionAvailable || !lowering.completionAvailable
                || lowering.diagnostic)
                return {false, lowering.diagnostic};
            if (!plan.success || !plan.executable || plan.originalPreparationContractVersion
                || plan.execution != rhi::BindlessExecution::Emulated
                || !rhi::semantics::OriginalBindlessExecutionLowering::capabilityAvailable(
                    caps_, plan.originalExecutionContractVersion)
                || caps_.faultReadbackContractVersion != 1u
                || plan.deviceIdentity != caps_.deviceIdentity || !registry_
                || !webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost())
                return {false, "RHI-WEBGPU-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE"};
            return {true, {}};
        }
        if (plan.originalPreparationContractVersion) {
            const auto lowering = rhi::semantics::OriginalBindlessPreparationLowering::lowering(
                rhi::Backend::WebGPU, plan.originalPreparationContractVersion);
            if (!lowering.preparationAvailable || lowering.diagnostic || plan.executable
                || plan.execution != rhi::BindlessExecution::Emulated
                || plan.deviceIdentity != caps_.deviceIdentity || !caps_.limitsQueried
                || !rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                    caps_, plan.originalPreparationContractVersion)
                || !registry_ || !webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost())
                return {false, "RHI-WEBGPU-BINDLESS-ORIGINAL-PREPARATION-UNAVAILABLE"};
            return {true, {}};
        }
        if (plan.execution != rhi::BindlessExecution::Emulated || !plan.executable
            || plan.deviceIdentity != caps_.deviceIdentity || !caps_.limitsQueried
            || !caps_.executablePayload || caps_.faultReadbackContractVersion != 1u
            || !webGpuNativeDeviceAvailable()) {
            return {false, "RHI-WEBGPU-BINDLESS-SNAPSHOT-EXECUTION-UNAVAILABLE"};
        }
        return {true, {}};
    }

    rhi::ValidationResult validateAuthorization(const rhi::BindlessShaderPackage& package,
        const rhi::BindlessSnapshot& snapshot) const override
    {
        if (snapshot.plan().originalPreparationContractVersion == 7u)
            return {false, "RHI-BINDLESS-ORIGINAL-STORAGE-PREPARATION-SNAPSHOT-ONLY"};
        if (rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageSnapshotPlan(
                snapshot.plan()))
            return {false, "RHI-BINDLESS-ORIGINAL-MIXED-IMAGE-SNAPSHOT-ONLY"};
        const auto creation = validateSnapshotCreation(snapshot.plan());
        if (!creation.success)
            return creation;
        const auto& artifact = package.artifact();
        if (!artifact.additionalDomains.empty()
            && !rhi::semantics::BindlessPipelineInterfaceContract::originalEmulatedBufferDomains(
                artifact,
                snapshot.plan().originalPreparationContractVersion
                    ? snapshot.plan().originalPreparationContractVersion
                    : snapshot.plan().originalExecutionContractVersion,
                snapshot.plan().originalPreparationContractVersion != 0u))
            return {false, "RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE"};
        if (artifact.execution != rhi::BindlessExecution::Emulated
            || (!snapshot.plan().executable
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    snapshot.plan(), 1u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    snapshot.plan(), 2u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    snapshot.plan(), 3u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    snapshot.plan(), 5u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    snapshot.plan(), 8u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    snapshot.plan(), 11u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    snapshot.plan(), 12u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageResourcePlan(
                    snapshot.plan()))
            || snapshot.plan().execution != rhi::BindlessExecution::Emulated
            || artifact.faultReportingContractVersion != 1u
            || artifact.faultLatch.contractVersion != 1u || !snapshot.faultLatchBuffer()
            || !snapshot.faultReadbackBuffer() || !snapshot.descriptorSet()) {
            return {false, "RHI-WEBGPU-BINDLESS-AUTHORIZATION-PAYLOAD-INCOMPLETE"};
        }
        const auto latch = webGpuNativePayloadFor(*snapshot.faultLatchBuffer());
        const auto readback = webGpuNativePayloadFor(*snapshot.faultReadbackBuffer());
        const auto descriptors =
            rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                snapshot.plan())
            ? prepareWebGpuOriginalImageCellDescriptors(snapshot)
            : webGpuNativePayloadFor(*snapshot.descriptorSet());
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (!latch || !latch->buffer || !readback || !readback->buffer || !descriptors
            || descriptors->bindings.empty()) {
            return {false, "RHI-WEBGPU-BINDLESS-AUTHORIZATION-NATIVE-PAYLOAD-MISSING"};
        }
#else
        (void)latch;
        (void)readback;
        (void)descriptors;
        return {false, "RHI-WEBGPU-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE"};
#endif
        return {true, {}};
    }

private:
    rhi::BindlessCapabilities caps_;
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    WebGpuRhiAllocator& allocator_;
    std::shared_ptr<WebGpuProducerCompletionOwner> processorCompletion_;
};

} // namespace

class WebGpuRhiDevice final : public rhi::GpuDevice {
public:
    explicit WebGpuRhiDevice(WebGpuRhiDeviceDesc desc, bool hostDiagnostic = false)
        : desc_(std::move(desc)), hostDiagnostic_(hostDiagnostic),
          allocator_({
              desc_.label,
              desc_.memoryBudgetBytes,
              desc_.allowNativeHandleExposureToUpperLayers,
          }),
          commandStats_(arp::core::makeShared<WebGpuNativeCommandStats>()),
          swapchainStats_(arp::core::makeShared<WebGpuNativeSwapchainStats>()),
          stagingUploadPool_(
              arp::core::makeShared<WebGpuRhiStagingUploadPool>(uploadStagingPoolDescFor(desc_))),
          queue_(commandStats_), ownedTextureReadbackExtension_(registry_),
          bindless_(webGpuNativeDeviceLimits(),
              registry_,
              desc_.runtimeProfile.logicalFrameSlots,
              allocator_)
    {
        desc_.capabilityContract.pushConstantUploadRingBudgetBytes =
            resolvePushConstantBudget(desc_).ringByteCapacity;
    }

    ~WebGpuRhiDevice() override { closeOriginalDeviceLifetimeForShutdown(); }

    rhi::Backend backend() const override { return rhi::Backend::WebGPU; }

    rhi::semantics::ShutdownDrainReport drainForShutdown(
        const rhi::semantics::ShutdownDrainRequest& request) override
    {
        const auto started = std::chrono::steady_clock::now();
        if (!request.validAt(started)) {
            return rhi::semantics::rejectedShutdownDrainReport(
                backend(), started, request, started);
        }
        // Browser queue completion is callback-driven.  A synchronous host
        // drain would either block the browser event loop or fake completion,
        // neither of which preserves ShutdownDrain/1.
        rhi::semantics::ShutdownDrainReport report;
        report.backend = backend();
        report.diagnosticId = "ARP-RHI-WEBGPU-SHUTDOWN-DRAIN-DEADLINE-UNSUPPORTED";
        rhi::semantics::finalizeShutdownDrainTiming(
            report, started, request, std::chrono::steady_clock::now());
        return report;
    }

    rhi::DeviceCapabilities capabilities() const override
    {
        auto caps =
            rhi::makeSharedDeviceCapabilities(rhi::Backend::WebGPU, "RHI WebGPU", desc_.label);
        const bool nativeAvailable = webGpuNativeDeviceAvailable() && !webGpuNativeDeviceLost();
        // A writable profile can restrict the actual device; it cannot replace it.
        caps.core.graphics = nativeAvailable
            && desc_.runtimeProfile.requiredFeatures.sampledTextures
            && desc_.runtimeProfile.requiredFeatures.canvasSwapchain;
        // Compute is mandatory on a created WebGPU GPUDevice. Do not run the
        // cold shader/pipeline probe from this snapshot accessor.
        caps.core.compute =
            nativeAvailable && desc_.runtimeProfile.optionalFeatures.computeDispatch;
        caps.explicitFeatures.explicitBarriers = nativeAvailable; // Ordered equivalent.
        caps.memory.deviceLocal = nativeAvailable;                // GPU-only resource intent.
        caps.memory.hostVisible = nativeAvailable; // Upload/readback resource intent.
        caps.explicitFeatures.pushConstants = false;
        // WebGPU has no native secondary command buffer API. The runtime
        // fallback plan exposes an explicit CPU replay mode; the raw native
        // capability remains false.
        caps.explicitFeatures.secondaryCommandBuffers = false;
        caps.swapchain.requestedImageCount = desc_.runtimeProfile.requestedSwapchainImageCount;
        caps.swapchain.maxFramesInFlight = desc_.runtimeProfile.logicalFrameSlots;
        // Generic queue submission has no original completion graph. Private
        // ordinary signal tickets do not implement the public fence contract.
        caps.features.nativeSync = false;
        caps.memory.transientAliasing = false;
        rhi::EngineFeatureRequirementSet requirements;
        requirements.logicalFrameSlots = 2;
        requirements.features = {
            {rhi::EngineFeature::PushConstants, {}},
            {rhi::EngineFeature::SecondaryCommandBuffer, {}},
            {rhi::EngineFeature::DescriptorTable, {}},
            {rhi::EngineFeature::TransientAliasing, {}},
            {rhi::EngineFeature::TimelineSync, {}},
            {rhi::EngineFeature::ComputeDispatch, {}},
            {rhi::EngineFeature::StorageTextureBinding, {}},
            {rhi::EngineFeature::NativeSync, {}},
            {rhi::EngineFeature::SubpassInputAttachment, {}},
            {rhi::EngineFeature::SubpassDepthInputAttachment, {}},
            {rhi::EngineFeature::Rg16FloatStorageTexture, {}},
            {rhi::EngineFeature::Rgba16FloatStorageTexture, {}},
            {rhi::EngineFeature::StorageBufferBinding, {}},
            {rhi::EngineFeature::SceneGeometryRayQuery, {}},
            {rhi::EngineFeature::IndirectDraw, {}},
        };
        auto plan =
            rhi::semantics::buildNativeEngineFallbackPlan(desc_.capabilityContract, requirements);
        plan.applyTo(caps);
        const auto& limits = desc_.runtimeProfile.limits;
        auto& compute = caps.computeResources;
        compute.sampledDepthTexture = desc_.runtimeProfile.requiredFeatures.sampledTextures
            && desc_.runtimeProfile.requiredFeatures.depthTestedIndexedDraw;
        compute.maxComputeWorkgroupInvocations = limits.maxComputeInvocationsPerWorkgroup;
        compute.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
        compute.maxComputeWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
        compute.maxComputeWorkgroupMemoryBytes = limits.maxComputeWorkgroupStorageSize;
        compute.maxStorageBufferBindingsPerStage = limits.maxStorageBuffersPerShaderStage;
        compute.maxStorageBufferBindingSizeBytes = limits.maxStorageBufferBindingSize;
        const bool complete =
            desc_.runtimeProfile.status == rhi::WebGpuRuntimeProfileStatus::PlatformValidated
            && compute.sampledDepthTexture && caps.features.computeDispatch
            && caps.features.storageBufferBinding && compute.maxComputeWorkgroupInvocations != 0u
            && compute.maxComputeWorkgroupSizeX != 0u && compute.maxComputeWorkgroupSizeY != 0u
            && compute.maxComputeWorkgroupMemoryBytes != 0u
            && compute.maxStorageBufferBindingsPerStage != 0u
            && compute.maxStorageBufferBindingSizeBytes != 0u;
        compute.mode =
            complete ? rhi::CapabilityMode::Native : rhi::CapabilityMode::DiagnosticFailure;
        caps.features.timestampQueries = rhi::semantics::hasExecutableOptionalNativeFeature(
            desc_.capabilityContract, rhi::semantics::NativeBackendFeature::TimestampQueries);
        caps.features.textureCompressionBc = hasFeature(
            desc_.capabilityContract, rhi::semantics::NativeBackendFeature::TextureCompressionBc);
        // R32SampledUpload/1 remains fail-closed until the browser/native
        // upload + unfiltered read payload has its own runtime evidence.
        caps.features.r32FloatSampledUpload = false;

        // Track A P2: push-constant lowering plan.
        {
            auto pcPlan = rhi::semantics::buildPushConstantLoweringPlan(
                rhi::Backend::WebGPU, desc_.capabilityContract);
            rhi::semantics::applyPushConstantLoweringPlan(caps, pcPlan);
        }

        caps.memory.transientAliasing = false;
        caps.features.transientAliasing = false;

        caps.features.computeDispatch = caps.features.computeDispatch && caps.core.compute;
        caps.features.sceneGeometryRayQuery =
            caps.features.sceneGeometryRayQuery && caps.features.computeDispatch;
        if (!nativeAvailable)
            rhi::revokeUnavailableNativeDeviceCapabilities(caps);
        rhi::synchronizeExecutionStatuses(caps);
        rhi::synchronizeQueryExecutionStatuses(caps);

        return caps;
    }

    rhi::MemoryStats memoryStats() const override
    {
        auto stats = allocator_.memoryStats();
        stats.backendFallbacksHiddenFromUpperLayer = true;
        return stats;
    }

    rhi::semantics::PhysicalBudgetLedgerReport physicalBudgetLedger() const override
    {
        rhi::semantics::PhysicalBudgetLedgerReport report;
        report.backend = backend();

        const auto memory = memoryStats();
        if (memory.budgetCapacityBytes != 0u) {
            report.category(rhi::semantics::PhysicalBudgetCategory::ResourceAllocation) =
                rhi::semantics::observedPhysicalBudgetObservation(
                    rhi::semantics::PhysicalBudgetUnit::Bytes,
                    memory.budgetCapacityBytes,
                    memory.allocatedBytes,
                    memory.peakAllocatedBytes,
                    memory.budgetRejectionCount,
                    memory.liveResourceCount);
        }

        if (desc_.uploadStagingByteCapacity != 0u && stagingUploadPool_) {
            const auto staging = stagingUploadPool_->stats();
            if (staging.boundedCapacity) {
                report.category(rhi::semantics::PhysicalBudgetCategory::UploadStaging) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::Bytes,
                        desc_.uploadStagingByteCapacity,
                        staging.liveBytes,
                        staging.peakBytes,
                        staging.failedWriteCount,
                        staging.liveSliceCount,
                        staging.retiredSliceCount);
            }
        }
        if (descriptorCache_) {
            const auto descriptors = descriptorCache_->budgetStats();
            if (descriptors.configured) {
                report.category(rhi::semantics::PhysicalBudgetCategory::Descriptor) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::Descriptors,
                        descriptors.capacity,
                        descriptors.current,
                        descriptors.peak,
                        descriptors.rejectionCount,
                        descriptorCache_->entryCount());
            }
        }
        if (pipelineCache_) {
            const auto pipelines = pipelineCache_->budgetStats();
            if (pipelines.configured) {
                report.category(rhi::semantics::PhysicalBudgetCategory::PipelineCache) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::PipelineEntries,
                        pipelines.capacity,
                        pipelines.current,
                        pipelines.peak,
                        pipelines.rejectionCount,
                        pipelineCache_->entryCount());
            }
        }
        return report;
    }

    rhi::GpuTimingSample latestGpuTiming() const override
    {
        rhi::GpuTimingSample sample;
        if (!desc_.runtimeProfile.browserRuntime
            || !desc_.runtimeProfile.requiredFeatures.canvasSwapchain) {
            sample.availability = rhi::GpuTimingAvailability::Unsupported;
            sample.diagnostic = "WebGPU GPU timing requires a browser native-device runtime";
        } else if (!rhi::semantics::hasNativeFeature(desc_.capabilityContract,
                       rhi::semantics::NativeBackendFeature::TimestampQueries)) {
            sample.availability = rhi::GpuTimingAvailability::FeatureNotEnabled;
            sample.diagnostic = "WebGPU timestamp-query feature was not enabled by the adapter";
        } else if (!webGpuNativeTimestampQuerySupported()) {
            sample.availability = rhi::GpuTimingAvailability::Unsupported;
            sample.diagnostic = "WebGPU timestamp-query payload is unavailable";
        } else {
            return webGpuNativeLatestGpuTimingSample();
        }
        return sample;
    }

    rhi::semantics::UploadStagingReadiness uploadStagingReadiness() const override
    {
        const auto completed =
            stagingUploadPool_ ? stagingUploadPool_->completedSubmissionSerial() : nullptr;
        return {
            rhi::semantics::UploadStagingReadinessStatus::Available,
            completed ? completed->load(std::memory_order_acquire) : 0u,
            nullptr,
        };
    }

    rhi::GpuQueue& queue() override { return queue_; }

    void* queryInterface(std::string_view extensionId) noexcept override
    {
        if (extensionId == rhi::kExtensionBindlessCross) {
            return static_cast<rhi::BindlessCrossExtension*>(&bindless_);
        }
        if (extensionId == rhi::kExtensionTextureReadback) {
            return &textureReadbackExtension_;
        }
        if (extensionId == rhi::kExtensionTextureReadbackOwnedPreparation) {
            return static_cast<rhi::TextureReadbackOwnedPreparationExtension*>(
                &ownedTextureReadbackExtension_);
        }
        return nullptr;
    }

    arp::core::UniquePtr<rhi::GpuBuffer> createBuffer(const rhi::BufferDesc& bufferDesc) override
    {
        return createBufferWithInitializer(bufferDesc, {});
    }

    core::UniquePtr<rhi::GpuBuffer> createBufferWithInitializer(
        const rhi::BufferDesc& bufferDesc, const WebGpuNativeBufferInitializer& initializer)
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        std::pmr::string capabilityDiagnostic;
        if (!validateWebGpuBufferCapability(
                bufferDesc, desc_.capabilityContract, capabilityDiagnostic)) {
            std::cerr << "[arp-webgpu] buffer capability rejected label="
                      << bufferDesc.label.c_str() << " reason=" << capabilityDiagnostic.c_str()
                      << '\n';
            publishWebGpuDeviceDiagnostic("capability-rejected", capabilityDiagnostic.c_str());
            return nullptr;
        }
        const auto allocation = allocator_.allocate({
            bufferDesc.label,
            bufferDesc.sizeBytes,
            bufferDesc.memory,
        });
        if (!allocation.success) {
            std::cerr << "[arp-webgpu] buffer allocation rejected label="
                      << bufferDesc.label.c_str() << " bytes=" << bufferDesc.sizeBytes
                      << " reason=" << allocation.diagnostics.c_str() << '\n';
            return nullptr;
        }
        auto buffer = createWebGpuRhiBuffer(nextResourceId(),
            bufferDesc,
            allocation,
            nativePayloadPolicy(),
            registry_,
            initializer);
        if (buffer) {
            registry_->registerBuffer(*buffer);
        }
        return completeOriginalResourceCreation(std::move(buffer));
    }

    const char* captureOriginalGeometry(
        const rhi::GpuBuffer& buffer, WebGpuTransferResourceObservation& observation) noexcept
    {
        if (const auto* id = validateOriginalResourceRegistration(buffer))
            return id;
        const auto registered = registry_->find(buffer.id());
        if (!registered
            || !core::SharedOwnerProjection::sameOwner(
                registered->buffer, webGpuNativePayloadFor(buffer)))
            return "RHI-WEBGPU-GEOMETRY-CONTENT-NATIVE-REGISTRATION";
        return observation.capture(buffer.id(), *registered);
    }

    rhi::CommandValidationResult writeBuffer(rhi::GpuBuffer& buffer,
        const void* data,
        std::uint64_t sizeBytes,
        std::uint64_t offsetBytes = 0) override
    {
        if (buffer.hostWritesSealed())
            return {false, "RHI-BUFFER-HOST-WRITES-SEALED", 0};
        if (!data || sizeBytes == 0u || offsetBytes > buffer.desc().sizeBytes
            || sizeBytes > buffer.desc().sizeBytes - offsetBytes) {
            return {false, "WebGPU RHI buffer write desc is invalid", 0};
        }
        if (!hasResourceUsage(buffer.desc().usage, rhi::ResourceUsage::CopyDst)) {
            return {false, "WebGPU RHI buffer write requires CopyDst usage", 0};
        }
        if ((offsetBytes & 3u) != 0u || (sizeBytes & 3u) != 0u) {
            return {false, "WebGPU RHI buffer write offset and size must be 4-byte aligned", 0};
        }
        const auto nativePayload = webGpuNativePayloadFor(buffer);
        const auto registered = registry_->find(buffer.id());
        if (!registered || registered->buffer != nativePayload) {
            return {false, "RHI-WEBGPU-BUFFER-WRITE-DEVICE-MISMATCH", 0};
        }
        if (!nativePayload) {
            return {false, "WebGPU RHI buffer write requires a native buffer payload", 0};
        }
        std::pmr::string diagnostics;
        if (!writeWebGpuNativeBufferPayload(
                *nativePayload, data, sizeBytes, offsetBytes, diagnostics)) {
            return {false, std::move(diagnostics), 0};
        }
        return {true, "", 1};
    }

    arp::core::UniquePtr<rhi::GpuBufferView> createBufferView(
        const rhi::GpuBuffer& buffer, const rhi::BufferViewDesc& viewDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createWebGpuRhiBufferView(nextResourceId(), buffer.id(), viewDesc);
    }

    arp::core::UniquePtr<rhi::GpuTexture> createTexture(
        const rhi::TextureDesc& textureDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createTexturePayload(textureDesc, nullptr, nullptr);
    }

    core::UniquePtr<rhi::GpuTexture> createOriginalContentTexturePayload(
        const rhi::TextureDesc& desc,
        const rhi::OriginalTextureContentCreationContract::Decision& decision,
        const rhi::OriginalTextureContentCreationBudget&,
        const char*& diagnostic) override
    {
        return createTexturePayload(desc, &decision, &diagnostic);
    }

    core::UniquePtr<rhi::GpuTexture> createTexturePayload(const rhi::TextureDesc& textureDesc,
        const rhi::OriginalTextureContentCreationContract::Decision* content,
        const char** diagnostic)
    {
        std::pmr::string capabilityDiagnostic;
        if (!validateWebGpuTextureCapability(
                textureDesc, desc_.capabilityContract, capabilityDiagnostic)) {
            if (diagnostic)
                *diagnostic = "RHI-CANONICAL-CONTENT-CREATION-CAPABILITY";
            std::cerr << "[arp-webgpu] texture capability rejected label="
                      << textureDesc.label.c_str() << " reason=" << capabilityDiagnostic.c_str()
                      << '\n';
            publishWebGpuDeviceDiagnostic("capability-rejected", capabilityDiagnostic.c_str());
            return nullptr;
        }
        const auto allocation = allocator_.allocate({
            textureDesc.label,
            estimateWebGpuRhiTextureBytes(textureDesc),
            textureDesc.memory,
        });
        if (!allocation.success) {
            if (diagnostic)
                *diagnostic = "RHI-CANONICAL-CONTENT-CREATION-GPU-ALLOCATION";
            std::cerr << "[arp-webgpu] texture allocation rejected label="
                      << textureDesc.label.c_str()
                      << " bytes=" << estimateWebGpuRhiTextureBytes(textureDesc)
                      << " reason=" << allocation.diagnostics.c_str() << '\n';
            publishWebGpuDeviceDiagnostic("allocation-rejected", allocation.diagnostics.c_str());
            return nullptr;
        }
        auto texture = content
            ? createWebGpuRhiTexture(nextResourceId(),
                  textureDesc,
                  allocation,
                  WebGpuNativeTextureFactory::createCanonicalContent(textureDesc, *content),
                  registry_)
            : createWebGpuRhiTexture(
                  nextResourceId(), textureDesc, allocation, nativePayloadPolicy(), registry_);
        if (texture) {
            registry_->registerTexture(*texture);
        } else {
            if (diagnostic)
                *diagnostic = "RHI-CANONICAL-CONTENT-CREATION-NATIVE-PAYLOAD";
            publishWebGpuDeviceDiagnostic(
                "native-resource-create-failed", textureDesc.label.c_str());
        }
        auto result = completeOriginalResourceCreation(std::move(texture));
        if (diagnostic && !result && !*diagnostic)
            *diagnostic = "RHI-CANONICAL-CONTENT-CREATION-REGISTRATION";
        return result;
    }

    rhi::ResourceBarrierResult prepareTextureForPoolReuse(rhi::GpuTexture& texture) override
    {
        if (texture.state() == rhi::ResourceState::Undefined) {
            return {true,
                "",
                texture.id(),
                rhi::ResourceState::Undefined,
                rhi::ResourceState::Undefined,
                true};
        }
        return transitionWebGpuRhiTexture(texture,
            rhi::ResourceBarrierDesc{texture.id(),
                rhi::defaultTextureSubresourceRange(texture.desc()),
                texture.state(),
                rhi::ResourceState::Undefined});
    }

    rhi::ResourceBarrierResult prepareBufferForPoolReuse(rhi::GpuBuffer& buffer) override
    {
        if (buffer.state() == rhi::ResourceState::Undefined) {
            return {true,
                "",
                buffer.id(),
                rhi::ResourceState::Undefined,
                rhi::ResourceState::Undefined};
        }
        return transitionWebGpuRhiBuffer(buffer,
            rhi::ResourceBarrierDesc{
                buffer.id(), {}, buffer.state(), rhi::ResourceState::Undefined});
    }

    rhi::TexturePlacementRequirements queryTexturePlacementRequirements(
        const rhi::TextureDesc& /*desc*/) override
    {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-WEBGPU-HEAP-PLACEMENT-UNAVAILABLE");
        return {};
    }

    arp::core::UniquePtr<rhi::GpuMemoryHeap> createMemoryHeap(
        std::uint64_t sizeBytes, std::uint64_t alignmentBytes, rhi::MemoryUsage /*usage*/) override
    {
        heapCreationAttempted_ = true;
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-WEBGPU-HEAP-PLACEMENT-UNAVAILABLE");
        (void)sizeBytes;
        (void)alignmentBytes;
        // WebGPU has no placed-resource or heap-aliasing primitive.  A
        // size-only heap would make a Degraded capability look executable.
        return nullptr;
    }

    std::string_view lastMemoryHeapCreationDiagnostic() const override
    {
        return heapCreationAttempted_ ? "RHI-WEBGPU-HEAP-PLACEMENT-UNAVAILABLE" : "";
    }

    /// WebGPU placed texture: aliasing is not supported by the WebGPU API.
    /// The capability-gated transient allocator uses ordinary pool allocation;
    /// this direct placed-resource entry point must fail closed.
    arp::core::UniquePtr<rhi::GpuTexture> createPlacedTexture(const rhi::TextureDesc& desc,
        const rhi::GpuMemoryHeap& /*heap*/,
        std::uint64_t /*offsetBytes*/) override
    {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-WEBGPU-HEAP-PLACEMENT-UNAVAILABLE");
        (void)desc;
        // Returning an independently allocated texture here would violate the
        // placed-resource ownership contract.  WebGPU uses the ordinary pool
        // allocation path when transient aliasing is degraded.
        return nullptr;
    }

    arp::core::UniquePtr<rhi::GpuTextureView> createTextureView(
        const rhi::GpuTexture& texture, const rhi::TextureViewDesc& viewDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        auto view = createWebGpuRhiTextureView(
            nextResourceId(), texture.id(), texture, viewDesc, nativePayloadPolicy(), registry_);
        if (view) {
            registry_->registerTextureView(*view);
        }
        return completeOriginalResourceCreation(std::move(view), &texture);
    }

    arp::core::UniquePtr<rhi::GpuSampler> createSampler(
        const rhi::SamplerDesc& samplerDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        auto sampler =
            createWebGpuRhiSampler(nextResourceId(), samplerDesc, nativePayloadPolicy(), registry_);
        if (sampler) {
            registry_->registerSampler(*sampler);
        }
        return completeOriginalResourceCreation(std::move(sampler));
    }

    arp::core::UniquePtr<rhi::PipelineLayout> createPipelineLayout(
        const rhi::PipelineLayoutDesc& layoutDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createWebGpuRhiPipelineLayout(layoutDesc);
    }

    rhi::PipelineValidationReport validateRenderPipeline(
        const rhi::RenderPipelineDesc& pipelineDesc) override
    {
        return validateWebGpuRhiRenderPipeline(pipelineDesc);
    }

    rhi::PipelineValidationReport validateComputePipeline(
        const rhi::ComputePipelineDesc& pipelineDesc) override
    {
        return validateWebGpuRhiComputePipeline(pipelineDesc);
    }

    arp::core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        const rhi::RenderPipelineDesc& pipelineDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        // Capability lowering gate: run the L2 lowering decision (which
        // includes validateRenderPipelineState plus per-backend capability
        // checks) before delegating to the backend pipeline builder. A
        // descriptor the device cannot lower must fail closed instead of
        // producing a payload that silently drops the unsupported state.
        const auto decision =
            rhi::semantics::lowerRenderPipelineState(desc_.capabilityContract, pipelineDesc);
        if (!decision.success) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            return nullptr;
        }
        return createWebGpuRhiRenderPipeline(pipelineDesc, nativePayloadPolicy());
    }

    arp::core::UniquePtr<rhi::ComputePipeline> createComputePipeline(
        const rhi::ComputePipelineDesc& pipelineDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        // Capability lowering gate: run the L2 compute lowering decision
        // (which includes validateComputePipelineState) before delegating to
        // the backend pipeline builder.
        const auto decision =
            rhi::semantics::lowerComputePipelineState(desc_.capabilityContract, pipelineDesc);
        if (!decision.success) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            return nullptr;
        }
        return createWebGpuRhiComputePipeline(pipelineDesc, nativePayloadPolicy());
    }

    core::UniquePtr<rhi::RenderPipeline> createAcceptedOriginalGraphicsPipeline(
        const rhi::RenderPipelineDesc& desc,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>& creation,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        const auto decision =
            rhi::semantics::lowerRenderPipelineState(desc_.capabilityContract, desc);
        if (!decision.success) {
            diagnostic = "RHI-ACCEPTED-GRAPHICS-FACTORY-STATE";
            return {};
        }
        return WebGpuAcceptedOriginalGraphicsPipelineFactory::create(
            desc, creation, quota, diagnostic);
    }

    core::UniquePtr<rhi::ComputePipeline> createAcceptedOriginalComputePipeline(
        const rhi::ComputePipelineDesc& desc,
        const std::shared_ptr<const rhi::OriginalComputePipelineCreation>& creation,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        const auto decision =
            rhi::semantics::lowerComputePipelineState(desc_.capabilityContract, desc);
        if (!decision.success) {
            diagnostic = "RHI-ACCEPTED-COMPUTE-FACTORY-STATE";
            return {};
        }
        return WebGpuAcceptedOriginalComputePipelineFactory::create(
            desc, creation, quota, diagnostic);
    }

    arp::core::UniquePtr<rhi::DescriptorArena> createDescriptorArena(
        const rhi::DescriptorArenaDesc& arenaDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createWebGpuRhiDescriptorArena(arenaDesc, registry_, nativePayloadPolicy());
    }

    core::UniquePtr<rhi::OriginalConformanceClearExecution> createConformanceClearExecution(
        core::UniquePtr<rhi::ConformanceClearWorkload> workload,
        const rhi::ConformanceClearExecutionRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        diagnostic = rhi::ConformanceClearExecutionContract::validate(request);
        if (diagnostic)
            return {};
        if ((diagnostic = originalDeviceNativeLossDiagnostic()))
            return {};
        if (!quota || quota->limitBytes() != request.observation.maxCpuMetadataBytes) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto execution = core::makeUnique<WebGpuConformanceClearExecution>(
                WebGpuConformanceClearExecution::Key{},
                *this,
                desc_.capabilityContract,
                request,
                quota);
            diagnostic = execution->initialize(std::move(workload));
            if (diagnostic)
                return {};
            return execution;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION";
            return {};
        }
    }

    core::UniquePtr<rhi::OriginalConformanceSampledExecution> createConformanceSampledExecution(
        core::UniquePtr<rhi::ConformanceSampledWorkload> workload,
        const rhi::ConformanceSampledExecutionRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        diagnostic = rhi::ConformanceSampledExecutionContract::validate(request);
        if (!diagnostic)
            diagnostic = request.artifacts.diagnostic();
        if (!diagnostic)
            diagnostic = originalDeviceNativeLossDiagnostic();
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != request.execution.observation.maxCpuMetadataBytes) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto execution = core::makeUnique<WebGpuConformanceClearExecution>(
                WebGpuConformanceClearExecution::Key{},
                *this,
                desc_.capabilityContract,
                request.execution,
                quota);
            diagnostic = execution->initialize(std::move(workload), request);
            if (diagnostic)
                return {};
            return execution;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION";
            return {};
        }
    }

    core::UniquePtr<rhi::OriginalConformanceStorageMrtExecution>
    createConformanceStorageMrtExecution(
        core::UniquePtr<rhi::ConformanceStorageMrtWorkload> workload,
        const rhi::ConformanceStorageMrtExecutionRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        diagnostic = rhi::ConformanceStorageMrtExecutionContract::validateWebGpu(request);
        if (!diagnostic)
            diagnostic = originalDeviceNativeLossDiagnostic();
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != request.execution.observation.maxCpuMetadataBytes) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto execution = core::makeUnique<WebGpuConformanceClearExecution>(
                WebGpuConformanceClearExecution::Key{},
                *this,
                desc_.capabilityContract,
                request.execution,
                quota);
            diagnostic = execution->initialize(std::move(workload), request);
            if (diagnostic)
                return {};
            return execution;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION";
            return {};
        }
    }

    arp::core::UniquePtr<rhi::CommandEncoder> createCommandEncoder(
        const rhi::CommandEncoderDesc& commandDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        const auto pushBudget = resolvePushConstantBudget(desc_);
        return createWebGpuRhiCommandEncoder(commandDesc,
            registry_,
            nativePayloadPolicy(),
            commandStats_,
            desc_.capabilityContract,
            pushBudget.ringByteCapacity,
            desc_.runtimeProfile.limits.maxUniformBufferBindingSize,
            pushBudget.uniformOffsetAlignment,
            bindless_.capabilities().executablePayload,
            stagingUploadPool_,
            bindless_.processorCompletion());
    }

    arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createSecondaryCommandEncoder(
        const rhi::SecondaryCommandEncoderDesc& commandDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        if (!commandDesc.allowCpuReplay) {
            return nullptr;
        }
        return createWebGpuRhiSecondaryCommandEncoder(commandDesc);
    }

    arp::core::UniquePtr<rhi::RenderBundleEncoder> createRenderBundleEncoder(
        const rhi::RenderBundleDesc& bundleDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        return createWebGpuRhiRenderBundleEncoder(
            bundleDesc, nativePayloadPolicy(), desc_.capabilityContract);
    }

    arp::core::UniquePtr<rhi::Swapchain> createSwapchain(
        const rhi::SwapchainDesc& swapchainDesc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        const auto policy = nativePayloadPolicy();
        std::printf("[wgpu-swap] policy enabled=%d required=%d surface='%s'\n",
            policy.enabled,
            policy.required,
            swapchainDesc.surfaceName.c_str());
        return createWebGpuRhiSwapchain(swapchainDesc, policy, swapchainStats_, registry_);
    }

    arp::core::UniquePtr<rhi::GpuFence> createFence() override
    {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-WEBGPU-PUBLIC-FENCE-RESOURCE-GRAPH-UNAVAILABLE");
        return nullptr;
    }

    arp::core::UniquePtr<rhi::GpuSemaphore> createSemaphore() override { return nullptr; }

    arp::core::UniquePtr<rhi::GpuTimelineSemaphore> createTimelineSemaphore(
        uint64_t initialValue = 0) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        if (!creationRuntimeAdmitted())
            return nullptr;

        (void)initialValue;
        // Queue completion callbacks can retire backend-owned resources, but
        // WebGPU core cannot encode an L2 queue-side timeline wait/signal.
        // Never expose the callback counter as a timeline semaphore.
        return nullptr;
    }

    arp::core::UniquePtr<rhi::QueryPool> createQueryPool(const rhi::QueryPoolDesc& desc) override
    {
        if (!creationRuntimeAdmitted())
            return nullptr;

        if (!rhi::semantics::hasExecutableOptionalNativeFeature(
                desc_.capabilityContract, rhi::semantics::NativeBackendFeature::TimestampQueries)) {
            return nullptr;
        }
        return createWebGpuRhiQueryPool(desc, desc_.capabilityContract);
    }

    rhi::PipelineCache& pipelineCache() override
    {
        if (!pipelineCache_) {
            pipelineCache_ = arp::core::makeUnique<rhi::PipelineCache>(*this);
        }
        return *pipelineCache_;
    }

    rhi::DescriptorCache& descriptorCache() override
    {
        if (!descriptorCache_) {
            descriptorCache_ = arp::core::makeUnique<rhi::DescriptorCache>(*this);
        }
        return *descriptorCache_;
    }

private:
    std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> bindOriginalDeviceNativeLoss(
        const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
        const rhi::OriginalDeviceNativeLossBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic) override
    {
        return originalLossSource_.prepare(receiver, budget, std::move(quota), diagnostic);
    }

    std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> bindOriginalObservedNativeLoss(
        const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
        const rhi::OriginalDeviceNativeLossBudget& budget,
        const rhi::OriginalDeviceNativeLossObserverRequest& request,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic) override
    {
        return originalLossSource_.prepareObserved(
            receiver, budget, request, std::move(quota), diagnostic);
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuBuffer& buffer) const noexcept override
    {
        return originalLossSource_.validate(buffer);
    }

    const char* validateOriginalNativeLossDescriptor(
        const rhi::DescriptorArena& arena) const noexcept override
    {
        return validateWebGpuOriginalDescriptorLossSource(arena, registry_);
    }

    const char* validateOriginalNativeLossDescriptor(
        const rhi::DescriptorSet& set) const noexcept override
    {
        return validateWebGpuOriginalDescriptorLossSource(set, registry_);
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuTexture& texture) const noexcept override
    {
        return originalLossSource_.validate(texture);
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuTextureView& view) const noexcept override
    {
        return originalLossSource_.validate(view);
    }

    const char* validateOriginalNativeLossPipeline(
        const rhi::ComputePipeline& pipeline) const noexcept override
    {
        return originalLossSource_.validate(pipeline);
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuSampler& sampler) const noexcept override
    {
        return originalLossSource_.validate(sampler);
    }

    const char* validateOriginalNativeLossPipeline(
        const rhi::RenderPipeline& pipeline) const noexcept override
    {
        return originalLossSource_.validate(pipeline);
    }

    WebGpuOriginalDeviceLossSource originalLossSource_;

    rhi::GpuResourceId nextResourceId() { return rhi::allocateGpuResourceId(); }

    WebGpuRhiNativePayloadPolicy nativePayloadPolicy() const
    {
        // The Emscripten entry point pre-initializes the browser WebGPU device
        // before constructing this RHI device.  Runtime profile promotion is
        // diagnostic metadata and can lag that device availability, so it must
        // not suppress the canvas swapchain payload used by the first frame.
        const auto enabled = !hostDiagnostic_ && webGpuNativeDeviceAvailable();
        return {
            enabled,
            !hostDiagnostic_,
        };
    }

    bool creationRuntimeAdmitted() const
    {
        if (hostDiagnostic_ != webGpuNativeDeviceAvailable())
            return true;
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-WEBGPU-NATIVE-DEVICE-UNAVAILABLE");
        return false;
    }

    WebGpuRhiDeviceDesc desc_;
    const bool hostDiagnostic_;
    bool heapCreationAttempted_ = false;
    WebGpuRhiAllocator allocator_;
    std::shared_ptr<WebGpuNativeCommandStats> commandStats_;
    std::shared_ptr<WebGpuNativeSwapchainStats> swapchainStats_;
    std::shared_ptr<WebGpuRhiStagingUploadPool> stagingUploadPool_;
    WebGpuRhiQueue queue_;
    WebGpuTextureReadbackExtensionImpl textureReadbackExtension_;
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_ =
        arp::core::makeShared<WebGpuRhiResourceRegistry>();
    WebGpuOwnedTextureReadbackExtension ownedTextureReadbackExtension_;
    WebGpuBindlessCrossExtension bindless_;
    arp::core::UniquePtr<rhi::PipelineCache> pipelineCache_;
    arp::core::UniquePtr<rhi::DescriptorCache> descriptorCache_;

public:
    WebGpuNativeCommandStats commandStats() const
    {
        return commandStats_ ? *commandStats_ : WebGpuNativeCommandStats{};
    }

    WebGpuNativeSwapchainStats swapchainStats() const
    {
        return swapchainStats_ ? *swapchainStats_ : WebGpuNativeSwapchainStats{};
    }
};

WebGpuRhiDeviceReport describeWebGpuRhiDevice(const WebGpuRhiDeviceDesc& desc)
{
    WebGpuRhiDeviceReport report;
    report.backend = rhi::Backend::WebGPU;
    report.runtimeCapabilityPromotedFromProbe = isRuntimePromotedFromProbe(desc);
    report.computePromotionValidated = desc.runtimeProfile.optionalFeatures.computeDispatch
        && hasFeature(
            desc.capabilityContract, rhi::semantics::NativeBackendFeature::ComputeDispatch);
    report.storageTexturePromotionValidated =
        desc.runtimeProfile.optionalFeatures.storageTextureBinding
        && hasFeature(
            desc.capabilityContract, rhi::semantics::NativeBackendFeature::StorageTextureBinding);
    report.storageBufferPromotionValidated = desc.runtimeProfile.optionalFeatures.storageBuffer
        && hasFeature(
            desc.capabilityContract, rhi::semantics::NativeBackendFeature::StorageBufferBinding);
    report.sceneGeometryRayQueryPromotionValidated =
        desc.runtimeProfile.optionalFeatures.sceneGeometryRayQuery
        && hasFeature(
            desc.capabilityContract, rhi::semantics::NativeBackendFeature::SceneGeometryRayQuery);
    report.backendDescriptorGroupsIntegrated = desc.enableBackendDescriptorGroups;
    report.backendDescriptorVocabularyHiddenFromPublicApi = true;
    report.pushConstantUploadRingEmulated = desc.enablePushConstantUploadRing;
    const auto pushBudget = resolvePushConstantBudget(desc);
    report.pushConstantDeclaredWorkloadByteBudget = pushBudget.declaredWorkloadByteBudget;
    report.pushConstantUploadRingByteCapacity = pushBudget.ringByteCapacity;
    report.pushConstantUploadRingAlignmentBytes = pushBudget.uniformOffsetAlignment;
    report.canvasLifecycleDiagnosticsSupported = desc.enableCanvasLifecycleDiagnostics;
    report.resizeDiagnosticsSupported = desc.enableResizeDiagnostics;
    report.surfaceLostDiagnosticsSupported = desc.enableSurfaceLostDiagnostics;
    report.hiddenTabFrameSkippingSupported = desc.enableHiddenTabFrameSkipping;
    report.memoryBudgetTelemetrySanitized = desc.memoryBudgetBytes > 0;
    report.boundedSurfaceSynchronization = !desc.allowBlockingSurfaceAcquire;
    report.queueSubmitSupported = false;
    report.swapchainCanvasLifecycleSupported = desc.enableCanvasLifecycleDiagnostics;
    report.nullDelegateUsed = false;
    report.backendOwnedResourceObjects = true;
    report.backendOwnedDescriptorObjects = true;
    report.backendOwnedPipelineObjects = true;
    report.backendOwnedCommandObjects = true;
    report.backendOwnedQueueSubmit = true;
    report.backendOwnedSwapchain = true;
    report.backendOwnedUploadStagingSupported = true;
    report.uploadStagingNativeHandlesHidden = true;
    report.uploadStagingByteCapacity = desc.uploadStagingByteCapacity;
    report.uploadStagingBounded =
        desc.enablePushConstantUploadRing && report.uploadStagingByteCapacity > 0;
    report.publicDeviceRuntimeBackingRequired = desc.runtimeProfile.browserRuntime
        && desc.runtimeProfile.canvasSurface
        && desc.runtimeProfile.status == rhi::WebGpuRuntimeProfileStatus::PlatformValidated;
    report.publicDeviceNativeDeviceAvailable = webGpuNativeDeviceAvailable();
    report.publicDeviceWgpuBackedObjects = false;
    report.publicDeviceSemanticOnlyFallback = true;
    report.publicDeviceDiagnosticOnlyFallback =
        report.publicDeviceRuntimeBackingRequired && !report.publicDeviceNativeDeviceAvailable;
#if defined(__EMSCRIPTEN__)
    report.nonEmscriptenDiagnosticOnly = false;
    report.nonEmscriptenNativeClaimRejected = false;
#else
    report.nonEmscriptenDiagnosticOnly = !report.publicDeviceNativeDeviceAvailable
        && report.publicDeviceSemanticOnlyFallback && report.publicDeviceDiagnosticOnlyFallback;
    report.nonEmscriptenNativeClaimRejected =
        !report.publicDeviceWgpuBackedObjects && !report.nativeDeviceAvailable;
#endif
    report.nativeDeviceAvailable = report.publicDeviceNativeDeviceAvailable;
    report.nativeBufferCreated = false;
    report.nativeTextureCreated = false;
    report.nativeTextureViewCreated = false;
    report.nativeSamplerCreated = false;
    report.nativeDescriptorGroupCreated = false;
    report.nativeShaderModuleCreated = false;
    report.nativePipelineCreated = false;
    report.nativeCommandEncoderCreated = false;
    report.nativeCommandBufferCreated = false;
    report.queueSubmitted = false;
    report.canvasAcquired = false;
    report.canvasPresentedByBrowser = false;
    report.resizeHandled = false;
    report.surfaceLossHandled = false;
    report.hiddenTabHandled = false;
    report.perFramePipelineRebuild = false;
    report.nativeHandleExposedToUpperLayer = desc.allowNativeHandleExposureToUpperLayers;
    report.perDrawPipelineCreationAllowed = desc.allowPerDrawPipelineCreation;
    report.perFrameShaderCompilationAllowed = desc.allowPerFrameShaderCompilation;

    if (desc.runtimeProfile.backend != rhi::Backend::WebGPU) {
        report.diagnostics = "WebGPU RHI device requires a WebGPU runtime profile";
        return report;
    }
    if (desc.runtimeProfile.status == rhi::WebGpuRuntimeProfileStatus::DiagnosticFailure) {
        report.diagnostics = "WebGPU RHI device requires a valid runtime profile";
        arp::core::appendDiagnostic(report.diagnostics, desc.runtimeProfile.diagnostics);
        return report;
    }
    if (!desc.runtimeProfile.browserRuntime || !desc.runtimeProfile.canvasSurface) {
        report.diagnostics = "WebGPU RHI device requires browser canvas runtime facts";
        return report;
    }
    if (desc.runtimeProfile.status == rhi::WebGpuRuntimeProfileStatus::PlatformValidated
        && !report.runtimeCapabilityPromotedFromProbe) {
        report.diagnostics = "WebGPU RHI device requires runtime capability promotion from a "
                             "queried runtime profile";
        return report;
    }
    if (desc.runtimeProfile.status != rhi::WebGpuRuntimeProfileStatus::PlatformValidated
        && desc.capabilityContract.queriedFromNativeDevice) {
        report.diagnostics = "WebGPU RHI device cannot promote unqueried runtime capabilities";
        return report;
    }
    std::pmr::string capabilityContractDiagnostic;
    if (!validateWebGpuCapabilityContract(desc, capabilityContractDiagnostic)) {
        report.diagnostics = std::move(capabilityContractDiagnostic);
        return report;
    }
    if (!hasFeature(desc.capabilityContract, rhi::semantics::NativeBackendFeature::RuntimeLimits)
        || !hasFeature(desc.capabilityContract,
            rhi::semantics::NativeBackendFeature::OptionalRuntimeFeatures)) {
        report.diagnostics = "WebGPU RHI device requires runtime limits and optional feature facts";
        return report;
    }
    if (desc.requireComputeStorageRuntimePromotion
        && (!report.runtimeCapabilityPromotedFromProbe || !report.computePromotionValidated
            || !report.storageTexturePromotionValidated
            || !report.storageBufferPromotionValidated)) {
        report.diagnostics =
            "WebGPU RHI device requires truthful runtime compute/storage promotion";
        return report;
    }
    if (!desc.enableBackendDescriptorGroups) {
        report.diagnostics = "WebGPU RHI device requires backend-owned descriptor groups";
        return report;
    }
    if (desc.uploadStagingByteCapacity == 0u) {
        report.diagnostics = "WebGPU RHI device requires a declared upload staging budget";
        return report;
    }
    if (!desc.enablePushConstantUploadRing
        || pushBudget.declaredWorkloadByteBudget < rhi::kL2AtomicPushConstantLimitBytes
        || pushBudget.ringByteCapacity == 0u || pushBudget.uniformOffsetAlignment == 0u
        || desc.runtimeProfile.limits.maxUniformBufferBindingSize
            < rhi::kL2AtomicPushConstantLimitBytes) {
        report.diagnostics =
            "WebGPU RHI device requires a declared, aligned push-constant upload-ring budget";
        return report;
    }
    if (!desc.enableCanvasLifecycleDiagnostics || !desc.enableResizeDiagnostics
        || !desc.enableSurfaceLostDiagnostics || !desc.enableHiddenTabFrameSkipping) {
        report.diagnostics =
            "WebGPU RHI device requires resize, surface-lost, and hidden-tab lifecycle diagnostics";
        return report;
    }
    if (desc.allowBlockingSurfaceAcquire) {
        report.diagnostics = "WebGPU RHI device must use bounded canvas surface synchronization";
        return report;
    }

    const auto invariants = rhi::validateSharedDeviceInvariants(rhi::Backend::WebGPU,
        "WebGPU",
        desc.capabilityContract.backend,
        desc.memoryBudgetBytes,
        desc.allowNativeHandleExposureToUpperLayers,
        desc.allowPerDrawPipelineCreation,
        desc.allowPerFrameShaderCompilation);
    if (!invariants.success) {
        report.diagnostics = invariants.diagnostics;
        return report;
    }
    report.nativeHandleExposedToUpperLayer = invariants.nativeHandleExposedToUpperLayer;
    report.perDrawPipelineCreationAllowed = invariants.perDrawPipelineCreationAllowed;
    report.perFrameShaderCompilationAllowed = invariants.perFrameShaderCompilationAllowed;

    report.success = true;
    return report;
}

WebGpuRhiCanvasLifecycleReport describeWebGpuRhiCanvasLifecycle(
    const WebGpuRhiDeviceDesc& desc, WebGpuRhiCanvasEvent event)
{
    WebGpuRhiCanvasLifecycleReport report;
    report.backend = rhi::Backend::WebGPU;

    const auto deviceReport = describeWebGpuRhiDevice(desc);
    if (!deviceReport.success) {
        report.diagnostics = deviceReport.diagnostics;
        return report;
    }

    const auto state = rhi::semantics::makeWebGpuCanvasLifecycleState(desc.runtimeProfile);
    const auto decision = rhi::semantics::lowerWebGpuCanvasLifecycleEvent(
        desc.runtimeProfile, state, platformEventFor(event));

    report.success = decision.success;
    report.diagnostics = decision.diagnostics;
    report.acquireAllowed = decision.state.acquireAllowed;
    report.presentAllowed = decision.state.presentAllowed;
    report.swapchainRebuildRequired = decision.state.swapchainRebuildRequired;
    report.releaseSurfaceResources = decision.state.releaseSurfaceResources;
    report.nativeHandleExposedToUpperLayer = decision.state.nativeHandleExposedToUpperLayer;

    switch (event) {
    case WebGpuRhiCanvasEvent::Resize:
        report.resizeDiagnosed =
            desc.enableResizeDiagnostics && decision.state.swapchainRebuildRequired;
        break;
    case WebGpuRhiCanvasEvent::SurfaceLost:
        report.surfaceLostDiagnosed = desc.enableSurfaceLostDiagnostics
            && decision.state.releaseSurfaceResources && !decision.state.acquireAllowed;
        break;
    case WebGpuRhiCanvasEvent::HiddenTab:
        report.hiddenTabFrameSkipped = desc.enableHiddenTabFrameSkipping
            && !decision.state.acquireAllowed && !decision.state.presentAllowed;
        break;
    case WebGpuRhiCanvasEvent::VisibleTab:
        break;
    }

    return report;
}

core::UniquePtr<rhi::GpuBuffer> createWebGpuRhiOriginalGeometryBuffer(rhi::GpuDevice& device,
    const rhi::BufferDesc& desc,
    const WebGpuNativeBufferInitializer& initializer,
    const char*& diagnostic)
{
    diagnostic = "RHI-WEBGPU-GEOMETRY-CONTENT-INITIALIZER";
    auto* original = dynamic_cast<WebGpuRhiDevice*>(&device);
    if (!original || !webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost()
        || !initializer.context || !initializer.initialize
        || desc.memory != rhi::MemoryUsage::CpuToGpu
        || desc.initialState != rhi::ResourceState::ShaderRead
        || (desc.usage != rhi::ResourceUsage::VertexBuffer
            && desc.usage != rhi::ResourceUsage::IndexBuffer
            && desc.usage != rhi::ResourceUsage::UniformBuffer
            && desc.usage != rhi::ResourceUsage::StorageBuffer))
        return {};
    if (!desc.sizeBytes || desc.sizeBytes > SIZE_MAX || desc.sizeBytes % 4u) {
        diagnostic = "RHI-WEBGPU-GEOMETRY-CONTENT-MAPPED-SIZE-ALIGNMENT";
        return {};
    }
    auto result = original->createBufferWithInitializer(desc, initializer);
    diagnostic = result ? nullptr : "RHI-WEBGPU-GEOMETRY-CONTENT-CREATE";
    return result;
}

const char* captureWebGpuRhiOriginalGeometryBuffer(rhi::GpuDevice& device,
    const rhi::GpuBuffer& buffer,
    WebGpuTransferResourceObservation& observation) noexcept
{
    auto* original = dynamic_cast<WebGpuRhiDevice*>(&device);
    return original ? original->captureOriginalGeometry(buffer, observation)
                    : "RHI-WEBGPU-GEOMETRY-CONTENT-DEVICE";
}

arp::core::UniquePtr<rhi::GpuDevice> createWebGpuRhiDevice(const WebGpuRhiDeviceDesc& desc)
{
    if (!webGpuNativeDeviceAvailable()) {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-WEBGPU-NATIVE-DEVICE-UNAVAILABLE");
        return nullptr;
    }
    if (!describeWebGpuRhiDevice(desc).success) {
        return nullptr;
    }
    return arp::core::makeUnique<WebGpuRhiDevice>(desc);
}

arp::core::UniquePtr<rhi::GpuDevice> createWebGpuRhiHostDiagnosticDevice(
    const WebGpuRhiDeviceDesc& desc, std::uint32_t contractVersion)
{
    if (contractVersion != 1u) {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-HOST-DIAGNOSTIC-DEVICE-VERSION");
        return nullptr;
    }
    if (webGpuNativeDeviceAvailable()) {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-HOST-DIAGNOSTIC-DEVICE-NATIVE-RUNTIME");
        return nullptr;
    }
    if (!describeWebGpuRhiDevice(desc).success)
        return nullptr;
    return arp::core::makeUnique<WebGpuRhiDevice>(desc, true);
}

WebGpuNativeCommandStats inspectWebGpuRhiNativeCommandStats(const rhi::GpuDevice& device)
{
    const auto* webGpuDevice = dynamic_cast<const WebGpuRhiDevice*>(&device);
    return webGpuDevice ? webGpuDevice->commandStats() : WebGpuNativeCommandStats{};
}

WebGpuNativeSwapchainStats inspectWebGpuRhiNativeSwapchainStats(const rhi::GpuDevice& device)
{
    const auto* webGpuDevice = dynamic_cast<const WebGpuRhiDevice*>(&device);
    return webGpuDevice ? webGpuDevice->swapchainStats() : WebGpuNativeSwapchainStats{};
}

} // namespace arp::rhi_webgpu
