#pragma once

#include "rhi/gpu_device.h"

#include <cassert>
#include <string_view>

namespace arp::tests {

inline void checkRecordedResourceStates(rhi::GpuDevice& device, bool waitForCompletion = true)
{
    using namespace rhi;
    auto buffer = device.createBuffer({"StateRollback.Buffer",
        64u,
        ResourceUsage::CopySrc | ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    TextureDesc desc{"StateRollback.Texture",
        {4u, 4u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::CopySrc | TextureUsage::CopyDst | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined};
    desc.mipLevelCount = 2u;
    auto texture = device.createTexture(desc);
    assert(buffer && texture);
    const TextureSubresourceRange mip0{0u, 1u, 0u, 1u, TextureAspect::Color};
    const TextureSubresourceRange mip1{1u, 1u, 0u, 1u, TextureAspect::Color};
    {
        auto encoder = device.createCommandEncoder({"StateRollback.Abandoned"});
        assert(encoder->transition(*buffer, buffer->state(), ResourceState::CopyDst).success);
        assert(encoder->transition(*buffer, buffer->state(), ResourceState::CopySrc).success);
        assert(encoder->transition(*texture, mip0, ResourceState::Undefined, ResourceState::CopyDst)
                .success);
        assert(texture->subresourceState(mip0) == ResourceState::CopyDst);
        assert(texture->subresourceState(mip1) == ResourceState::Undefined);
    }
    assert(buffer->state() == ResourceState::Undefined);
    assert(texture->subresourceState(mip0) == ResourceState::Undefined);
    assert(texture->subresourceState(mip1) == ResourceState::Undefined);
    {
        auto first = device.createCommandEncoder({"StateRollback.First"});
        assert(first->transition(*buffer, buffer->state(), ResourceState::CopyDst).success);
        auto second = device.createCommandEncoder({"StateRollback.Conflict"});
        const auto conflict = second->transition(*buffer, buffer->state(), ResourceState::CopySrc);
        assert(
            !conflict.success && conflict.diagnostics == "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
        second.reset();
        assert(buffer->state() == ResourceState::CopyDst);
    }
    assert(buffer->state() == ResourceState::Undefined);
    {
        auto encoder = device.createCommandEncoder({"StateRollback.Finished"});
        assert(encoder->transition(*buffer, buffer->state(), ResourceState::CopyDst).success);
        auto commands = encoder->finish();
        assert(commands);
        const auto afterFinish =
            encoder->transition(*buffer, ResourceState::CopyDst, ResourceState::CopySrc);
        assert(!afterFinish.success && !afterFinish.diagnostics.empty());
        assert(!encoder->lastValidation().success);
        assert(buffer->state() == ResourceState::CopyDst);
        const auto textureAfterFinish =
            encoder->transition(*texture, mip0, ResourceState::Undefined, ResourceState::CopyDst);
        assert(!textureAfterFinish.success && !textureAfterFinish.diagnostics.empty());
        assert(texture->subresourceState(mip0) == ResourceState::Undefined);
        encoder.reset();
        assert(buffer->state() == ResourceState::CopyDst);
        commands.reset();
        assert(buffer->state() == ResourceState::Undefined);
    }
    {
        auto encoder = device.createCommandEncoder({"StateRollback.Accepted"});
        assert(encoder->transition(*buffer, buffer->state(), ResourceState::CopyDst).success);
        assert(encoder->transition(*texture, mip0, ResourceState::Undefined, ResourceState::CopyDst)
                .success);
        auto commands = encoder->finish();
        assert(commands && device.queue().submit(*commands).hasAcceptedWork());
        commands.reset();
        encoder.reset();
        assert(buffer->state() == ResourceState::CopyDst);
        assert(texture->subresourceState(mip0) == ResourceState::CopyDst);
        if (waitForCompletion)
            device.waitIdle();
    }
    {
        auto encoder = device.createCommandEncoder({"StateRollback.Retry"});
        assert(
            encoder->transition(*texture, mip0, ResourceState::CopyDst, ResourceState::ShaderRead)
                .success);
        assert(encoder->transition(*texture, mip1, ResourceState::Undefined, ResourceState::CopyDst)
                .success);
    }
    assert(texture->subresourceState(mip0) == ResourceState::CopyDst);
    assert(texture->subresourceState(mip1) == ResourceState::Undefined);
    {
        auto encoder = device.createCommandEncoder({"StateRollback.ResourceDestroyed"});
        assert(encoder->transition(*buffer, buffer->state(), ResourceState::CopySrc).success);
        buffer.reset(); // The journal must detach the dead CPU wrapper.
    }
}

} // namespace arp::tests
