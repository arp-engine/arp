#pragma once

#include "original_accepted_graphics_pipeline_factory_fixture.h"
#include "original_command_descriptor_state_fixture.h"
#include "original_runtime_length_fixture.h"
#include "original_whole_descriptor_write_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/semantics/command_lowering.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
#include "original_finite_descriptor_write_fixture.h"
#include "webgpu_ordinary_pipeline_fixture.h"
#endif
#include "original_typed_dependency_decision_fixture.h"
#include "original_typed_native_graphics_execution_fixture.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_shader_usage_validation.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
#include "rhi/backends/webgpu/webgpu_attachment_operation_lowering.h"
#endif
#include "rhi/frontend/bindless_snapshot.h"

namespace arp::test {
// Shared original graphics actor. The default remains the historical raw probe;
// the independent execution specialization consumes the real typed issuers.
// Verification readback stays outside the original root and grants no proof.
class OriginalTypedAttachmentDependencyProbeFixture final {
    using Pass = rhi::RenderPassArguments;
    static constexpr std::uint64_t metadataBytes = 65536u;

    static rhi::RenderPassDesc passFields(rhi::GpuResourceId view, rhi::LoadOp load)
    {
        rhi::RenderPassDesc desc;
        rhi::RenderPassColorAttachmentDesc color;
        color.target = view;
        color.format = rhi::TextureFormat::Rgba8Unorm;
        color.loadOp = load;
        color.storeOp = rhi::StoreOp::Store;
        color.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
        desc.colorAttachments.push_back(color);
        return desc;
    }

public:
    static void verifyContract()
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* id = nullptr;
        auto clearFields = passFields({1u}, rhi::LoadOp::Clear),
             loadFields = passFields({1u}, rhi::LoadOp::Load);
        auto clear = Pass::prepareOriginal(
            clearFields, {1u, true, 1u, 0u, 0u, 0u, 0u, metadataBytes}, quota, id);
        auto load = Pass::prepareOriginal(
            loadFields, {1u, true, 1u, 0u, 0u, 0u, 0u, metadataBytes}, quota, id);
        assert(clear && load && !core::SharedOwnerProjection::sameOwner(clear, load));
        assert(clear->matches(clearFields) && load->matches(loadFields)
            && !load->matches(clearFields));
        auto wrong = loadFields;
        wrong.colorAttachments[0].storeOp = rhi::StoreOp::DontCare;
        assert(!load->matches(wrong));
        wrong = loadFields;
        wrong.colorAttachments[0].target = {2u};
        assert(!load->matches(wrong));
        wrong = loadFields;
        wrong.colorAttachments[0].format = rhi::TextureFormat::Rgba16Float;
        assert(!load->matches(wrong));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                rhi::ResourceMemoryBarrierDesc memory;
                memory.resource = {2u};
                memory.sourceStages = memory.destinationStages =
                    rhi::PipelineStage::ColorAttachmentOutput;
                memory.sourceAccess = memory.destinationAccess =
                    rhi::ResourceAccess::ColorAttachmentRead
                    | rhi::ResourceAccess::ColorAttachmentWrite;
                const auto* refusal =
                    rhi::semantics::TypedShaderUsageScopeContract::rejectUnfrozenMemory(
                        memory, rhi::ResourceState::ColorAttachment);
                assert(refusal
                    && core::StringView(refusal) == "RHI-TYPED-USAGE-SCOPE-DEPENDENCY-NOT-FROZEN");
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto gate =
                        arp::rhi::semantics::OriginalTypedUsageProjectionLowering::nativeLowering(
                            backend);
                    assert(gate.diagnostic && !gate.executionAvailable);
                }
                assert(clear->matches(clearFields) && load->matches(loadFields));
                return nullptr;
            }));
        clear.reset();
        load.reset();
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_ATTACHMENT_DEPENDENCY_PROBE_CONTRACT_OK original-distinct-clear-load-store owners load-store-target-format-refuse metadata-hot0 attachment-memory-unfrozen four-native-gates-closed");
    }

    template <bool execute = false, bool finite = false, class Factory, class Wait, class Map>
    static void verify(Factory&& create, Wait&& wait, Map&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        using Sources = rhi::OriginalCommandResourceSources;
        using Source = rhi::OriginalCommandDescriptorSetSource;
        using Inputs = rhi::OriginalCommandInputs;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Projection = rhi::common::OriginalTypedUsageProjection;
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        constexpr std::uint32_t ownerCeiling = 128u, bufferBytes = 48u, width = 2u, height = 2u;
        OriginalRuntimeLengthFixture authored;
        const auto budget = authored.moduleBudget();
        core::pmr::String diagnostic;
        auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        auto artifact = shader::GraphicsShaderModuleArtifact::load(
            "/runtime-length-fixture/original/graphics_probe/runtime_length_graphics_probe.graphics.module.yaml",
            budget,
            artifactQuota,
            diagnostic);
        assert(artifact);
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto input = rhi::OriginalGraphicsPipelineInput::prepare(
            artifact, {2u, metadataBytes, 1u, true}, inputQuota, diagnostic);
        assert(input);
        auto layout = rhi::buildPipelineLayoutDescFromShaderReflection(input->package().reflection);
        assert(layout.descriptorLayouts.size() == 1u
            && layout.descriptorLayouts[0].bindings.size() == 2u);
        auto definition = WebGpuOrdinaryPipelineFixture::graphicsState();
        definition.vertexShader = input->package().entrypoints[0];
        definition.fragmentShader = input->package().entrypoints[1];
        definition.cullMode = rhi::CullMode::None;
        definition.frontFace = rhi::FrontFace::CounterClockwise;
        definition.topology = rhi::PrimitiveTopology::TriangleList;
        definition.multisample.sampleCount = 1u;
        const rhi::OriginalDeviceNativeLossObserverRequest observed{
            1u, true, 3u, 4u, metadataBytes};
        auto context = OriginalAcceptedGraphicsPipelineFactoryFixture::prepareContext(
            create, input, layout, definition, 2u, true, 1u, 7u, execute ? &observed : nullptr);
        context->ready();
        const char* id = nullptr;
        auto pipeline =
            rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(context->publish(), id);
        assert(pipeline);
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes),
             graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> writeQuotas;
        {
            rhi::BufferDesc storage;
            storage.sizeBytes = bufferBytes;
            storage.usage = rhi::ResourceUsage::StorageBuffer;
            storage.memory = rhi::MemoryUsage::GpuOnly;
            storage.initialState = rhi::ResourceState::Undefined;
            core::UniquePtr<rhi::GpuBuffer> object;
            {
                core::CpuMemoryQuotaScope scope(context->quotas[2]);
                object = context->device->createBuffer(storage);
            }
            assert(object);
            auto buffer = rhi::OriginalCommandBufferLease::prepareOriginal(
                object, {1u, true, 2u, metadataBytes}, sourceQuota, id);
            assert(buffer);
            rhi::TextureDesc textureFields;
            textureFields.extent = {width, height, 1u};
            textureFields.format = rhi::TextureFormat::Rgba8Unorm;
            textureFields.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
            textureFields.memory = rhi::MemoryUsage::GpuOnly;
            textureFields.initialState = rhi::ResourceState::Undefined;
            textureFields.mipLevelCount = 1u;
            textureFields.arrayLayerCount = 1u;
            textureFields.sampleCount = 1u;
            core::UniquePtr<rhi::GpuTexture> textureObject;
            core::UniquePtr<rhi::GpuTextureView> viewObject;
            {
                core::CpuMemoryQuotaScope scope(context->quotas[2]);
                textureObject = context->device->createTexture(textureFields);
                assert(textureObject);
                rhi::TextureViewDesc fields;
                fields.format = textureFields.format;
                fields.usage = textureFields.usage;
                fields.aspect = rhi::TextureAspect::Color;
                viewObject = context->device->createTextureView(*textureObject, fields);
                assert(viewObject);
            }
            auto texture = rhi::OriginalCommandTextureLease::prepareOriginal(
                textureObject, {1u, true, 2u, metadataBytes}, sourceQuota, id);
            assert(texture);
            auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                viewObject, {1u, true, 2u, metadataBytes}, sourceQuota, id);
            assert(viewLease);
            auto view = rhi::OriginalCommandTextureViewSource::prepareOriginal(
                viewLease, texture, {1u, true, 5u, metadataBytes}, sourceQuota, id);
            assert(view);
            auto pipelineSource =
                rhi::OriginalCommandGraphicsPipelineSource::prepareOriginal(pipeline,
                    {1u,
                        true,
                        rhi::OriginalCommandGraphicsPipelineSource::kind,
                        pipeline->originalResourceRegistration().generation(),
                        metadataBytes,
                        rhi::OriginalCommandGraphicsPipelineSource::exclusiveOwnerCount},
                    sourceQuota,
                    *context->device,
                    id);
            assert(pipelineSource);
            // Original source order is Buffer, Texture, View, Pipeline, Set.
            const std::array<Sources::Source, 3u> prefix{buffer, texture, view};
            rhi::OriginalCommandAdmissionBudget admission;
            admission.contractVersion = 1u;
            admission.prototypeOptIn = true;
            for (const auto domain : {rhi::CommandReferenceDomain::Buffer,
                     rhi::CommandReferenceDomain::Texture,
                     rhi::CommandReferenceDomain::TextureView,
                     rhi::CommandReferenceDomain::Pipeline,
                     rhi::CommandReferenceDomain::Descriptor})
                admission.maxSourceOwners[static_cast<std::size_t>(domain)] = 1u;
            constexpr std::uint32_t baseCommands = 11u, addedDependencies = 5u,
                                    originalDependencies = 1u;
            admission.maxCommands = baseCommands + addedDependencies;
            admission.maxReferences = 5u + addedDependencies;
            admission.maxInputDescriptors = 5u + addedDependencies;
            admission.maxGraphOwners = ownerCeiling;
            admission.maxCpuBytes.fill(metadataBytes);
            admission.maxPassColorAttachments = 2u;
            admission.maxPassAttachmentReferences = 2u;
            admission.maxStateObservations = 6u;
            admission.maxTextureRanges = 5u;
            admission.maxStateTransactions = 1u;
            admission.maxNativePayloadObjects = 5u;
            admission.maxTypedNativeCommandCalls = 1u + baseCommands - originalDependencies;
            if constexpr (execute)
                OriginalTypedNativeGraphicsExecutionFixture::authorizeSource(admission);
            auto root = Sources::prepareColdOriginalTyped(prefix.data(),
                prefix.size(),
                admission,
                {finite ? 3u : 2u, true, 2u},
                sourceQuota,
                id);
            assert(root);
            assert(!Sources::appendOriginalTyped(root, {pipelineSource}, *context->device));
            std::shared_ptr<rhi::DescriptorArena> arena;
            {
                core::CpuMemoryQuotaScope scope(context->quotas[2]);
                rhi::DescriptorArenaDesc fields;
                fields.maxSets = 1u;
                fields.maxDescriptors = 2u;
                arena = core::shareUnique(context->device->createOriginalDescriptorArena(fields));
            }
            assert(arena);
            core::UniquePtr<rhi::DescriptorSet> setObject;
            {
                core::CpuMemoryQuotaScope scope(context->quotas[2]);
                rhi::DescriptorSetDesc fields;
                fields.layout = rhi::DescriptorStateCopy::copy(layout.descriptorLayouts[0]);
                setObject = context->device->allocateOriginalDescriptorSet(arena, fields);
            }
            assert(setObject);
            std::array<Source::Element, 2u> elements;
            std::array<rhi::OriginalWholeDescriptorBufferWrite, 2u> writes;
            for (std::size_t i = 0u; i < elements.size(); ++i) {
                auto& e = elements[i];
                const auto& declared = layout.descriptorLayouts[0].bindings[i];
                e.sourceIndex = 0u;
                e.registrationGeneration =
                    buffer->originalObject()->originalResourceRegistration().generation();
                e.write.binding = declared.binding;
                e.write.type = declared.type;
                e.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
                e.write.resource = buffer->resource();
                e.write.resourceState = rhi::ResourceState::ShaderRead;
                e.stages = rhi::bindlessPipelineStagesForShader(declared.stages);
                e.access = rhi::ResourceAccess::ShaderRead;
                e.bufferAccessBytes = buffer->layout().sizeBytes;
                writes[i] = {e.write, buffer, 0u};
            }
            rhi::OriginalCommandDescriptorSourceBudget sourceBudget{
                2u, true, 2u, 2u, Source::canonicalExclusiveRetainedOwnerCount, metadataBytes};
            if constexpr (finite) {
                static_assert(execute, "Finite acceptance consumes actual original typed issuers");
                std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u> declarations;
                for (std::size_t i = 0u; i < elements.size(); ++i) {
                    auto& e = elements[i];
                    const auto target = OriginalWholeDescriptorWriteFixture::originalTarget(
                        pipeline, 0u, e.write.binding);
                    assert(target.minimumBindingBytes && target.minimumBindingBytes < bufferBytes);
                    e.write.bufferBindingRange = {2u, 0u, target.minimumBindingBytes};
                    rhi::DescriptorBufferBindingRangeContract::Resolved range;
                    assert(!rhi::DescriptorBufferBindingRangeContract::resolve(
                        e.write.bufferBindingRange, bufferBytes, 0u, range));
                    e.bufferAccessOffset = range.offsetBytes;
                    e.bufferAccessBytes = range.sizeBytes;
                    declarations[i] = {e.write, buffer, 0u};
                }
                id = OriginalFiniteDescriptorWriteFixture::commitActual(
                    *context, pipeline, *setObject, 0u, declarations, writeQuotas);
                if (id)
                    std::fprintf(stderr, "finite graphics write: %s\n", id);
                assert(!id);
                sourceBudget.contractVersion = 3u;
                sourceBudget.maxExclusiveRetainedOwners = Source::finiteExclusiveRetainedOwnerCount;
                sourceBudget.finitePipelineSourceIndex = 3u;
                sourceBudget.finiteFootprintContractVersion = 1u;
                sourceBudget.finiteFullBoundViewOptIn = true;
            } else
                OriginalWholeDescriptorWriteFixture::commitActual(
                    *context, pipeline, *setObject, 0u, writes, writeQuotas);
            auto set = Source::prepareOriginal(setObject,
                arena,
                root,
                elements.data(),
                elements.size(),
                sourceBudget,
                sourceQuota,
                *context->device,
                id);
            if (!set)
                std::fprintf(stderr, "attachment probe Source2: %s\n", id ? id : "unknown");
            assert(set);
            id = Sources::appendOriginalTyped(root, {set}, *context->device);
            if (id)
                std::fprintf(stderr, "attachment source assembly: %s\n", id);
            assert(!id);
            auto clearFields = passFields(view->resource(), rhi::LoadOp::Clear),
                 loadFields = passFields(view->resource(), rhi::LoadOp::Load);
            auto clear = Pass::prepareOriginal(
                clearFields, {1u, true, 1u, 0u, 0u, 0u, 0u, metadataBytes}, graphQuota, id);
            auto load = Pass::prepareOriginal(
                loadFields, {1u, true, 1u, 0u, 0u, 0u, 0u, metadataBytes}, graphQuota, id);
            assert(clear && load);
            const std::array<K, baseCommands> kinds{K::BeginRenderPass,
                K::BindGraphicsPipeline,
                K::BindRenderDescriptor,
                K::SetViewport,
                K::SetScissor,
                K::Draw,
                K::EndRenderPass,
                K::TextureMemoryBarrier,
                K::BeginRenderPass,
                K::EndRenderPass,
                K::Finish};
            core::pmr::Vector<rhi::CommandUsage> commands;
            core::pmr::Vector<rhi::OriginalCommandUsage::Reference> refs;
            core::pmr::Vector<rhi::CommandVariableArguments::Input> fields;
            core::pmr::Vector<Inputs::OriginalDescriptor> records;
            commands.reserve(baseCommands);
            refs.reserve(5u);
            fields.reserve(baseCommands);
            records.reserve(5u);
            rhi::ResourceMemoryBarrierDesc memory;
            memory.resource = texture->resource();
            memory.sourceStages = memory.destinationStages =
                rhi::PipelineStage::ColorAttachmentOutput;
            memory.sourceAccess = memory.destinationAccess =
                rhi::ResourceAccess::ColorAttachmentRead
                | rhi::ResourceAccess::ColorAttachmentWrite;
            const auto& stateDefinition = pipeline->originalGraphicsCreation()->definition();
            for (std::size_t i = 0u; i < kinds.size(); ++i) {
                const auto kind = kinds[i];
                commands.push_back({kind, static_cast<std::uint32_t>(refs.size()), 0u});
                if (kind == K::BeginRenderPass) {
                    refs.push_back({2u});
                    commands.back().referenceCount = 1u;
                    records.push_back({i, i == 0u ? clear : load});
                }
                if (kind == K::BindGraphicsPipeline) {
                    refs.push_back({3u});
                    commands.back().referenceCount = 1u;
                    records.push_back({i,
                        rhi::OriginalCommandPipelineBinding{
                            kind, 3u, 0u, pipeline->originalResourceRegistration().generation()}});
                }
                if (kind == K::BindRenderDescriptor) {
                    refs.push_back({4u});
                    commands.back().referenceCount = 1u;
                    records.push_back({i,
                        rhi::OriginalCommandDescriptorBinding{kind,
                            4u,
                            3u,
                            0u,
                            set->originalObject()->originalResourceRegistration().generation(),
                            set->frozenContentRevision()}});
                }
                if (kind == K::TextureMemoryBarrier) {
                    refs.push_back({1u});
                    commands.back().referenceCount = 1u;
                    records.push_back(
                        {i, *rhi::ResourceDependencyArguments::capture(kind, memory)});
                }
                if (kind == K::BindRenderDescriptor)
                    fields.push_back({*Scalar::capture(kind, Scalar::DescriptorBinding{0u}), {}});
                else if (kind == K::SetViewport)
                    fields.push_back({*Scalar::capture(kind, stateDefinition.viewport), {}});
                else if (kind == K::SetScissor)
                    fields.push_back({*Scalar::capture(kind, stateDefinition.scissor), {}});
                else if (kind == K::Draw)
                    fields.push_back({*Scalar::capture(kind, Scalar::Draw{3u, 1u, 0u, 0u}), {}});
                else
                    fields.push_back({*Scalar::capture(kind, Scalar::None{}), {}});
            }
            auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(root,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                fields.data(),
                records.data(),
                records.size(),
                {3u, true, 1u, 1u, 0u, 0u},
                graphQuota,
                id);
            assert(inputs);
            auto state = OriginalCommandDescriptorStateFixture::verify<rhi::RenderPipeline>(inputs,
                *context->device,
                true,
                false,
                false,
                {1u, true, 1u, 1u, true, true, 1u, 2u});
            const auto& stream = state.stream.program;
            assert(stream->counts().commands == 16u && stream->counts().renderPasses == 2u
                && stream->counts().consumptions == 1u && stream->counts().implicitAccesses == 2u
                && state.graph->textureEntries().size() == 5u);
            auto loss = context->device->observeOriginalDeviceNativeLoss(id);
            assert(loss && !id);
            const auto resourceBytes = buffer->originalObject()->allocation().estimatedSizeBytes
                + texture->originalObject()->allocation().estimatedSizeBytes;
            state.nativeClosure = Closure::prepareOriginalTypedLossBound(state.transaction,
                stream,
                *context->device,
                {2u, true, 3u, 5u, ownerCeiling, resourceBytes, metadataBytes, 1u, 1u},
                *loss,
                id);
            if (!state.nativeClosure)
                std::fprintf(stderr, "attachment probe closure: %s\n", id ? id : "unknown");
            assert(state.nativeClosure);
            const Projection::StateBoundRequest projection{1u,
                true,
                {1u,
                    true,
                    {1u, true, 1u, 1u, true, true, 1u, 2u},
                    {1u, true, 1u, 1u, true, true, 16u, 1u, 2u},
                    16u,
                    2u,
                    6u}};
            std::array<Projection::AttachmentPoint, 2u> points;
            std::size_t attachments = 0u;
            auto projected = Projection::visitStateBound(
                stream, state.graph, projection, [&](const auto& point) noexcept -> const char* {
                    if (const auto* target = std::get_if<Projection::AttachmentPoint>(&point)) {
                        assert(attachments < points.size());
                        points[attachments++] = *target;
                    }
                    return nullptr;
                });
            assert(
                !projected.diagnostic && projected.counts.contributions == 6u && attachments == 2u);
            assert(points[0].scope.first == 2u && points[0].scope.last == 8u
                && points[1].scope.first == 11u && points[1].scope.last == 12u);
            assert(points[0].resource == texture->resource()
                && points[1].resource == texture->resource()
                && points[0].original.range == points[1].original.range);
            const auto* refusal =
                rhi::semantics::TypedShaderUsageScopeContract::rejectUnfrozenMemory(
                    memory, rhi::ResourceState::ColorAttachment);
            assert(refusal
                && core::StringView(refusal) == "RHI-TYPED-USAGE-SCOPE-DEPENDENCY-NOT-FROZEN");
            const auto& actualInputs = stream->originalInputs();
            const auto& first = std::get<std::shared_ptr<const Pass>>(
                *actualInputs->descriptorAt(points[0].scope.first));
            const auto& second = std::get<std::shared_ptr<const Pass>>(
                *actualInputs->descriptorAt(points[1].scope.first));
            assert(core::SharedOwnerProjection::sameOwner(first, clear)
                && core::SharedOwnerProjection::sameOwner(second, load));
            const Inputs::IncomingReference reference{viewLease->originalObject()};
            const auto& arguments = *actualInputs->originalScalars()->originalArguments();
            const auto index = points[1].scope.first;
            const auto& record = arguments.records()[index];
            const rhi::CommandVariableArguments::Input incomingField{record.scalar,
                arguments.text(index),
                arguments.offsets(index),
                static_cast<std::uint32_t>(record.offsetCount),
                arguments.bytes(index),
                static_cast<std::uint32_t>(record.byteCount)};
            Inputs::Incoming incoming{incomingField, &loadFields, &reference, 1u};
            assert(!actualInputs->compareOriginalInput(points[1].scope.first, incoming));
            auto wrong = rhi::RenderPassStateCopy::copy(loadFields);
            incoming.descriptor = &wrong;
            for (const auto change : {0u, 1u, 2u}) {
                wrong = rhi::RenderPassStateCopy::copy(loadFields);
                if (change == 0u)
                    wrong.colorAttachments[0].loadOp = rhi::LoadOp::Clear;
                if (change == 1u)
                    wrong.colorAttachments[0].storeOp = rhi::StoreOp::DontCare;
                if (change == 2u)
                    wrong.colorAttachments[0].target = texture->resource();
                assert(actualInputs->compareOriginalInput(points[1].scope.first, incoming));
            }
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 10u> pressure{sourceQuota,
                graphQuota,
                state.stateQuota,
                state.recordingQuota,
                state.stream.quota,
                context->lossQuota,
                context->quotas[1],
                writeQuotas[0],
                writeQuotas[1],
                writeQuotas[2]};
            if constexpr (execute) {
                using Decision = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
                state.dependencyDecision = Decision::prepareOriginal(state.nativeClosure,
                    {1u,
                        true,
                        OriginalTypedDependencyDecisionFixture::requestFor(
                            state, projection.projection),
                        state.nativeClosure->retainedOwnerCount() + 1u,
                        state.recordingQuota->limitBytes()},
                    id);
                assert(state.dependencyDecision && !id);
            } else
                state.dependencyDecision = OriginalTypedDependencyDecisionFixture::verify(state,
                    *context->device,
                    state.nativeClosure,
                    pressure,
                    "dependent-attachment",
                    projection.projection);
            const auto& frozen = state.dependencyDecision->records();
            assert(frozen.size() == 6u);
            assert(frozen[2].original.commandIndex == 9u && frozen[2].original.texture
                && frozen[2].original.preceding->scope.first == 2u
                && frozen[2].original.following->scope.first == 11u
                && frozen[3].original.commandIndex == 10u && !frozen[3].original.following
                && frozen[3].original.clause
                    == rhi::common::OriginalTypedDependencyProjection::Clause::FinalBookkeeping
                && state.dependencyDecision->counts().initial == 2u
                && state.dependencyDecision->counts().between == 1u
                && state.dependencyDecision->counts().final == 3u);
            std::puts(
                "BINDLESS_RHI_TYPED_DEPENDENCY_DECISION_ATTACHMENT_POSITIONS_OK original16 attachment-memory9 render2-8-to11-12 buffer-memory10-final-no-following clauses2-1-3 candidate-only");
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    pressure, [&]() noexcept -> const char* {
                        const auto inspected =
                            Projection::inspectStateBound(stream, state.graph, projection);
                        assert(!inspected.diagnostic && inspected.counts.matches(projected.counts));
                        return state.nativeClosure->validate();
                    }));
            });
            if constexpr (execute)
                OriginalTypedNativeGraphicsExecutionFixture::execute<finite>(state, *context);
            const auto pitch =
                core::alignCpuAllocationOffsetUp(width * rhi::bytesPerPixel(textureFields.format),
                    rhi::kTextureUploadRowPitchAlignment);
            assert(pitch.success && pitch.alignedValue <= UINT32_MAX);
            rhi::BufferDesc downloadFields;
            downloadFields.sizeBytes = pitch.alignedValue * height;
            downloadFields.usage = rhi::ResourceUsage::CopyDst;
            downloadFields.memory = rhi::MemoryUsage::GpuToCpu;
            downloadFields.initialState = rhi::ResourceState::Undefined;
            core::UniquePtr<rhi::GpuBuffer> downloadObject;
            {
                core::CpuMemoryQuotaScope scope(context->quotas[2]);
                downloadObject = context->device->createBuffer(downloadFields);
            }
            assert(downloadObject);
            auto download = rhi::OriginalCommandBufferLease::prepareOriginal(
                downloadObject, {1u, true, 2u, metadataBytes}, sourceQuota, id);
            assert(download);
            auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
            auto target = rhi_webgpu::webGpuNativePayloadFor(*texture->originalObject());
            auto targetView = rhi_webgpu::webGpuNativePayloadFor(*viewLease->originalObject());
            auto readback = rhi_webgpu::webGpuNativePayloadFor(*download->originalObject());
            auto group = rhi_webgpu::webGpuNativePayloadFor(*set->originalObject());
            const auto device = static_cast<WGPUDevice>(state.nativeClosure->nativeDevice());
            assert(device && native && target && targetView && readback && group);
            assert(target->ownerDevice == device && targetView->parentTexture == target
                && readback->ownerDevice == device
                && native->issuedContract()->nativeDevice() == device);
            const auto shared =
                OriginalWholeDescriptorWriteFixture::originalTarget(pipeline, 0u, 0u);
            const auto fragment =
                OriginalWholeDescriptorWriteFixture::originalTarget(pipeline, 0u, 1u);
            std::uint64_t sharedCount = 0u, fragmentCount = 0u;
            if constexpr (finite) {
                const auto& declarations = set->frozenFiniteDeclarations();
                assert(declarations.size() == 2u);
                assert(!shared.runtimeArrayLength(
                           declarations[0].write.bufferBindingRange.sizeBytes, sharedCount)
                    && !fragment.runtimeArrayLength(
                        declarations[1].write.bufferBindingRange.sizeBytes, fragmentCount));
            } else
                assert(!shared.runtimeArrayLength(bufferBytes, sharedCount)
                    && !fragment.runtimeArrayLength(bufferBytes, fragmentCount));
            const std::array<std::uint64_t, 4u> expected{
                sharedCount, shared.runtimeArrayStrideBytes, sharedCount, fragmentCount};
            for (const auto value : expected)
                assert(value <= UINT8_MAX);
            auto queue = wgpuDeviceGetQueue(device);
            assert(queue);
            rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
            auto encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
            auto encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
            assert(encoder);
            const std::array<std::shared_ptr<const Pass>, 2u> passes{first, second};
            if constexpr (!execute) {
                for (std::size_t p = 0u; p < passes.size(); ++p) {
                    const auto& fields = passes[p]->desc();
                    assert(
                        fields.colorAttachments.size() == 1u && !fields.hasDepthStencilAttachment);
                    const auto& attachment = fields.colorAttachments[0];
                    assert(attachment.target == view->resource()
                        && attachment.format == textureFields.format);
                    auto color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                    color.view = targetView->view;
                    color.loadOp =
                        rhi_webgpu::WebGpuAttachmentOperationLowering::load(attachment.loadOp);
                    color.storeOp =
                        rhi_webgpu::WebGpuAttachmentOperationLowering::store(attachment.storeOp);
                    color.clearValue = {attachment.clearColor.r,
                        attachment.clearColor.g,
                        attachment.clearColor.b,
                        attachment.clearColor.a};
                    auto passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                    passDesc.colorAttachmentCount = 1u;
                    passDesc.colorAttachments = &color;
                    auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                    assert(pass);
                    if (p == 0u) {
                        wgpuRenderPassEncoderSetPipeline(pass, native->pipeline);
                        const auto& v = stateDefinition.viewport;
                        const auto& sc = stateDefinition.scissor;
                        wgpuRenderPassEncoderSetViewport(
                            pass, v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth);
                        wgpuRenderPassEncoderSetScissorRect(pass, sc.x, sc.y, sc.width, sc.height);
                        assert(!group->dynamicOffsetCount);
                        wgpuRenderPassEncoderSetBindGroup(
                            pass, shared.group, group->group, 0u, nullptr);
                        const auto* draw = actualInputs->originalScalars()
                                               ->originalArguments()
                                               ->records()[7u]
                                               .scalar.template get<Scalar::Draw>();
                        assert(draw);
                        wgpuRenderPassEncoderDraw(pass,
                            draw->vertices,
                            draw->instances,
                            draw->firstVertex,
                            draw->firstInstance);
                    }
                    wgpuRenderPassEncoderEnd(pass);
                    wgpuRenderPassEncoderRelease(pass);
                }
            }
            auto from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            from.texture = target->texture;
            from.aspect = WGPUTextureAspect_All;
            auto to = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            to.buffer = readback->buffer;
            to.layout.bytesPerRow = static_cast<std::uint32_t>(pitch.alignedValue);
            to.layout.rowsPerImage = height;
            const WGPUExtent3D extent{width, height, 1u};
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &from, &to, &extent);
            auto commandDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            auto command = wgpuCommandEncoderFinish(encoder, &commandDesc);
            assert(command);
            assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device),
                rhi_webgpu::WebGpuNativeErrorScopeBatch::count,
                0u));
            rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
            wgpuQueueSubmit(queue, 1u, &command);
            assert(wait(queue) && map(readback->buffer, downloadFields.sizeBytes));
            const auto* bytes = static_cast<const std::uint8_t*>(
                wgpuBufferGetConstMappedRange(readback->buffer, 0u, downloadFields.sizeBytes));
            assert(bytes);
            for (std::uint32_t y = 0u; y < height; ++y)
                for (std::uint32_t x = 0u; x < width; ++x)
                    for (std::size_t c = 0u; c < expected.size(); ++c)
                        assert(bytes[y * pitch.alignedValue
                                   + x * rhi::bytesPerPixel(textureFields.format) + c]
                            == expected[c]);
            if constexpr (!execute)
                std::printf(
                    "BINDLESS_RHI_TYPED_ATTACHMENT_DEPENDENCY_PROBE_VALUES pixels%u,%u,%u,%u expected%llu,%llu,%llu,%llu pixels4 clear-store-load-store original-stream2-commands16 texture-points5 contributions6 dependency-between-scopes same-original-view real-completion-map probe-only sync-unfrozen native-closed\n",
                    bytes[0],
                    bytes[1],
                    bytes[2],
                    bytes[3],
                    static_cast<unsigned long long>(expected[0]),
                    static_cast<unsigned long long>(expected[1]),
                    static_cast<unsigned long long>(expected[2]),
                    static_cast<unsigned long long>(expected[3]));
            else if constexpr (!finite)
                std::printf(
                    "BINDLESS_RHI_TYPED_NATIVE_GRAPHICS_VALUES_OK pixels%u,%u,%u,%u pixels4 genuine-original-typed-render-and-queue-commit verification-copy-map-only\n",
                    bytes[0],
                    bytes[1],
                    bytes[2],
                    bytes[3]);
            else
                std::printf(
                    "BINDLESS_RHI_TYPED_NATIVE_FINITE_GRAPHICS_VALUES_OK pixels%u,%u,%u,%u pixels4 expected-from-original-finite-view-and-stride real-typed-queue-commit verification-copy-map-only\n",
                    bytes[0],
                    bytes[1],
                    bytes[2],
                    bytes[3]);
            wgpuBufferUnmap(readback->buffer);
            assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device),
                rhi_webgpu::WebGpuNativeErrorScopeBatch::count,
                0u));
            wgpuCommandBufferRelease(command);
            wgpuCommandEncoderRelease(encoder);
            wgpuQueueRelease(queue);
            assert(
                state.transaction->journalEmpty() && !state.transaction->nativeExecutionProven());
            if constexpr (!execute)
                assert(!state.nativeClosure->validate());
            else
                assert(state.transaction->status()
                    == rhi::OriginalCommandStateTransaction::Status::Committed);
            state = {};
        }
        pipeline.reset();
        context->retire();
        assert(!sourceQuota->liveBytes() && !graphQuota->liveBytes());
        for (const auto& quota : writeQuotas)
            assert(!quota->liveBytes());
        if constexpr (!execute)
            std::puts(
                "BINDLESS_RHI_TYPED_ATTACHMENT_DEPENDENCY_PROBE_RELEASE_OK genuine-owners-reclaimed original-pass-intent-source-refusals metadata-hot0-query0 verification-readback-separated no-new-proof-issuer");
        else if constexpr (!finite)
            std::puts(
                "BINDLESS_RHI_TYPED_NATIVE_GRAPHICS_RELEASE_OK original-source-graph-registry-writes-independent-quotas-zero no-new-proof-issuer");
        else
            std::puts(
                "BINDLESS_RHI_TYPED_NATIVE_FINITE_GRAPHICS_RELEASE_OK original-Source3-finite-writes-graph-registry-independent-quotas-zero no-new-proof-issuer");
#endif
    }
};
} // namespace arp::test
