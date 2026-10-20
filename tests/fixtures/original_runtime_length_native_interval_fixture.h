#pragma once
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_finite_descriptor_write_fixture.h"
#include "original_runtime_length_native_pipeline_fixture.h"
#include "original_whole_descriptor_write_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_runtime_length_interval_lowering.h"

namespace arp::test {
// Actual native observations using existing original owners. This fixture never
// opens the descriptor-write/request2 or canonical command execution gates.
class OriginalRuntimeLengthNativeIntervalFixture final {
    static constexpr std::uint64_t cpuBytes = 64u * 1024u, maxInputBytes = 4096u, outputBytes = 8u;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Contract = rhi::OriginalDescriptorBufferBindingContract;
    using Lease = rhi::OriginalCommandBufferLease;
    using Lowering = rhi_webgpu::WebGpuOriginalRuntimeLengthIntervalLowering;
    using ComputeContext = OriginalAcceptedComputePipelineFactoryFixture::PreparedContext;

    struct LossCase final {
        core::UniquePtr<ComputeContext> context;
        std::shared_ptr<rhi::ComputePipeline> pipeline;
        std::shared_ptr<const Lease> buffer;
        std::shared_ptr<const Lease> output;
        std::shared_ptr<core::CpuMemoryQuota> quota;
        std::shared_ptr<core::CpuMemoryQuota> outputQuota;
        core::UniquePtr<OriginalFiniteDescriptorWriteFixture> finite;
        std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u> writes;
        std::shared_ptr<core::CpuMemoryQuota> inputQuota, artifactQuota;
    };

    inline static core::UniquePtr<LossCase> loss_;

    template <class Context>
    static auto buffer(Context& c,
        std::uint64_t bytes,
        rhi::ResourceUsage usage,
        rhi::MemoryUsage memory,
        const std::shared_ptr<core::CpuMemoryQuota>& quota)
    {
        rhi::BufferDesc desc;
        desc.sizeBytes = bytes;
        desc.usage = usage;
        desc.memory = memory;
        desc.initialState = rhi::ResourceState::Undefined;
        core::UniquePtr<rhi::GpuBuffer> object;
        {
            core::CpuMemoryQuotaScope scope(c.quotas[2]);
            object = c.device->createBuffer(desc);
        }
        assert(object);
        assert(!c.device->validateOriginalResourceRegistration(*object));
        const char* diagnostic = nullptr;
        auto lease = Lease::prepareOriginal(
            object, {1u, true, Lease::retainedOwnerCount, cpuBytes}, quota, diagnostic);
        assert(lease && !object && !diagnostic);
        return lease;
    }

    template <class Context, class Pipeline, class Factory>
    static void verifyJoin(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const Lease>& source,
        const std::shared_ptr<const Lease>& wrongUsage,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        std::uint32_t alignment,
        Factory&& create)
    {
        Contract::IntervalDiagnosticObservation observation;
        const auto check = [&](Contract::IntervalDiagnosticRequest request,
                               const auto& owner,
                               const auto& lease,
                               std::uint32_t set,
                               std::uint32_t binding,
                               rhi::DescriptorBufferBindingRange range,
                               std::uint32_t dynamic) noexcept {
            observation.range = {99u, 99u};
            observation.runtimeLength = 99u;
            observation.minimumBindingBytes = 99u;
            const auto* id = c.device->diagnoseOriginalRuntimeLengthInterval(
                request, owner, lease, set, binding, range, dynamic, observation);
            auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.buffer = reinterpret_cast<WGPUBuffer>(std::uintptr_t{1u});
            entry.offset = 99u;
            entry.size = 99u;
            std::uint32_t group = 99u, displacement = 99u;
            const auto* nativeId = Lowering::lower(request,
                *c.device,
                owner,
                lease,
                set,
                binding,
                range,
                dynamic,
                entry,
                group,
                displacement);
            assert(bool(id) == bool(nativeId));
            if (nativeId)
                assert(!entry.buffer && !entry.offset && !entry.size && !group && !displacement);
            else
                assert(entry.buffer && entry.offset == range.offsetBytes
                    && entry.size == range.sizeBytes && entry.binding == observation.binding
                    && group == observation.group && displacement == dynamic);
            if (id)
                assert(!observation.range.offsetBytes && !observation.range.sizeBytes
                    && !observation.runtimeLength && !observation.minimumBindingBytes);
            else
                assert(observation.range.offsetBytes == range.offsetBytes + dynamic
                    && observation.range.sizeBytes == range.sizeBytes && observation.runtimeLength
                    && !observation.nativeExecutionProven());
            return id;
        };
        const rhi::DescriptorBufferBindingRange range{2u, alignment, 16u};
        assert(!check({1u, true}, pipeline, source, 0u, 0u, range, alignment));
        assert(check({}, pipeline, source, 0u, 0u, range, 0u));
        assert(check({2u, true}, pipeline, source, 0u, 0u, range, 0u));
        assert(check({1u, false}, pipeline, source, 0u, 0u, range, 0u));
        assert(check({1u, true}, std::shared_ptr<Pipeline>{}, source, 0u, 0u, range, 0u));
        auto forgedPipeline = std::shared_ptr<Pipeline>(sourceQuota, pipeline.get());
        assert(check({1u, true}, forgedPipeline, source, 0u, 0u, range, 0u));
        forgedPipeline.reset();
        auto forgedLease = std::shared_ptr<const Lease>(c.quotas[0], source.get());
        assert(check({1u, true}, pipeline, forgedLease, 0u, 0u, range, 0u));
        forgedLease.reset();
        assert(check({1u, true}, pipeline, std::shared_ptr<const Lease>{}, 0u, 0u, range, 0u));
        assert(check({1u, true}, pipeline, wrongUsage, 0u, 0u, range, 0u));
        assert(check({1u, true}, pipeline, source, 1u, 0u, range, 0u));
        assert(check({1u, true}, pipeline, source, 0u, 2u, range, 0u));
        assert(check({1u, true}, pipeline, source, 0u, 0u, {}, 0u));
        assert(check({1u, true}, pipeline, source, 0u, 0u, {2u, 1u, 16u}, alignment - 1u));
        assert(check({1u, true}, pipeline, source, 0u, 0u, {2u, alignment, 1u}, 0u));
        assert(check(
            {1u, true}, pipeline, source, 0u, 0u, {2u, alignment, source->layout().sizeBytes}, 0u));
        auto foreign = create();
        assert(foreign);
        assert(foreign->diagnoseOriginalRuntimeLengthInterval(
            {1u, true}, pipeline, source, 0u, 0u, range, 0u, observation));
        // A separate logical registry on this same native device cannot supply
        // an original buffer. Reuse the existing context's exact authored budget.
        auto lifetime = core::makeShared<core::CpuMemoryQuota>(cpuBytes),
             registry = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto foreignLeaseQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        assert(!foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, lifetime));
        rhi::OriginalResourceRegistrationBudget budget;
        budget.contractVersion = 5u;
        budget.prototypeOptIn = true;
        budget.maxBuffers = 1u;
        budget.maxRetainedOwners = rhi::OriginalResourceRegistrationContract::fixedOwnerCount
            + rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
        budget.maxCpuMetadataBytes = cpuBytes;
        assert(!foreign->prepareOriginalResourceRegistration(budget, registry));
        rhi::BufferDesc foreignDesc;
        foreignDesc.sizeBytes = source->layout().sizeBytes;
        foreignDesc.usage = rhi::ResourceUsage::StorageBuffer;
        core::UniquePtr<rhi::GpuBuffer> object;
        {
            core::CpuMemoryQuotaScope scope(foreignLeaseQuota);
            object = foreign->createBuffer(foreignDesc);
        }
        assert(object);
        const char* diagnostic = nullptr;
        auto foreignLease = Lease::prepareOriginal(
            object, {1u, true, Lease::retainedOwnerCount, cpuBytes}, foreignLeaseQuota, diagnostic);
        assert(foreignLease);
        assert(check({1u, true}, pipeline, foreignLease, 0u, 0u, range, 0u));
        foreignLease.reset();
        foreign.reset();
        assert(!foreignLeaseQuota->liveBytes() && !registry->liveBytes() && !lifetime->liveBytes());
        sourceQuota->setLimitBytes(cpuBytes - 1u);
        assert(check({1u, true}, pipeline, source, 0u, 0u, range, 0u));
        sourceQuota->setLimitBytes(cpuBytes);
        for (const auto& q : c.quotas) {
            if (q == c.quotas[2])
                continue; // Layout/ordinary object allocation has no independently stamped limit.
            q->setLimitBytes(cpuBytes - 1u);
            assert(check({1u, true}, pipeline, source, 0u, 0u, range, 0u));
            q->setLimitBytes(cpuBytes);
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                c.quotas, [&]() noexcept -> const char* {
                    return check({1u, true}, pipeline, source, 0u, 0u, range, alignment);
                }));
        });
        EM_ASM({
            // clang-format off
                Module.arpIntervalPhysical = Module.preinitializedWebGPUDevice;
                Module.preinitializedWebGPUDevice = {};
            // clang-format on
        });
        assert(check({1u, true}, pipeline, source, 0u, 0u, range, 0u));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice = Module.arpIntervalPhysical;
                delete Module.arpIntervalPhysical;
            // clang-format on
        });
        assert(!check({1u, true}, pipeline, source, 0u, 0u, range, 0u));
        assert(pipeline->originalDescriptorBufferBindingDiagnostic(
            {2u, true}, rhi::DescriptorType::StorageBuffer, 16u, 0u));
        assert(rhi::DescriptorBufferBindingRangeContract::writeDiagnostic(range, true));
        const auto native = rhi_webgpu::webGpuNativePayloadFor(*source->originalObject());
        assert(native);
        const auto rejectNative = [&] {
            auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.offset = 99u;
            entry.size = 99u;
            std::uint32_t group = 99u, displacement = 99u;
            assert(Lowering::lower({1u, true},
                       *c.device,
                       pipeline,
                       source,
                       0u,
                       0u,
                       range,
                       alignment,
                       entry,
                       group,
                       displacement)
                && !entry.buffer && !entry.offset && !entry.size && !group && !displacement);
        };
        const auto extent = native->sizeBytes;
        native->sizeBytes = extent + 4u;
        rejectNative();
        native->sizeBytes = extent;
        const auto device = native->ownerDevice;
        native->ownerDevice = nullptr;
        rejectNative();
        native->ownerDevice = device;
        // An approved diagnostic entry never opens legacy native group/bundle
        // consumers. Test both binding positions so a later finite entry cannot
        // cause earlier entries to query limits before deterministic rejection.
        core::pmr::Vector<rhi_webgpu::WebGpuNativeDescriptorBindingPayload> bindings;
        const auto& declaration = pipeline->originalLayoutOwner()->desc().descriptorLayouts[0];
        for (const auto& layoutBinding : declaration.bindings) {
            bindings.emplace_back();
            auto& item = bindings.back();
            item.binding = layoutBinding;
            item.write.binding = layoutBinding.binding;
            item.write.type = layoutBinding.type;
            item.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
            item.write.resource = source->originalObject()->id();
            item.buffer = native;
        }
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpClosedRangeLayout = d.createBindGroupLayout;
                Module.arpClosedRangeGroup = d.createBindGroup;
                Module.arpClosedRangeCalls = 0;
                d.createBindGroupLayout = function(s) {
                    Module.arpClosedRangeCalls++;
                    return Module.arpClosedRangeLayout.call(this, s);
                };
                d.createBindGroup = function(s) {
                    Module.arpClosedRangeCalls++;
                    return Module.arpClosedRangeGroup.call(this, s);
                };
            // clang-format on
        });
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            for (std::size_t i = 0u; i < bindings.size(); ++i) {
                bindings[i].write.bufferBindingRange = range;
                assert(
                    !rhi_webgpu::createWebGpuNativeDescriptorGroupPayload(declaration, bindings));
                bindings[i].write.bufferBindingRange = {};
            }
            rhi_webgpu::WebGpuNativeDescriptorGroupPayload group;
            group.bindings = bindings;
            group.bindings[0].write.bufferBindingRange = range;
            assert(core::StringView(
                       rhi_webgpu::WebGpuDescriptorDynamicOffsetValidation::validateBinding(
                           group, 0u, alignment, {}))
                == "RHI-DESCRIPTOR-BUFFER-SUBRANGE-LOWERING-MISSING");
        });
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpClosedRangeCalls === 0;
            // clang-format on
        }));
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                d.createBindGroupLayout = Module.arpClosedRangeLayout;
                d.createBindGroup = Module.arpClosedRangeGroup;
                delete Module.arpClosedRangeLayout;
                delete Module.arpClosedRangeGroup;
                delete Module.arpClosedRangeCalls;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_ORIGINAL_RUNTIME_INTERVAL_LEGACY_CLOSED_OK original-native-buffer both-binding-positions group-layout-calls0 queries0 dynamic-bundle-reject diagnostic-entry-independent");
    }

    template <class Context, class Pipeline, class Numeric>
    static void wholeScopes(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        OriginalFiniteDescriptorWriteFixture& target,
        const std::array<std::shared_ptr<const Lease>, 2u>& buffers,
        Numeric&& numeric)
    {
        std::array<rhi::OriginalWholeDescriptorBufferWrite, 2u> writes;
        for (std::size_t i = 0u; i < writes.size(); ++i) {
            writes[i].buffer = buffers[i];
            writes[i].write.binding = static_cast<std::uint32_t>(i);
            writes[i].write.type = c.layout->desc().descriptorLayouts[0].bindings[i].type;
            writes[i].write.resourceKind = rhi::DescriptorResourceKind::Buffer;
            writes[i].write.resource = buffers[i]->resource();
        }
        auto payload = OriginalWholeDescriptorWriteFixture::verifyActual(
            c, pipeline, *target.descriptor, 0u, writes);
        assert(!target.descriptor->originalScopedContentDiagnostic());
        numeric(payload);
        payload.reset();
        OriginalWholeDescriptorWriteFixture::verifyPendingTermination(
            c, pipeline, target.descriptor, target.arena, 0u, writes, false);
        target.descriptor.reset();
        target.arena->reset();
        target.allocateTarget(c);
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> committedQuotas;
        OriginalWholeDescriptorWriteFixture::commitActual(
            c, pipeline, *target.descriptor, 0u, writes, committedQuotas);
        OriginalWholeDescriptorWriteFixture::verifyPendingTermination(
            c, pipeline, target.descriptor, target.arena, 0u, writes, true);
        for (const auto& quota : committedQuotas)
            assert(!quota->liveBytes());
        assert(!target.descriptor);
        target.arena->reset();
        target.allocateTarget(c);
    }

    template <class Context, class Wait, class Map>
    static void graphics(Context& c,
        const std::shared_ptr<rhi::RenderPipeline>& pipeline,
        const std::shared_ptr<const Lease>& input,
        std::uint32_t alignment,
        Wait&& wait,
        Map&& map)
    {
        constexpr std::uint32_t width = 2u, height = 2u;
        const auto aligned = core::alignCpuAllocationOffsetUp(
            width * rhi::bytesPerPixel(rhi::TextureFormat::Rgba8Unorm),
            rhi::kTextureUploadRowPitchAlignment);
        assert(aligned.success && aligned.alignedValue <= UINT32_MAX);
        const auto pitch = static_cast<std::uint32_t>(aligned.alignedValue);
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto download = buffer(c,
            std::uint64_t(pitch) * height,
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            quota);
        rhi::TextureDesc desc;
        desc.extent = {width, height, 1u};
        desc.format = rhi::TextureFormat::Rgba8Unorm;
        desc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
        desc.memory = rhi::MemoryUsage::GpuOnly;
        desc.initialState = rhi::ResourceState::Undefined;
        desc.mipLevelCount = 1u;
        desc.arrayLayerCount = 1u;
        desc.sampleCount = 1u;
        core::UniquePtr<rhi::GpuTexture> texture;
        core::UniquePtr<rhi::GpuTextureView> view;
        {
            core::CpuMemoryQuotaScope scope(c.quotas[2]);
            texture = c.device->createTexture(desc);
            assert(texture);
            rhi::TextureViewDesc fields;
            fields.format = desc.format;
            fields.usage = desc.usage;
            fields.aspect = rhi::TextureAspect::Color;
            view = c.device->createTextureView(*texture, fields);
            assert(view);
        }
        assert(!c.device->validateOriginalResourceRegistration(*texture)
            && !c.device->validateOriginalResourceRegistration(*view));
        const char* attachmentDiagnostic = nullptr;
        const auto& sourceQuota = input->originalMetadataQuota();
        auto textureLease = rhi::OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, cpuBytes}, sourceQuota, attachmentDiagnostic);
        assert(textureLease);
        auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            view, {1u, true, 2u, cpuBytes}, sourceQuota, attachmentDiagnostic);
        assert(viewLease);
        auto viewSource = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            viewLease, textureLease, {1u, true, 5u, cpuBytes}, sourceQuota, attachmentDiagnostic);
        assert(viewSource);
        const std::array<rhi::OriginalCommandResourceSources::Source, 2u> attachments{
            textureLease, viewSource};
        {
            auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
            auto source = rhi_webgpu::webGpuNativePayloadFor(*input->originalObject());
            auto target = rhi_webgpu::webGpuNativePayloadFor(*textureLease->originalObject());
            auto targetView = rhi_webgpu::webGpuNativePayloadFor(*viewLease->originalObject());
            auto readback = rhi_webgpu::webGpuNativePayloadFor(*download->originalObject());
            assert(
                native && native->issuedContract() && source && target && targetView && readback);
            auto device = static_cast<WGPUDevice>(native->issuedContract()->nativeDevice());
            assert(device && source->ownerDevice == device && target->ownerDevice == device
                && targetView->parentTexture == target && readback->ownerDevice == device);
            OriginalFiniteDescriptorWriteFixture finite(c);
            auto queue = wgpuDeviceGetQueue(device);
            assert(queue);
            const auto record = [&](WGPUBindGroup group,
                                    std::uint32_t physicalGroup,
                                    std::uint32_t offsetCount,
                                    const std::uint32_t* offsets,
                                    const std::array<std::uint64_t, 4u>& expected) {
                rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
                auto encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
                auto encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
                assert(encoder);
                auto color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                color.view = targetView->view;
                color.loadOp = WGPULoadOp_Clear;
                color.storeOp = WGPUStoreOp_Store;
                color.clearValue = {1.0, 1.0, 1.0, 1.0};
                auto passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.colorAttachmentCount = 1u;
                passDesc.colorAttachments = &color;
                auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                assert(pass);
                wgpuRenderPassEncoderSetPipeline(pass, native->pipeline);
                const auto& state = pipeline->originalGraphicsCreation()->definition();
                assert(state.hasViewport && state.hasScissor);
                wgpuRenderPassEncoderSetViewport(pass,
                    state.viewport.x,
                    state.viewport.y,
                    state.viewport.width,
                    state.viewport.height,
                    state.viewport.minDepth,
                    state.viewport.maxDepth);
                wgpuRenderPassEncoderSetScissorRect(pass,
                    state.scissor.x,
                    state.scissor.y,
                    state.scissor.width,
                    state.scissor.height);
                wgpuRenderPassEncoderSetBindGroup(pass, physicalGroup, group, offsetCount, offsets);
                wgpuRenderPassEncoderDraw(pass, 3u, 1u, 0u, 0u);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
                auto from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                from.texture = target->texture;
                from.aspect = WGPUTextureAspect_All;
                auto to = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
                to.buffer = readback->buffer;
                to.layout.bytesPerRow = pitch;
                to.layout.rowsPerImage = height;
                const WGPUExtent3D extent{width, height, 1u};
                wgpuCommandEncoderCopyTextureToBuffer(encoder, &from, &to, &extent);
                auto commandDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                auto command = wgpuCommandEncoderFinish(encoder, &commandDesc);
                assert(command);
                wgpuQueueSubmit(queue, 1u, &command);
                assert(wait(queue) && map(readback->buffer, download->layout().sizeBytes));
                const auto* bytes = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(
                    readback->buffer, 0u, download->layout().sizeBytes));
                assert(bytes);
                for (std::uint32_t y = 0u; y < height; ++y)
                    for (std::uint32_t x = 0u; x < width; ++x)
                        for (std::size_t channel = 0u; channel < expected.size(); ++channel)
                            assert(bytes[y * pitch + x * rhi::bytesPerPixel(desc.format) + channel]
                                == expected[channel]);
                const std::array<std::uint8_t, 4u> result{bytes[0], bytes[1], bytes[2], bytes[3]};
                wgpuBufferUnmap(readback->buffer);
                wgpuCommandBufferRelease(command);
                wgpuCommandEncoderRelease(encoder);
                assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device),
                    rhi_webgpu::WebGpuNativeErrorScopeBatch::count,
                    0u));
                return result;
            };
            wholeScopes(c, pipeline, finite, {input, input}, [&](const auto& payload) {
                const auto shared =
                    OriginalWholeDescriptorWriteFixture::originalTarget(pipeline, 0u, 0u);
                const auto fragment =
                    OriginalWholeDescriptorWriteFixture::originalTarget(pipeline, 0u, 1u);
                std::uint64_t count = 0u, fragmentCount = 0u;
                assert(!shared.runtimeArrayLength(input->layout().sizeBytes, count)
                    && !fragment.runtimeArrayLength(input->layout().sizeBytes, fragmentCount));
                // The authored shader divides each integer by255; RGBA8 UNORM
                // clamps that normalized output to[0,1]. This is format math,
                // not an inferred resource size or compatibility fallback.
                const auto unorm = [](std::uint64_t value) {
                    return std::min<std::uint64_t>(value, UINT8_MAX);
                };
                const std::array<std::uint64_t, 4u> expected{unorm(count),
                    unorm(shared.runtimeArrayStrideBytes),
                    unorm(count),
                    unorm(fragmentCount)};
                const std::uint32_t zero = 0u;
                const auto pixels = record(payload->group,
                    shared.group,
                    payload->dynamicOffsetCount,
                    payload->dynamicOffsetCount ? &zero : nullptr,
                    expected);
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_GRAPHICS_VALUES extent%llu pixels%u,%u,%u,%u expected%llu,%llu,%llu,%llu exact-committed-group pixels4 zero-dynamics real-completion-map\n",
                    static_cast<unsigned long long>(input->layout().sizeBytes),
                    pixels[0],
                    pixels[1],
                    pixels[2],
                    pixels[3],
                    static_cast<unsigned long long>(expected[0]),
                    static_cast<unsigned long long>(expected[1]),
                    static_cast<unsigned long long>(expected[2]),
                    static_cast<unsigned long long>(expected[3]));
            });
            const std::array<std::uint64_t, 5> sizes{16u, 32u, 36u, 48u, 60u};
            for (std::size_t i = 0u; i < sizes.size(); ++i) {
                const auto dynamic = i % 2u ? alignment : 0u;
                const rhi::DescriptorBufferBindingRange shared{2u, alignment, sizes[i]},
                    fragment{2u, 2u * alignment, 4u * (i + 1u)};
                Contract::IntervalDiagnosticObservation sharedFacts, fragmentFacts;
                assert(!c.device->diagnoseOriginalRuntimeLengthInterval(
                    {1u, true}, pipeline, input, 0u, 0u, shared, dynamic, sharedFacts));
                assert(!c.device->diagnoseOriginalRuntimeLengthInterval(
                    {1u, true}, pipeline, input, 0u, 1u, fragment, 0u, fragmentFacts));
                assert(sharedFacts.stageMask
                        == static_cast<std::uint32_t>(
                            rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment)
                    && fragmentFacts.stageMask
                        == static_cast<std::uint32_t>(rhi::ShaderStage::Fragment));
                Contract::IntervalDiagnosticObservation rejected;
                rejected.runtimeLength = 99u;
                assert(c.device->diagnoseOriginalRuntimeLengthInterval(
                           {1u, true}, pipeline, input, 0u, 1u, fragment, alignment, rejected)
                    && !rejected.runtimeLength && !rejected.range.sizeBytes);
                assert(sharedFacts.group == fragmentFacts.group
                    && sharedFacts.binding != fragmentFacts.binding);
                const std::array<std::uint64_t, 4> expected{sharedFacts.runtimeLength,
                    sharedFacts.runtimeStrideBytes,
                    sharedFacts.runtimeLength,
                    fragmentFacts.runtimeLength};
                for (auto value : expected)
                    assert(value && value < 255u);

                std::array<WGPUBindGroupEntry, 2> entries{
                    WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
                std::uint32_t sharedGroup = 99u, fragmentGroup = 99u, sharedDynamic = 99u,
                              fragmentDynamic = 99u;
                assert(!Lowering::lower({1u, true},
                    *c.device,
                    pipeline,
                    input,
                    0u,
                    0u,
                    shared,
                    dynamic,
                    entries[0],
                    sharedGroup,
                    sharedDynamic));
                assert(!Lowering::lower({1u, true},
                    *c.device,
                    pipeline,
                    input,
                    0u,
                    1u,
                    fragment,
                    0u,
                    entries[1],
                    fragmentGroup,
                    fragmentDynamic));
                assert(sharedGroup == sharedFacts.group && fragmentGroup == fragmentFacts.group
                    && sharedDynamic == dynamic && !fragmentDynamic
                    && entries[0].offset == shared.offsetBytes
                    && entries[0].size == shared.sizeBytes
                    && entries[1].offset == fragment.offsetBytes
                    && entries[1].size == fragment.sizeBytes);
                std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u> authored;
                for (std::size_t j = 0u; j < authored.size(); ++j) {
                    authored[j].buffer = input;
                    authored[j].dynamicOffset = j ? 0u : dynamic;
                    authored[j].write.binding = static_cast<std::uint32_t>(j);
                    authored[j].write.type = c.layout->desc().descriptorLayouts[0].bindings[j].type;
                    authored[j].write.resource = input->originalObject()->id();
                    authored[j].write.resourceKind = rhi::DescriptorResourceKind::Buffer;
                    authored[j].write.bufferBindingRange = j ? fragment : shared;
                }
                if (!i) {
                    finite.budgets(c, pipeline, authored);
                    finite.nativeFailure(c, pipeline, authored);
                }
                auto committed = finite.commit(c, pipeline, authored, !i);
                auto group = committed->group;
                if (i + 1u == sizes.size()) {
                    finite.sourceCustody(c, pipeline, authored, true, &attachments);
                    auto adopted = rhi_webgpu::webGpuNativePayloadFor(
                        *finite.retainedFiniteSource->originalObject());
                    assert(adopted);
                    std::uint32_t adoptedGroup = 99u, adoptedCount = 99u;
                    const std::uint32_t* adoptedOffsets = nullptr;
                    assert(!adopted->diagnoseOriginalFiniteGroup(
                               adoptedGroup, adoptedOffsets, adoptedCount)
                        && adoptedGroup == sharedGroup && adoptedCount == 1u
                        && adoptedOffsets[0] == sharedDynamic);
                    group = adopted->group;
                }
                const auto bytes = record(group, sharedGroup, 1u, &sharedDynamic, expected);
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_RUNTIME_INTERVAL_GRAPHICS_VALUES case%zu base%llu dynamic%u size%llu bytes%u,%u,%u,%u expected%llu,%llu,%llu,%llu\n",
                    i,
                    static_cast<unsigned long long>(entries[0].offset),
                    sharedDynamic,
                    static_cast<unsigned long long>(entries[0].size),
                    bytes[0],
                    bytes[1],
                    bytes[2],
                    bytes[3],
                    static_cast<unsigned long long>(expected[0]),
                    static_cast<unsigned long long>(expected[1]),
                    static_cast<unsigned long long>(expected[2]),
                    static_cast<unsigned long long>(expected[3]));
                if (i + 1u == sizes.size())
                    std::puts(
                        "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_CUSTODY_GRAPHICS_OUTPUT_OK exact-source3-adopted-group pixels4 match-runtime-length");

                if (i + 1u == sizes.size()) {
                    // Numeric work completed; release its adopted set before
                    // the independent caller-drop probe uses the second slot.
                    committed.reset();
                    finite.recreateTarget(c, pipeline, authored);
                    finite.negatives(c, pipeline, authored);
                    finite.sourceCustody(c, pipeline, authored, false, &attachments);
                    finite.ordinaryPositional(c, pipeline, authored, &attachments);
                    finite.recreateTarget(c, pipeline, authored, false);
                    finite.sourceCustody(c, pipeline, authored, true, &attachments, true);
                }
            }
            wgpuQueueRelease(queue);
            finite.release();
            std::puts(
                "BINDLESS_RHI_ORIGINAL_FINITE_WRITE_GRAPHICS_OK committed-group cases5 pixels4 bounded-native-scopes hot-finish0 source12-closed");
        }
        view.reset();
        texture.reset();
        download.reset();
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_RUNTIME_INTERVAL_NATIVE_GRAPHICS_OK cases5 pixels4 vertex-count-stride fragment-shared-and-only-count original-pipeline-buffers unchanged-size clean-scopes canonical-closed");
    }

    template <class Context, class Wait, class Map>
    static void compute(Context& c,
        const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        const std::shared_ptr<const Lease>& input,
        const std::shared_ptr<const Lease>& output,
        const std::shared_ptr<const Lease>& readback,
        std::uint32_t alignment,
        bool byteAddress,
        Wait&& wait,
        Map&& map)
    {
        auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
        auto source = rhi_webgpu::webGpuNativePayloadFor(*input->originalObject());
        auto destination = rhi_webgpu::webGpuNativePayloadFor(*output->originalObject());
        auto download = rhi_webgpu::webGpuNativePayloadFor(*readback->originalObject());
        assert(native && source && destination && download && native->issuedContract());
        auto device = static_cast<WGPUDevice>(native->issuedContract()->nativeDevice());
        assert(device && source->ownerDevice == device && destination->ownerDevice == device
            && download->ownerDevice == device);
        assert(source->sizeBytes == input->layout().sizeBytes
            && destination->sizeBytes == outputBytes && download->sizeBytes == outputBytes);
        Contract::IntervalDiagnosticObservation outputFacts;
        assert(!c.device->diagnoseOriginalRuntimeProfileBufferInterval(
                   {1u, true}, pipeline, output, 0u, 1u, {2u, 0u, outputBytes}, 0u, outputFacts)
            && outputFacts.minimumBindingBytes && outputFacts.binding == 1u);
        assert(c.device->diagnoseOriginalRuntimeLengthInterval(
                   {1u, true}, pipeline, output, 0u, 1u, {2u, 0u, outputBytes}, 0u, outputFacts)
            && !outputFacts.minimumBindingBytes);
        OriginalFiniteDescriptorWriteFixture finite(c);
        auto queue = wgpuDeviceGetQueue(device);
        assert(queue);
        const auto record = [&](WGPUBindGroup group,
                                std::uint32_t physicalGroup,
                                std::uint32_t offsetCount,
                                const std::uint32_t* offsets) {
            rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
            auto encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
            auto encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
            assert(encoder);
            auto passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            auto pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            assert(pass);
            wgpuComputePassEncoderSetPipeline(pass, native->pipeline);
            wgpuComputePassEncoderSetBindGroup(pass, physicalGroup, group, offsetCount, offsets);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, destination->buffer, 0u, download->buffer, 0u, outputBytes);
            auto commandDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            auto command = wgpuCommandEncoderFinish(encoder, &commandDesc);
            assert(command);
            wgpuQueueSubmit(queue, 1u, &command);
            assert(wait(queue) && map(download->buffer, outputBytes));
            const auto* values = static_cast<const std::uint32_t*>(
                wgpuBufferGetConstMappedRange(download->buffer, 0u, outputBytes));
            assert(values);
            const std::array<std::uint32_t, 2u> result{values[0], values[1]};
            wgpuBufferUnmap(download->buffer);
            wgpuCommandBufferRelease(command);
            wgpuCommandEncoderRelease(encoder);
            assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device),
                rhi_webgpu::WebGpuNativeErrorScopeBatch::count,
                0u));
            return result;
        };
        wholeScopes(c, pipeline, finite, {input, output}, [&](const auto& payload) {
            const auto facts =
                OriginalWholeDescriptorWriteFixture::originalTarget(pipeline, 0u, 0u);
            std::uint64_t length = 0u;
            assert(!facts.runtimeArrayLength(input->layout().sizeBytes, length));
            const auto expected = length * (byteAddress ? facts.runtimeArrayStrideBytes : 1u);
            const std::uint32_t zero = 0u;
            const auto values = record(payload->group,
                facts.group,
                payload->dynamicOffsetCount,
                payload->dynamicOffsetCount ? &zero : nullptr);
            assert(expected <= UINT32_MAX && values[0] == expected);
            if (!byteAddress)
                assert(values[1] == facts.runtimeArrayStrideBytes);
            std::printf(
                "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_COMPUTE_VALUES %s extent%llu value%u expected%llu stride%llu exact-committed-group zero-dynamics real-completion-map\n",
                byteAddress ? "byteaddress" : "structured",
                static_cast<unsigned long long>(input->layout().sizeBytes),
                values[0],
                static_cast<unsigned long long>(expected),
                static_cast<unsigned long long>(facts.runtimeArrayStrideBytes));
        });
        const std::array<std::uint64_t, 5> sizes{16u, 32u, 36u, 48u, 60u};
        for (std::size_t i = 0u; i < sizes.size(); ++i) {
            const auto dynamic = i % 2u ? alignment : 0u;
            const rhi::DescriptorBufferBindingRange range{2u, alignment, sizes[i]};
            Contract::IntervalDiagnosticObservation observation;
            assert(!c.device->diagnoseOriginalRuntimeLengthInterval(
                {1u, true}, pipeline, input, 0u, 0u, range, dynamic, observation));
            assert(observation.group == 0u && observation.binding == 0u
                && observation.runtimeLength <= UINT32_MAX);

            std::array<WGPUBindGroupEntry, 2> entries{
                WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
            std::uint32_t loweredGroup = 99u, loweredDynamic = 99u;
            assert(!Lowering::lower({1u, true},
                *c.device,
                pipeline,
                input,
                0u,
                0u,
                range,
                dynamic,
                entries[0],
                loweredGroup,
                loweredDynamic));
            assert(loweredGroup == observation.group && loweredDynamic == dynamic
                && entries[0].offset == range.offsetBytes && entries[0].size == range.sizeBytes);
            std::uint32_t outputGroup = 99u, outputDynamic = 99u;
            assert(!Lowering::lowerBindingDiagnostic({1u, true},
                *c.device,
                pipeline,
                output,
                0u,
                1u,
                {2u, 0u, outputBytes},
                0u,
                entries[1],
                outputGroup,
                outputDynamic));
            assert(outputGroup == loweredGroup && !outputDynamic && entries[1].binding == 1u
                && entries[1].size == outputBytes);
            std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u> authored;
            authored[0].buffer = input;
            authored[0].dynamicOffset = dynamic;
            authored[1].buffer = output;
            for (std::size_t j = 0u; j < authored.size(); ++j) {
                authored[j].write.binding = static_cast<std::uint32_t>(j);
                authored[j].write.type = c.layout->desc().descriptorLayouts[0].bindings[j].type;
                authored[j].write.resource = authored[j].buffer->originalObject()->id();
                authored[j].write.resourceKind = rhi::DescriptorResourceKind::Buffer;
                authored[j].write.bufferBindingRange =
                    j ? rhi::DescriptorBufferBindingRange{2u, 0u, outputBytes} : range;
            }
            if (!i) {
                finite.budgets(c, pipeline, authored);
                finite.nativeFailure(c, pipeline, authored);
            }
            auto committed = finite.commit(c, pipeline, authored, !i);
            auto group = committed->group;
            if (i + 1u == sizes.size()) {
                finite.sourceCustody(c, pipeline, authored, true);
                auto adopted = rhi_webgpu::webGpuNativePayloadFor(
                    *finite.retainedFiniteSource->originalObject());
                assert(adopted);
                std::uint32_t adoptedGroup = 99u, adoptedCount = 99u;
                const std::uint32_t* adoptedOffsets = nullptr;
                assert(!adopted->diagnoseOriginalFiniteGroup(
                           adoptedGroup, adoptedOffsets, adoptedCount)
                    && adoptedGroup == loweredGroup && adoptedCount == 1u
                    && adoptedOffsets[0] == loweredDynamic);
                group = adopted->group;
            }
            const auto values = record(group, loweredGroup, 1u, &loweredDynamic);
            const auto expected =
                observation.runtimeLength * (byteAddress ? observation.runtimeStrideBytes : 1u);
            assert(expected <= UINT32_MAX && values[0] == expected);
            if (!byteAddress)
                assert(values[1] == observation.runtimeStrideBytes);
            if (i + 1u == sizes.size())
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_CUSTODY_COMPUTE_OUTPUT_OK exact-source3-adopted-group runtime-length-and-stride");
            std::printf(
                "BINDLESS_RHI_ORIGINAL_RUNTIME_INTERVAL_COMPUTE_VALUES %s case%zu base%llu dynamic%u size%llu value%u expected%llu\n",
                byteAddress ? "byteaddress" : "structured",
                i,
                static_cast<unsigned long long>(entries[0].offset),
                loweredDynamic,
                static_cast<unsigned long long>(entries[0].size),
                values[0],
                static_cast<unsigned long long>(expected));
            if (i + 1u == sizes.size()) {
                // Numeric work completed; release its adopted set before
                // the independent caller-drop probe uses the second slot.
                committed.reset();
                finite.recreateTarget(c, pipeline, authored);
                finite.negatives(c, pipeline, authored);
                finite.sourceCustody(c, pipeline, authored);
                finite.ordinaryPositional(c, pipeline, authored);
                finite.recreateTarget(c, pipeline, authored, false);
                finite.sourceCustody(c, pipeline, authored, true, nullptr, true);
            }
        }
        wgpuQueueRelease(queue);
        finite.release();
        std::puts(
            "BINDLESS_RHI_ORIGINAL_FINITE_WRITE_COMPUTE_OK committed-group cases5 bounded-native-scopes hot-finish0 source12-closed");
    }

    template <bool Graphics, class Factory, class Wait, class Map>
    static void publication(Factory&& create,
        Wait&& wait,
        Map&& map,
        const char* path,
        const char* folder,
        bool byteAddress = false,
        bool retainLoss = false,
        bool graphicsProbe = false)
    {
        OriginalRuntimeLengthFixture authored;
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        const auto budget = [&] {
            if constexpr (Graphics)
                return authored.moduleBudget();
            else
                return authored.entryBudget();
        }();
        using Artifact = std::conditional_t<Graphics,
            shader::GraphicsShaderModuleArtifact,
            shader::SingleEntryShaderArtifact>;
        auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        auto artifact = Artifact::load(path, budget, artifactQuota, diagnostic);
        assert(artifact);
        using Input = OriginalRuntimeLengthPipelineInputFixture::Input<Artifact>;
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto input = Input::prepare(artifact, {2u, cpuBytes, 1u, true}, inputQuota, diagnostic);
        assert(input);
        auto layout = rhi::buildPipelineLayoutDescFromShaderReflection(input->package().reflection);
        layout.descriptorLayouts[0].bindings[0].dynamicOffset = true;
        auto c = [&] {
            if constexpr (Graphics) {
                auto state = WebGpuOrdinaryPipelineFixture::graphicsState();
                state.vertexShader = input->package().entrypoints[0];
                state.fragmentShader = input->package().entrypoints[1];
                state.cullMode = rhi::CullMode::None;
                state.frontFace = rhi::FrontFace::CounterClockwise;
                state.topology = rhi::PrimitiveTopology::TriangleList;
                state.multisample.sampleCount = 1u;
                return OriginalAcceptedGraphicsPipelineFactoryFixture::prepareContext(create,
                    input,
                    layout,
                    state,
                    graphicsProbe ? 5u : 4u,
                    graphicsProbe,
                    graphicsProbe ? 2u : 0u,
                    7u);
            } else
                return OriginalAcceptedComputePipelineFactoryFixture::prepareContext(
                    create, input, layout, 4u, 2u, 7u);
        }();
        EM_ASM(
            {
                // clang-format off
                    const p = UTF8ToString($0);
                    FS.rename(p, p + ".original-interval-hidden");
                // clang-format on
            },
            folder);
        artifact.reset();
        c->ready();
        const char* id = nullptr;
        auto pipeline = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(c->publish(), id);
        assert(pipeline && !id);
        const auto limits = rhi_webgpu::webGpuNativeDeviceLimits();
        const auto alignment = limits.storageBufferOffsetAlignment;
        assert(alignment > 1u && alignment <= (maxInputBytes - 64u) / 3u);
        const auto extent = std::uint64_t(alignment) * 3u + 64u;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto sourceUsage = rhi::ResourceUsage::StorageBuffer;
        // The graphics probe explicitly shares its read-only source with the
        // indexed shader-consumption fixture. This does not issue native draws.
        if constexpr (Graphics)
            if (graphicsProbe)
                sourceUsage = sourceUsage | rhi::ResourceUsage::IndexBuffer;
        auto source = buffer(*c, extent, sourceUsage, rhi::MemoryUsage::GpuOnly, quotas[0]);
        auto destination = buffer(*c,
            outputBytes,
            rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc,
            rhi::MemoryUsage::GpuOnly,
            quotas[0]);
        auto download = buffer(
            *c, outputBytes, rhi::ResourceUsage::CopyDst, rhi::MemoryUsage::GpuToCpu, quotas[2]);
        auto wrong = buffer(
            *c, extent, rhi::ResourceUsage::UniformBuffer, rhi::MemoryUsage::GpuOnly, quotas[3]);
        verifyJoin(*c, pipeline, source, wrong, quotas[0], alignment, create);
        const auto artifactLimit = artifactQuota->limitBytes();
        artifactQuota->setLimitBytes(artifactLimit - 1u);
        Contract::IntervalDiagnosticObservation quotaFailure;
        quotaFailure.runtimeLength = 99u;
        assert(c->device->diagnoseOriginalRuntimeLengthInterval(
                   {1u, true}, pipeline, source, 0u, 0u, {2u, alignment, 16u}, 0u, quotaFailure)
            && !quotaFailure.runtimeLength);
        artifactQuota->setLimitBytes(artifactLimit);
        inputQuota->setLimitBytes(cpuBytes - 1u);
        assert(c->device->diagnoseOriginalRuntimeLengthInterval(
            {1u, true}, pipeline, source, 0u, 0u, {2u, alignment, 16u}, 0u, quotaFailure));
        inputQuota->setLimitBytes(cpuBytes);
        if constexpr (Graphics) {
            if (graphicsProbe)
                graphics(*c, pipeline, source, alignment, wait, map);
        }
        if constexpr (!Graphics) {
            compute(*c, pipeline, source, destination, download, alignment, byteAddress, wait, map);
            std::printf(
                "BINDLESS_RHI_ORIGINAL_RUNTIME_INTERVAL_NATIVE_COMPUTE_OK %s cases5 original-pipeline-buffers native-base-plus-dynamic unchanged-size exact-length clean-scopes canonical-closed\n",
                path);
            if (retainLoss) {
                const auto prepareTerminal = [&](bool dependencyLossCandidate = false) {
                    auto terminal = core::makeUnique<LossCase>();
                    terminal->context =
                        OriginalAcceptedComputePipelineFactoryFixture::prepareContext(
                            create, input, layout, 2u, 2u, dependencyLossCandidate ? 7u : 5u);
                    terminal->context->ready();
                    terminal->pipeline = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(
                        terminal->context->publish(), id);
                    assert(terminal->pipeline);
                    terminal->quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
                    terminal->outputQuota = terminal->quota;
                    terminal->inputQuota = inputQuota;
                    terminal->artifactQuota = artifactQuota;
                    terminal->buffer = buffer(*terminal->context,
                        extent,
                        rhi::ResourceUsage::StorageBuffer,
                        rhi::MemoryUsage::GpuOnly,
                        terminal->quota);
                    terminal->output = buffer(*terminal->context,
                        outputBytes,
                        rhi::ResourceUsage::StorageBuffer,
                        rhi::MemoryUsage::GpuOnly,
                        terminal->outputQuota);
                    terminal->finite =
                        core::makeUnique<OriginalFiniteDescriptorWriteFixture>(*terminal->context);
                    for (std::size_t i = 0u; i < terminal->writes.size(); ++i) {
                        auto& item = terminal->writes[i];
                        item.buffer = i ? terminal->output : terminal->buffer;
                        item.write.binding = static_cast<std::uint32_t>(i);
                        item.write.type = layout.descriptorLayouts[0].bindings[i].type;
                        item.write.resource = item.buffer->originalObject()->id();
                        item.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
                        item.write.bufferBindingRange = {
                            2u, i ? 0u : alignment, i ? outputBytes : 16u};
                    }
                    assert(
                        c->device->beginOriginalFiniteDescriptorWrite(*terminal->finite->descriptor,
                            pipeline,
                            0u,
                            terminal->writes.data(),
                            terminal->writes.size(),
                            terminal->finite->request,
                            terminal->finite->quotas));
                    assert(terminal->finite->begin(*terminal->context, pipeline, terminal->writes));
                    terminal->finite->commit(
                        *terminal->context, terminal->pipeline, terminal->writes);
                    terminal->finite->sourceCustody(*terminal->context,
                        terminal->pipeline,
                        terminal->writes,
                        true,
                        nullptr,
                        false,
                        dependencyLossCandidate);
                    assert(!terminal->finite->begin(
                        *terminal->context, terminal->pipeline, terminal->writes));
                    return terminal;
                };
                for (const bool destroy : std::array{false, true}) {
                    auto terminal = prepareTerminal();
                    const auto revision = terminal->finite->descriptor->contentRevision();
                    auto group = rhi_webgpu::webGpuNativePayloadFor(*terminal->finite->descriptor);
                    OriginalFiniteDescriptorWriteFixture::drain(*terminal->finite->descriptor);
                    terminal->pipeline.reset();
                    terminal->buffer.reset();
                    terminal->output.reset();
                    terminal->writes = {};
                    assert(!rhi::OriginalCommandDescriptorSetSource::validateOriginal(
                        terminal->finite->retainedFiniteSource));
                    assert(!terminal->finite->retainedFiniteInputs->validate());
                    OriginalCommandDescriptorConsumptionFixture::verifyRetained(
                        terminal->finite->retainedFiniteInputs, *terminal->context->device, true);
                    OriginalCommandDescriptorStateFixture::verifyRetained(
                        terminal->finite->retainedTypedState, true);
                    // The fixture retains the genuine unique-created mutable
                    // buffer under a const lease; public sealing is monotonic.
                    const_cast<rhi::GpuBuffer*>(
                        terminal->finite->retainedFiniteSource->frozenFiniteDeclarations()[1]
                            .buffer->originalObject())
                        ->sealWrites();
                    assert(terminal->finite->retainedFiniteInputs->validate());
                    OriginalCommandDescriptorConsumptionFixture::verifyRetained(
                        terminal->finite->retainedFiniteInputs, *terminal->context->device, false);
                    OriginalCommandDescriptorStateFixture::verifyRetained(
                        terminal->finite->retainedTypedState, false);
                    assert(
                        core::StringView(terminal->finite->descriptor->finishOriginalFiniteWrite())
                            == "RHI-BUFFER-GPU-WRITES-SEALED"
                        && terminal->finite->descriptor->contentRevision() == revision);
                    if (destroy)
                        terminal->context->device.reset();
                    else {
                        terminal->context->device->retireOriginalDeviceLifetime();
                        assert(rhi::OriginalCommandDescriptorSetSource::validateOriginal(
                            terminal->finite->retainedFiniteSource));
                    }
                    assert(terminal->finite->descriptor->finishOriginalFiniteWrite()
                        && terminal->finite->descriptor->contentRevision() == revision);
                    assert(rhi::OriginalCommandDescriptorSetSource::validateOriginal(
                        terminal->finite->retainedFiniteSource));
                    OriginalCommandDescriptorStateFixture::verifyRetained(
                        terminal->finite->retainedTypedState, false);
                    std::uint32_t g = 99u, count = 99u;
                    const std::uint32_t* offsets = nullptr;
                    assert(group->diagnoseOriginalFiniteGroup(g, offsets, count) && !g && !count
                        && !offsets);
                    assert(!terminal->finite->descriptor->cancelOriginalFiniteWrite());
                    group.reset();
                    terminal->finite->release();
                    terminal->finite.reset();
                    terminal->writes = {};
                    terminal->pipeline.reset();
                    terminal->buffer.reset();
                    terminal->output.reset();
                    terminal->context->retire();
                    assert(!terminal->quota->liveBytes() && !terminal->outputQuota->liveBytes());
                }
                std::puts(
                    "BINDLESS_RHI_TYPED_STREAM_TERMINAL_OK retained-stream2 caller-drop write-seal retirement device-destruction rejected quotas-reclaimed");
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_FINITE_WRITE_TERMINAL_OK gpu-write-seal foreign-registry retirement-device-destruction committed-and-pending-closed no-stale-device-dereference quotas-reclaimed");
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_DIAGNOSTIC_TERMINAL_OK original-registry-retirement pending-replacement-no-authority");
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_CUSTODY_TERMINAL_OK retained-source3 write-seal retirement device-destruction no-stale-device-dereference");
                std::puts(
                    "BINDLESS_RHI_DESCRIPTOR_STATE_TERMINAL_OK retained-state2-plan caller-drop write-seal retirement device-destruction prepared-journal-empty quotas-reclaimed");
                loss_ = prepareTerminal(true);
                Contract::IntervalDiagnosticObservation observed;
                assert(!loss_->context->device->diagnoseOriginalRuntimeLengthInterval({1u, true},
                    loss_->pipeline,
                    loss_->buffer,
                    0u,
                    0u,
                    {2u, alignment, 16u},
                    0u,
                    observed));
                assert(
                    !loss_->finite->retainedTypedState.transaction->applyOriginalPrototypePlan());
            }
        }
        input.reset();
        Contract::IntervalDiagnosticObservation observed;
        assert(!c->device->diagnoseOriginalRuntimeLengthInterval(
            {1u, true}, pipeline, source, 0u, 0u, {2u, alignment, 16u}, alignment, observed));
        c->device->retireOriginalDeviceLifetime();
        assert(c->device->diagnoseOriginalRuntimeLengthInterval(
                   {1u, true}, pipeline, source, 0u, 0u, {2u, alignment, 16u}, 0u, observed)
            && !observed.range.sizeBytes);
        auto retiredEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        retiredEntry.offset = 99u;
        retiredEntry.size = 99u;
        std::uint32_t retiredGroup = 99u, retiredDynamic = 99u;
        assert(Lowering::lower({1u, true},
                   *c->device,
                   pipeline,
                   source,
                   0u,
                   0u,
                   {2u, alignment, 16u},
                   0u,
                   retiredEntry,
                   retiredGroup,
                   retiredDynamic)
            && !retiredEntry.buffer && !retiredEntry.offset && !retiredEntry.size && !retiredGroup
            && !retiredDynamic);
        pipeline.reset();
        source.reset();
        destination.reset();
        download.reset();
        wrong.reset();
        c->retire();
        c.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        if (!retainLoss)
            assert(!inputQuota->liveBytes() && !artifactQuota->liveBytes());
        EM_ASM(
            {
                // clang-format off
                    const p = UTF8ToString($0);
                    FS.rename(p + ".original-interval-hidden", p);
                // clang-format on
            },
            folder);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_RUNTIME_INTERVAL_JOIN_OK %s exact-registry-owner-buffer-operation-limits hot0 queries0 forged-foreign-retired-reject files-absent execution-closed\n",
            Graphics ? "graphics" : path);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_RUNTIME_INTERVAL_LOWERING_OK %s original-entry static-base unchanged-size separate-dynamic native-device-extent-check cleared-failures hot0 queries0 execution-closed\n",
            path);
    }
#endif
public:
    template <class Factory, class Wait, class Map>
    static void verify(Factory&& create, Wait&& wait, Map&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        for (const auto* variant : {"structured", "rwstructured", "byteaddress", "rwbyteaddress"}) {
            core::pmr::String folder(
                "/runtime-length-fixture/original/", &core::defaultCpuMemoryResource());
            folder += variant;
            core::pmr::String path(folder, &core::defaultCpuMemoryResource());
            path += "/runtime_length_";
            path += variant;
            path += ".computeMain.entry.yaml";
            publication<false>(create,
                wait,
                map,
                path.c_str(),
                folder.c_str(),
                std::strstr(variant, "byteaddress") != nullptr,
                std::strcmp(variant, "rwbyteaddress") == 0);
        }
        publication<true>(create,
            wait,
            map,
            "/runtime-length-fixture/original/graphics/runtime_length_graphics.graphics.module.yaml",
            "/runtime-length-fixture/original/graphics");
        publication<true>(create,
            wait,
            map,
            "/runtime-length-fixture/original/graphics_probe/runtime_length_graphics_probe.graphics.module.yaml",
            "/runtime-length-fixture/original/graphics_probe",
            false,
            false,
            true);
#else
        (void)create;
        (void)wait;
        (void)map;
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        assert(loss_ && rhi_webgpu::webGpuNativeDeviceLost());
        Contract::IntervalDiagnosticObservation observed;
        observed.range = {99u, 99u};
        observed.runtimeLength = 99u;
        assert(loss_->context->device->diagnoseOriginalRuntimeLengthInterval(
                   {1u, true}, loss_->pipeline, loss_->buffer, 0u, 0u, {2u, 0u, 16u}, 0u, observed)
            && !observed.range.sizeBytes && !observed.runtimeLength);
        auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.offset = 99u;
        entry.size = 99u;
        std::uint32_t group = 99u, displacement = 99u;
        assert(Lowering::lower({1u, true},
                   *loss_->context->device,
                   loss_->pipeline,
                   loss_->buffer,
                   0u,
                   0u,
                   {2u, 0u, 16u},
                   0u,
                   entry,
                   group,
                   displacement)
            && !entry.buffer && !entry.offset && !entry.size && !group && !displacement);
        const auto revision = loss_->finite->descriptor->contentRevision();
        assert(loss_->finite->diagnoseSource(*loss_->context, loss_->pipeline, loss_->writes));
        assert(rhi::OriginalCommandDescriptorSetSource::validateOriginal(
            loss_->finite->retainedFiniteSource));
        assert(loss_->finite->retainedFiniteInputs->validate());
        OriginalCommandDescriptorConsumptionFixture::verifyRetained(
            loss_->finite->retainedFiniteInputs, *loss_->context->device, false);
        OriginalCommandDescriptorStateFixture::verifyRetained(
            loss_->finite->retainedTypedState, false, true);
        OriginalTypedUsageProjectionFixture::verifyUnavailable(
            loss_->finite->retainedTypedState, *loss_->context->device, "finite-physical-loss");
        assert(loss_->finite->retainedTypedState.transaction->commitOriginalPendingOwnership());
        assert(!loss_->finite->retainedTypedState.transaction->abandonOriginalPendingOwnership());
        assert(loss_->finite->retainedTypedState.transaction->journalEmpty()
            && loss_->finite->retainedTypedState.transaction->validate());
        assert(loss_->finite->descriptor->finishOriginalFiniteWrite()
            && loss_->finite->descriptor->contentRevision() == revision);
        auto finiteGroup = rhi_webgpu::webGpuNativePayloadFor(*loss_->finite->descriptor);
        std::uint32_t finiteGroupNumber = 99u, count = 99u;
        const std::uint32_t* offsets = nullptr;
        assert(finiteGroup->diagnoseOriginalFiniteGroup(finiteGroupNumber, offsets, count)
            && !finiteGroupNumber && !count && !offsets);
        OriginalFiniteDescriptorWriteFixture::drain(*loss_->finite->descriptor);
        assert(!loss_->finite->descriptor->cancelOriginalFiniteWrite());
        finiteGroup.reset();
        loss_->finite->release();
        loss_->finite.reset();
        loss_->writes = {};
        loss_->buffer.reset();
        loss_->output.reset();
        loss_->pipeline.reset();
        loss_->context->retire();
        loss_->context.reset();
        assert(!loss_->quota->liveBytes() && !loss_->outputQuota->liveBytes()
            && !loss_->inputQuota->liveBytes() && !loss_->artifactQuota->liveBytes());
        loss_.reset();
        std::puts(
            "BINDLESS_RHI_TYPED_STREAM_LOSS_OK retained-stream2 actual-physical-loss pending-plan cancel journal-empty rejected quotas-reclaimed native-closed");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_FINITE_WRITE_LOSS_OK actual-device-loss pending-and-committed-closed old-revision quotas-reclaimed");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_DIAGNOSTIC_LOSS_OK actual-physical-loss rejected quotas-reclaimed");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_CUSTODY_LOSS_OK retained-source3 actual-physical-loss rejected canonical-and-private-quotas-reclaimed");
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_POSITIONAL_LOSS_OK retained-inputs3 actual-physical-loss rejected graph-source-private-quotas-reclaimed");
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_CONSUMPTION_LOSS_OK original-consumption-inputs caller-drop-write-seal actual-physical-loss emitted0 quotas-reclaimed");
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_STATE_LOSS_OK retained-state2-plan actual-physical-loss pending-commit-rejected CPU-prototype-cancel journal-empty quotas-reclaimed native-closed");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_RUNTIME_INTERVAL_LOSS_OK actual-physical-loss closed-observation existing-owners-reclaimed no-command-authority");
#endif
    }
};
} // namespace arp::test
