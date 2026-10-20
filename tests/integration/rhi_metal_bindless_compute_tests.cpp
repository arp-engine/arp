#include "../fixtures/bindless_metal_graphics_pipeline.h"
#include "../fixtures/metal_original_bindless_execution_fixture.h"
#include "../fixtures/metal_original_bindless_root_fixture.h"
#include "../fixtures/original_bindless_root_source_fixture.h"
#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/gpu_extensions.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>

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

class ForgedComputeBinding final : public BindlessPreparedComputeBinding {
public:
    explicit ForgedComputeBinding(const BindlessPreparedComputeBinding& original)
        : BindlessPreparedComputeBinding(original.owners())
    {}

    ValidationResult validatePayload() const override { return {true, {}}; }

    std::uint32_t argumentBufferCount() const noexcept override { return 0u; }

    std::uint64_t argumentBufferBytes() const noexcept override { return 0u; }
};

class ComputeFixture final {
public:
    static void capturePublicComposedRoot(
        std::shared_ptr<const BindlessShaderPackage> package, const char* path)
    {
        auto device = createMetalRhiDevice(deviceDesc());
        assert(device);
        FrameCaptureRequest request;
        request.outputPath = path;
        const auto started = device->beginFrameCapture(request);
        if (!started.success())
            std::fprintf(stderr, "%s\n", started.diagnostics.c_str());
        assert(started.success());
        validateComposedRoot(std::move(package), true, true);
        const auto completed = device->endFrameCapture();
        if (!completed.success())
            std::fprintf(stderr, "%s\n", completed.diagnostics.c_str());
        assert(completed.success());
        std::printf("BINDLESS_METAL_PUBLIC_COMPOSED_CAPTURE %s\n", path);
    }

    static void validateComposedRoot(std::shared_ptr<const BindlessShaderPackage> package,
        bool publicExecution = false,
        bool captureOnly = false)
    {
        check(validateMetalBindlessComputeArtifact(package->artifact()));
        arp::test::validateOriginalBindlessRootSource(package,
            createMetalRhiDevice(deviceDesc()),
            "computeMain",
            false,
            arp::test::MetalOriginalBindlessRootFixture{});
        for (const bool retire : {false, true})
            for (const bool submit : {false, true}) {
                if (retire && !submit)
                    continue;
                for (const bool drop : {false, true})
                    for (const bool fault : {false, true}) {
                        if (captureOnly && (retire || !submit || drop))
                            continue;
                        const auto run = [&](auto execute) {
                            execute.submit = submit;
                            execute.dropCommand = drop;
                            execute.retireDevice = retire;
                            execute.expectedFault = fault;
                            execute.indexedFault = fault;
                            execute.expectedCounters = {49u, fault ? 777u : 49u};
                            execute.publicExecution = publicExecution;
                            arp::test::validateOriginalBindlessRootSource(package,
                                createMetalRhiDevice(deviceDesc()),
                                "computeMain",
                                true,
                                arp::test::OriginalBindlessRootFixtureObserver{},
                                execute);
                        };
                        if (publicExecution)
                            run(arp::test::MetalPublicBindlessExecutionFixture{});
                        else
                            run(arp::test::MetalComposedBindlessExecutionFixture{});
                    }
            }
    }

    static void validateOriginalRootCustody(std::shared_ptr<const BindlessShaderPackage> package)
    {
        auto device = createMetalRhiDevice(deviceDesc());
        assert(device);
        // Real native buffers/argument buffers and prepared owners; deliberately
        // no shader access or CPU-state replay of uninitialized GPU resources.
        arp::test::validateOriginalBindlessRootSource(package,
            std::move(device),
            "cleanMain",
            false,
            arp::test::MetalOriginalBindlessRootFixture{});
        std::puts(
            "metalOriginalRootCustody: real-device canonical inventory, hot allocation0 and quota retirement; no execution grant");
        for (const bool submit : {false, true})
            for (const bool drop : {false, true})
                for (const bool fault : {false, true})
                    arp::test::validateOriginalBindlessRootSource(package,
                        createMetalRhiDevice(deviceDesc()),
                        fault ? "faultMain" : "cleanMain",
                        true,
                        arp::test::OriginalBindlessRootFixtureObserver{},
                        arp::test::MetalOriginalBindlessExecutionFixture{
                            submit, drop, fault ? 1u : 0u});
        for (const bool drop : {false, true})
            for (const bool fault : {false, true})
                arp::test::validateOriginalBindlessRootSource(package,
                    createMetalRhiDevice(deviceDesc()),
                    fault ? "faultMain" : "cleanMain",
                    true,
                    arp::test::OriginalBindlessRootFixtureObserver{},
                    arp::test::MetalOriginalBindlessExecutionFixture{
                        true, drop, fault ? 1u : 0u, true});
        for (const bool retire : {false, true})
            for (const bool submit : {false, true}) {
                if (retire && !submit)
                    continue;
                for (const bool drop : {false, true})
                    for (const bool fault : {false, true}) {
                        arp::test::MetalPublicBindlessExecutionFixture execute;
                        execute.composed = false;
                        execute.publicExecution = true;
                        execute.submit = submit;
                        execute.dropCommand = drop;
                        execute.retireDevice = retire;
                        execute.expectedFault = fault;
                        arp::test::validateOriginalBindlessRootSource(package,
                            createMetalRhiDevice(deviceDesc()),
                            fault ? "faultMain" : "cleanMain",
                            true,
                            arp::test::OriginalBindlessRootFixtureObserver{},
                            execute);
                    }
            }
    }

    explicit ComputeFixture(std::shared_ptr<const BindlessShaderPackage> package)
        : package_(std::move(package))
    {
        const auto desc = deviceDesc();
        const auto report = describeMetalRhiDevice(desc);
        check(report);
        device_ = createMetalRhiDevice(desc);
        assert(device_);
        extension_ =
            static_cast<BindlessCrossExtension*>(device_->queryInterface(kExtensionBindlessCross));
        assert(extension_ && extension_->capabilities().execution == BindlessExecution::Native);
        assert(extension_->capabilities().preparedComputeBindingContractVersion == 1u);
        assert(extension_->capabilities().preparedGraphicsBindingContractVersion == 0u);
        assert(extension_->capabilities().preparedGraphicsPassFaultScopeContractVersion == 0u);
        assert(extension_->capabilities().indirectInstanceConsumerContractVersion == 0u);
        request_.optIn = true;
        request_.deviceIdentity = extension_->capabilities().deviceIdentity;
        request_.shaderPackageIdentity = package_->artifact().shaderPackageIdentity;
        std::uint32_t descriptorCount = 0u;
        for (const auto& binding : package_->artifact().physicalLayout.bindings)
            descriptorCount += binding.arrayCount;
        request_.budget = {1u, descriptorCount, package_->artifact().snapshotBytes, 16u};
        for (const auto& table : package_->artifact().tables) {
            const DescriptorBindingDesc* binding = nullptr;
            for (const auto& candidate : package_->artifact().physicalLayout.bindings)
                if (candidate.binding == table.firstBinding)
                    binding = &candidate;
            assert(binding);
            request_.tables.push_back({table.tableId,
                *binding,
                table.capacity,
                table.operations,
                table.nonUniform,
                table.bufferLayout});
        }
        snapshots_ = core::makeUnique<BindlessSnapshotPool>(*device_, request_.budget);
        snapshot_ = snapshots_->prepareShared(request_, package_->artifact(), diagnostic_);
        if (!snapshot_)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(snapshot_);
        for (unsigned i = 0u; i < 2u; ++i) {
            const std::array<float, 4> globals{float(1u + i * 2u), 0, 0, 0};
            const std::uint32_t input = 10u + i * 10u, counter = 100u + i * 100u;
            globals_[i] = makeBuffer("Globals",
                sizeof(globals),
                ResourceUsage::UniformBuffer,
                ResourceState::ShaderRead,
                globals.data());
            inputs_[i] = makeBuffer("Inputs",
                sizeof(input),
                ResourceUsage::StorageBuffer,
                ResourceState::ShaderRead,
                &input);
            counters_[i] = makeBuffer("Counters",
                sizeof(counter),
                ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
                ResourceState::ShaderWrite,
                &counter);
            check(snapshot_->writeBuffer(snapshot_->handle(1u, i), *globals_[i]));
            check(snapshot_->writeBuffer(snapshot_->handle(2u, i), *inputs_[i]));
            check(snapshot_->writeBuffer(snapshot_->handle(3u, i), *counters_[i]));
            addAccess(*globals_[i], false);
            addAccess(*inputs_[i], false);
            addAccess(*counters_[i], true);
        }
        check(snapshot_->seal(*device_));
        accesses_.push_back(snapshot_->metadataAccess());
        accesses_.push_back(snapshot_->faultLatchAccess());
        accesses_.push_back(snapshot_->faultReadbackAccess());
        authorization_ = extension_->authorize(package_, *snapshot_, diagnostic_);
        if (!authorization_)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(authorization_);
        RenderPipelineDesc graphicsState;
        graphicsState.colorFormat = TextureFormat::Rgba8Unorm;
        graphicsState.dynamicViewportScissor = true;
        diagnostic_.reserve(sizeof("RHI-METAL-BINDLESS-GRAPHICS-EXECUTION-UNAVAILABLE") - 1u);
        const auto beforeGraphicsGate = core::defaultCpuAllocator().stats().allocationCount;
        assert(!extension_->createRenderPipeline(
                   authorization_, graphicsState, "vertexMain", "cleanFragment", diagnostic_)
            && diagnostic_ == "RHI-METAL-BINDLESS-GRAPHICS-EXECUTION-UNAVAILABLE");
        assert(!createMetalRhiAuthorizedBindlessRenderPipeline(*extension_,
                   authorization_,
                   graphicsState,
                   "vertexMain",
                   "cleanFragment",
                   desc.capabilityContract,
                   {},
                   {},
                   diagnostic_)
            && diagnostic_ == "RHI-METAL-BINDLESS-GRAPHICS-EXECUTION-UNAVAILABLE");
        assert(core::defaultCpuAllocator().stats().allocationCount == beforeGraphicsGate);
    }

    void validateBeforePassFaultClear()
    {
        // Seed a real nonzero GPU latch through the already-authorized Compute path.
        auto pipeline = core::shareUnique(
            extension_->createComputePipeline(authorization_, "faultMain", diagnostic_));
        assert(pipeline);
        const auto retainedBytes = snapshot_->plan().snapshotBytes + package_->sourceBytes();
        BindlessComputeBindingPool pool(
            *extension_, {1u, retainedBytes + 65536u, retainedBytes + 65536u, 3u, 65536u});
        auto binding = pool.prepare(
            {true, snapshot_, pipeline, snapshot_->plan(), snapshot_->generation()}, diagnostic_);
        assert(binding);
        auto seed = device_->createCommandEncoder({"SeedFault", 1u, 1u});
        assert(seed);
        auto seedPass = seed->beginComputePass({"SeedFault"});
        assert(seedPass);
        check(seedPass->setPipeline(*pipeline));
        auto* indexed =
            static_cast<BindlessPassExtension*>(seedPass->queryInterface(kExtensionBindlessCross));
        assert(indexed);
        BindlessAccessArray accesses(accesses_.data(), accesses_.size());
        check(indexed->bindPreparedComputeBinding(binding, accesses));
        check(seedPass->dispatch(1u, 1u, 1u));
        check(seedPass->end());
        auto seededCommands = seed->finish();
        assert(seededCommands);
        check(device_->queue().submit(*seededCommands));
        device_->waitIdle();
        assert(metalRhiNativePayloadFor(*seededCommands)
                   ->bindlessFaultReport(snapshot_->generation())
                   .faultBits
            == 1u);
        assert(snapshot_->faultLatchPriorState() == ResourceState::CopySrc);

        {
            auto encoder = device_->createCommandEncoder({"NoFaultOutcomeCapacity", 0u, 0u});
            assert(encoder);
            auto native = metalRhiNativePayloadFor(*encoder);
            assert(native);
            const auto domainAlias =
                std::shared_ptr<const BindlessFaultDomain>(pipeline, snapshot_.get());
            assert(!native->scheduleBindlessFaultReadback(domainAlias, diagnostic_)
                && diagnostic_ == "RHI-BINDLESS-FAULT-SUBMISSION-OWNER-MISMATCH");
            assert(!native->recordBindlessFaultLatchClearBeforePass(
                       domainAlias, ResourceState::CopySrc, diagnostic_)
                && diagnostic_ == "RHI-BINDLESS-FAULT-SUBMISSION-OWNER-MISMATCH");
            assert(!native->stats()->nativeBlitEncoderCreated
                && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 0u);
            assert(native->stats()->nativeBindlessFaultOutcomePreparationCount == 0u);
            assert(!native->scheduleBindlessFaultReadback(snapshot_, diagnostic_)
                && diagnostic_ == "RHI-METAL-BINDLESS-FAULT-READBACK-CAPACITY");
            assert(!native->stats()->nativeBlitEncoderCreated
                && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 0u);
        }

        for (unsigned mode = 0u; mode < 3u; ++mode) {
            auto encoder = device_->createCommandEncoder({"BeforePassClear", 1u, 1u});
            assert(encoder);
            auto native = metalRhiNativePayloadFor(*encoder);
            assert(native);
            assert(native->stats()->nativeBindlessFaultOutcomePreparationCount == 1u
                && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 0u);
            assert(!native->recordBindlessFaultLatchClearBeforePass(
                       snapshot_, ResourceState::Undefined, diagnostic_)
                && diagnostic_ == "RHI-METAL-BINDLESS-FAULT-CLEAR-PRIOR-STATE");
            assert(!native->stats()->nativeBlitEncoderCreated);
            assert(native->recordBindlessFaultLatchClearBeforePass(
                snapshot_, ResourceState::CopySrc, diagnostic_));
            assert(native->stats()->nativeBlitCopyEncoded
                && !native->stats()->nativeRenderEncoderCreated);
            assert(!native->recordBindlessFaultLatchClearBeforePass(
                       snapshot_, ResourceState::CopySrc, diagnostic_)
                && diagnostic_ == "RHI-METAL-BINDLESS-FAULT-GENERATION-DUPLICATE");
            assert(!native->finish(diagnostic_)
                && diagnostic_ == "RHI-METAL-BINDLESS-FAULT-READBACK-NOT-SCHEDULED");
            auto competing = device_->createCommandEncoder({"CompetingFaultLease", 1u, 1u});
            assert(competing);
            auto competingNative = metalRhiNativePayloadFor(*competing);
            assert(competingNative);
            assert(!competingNative->recordBindlessFaultLatchClearBeforePass(
                       snapshot_, ResourceState::CopySrc, diagnostic_)
                && diagnostic_ == "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
            assert(!competingNative->stats()->nativeBlitEncoderCreated);
            competing.reset();
            if (mode == 0u) {
                encoder.reset();
                auto lease = BindlessFaultSubmissionLease::acquire(snapshot_, diagnostic_);
                assert(lease.valid());
                continue;
            }
            auto readback = native->scheduleBindlessFaultReadback(snapshot_, diagnostic_);
            assert(readback);
            assert(native->stats()->nativeBindlessFaultOutcomePreparationCount == 1u
                && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 1u);
            assert(readback->status() == BindlessFaultValidationStatus::Pending);
            auto commands = encoder->finish();
            assert(commands);
            assert(!native->recordBindlessFaultLatchClearBeforePass(
                       snapshot_, ResourceState::CopySrc, diagnostic_)
                && diagnostic_ == "RHI-METAL-BINDLESS-FAULT-CLEAR-RECORDING-INVALID");
            assert(!native->scheduleBindlessFaultReadback(snapshot_, diagnostic_)
                && diagnostic_ == "RHI-METAL-BINDLESS-FAULT-READBACK-RECORDING-INVALID");
            assert(native->stats()->nativeBindlessFaultOutcomeConsumeCount == 1u);
            encoder.reset();
            if (mode == 1u) {
                commands.reset();
                assert(readback->status() == BindlessFaultValidationStatus::Abandoned);
                continue;
            }
            auto timeline = device_->createTimelineSemaphore(0u);
            auto fence = device_->createFence();
            assert(timeline && fence);
            check(device_->queue().submit(*commands, timeline.get(), 1u, nullptr, 0u, fence.get()));
            assert(!fence->isSignaled()
                && readback->status() == BindlessFaultValidationStatus::Pending);
            auto lease = BindlessFaultSubmissionLease::acquire(snapshot_, diagnostic_);
            assert(!lease.valid() && diagnostic_ == "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
            timeline->signal(1u);
            fence->wait();
            assert(readback->status() == BindlessFaultValidationStatus::Clean
                && readback->faultBits() == 0u);
        }

        auto zeroBudget = device_->createCommandEncoder({"BeforePassZeroBudget", 1u, 0u});
        assert(zeroBudget);
        auto zeroNative = metalRhiNativePayloadFor(*zeroBudget);
        assert(zeroNative);
        assert(!zeroNative->recordBindlessFaultLatchClearBeforePass(
                   snapshot_, ResourceState::CopySrc, diagnostic_)
            && diagnostic_ == "RHI-METAL-BINDLESS-FAULT-LEASE-CAPACITY");
        assert(!zeroNative->stats()->nativeBlitEncoderCreated);

        auto encoder = device_->createCommandEncoder({"RejectOpenEncoders", 1u, 1u});
        assert(encoder);
        auto native = metalRhiNativePayloadFor(*encoder);
        assert(native);
        auto compute = encoder->beginComputePass({"RejectCompute"});
        assert(compute);
        assert(!native->recordBindlessFaultLatchClearBeforePass(
            snapshot_, ResourceState::CopySrc, diagnostic_));
        check(compute->end());
        auto transfer = encoder->beginTransferPass({"RejectBlit"});
        assert(transfer);
        assert(!native->recordBindlessFaultLatchClearBeforePass(
            snapshot_, ResourceState::CopySrc, diagnostic_));
        check(transfer->end());
        auto target = device_->createTexture({"FaultBoundaryAttachment",
            {1u, 1u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::RenderAttachment,
            MemoryUsage::GpuOnly,
            ResourceState::ColorAttachment});
        assert(target);
        RenderPassDesc renderDesc;
        RenderPassColorAttachmentDesc color;
        color.target = target->id();
        color.format = target->desc().format;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        color.clearColor = {0.25f, 0.5f, 0.75f, 1.0f};
        renderDesc.colorAttachments.push_back(color);
        auto render = encoder->beginRenderPass(renderDesc);
        assert(render);
        const auto stats = *native->stats();
        assert(!native->recordBindlessFaultLatchClearBeforePass(
                   snapshot_, ResourceState::CopySrc, diagnostic_)
            && diagnostic_ == "RHI-METAL-BINDLESS-FAULT-CLEAR-RECORDING-INVALID");
        assert(
            !native->recordBindlessFaultLatchClear(snapshot_, ResourceState::CopySrc, diagnostic_));
        assert(!native->scheduleBindlessFaultReadback(snapshot_, diagnostic_));
        assert(native->stats()->nativeColorClearValue.r == stats.nativeColorClearValue.r
            && native->stats()->nativeColorClearValue.g == stats.nativeColorClearValue.g
            && native->stats()->nativeColorClearValue.b == stats.nativeColorClearValue.b);
        check(render->end());
        auto commands = encoder->finish();
        assert(commands);
        auto lease = BindlessFaultSubmissionLease::acquire(snapshot_, diagnostic_);
        assert(lease.valid());
        std::puts(
            "beforePassFaultClear: seeded 1, completed 0; exclusive/cancel/boundary gates passed");
    }

    void validateMixedPreparedRetention(const BindlessShaderPackage& graphicsPackage)
    {
        test::MetalGraphicsPipelineFixture fixture(graphicsPackage, "vertexMain", "cleanFragment");
        auto graphicsPipeline = fixture.create();
        assert(graphicsPipeline && graphicsPipeline->nativeRuntimeBacked);
        auto graphicsDescriptors = core::makeShared<MetalRhiNativeDescriptorGroupPayload>(
            *metalRhiNativePayloadFor(*snapshot_->descriptorSet()));
        // This test records bindings only. Both generated schemas explicitly
        // share binding/type/capacity; stage intent comes from the Graphics schema.
        for (auto& binding : graphicsDescriptors->bindings) {
            const DescriptorBindingDesc* graphics = nullptr;
            for (const auto& candidate : graphicsPackage.artifact().physicalLayout.bindings)
                if (candidate.binding == binding.binding)
                    graphics = &candidate;
            assert(graphics && graphics->type == binding.type
                && graphics->arrayCount == binding.arrayCount);
            binding.stages = graphics->stages;
        }
        std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2> layouts;
        for (unsigned i = 0u; i < 2u; ++i) {
            auto layout = core::makeShared<MetalRhiPreparedGraphicsStageLayout>();
            check(lowerMetalRhiPreparedGraphicsStage(graphicsPackage,
                i ? "cleanFragment" : "vertexMain",
                i ? ShaderStage::Fragment : ShaderStage::Vertex,
                *layout));
            layouts[i] = std::move(layout);
        }
        auto computePipeline = core::shareUnique(
            extension_->createComputePipeline(authorization_, "cleanMain", diagnostic_));
        assert(computePipeline);
        const auto retained = snapshot_->plan().snapshotBytes + package_->sourceBytes();
        BindlessComputeBindingPool pool(
            *extension_, {1u, retained + 65536u, retained + 65536u, 3u, 65536u});
        auto target = device_->createTexture({"MixedPreparedRetention",
            {2u, 2u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::RenderAttachment,
            MemoryUsage::GpuOnly,
            ResourceState::ColorAttachment});
        assert(target);
        RenderPassDesc renderDesc;
        renderDesc.colorAttachments.push_back({"MixedPreparedColor",
            target->id(),
            target->desc().format,
            LoadOp::Clear,
            StoreOp::Store});
        for (unsigned order = 0u; order < 2u; ++order) {
            for (unsigned capacity = 1u; capacity <= 2u; ++capacity) {
                auto compute = pool.prepare(
                    {true, snapshot_, computePipeline, snapshot_->plan(), snapshot_->generation()},
                    diagnostic_);
                assert(compute);
                auto graphics = prepareMetalRhiNativeGraphicsBindings(graphicsPipeline,
                    graphicsDescriptors,
                    graphicsPackage.artifact().set,
                    layouts,
                    {1u, 65536u, 65536u, 16u, 65536u},
                    diagnostic_);
                assert(graphics);
                auto encoder = device_->createCommandEncoder({"MixedPreparedOwners", 1u, capacity});
                assert(encoder);
                auto native = metalRhiNativePayloadFor(*encoder);
                assert(native);
                for (unsigned step = 0u; step < 2u; ++step) {
                    const bool secondRejected = step == 1u && capacity == 1u;
                    if ((order + step) % 2u == 0u) {
                        auto pass = encoder->beginRenderPass(renderDesc);
                        assert(pass);
                        assert(native->recordGraphicsPipelineBind(graphicsPipeline, diagnostic_));
                        assert(native->recordPreparedGraphicsBinding(graphics, diagnostic_)
                            == !secondRejected);
                        if (secondRejected)
                            assert(diagnostic_ == "RHI-BINDLESS-PREPARED-GRAPHICS-COMMAND-BUDGET");
                        check(pass->end());
                    } else {
                        auto pass = encoder->beginComputePass({"MixedPreparedCompute"});
                        assert(pass);
                        check(pass->setPipeline(*computePipeline));
                        assert(
                            native->recordPreparedComputeBinding(
                                metalRhiPreparedComputePayloadFor(*compute), compute, diagnostic_)
                            == !secondRejected);
                        if (secondRejected)
                            assert(diagnostic_ == "RHI-BINDLESS-PREPARED-COMPUTE-COMMAND-BUDGET");
                        check(pass->end());
                    }
                }
                std::weak_ptr<const BindlessPreparedComputeBinding> observedCompute = compute;
                std::weak_ptr<const MetalRhiNativePreparedGraphicsBindings> observedGraphics =
                    graphics;
                compute.reset();
                graphics.reset();
                assert(observedCompute.expired() == (capacity == 1u && order == 0u));
                assert(observedGraphics.expired() == (capacity == 1u && order == 1u));
                auto commands = encoder->finish();
                assert(commands);
                encoder.reset();
                auto timeline = device_->createTimelineSemaphore(0u);
                auto fence = device_->createFence();
                assert(timeline && fence);
                check(device_->queue().submit(
                    *commands, timeline.get(), 1u, nullptr, 0u, fence.get()));
                commands.reset();
                assert(!fence->isSignaled());
                if (capacity == 2u)
                    assert(!observedCompute.expired() && !observedGraphics.expired());
                timeline->signal(1u);
                fence->wait();
                assert(observedCompute.expired() && observedGraphics.expired()
                    && pool.liveBindings() == 0u);
            }
        }
        std::puts(
            "mixedPreparedOwners: shared capacity 1/2, both orders, native completion; no Graphics draw");
    }

    void run(std::string_view entry, bool faulted, bool abandon)
    {
        auto pipeline = core::shareUnique(
            extension_->createComputePipeline(authorization_, entry, diagnostic_));
        if (!pipeline)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(pipeline);
        const auto retainedBytes = snapshot_->plan().snapshotBytes + package_->sourceBytes();
        BindlessComputeBindingBudget budget{
            1u, retainedBytes + 65536u, retainedBytes + 65536u, 3u, 65536u};
        auto pool = core::makeUnique<BindlessComputeBindingPool>(*extension_, budget);
        BindlessGraphicsBindingPool graphicsPool(*extension_, budget);
        BindlessGraphicsBindingDesc graphicsDesc;
        graphicsDesc.optIn = true;
        assert(!graphicsPool.prepare(graphicsDesc, diagnostic_)
            && diagnostic_ == "RHI-BINDLESS-PREPARED-GRAPHICS-UNAVAILABLE");
        assert(graphicsPool.liveBindings() == 0u && graphicsPool.chargedBytes() == 0u);
        BindlessComputeBindingDesc desc{
            true, snapshot_, pipeline, snapshot_->plan(), snapshot_->generation()};
        auto denied = desc;
        denied.optIn = false;
        assert(
            !pool->prepare(denied, diagnostic_) && diagnostic_ == "RHI-BINDLESS-OPT-IN-REQUIRED");
        denied = desc;
        denied.pipeline = std::shared_ptr<const ComputePipeline>(snapshot_, pipeline.get());
        assert(!pool->prepare(denied, diagnostic_)
            && diagnostic_ == "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH");
        denied = desc;
        denied.snapshot = std::shared_ptr<const BindlessSnapshot>(pipeline, snapshot_.get());
        assert(!pool->prepare(denied, diagnostic_)
            && diagnostic_ == "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH");
        denied = desc;
        ++denied.generation;
        assert(!pool->prepare(denied, diagnostic_)
            && diagnostic_ == "RHI-BINDLESS-PREPARED-COMPUTE-PLAN-MISMATCH");
        denied = desc;
        ++denied.plan.deviceIdentity;
        assert(!pool->prepare(denied, diagnostic_)
            && diagnostic_ == "RHI-BINDLESS-PREPARED-COMPUTE-PLAN-MISMATCH");
        assert(pool->liveBindings() == 0u && pool->chargedBytes() == 0u);
        auto binding = pool->prepare(desc, diagnostic_);
        if (!binding)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(
            binding && binding->argumentBufferCount() == 3u && binding->argumentBufferBytes() > 0u);
        std::printf("preparedArgumentBuffers: %u\npreparedArgumentBytes: %llu\n",
            binding->argumentBufferCount(),
            static_cast<unsigned long long>(binding->argumentBufferBytes()));
        assert(!pool->prepare(desc, diagnostic_)
            && diagnostic_ == "RHI-BINDLESS-PREPARED-COMPUTE-POOL-FULL");
        auto small = budget;
        small.maxArgumentBuffers = 2u;
        BindlessComputeBindingPool tooFew(*extension_, small);
        assert(!tooFew.prepare(desc, diagnostic_)
            && diagnostic_ == "RHI-METAL-PREPARED-COMPUTE-ARGUMENT-COUNT-BUDGET");
        assert(tooFew.liveBindings() == 0u && tooFew.chargedBytes() == 0u);
        small = budget;
        small.maxArgumentBufferBytes = 1u;
        BindlessComputeBindingPool tooSmall(*extension_, small);
        assert(!tooSmall.prepare(desc, diagnostic_)
            && diagnostic_ == "RHI-METAL-PREPARED-COMPUTE-ARGUMENT-BYTE-BUDGET");
        assert(tooSmall.liveBindings() == 0u && tooSmall.chargedBytes() == 0u);
        validateRecordingGates(binding, budget);

        auto encoder = device_->createCommandEncoder({"MetalBindlessCompute", 1u, 1u});
        assert(encoder);
        assert(
            encoder->prepareBindlessGraphicsPass({}, BindlessAccessArray(nullptr, 0u)).diagnostics
            == "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-UNAVAILABLE");
        auto pass = encoder->beginComputePass({"MetalBindlessCompute"});
        assert(pass);
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        assert(indexed);
        BindlessAccessArray declarations(accesses_.data(), accesses_.size());
        assert(indexed->bindPreparedGraphicsBinding({}, declarations).diagnostics
            == "RHI-BINDLESS-PREPARED-GRAPHICS-UNAVAILABLE");
        assert(!indexed->bindPreparedComputeBinding(binding, declarations).success);
        check(pass->setPipeline(*pipeline));
        const auto bindingAlias =
            std::shared_ptr<const BindlessPreparedComputeBinding>(snapshot_, binding.get());
        const auto nativeBeforeAlias = metalRhiNativePayloadFor(*encoder);
        assert(nativeBeforeAlias);
        const auto aliasCount = nativeBeforeAlias->stats()->nativeArgumentBufferReuseCount;
        assert(indexed->bindPreparedComputeBinding(bindingAlias, declarations).diagnostics
            == "RHI-BINDLESS-PREPARED-COMPUTE-PAYLOAD-MISMATCH");
        assert(nativeBeforeAlias->stats()->nativeArgumentBufferReuseCount == aliasCount
            && nativeBeforeAlias->stats()->nativeBindlessFaultOutcomeConsumeCount == 0u);
        assert(!pass->dispatch(1u, 1u, 1u).success);
        BindlessAccessArray empty(nullptr, 0u);
        assert(!indexed->bindPreparedComputeBinding(binding, empty).success);
        assert(!indexed
                ->bindSnapshot(snapshot_, snapshot_->plan(), snapshot_->generation(), declarations)
                .success);
        check(indexed->bindPreparedComputeBinding(binding, declarations));
        const std::array<std::uint32_t, 3> groups{1u, 1u, 1u};
        auto indirect = makeBuffer("RejectedIndirectGroups",
            sizeof(groups),
            ResourceUsage::IndirectBuffer,
            ResourceState::ShaderRead,
            groups.data());
        const auto rejectedIndirect = pass->dispatchIndirect(*indirect, 0u);
        assert(!rejectedIndirect.success
            && rejectedIndirect.diagnostics == "RHI-BINDLESS-INDIRECT-DISPATCH-UNSUPPORTED");
        auto nativeBinding = metalRhiPreparedComputePayloadFor(*binding);
        auto nativeEncoder = metalRhiNativePayloadFor(*encoder);
        assert(nativeBinding && nativeEncoder);
        const auto payloadAlias = std::shared_ptr<const MetalRhiNativePreparedComputeBindings>(
            snapshot_, nativeBinding.get());
        const auto beforeAliasRebind = nativeEncoder->stats()->nativeArgumentBufferReuseCount;
        assert(!nativeEncoder->recordPreparedComputeBinding(payloadAlias, binding, diagnostic_)
            && diagnostic_ == "RHI-METAL-PREPARED-COMPUTE-OWNER-MISMATCH");
        assert(
            !nativeEncoder->recordPreparedComputeBinding(nativeBinding, bindingAlias, diagnostic_)
            && diagnostic_ == "RHI-METAL-PREPARED-COMPUTE-OWNER-MISMATCH");
        assert(nativeEncoder->stats()->nativeArgumentBufferReuseCount == beforeAliasRebind);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 32u; ++i)
            assert(
                nativeEncoder->recordPreparedComputeBinding(nativeBinding, binding, diagnostic_));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        check(pass->dispatch(1u, 1u, 1u));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::puts("coreAllocationsDuring32RebindsAndDispatch: 0");
        check(pass->end());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        const auto stats = *nativeEncoder->stats();
        assert(stats.nativeArgumentBufferAllocationCount == 0u
            && stats.nativeArgumentBufferReuseCount >= 3u);

        auto readback = device_->createBuffer({"CounterReadback",
            8u,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        assert(readback);
        for (const auto& counter : counters_)
            check(
                encoder->transition(*counter, ResourceState::ShaderWrite, ResourceState::CopySrc));
        auto transfer = encoder->beginTransferPass({"CounterReadback"});
        assert(transfer);
        for (unsigned i = 0u; i < 2u; ++i)
            check(
                transfer->copyBufferToBuffer({counters_[i]->id(), readback->id(), 0u, i * 4u, 4u}));
        check(transfer->end());
        auto commands = encoder->finish();
        assert(commands);
        auto nativeCommand = metalRhiNativePayloadFor(*commands);
        const auto generation = snapshot_->generation();
        assert(nativeCommand->bindlessFaultReport(generation).status
            == BindlessFaultValidationStatus::Pending);
        std::weak_ptr<const BindlessPreparedComputeBinding> observed = binding;
        std::weak_ptr<const MetalRhiNativePreparedComputeBindings> observedPayload = nativeBinding;
        desc = {};
        denied = {};
        binding.reset();
        pipeline.reset();
        pass.reset();
        encoder.reset();
        nativeEncoder.reset();
        nativeBinding.reset();
        assert(!observed.expired() && pool->liveBindings() == 1u);
        if (abandon) {
            commands.reset();
            assert(observed.expired() && observedPayload.expired() && pool->liveBindings() == 0u);
            assert(nativeCommand->bindlessFaultReport(generation).status
                == BindlessFaultValidationStatus::Abandoned);
            std::puts("abandon: passed");
            return;
        }
        auto timeline = device_->createTimelineSemaphore(0u);
        auto fence = device_->createFence();
        assert(timeline && fence);
        check(device_->queue().submit(*commands, timeline.get(), 1u, nullptr, 0u, fence.get()));
        commands.reset();
        assert(!fence->isSignaled() && !observed.expired() && pool->liveBindings() == 1u);
        assert(nativeCommand->bindlessFaultReport(generation).status
            == BindlessFaultValidationStatus::Pending);
        pool.reset();
        snapshot_.reset();
        snapshots_.reset();
        authorization_.reset();
        package_.reset();
        timeline->signal(1u);
        fence->wait();
        assert(observed.expired() && observedPayload.expired());
        const auto report = nativeCommand->bindlessFaultReport(generation);
        assert(report.status
            == (faulted ? BindlessFaultValidationStatus::Faulted
                        : BindlessFaultValidationStatus::Clean));
        assert(report.faultBits == (faulted ? 1u : 0u) && report.validate().success == !faulted);
        std::array<std::uint32_t, 2> words{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
            words.data(),
            sizeof(words),
            0u,
            sizeof(words),
            diagnostic_));
        assert((words == std::array<std::uint32_t, 2>{114u, 226u}));
        std::printf("entry: %.*s\ncounters: [%u, %u]\nfaultBits: %u\ncompletionRetention: passed\n",
            int(entry.size()),
            entry.data(),
            words[0],
            words[1],
            report.faultBits);
    }

private:
    static MetalRhiDeviceDesc deviceDesc()
    {
        MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        desc.memoryBudgetBytes = 1024u * 1024u;
        desc.enableBindlessCompute = true;
        return desc;
    }

    void validateRecordingGates(
        const std::shared_ptr<const BindlessPreparedComputeBinding>& binding,
        const BindlessComputeBindingBudget& budget)
    {
        BindlessAccessArray declarations(accesses_.data(), accesses_.size());
        auto foreign = createMetalRhiDevice(deviceDesc());
        assert(foreign);
        auto* foreignExtension =
            static_cast<BindlessCrossExtension*>(foreign->queryInterface(kExtensionBindlessCross));
        assert(foreignExtension
            && foreignExtension->capabilities().deviceIdentity != request_.deviceIdentity);
        assert(!foreignExtension->authorize(package_, *snapshot_, diagnostic_));
        assert(!foreignExtension->createComputePipeline(authorization_, "cleanMain", diagnostic_));
        BindlessComputeBindingPool foreignPool(*foreignExtension, budget);
        assert(!foreignPool.prepare(binding->owners()->desc(), diagnostic_)
            && diagnostic_ == "RHI-BINDLESS-PREPARED-COMPUTE-AUTHORIZATION-INVALID");
        assert(foreignPool.liveBindings() == 0u && foreignPool.chargedBytes() == 0u);
        auto foreignEncoder = foreign->createCommandEncoder({"ForeignCompute", 1u, 1u});
        assert(foreignEncoder);
        auto foreignPass = foreignEncoder->beginComputePass({"ForeignCompute"});
        assert(
            foreignPass && !foreignPass->setPipeline(*binding->owners()->desc().pipeline).success);
        check(foreignPass->end());

        auto otherPipeline =
            extension_->createComputePipeline(authorization_, "cleanMain", diagnostic_);
        assert(otherPipeline);
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            auto encoder = device_->createCommandEncoder(
                {"ComputeNegativeGate", mode == 2u ? 0u : 1u, mode == 1u ? 0u : 1u});
            assert(encoder);
            auto pass = encoder->beginComputePass({"ComputeNegativeGate"});
            assert(pass);
            auto* indexed =
                static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
            if (mode != 0u) {
                assert(!indexed);
                assert(!pass->setPipeline(*binding->owners()->desc().pipeline).success);
            } else {
                assert(indexed);
                check(pass->setPipeline(*otherPipeline));
                assert(!indexed->bindPreparedComputeBinding(binding, declarations).success);
                check(pass->setPipeline(*binding->owners()->desc().pipeline));
                auto forged = core::makeShared<ForgedComputeBinding>(*binding);
                assert(!indexed->bindPreparedComputeBinding(forged, declarations).success);
                assert(!pass->dispatch(1u, 1u, 1u).success);
            }
            const auto stats = *metalRhiNativePayloadFor(*encoder)->stats();
            assert(stats.nativeArgumentBufferReuseCount == 0u
                && stats.nativeArgumentBufferAllocationCount == 0u);
            check(pass->end());
        }
        std::puts("negativeOwnershipAndRecordingGates: passed");
    }

    core::UniquePtr<GpuBuffer> makeBuffer(const char* label,
        std::uint64_t bytes,
        ResourceUsage usage,
        ResourceState state,
        const void* data)
    {
        auto buffer = device_->createBuffer({label, bytes, usage, MemoryUsage::CpuToGpu, state});
        assert(buffer);
        check(device_->writeBuffer(*buffer, data, bytes));
        return buffer;
    }

    void addAccess(const GpuBuffer& buffer, bool writable)
    {
        BindlessResourceAccess access;
        access.resource = buffer.id();
        access.kind = BindlessAccessKind::Buffer;
        access.state = writable ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
        access.stages = ShaderStage::Compute;
        access.pipelineStages = PipelineStage::ComputeShader;
        access.access = ResourceAccess::ShaderRead
            | (writable ? ResourceAccess::ShaderWrite : ResourceAccess::None);
        access.bufferSize = buffer.desc().sizeBytes;
        accesses_.push_back(access);
    }

    std::shared_ptr<const BindlessShaderPackage> package_;
    core::UniquePtr<GpuDevice> device_;
    const BindlessCrossExtension* extension_ = nullptr;
    BindlessRequirements request_;
    core::UniquePtr<BindlessSnapshotPool> snapshots_;
    std::shared_ptr<BindlessSnapshot> snapshot_;
    std::shared_ptr<const BindlessAuthorization> authorization_;
    std::array<core::UniquePtr<GpuBuffer>, 2> globals_, inputs_, counters_;
    core::pmr::Vector<BindlessResourceAccess> accesses_;
    core::pmr::String diagnostic_;
};
} // namespace

int main(int argc, char** argv)
{
    core::pmr::String diagnostic;
    if (argc == 4 && std::string_view(argv[1]) == "--capture-public-composed-root") {
        auto composed = BindlessShaderPackage::load(
            argv[2], {1024u * 1024u, 4u * 1024u * 1024u, 32u, 2u, 16u, 3u}, diagnostic);
        assert(composed);
        ComputeFixture::capturePublicComposedRoot(std::move(composed), argv[3]);
        return 0;
    }
    if (argc == 3
        && (std::string_view(argv[1]) == "--composed-root"
            || std::string_view(argv[1]) == "--public-composed-root")) {
        auto composed = BindlessShaderPackage::load(
            argv[2], {1024u * 1024u, 4u * 1024u * 1024u, 32u, 2u, 16u, 3u}, diagnostic);
        if (!composed)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(composed);
        ComputeFixture::validateComposedRoot(
            std::move(composed), std::string_view(argv[1]) == "--public-composed-root");
        return 0;
    }
    auto package = BindlessShaderPackage::load(ARP_METAL_BINDLESS_PUBLICATION,
        {1024u * 1024u, 4u * 1024u * 1024u, 32u, 2u, 8u, 3u},
        diagnostic);
    if (!package)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(package);
    check(validateMetalBindlessComputeArtifact(package->artifact()));
    ComputeFixture::validateOriginalRootCustody(package);
    ComputeFixture(package).validateBeforePassFaultClear();
    auto graphicsPackage = BindlessShaderPackage::load(ARP_METAL_GRAPHICS_PUBLICATION,
        {1024u * 1024u, 4u * 1024u * 1024u, 32u, 3u, 8u, 3u},
        diagnostic);
    assert(graphicsPackage);
    ComputeFixture(package).validateMixedPreparedRetention(*graphicsPackage);
    for (unsigned mode = 0u; mode < 5u; ++mode) {
        auto unsupported = package->artifact();
        if (mode == 0u)
            unsupported.physicalLayout.bindings.front().stages = ShaderStage::Fragment;
        if (mode == 1u)
            unsupported.physicalLayout.bindings.front().type = DescriptorType::SampledTexture;
        if (mode == 2u)
            unsupported.indirectInstanceConsumer.contractVersion = 1u;
        if (mode == 3u)
            unsupported.otherBindGroups = 1u;
        if (mode == 4u)
            unsupported.reservedPerStage[2u].storageBuffers = 1u;
        assert(!validateMetalBindlessComputeArtifact(unsupported).success);
    }
    for (unsigned mode = 0u; mode < 3u; ++mode) {
        ComputeFixture fixture(package);
        fixture.run(mode == 1u ? "faultMain" : "cleanMain", mode == 1u, mode == 2u);
    }
}
