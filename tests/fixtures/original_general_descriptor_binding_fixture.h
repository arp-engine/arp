#pragma once

#include "original_descriptor_buffer_binding_fixture.h"
#include "original_general_descriptor_write_fixture.h"
#include "rhi/frontend/descriptor_general_binding_validation.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include "rhi/backends/webgpu/webgpu_original_general_binding_lowering.h"
#endif

namespace arp::test {
class OriginalGeneralDescriptorBindingFixture final {
    using C = rhi::OriginalGeneralDescriptorBindingContract;

public:
    static void verifyContract()
    {
        OriginalGeneralDescriptorWriteFixture::verifyContract();
        assert(!C::validateRequest({1u, true}));
        assert(C::validateRequest({}));
        assert(C::validateRequest({1u, false}));
        assert(!C::validateRequest({2u, true}));
        assert(C::validateRequest({3u, true}));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = C::lowering(backend);
            assert(d.preparationAvailable == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
            assert(!d.executionAvailable && (d.diagnostic == nullptr) == d.preparationAvailable);
        }
        for (const auto type : {rhi::DescriptorType::UniformBuffer,
                 rhi::DescriptorType::ReadOnlyStorageBuffer,
                 rhi::DescriptorType::StorageBuffer}) {
            rhi::DescriptorBindingDesc b;
            b.type = type;
            b.stages = rhi::ShaderStage::Compute;
            rhi::OriginalCommandDescriptorSetElement e;
            e.write.type = type;
            e.write.resource = {1u};
            e.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
            e.write.bufferBindingRange = {2u, 256u, 16u};
            assert(!C::validateShape(b, e, 2u) && C::validateShape(b, e));
            e.dynamicOffset = 256u;
            assert(C::validateShape(b, e, 2u));
            b.dynamicOffset = true;
            assert(!C::validateShape(b, e, 2u));
            e.write.bufferBindingRange.sizeBytes = 0u;
            assert(C::validateShape(b, e, 2u));
            e.write.bufferBindingRange = {};
            assert(C::validateShape(b, e, 2u));
            e.write.bufferBindingRange = {2u, 256u, 16u};
            b.arrayCount = 2u;
            assert(C::validateShape(b, e, 2u));
            b.arrayCount = 1u;
            b.tableBacked = true;
            assert(C::validateShape(b, e, 2u));
        }
        for (const auto type : {rhi::DescriptorType::Sampler,
                 rhi::DescriptorType::SampledTexture,
                 rhi::DescriptorType::StorageTexture}) {
            rhi::DescriptorBindingDesc b;
            b.type = type;
            b.stages = rhi::ShaderStage::Compute;
            rhi::OriginalCommandDescriptorSetElement e;
            e.write.type = type;
            e.write.resource = {1u};
            e.write.resourceKind = type == rhi::DescriptorType::Sampler
                ? rhi::DescriptorResourceKind::Sampler
                : rhi::DescriptorResourceKind::TextureView;
            assert(!C::validateShape(b, e, 2u));
            e.write.bufferBindingRange = {2u, 0u, 16u};
            assert(C::validateShape(b, e, 2u));
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_FINITE_BINDING_CONTRACT_OK explicit2 buffer-types3 nonbuffer-preserved static-dynamic distinct-profile execution-closed");
        assert(C::lowering(static_cast<rhi::Backend>(UINT32_MAX)).diagnostic);
        for (const auto type : {rhi::DescriptorType::UniformBuffer,
                 rhi::DescriptorType::ReadOnlyStorageBuffer,
                 rhi::DescriptorType::StorageBuffer,
                 rhi::DescriptorType::SampledTexture,
                 rhi::DescriptorType::StorageTexture,
                 rhi::DescriptorType::Sampler}) {
            rhi::DescriptorBindingDesc b;
            b.type = type;
            b.stages = rhi::ShaderStage::Compute;
            rhi::OriginalCommandDescriptorSetElement e;
            e.write.type = type;
            e.write.resource = {1u};
            e.write.resourceKind = type == rhi::DescriptorType::Sampler
                ? rhi::DescriptorResourceKind::Sampler
                : type == rhi::DescriptorType::SampledTexture
                    || type == rhi::DescriptorType::StorageTexture
                ? rhi::DescriptorResourceKind::TextureView
                : rhi::DescriptorResourceKind::Buffer;
            assert(!C::validateShape(b, e));
            for (std::uint32_t field = 0u; field < 12u; ++field) {
                auto bad = e;
                auto shape = b;
                switch (field) {
                case 0u:
                    shape.arrayCount = 2u;
                    break;
                case 1u:
                    shape.tableBacked = true;
                    break;
                case 2u:
                    shape.dynamicOffset = true;
                    break;
                case 3u:
                    shape.stages = rhi::ShaderStage::None;
                    break;
                case 4u:
                    ++bad.write.binding;
                    break;
                case 5u:
                    ++bad.write.arrayElement;
                    break;
                case 6u:
                    bad.write.resource = {};
                    break;
                case 7u:
                    bad.write.physicalLowering.enabled = true;
                    break;
                case 8u:
                    bad.dynamicOffset = 1u;
                    break;
                case 9u:
                    bad.write.bufferBindingRange = {2u, 0u, 16u};
                    break;
                case 10u:
                    bad.write.type = rhi::DescriptorType::InputAttachment;
                    break;
                case 11u:
                    bad.write.resourceKind = type == rhi::DescriptorType::Sampler
                        ? rhi::DescriptorResourceKind::Buffer
                        : rhi::DescriptorResourceKind::Sampler;
                    break;
                }
                assert(C::validateShape(shape, bad));
            }
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_DESCRIPTOR_BINDING_CONTRACT_OK explicit1 six-types scalar-whole four-backends no-recording-or-execution-authority");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Pipeline, std::size_t Count>
    static void verifyActual(rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        const std::array<rhi::OriginalCommandDescriptorSetElement, Count>& elements,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota)
    {
        using Lowering = rhi_webgpu::WebGpuOriginalGeneralBindingLowering;
        const auto check = [&](C::Request request, const auto& e) noexcept {
            WGPUBindGroupEntry entry{};
            entry.binding = UINT32_MAX;
            entry.buffer = reinterpret_cast<WGPUBuffer>(std::uintptr_t{1u});
            std::uint32_t group = UINT32_MAX;
            const auto* id =
                Lowering::lower(request, device, pipeline, sources, 0u, e, entry, group);
            if (id)
                assert(!entry.binding && !entry.buffer && !entry.textureView && !entry.sampler
                    && !entry.offset && !entry.size && !group);
            else {
                const auto owner =
                    rhi::OriginalCommandDescriptorDependencies::capture(*sources, e.sourceIndex);
                assert(owner);
                std::visit(
                    [&](const auto& lease) {
                        using T = std::remove_const_t<
                            typename std::decay_t<decltype(lease)>::element_type>;
                        const auto native =
                            rhi_webgpu::webGpuNativePayloadFor(*lease->originalObject());
                        if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>)
                            assert(entry.buffer == native->buffer
                                && entry.size == lease->layout().sizeBytes && !entry.offset
                                && !entry.textureView && !entry.sampler);
                        else if constexpr (std::is_same_v<T, rhi::OriginalCommandTextureViewSource>)
                            assert(entry.textureView == native->view && !entry.buffer
                                && !entry.sampler && !entry.size);
                        else
                            assert(entry.sampler == native->sampler && !entry.buffer
                                && !entry.textureView && !entry.size);
                    },
                    *owner);
            }
            return id;
        };
        for (const auto& e : elements) {
            const auto* first = check({1u, true}, e);
            if (first)
                std::fprintf(stderr, "general native binding %u: %s\n", e.write.binding, first);
            assert(!first);
            assert(check({}, e));
            assert(check({1u, false}, e));
            assert(check({3u, true}, e));
            for (std::uint32_t field = 0u; field < 7u; ++field) {
                auto bad = e;
                switch (field) {
                case 0u:
                    ++bad.registrationGeneration;
                    break;
                case 1u:
                    bad.sourceIndex = SIZE_MAX;
                    break;
                case 2u:
                    bad.write.physicalLowering.enabled = true;
                    break;
                case 3u:
                    bad.write.arrayElement = 1u;
                    break;
                case 4u:
                    bad.dynamicOffset = 1u;
                    break;
                case 5u:
                    bad.stages = rhi::PipelineStage::FragmentShader;
                    break;
                case 6u:
                    bad.bufferAccessBytes = e.bufferAccessBytes ? e.bufferAccessBytes - 1u : 1u;
                    break;
                }
                assert(check({1u, true}, bad));
            }
            const auto owner =
                rhi::OriginalCommandDescriptorDependencies::capture(*sources, e.sourceIndex);
            std::visit(
                [&](const auto& lease) {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(lease)>::element_type>;
                    auto native = rhi_webgpu::webGpuNativePayloadFor(*lease->originalObject());
                    if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>) {
                        const auto handle = native->buffer;
                        native->buffer = nullptr;
                        assert(check({1u, true}, e));
                        native->buffer = handle;
                        const auto size = native->sizeBytes;
                        ++native->sizeBytes;
                        assert(check({1u, true}, e));
                        native->sizeBytes = size;
                    } else if constexpr (std::is_same_v<T, rhi::OriginalCommandTextureViewSource>) {
                        const auto handle = native->view;
                        native->view = nullptr;
                        assert(check({1u, true}, e));
                        native->view = handle;
                        native->view = reinterpret_cast<WGPUTextureView>(std::uintptr_t{1u});
                        assert(check({1u, true}, e));
                        native->view = handle;
                        const auto parent = native->parentTexture;
                        native->parentTexture.reset();
                        assert(check({1u, true}, e));
                        native->parentTexture = parent;
                    } else {
                        const auto handle = native->sampler;
                        native->sampler = nullptr;
                        assert(check({1u, true}, e));
                        native->sampler = handle;
                        // A different nonnull handle must fail frozen registry identity.
                        native->sampler = reinterpret_cast<WGPUSampler>(std::uintptr_t{1u});
                        assert(check({1u, true}, e));
                        native->sampler = handle;
                        const auto originalDevice = native->ownerDevice;
                        native->ownerDevice = nullptr;
                        assert(check({1u, true}, e));
                        native->ownerDevice = originalDevice;
                        rhi_webgpu::WebGpuOrdinaryResourceObservation observation;
                        assert(!rhi_webgpu::observeOriginalWebGpuResource(
                            *lease->originalObject(), observation));
                        native->sampler = reinterpret_cast<WGPUSampler>(std::uintptr_t{1u});
                        assert(rhi_webgpu::validateOriginalWebGpuResource(
                            *lease->originalObject(), observation));
                        native->sampler = handle;
                        assert(!rhi_webgpu::validateOriginalWebGpuResource(
                            *lease->originalObject(), observation));
                    }
                },
                *owner);
            assert(!check({1u, true}, e));
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota}, [&]() noexcept -> const char* {
                for (const auto& e : elements)
                    if (const auto* id = check({1u, true}, e))
                        return id;
                return nullptr;
            }));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            for (const auto& e : elements)
                assert(!check({1u, true}, e));
        });
        std::puts(
            "BINDLESS_RHI_GENERAL_DESCRIPTOR_NATIVE_BINDING_OK exact-original-pipeline canonical-owners frozen-registry whole-target-buffer texture-parent sampler scalar-mapping cleared-failure hot0-query0 no-group-or-execution-authority");
    }
#endif
};
} // namespace arp::test
