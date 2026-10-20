#pragma once

#include "rhi/gpu_commands.h"
#include "rhi/gpu_pipeline.h"

#include <array>

namespace arp::renderer {

inline void useDynamicRasterViewport(rhi::RenderPipelineDesc& desc)
{
    desc.hasViewport = false;
    desc.hasScissor = false;
    desc.viewport = {};
    desc.scissor = {};
    desc.dynamicViewportScissor = true;
}

// The caller opts its pipeline into dynamicViewportScissor and supplies the
// current attachment extent. No initial-window size or backend state is used.
inline rhi::CommandValidationResult bindRasterPipeline(
    rhi::RenderPassEncoder& pass, const rhi::RenderPipeline& pipeline, rhi::Extent2D extent)
{
    if (extent.width == 0u || extent.height == 0u) {
        return {false, "RENDERER-RASTER-EXTENT-INVALID", 0u};
    }
    auto result = pass.setPipeline(pipeline);
    if (result.success)
        result = pass.setViewport({0.0f,
            0.0f,
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
            0.0f,
            1.0f});
    if (result.success)
        result = pass.setScissor({0, 0, extent.width, extent.height});
    return result;
}

// Scoped to one encoder and attachment extent, with immutable descriptors,
// buffers and offsets (no uploads/state changes inside the cached scope).
// All binds in that scope must go through this object. A pipeline change
// invalidates descriptor and buffer tracking
// just as RenderPassEncoder::setPipeline does. Failed binds are never cached.
template <std::size_t SetCount, std::size_t VertexSlotCount = 2>
class RasterPassBindingCache final {
public:
    template <class Pass>
    rhi::CommandValidationResult pipeline(Pass& pass, const rhi::RenderPipeline& value)
    {
        if (pipeline_ == &value)
            return {true, {}, 0u};
        invalidate();
        auto result = pass.setPipeline(value);
        if (result.success)
            pipeline_ = &value;
        return result;
    }

    template <class Pass>
    rhi::CommandValidationResult rasterPipeline(
        Pass& pass, const rhi::RenderPipeline& value, rhi::Extent2D extent)
    {
        if (extent.width == 0u || extent.height == 0u)
            return {false, "RENDERER-RASTER-EXTENT-INVALID", 0u};
        if (pipeline_ == &value && extent_.width == extent.width && extent_.height == extent.height)
            return {true, {}, 0u};
        invalidate();
        auto result = bindRasterPipeline(pass, value, extent);
        if (result.success) {
            pipeline_ = &value;
            extent_ = extent;
        }
        return result;
    }

    template <class Pass>
    rhi::CommandValidationResult descriptor(
        Pass& pass, std::uint32_t index, const rhi::DescriptorSet& value)
    {
        if (index >= SetCount)
            return {false, "RENDERER-PASS-BINDING-CACHE-SET-OUT-OF-RANGE", 0u};
        if (sets_[index] == &value)
            return {true, {}, 0u};
        sets_[index] = nullptr;
        auto result = pass.bindDescriptorSet(index, value);
        if (result.success)
            sets_[index] = &value;
        return result;
    }

    template <class Pass>
    rhi::CommandValidationResult vertexBuffer(
        Pass& pass, std::uint32_t slot, const rhi::GpuBuffer& value, std::uint64_t offset)
    {
        if (slot >= VertexSlotCount)
            return {false, "RENDERER-PASS-BINDING-CACHE-VERTEX-SLOT-OUT-OF-RANGE", 0u};
        auto& bound = vertices_[slot];
        if (bound.buffer == &value && bound.offset == offset)
            return {true, {}, 0u};
        bound = {};
        auto result = pass.bindVertexBuffer(slot, value, offset);
        if (result.success)
            bound = {&value, offset};
        return result;
    }

    template <class Pass>
    rhi::CommandValidationResult indexBuffer(
        Pass& pass, const rhi::GpuBuffer& value, rhi::IndexFormat format, std::uint64_t offset)
    {
        if (index_.buffer == &value && index_.offset == offset && indexFormat_ == format)
            return {true, {}, 0u};
        index_ = {};
        auto result = pass.bindIndexBuffer(value, format, offset);
        if (result.success) {
            index_ = {&value, offset};
            indexFormat_ = format;
        }
        return result;
    }

    void invalidate()
    {
        pipeline_ = nullptr;
        extent_ = {};
        sets_.fill(nullptr);
        vertices_.fill({});
        index_ = {};
    }

private:
    const rhi::RenderPipeline* pipeline_ = nullptr;
    rhi::Extent2D extent_{};
    std::array<const rhi::DescriptorSet*, SetCount> sets_{};

    struct BufferBinding {
        const rhi::GpuBuffer* buffer = nullptr;
        std::uint64_t offset = 0u;
    };

    std::array<BufferBinding, VertexSlotCount> vertices_{};
    BufferBinding index_{};
    rhi::IndexFormat indexFormat_ = rhi::IndexFormat::Uint32;
};

} // namespace arp::renderer
