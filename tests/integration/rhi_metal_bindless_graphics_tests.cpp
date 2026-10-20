#include "../fixtures/bindless_graphics_native_memory_package_fixture.h"
#include "../fixtures/bindless_metal_graphics_pipeline.h"
#include "../fixtures/bindless_metal_public_graphics_fixture.h"
#include "../fixtures/metal_completed_graphics_fixture.h"
#include "../fixtures/metal_native_result_loss_fixture.h"
#include "../fixtures/metal_ordinary_depth_graphics_execution_fixture.h"
#include "../fixtures/metal_original_bindless_graphics_content_fixture.h"
#include "../fixtures/metal_original_bindless_root_fixture.h"
#include "../fixtures/metal_original_depth_execution_fixture.h"
#include "../fixtures/metal_original_storage_execution_fixture.h"
#include "../fixtures/original_bindless_root_source_fixture.h"
#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/bindless_snapshot_metadata.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/gpu_extensions.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;

template <class Result> void check(const Result& result)
{
    if (!result.success)
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
    assert(result.success);
}

class GraphicsExecutionFixture final : public BindlessSnapshotMetadataSource {
public:
    explicit GraphicsExecutionFixture(std::shared_ptr<const BindlessShaderPackage> package,
        std::shared_ptr<GpuDevice> device = {})
        : packageOwner_(std::move(package)), package_(*packageOwner_), device_(std::move(device))
    {
        MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        desc.memoryBudgetBytes = 1024u * 1024u;
        if (!device_)
            device_ = core::shareUnique(createMetalRhiDevice(desc));
        assert(device_);
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device_->queryInterface(kExtensionBindlessCross));
        assert(extension && extension->capabilities().preparedGraphicsBindingContractVersion == 0u
            && extension->capabilities().preparedGraphicsPassFaultScopeContractVersion == 0u);
        const auto& artifact = package_.artifact();
        check(validateMetalBindlessGraphicsPackage(package_));
        assert(artifact.execution == BindlessExecution::Native && artifact.tables.size() == 3u
            && artifact.faultLatch.contractVersion == 1u
            && artifact.faultReportingContractVersion == 1u);
        BindlessFaultDomainDesc faultDesc;
        faultDesc.label = "GraphicsFaultDomain";
        faultDesc.maxResidentBytes = 65536u;
        core::pmr::String diagnostic;
        faultDomain_ = BindlessOwnedFaultDomain::create(*device_, faultDesc, diagnostic);
        assert(faultDomain_);
        core::pmr::Vector<std::uint32_t> metadata(artifact.snapshotMetadata.wordCount);
        check(buildBindlessSnapshotMetadata(artifact.snapshotMetadata,
            faultDomain_->generation(),
            *this,
            metadata.data(),
            metadata.size()));
        metadata_ = buffer("GraphicsMetadata",
            metadata.size() * sizeof(std::uint32_t),
            ResourceUsage::StorageBuffer,
            ResourceState::ShaderRead,
            metadata.data());
        core::pmr::Vector<MetalRhiNativeDescriptorBindingInput> bindings;
        for (const auto& physical : artifact.physicalLayout.bindings) {
            const BindlessTableMapping* table = nullptr;
            for (const auto& candidate : artifact.tables)
                if (candidate.firstBinding == physical.binding)
                    table = &candidate;
            if (!table) {
                assert(physical.arrayCount == 1u);
                if (physical.binding == artifact.snapshotMetadata.binding)
                    append(bindings, physical, 0u, *metadata_);
                else {
                    assert(physical.binding == artifact.faultLatch.binding);
                    append(bindings, physical, 0u, *faultDomain_->faultLatchBuffer());
                }
                continue;
            }
            assert(table->capacity == 2u && physical.arrayCount == table->capacity);
            for (unsigned element = 0u; element < table->capacity; ++element) {
                core::UniquePtr<GpuBuffer> resource;
                const auto stride = table->bufferLayout.elementStrideBytes;
                assert(stride >= table->bufferLayout.minBindingSizeBytes);
                if (table->tableId == 1u) {
                    assert(physical.type == DescriptorType::UniformBuffer
                        && stride == sizeof(colors_[element]));
                    resource = buffer("GraphicsGlobals",
                        stride,
                        ResourceUsage::UniformBuffer,
                        ResourceState::ShaderRead,
                        colors_[element].data());
                } else if (table->tableId == 2u) {
                    assert(physical.type == DescriptorType::ReadOnlyStorageBuffer
                        && stride == sizeof(std::uint32_t));
                    const auto value = 10u * (element + 1u);
                    resource = buffer("GraphicsInputs",
                        stride,
                        ResourceUsage::StorageBuffer,
                        ResourceState::ShaderRead,
                        &value);
                } else {
                    assert(table->tableId == 3u && physical.type == DescriptorType::StorageBuffer
                        && stride == sizeof(std::uint32_t));
                    const auto value = 100u * (element + 1u);
                    resource = buffer("GraphicsCounters",
                        stride,
                        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
                        ResourceState::ShaderWrite,
                        &value);
                    counters_[element] = resource.get();
                }
                append(bindings, physical, element, *resource);
                resources_.push_back(std::move(resource));
            }
        }
        descriptors_ =
            createMetalRhiNativeDescriptorGroupPayload(artifact.physicalLayout, bindings);
        assert(descriptors_ && descriptors_->nativeRuntimeBacked);
        BindlessResourceAccess readback;
        readback.resource = faultDomain_->faultReadbackBuffer()->id();
        readback.kind = BindlessAccessKind::Buffer;
        readback.bufferSize = sizeof(std::uint32_t);
        readback.state = ResourceState::CopyDst;
        readback.pipelineStages = PipelineStage::Transfer;
        readback.access = ResourceAccess::TransferWrite;
        accesses_.push_back(readback);
    }

    std::size_t tableCount() const noexcept override { return package_.artifact().tables.size(); }

    bool readTable(std::size_t index, BindlessMetadataTableSource& out) const override
    {
        if (index >= tableCount())
            return false;
        const auto& table = package_.artifact().tables[index];
        out = {&table, table.capacity};
        return true;
    }

    bool readElementCount(
        std::size_t index, std::uint32_t element, std::uint32_t& count) const override
    {
        if (index >= tableCount() || element >= package_.artifact().tables[index].capacity)
            return false;
        count = 1u;
        return true;
    }

    void validateDomainCancellationAndIdentity()
    {
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        BindlessFaultDomainDesc desc;
        desc.label = "GraphicsDomainOwnership";
        desc.maxResidentBytes = 65536u;
        for (unsigned mode = 0u; mode < 2u; ++mode) {
            auto domain = BindlessOwnedFaultDomain::create(*device_, desc, diagnostic);
            assert(domain);
            auto encoder = device_->createCommandEncoder({"GraphicsDomainCancel", 1u, 1u});
            assert(encoder);
            auto native = metalRhiNativePayloadFor(*encoder);
            assert(native);
            assert(native->stats()->nativeBindlessFaultOutcomePreparationCount == 1u
                && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 0u);
            assert(native->recordBindlessFaultLatchClearBeforePass(
                domain, ResourceState::Undefined, diagnostic));
            assert(domain->faultLatchPriorState() == ResourceState::Undefined);
            assert(!native->finish(diagnostic)
                && diagnostic == "RHI-METAL-BINDLESS-FAULT-READBACK-NOT-SCHEDULED");
            std::shared_ptr<MetalRhiBindlessFaultReadback> readback;
            if (mode) {
                readback = native->scheduleBindlessFaultReadback(domain, diagnostic);
                assert(readback);
                assert(native->stats()->nativeBindlessFaultOutcomePreparationCount == 1u
                    && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 1u);
                auto commands = encoder->finish();
                assert(commands);
                encoder.reset();
                commands.reset();
                assert(readback->status() == BindlessFaultValidationStatus::Abandoned);
            } else
                encoder.reset();
            assert(domain->faultLatchPriorState() == ResourceState::Undefined);
            auto lease = BindlessFaultSubmissionLease::acquire(domain, diagnostic);
            assert(lease.valid());
        }
        auto null = createNullDevice();
        auto foreignNull = BindlessOwnedFaultDomain::create(*null, desc, diagnostic);
        assert(foreignNull);
        auto encoder = device_->createCommandEncoder({"GraphicsDomainForeign", 1u, 1u});
        assert(encoder);
        auto native = metalRhiNativePayloadFor(*encoder);
        assert(native);
        assert(!native->recordBindlessFaultLatchClearBeforePass(
                   foreignNull, ResourceState::Undefined, diagnostic)
            && diagnostic == "RHI-METAL-BINDLESS-FAULT-DOMAIN-DEVICE-MISMATCH");
        assert(!native->stats()->nativeBlitEncoderCreated);
        // Two L2 devices can share one physical Metal device. Registry ownership
        // must still reject the other logical device's resources.
        MetalRhiDeviceDesc otherDesc;
        otherDesc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        otherDesc.capabilityContract.queriedFromNativeDevice = true;
        otherDesc.capabilityContract.queriedExtendedFeatures = true;
        otherDesc.uploadStagingPageSizeBytes = 4096u;
        otherDesc.uploadStagingMaxPageCount = 1u;
        otherDesc.memoryBudgetBytes = 1024u * 1024u;
        auto other = createMetalRhiDevice(otherDesc);
        assert(other);
        auto foreignMetal = BindlessOwnedFaultDomain::create(*other, desc, diagnostic);
        assert(foreignMetal);
        assert(!native->recordBindlessFaultLatchClearBeforePass(
                   foreignMetal, ResourceState::Undefined, diagnostic)
            && diagnostic == "RHI-METAL-BINDLESS-FAULT-DOMAIN-DEVICE-MISMATCH");
        assert(!native->stats()->nativeBlitEncoderCreated);
        std::puts("faultDomainCancellationAndLogicalDeviceIdentity: passed");
    }

    void validateRetainedPipelineFactory()
    {
        auto contract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        RenderPipelineDesc state;
        state.colorFormat = TextureFormat::Rgba8Unorm;
        state.dynamicViewportScissor = true;
        core::pmr::String diagnostic;
        auto pipeline = core::shareUnique(retainedPipeline("cleanFragment"));
        for (unsigned mode = 0u; mode < 8u; ++mode) {
            auto invalid = state;
            if (mode == 0u)
                invalid.layout = pipeline->layout();
            if (mode == 1u)
                invalid.vertexShader.entryPoint = "caller-authored";
            if (mode == 2u)
                invalid.multisample.sampleCount = 2u;
            if (mode == 3u)
                invalid.subpassIndex = 1u;
            if (mode == 4u)
                invalid.topology = PrimitiveTopology::PatchList;
            if (mode == 5u)
                invalid.dynamicViewportScissor = false;
            if (mode == 6u)
                invalid.colorFormat = TextureFormat::Unknown;
            assert(!createMetalRhiRetainedGraphicsPipeline(packageOwner_,
                invalid,
                "vertexMain",
                mode == 7u ? std::string_view{} : std::string_view{"cleanFragment"},
                contract,
                diagnostic));
            const auto expected = mode < 2u ? "RHI-BINDLESS-PIPELINE-AUTHORED-INTERFACE"
                : mode < 5u                 ? "RHI-BINDLESS-PIPELINE-GRAPHICS-ABI-UNAVAILABLE"
                : mode == 5u                ? "RHI-BINDLESS-PIPELINE-VIEWPORT-SCISSOR-REQUIRED"
                : mode == 6u ? "depth-only render pipeline requires explicit depth/stencil format"
                             : "RHI-BINDLESS-PIPELINE-FRAGMENT-REQUIRED";
            assert(diagnostic == expected);
        }
        assert(!createMetalRhiRetainedGraphicsPipeline(
                   {}, state, "vertexMain", "cleanFragment", contract, diagnostic)
            && diagnostic == "RHI-METAL-BINDLESS-GRAPHICS-PACKAGE-MISSING");
        const auto packageAlias =
            std::shared_ptr<const BindlessShaderPackage>(pipeline, packageOwner_.get());
        assert(!createMetalRhiRetainedGraphicsPipeline(
                   packageAlias, state, "vertexMain", "cleanFragment", contract, diagnostic)
            && diagnostic == "RHI-METAL-BINDLESS-GRAPHICS-PACKAGE-OWNER-MISMATCH");
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            assert(!createMetalRhiRetainedGraphicsPipeline(packageOwner_,
                       state,
                       mode == 0u       ? "missingVertex"
                           : mode == 1u ? "cleanFragment"
                                        : "vertexMain",
                       mode == 2u ? "vertexMain" : "cleanFragment",
                       contract,
                       diagnostic)
                && diagnostic == "RHI-BINDLESS-PIPELINE-ENTRY-MISMATCH");
        }
        auto unsupported = contract;
        unsupported.backend = Backend::Null;
        assert(!createMetalRhiRetainedGraphicsPipeline(
                   packageOwner_, state, "vertexMain", "cleanFragment", unsupported, diagnostic)
            && diagnostic == "RHI-METAL-BINDLESS-GRAPHICS-BACKEND-MISMATCH");
        unsupported = contract;
        unsupported.features.erase(std::remove(unsupported.features.begin(),
                                       unsupported.features.end(),
                                       semantics::NativeBackendFeature::ArgumentBuffers),
            unsupported.features.end());
        assert(!createMetalRhiRetainedGraphicsPipeline(
                   packageOwner_, state, "vertexMain", "cleanFragment", unsupported, diagnostic)
            && diagnostic == "RHI-METAL-DESCRIPTOR-TABLE-CAPABILITY-NOT-AUTHORIZED");
        ShaderPackageDesc interface;
        const BindlessPackageModule* selected = nullptr;
        assert(appendBindlessShaderEntry(
            package_, "vertexMain", ShaderStage::Vertex, interface, diagnostic, &selected));
        assert(selected && selected->entry == "vertexMain");
        assert(!appendBindlessShaderEntry(
                   package_, "cleanFragment", ShaderStage::Vertex, interface, diagnostic, &selected)
            && !selected);
        // This independent publication owner must be retained by the concrete
        // pipeline after source callers release it; no borrowed factory view.
        auto package = BindlessShaderPackage::load(ARP_METAL_GRAPHICS_PUBLICATION,
            {1024u * 1024u, 4u * 1024u * 1024u, 32u, 3u, 8u, 3u},
            diagnostic);
        assert(package);
        std::weak_ptr<const BindlessShaderPackage> observed = package;
        auto owned = createMetalRhiRetainedGraphicsPipeline(
            package, state, "vertexMain", "cleanFragment", contract, diagnostic);
        assert(owned);
        package.reset();
        assert(!observed.expired());
        assert(owned->layout()->desc().descriptorLayouts.front().bindings.size()
                == package_.artifact().physicalLayout.bindings.size()
            && owned->validationReport().entrypointCount == 2u
            && owned->shaderReflection().pushConstantByteCount == 0u);
        owned.reset();
        assert(observed.expired());
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device_->queryInterface(kExtensionBindlessCross));
        assert(extension);
        assert(
            !extension->createRenderPipeline({}, state, "vertexMain", "cleanFragment", diagnostic)
            && diagnostic == "RHI-METAL-BINDLESS-GRAPHICS-EXECUTION-UNAVAILABLE");
        std::puts(
            "retainedGraphicsFactory: state/entry/capability gates and owned package/layout passed");
    }

    int capture(const char* path)
    {
        const auto capability = device_->frameCaptureCapability(FrameCaptureTool::NativeGpuTrace);
        if (!capability.available) {
            std::fprintf(stderr, "%s\n", capability.diagnostics.c_str());
            return 1;
        }
        FrameCaptureRequest request;
        request.outputPath = path;
        const auto started = device_->beginFrameCapture(request);
        if (!started.success()) {
            std::fprintf(stderr, "%s\n", started.diagnostics.c_str());
            return 1;
        }
        core::pmr::String output{path, &core::defaultCpuMemoryResource()};
        output += ".fault.ppm";
        run("faultFragment", true, false, 0u, output.c_str());
        output = path;
        output += ".clean.ppm";
        run("cleanFragment", false, true, 1u, output.c_str());
        const auto completed = device_->endFrameCapture();
        if (!completed.success()) {
            std::fprintf(stderr, "%s\n", completed.diagnostics.c_str());
            return 1;
        }
        std::printf("METAL_BINDLESS_GRAPHICS_CAPTURE_COMPLETED %s\n", path);
        return 0;
    }

    void run(std::string_view entry,
        bool faulted,
        bool releaseOwners = true,
        unsigned previousDraws = 0u,
        const char* outputPath = nullptr)
    {
        test::MetalGraphicsPipelineFixture fixture(package_, "vertexMain", entry);
        auto ownedPipeline = retainedPipeline(entry);
        auto pipeline = metalRhiNativePayloadFor(*ownedPipeline);
        assert(pipeline && pipeline->nativeRuntimeBacked);
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        auto prepared = prepare(pipeline, entry);
        constexpr unsigned width = 2u, height = 2u;
        const auto footprint = core::alignCpuAllocationOffsetUp(
            width * bytesPerPixel(TextureFormat::Rgba8Unorm), kTextureUploadRowPitchAlignment);
        assert(footprint.success && footprint.alignedValue <= UINT32_MAX);
        const auto rowBytes = static_cast<std::uint32_t>(footprint.alignedValue),
                   pixelBytes = rowBytes * height;
        constexpr unsigned dataBytes = 2u * sizeof(std::uint32_t);
        auto target = device_->createTexture({"GraphicsExecutionTarget",
            {width, height, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::RenderAttachment | TextureUsage::CopySrc,
            MemoryUsage::GpuOnly,
            ResourceState::ColorAttachment});
        auto readback = device_->createBuffer({"GraphicsExecutionReadback",
            pixelBytes + dataBytes,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        assert(target && readback);
        auto encoder = device_->createCommandEncoder({"PrivateGraphicsExecution",
            1u,
            1u,
            0u,
            0u,
            static_cast<std::uint32_t>(accesses_.size())});
        assert(encoder);
        auto native = metalRhiNativePayloadFor(*encoder);
        assert(native);
        BindlessAccessArray declarations(accesses_.data(), accesses_.size());
        const auto beforeIntent = encoder->lastValidation().recordedCommandCount;
        assert(encoder->prepareBindlessGraphicsPass({}, declarations).diagnostics
            == "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-UNAVAILABLE");
        assert(encoder->lastValidation().recordedCommandCount == beforeIntent);
        assert(
            !native->prepareGraphicsPassScope(
                std::shared_ptr<const BindlessPreparedGraphicsBinding>{}, declarations, diagnostic)
            && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH");
        assert(!native->stats()->nativeBlitEncoderCreated
            && !native->stats()->nativeRenderEncoderCreated);
        auto preparedAlias = std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>(
            faultDomain_, prepared.get());
        assert(!validateMetalRhiNativeGraphicsBindings(preparedAlias, diagnostic)
            && diagnostic == "RHI-METAL-PREPARED-GRAPHICS-OWNER-MISMATCH");
        assert(
            !native->prepareGraphicsPassScope(preparedAlias, faultDomain_, declarations, diagnostic)
            && diagnostic == "RHI-METAL-PREPARED-GRAPHICS-OWNER-MISMATCH");
        assert(!native->stats()->nativeBlitEncoderCreated
            && !native->stats()->nativeRenderEncoderCreated
            && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 0u);
        preparedAlias.reset();
        auto missing = accesses_;
        missing.pop_back();
        assert(!native->prepareGraphicsPassScope(prepared,
                   faultDomain_,
                   BindlessAccessArray(missing.data(), missing.size()),
                   diagnostic)
            && diagnostic == "RHI-BINDLESS-ACCESS-SET-INCOMPLETE");
        assert(!native->stats()->nativeBlitEncoderCreated);
        assert(native->prepareGraphicsPassScope(prepared, faultDomain_, declarations, diagnostic));
        assert(!native->prepareGraphicsPassScope(prepared, faultDomain_, declarations, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-RECORDING-INVALID");
        assert(!native->recordComputePassBegin(diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
        assert(!native->recordBlitPassBegin(diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
        assert(
            !native->finish(diagnostic) && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-NOT-CONSUMED");
        auto competing = device_->createCommandEncoder({"GraphicsCompetingFaultLease", 1u, 1u});
        assert(competing);
        assert(!metalRhiNativePayloadFor(*competing)
                   ->recordBindlessFaultLatchClearBeforePass(
                       faultDomain_, faultDomain_->faultLatchPriorState(), diagnostic)
            && diagnostic == "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
        competing.reset();
        RenderPassDesc render;
        RenderPassColorAttachmentDesc color;
        color.target = target->id();
        color.format = target->desc().format;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        color.clearColor = {0.25f, 0.5f, 0.75f, 1.0f};
        render.colorAttachments.push_back(color);
        auto incompatible = render;
        incompatible.colorAttachments[0].format = TextureFormat::Rgba16Float;
        assert(!native->recordRenderPassBegin(incompatible, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-PASS-MISMATCH");
        assert(!native->stats()->nativeRenderEncoderCreated);
        auto pass = encoder->beginRenderPass(render);
        assert(pass);
        assert(!pass->queryInterface(kExtensionBindlessCross));
        assert(pass->nextSubpass().diagnostics == "RHI-METAL-GRAPHICS-SCOPE-SUBPASS-UNSUPPORTED");
        assert(pass->drawIndexedIndirect(*readback, 0u, 1u, sizeof(DrawIndexedIndirectCommand))
                   .diagnostics
            == "RHI-METAL-GRAPHICS-SCOPE-INDIRECT-UNSUPPORTED");
        assert(pass->drawIndexedIndirectCount(
                       *readback, 0u, *readback, 0u, 1u, sizeof(DrawIndexedIndirectCommand))
                   .diagnostics
            == "RHI-METAL-GRAPHICS-SCOPE-INDIRECT-UNSUPPORTED");
        auto nullDevice = createNullDevice();
        auto secondaryEncoder =
            nullDevice->createSecondaryCommandEncoder({"RejectedGraphicsSecondary"});
        auto bundleEncoder = nullDevice->createRenderBundleEncoder({"RejectedGraphicsBundle"});
        assert(secondaryEncoder && bundleEncoder);
        auto secondary = secondaryEncoder->finish();
        auto bundle = bundleEncoder->finish();
        assert(secondary && bundle);
        assert(pass->executeSecondary(*secondary).diagnostics
            == "RHI-METAL-GRAPHICS-SCOPE-SECONDARY-UNSUPPORTED");
        assert(pass->executeRenderBundle(*bundle).diagnostics
            == "RHI-METAL-GRAPHICS-SCOPE-SECONDARY-UNSUPPORTED");
        auto otherPipeline = fixture.create();
        assert(otherPipeline);
        assert(!native->recordGraphicsPipelineBind(otherPipeline, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-PIPELINE-MISMATCH");
        assert(native->recordGraphicsPipelineBind(pipeline, diagnostic));
        assert(native->recordSetViewport({0, 0, width, height, 0, 1}, diagnostic));
        assert(native->recordSetScissor({0, 0, width, height}, diagnostic));
        assert(!native->recordPreparedGraphicsBinding(prepared, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH");
        auto otherPrepared = prepare(pipeline, entry);
        assert(!native->recordScopedGraphicsBinding(otherPrepared, declarations, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH");
        otherPrepared.reset();
        otherPipeline.reset();
        assert(!native->recordDescriptorSetBind(package_.artifact().set,
                   descriptors_,
                   ShaderStage::Vertex | ShaderStage::Fragment,
                   nullptr,
                   0u,
                   diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH");
        assert(!native->recordDescriptorTableBind(
                   package_.artifact().set, {}, ShaderStage::Fragment, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH");
        assert(!native->recordDraw(3u, 1u, 0u, 0u, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-BINDING-REQUIRED");
        assert(!native->recordRenderPassEnd(diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-BINDING-REQUIRED");
        auto reordered = accesses_;
        std::swap(reordered[0], reordered[1]);
        assert(!native->recordScopedGraphicsBinding(
                   prepared, BindlessAccessArray(reordered.data(), reordered.size()), diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-BINDING-MISMATCH");
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 32u; ++i)
            assert(native->recordScopedGraphicsBinding(prepared, declarations, diagnostic));
        assert(!native->recordDrawIndexedIndirect({}, 0u, 1u, 20u, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-INDIRECT-UNSUPPORTED");
        assert(!native->recordDrawIndexedIndirectCount({}, 0u, {}, 0u, 1u, 20u, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-INDIRECT-UNSUPPORTED");
        assert(!native->recordNextSubpass(diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-SUBPASS-UNSUPPORTED");
        assert(!native->executeSecondary({}, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-SECONDARY-UNSUPPORTED");
        assert(!native->executeSecondaryCommand({}, 0u, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-SECONDARY-UNSUPPORTED");
        assert(native->recordDraw(3u, 1u, 0u, 0u, diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        check(pass->end());
        assert(pass->draw(3u, 1u, 0u, 0u).diagnostics == "RHI-METAL-GRAPHICS-PASS-NOT-OPEN");
        assert(native->bindlessFaultReport(faultDomain_->generation()).status
            == BindlessFaultValidationStatus::Pending);
        TextureSubresourceRange range;
        check(encoder->transition(
            *target, range, ResourceState::ColorAttachment, ResourceState::CopySrc));
        for (auto* counter : counters_)
            check(
                encoder->transition(*counter, ResourceState::ShaderWrite, ResourceState::CopySrc));
        auto copy = encoder->beginTransferPass({"GraphicsExecutionReadback"});
        assert(copy);
        check(copy->copyTextureToBuffer(
            {target->id(), readback->id(), range, 0u, rowBytes, height, target->desc().extent}));
        for (unsigned i = 0u; i < counters_.size(); ++i)
            check(copy->copyBufferToBuffer({counters_[i]->id(),
                readback->id(),
                0u,
                pixelBytes + i * sizeof(std::uint32_t),
                sizeof(std::uint32_t)}));
        check(copy->end());
        if (!releaseOwners)
            for (auto* counter : counters_)
                check(encoder->transition(
                    *counter, ResourceState::CopySrc, ResourceState::ShaderWrite));
        auto commands = encoder->finish();
        assert(commands);
        std::weak_ptr<const MetalRhiNativePreparedGraphicsBindings> observed = prepared;
        prepared.reset();
        if (releaseOwners) {
            descriptors_.reset();
            resources_.clear();
            metadata_.reset();
        }
        const auto generation = faultDomain_->generation();
        std::weak_ptr<BindlessOwnedFaultDomain> observedDomain = faultDomain_;
        if (releaseOwners)
            faultDomain_.reset();
        auto timeline = device_->createTimelineSemaphore(0u);
        auto fence = device_->createFence();
        assert(timeline && fence);
        check(device_->queue().submit(*commands, timeline.get(), 1u, nullptr, 0u, fence.get()));
        commands.reset();
        encoder.reset();
        assert(!fence->isSignaled() && !observed.expired() && !observedDomain.expired());
        assert(native->bindlessFaultReport(generation).status
            == BindlessFaultValidationStatus::Pending);
        timeline->signal(1u);
        fence->wait();
        assert(observed.expired()
            && (releaseOwners ? observedDomain.expired() : !observedDomain.expired()));
        if (!releaseOwners) {
            assert(faultDomain_->faultLatchPriorState() == ResourceState::CopySrc);
            auto lease = BindlessFaultSubmissionLease::acquire(faultDomain_, diagnostic);
            assert(lease.valid());
        }
        const auto faultReport = native->bindlessFaultReport(generation);
        assert(faultReport.status
                == (faulted ? BindlessFaultValidationStatus::Faulted
                            : BindlessFaultValidationStatus::Clean)
            && faultReport.faultBits == (faulted ? 1u : 0u)
            && faultReport.validate().success == !faulted);
        core::pmr::Vector<std::uint8_t> pixels(pixelBytes);
        std::array<std::uint32_t, 2> words{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
            pixels.data(),
            pixels.size(),
            0u,
            readback->desc().sizeBytes,
            diagnostic));
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
            words.data(),
            sizeof(words),
            pixelBytes,
            readback->desc().sizeBytes,
            diagnostic));
        assert((words
            == std::array<std::uint32_t, 2>{102u + 2u * previousDraws, 202u + 2u * previousDraws}));
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x)
                for (unsigned channel = 0u; channel < 4u; ++channel)
                    assert(pixels[y * rowBytes + x * 4u + channel]
                        == (colors_[x][channel] == 1.0f ? 255u : 0u));
        if (outputPath) {
            auto* file = std::fopen(outputPath, "wb");
            assert(file);
            assert(std::fprintf(file, "P6\n%u %u\n255\n", width, height) > 0);
            for (unsigned y = 0u; y < height; ++y)
                for (unsigned x = 0u; x < width; ++x)
                    assert(std::fwrite(pixels.data() + y * rowBytes + x * 4u, 1u, 3u, file) == 3u);
            assert(std::fclose(file) == 0);
        }
        std::printf(
            "entry: %.*s\ncounters: [%u, %u]\nfaultBits: %u\npixels: red/green, 2x2 exact RGBA\ncoreAllocationsDuringBindAndDraw: 0\n",
            int(entry.size()),
            entry.data(),
            words[0],
            words[1],
            faultReport.faultBits);
    }

    void validateScopeRejectionAndCancellation()
    {
        test::MetalGraphicsPipelineFixture fixture(package_, "vertexMain", "cleanFragment");
        auto pipeline = fixture.create();
        assert(pipeline);
        auto prepared = prepare(pipeline, "cleanFragment");
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        const auto count = static_cast<std::uint32_t>(accesses_.size());
        BindlessAccessArray declarations(accesses_.data(), accesses_.size());
        for (unsigned mode = 0u; mode < 4u; ++mode) {
            CommandEncoderDesc desc{"GraphicsScopeBudget",
                mode == 0u ? 0u : 1u,
                mode == 1u ? 0u : 1u,
                0u,
                0u,
                mode == 2u       ? 0u
                    : mode == 3u ? count - 1u
                                 : count};
            auto encoder = device_->createCommandEncoder(desc);
            assert(encoder);
            auto native = metalRhiNativePayloadFor(*encoder);
            assert(native);
            assert(!native->prepareGraphicsPassScope(
                prepared, faultDomain_, declarations, diagnostic));
            assert(diagnostic
                == (mode < 2u ? "RHI-METAL-GRAPHICS-SCOPE-COMMAND-BUDGET"
                              : "RHI-BINDLESS-GRAPHICS-PASS-ACCESS-BUDGET"));
            assert(!native->stats()->nativeBlitEncoderCreated
                && !native->stats()->nativeRenderEncoderCreated);
        }
        auto encoder =
            device_->createCommandEncoder({"GraphicsScopeDeclarations", 1u, 1u, 0u, 0u, count});
        assert(encoder);
        auto native = metalRhiNativePayloadFor(*encoder);
        assert(native);
        for (unsigned mode = 0u; mode < 8u; ++mode) {
            auto invalid = accesses_;
            if (mode == 0u)
                invalid.erase(invalid.begin());
            if (mode == 1u)
                --invalid[0].bufferSize;
            if (mode == 2u)
                invalid[0].stages = ShaderStage::Compute;
            if (mode == 3u)
                invalid[0].pipelineStages = PipelineStage::Transfer;
            if (mode == 4u)
                invalid[0].state = ResourceState::CopySrc;
            if (mode == 5u)
                invalid[0].access = ResourceAccess::TransferRead;
            if (mode == 6u)
                invalid[0].resource = {};
            if (mode == 7u)
                ++invalid[0].bufferSize;
            assert(!native->prepareGraphicsPassScope(prepared,
                faultDomain_,
                BindlessAccessArray(invalid.data(), invalid.size()),
                diagnostic));
            assert(diagnostic
                == (mode < 6u ? "RHI-BINDLESS-ACCESS-SET-INCOMPLETE"
                              : "RHI-METAL-GRAPHICS-SCOPE-ACCESS-RESOURCE-MISMATCH"));
            assert(!native->stats()->nativeBlitEncoderCreated
                && !native->stats()->nativeRenderEncoderCreated);
            auto lease = BindlessFaultSubmissionLease::acquire(faultDomain_, diagnostic);
            assert(lease.valid());
        }
        BindlessFaultDomainDesc domainDesc;
        domainDesc.label = "WrongScopeDomain";
        domainDesc.maxResidentBytes = 65536u;
        auto otherDomain = BindlessOwnedFaultDomain::create(*device_, domainDesc, diagnostic);
        assert(otherDomain);
        assert(!native->prepareGraphicsPassScope(prepared, otherDomain, declarations, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-FAULT-DOMAIN-MISMATCH");
        assert(!native->prepareGraphicsPassScope(prepared, {}, declarations, diagnostic)
            && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-FAULT-DOMAIN-INVALID");
        assert(!native->stats()->nativeBlitEncoderCreated);
        encoder.reset();

        auto target = device_->createTexture({"ScopeCancellationAttachment",
            {2u, 2u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::RenderAttachment,
            MemoryUsage::GpuOnly,
            ResourceState::ColorAttachment});
        assert(target);
        RenderPassDesc render;
        RenderPassColorAttachmentDesc color;
        color.target = target->id();
        color.format = target->desc().format;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        render.colorAttachments.push_back(color);
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            encoder =
                device_->createCommandEncoder({"GraphicsScopeCancellation", 1u, 1u, 0u, 0u, count});
            assert(encoder);
            native = metalRhiNativePayloadFor(*encoder);
            assert(native);
            auto owned = prepare(pipeline, "cleanFragment");
            std::weak_ptr<const MetalRhiNativePreparedGraphicsBindings> observed = owned;
            assert(native->prepareGraphicsPassScope(owned, faultDomain_, declarations, diagnostic));
            owned.reset();
            assert(!observed.expired());
            if (mode > 0u)
                assert(native->recordRenderPassBegin(render, diagnostic));
            if (mode == 2u) {
                assert(native->recordGraphicsPipelineBind(pipeline, diagnostic));
                auto retained = observed.lock();
                assert(retained);
                assert(native->recordScopedGraphicsBinding(retained, declarations, diagnostic));
                retained.reset();
                assert(native->recordRenderPassEnd(diagnostic));
                assert(native->bindlessFaultReport(faultDomain_->generation()).status
                    == BindlessFaultValidationStatus::Pending);
                auto commands = encoder->finish();
                assert(commands);
                commands.reset();
            }
            encoder.reset();
            assert(observed.expired()
                && faultDomain_->faultLatchPriorState() == ResourceState::Undefined);
            if (mode == 2u)
                assert(native->bindlessFaultReport(faultDomain_->generation()).status
                    == BindlessFaultValidationStatus::Abandoned);
            auto lease = BindlessFaultSubmissionLease::acquire(faultDomain_, diagnostic);
            assert(lease.valid());
        }
        std::puts("scopeBudgetsDeclarationsIdentityAndCancellation: passed");
    }

    void validateAttachmentPreservation(bool depth)
    {
        const auto fragment = depth ? std::string_view{} : std::string_view{"cleanFragment"};
        test::MetalGraphicsPipelineFixture fixture(package_, "vertexMain", fragment);
        auto ownedPipeline = retainedPipeline(fragment);
        auto pipeline = metalRhiNativePayloadFor(*ownedPipeline);
        assert(pipeline);
        auto prepared = prepare(pipeline, fragment);
        if (depth)
            assert(!prepared->stages[1] && !prepared->layouts[1] && !pipeline->fragmentFunction);
        constexpr unsigned width = 2u, height = 2u;
        const auto format = depth ? TextureFormat::D32Float : TextureFormat::Rgba8Unorm;
        const auto initial =
            depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        const auto pitch = core::alignCpuAllocationOffsetUp(
            width * bytesPerPixel(format), kTextureUploadRowPitchAlignment);
        assert(pitch.success && pitch.alignedValue <= UINT32_MAX);
        const auto rowBytes = static_cast<std::uint32_t>(pitch.alignedValue),
                   pixelBytes = rowBytes * height;
        auto target = device_->createTexture({"ScopedAttachmentPreservation",
            {width, height, 1u},
            format,
            TextureUsage::RenderAttachment | TextureUsage::CopySrc,
            MemoryUsage::GpuOnly,
            initial});
        assert(target);
        auto output = device_->createBuffer({"ScopedAttachmentReadback",
            pixelBytes + sizeof(std::uint32_t) * counters_.size(),
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        assert(output);
        auto encoder = device_->createCommandEncoder({"ScopedAttachmentPreservation",
            1u,
            1u,
            0u,
            0u,
            static_cast<std::uint32_t>(accesses_.size())});
        assert(encoder);
        auto native = metalRhiNativePayloadFor(*encoder);
        assert(native);
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        RenderPassDesc render;
        if (depth) {
            render.hasDepthStencilAttachment = true;
            render.depthStencilAttachment.target = target->id();
            render.depthStencilAttachment.format = format;
            render.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
            render.depthStencilAttachment.depthStoreOp = StoreOp::Store;
        } else {
            RenderPassColorAttachmentDesc color;
            color.target = target->id();
            color.format = format;
            color.loadOp = LoadOp::Clear;
            color.storeOp = StoreOp::Store;
            color.clearColor = {0, 0, 1, 1};
            render.colorAttachments.push_back(color);
            assert(native->recordRenderPassBegin(render, diagnostic));
            assert(native->recordRenderPassEnd(diagnostic));
            render.colorAttachments[0].loadOp = LoadOp::Load;
            // A conspicuous clear value must remain unused by the scoped Load.
            render.colorAttachments[0].clearColor = {1, 0, 1, 1};
        }
        BindlessAccessArray declarations(accesses_.data(), accesses_.size());
        assert(native->prepareGraphicsPassScope(prepared, faultDomain_, declarations, diagnostic));
        assert(native->recordRenderPassBegin(render, diagnostic));
        assert(native->recordGraphicsPipelineBind(pipeline, diagnostic));
        assert(native->recordSetViewport({0, 0, width, height, 0, 1}, diagnostic));
        assert(native->recordSetScissor({0, 0, depth ? width : 1u, height}, diagnostic));
        assert(native->recordScopedGraphicsBinding(prepared, declarations, diagnostic));
        assert(native->recordDraw(3u, 1u, 0u, 0u, diagnostic));
        assert(native->recordRenderPassEnd(diagnostic));
        assert(native->bindlessFaultReport(faultDomain_->generation()).status
            == BindlessFaultValidationStatus::Pending);
        TextureSubresourceRange range;
        range.aspect = depth ? TextureAspect::Depth : TextureAspect::Color;
        check(encoder->transition(*target, range, initial, ResourceState::CopySrc));
        for (auto* counter : counters_)
            check(
                encoder->transition(*counter, ResourceState::ShaderWrite, ResourceState::CopySrc));
        auto copy = encoder->beginTransferPass({"ScopedAttachmentReadback"});
        assert(copy);
        check(copy->copyTextureToBuffer(
            {target->id(), output->id(), range, 0u, rowBytes, height, target->desc().extent}));
        for (unsigned i = 0u; i < counters_.size(); ++i)
            check(copy->copyBufferToBuffer({counters_[i]->id(),
                output->id(),
                0u,
                pixelBytes + i * sizeof(std::uint32_t),
                sizeof(std::uint32_t)}));
        check(copy->end());
        auto commands = encoder->finish();
        assert(commands);
        auto fence = device_->createFence();
        assert(fence);
        check(device_->queue().submit(*commands, nullptr, 0u, nullptr, 0u, fence.get()));
        fence->wait();
        assert(faultDomain_->faultLatchPriorState() == ResourceState::CopySrc);
        const auto report = native->bindlessFaultReport(faultDomain_->generation());
        assert(report.status == BindlessFaultValidationStatus::Clean && report.faultBits == 0u);
        core::pmr::Vector<std::uint8_t> pixels(pixelBytes);
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*output),
            pixels.data(),
            pixels.size(),
            0u,
            output->desc().sizeBytes,
            diagnostic));
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x) {
                const auto offset = y * rowBytes + x * 4u;
                if (depth) {
                    float value = 1.0f;
                    std::memcpy(&value, pixels.data() + offset, sizeof(value));
                    assert(value == 0.0f);
                } else {
                    const std::array<std::uint8_t, 4> expected = x == 0u
                        ? std::array<std::uint8_t, 4>{255, 0, 0, 255}
                        : std::array<std::uint8_t, 4>{0, 0, 255, 255};
                    for (unsigned channel = 0u; channel < 4u; ++channel)
                        assert(pixels[offset + channel] == expected[channel]);
                }
            }
        std::array<std::uint32_t, 2> words{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*output),
            words.data(),
            sizeof(words),
            pixelBytes,
            output->desc().sizeBytes,
            diagnostic));
        assert((words
            == (depth ? std::array<std::uint32_t, 2>{100u, 200u}
                      : std::array<std::uint32_t, 2>{102u, 200u})));
        std::puts(depth
                ? "vertexOnlyScopedDepth: exact D32 zero, Clean, counters unchanged"
                : "scopedColorLoadStore: exact red/blue, preserved untouched column, Clean");
    }

    void validateMultipleScopeBudgets(unsigned mode)
    {
        GraphicsExecutionFixture peer(packageOwner_, device_);
        test::MetalGraphicsPipelineFixture cleanFixture(package_, "vertexMain", "cleanFragment");
        test::MetalGraphicsPipelineFixture faultFixture(package_, "vertexMain", "faultFragment");
        auto cleanPipeline = cleanFixture.create(), faultPipeline = faultFixture.create();
        assert(cleanPipeline && faultPipeline);
        auto clean = prepare(cleanPipeline, "cleanFragment"),
             fault = peer.prepare(faultPipeline, "faultFragment");
        const auto createTarget = [&] {
            return device_->createTexture({"MultipleScopeAttachment",
                {2u, 2u, 1u},
                TextureFormat::Rgba8Unorm,
                TextureUsage::RenderAttachment,
                MemoryUsage::GpuOnly,
                ResourceState::ColorAttachment});
        };
        auto firstTarget = createTarget(), secondTarget = createTarget();
        assert(firstTarget && secondTarget);
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        auto encoder = device_->createCommandEncoder({"MultipleGraphicsScopes",
            mode == 0u ? 1u : 2u,
            mode == 1u ? 1u : 2u,
            0u,
            0u,
            static_cast<std::uint32_t>(accesses_.size())});
        assert(encoder);
        auto native = metalRhiNativePayloadFor(*encoder);
        assert(native);
        auto captured = accesses_;
        BindlessAccessArray declarations(captured.data(), captured.size());
        BindlessAccessArray sourceDeclarations(accesses_.data(), accesses_.size());
        BindlessAccessArray peerDeclarations(peer.accesses_.data(), peer.accesses_.size());
        const auto record =
            [&](const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>& binding,
                const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline,
                const BindlessAccessDeclarations& intent,
                const GpuTexture& target) {
                RenderPassDesc render;
                RenderPassColorAttachmentDesc color;
                color.target = target.id();
                color.format = target.desc().format;
                color.loadOp = LoadOp::Clear;
                color.storeOp = StoreOp::Store;
                render.colorAttachments.push_back(color);
                assert(native->recordRenderPassBegin(render, diagnostic));
                assert(native->recordGraphicsPipelineBind(pipeline, diagnostic));
                assert(native->recordSetViewport({0, 0, 2u, 2u, 0, 1}, diagnostic));
                assert(native->recordSetScissor({0, 0, 2u, 2u}, diagnostic));
                assert(native->recordScopedGraphicsBinding(binding, intent, diagnostic));
                assert(native->recordDraw(3u, 1u, 0u, 0u, diagnostic));
                assert(native->recordRenderPassEnd(diagnostic));
            };
        assert(
            native->prepareGraphicsPassScope(clean, faultDomain_, sourceDeclarations, diagnostic));
        // Capturing intent gives the command ownership independent from source
        // lifetime or caller mutations. Reordering the source cannot rewrite it.
        std::swap(accesses_[0], accesses_[1]);
        record(clean, cleanPipeline, declarations, *firstTarget);
        assert(native->stats()->nativeBindlessFaultOutcomePreparationCount == (mode == 1u ? 1u : 2u)
            && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 1u);
        assert(native->bindlessFaultReport(faultDomain_->generation()).status
            == BindlessFaultValidationStatus::Pending);
        assert(!native->prepareGraphicsPassScope(clean, faultDomain_, declarations, diagnostic)
            && diagnostic == "RHI-METAL-BINDLESS-FAULT-GENERATION-DUPLICATE");
        if (mode < 2u) {
            assert(!native->prepareGraphicsPassScope(
                       fault, peer.faultDomain_, peerDeclarations, diagnostic)
                && diagnostic == "RHI-METAL-GRAPHICS-SCOPE-COMMAND-BUDGET");
            assert(native->bindlessFaultReport(faultDomain_->generation()).status
                == BindlessFaultValidationStatus::Pending);
            {
                auto lease = BindlessFaultSubmissionLease::acquire(peer.faultDomain_, diagnostic);
                assert(lease.valid());
            }
            auto commands = encoder->finish();
            assert(commands);
            commands.reset();
            encoder.reset();
            assert(native->bindlessFaultReport(faultDomain_->generation()).status
                == BindlessFaultValidationStatus::Abandoned);
            auto lease = BindlessFaultSubmissionLease::acquire(faultDomain_, diagnostic);
            assert(lease.valid());
            assert(faultDomain_->faultLatchPriorState() == ResourceState::Undefined
                && peer.faultDomain_->faultLatchPriorState() == ResourceState::Undefined);
        } else {
            assert(native->prepareGraphicsPassScope(
                fault, peer.faultDomain_, peerDeclarations, diagnostic));
            record(fault, faultPipeline, peerDeclarations, *secondTarget);
            assert(native->stats()->nativeBindlessFaultOutcomePreparationCount == 2u
                && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 2u);
            const auto firstGeneration = faultDomain_->generation(),
                       secondGeneration = peer.faultDomain_->generation();
            assert(native->bindlessFaultReport(secondGeneration).status
                == BindlessFaultValidationStatus::Pending);
            auto commands = encoder->finish();
            assert(commands);
            std::weak_ptr<const MetalRhiNativePreparedGraphicsBindings> firstOwner = clean,
                                                                        secondOwner = fault;
            std::weak_ptr<BindlessOwnedFaultDomain> firstDomain = faultDomain_,
                                                    secondDomain = peer.faultDomain_;
            clean.reset();
            fault.reset();
            cleanPipeline.reset();
            faultPipeline.reset();
            descriptors_.reset();
            peer.descriptors_.reset();
            resources_.clear();
            peer.resources_.clear();
            metadata_.reset();
            peer.metadata_.reset();
            faultDomain_.reset();
            peer.faultDomain_.reset();
            firstTarget.reset();
            secondTarget.reset();
            auto timeline = device_->createTimelineSemaphore(0u);
            auto fence = device_->createFence();
            assert(timeline && fence);
            const auto submitAllocations = core::defaultCpuAllocator().stats().allocationCount;
            check(device_->queue().submit(*commands, timeline.get(), 1u, nullptr, 0u, fence.get()));
            assert(core::defaultCpuAllocator().stats().allocationCount == submitAllocations + 1u);
            commands.reset();
            encoder.reset();
            assert(!firstOwner.expired() && !secondOwner.expired() && !firstDomain.expired()
                && !secondDomain.expired());
            timeline->signal(1u);
            fence->wait();
            assert(firstOwner.expired() && secondOwner.expired() && firstDomain.expired()
                && secondDomain.expired());
            const auto first = native->bindlessFaultReport(firstGeneration),
                       second = native->bindlessFaultReport(secondGeneration);
            assert(first.status == BindlessFaultValidationStatus::Clean && first.faultBits == 0u);
            assert(
                second.status == BindlessFaultValidationStatus::Faulted && second.faultBits == 1u);
        }
        std::printf("multipleScopeBudgetMode: %u passed\n", mode);
    }

private:
    core::UniquePtr<RenderPipeline> retainedPipeline(std::string_view fragment)
    {
        RenderPipelineDesc state;
        state.label = "OwnedRetainedGraphicsFixture";
        state.dynamicViewportScissor = true;
        if (fragment.empty()) {
            state.colorFormat = TextureFormat::Unknown;
            state.depthStencil.format = TextureFormat::D32Float;
            state.depthStencil.depthTestEnabled = true;
            state.depthStencil.depthWriteEnabled = true;
        } else
            state.colorFormat = TextureFormat::Rgba8Unorm;
        core::pmr::String diagnostic;
        auto result = createMetalRhiRetainedGraphicsPipeline(packageOwner_,
            state,
            "vertexMain",
            fragment,
            semantics::nativeBackendCapabilityContract(Backend::Metal),
            diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(result && result->layout() && result->validationReport().success
            && !result->bindlessAuthorization()
            && result->bindlessShaderStages() == ShaderStage::None);
        const auto entries = metalRhiSelectedGraphicsEntriesFor(*result);
        assert(entries.vertex == "vertexMain" && entries.fragment == fragment);
        return result;
    }

    std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> prepare(
        const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline, std::string_view fragment)
    {
        std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2> layouts;
        for (unsigned i = 0u; i < (fragment.empty() ? 1u : 2u); ++i) {
            auto layout = core::makeShared<MetalRhiPreparedGraphicsStageLayout>();
            check(lowerMetalRhiPreparedGraphicsStage(package_,
                i ? fragment : "vertexMain",
                i ? ShaderStage::Fragment : ShaderStage::Vertex,
                *layout));
            layouts[i] = std::move(layout);
        }
        core::pmr::String diagnostic;
        auto result = prepareMetalRhiNativeGraphicsBindings(pipeline,
            descriptors_,
            package_.artifact().set,
            layouts,
            {1u, 65536u, 65536u, 8u, 65536u},
            diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(result);
        return result;
    }

    core::UniquePtr<GpuBuffer> buffer(const char* label,
        std::uint64_t bytes,
        ResourceUsage usage,
        ResourceState state,
        const void* data)
    {
        auto result = device_->createBuffer({label, bytes, usage, MemoryUsage::CpuToGpu, state});
        assert(result);
        check(device_->writeBuffer(*result, data, bytes));
        return result;
    }

    void append(core::pmr::Vector<MetalRhiNativeDescriptorBindingInput>& inputs,
        const DescriptorBindingDesc& physical,
        unsigned element,
        const GpuBuffer& resource)
    {
        MetalRhiNativeDescriptorBindingInput input;
        input.binding = physical;
        input.arrayElement = element;
        input.write.binding = physical.binding;
        input.write.arrayElement = element;
        input.write.type = physical.type;
        input.write.resourceKind = DescriptorResourceKind::Buffer;
        input.write.resource = resource.id();
        input.buffer = metalRhiNativePayloadFor(resource);
        inputs.push_back(std::move(input));
        BindlessResourceAccess access;
        access.resource = resource.id();
        access.kind = BindlessAccessKind::Buffer;
        access.bufferSize = resource.desc().sizeBytes;
        access.stages = physical.stages;
        access.pipelineStages = bindlessPipelineStagesForShader(physical.stages);
        const bool writable = physical.type == DescriptorType::StorageBuffer;
        access.state = writable ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
        access.access = writable ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
                                 : ResourceAccess::ShaderRead;
        accesses_.push_back(access);
    }

    std::shared_ptr<const BindlessShaderPackage> packageOwner_;
    const BindlessShaderPackage& package_;
    std::shared_ptr<GpuDevice> device_;
    core::UniquePtr<GpuBuffer> metadata_;
    std::shared_ptr<BindlessOwnedFaultDomain> faultDomain_;
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> descriptors_;
    core::pmr::Vector<core::UniquePtr<GpuBuffer>> resources_;
    core::pmr::Vector<BindlessResourceAccess> accesses_;
    std::array<GpuBuffer*, 2> counters_{};
    const std::array<std::array<float, 4>, 2> colors_{{{1, 0, 0, 1}, {0, 1, 0, 1}}};
};
} // namespace

static void validateRuntimeMemberRefusals(const std::filesystem::path& publication)
{
    using Proof = test::BindlessGraphicsRuntimeNativeMemoryPackageFixture;
    const auto quota = core::makeShared<core::CpuMemoryQuota>(32u * 1024u * 1024u);
    for (unsigned mode = 0u; mode != 3u; ++mode) {
        {
            core::CpuMemoryQuotaScope scope(quota);
            Proof proof(publication.parent_path());
            for (auto& binding : proof.bindings[0])
                if (binding.set == 0u && binding.binding == 3u) {
                    if (!mode)
                        binding.targetTypes.maxNodes = 1u;
                    else if (mode == 1u)
                        binding.relation.maxPairs = 1u;
                    else
                        binding.relation.rootForm = Proof::Lowering::RootForm::Block;
                }
            BindlessPackageLoadBudget budget{1024u * 1024u, 4u * 1024u * 1024u, 32u, 3u, 8u, 3u};
            budget.ordinaryMemory = &proof.request;
            core::pmr::String diagnostic;
            auto package = BindlessShaderPackage::load(publication, budget, diagnostic);
            if (mode == 1u) {
                // Source validation consumes the same pair budget before any
                // selected driver graph or pipeline can be published.
                assert(!package && diagnostic.find("TYPE-PAIR-BUDGET") != diagnostic.npos);
            } else {
                if (!package)
                    std::fprintf(stderr, "Runtime refusal package: %s\n", diagnostic.c_str());
                assert(package);
                const auto baseline = quota->liveBytes();
                RenderPipelineDesc state;
                state.dynamicViewportScissor = true;
                state.colorFormat = TextureFormat::Rgba8Unorm;
                const auto rejected = createMetalRhiRetainedGraphicsPipeline(package,
                    state,
                    "vertexMain",
                    "cleanFragment",
                    semantics::nativeBackendCapabilityContract(Backend::Metal),
                    diagnostic);
                const char* expected = !mode ? "TYPE-NODE-BUDGET"
                    : mode == 1u             ? "TYPE-PAIR-BUDGET"
                                             : "MEMORY-ARGUMENT";
                if (rejected || diagnostic.find(expected) == diagnostic.npos)
                    std::fprintf(
                        stderr, "Metal runtime expected %s got %s\n", expected, diagnostic.c_str());
                assert(!rejected && diagnostic.find(expected) != diagnostic.npos);
                core::pmr::String{}.swap(diagnostic);
                assert(quota->liveBytes() == baseline);
            }
        }
        assert(!quota->liveBytes());
    }
    std::puts(
        "BINDLESS_METAL_RUNTIME_MEMBER2_REFUSALS_OK driver-budget pair-budget explicit-form no-publication retirement0");
}

int main(int argc, char** argv)
{
    core::pmr::String diagnostic;
    const char* publication = ARP_METAL_GRAPHICS_PUBLICATION;
    if (argc >= 3 && std::strcmp(argv[1], "--publication") == 0) {
        publication = argv[2];
        argc -= 2;
        argv += 2;
    }
    const bool runtimeMembers =
        argc == 2 && std::strcmp(argv[1], "--original-runtime-members") == 0;
    const bool captureRuntime = argc == 3 && std::strcmp(argv[1], "--capture-runtime-members") == 0;
    if (runtimeMembers)
        validateRuntimeMemberRefusals(publication);
    std::shared_ptr<const BindlessShaderPackage> package;
    if (runtimeMembers || captureRuntime) {
        test::BindlessGraphicsRuntimeNativeMemoryPackageFixture memory(
            std::filesystem::path(publication).parent_path());
        BindlessPackageLoadBudget budget{1024u * 1024u, 4u * 1024u * 1024u, 32u, 3u, 8u, 3u};
        budget.ordinaryMemory = &memory.request;
        package = BindlessShaderPackage::load(publication, budget, diagnostic);
    } else
        package = BindlessShaderPackage::load(
            publication, {1024u * 1024u, 4u * 1024u * 1024u, 32u, 3u, 8u, 3u}, diagnostic);
    if (!package)
        std::fprintf(stderr, "Metal original package: %s\n", diagnostic.c_str());
    assert(package);
    const bool ordinaryDepthExecution =
        (argc == 4 || argc == 5) && std::strcmp(argv[1], "--ordinary-depth-execution") == 0;
    const bool ordinaryDepth = ordinaryDepthExecution
        || (argc == 4 && std::strcmp(argv[1], "--ordinary-depth-closure") == 0);
    test::MetalOrdinaryDepthClosureFixture::executionCheck = ordinaryDepthExecution
        ? &test::MetalOrdinaryDepthGraphicsExecutionFixture::execute
        : nullptr;
    if ((argc == 3 || argc == 4 || ordinaryDepthExecution)
        && (std::strcmp(argv[1], "--public-depth") == 0 || ordinaryDepth
            || std::strcmp(argv[1], "--public-mixed-content-plan") == 0
            || std::strcmp(argv[1], "--public-mixed-execution") == 0
            || std::strcmp(argv[1], "--public-mixed-lifecycle") == 0)) {
        const bool mixedLifecycle = std::strcmp(argv[1], "--public-mixed-lifecycle") == 0;
        const bool mixedExecution =
            mixedLifecycle || std::strcmp(argv[1], "--public-mixed-execution") == 0;
        const bool mixedContent =
            mixedExecution || std::strcmp(argv[1], "--public-mixed-content-plan") == 0;
        test::OriginalMixedDepthStoragePlanFixture::executionCheck =
            mixedExecution ? &test::MetalOriginalStorageExecutionFixture::executeMixed : nullptr;
        test::MetalOriginalStorageExecutionFixture::mixedDepthValues = {
            .01125f, .01375f, .01125f, .01375f};
        const bool captureDepth =
            (argc == 4 && !ordinaryDepth) || (ordinaryDepthExecution && argc == 5);
        const auto* capturePath = ordinaryDepthExecution && argc == 5 ? argv[4] : argv[3];
        test::MetalDepthRootBackendFixture::captureSubmittedOnly = captureDepth;
        test::MetalOrdinaryDepthGraphicsBackendFixture::captureSubmittedOnly = captureDepth;
        auto depth = BindlessShaderPackage::load(
            argv[2], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
        assert(depth);
        std::shared_ptr<const BindlessShaderPackage> ordinaryPackage;
        if (ordinaryDepth) {
            ordinaryPackage = BindlessShaderPackage::load(argv[3],
                {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                diagnostic);
            assert(ordinaryPackage);
        }
        if (captureDepth) {
            FrameCaptureRequest request;
            request.tool = FrameCaptureTool::NativeGpuTrace;
            request.outputPath = capturePath;
            const auto begun = beginMetalRhiFrameCapture(request);
            if (!begun.success()) {
                std::fprintf(stderr, "%s\n", begun.diagnostics.c_str());
                return 1;
            }
        }
        for (unsigned terminal = 0u; terminal
            < (captureDepth || ordinaryDepth || (mixedContent && !mixedLifecycle) ? 1u : 5u);
            ++terminal)
            for (const bool fault : {false, true}) {
                if (mixedContent && !mixedExecution && fault)
                    continue;
                if (ordinaryDepth && fault)
                    continue; // Observer verifies both ordinary entries on the same producer.
                if (terminal && fault && !mixedLifecycle)
                    continue;
                test::MetalOriginalStorageExecutionFixture::terminal =
                    mixedLifecycle ? terminal : 0u;
                test::MetalOriginalStorageExecutionFixture::fault = fault;
                auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
                desc.memoryBudgetBytes = 16u * 1024u * 1024u;
                desc.enableBindlessCompute = true;
                desc.enableBindlessComputeImages = true;
                desc.enableBindlessGraphicsImages = ordinaryDepth;
                test::MetalDepthProducerDataFixture data;
                data.mixedContent = mixedContent;
                data.ordinaryDepth = ordinaryDepth;
                data.retireDevice = terminal == 2u || terminal == 4u;
                test::validateOriginalBindlessRootSource(package,
                    createMetalRhiDevice(desc),
                    "cleanMain",
                    false,
                    test::MetalOriginalCompletedDepthFixture{depth,
                        mixedExecution ? false : fault,
                        mixedLifecycle ? 0u : terminal,
                        mixedContent,
                        ordinaryPackage},
                    data,
                    true,
                    {},
                    "vertexMain",
                    true);
            }
        if (captureDepth) {
            const auto ended = endMetalRhiFrameCapture();
            if (!ended.success()) {
                std::fprintf(stderr, "%s\n", ended.diagnostics.c_str());
                return 1;
            }
            std::printf("METAL_PUBLIC_DEPTH_EXECUTION_CAPTURE_COMPLETED %s\n", capturePath);
        }
        test::OriginalMixedDepthStoragePlanFixture::executionCheck = nullptr;
        test::MetalOrdinaryDepthClosureFixture::executionCheck = nullptr;
        test::MetalOriginalStorageExecutionFixture::terminal = 0u;
        return 0;
    }
    const bool sampled = argc == 2 && std::strcmp(argv[1], "--original-sampled-graphics") == 0;
    const bool captureSampled =
        argc == 3 && std::strcmp(argv[1], "--capture-original-sampled-graphics") == 0;
    if (sampled || captureSampled) {
        {
            auto base = createMetalRhiDevice(test::MetalPublicGraphicsFixture::deviceDesc());
            assert(base);
            auto* extension =
                static_cast<BindlessCrossExtension*>(base->queryInterface(kExtensionBindlessCross));
            assert(extension);
            const auto request = test::BindlessPackageRequestFixture::make(
                package->artifact(), extension->capabilities().deviceIdentity);
            const auto refused = extension->plan(request, package->artifact());
            assert(!refused.success && !refused.executable
                && !extension->capabilities().support[1u][0u].nonUniform);
        }
        const bool array = std::any_of(package->artifact().physicalLayout.bindings.begin(),
            package->artifact().physicalLayout.bindings.end(),
            [](const auto& b) {
                return b.type == DescriptorType::SampledTexture
                    && b.textureViewDimension == TextureViewDimension::Texture2DArray;
            });
        if (captureSampled) {
            FrameCaptureRequest capture;
            capture.tool = FrameCaptureTool::NativeGpuTrace;
            capture.outputPath = argv[2];
            const auto begun = beginMetalRhiFrameCapture(capture);
            if (!begun.success()) {
                std::fprintf(stderr, "%s\n", begun.diagnostics.c_str());
                return 1;
            }
        }
        for (const bool cancel : {true, false})
            for (const bool fault : {false, true})
                for (const bool drop : {false, true})
                    for (const bool retire : {false, true}) {
                        if ((cancel && (drop || retire))
                            || (captureSampled && (cancel || drop || retire)))
                            continue;
                        auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
                        desc.memoryBudgetBytes = 16u * 1024u * 1024u;
                        desc.enableBindlessGraphicsImages = true;
                        test::MetalCompletedSampledGraphicsFixture data;
                        data.arrayTextures = array;
                        data.retireDevice = retire;
                        test::MetalOriginalBindlessGraphicsContentFixture observer{
                            true, cancel, fault ? 1u : 0u, {}, drop, retire};
                        observer.arraySampling = array;
                        observer.verifyOutputs = array
                            ? test::verifyMetalCompletedSampledGraphicsArray
                            : test::verifyMetalCompletedSampledGraphics2D;
                        test::validateOriginalBindlessRootSource(package,
                            createMetalRhiDevice(desc),
                            "cleanMain",
                            false,
                            observer,
                            data,
                            true,
                            fault       ? "faultFragment"
                                : array ? "cleanFragment"
                                        : "fragmentMain");
                        std::printf(
                            "BINDLESS_METAL_SAMPLED_GRAPHICS_OK array=%d fault=%d cancel=%d drop=%d retire=%d\n",
                            array,
                            fault,
                            cancel,
                            drop,
                            retire);
                    }
        if (captureSampled) {
            const auto ended = endMetalRhiFrameCapture();
            if (!ended.success()) {
                std::fprintf(stderr, "%s\n", ended.diagnostics.c_str());
                return 1;
            }
            std::printf("METAL_SAMPLED_GRAPHICS_CAPTURE_COMPLETED %s\n", argv[2]);
        }
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--native-result-loss") == 0) {
        test::MetalNativeResultLossFixture::verify(test::MetalPublicGraphicsFixture::deviceDesc());
        return 0;
    }
    const bool originalRoot = argc == 2 && std::strcmp(argv[1], "--original-root") == 0;
    const bool composedGraphics =
        runtimeMembers || (argc == 2 && std::strcmp(argv[1], "--original-graphics-domains") == 0);
    if (composedGraphics
        || (argc == 2 && std::strcmp(argv[1], "--original-graphics-execute") == 0)) {
        for (const auto format : {TextureFormat::Rgba8Unorm, TextureFormat::Rgba16Float})
            for (const auto cancel : {true, false})
                for (const auto faulted : {false, true})
                    for (const auto drop : {false, true})
                        for (const auto retire : {false, true}) {
                            if (cancel && (drop || retire))
                                continue;
                            if (composedGraphics)
                                test::validateOriginalBindlessRootSource(package,
                                    createMetalRhiDevice(
                                        test::MetalPublicGraphicsFixture::deviceDesc()),
                                    "cleanMain",
                                    false,
                                    test::MetalOriginalBindlessGraphicsContentFixture{
                                        true, cancel, faulted ? 1u : 0u, {}, drop, retire},
                                    test::MetalOriginalGraphicsDomainsBufferDataFixture{
                                        retire, runtimeMembers, faulted},
                                    true,
                                    faulted ? "faultFragment" : "cleanFragment",
                                    "vertexMain",
                                    false,
                                    test::OrdinaryRootMutation::None,
                                    format);
                            else
                                test::validateOriginalBindlessRootSource(package,
                                    createMetalRhiDevice(
                                        test::MetalPublicGraphicsFixture::deviceDesc()),
                                    "cleanMain",
                                    false,
                                    test::MetalOriginalBindlessGraphicsContentFixture{
                                        true, cancel, faulted ? 1u : 0u, {}, drop, retire},
                                    test::MetalOriginalGraphicsBufferDataFixture{retire},
                                    true,
                                    faulted ? "faultFragment" : "cleanFragment",
                                    "vertexMain",
                                    false,
                                    test::OrdinaryRootMutation::None,
                                    format);
                        }
        return 0;
    }
    const bool captureDomains = captureRuntime
        || (argc == 3 && std::strcmp(argv[1], "--capture-original-graphics-domains") == 0);
    if (captureDomains || (argc == 3 && std::strcmp(argv[1], "--capture-original-graphics") == 0)) {
        FrameCaptureRequest request;
        request.tool = FrameCaptureTool::NativeGpuTrace;
        request.outputPath = argv[2];
        const auto begun = beginMetalRhiFrameCapture(request);
        if (!begun.success()) {
            std::fprintf(stderr, "%s\n", begun.diagnostics.c_str());
            return 1;
        }
        for (const auto faulted : {false, true}) {
            if (captureDomains)
                test::validateOriginalBindlessRootSource(package,
                    createMetalRhiDevice(test::MetalPublicGraphicsFixture::deviceDesc()),
                    "cleanMain",
                    false,
                    test::MetalOriginalBindlessGraphicsContentFixture{
                        true, false, faulted ? 1u : 0u},
                    test::MetalOriginalGraphicsDomainsBufferDataFixture{
                        false, captureRuntime, faulted},
                    true,
                    faulted ? "faultFragment" : "cleanFragment");
            else
                test::validateOriginalBindlessRootSource(package,
                    createMetalRhiDevice(test::MetalPublicGraphicsFixture::deviceDesc()),
                    "cleanMain",
                    false,
                    test::MetalOriginalBindlessGraphicsContentFixture{
                        true, false, faulted ? 1u : 0u},
                    test::MetalOriginalGraphicsBufferDataFixture{},
                    true,
                    faulted ? "faultFragment" : "cleanFragment");
        }
        const auto ended = endMetalRhiFrameCapture();
        if (!ended.success()) {
            std::fprintf(stderr, "%s\n", ended.diagnostics.c_str());
            return 1;
        }
        std::printf("METAL_ORIGINAL_GRAPHICS_CAPTURE_COMPLETED %s\n", argv[2]);
        return 0;
    }
    if (argc == 2
        && (std::strcmp(argv[1], "--original-geometry-vertex") == 0
            || std::strcmp(argv[1], "--original-geometry-instance") == 0)) {
        for (const bool cancel : {true, false})
            for (const bool fault : {false, true})
                for (const bool drop : {false, true}) {
                    if (cancel && drop)
                        continue;
                    auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
                    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
                    desc.enableBindlessGraphicsImages = true;
                    test::MetalOriginalGeometryDataFixture data;
                    data.instanceRate = std::strcmp(argv[1], "--original-geometry-instance") == 0;
                    data.nativeStagingCeiling = desc.memoryBudgetBytes;
                    test::MetalOriginalBindlessGraphicsContentFixture observer;
                    observer.execute = true;
                    observer.cancel = cancel;
                    observer.dropView = drop;
                    observer.expectedFaultBits = fault ? 1u : 0u;
                    observer.geometry = data.initialized;
                    observer.verifyOutputs = test::verifyMetalInitializedGeometryGraphics;
                    test::validateOriginalBindlessRootSource(package,
                        createMetalRhiDevice(desc),
                        "cleanMain",
                        false,
                        observer,
                        data,
                        true,
                        fault ? "faultFragment" : "fragmentMain");
                    data.initialized->actors = {};
                    for (const auto& q : data.initialized->quotas)
                        assert(!q.metadata->liveBytes() && !q.payload->liveBytes()
                            && !q.resource->liveBytes() && !q.lease->liveBytes());
                    std::printf(
                        "BINDLESS_METAL_INITIALIZED_GEOMETRY instance=%d fault=%d cancel=%d drop=%d\n",
                        data.instanceRate,
                        fault,
                        cancel,
                        drop);
                }
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--original-graphics-content") == 0) {
        for (const auto entry : {"cleanFragment", "faultFragment"})
            test::validateOriginalBindlessRootSource(package,
                createMetalRhiDevice(test::MetalPublicGraphicsFixture::deviceDesc()),
                "cleanMain",
                false,
                test::MetalOriginalBindlessGraphicsContentFixture{},
                test::OriginalBindlessRootFixtureExecution{},
                true,
                entry);
        return 0;
    }
    if (originalRoot) {
        for (const auto entry : {"cleanFragment", "faultFragment"})
            test::validateOriginalBindlessRootSource(package,
                createMetalRhiDevice(test::MetalPublicGraphicsFixture::deviceDesc()),
                "cleanMain",
                false,
                test::MetalOriginalBindlessRootFixture{},
                test::OriginalBindlessRootFixtureExecution{},
                false,
                entry);
        return 0;
    }
    if (argc == 4 && std::strcmp(argv[1], "--capture-public") == 0)
        return test::MetalPublicGraphicsFixture(package).capture(argv[2], argv[3]);
    if (argc == 4 && std::strcmp(argv[1], "--capture-public-depth") == 0)
        return test::MetalPublicGraphicsFixture(package).capture(argv[2], argv[3], true);
    if (argc == 3 && std::strcmp(argv[1], "--capture") == 0)
        return GraphicsExecutionFixture(package).capture(argv[2]);
    const bool publicOnly = argc == 2 && std::strcmp(argv[1], "--public-only") == 0;
    if (argc != 1 && !publicOnly) {
        std::fputs(
            "usage: [--publication package.yaml] [--original-root | --public-only | --capture trace | --capture-public trace pixels | --capture-public-depth trace pixels]\n",
            stderr);
        return 2;
    }
    test::MetalPublicGraphicsFixture(package).run(false);
    test::MetalPublicGraphicsFixture(package).run(true);
    test::MetalPublicGraphicsFixture(package).run(false, true);
    test::MetalPublicGraphicsFixture(package).run(false, false, nullptr, true);
    test::MetalPublicGraphicsFixture(package, true).run(true, false, nullptr, true);
    if (publicOnly)
        return 0;
    GraphicsExecutionFixture(package).validateDomainCancellationAndIdentity();
    GraphicsExecutionFixture(package).validateRetainedPipelineFactory();
    GraphicsExecutionFixture(package).validateScopeRejectionAndCancellation();
    GraphicsExecutionFixture(package).validateAttachmentPreservation(false);
    GraphicsExecutionFixture(package).validateAttachmentPreservation(true);
    for (unsigned mode = 0u; mode < 3u; ++mode)
        GraphicsExecutionFixture(package).validateMultipleScopeBudgets(mode);
    GraphicsExecutionFixture(package).run("cleanFragment", false);
    GraphicsExecutionFixture(package).run("faultFragment", true);
    GraphicsExecutionFixture reused(package);
    reused.run("faultFragment", true, false);
    reused.run("cleanFragment", false, true, 1u);
}
