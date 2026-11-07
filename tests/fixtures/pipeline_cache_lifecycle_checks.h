#pragma once

#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/gpu_device.h"
#include "tests/fixtures/generated_shader_package_fixture.h"

#include <cassert>
#include <filesystem>

namespace arp::tests {
// Run against each suite's actual selected device. HostDiagnosticDevice results
// are host contracts only; this helper does not manufacture native execution.
inline void checkPipelineCacheLifecycle(rhi::GpuDevice& device, const rhi::RenderPipelineDesc& desc)
{
    rhi::PipelineCache cache(device);
    auto first = cache.acquireRenderPipelineLease(desc);
    assert(first && first->validationReport().backend == device.backend());
    auto layout = device.createPipelineLayout(desc.layout->desc());
    assert(layout);
    auto equivalent = rhi::RenderPipelineStateCopy::copy(desc);
    equivalent.layout = layout.get();
    auto second = cache.acquireRenderPipelineLease(equivalent);
    assert(second.get() == first.get());
    assert(cache.renderPipelineMissCount() == 1 && cache.renderPipelineHitCount() == 1);

    auto collision = rhi::ShaderPackageStateCopy::copy(*desc.shaderPackage);
    collision.label += "/same-hash-distinct-definition";
    assert(rhi::stableShaderPackageHash(collision)
        == rhi::stableShaderPackageHash(*desc.shaderPackage));
    equivalent.shaderPackage = &collision;
    auto distinct = cache.acquireRenderPipelineLease(equivalent);
    assert(distinct && distinct.get() != first.get());
    collision.generatedReflectionHydrated = false;
    assert(!cache.acquireRenderPipelineLease(equivalent));
    equivalent = rhi::RenderPipelineStateCopy::copy(desc);
    equivalent.layout = nullptr;
    assert(!cache.acquireRenderPipelineLease(equivalent));
    assert(cache.renderPipelineMissCount() == 2 && cache.renderPipelineHitCount() == 1);

    cache.evictStale(100, 1);
    assert(cache.entryCount() == 3); // One shared layout and two leased PSOs.
    cache.clearForShutdown();
    assert(cache.entryCount() == 0);
    layout.reset();
    collision.generatedArtifactSet.reset();
    assert(first->validationReport().success && distinct->validationReport().success);

    rhi::PipelineCache other(device);
    auto separate = other.acquireRenderPipelineLease(desc);
    assert(separate && separate.get() != first.get());
    assert(other.renderPipelineHitCount() == 0 && other.renderPipelineMissCount() == 1);
    separate.reset();
    other.evictStale(100, 1);
    assert(other.entryCount() == 0);
}

inline void checkPipelineCacheGeneratedFixture(rhi::GpuDevice& device, const char* publication)
{
    const auto directory = std::filesystem::path(publication).parent_path();
    const auto path = [&](const char* suffix) {
        return core::toPmrString(
            (directory / (std::string("ordinary_graphics_storage_read") + suffix))
                .generic_string());
    };
    rhi::ShaderPackageDesc package;
    package.slangModulePath = path(".slang");
    package.artifacts = {path(".spv"), path(".metal"), path(".wgsl"), path(".reflection.yaml")};
    package.entrypoints = {{package.slangModulePath, "vertexMain", rhi::ShaderStage::Vertex},
        {package.slangModulePath, "fragmentMain", rhi::ShaderStage::Fragment}};
    package.reflection.descriptorBindings = {
        {0, 3, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Fragment}};
    rhi::test::GeneratedShaderPackageFixture::prepare(package, publication);
    // This fixture has no push constants, so the host Metal contract's lack
    // of executable native push upload cannot skip this lifecycle check.
    assert(package.reflection.pushConstantByteCount == 0);
    auto layout = device.createPipelineLayout(
        rhi::buildPipelineLayoutDescFromShaderReflection(package.reflection));
    assert(layout);
    rhi::RenderPipelineDesc desc;
    desc.layout = layout.get();
    desc.shaderPackage = &package;
    desc.vertexShader = package.entrypoints[0];
    desc.fragmentShader = package.entrypoints[1];
    desc.colorTargets.push_back({rhi::TextureFormat::Rgba8Unorm});
    desc.viewport = {0, 0, 64, 64, 0, 1};
    desc.scissor = {0, 0, 64, 64};
    checkPipelineCacheLifecycle(device, desc);
}
} // namespace arp::tests
