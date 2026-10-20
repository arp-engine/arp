#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "rhi/frontend/general_initialized_buffer_content.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalGeneralFiniteInitializedContentFixture final {
public:
    static void verifyContract(rhi::ShaderStage shaderStage = rhi::ShaderStage::Compute)
    {
        assert(
            shaderStage == rhi::ShaderStage::Compute || shaderStage == rhi::ShaderStage::Fragment);
        const auto pipelineStage = shaderStage == rhi::ShaderStage::Compute
            ? rhi::PipelineStage::ComputeShader
            : rhi::PipelineStage::FragmentShader;
        using Content = rhi::OriginalGeneralFiniteInitializedBufferContentContract;
        using Upload = Content::Initialized;
        const Content::Request request{1u, true, Upload::retainedOwnerBound, 128u, 128u};
        assert(!Content::validateRequest(request));
        auto badRequest = request;
        badRequest.contractVersion = 0u;
        assert(Content::validateRequest(badRequest));
        badRequest = request;
        badRequest.contractVersion = 2u;
        assert(Content::validateRequest(badRequest));
        badRequest = request;
        badRequest.prototypeOptIn = false;
        assert(Content::validateRequest(badRequest));
        badRequest = request;
        --badRequest.maxRetainedOwners;
        assert(Content::validateRequest(badRequest));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto result = Content::lowering(backend);
            assert(!result.executionAvailable
                && result.associationAvailable == (backend == rhi::Backend::WebGPU));
            assert(result.validatorOnly == (backend == rhi::Backend::Null));
            assert((result.mode == rhi::CapabilityMode::Equivalent) == result.associationAvailable);
            assert((result.diagnostic == nullptr) == result.associationAvailable);
        }
        assert(!Content::lowering(static_cast<rhi::Backend>(UINT32_MAX)).associationAvailable);
        Upload::Budget initialization{4u,
            true,
            1u,
            Upload::callbackCount,
            Upload::retainedOwnerBound,
            128u,
            65536u,
            65536u,
            65536u,
            65536u,
            65536u};
        rhi::DescriptorBindingDesc binding;
        binding.dynamicOffset = true;
        binding.stages = shaderStage;
        rhi::OriginalCommandDescriptorSetElement element;
        element.write.resource = {17u};
        element.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
        element.write.resourceState = rhi::ResourceState::ShaderRead;
        element.write.bufferBindingRange = {2u, 32u, 16u};
        element.dynamicOffset = 16u;
        element.bufferAccessOffset = 48u;
        element.bufferAccessBytes = 16u;
        element.stages = pipelineStage;
        element.access = rhi::ResourceAccess::ShaderRead;
        for (const auto type :
            {rhi::DescriptorType::UniformBuffer, rhi::DescriptorType::ReadOnlyStorageBuffer}) {
            binding.type = element.write.type = type;
            initialization.shaderContent = {1u, true, type, 128u};
            const auto check = [&](const auto& req,
                                   const auto& init,
                                   const auto& e,
                                   std::uint32_t version = 5u,
                                   std::uint64_t backing = 128u,
                                   std::uint64_t payload = 128u,
                                   std::uint64_t allocation = 128u) noexcept {
                Content::Resolved result{999u, 999u};
                const auto* id = Content::validateDeclaration(
                    req, version, init, binding, e, backing, payload, allocation, result);
                if (id)
                    assert(!result.offsetBytes && !result.sizeBytes);
                else
                    assert(result.offsetBytes == e.bufferAccessOffset
                        && result.sizeBytes == e.bufferAccessBytes);
                return id;
            };
            assert(!check(request, initialization, element));
            auto staticView = element;
            staticView.dynamicOffset = 0u;
            staticView.bufferAccessOffset = 32u;
            assert(!check(request, initialization, staticView));
            for (const auto old : {1u, 2u, 3u, 4u, 6u})
                assert(check(request, initialization, element, old));
            // A sixteen-byte selected view cannot discount complete retained bytes.
            badRequest = request;
            badRequest.maxInitializedBytes = 16u;
            assert(check(badRequest, initialization, element));
            badRequest = request;
            --badRequest.maxInitializedBytes;
            assert(check(badRequest, initialization, element));
            badRequest = request;
            --badRequest.maxRetainedNativeBytes;
            assert(check(badRequest, initialization, element));
            assert(check(request, initialization, element, 5u, 127u));
            assert(check(request, initialization, element, 5u, 128u, 16u));
            assert(check(request, initialization, element, 5u, 128u, 128u, 16u));
            auto badInitialization = initialization;
            badInitialization.shaderContent.bindingBytes = 16u;
            assert(check(request, badInitialization, element));
            badInitialization = initialization;
            badInitialization.contractVersion = 3u;
            assert(check(request, badInitialization, element));
            auto hostile = element;
            hostile.write.bufferBindingRange.offsetBytes = UINT64_MAX;
            assert(check(request, initialization, hostile));
            hostile = element;
            hostile.dynamicOffset = UINT32_MAX;
            assert(check(request, initialization, hostile));
            hostile = element;
            --hostile.bufferAccessBytes;
            assert(check(request, initialization, hostile));
            hostile = element;
            ++hostile.bufferAccessOffset;
            assert(check(request, initialization, hostile));
            hostile = element;
            hostile.access = rhi::ResourceAccess::ShaderWrite;
            assert(check(request, initialization, hostile));
            hostile = element;
            hostile.write.type = rhi::DescriptorType::StorageBuffer;
            assert(check(request, initialization, hostile));
            // The exact end is legal; containment cannot wrap at either offset.
            auto endpoint = element;
            endpoint.dynamicOffset = 80u;
            endpoint.bufferAccessOffset = 112u;
            assert(!check(request, initialization, endpoint));
            ++endpoint.dynamicOffset;
            ++endpoint.bufferAccessOffset;
            assert(check(request, initialization, endpoint));
            auto whole = element;
            whole.write.bufferBindingRange = {};
            whole.dynamicOffset = 0u;
            whole.bufferAccessOffset = 0u;
            whole.bufferAccessBytes = 128u;
            binding.dynamicOffset = false;
            assert(!check(request, initialization, whole));
            binding.dynamicOffset = true;
            // Staging/4 continues to describe complete backing, independently of views.
            assert(!Upload::validateShaderPayload(initialization, 128u));
            assert(Upload::validateShaderPayload(initialization, 16u));
        }
        if (shaderStage == rhi::ShaderStage::Fragment)
            std::puts(
                "BINDLESS_RHI_GENERAL_GRAPHICS_INITIALIZED_CONTENT_CONTRACT_OK Fragment request1 Source5 Staging4 complete-backing static-dynamic-whole backends4 execution-closed");
        else
            std::puts(
                "BINDLESS_RHI_GENERAL_FINITE_INITIALIZED_CONTENT_CONTRACT_OK request1 Source5 Staging4 complete-backing static-dynamic containment old-profile-isolation backends4 execution-closed");
    }

    static void verifyUnsupported(const rhi::GpuDevice& device)
    {
        using Content = rhi::OriginalGeneralFiniteInitializedBufferContentContract;
        const Content::Request request{
            1u, true, Content::Initialized::retainedOwnerBound, 128u, 128u};
        Content::Resolved output{777u, 888u};
        const auto* id =
            rhi_webgpu::WebGpuCanonicalBufferUploadSource::validateGeneralFiniteInitializedContent(
                device, request, {}, {}, 0u, nullptr, 0u, 1u, output);
        assert(core::StringView(id) == Content::lowering(device.backend()).diagnostic);
        assert(!output.offsetBytes && !output.sizeBytes);
    }
};
} // namespace arp::test
