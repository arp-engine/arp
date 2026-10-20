#pragma once
#include "../rhi_shader_package_test_helpers.h"
#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/frontend/pipeline_state_copy.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class PipelineDefinitionIdentityFixture final {
public:
    static void verify()
    {
        auto package = rhi::test::makePushContractShaderPackage();
        rhi::RenderPipelineDesc original;
        original.shaderPackage = &package;
        original.vertexShader = package.entrypoints[0];
        original.fragmentShader = package.entrypoints[1];
        original.colorTargets.resize(1);
        original.vertexBuffers.resize(1);
        original.vertexBuffers[0].attributes.resize(1);
        original.renderPassCompatibility.subpasses.resize(1);
        original.renderPassCompatibility.subpassDependencies.resize(1);
        auto changed = rhi::RenderPipelineStateCopy::copy(original);
        assert(rhi::PipelineDefinitionIdentity::matches(original, changed));
        const auto reject = [&](auto change) {
            changed = rhi::RenderPipelineStateCopy::copy(original);
            change(changed);
            assert(!rhi::PipelineDefinitionIdentity::matches(original, changed));
        };
        reject([](auto& p) {
            p.viewport.x = -0.f;
        });
        reject([](auto& p) {
            p.scissor.width = 1;
        });
        reject([](auto& p) {
            p.depthStencil.depthBiasClamp = -0.f;
        });
        reject([](auto& p) {
            p.depthStencil.front.compare = rhi::CompareOp::Never;
        });
        reject([](auto& p) {
            p.colorTargets[0].alphaBlend.operation = rhi::BlendOp::Max;
        });
        reject([](auto& p) {
            p.vertexBuffers[0].attributes[0].offsetBytes = 1;
        });
        reject([](auto& p) {
            p.renderPassCompatibility.subpassDependencies[0].dstSubpass = 1;
        });
        reject([](auto& p) {
            p.renderPassCompatibility.subpasses[0].colorAttachmentIndices.push_back(0);
        });
        reject([](auto& p) {
            p.vertexShader.computeWorkgroupSizeX = 1;
        });
        auto copy = rhi::ShaderPackageStateCopy::copy(package);
        changed = rhi::RenderPipelineStateCopy::copy(original);
        changed.shaderPackage = &copy;
        assert(rhi::PipelineDefinitionIdentity::matches(original, changed));
        copy.reflection.pushConstantMembers[0].matrixStride++;
        assert(!rhi::PipelineDefinitionIdentity::matches(original, changed));
        copy = rhi::ShaderPackageStateCopy::copy(package);
        copy.generatedReflectionHydrated = !copy.generatedReflectionHydrated;
        assert(!rhi::PipelineDefinitionIdentity::matches(original, changed));
        changed = rhi::RenderPipelineStateCopy::copy(original);
        const auto count = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(rhi::PipelineDefinitionIdentity::matches(original, changed));
        changed.depthStencil.depthBiasClamp = -0.f;
        assert(!rhi::PipelineDefinitionIdentity::matches(original, changed));
        std::pmr::set_default_resource(previous);
        assert(count == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_PIPELINE_DEFINITION_IDENTITY_OK exact-nested-package-reflection inactive-fields float-bits hostile-pmr allocation0");
    }
};
} // namespace arp::test
