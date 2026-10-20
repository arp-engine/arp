#pragma once

#include "bindless_package_request_fixture.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/gpu_extensions.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {

// Exercises public device-issued owners and pass APIs. Native access below is
// observation/readback only; it never creates or records the tested binding.
class MetalPublicGraphicsFixture final {
public:
    explicit MetalPublicGraphicsFixture(
        std::shared_ptr<const rhi::BindlessShaderPackage> package, bool vertexFault = false)
        : package_(std::move(package)), vertexFault_(vertexFault)
    {
        using namespace rhi;
        using namespace rhi_metal;
        device_ = createMetalRhiDevice(deviceDesc());
        assert(device_);
        extension_ =
            static_cast<BindlessCrossExtension*>(device_->queryInterface(kExtensionBindlessCross));
        assert(extension_ && metalBindlessGraphicsExecutionAvailable(extension_->capabilities()));
        assert(extension_->capabilities().preparedComputeBindingContractVersion == 0u
            && extension_->capabilities().originalGraphicsExecutionContractVersion == 1u);
        auto request = BindlessPackageRequestFixture::make(
            package_->artifact(), extension_->capabilities().deviceIdentity);
        // The existing vertex entry reads indices 0 and 1. A single live input
        // makes index 1 invalid without changing the sealed shader or metadata.
        if (vertexFault_)
            for (auto& table : request.tables)
                if (table.binding.type == DescriptorType::ReadOnlyStorageBuffer)
                    table.resourceCount = 1u;
        snapshots_ = core::makeUnique<BindlessSnapshotPool>(*device_, request.budget);
        snapshot_ = snapshots_->prepareShared(request, package_->artifact(), diagnostic_);
        if (!snapshot_)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(snapshot_);
        for (const auto& table : package_->artifact().tables) {
            const auto* binding =
                findDescriptorSetBinding(package_->artifact().physicalLayout, table.firstBinding);
            assert(binding && table.capacity == 2u);
            for (unsigned i = 0u; i < snapshot_->activeCount(table.tableId); ++i) {
                const bool uniform = binding->type == DescriptorType::UniformBuffer;
                const bool writable = binding->type == DescriptorType::StorageBuffer;
                const std::uint32_t value = (writable ? 100u : 10u) * (i + 1u);
                const std::array<float, 4> color =
                    i == 0u ? std::array<float, 4>{1, 0, 0, 1} : std::array<float, 4>{0, 1, 0, 1};
                assert(table.bufferLayout.minBindingSizeBytes
                    == (uniform ? sizeof(color) : sizeof(value)));
                auto buffer = device_->createBuffer({"PublicGraphicsInput",
                    table.bufferLayout.minBindingSizeBytes,
                    uniform ? ResourceUsage::UniformBuffer
                            : ResourceUsage::StorageBuffer
                            | (writable ? ResourceUsage::CopySrc : ResourceUsage::None),
                    MemoryUsage::CpuToGpu,
                    writable ? ResourceState::ShaderWrite : ResourceState::ShaderRead});
                assert(buffer);
                check(device_->writeBuffer(*buffer,
                    uniform ? static_cast<const void*>(color.data()) : &value,
                    buffer->desc().sizeBytes));
                check(snapshot_->writeBuffer(snapshot_->handle(table.tableId, i), *buffer));
                BindlessResourceAccess access;
                access.resource = buffer->id();
                access.kind = BindlessAccessKind::Buffer;
                access.bufferSize = buffer->desc().sizeBytes;
                access.stages = binding->stages;
                access.pipelineStages = bindlessPipelineStagesForShader(binding->stages);
                access.state = writable ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
                access.access = ResourceAccess::ShaderRead
                    | (writable ? ResourceAccess::ShaderWrite : ResourceAccess::None);
                accesses_.push_back(access);
                if (writable)
                    counters_[i] = buffer.get();
                buffers_.push_back(std::move(buffer));
            }
            if (snapshot_->activeCount(table.tableId) < table.capacity)
                check(snapshot_->initializeInactiveSlots(
                    snapshot_->handle(table.tableId, 0u), *device_));
        }
        check(snapshot_->seal(*device_));
        accesses_.push_back(snapshot_->metadataAccess());
        accesses_.push_back(snapshot_->faultLatchAccess());
        accesses_.push_back(snapshot_->faultReadbackAccess());
        authorization_ = extension_->authorize(package_, *snapshot_, diagnostic_);
        if (!authorization_)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(authorization_);
        assert(!extension_->createComputePipeline(authorization_, "cleanMain", diagnostic_)
            && diagnostic_ == "RHI-METAL-BINDLESS-EXECUTION-UNAVAILABLE");
    }

    void run(bool faulted,
        bool abandon = false,
        const char* pixelsPath = nullptr,
        bool depthOnly = false)
    {
        using namespace rhi;
        using namespace rhi_metal;
        assert(!vertexFault_ || (depthOnly && faulted));
        assert(!depthOnly || faulted == vertexFault_);
        RenderPipelineDesc state;
        state.dynamicViewportScissor = true;
        if (depthOnly) {
            state.colorFormat = TextureFormat::Unknown;
            state.depthStencil.format = TextureFormat::D32Float;
            state.depthStencil.depthTestEnabled = true;
            state.depthStencil.depthWriteEnabled = true;
        } else
            state.colorFormat = TextureFormat::Rgba8Unorm;
        auto pipeline = core::shareUnique(extension_->createRenderPipeline(authorization_,
            state,
            "vertexMain",
            depthOnly     ? ""
                : faulted ? "faultFragment"
                          : "cleanFragment",
            diagnostic_));
        if (!pipeline)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(pipeline && pipeline->bindlessAuthorization() == authorization_);
        assert(pipeline->bindlessShaderStages()
            == (depthOnly ? ShaderStage::Vertex : ShaderStage::Vertex | ShaderStage::Fragment));
        const auto retained = snapshot_->plan().snapshotBytes + package_->sourceBytes();
        BindlessGraphicsBindingBudget budget{1u, retained + 65536u, retained + 65536u, 6u, 65536u};
        auto pool = core::makeUnique<BindlessGraphicsBindingPool>(*extension_, budget);
        BindlessGraphicsBindingDesc desc{
            true, snapshot_, pipeline, snapshot_->plan(), snapshot_->generation()};
        auto prepared = pool->prepare(desc, diagnostic_);
        if (!prepared)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(prepared && prepared->owners()->authorization() == authorization_);
        assert(!pool->prepare(desc, diagnostic_) && pool->liveBindings() == 1u);
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            auto invalid = desc;
            if (mode == 0u)
                ++invalid.generation;
            if (mode == 1u)
                ++invalid.plan.identity;
            if (mode == 2u)
                invalid.optIn = false;
            assert(!pool->prepare(invalid, diagnostic_) && pool->liveBindings() == 1u
                && pool->chargedBytes() == budget.maxBindingBytes);
        }
        BindlessAccessArray declarations(accesses_.data(), accesses_.size());
        validateGates(prepared, declarations, budget);
        constexpr std::uint32_t width = 2u, height = 2u;
        const auto format = depthOnly ? TextureFormat::D32Float : TextureFormat::Rgba8Unorm;
        const auto initial =
            depthOnly ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        const auto row = core::alignCpuAllocationOffsetUp(
            width * bytesPerPixel(format), kTextureUploadRowPitchAlignment);
        assert(row.success && row.alignedValue <= UINT32_MAX);
        const auto rowBytes = static_cast<std::uint32_t>(row.alignedValue),
                   pixelBytes = rowBytes * height;
        auto target = device_->createTexture({"PublicGraphicsTarget",
            {width, height, 1u},
            format,
            TextureUsage::RenderAttachment | TextureUsage::CopySrc,
            MemoryUsage::GpuOnly,
            initial});
        auto readback = device_->createBuffer({"PublicGraphicsReadback",
            pixelBytes + sizeof(std::uint32_t) * 2u,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        assert(target && readback);
        auto encoder = device_->createCommandEncoder({"PublicGraphicsExecution",
            1u,
            1u,
            0u,
            0u,
            static_cast<std::uint32_t>(accesses_.size())});
        assert(encoder);
        check(encoder->prepareBindlessGraphicsPass(prepared, declarations));
        RenderPassDesc passDesc;
        if (depthOnly) {
            passDesc.hasDepthStencilAttachment = true;
            passDesc.depthStencilAttachment.target = target->id();
            passDesc.depthStencilAttachment.format = format;
            passDesc.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
            passDesc.depthStencilAttachment.depthStoreOp = StoreOp::Store;
            passDesc.depthStencilAttachment.depthClearValue = 1.0f;
        } else {
            RenderPassColorAttachmentDesc color;
            color.target = target->id();
            color.format = format;
            color.loadOp = LoadOp::Clear;
            color.storeOp = StoreOp::Store;
            color.clearColor = {0, 0, 1, 1};
            passDesc.colorAttachments.push_back(color);
        }
        auto pass = encoder->beginRenderPass(passDesc);
        assert(pass);
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        assert(indexed);
        check(pass->setPipeline(*pipeline));
        check(pass->setViewport({0, 0, float(width), float(height), 0, 1}));
        check(pass->setScissor({0, 0, width, height}));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 32u; ++i)
            check(indexed->bindPreparedGraphicsBinding(prepared, declarations));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        check(pass->draw(3u, 1u, 0u, 0u));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        check(pass->end());
        TextureSubresourceRange range;
        range.aspect = depthOnly ? TextureAspect::Depth : TextureAspect::Color;
        check(encoder->transition(*target, range, initial, ResourceState::CopySrc));
        for (auto* counter : counters_)
            check(
                encoder->transition(*counter, ResourceState::ShaderWrite, ResourceState::CopySrc));
        auto copy = encoder->beginTransferPass({"PublicGraphicsReadback"});
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
        auto commands = encoder->finish();
        assert(commands);
        auto observedCommand = metalRhiNativePayloadFor(*commands);
        assert(observedCommand);
        std::weak_ptr<const BindlessPreparedGraphicsBinding> observed = prepared;
        std::weak_ptr<const MetalRhiNativePreparedGraphicsBindings> nativeObserved =
            metalRhiPreparedGraphicsPayloadFor(*prepared);
        std::weak_ptr<BindlessSnapshot> snapshotObserved = snapshot_;
        const auto generation = snapshot_->generation();
        prepared.reset();
        desc = {};
        pipeline.reset();
        pass.reset();
        encoder.reset();
        assert(!observed.expired() && pool->liveBindings() == 1u);
        if (abandon) {
            commands.reset();
            assert(observed.expired() && nativeObserved.expired() && pool->liveBindings() == 0u
                && pool->chargedBytes() == 0u);
            assert(observedCommand->bindlessFaultReport(generation).status
                == BindlessFaultValidationStatus::Abandoned);
            std::puts("publicGraphicsCancellation: typed/native owners retired; pool budget0");
            return;
        }
        auto timeline = device_->createTimelineSemaphore(0u);
        auto fence = device_->createFence();
        assert(timeline && fence);
        check(device_->queue().submit(*commands, timeline.get(), 1u, nullptr, 0u, fence.get()));
        commands.reset();
        snapshot_.reset();
        snapshots_.reset();
        authorization_.reset();
        package_.reset();
        assert(!fence->isSignaled() && !observed.expired() && !snapshotObserved.expired());
        assert(pool->liveBindings() == 1u && pool->chargedBytes() == budget.maxBindingBytes);
        assert(observedCommand->bindlessFaultReport(generation).status
            == BindlessFaultValidationStatus::Pending);
        timeline->signal(1u);
        fence->wait();
        assert(observed.expired() && nativeObserved.expired() && snapshotObserved.expired());
        assert(pool->liveBindings() == 0u && pool->chargedBytes() == 0u);
        const auto fault = observedCommand->bindlessFaultReport(generation);
        assert(fault.status
                == (faulted ? BindlessFaultValidationStatus::Faulted
                            : BindlessFaultValidationStatus::Clean)
            && fault.faultBits == (faulted ? 1u : 0u) && fault.validate().success == !faulted);
        core::pmr::Vector<std::uint8_t> pixels(pixelBytes);
        std::array<std::uint32_t, 2u> words{};
        const auto nativeReadback = metalRhiNativePayloadFor(*readback);
        assert(readMetalRhiNativeBuffer(nativeReadback,
            pixels.data(),
            pixels.size(),
            0u,
            readback->desc().sizeBytes,
            diagnostic_));
        assert(readMetalRhiNativeBuffer(nativeReadback,
            words.data(),
            sizeof(words),
            pixelBytes,
            readback->desc().sizeBytes,
            diagnostic_));
        assert((words
            == (depthOnly ? std::array<std::uint32_t, 2u>{100u, 200u}
                          : std::array<std::uint32_t, 2u>{102u, 202u})));
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x) {
                if (depthOnly) {
                    float value = 1.0f;
                    std::memcpy(
                        &value, pixels.data() + y * rowBytes + x * sizeof(value), sizeof(value));
                    assert(value == 0.0f);
                    continue;
                }
                const std::array<std::uint8_t, 4u> expected = x == 0u
                    ? std::array<std::uint8_t, 4u>{255, 0, 0, 255}
                    : std::array<std::uint8_t, 4u>{0, 255, 0, 255};
                for (unsigned c = 0u; c < expected.size(); ++c)
                    assert(pixels[y * rowBytes + x * 4u + c] == expected[c]);
            }
        if (pixelsPath) {
            auto* file = std::fopen(pixelsPath, "wb");
            assert(file);
            assert(std::fprintf(
                       file, depthOnly ? "P5\n%u %u\n255\n" : "P6\n%u %u\n255\n", width, height)
                > 0);
            for (unsigned y = 0u; y < height; ++y)
                for (unsigned x = 0u; x < width; ++x)
                    assert(std::fwrite(
                               pixels.data() + y * rowBytes + x * 4u, 1u, depthOnly ? 1u : 3u, file)
                        == (depthOnly ? 1u : 3u));
            assert(std::fclose(file) == 0);
        }
        std::printf(depthOnly
                ? "publicGraphicsDepth: counters100/200, exact2x2 D32 zero, fault%u, typed/native completion retention, pool budget0, hot allocation0\n"
                : "publicGraphics: counters102/202, exact2x2 RGBA, fault%u, typed/native completion retention, pool budget0, hot allocation0\n",
            fault.faultBits);
    }

    int capture(const char* path, const char* pixelsPath, bool depthOnly = false)
    {
        using namespace rhi;
        FrameCaptureRequest request;
        request.tool = FrameCaptureTool::NativeGpuTrace;
        request.outputPath = path;
        const auto started = device_->beginFrameCapture(request);
        if (!started.success()) {
            std::fprintf(stderr, "%s\n", started.diagnostics.c_str());
            return 1;
        }
        run(false, false, pixelsPath, depthOnly);
        const auto completed = device_->endFrameCapture();
        if (!completed.success()) {
            std::fprintf(stderr, "%s\n", completed.diagnostics.c_str());
            return 1;
        }
        std::printf("METAL_PUBLIC_GRAPHICS_CAPTURE_COMPLETED %s\n", path);
        return 0;
    }

    static rhi_metal::MetalRhiDeviceDesc deviceDesc()
    {
        rhi_metal::MetalRhiDeviceDesc desc;
        desc.capabilityContract =
            rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        desc.memoryBudgetBytes = 1024u * 1024u;
        desc.enableBindlessGraphics = true;
        return desc;
    }

private:
    template <class Result> static void check(const Result& result)
    {
        if (!result.success)
            std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
        assert(result.success);
    }

    void validateGates(const std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>& binding,
        const rhi::BindlessAccessDeclarations& accesses,
        const rhi::BindlessGraphicsBindingBudget& budget)
    {
        using namespace rhi;
        using namespace rhi_metal;
        RenderPipelineDesc state;
        state.colorFormat = TextureFormat::Rgba8Unorm;
        state.dynamicViewportScissor = true;
        auto foreign = createMetalRhiDevice(deviceDesc());
        assert(foreign);
        auto* other =
            static_cast<BindlessCrossExtension*>(foreign->queryInterface(kExtensionBindlessCross));
        assert(other);
        assert(!other->authorize(package_, *snapshot_, diagnostic_));
        assert(!other->createRenderPipeline(
            authorization_, state, "vertexMain", "cleanFragment", diagnostic_));
        BindlessGraphicsBindingPool otherPool(*other, budget);
        assert(!otherPool.prepare(binding->owners()->desc(), diagnostic_)
            && otherPool.liveBindings() == 0u && otherPool.chargedBytes() == 0u);
        auto encoder = foreign->createCommandEncoder({"PublicGraphicsForeign",
            1u,
            1u,
            0u,
            0u,
            static_cast<std::uint32_t>(accesses_.size())});
        assert(encoder);
        assert(!encoder->prepareBindlessGraphicsPass(binding, accesses).success);
        assert(!metalRhiNativePayloadFor(*encoder)->stats()->nativeBlitEncoderCreated);
        auto malformed = device_->createCommandEncoder(
            {"PublicGraphicsAccess", 1u, 1u, 0u, 0u, static_cast<std::uint32_t>(accesses_.size())});
        assert(malformed);
        BindlessAccessArray missing(accesses_.data(), accesses_.size() - 1u);
        assert(!malformed->prepareBindlessGraphicsPass(binding, missing).success);
        auto alias =
            std::shared_ptr<const BindlessPreparedGraphicsBinding>(snapshot_, binding.get());
        assert(!malformed->prepareBindlessGraphicsPass(alias, accesses).success);
        assert(!metalRhiNativePayloadFor(*malformed)->stats()->nativeBlitEncoderCreated);
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            auto limited = device_->createCommandEncoder({"PublicGraphicsCapacity",
                mode == 0u ? 0u : 1u,
                mode == 1u ? 0u : 1u,
                0u,
                0u,
                mode == 2u ? 0u : static_cast<std::uint32_t>(accesses_.size())});
            assert(limited);
            assert(!limited->prepareBindlessGraphicsPass(binding, accesses).success);
            assert(!metalRhiNativePayloadFor(*limited)->stats()->nativeBlitEncoderCreated);
        }
        auto cancelled = device_->createCommandEncoder({"PublicGraphicsPendingCancel",
            1u,
            1u,
            0u,
            0u,
            static_cast<std::uint32_t>(accesses_.size())});
        assert(cancelled);
        check(cancelled->prepareBindlessGraphicsPass(binding, accesses));
        auto native = metalRhiNativePayloadFor(*cancelled);
        assert(native);
        assert(!cancelled->finish());
        cancelled.reset();
        // No pass ended, so no readback outcome was scheduled. Cancellation
        // releases the exclusive lease without manufacturing a GPU result.
        assert(native->bindlessFaultReport(snapshot_->generation()).status
            == BindlessFaultValidationStatus::Unavailable);
        auto lease = BindlessFaultSubmissionLease::acquire(snapshot_, diagnostic_);
        assert(lease.valid());
    }

    std::shared_ptr<const rhi::BindlessShaderPackage> package_;
    core::UniquePtr<rhi::GpuDevice> device_;
    const rhi::BindlessCrossExtension* extension_ = nullptr;
    core::UniquePtr<rhi::BindlessSnapshotPool> snapshots_;
    std::shared_ptr<rhi::BindlessSnapshot> snapshot_;
    std::shared_ptr<const rhi::BindlessAuthorization> authorization_;
    core::pmr::Vector<core::UniquePtr<rhi::GpuBuffer>> buffers_;
    std::array<rhi::GpuBuffer*, 2u> counters_{};
    core::pmr::Vector<rhi::BindlessResourceAccess> accesses_;
    core::pmr::String diagnostic_;
    bool vertexFault_ = false;
};

} // namespace arp::test
