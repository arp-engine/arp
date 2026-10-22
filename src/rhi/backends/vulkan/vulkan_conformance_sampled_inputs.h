#pragma once

#include "core/memory/initialized_byte_buffer.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/conformance/sampled_execution.h"
#include "shader/graphics_shader_module_artifact.h"
#include "shader/single_entry_shader_artifact.h"

namespace arp::rhi_vulkan {
class VulkanConformanceSampledInputs final {
    friend class VulkanConformanceClearExecution;
    friend class VulkanConformanceClearFrame;

    class Key {
        friend class VulkanConformanceClearExecution;
        Key() = default;
    };

    struct Buffer {
        rhi::ConformanceSampledInput role;
        std::size_t frame;
        std::shared_ptr<rhi::GpuBuffer> object;
        std::shared_ptr<VulkanRhiNativeResourcePayload> native;
        core::InitializedByteBuffer content;
    };

public:
    explicit VulkanConformanceSampledInputs(Key, std::uint64_t budget) : budget_(budget) {}

private:
    const Buffer* buffer(const VulkanRhiNativeResourcePayload&,
        rhi::ConformanceSampledInput,
        const void*,
        std::size_t) const noexcept;
    const char* diagnostic() const noexcept;
    core::pmr::Vector<Buffer> buffers_{&core::defaultCpuMemoryResource()};
    std::shared_ptr<rhi::GpuSampler> sampler_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativeSampler_;
    std::array<std::shared_ptr<VulkanRhiNativeResourcePayload>, 2> nativePipelines_;
    std::array<std::shared_ptr<rhi::RenderPipeline>, 2> pipelines_;
    std::shared_ptr<const shader::GraphicsShaderModuleArtifact> artifact_;
    std::shared_ptr<rhi::ComputePipeline> compute_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativeCompute_;
    std::shared_ptr<const shader::SingleEntryShaderArtifact> computeArtifact_;

    struct Texture {
        std::size_t frame, role;
        std::shared_ptr<rhi::GpuTexture> object;
    };

    core::pmr::Vector<Texture> textures_{&core::defaultCpuMemoryResource()};
    std::uint64_t textureBudget_ = 0, nativeTextureBytes_ = 0;
    bool storageMrt_ = false;
    std::uint64_t budget_, nativeBytes_ = 0;
};
} // namespace arp::rhi_vulkan
