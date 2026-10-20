#pragma once
#include "rhi/backends/webgpu/native_validation.h"
#include "rhi/gpu_pipeline.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>
#if defined(__EMSCRIPTEN__)
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_ordinary_pipeline_fixture.h"
#endif

namespace arp::test {
class WebGpuDescriptorArrayAbiFixture final {
    using Contract = rhi_webgpu::WebGpuDescriptorArrayAbiContract;

public:
    static void verifyContract()
    {
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(Contract::version == 1u && !Contract::nativeArraysAvailable);
        assert(
            std::string_view(Contract::validateCount(0u)) == "RHI-WEBGPU-DESCRIPTOR-ARRAY-COUNT");
        assert(!Contract::validateCount(1u));
        for (auto count : std::array{2u, 3u, std::numeric_limits<std::uint32_t>::max()})
            assert(std::string_view(Contract::validateCount(count))
                == "RHI-WEBGPU-DESCRIPTOR-ARRAY-ABI-UNSUPPORTED");
        std::array<rhi::ShaderReflectionBinding, 2> bindings{};
        bindings[0].arrayCount = 1u;
        bindings[1].arrayCount = 1u;
        assert(!Contract::validateBindings(bindings));
        bindings[1].arrayCount = 3u;
        assert(Contract::validateBindings(bindings));
        std::pmr::set_default_resource(previous);
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_ARRAY_ABI_CONTRACT_OK version1 scalar1 arrays-closed zero-rejected allocation0");
    }
#if defined(__EMSCRIPTEN__)
    static void verifyActual(
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& compute,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& graphics)
    {
        rhi::DescriptorBindingDesc depth{
            0u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Compute, 3u};
        depth.depthTexture = true;
        rhi::DescriptorLayoutDesc inputs;
        inputs.name = "Inputs";
        inputs.bindings.push_back(depth);
        rhi::DescriptorLayoutDesc output;
        output.name = "Output";
        output.bindings.push_back(
            {0u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute, 1u});
        rhi::PipelineLayoutDesc computeDefinition;
        computeDefinition.descriptorLayouts.push_back(std::move(inputs));
        computeDefinition.descriptorLayouts.push_back(std::move(output));
        computeDefinition.descriptorSets.push_back({0u, "Inputs"});
        computeDefinition.descriptorSets.push_back({1u, "Output"});
        auto computeLayout =
            core::shareUnique(rhi_webgpu::createWebGpuRhiPipelineLayout(computeDefinition));
        assert(computeLayout);
        rhi::PipelineLayoutDesc graphicsDefinition;
        rhi::DescriptorLayoutDesc fragmentInputs;
        fragmentInputs.name = "Inputs";
        depth.stages = rhi::ShaderStage::Fragment;
        fragmentInputs.bindings.push_back(depth);
        graphicsDefinition.descriptorLayouts.push_back(std::move(fragmentInputs));
        graphicsDefinition.descriptorSets.push_back({0u, "Inputs"});
        auto graphicsLayout =
            core::shareUnique(rhi_webgpu::createWebGpuRhiPipelineLayout(graphicsDefinition));
        assert(graphicsLayout);
        auto graphicsState = WebGpuOrdinaryPipelineFixture::graphicsState();
        graphicsState.layout = graphicsLayout.get();
        graphicsState.shaderPackage = &graphics->package();
        graphicsState.vertexShader = graphics->package().entrypoints[0];
        graphicsState.fragmentShader = graphics->package().entrypoints[1];
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        core::pmr::String diagnostic;
        diagnostic.reserve(128u);
        EM_ASM({
            // clang-format off
                const device = Module.preinitializedWebGPUDevice;
                Module.arpArrayGateMethods =
                    Array.of("createShaderModule", "createBindGroupLayout", "createPipelineLayout",
                             "createComputePipeline", "createRenderPipeline");
                Module.arpArrayGateOriginals = Module.arpArrayGateMethods.map(name => device[name]);
                Module.arpArrayGateNativeCalls = 0;
                Module.arpArrayGateMethods.forEach((name, index) => {
                    device[name] = function(...args) {
                        Module.arpArrayGateNativeCalls++;
                        return Module.arpArrayGateOriginals[index].apply(this, args);
                    };
                });
            // clang-format on
        });
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes());
            assert(full.charged());
            assert(!rhi_webgpu::WebGpuOriginalArtifactComputeFactory::create(compute,
                *computeLayout,
                "array-gate",
                {1u,
                    true,
                    compute->artifact()->bytes(shader::ShaderArtifactRole::Wgsl).size(),
                    65536u},
                quota,
                diagnostic));
            assert(std::string_view(diagnostic) == "RHI-WEBGPU-DESCRIPTOR-ARRAY-ABI-UNSUPPORTED");
            assert(!rhi_webgpu::WebGpuOriginalArtifactGraphicsFactory::create(graphics,
                graphicsState,
                {1u,
                    true,
                    graphics->artifact()->bytes(shader::ShaderArtifactRole::Wgsl).size(),
                    65536u},
                quota,
                diagnostic));
            assert(std::string_view(diagnostic) == "RHI-WEBGPU-DESCRIPTOR-ARRAY-ABI-UNSUPPORTED");
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount
            && !quota->liveBytes());
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpArrayGateNativeCalls;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                const device = Module.preinitializedWebGPUDevice;
                Module.arpArrayGateMethods.forEach(
                    (name, index) => { device[name] = Module.arpArrayGateOriginals[index]; });
                delete Module.arpArrayGateMethods;
                delete Module.arpArrayGateOriginals;
                delete Module.arpArrayGateNativeCalls;
            // clang-format on
        });
        probe(*compute->artifact(), "compute");
        probe(*graphics->artifact()->vertex(), "vertex");
        probe(*graphics->artifact()->fragment(), "fragment");
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_ARRAY_ABI_NATIVE_GATE_OK compute-graphics original-artifacts native-calls0 allocation0 quota-exhausted probes3 execution-closed");
    }

private:
    template <class Artifact> static void probe(const Artifact& artifact, const char* name)
    {
        const auto& bytes = artifact.bytes(shader::ShaderArtifactRole::Wgsl);
        EM_ASM(
            {
                // clang-format off
                    const device = Module.preinitializedWebGPUDevice;
                    const source = new TextDecoder("utf-8", {fatal : true}).decode(HEAPU8.subarray($0, $0 + $1));
                    const stage = UTF8ToString($2);
                    Module.arpArrayShaderProbeDone = 0;
                    device.pushErrorScope("validation");
                    let module;
                    try {
                        module = device.createShaderModule(
                            {label : "original-descriptor-array-" + stage, code : source});
                    } catch (error) {
                        device.popErrorScope().then(() => {
                            Module.print("ARRAY_PROBE_EXCEPTION " + String(error));
                            Module.arpArrayShaderProbeDone = -1;
                        });
                        return;
                    }
                    const scope = device.popErrorScope();
                    Promise.all([ module.getCompilationInfo(), scope ])
                        .then(([ info, error ]) => {
                            if (device !== Module.preinitializedWebGPUDevice)
                                throw new Error("original probe device changed");
                            const messages = Array.from(info.messages, message => ({
                                                                           type : message.type,
                                                                           message : message.message,
                                                                           lineNum : message.lineNum,
                                                                           linePos : message.linePos,
                                                                           offset : message.offset,
                                                                           length : message.length
                                                                       }));
                            const rejected = messages.some(message => message.type === "error") || !!error;
                            Module.print("BINDLESS_RHI_DESCRIPTOR_ARRAY_NATIVE_OBSERVATION " + JSON.stringify({
                                stage : stage,
                                enabledFeatures : Array.from(device.features),
                                sourceBytes : $1,
                                scopeError : error ? error.message : null,
                                messages : messages,
                                status : rejected ? "DiagnosticFailure" : "ShaderAcceptedOnly",
                                diagnostic : rejected ? "RHI-WEBGPU-DESCRIPTOR-ARRAY-WGSL-REJECTED"
                                                      : "RHI-WEBGPU-DESCRIPTOR-ARRAY-ENTRY-PAYLOAD-UNPROVEN"
                            }));
                            Module.arpArrayShaderProbeDone = 1;
                        })
                        .catch(error => {
                            Module.print("ARRAY_PROBE_EXCEPTION " + String(error));
                            Module.arpArrayShaderProbeDone = -1;
                        });
                // clang-format on
            },
            bytes.data(),
            bytes.size(),
            name);
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpArrayShaderProbeDone;
                // clang-format on
            }) != 0;
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpArrayShaderProbeDone;
            // clang-format on
        }) == 1);
        EM_ASM({
            // clang-format off
                delete Module.arpArrayShaderProbeDone;
            // clang-format on
        });
    }
#endif
};
} // namespace arp::test
