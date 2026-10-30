#pragma once

#include "fixtures/generated_shader_package_fixture.h"
#include "rhi/gpu_pipeline.h"

#include <cassert>
#include <cstdint>
#include <utility>

namespace arp::rhi::test {

// Contract fixtures use one reflected uint32 array to model an opaque
// AtomicFullBlockPush payload.  The member metadata is intentionally real:
// validateShaderPackage checks offsets, array stride and total size rather
// than accepting a byte-count-only placeholder.
inline void addAtomicFullBlockPushMember(
    ShaderReflectionDesc& reflection, const std::uint32_t byteCount)
{
    assert(byteCount > 0u);
    assert((byteCount % sizeof(std::uint32_t)) == 0u);
    ShaderPushConstantMemberDesc member;
    member.name = "data";
    member.scalarType = "uint32";
    member.scalarWidth = 32u;
    member.arrayDims.push_back(byteCount / sizeof(std::uint32_t));
    member.arrayStride = sizeof(std::uint32_t);
    member.offset = 0u;
    member.size = byteCount;
    reflection.pushConstantMembers.push_back(std::move(member));
}

// Real Slang fixtures may expose a push-constant block as a sequence of
// float4 members rather than one opaque uint array. Keep the test package ABI
// identical to the generated SPIRV-Reflect member list in that case.
inline void addFloat4PushConstantMembers(
    ShaderReflectionDesc& reflection, const std::uint32_t memberCount)
{
    assert(memberCount > 0u);
    for (std::uint32_t index = 0; index < memberCount; ++index) {
        ShaderPushConstantMemberDesc member;
        member.name = "data";
        member.name += arp::core::toPmrString(std::to_string(index));
        member.scalarType = "float";
        member.scalarWidth = 32u;
        member.vecSize = 4u;
        member.offset = index * 16u;
        member.size = 16u;
        reflection.pushConstantMembers.push_back(std::move(member));
    }
}

inline ShaderPackageDesc makePushContractShaderPackage(bool preparedInput = false)
{
    ShaderPackageDesc package;
    package.label = "CommandValidationShaderPackage";
    package.slangModulePath = "rhi_push_contract.slang";
    package.entrypoints = {{package.slangModulePath, "vsMain", ShaderStage::Vertex},
        {package.slangModulePath, "fsMain", ShaderStage::Fragment},
        {package.slangModulePath,
            "csMain",
            ShaderStage::Compute,
            ShaderSourceLanguage::Slang,
            1,
            1,
            1}};
    package.artifacts.spirvPath = "rhi_push_contract.spv";
    package.artifacts.mslPath = "rhi_push_contract.metal";
    package.artifacts.wgslPath = "rhi_push_contract.wgsl";
    package.artifacts.reflectionPath = "rhi_push_contract.reflection.yaml";
    package.reflection.pushConstantByteCount = 128;
    package.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    package.reflection.pushConstantStages = "CS|FS|VS";
    package.reflection.pushConstantWebGpuBindingGroup = 0;
    package.reflection.pushConstantWebGpuBindingIndex = 0;
    package.reflection.pushConstantMetalBufferIndex = 0;
    package.reflection.computeWorkgroupSizeX = 1;
    package.reflection.computeWorkgroupSizeY = 1;
    package.reflection.computeWorkgroupSizeZ = 1;
    package.reflection.metalGpuCountedIndirectStatus = "Unavailable";
    package.reflection.metalTessellationStatus = "Unavailable";
    addFloat4PushConstantMembers(package.reflection, 8u);
    if (preparedInput)
        GeneratedShaderPackageFixture::prepare(package, "rhi_push_contract.artifact-set.yaml");
    return package;
}

} // namespace arp::rhi::test
