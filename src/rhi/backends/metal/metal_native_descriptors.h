#pragma once
#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_binding_facts.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/backends/metal/metal_resource_ownership.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/gpu_descriptors.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arp::rhi {
class OriginalCompletedDepthTextureSource;
}

namespace arp::rhi_metal {

enum class MetalRhiArgumentKind { Buffer, Texture, Sampler };

enum class MetalRhiArgumentResourceUse {
    None,
    Read,
    ReadSample,
    ReadWrite,
};

constexpr MetalRhiArgumentResourceUse metalRhiArgumentResourceUse(rhi::DescriptorType type) noexcept
{
    switch (type) {
    case rhi::DescriptorType::UniformBuffer:
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        return MetalRhiArgumentResourceUse::Read;
    case rhi::DescriptorType::SampledTexture:
        return MetalRhiArgumentResourceUse::ReadSample;
    case rhi::DescriptorType::StorageBuffer:
    case rhi::DescriptorType::StorageTexture:
        return MetalRhiArgumentResourceUse::ReadWrite;
    case rhi::DescriptorType::Sampler:
    case rhi::DescriptorType::InputAttachment:
        return MetalRhiArgumentResourceUse::None;
    }
    return MetalRhiArgumentResourceUse::None;
}

struct MetalRhiNativeDescriptorBindingInput {
    rhi::DescriptorBindingDesc binding;
    rhi::DescriptorBindingWrite write;
    std::uint32_t arrayElement = 0;
    std::uint32_t dynamicOffsetIndex = UINT32_MAX;
    std::shared_ptr<MetalRhiNativeBufferPayload> buffer;
    std::shared_ptr<MetalRhiNativeTextureViewPayload> textureView;
    std::shared_ptr<MetalRhiNativeSamplerPayload> sampler;
};

struct MetalRhiNativeArgumentBindingPayload {
    std::uint32_t binding = 0;
    std::uint32_t arrayCount = 1;
    std::uint32_t arrayElement = 0;
    rhi::DescriptorType type = rhi::DescriptorType::SampledTexture;
    rhi::ShaderStage stages = rhi::ShaderStage::None;
    bool depthTexture = false;
    rhi::DescriptorResourceKind resourceKind = rhi::DescriptorResourceKind::TextureView;
    MetalRhiArgumentKind argumentKind = MetalRhiArgumentKind::Texture;
    std::shared_ptr<MetalRhiNativeBufferPayload> buffer;
    std::shared_ptr<MetalRhiNativeTextureViewPayload> textureView;
    std::shared_ptr<MetalRhiNativeSamplerPayload> sampler;
    std::uint32_t dynamicOffsetIndex = UINT32_MAX;
    rhi::GpuResourceId resourceId;
    // Baked only by ordinary uniform-offset preparation, from actual native facts.
    std::uint64_t bufferOffsetBytes = 0u;
    std::uint64_t bufferOffsetAlignment = 0u;
    std::uint64_t bufferRequiredBytes = 0u;
    // Independent VS/FS direct uniform/read-only storage ABI ranges.
    std::array<MetalRhiStageBufferRange, 2u> graphicsBufferRanges;
    // Independent VS/FS readonly array pointee ABI; never outer pointer tables.
    std::array<MetalRhiStageBufferPointeeRange, 2u> graphicsBufferPointeeRanges;
    // Frozen by function preparation for every canonical direct/array element.
    MetalRhiNativeResourceObservation preparedNativeIdentity;

    std::shared_ptr<const void> payloadOwner() const noexcept
    {
        return metalRhiCanonicalResourceOwner(resourceKind, buffer, {}, textureView, sampler);
    }
};

struct MetalRhiNativeDescriptorGroupPayload {
    MetalRhiNativeDescriptorGroupPayload() = default;

    MetalRhiNativeDescriptorGroupPayload(const MetalRhiNativeDescriptorGroupPayload& other)
        : nativeDevice(other.nativeDevice),
          bindings(other.bindings, &core::defaultCpuMemoryResource()),
          nativeRuntimeBacked(other.nativeRuntimeBacked),
          nativeRuntimeDiagnosticOnly(other.nativeRuntimeDiagnosticOnly),
          nativeHandleExposedToUpperLayer(other.nativeHandleExposedToUpperLayer),
          nativeObject(other.nativeObject),
          originalCompletedTextureSources(
              other.originalCompletedTextureSources, &core::defaultCpuMemoryResource()),
          originalCompletedDepthSources(
              other.originalCompletedDepthSources, &core::defaultCpuMemoryResource()),
          originalTextureCellSources(
              other.originalTextureCellSources, &core::defaultCpuMemoryResource()),
          diagnostics(other.diagnostics, &core::defaultCpuMemoryResource())
    {}

    MetalRhiNativeDescriptorGroupPayload& operator=(
        const MetalRhiNativeDescriptorGroupPayload&) = default;
    std::shared_ptr<void> nativeDevice;
    core::pmr::Vector<MetalRhiNativeArgumentBindingPayload> bindings{
        &core::defaultCpuMemoryResource()};
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::shared_ptr<void> nativeObject;
    // Exact published typed Sources; ordinary encoders cannot consume them.
    core::pmr::Vector<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>
        originalCompletedTextureSources{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>>
        originalCompletedDepthSources{&core::defaultCpuMemoryResource()};
    // Storage cell identities grant no completed-content or command authority.
    core::pmr::Vector<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>
        originalTextureCellSources{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct MetalRhiNativePipelinePayload;

// No native calls: use the same canonical metadata at capture and observation.
class MetalRhiPreparedDescriptorIdentityValidator final {
public:
    static const char* capture(MetalRhiNativeDescriptorGroupPayload& group) noexcept
    {
        for (auto& binding : group.bindings) {
            const auto* resource = metalRhiCanonicalNativeResource(
                binding.resourceKind, binding.buffer, {}, binding.textureView, binding.sampler);
            if (!resource || !resource->nativeDevice || !resource->nativeObject)
                return "RHI-METAL-PREPARED-RESOURCE-NATIVE-IDENTITY-MISSING";
            binding.preparedNativeIdentity = MetalRhiNativeResourceObservation(*resource);
        }
        return nullptr;
    }

    static const char* validate(const MetalRhiNativeDescriptorGroupPayload& group) noexcept
    {
        for (const auto& binding : group.bindings) {
            const auto* resource = metalRhiCanonicalNativeResource(
                binding.resourceKind, binding.buffer, {}, binding.textureView, binding.sampler);
            if (!resource || !binding.preparedNativeIdentity.matches(*resource))
                return "RHI-METAL-PREPARED-RESOURCE-NATIVE-IDENTITY-MISMATCH";
        }
        return nullptr;
    }
};

struct MetalRhiPreparedFunctionDirectBinding {
    std::uint32_t element = 0;
    std::uint32_t slot = UINT32_MAX;
};

struct MetalRhiPreparedFunctionArgumentBuffer {
    std::uint32_t binding = 0;
    std::uint32_t slot = UINT32_MAX;
    std::shared_ptr<void> buffer;
    MetalRhiNativeResourceObservation preparedNativeIdentity;
};

struct MetalRhiNativePreparedFunctionBindings {
    std::shared_ptr<MetalRhiNativePipelinePayload> pipeline;
    std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload> descriptors;
    std::uint32_t set = 0;
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    std::shared_ptr<void> function;
    std::uint64_t argumentBytes = 0;
    core::pmr::Vector<MetalRhiPreparedFunctionDirectBinding> direct{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiPreparedFunctionArgumentBuffer> arrays{
        &core::defaultCpuMemoryResource()};
};

using MetalRhiPreparedComputeDirectBinding = MetalRhiPreparedFunctionDirectBinding;
using MetalRhiPreparedComputeArgumentBuffer = MetalRhiPreparedFunctionArgumentBuffer;

struct MetalRhiNativePreparedComputeBindings final : MetalRhiNativePreparedFunctionBindings,
                                                     core::SharedObjectIdentity {};

struct MetalRhiPreparedGraphicsStageLayout;

struct MetalRhiNativePreparedGraphicsBindings : core::SharedObjectIdentity {
private:
    class ConstructionKey {
        friend std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>
        prepareMetalRhiNativeGraphicsBindings(std::shared_ptr<MetalRhiNativePipelinePayload>,
            std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>,
            std::uint32_t,
            const std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2>&,
            const rhi::BindlessGraphicsBindingBudget&,
            core::pmr::String&);

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    friend std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>
    prepareMetalRhiNativeGraphicsBindings(std::shared_ptr<MetalRhiNativePipelinePayload>,
        std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>,
        std::uint32_t,
        const std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2>&,
        const rhi::BindlessGraphicsBindingBudget&,
        core::pmr::String&);

public:
    explicit MetalRhiNativePreparedGraphicsBindings(ConstructionKey) {}

    MetalRhiNativePreparedGraphicsBindings(const MetalRhiNativePreparedGraphicsBindings&) = delete;
    std::shared_ptr<MetalRhiNativePipelinePayload> pipeline;
    std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload> descriptors;
    std::uint32_t set = 0;
    std::uint64_t argumentBytes = 0;
    std::uint64_t encodedArgumentBytes = 0;
    std::uint32_t argumentCount = 0;
    std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2> layouts;
    std::array<std::shared_ptr<const MetalRhiNativePreparedFunctionBindings>, 2> stages;
};

// Preparation only. Uses the actual compiled Compute function's encoders.
std::shared_ptr<const MetalRhiNativePreparedComputeBindings> prepareMetalRhiNativeComputeBindings(
    std::shared_ptr<MetalRhiNativePipelinePayload>,
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>,
    std::uint32_t set,
    const rhi::BindlessComputeBindingBudget&,
    core::pmr::String& diagnostic);

// Ordinary Root/2 scalar buffers use the same function preparation with an
// exact zero argument-storage budget. This cannot allocate a GPU argument buffer.
std::shared_ptr<const MetalRhiNativePreparedComputeBindings>
prepareMetalRhiNativeScalarComputeBindings(std::shared_ptr<MetalRhiNativePipelinePayload>,
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>,
    std::uint32_t set,
    core::pmr::String& diagnostic);
bool validateMetalRhiNativePreparedComputeBindings(
    const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>&,
    core::pmr::String& diagnostic);

bool validateMetalRhiNativeGraphicsBindings(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
    core::pmr::String& diagnostic);

// Private preparation prerequisite only; no public Graphics execution ticket.
std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> prepareMetalRhiNativeGraphicsBindings(
    std::shared_ptr<MetalRhiNativePipelinePayload>,
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>,
    std::uint32_t set,
    const std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2>&,
    const rhi::BindlessGraphicsBindingBudget&,
    core::pmr::String& diagnostic);

struct MetalRhiNativeDescriptorTableEntry {
    std::uint32_t index = 0;
    rhi::DescriptorType type = rhi::DescriptorType::SampledTexture;
    rhi::DescriptorResourceKind resourceKind = rhi::DescriptorResourceKind::TextureView;
    std::shared_ptr<MetalRhiNativeBufferPayload> buffer;
    std::shared_ptr<MetalRhiNativeTextureViewPayload> textureView;
    std::shared_ptr<MetalRhiNativeSamplerPayload> sampler;
};

struct MetalRhiNativeDescriptorTablePayload {
    std::shared_ptr<void> nativeDevice;
    rhi::DescriptorTableDesc desc;
    std::pmr::vector<MetalRhiNativeDescriptorTableEntry> entries{&core::defaultCpuMemoryResource()};
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::pmr::string diagnostics;
};

struct MetalRhiDescriptorPayloadInspection {
    bool backendPayloadPresent = false;
    bool argumentBindingsBacked = false;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t bindingCount = 0;
    std::uint32_t bufferArgumentCount = 0;
    std::uint32_t textureArgumentCount = 0;
    std::uint32_t samplerArgumentCount = 0;
    bool uniformBufferBound = false;
    bool storageBufferBound = false;
    bool sampledTextureBound = false;
    bool storageTextureBound = false;
    bool samplerBound = false;
    bool framebufferFetchBound = false;
};

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> createMetalRhiNativeDescriptorGroupPayload(
    const rhi::DescriptorLayoutDesc& layout,
    const std::pmr::vector<MetalRhiNativeDescriptorBindingInput>& bindings);

std::shared_ptr<MetalRhiNativeDescriptorTablePayload> createMetalRhiNativeDescriptorTablePayload(
    const rhi::DescriptorTableDesc& desc,
    const std::pmr::vector<MetalRhiNativeDescriptorTableEntry>& entries);

/// Returns true only when the runtime can create the resource argument-buffer
/// objects required by the MetalArgumentBuffer/1 lowering.
bool metalRhiNativeArgumentBuffersAvailable();

} // namespace arp::rhi_metal
