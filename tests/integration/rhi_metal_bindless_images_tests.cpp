#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_bindless_validation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/gpu_extensions.h"
#include "tests/fixtures/metal_completed_texture_root_fixture.h"
#include "tests/fixtures/metal_original_bindless_root_fixture.h"
#include "tests/fixtures/original_bindless_root_source_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Metal bindless image tests require assertions
#endif

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

class ImageExecutionFixture final {
public:
    static void verifySamplerNativeOwnerAdmission(const MetalRhiDeviceDesc& desc)
    {
        auto device = createMetalRhiDevice(desc);
        assert(device);
        auto first = device->createSampler({});
        assert(first);
        auto native = metalRhiNativePayloadFor(*first);
        assert(native);
        const auto original = native->resource.nativeObject;
        const auto foreign = core::makeShared<std::uint32_t>(0u);
        native->resource.nativeObject = std::shared_ptr<void>(foreign, original.get());
        auto second = device->createSampler({});
        assert(second);
        auto secondNative = metalRhiNativePayloadFor(*second);
        assert(secondNative);
        assert(!core::SharedOwnerProjection::sameControlBlock(
            secondNative->resource.nativeObject, foreign));
        auto third = device->createSampler({});
        assert(third);
        const auto thirdNative = metalRhiNativePayloadFor(*third);
        assert(thirdNative);
        if (secondNative->resource.nativeObject.get() == thirdNative->resource.nativeObject.get())
            assert(core::SharedOwnerProjection::sameOwner(
                secondNative->resource.nativeObject, thirdNative->resource.nativeObject));
        native->resource.nativeObject = original;
        std::printf(
            "BINDLESS_METAL_SAMPLER_OWNER_ADMISSION cached=%d forged-existing-owner-not-adopted genuine-owner-reused\n",
            secondNative->resource.nativeObject.get() == thirdNative->resource.nativeObject.get());
    }

    explicit ImageExecutionFixture(std::shared_ptr<const BindlessShaderPackage> package,
        bool comparison = false,
        bool wrongSampledFormat = false)
        : package_(std::move(package))
    {
        MetalRhiDeviceDesc desc;
        desc.label = "MetalComputeImagesFixture";
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.memoryBudgetBytes = 16u * 1024u * 1024u;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        desc.enableBindlessCompute = true;
        // Base opt-in must retain its buffer-only operation allowlist.
        auto base = createMetalRhiDevice(desc);
        assert(base);
        auto* baseExtension =
            static_cast<BindlessCrossExtension*>(base->queryInterface(kExtensionBindlessCross));
        assert(baseExtension
            && baseExtension->capabilities().support[2u][0u].operations == BindlessOperation::None
            && !baseExtension->capabilities().support[2u][1u].nonUniform);
        desc.enableBindlessComputeImages = true;
        device_ = createMetalRhiDevice(desc);
        assert(device_);
        extension_ =
            static_cast<BindlessCrossExtension*>(device_->queryInterface(kExtensionBindlessCross));
        assert(extension_ && extension_->capabilities().execution == BindlessExecution::Native);
        assert(extension_->capabilities().support[2u][0u].nonUniform
            && extension_->capabilities().support[2u][1u].nonUniform
            && extension_->capabilities().support[2u][2u].nonUniform);
        assert(!metalBindlessGraphicsExecutionAvailable(extension_->capabilities()));
        const auto nativeFeatures = probeMetalRhiNativeFeatures();
        assert(nativeFeatures.r32FloatReadWriteTexture
            && extension_->capabilities().combinedSampledStorageTextureLimits[2u]
                == nativeFeatures.maxTextureBindingsPerStage);
        BindlessRequirements request;
        request.optIn = true;
        request.deviceIdentity = extension_->capabilities().deviceIdentity;
        request.shaderPackageIdentity = package_->artifact().shaderPackageIdentity;
        std::uint32_t elements = 0u;
        for (const auto& binding : package_->artifact().physicalLayout.bindings)
            elements += binding.arrayCount;
        request.budget = {1u, elements, package_->artifact().snapshotBytes, 32u};
        for (const auto& table : package_->artifact().tables) {
            const DescriptorBindingDesc* physical = nullptr;
            for (const auto& binding : package_->artifact().physicalLayout.bindings)
                if (binding.binding == table.firstBinding)
                    physical = &binding;
            assert(physical);
            request.tables.push_back({table.tableId,
                *physical,
                table.capacity,
                table.operations,
                table.nonUniform,
                table.bufferLayout});
        }
        auto baseRequest = request;
        baseRequest.deviceIdentity = baseExtension->capabilities().deviceIdentity;
        const auto denied = baseExtension->plan(baseRequest, package_->artifact());
        assert(!denied.success && !denied.executable);
        snapshots_ = core::makeUnique<BindlessSnapshotPool>(*device_, request.budget);
        diagnostic_.reserve(256u);
        snapshot_ = snapshots_->prepareShared(request, package_->artifact(), diagnostic_);
        if (!snapshot_)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(snapshot_);
        for (unsigned i = 0u; i < images_.size(); ++i) {
            const auto sampledFormat =
                wrongSampledFormat ? TextureFormat::R32Float : TextureFormat::Rgba8Unorm;
            images_[i] = device_->createTexture({"IndexedSampledMipImage",
                {2u, 1u, 1u},
                sampledFormat,
                TextureUsage::Sampled | TextureUsage::CopyDst,
                MemoryUsage::GpuOnly,
                ResourceState::Undefined,
                2u});
            outputs_[i] = device_->createTexture({"IndexedStorageImage",
                {1u, 1u, 1u},
                TextureFormat::R32Float,
                TextureUsage::Storage | TextureUsage::CopyDst | TextureUsage::CopySrc,
                MemoryUsage::GpuOnly,
                ResourceState::Undefined});
            assert(images_[i] && outputs_[i]);
            imageViews_[i] = device_->createTextureView(*images_[i],
                {"IndexedSampledView",
                    sampledFormat,
                    TextureUsage::Sampled,
                    TextureViewDimension::Texture2D,
                    0u,
                    2u});
            outputViews_[i] = device_->createTextureView(*outputs_[i],
                {"IndexedStorageView", TextureFormat::R32Float, TextureUsage::Storage});
            SamplerDesc sampler;
            sampler.label = i ? "IndexedLinear" : "IndexedNearest";
            sampler.minFilter = sampler.magFilter =
                i ? SamplerFilter::Linear : SamplerFilter::Nearest;
            sampler.mipFilter = SamplerMipFilter::Nearest;
            if (comparison && i == 0u)
                sampler.compareOp = CompareOp::Less;
            samplers_[i] = device_->createSampler(sampler);
            results_[i] = device_->createBuffer({"IndexedImageResults",
                sizeof(initialResults),
                ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderWrite});
            assert(imageViews_[i] && outputViews_[i] && samplers_[i] && results_[i]);
            check(
                device_->writeBuffer(*results_[i], initialResults.data(), sizeof(initialResults)));
        }
        upload();
        for (unsigned i = 0u; i < images_.size(); ++i) {
            check(snapshot_->writeTexture(
                snapshot_->handle(1u, i), *imageViews_[i], ResourceState::ShaderRead));
            check(snapshot_->writeSampler(snapshot_->handle(2u, i), *samplers_[i]));
            check(snapshot_->writeTexture(
                snapshot_->handle(3u, i), *outputViews_[i], ResourceState::ShaderWrite));
            check(snapshot_->writeBuffer(snapshot_->handle(4u, i), *results_[i]));
            BindlessResourceAccess sampled;
            sampled.resource = images_[i]->id();
            sampled.kind = BindlessAccessKind::Texture;
            sampled.stages = ShaderStage::Compute;
            sampled.pipelineStages = PipelineStage::ComputeShader;
            sampled.state = ResourceState::ShaderRead;
            sampled.access = ResourceAccess::ShaderRead;
            sampled.textureRange = defaultTextureSubresourceRange(images_[i]->desc());
            accesses_.push_back(sampled);
            auto storage = sampled;
            storage.resource = outputs_[i]->id();
            storage.state = ResourceState::ShaderWrite;
            storage.access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
            storage.textureRange = defaultTextureSubresourceRange(outputs_[i]->desc());
            accesses_.push_back(storage);
            BindlessResourceAccess result;
            result.resource = results_[i]->id();
            result.kind = BindlessAccessKind::Buffer;
            result.stages = ShaderStage::Compute;
            result.pipelineStages = PipelineStage::ComputeShader;
            result.state = ResourceState::ShaderWrite;
            result.access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
            result.bufferSize = sizeof(initialResults);
            accesses_.push_back(result);
        }
        check(snapshot_->seal(*device_));
        accesses_.push_back(snapshot_->metadataAccess());
        accesses_.push_back(snapshot_->faultLatchAccess());
        accesses_.push_back(snapshot_->faultReadbackAccess());
        authorization_ = extension_->authorize(package_, *snapshot_, diagnostic_);
        if (comparison || wrongSampledFormat) {
            assert(!authorization_
                && diagnostic_
                    == (comparison ? "RHI-METAL-BINDLESS-COMPUTE-IMAGE-SAMPLER-UNSUPPORTED"
                                   : "RHI-METAL-BINDLESS-COMPUTE-IMAGE-VIEW-UNSUPPORTED"));
            std::puts(comparison ? "actualComparisonSampler: rejected"
                                 : "actualSampledR32Format: rejected");
        } else {
            if (!authorization_)
                std::fprintf(stderr, "%s\n", diagnostic_.c_str());
            assert(authorization_);
        }
    }

    void run(bool faulted, bool abandon, bool delayed = true, bool explicitFence = true)
    {
        auto pipeline = core::shareUnique(extension_->createComputePipeline(
            authorization_, faulted ? "faultMain" : "cleanMain", diagnostic_));
        if (!pipeline)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(pipeline);
        BindlessComputeBindingBudget budget{1u, 1024u * 1024u, 1024u * 1024u, 4u, 65536u};
        BindlessComputeBindingPool pool(*extension_, budget);
        auto binding = pool.prepare(
            {true, snapshot_, pipeline, snapshot_->plan(), snapshot_->generation()}, diagnostic_);
        if (!binding)
            std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        assert(binding);
        assert(binding->argumentBufferCount() == 4u
            && binding->argumentBufferBytes() <= budget.maxArgumentBufferBytes);
        auto limited = budget;
        limited.maxArgumentBuffers = 3u;
        BindlessComputeBindingPool shortPool(*extension_, limited);
        assert(!shortPool.prepare(
                   {true, snapshot_, pipeline, snapshot_->plan(), snapshot_->generation()},
                   diagnostic_)
            && shortPool.liveBindings() == 0u && shortPool.chargedBytes() == 0u);
        limited = budget;
        limited.maxArgumentBufferBytes = binding->argumentBufferBytes() - 1u;
        BindlessComputeBindingPool shortBytes(*extension_, limited);
        assert(!shortBytes.prepare(
                   {true, snapshot_, pipeline, snapshot_->plan(), snapshot_->generation()},
                   diagnostic_)
            && shortBytes.liveBindings() == 0u && shortBytes.chargedBytes() == 0u);
        auto encoder = device_->createCommandEncoder({"IndexedImages", 1u, 1u});
        assert(encoder);
        auto pass = encoder->beginComputePass({"IndexedImages"});
        assert(pass);
        check(pass->setPipeline(*pipeline));
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        assert(indexed);
        assert(!indexed->bindPreparedComputeBinding(binding, BindlessAccessArray(nullptr, 0u))
                .success);
        const BindlessAccessArray accesses(accesses_.data(), accesses_.size());
        const auto sampled = accesses_[0u];
        accesses_[0u].resource = {};
        assert(!indexed->bindPreparedComputeBinding(binding, accesses).success);
        accesses_[0u] = sampled;
        const auto storage = accesses_[1u];
        accesses_[1u].access = ResourceAccess::ShaderRead;
        assert(!indexed->bindPreparedComputeBinding(binding, accesses).success);
        accesses_[1u] = storage;
        check(indexed->bindPreparedComputeBinding(binding, accesses));
        auto prepared = metalRhiPreparedComputePayloadFor(*binding);
        assert(prepared);
        const auto native = metalRhiNativePayloadFor(*encoder);
        assert(native);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 32u; ++i)
            assert(native->recordPreparedComputeBinding(prepared, binding, diagnostic_));
        check(pass->dispatch(1u, 1u, 1u));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        check(pass->end());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        assert(native->stats()->nativeBindlessFaultOutcomePreparationCount == 1u
            && native->stats()->nativeBindlessFaultOutcomeConsumeCount == 1u);
        const auto rowBytes = kTextureUploadRowPitchAlignment;
        auto readback = device_->createBuffer({"IndexedImageReadback",
            2u * rowBytes + 2u * sizeof(initialResults),
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        assert(readback);
        for (unsigned i = 0u; i < outputs_.size(); ++i) {
            check(encoder->transition(
                *outputs_[i], ResourceState::ShaderWrite, ResourceState::CopySrc));
            check(encoder->transition(
                *results_[i], ResourceState::ShaderWrite, ResourceState::CopySrc));
        }
        auto copy = encoder->beginTransferPass({"IndexedImageReadback"});
        assert(copy);
        for (unsigned i = 0u; i < outputs_.size(); ++i) {
            check(copy->copyTextureToBuffer({outputs_[i]->id(),
                readback->id(),
                defaultTextureSubresourceRange(outputs_[i]->desc()),
                i * rowBytes,
                rowBytes,
                1u,
                outputs_[i]->desc().extent}));
            check(copy->copyBufferToBuffer({results_[i]->id(),
                readback->id(),
                0u,
                2u * rowBytes + i * sizeof(initialResults),
                sizeof(initialResults)}));
        }
        check(copy->end());
        auto commands = encoder->finish();
        assert(commands);
        auto nativeCommands = metalRhiNativePayloadFor(*commands);
        assert(nativeCommands);
        const auto generation = snapshot_->generation();
        std::weak_ptr<const BindlessPreparedComputeBinding> observed = binding;
        std::weak_ptr<const MetalRhiNativePreparedComputeBindings> observedPrepared = prepared;
        std::weak_ptr<MetalRhiNativeSamplerPayload> observedSampler =
            metalRhiNativePayloadFor(*samplers_[0]);
        binding.reset();
        pipeline.reset();
        pass.reset();
        encoder.reset();
        snapshot_.reset();
        snapshots_.reset();
        authorization_.reset();
        package_.reset();
        for (unsigned i = 0u; i < images_.size(); ++i) {
            imageViews_[i].reset();
            outputViews_[i].reset();
            images_[i].reset();
            outputs_[i].reset();
            results_[i].reset();
            samplers_[i].reset();
        }
        // The local prepared pointer is released before observing completion ownership.
        prepared.reset();
        assert(!observed.expired() && !observedSampler.expired() && pool.liveBindings() == 1u);
        if (abandon) {
            commands.reset();
            assert(observed.expired() && pool.liveBindings() == 0u && pool.chargedBytes() == 0u);
            assert(observedPrepared.expired() && observedSampler.expired());
            assert(nativeCommands->bindlessFaultReport(generation).status
                == BindlessFaultValidationStatus::Abandoned);
            std::puts("imagePreparationCancellation: passed");
            return;
        }
        auto timeline = device_->createTimelineSemaphore(0u);
        auto fence = explicitFence ? device_->createFence() : core::UniquePtr<GpuFence>{};
        auto completion = nativeCommands->preparedCompletionState();
        assert(timeline && completion && !completion->isComplete() && (fence || !explicitFence));
        const auto submitAllocations = core::defaultCpuAllocator().stats().allocationCount;
        check(device_->queue().submit(*commands,
            delayed ? timeline.get() : nullptr,
            delayed ? 1u : 0u,
            nullptr,
            0u,
            fence.get()));
        // Query-free explicit-fence submission allocates only the actual wait
        // command owner; resource and fault completion state is preallocated.
        assert(core::defaultCpuAllocator().stats().allocationCount
            == submitAllocations + (delayed ? 1u : 0u));
        commands.reset();
        if (delayed) {
            assert((fence ? !fence->isSignaled() : !completion->isComplete()) && !observed.expired()
                && pool.liveBindings() == 1u);
            assert(nativeCommands->bindlessFaultReport(generation).status
                == BindlessFaultValidationStatus::Pending);
            timeline->signal(1u);
        }
        if (fence)
            fence->wait();
        else
            completion->wait();
        assert(observed.expired() && pool.liveBindings() == 0u && pool.chargedBytes() == 0u);
        assert(observedPrepared.expired() && observedSampler.expired());
        const auto report = nativeCommands->bindlessFaultReport(generation);
        assert(report.status
                == (faulted ? BindlessFaultValidationStatus::Faulted
                            : BindlessFaultValidationStatus::Clean)
            && report.faultBits == (faulted ? 1u : 0u));
        std::array<std::uint8_t, 2u * kTextureUploadRowPitchAlignment + 2u * sizeof(initialResults)>
            bytes{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
            bytes.data(),
            bytes.size(),
            0u,
            bytes.size(),
            diagnostic_));
        const std::array<float, 2> expectedStorage{0.75f, 1.75f};
        const std::array<std::array<float, 4>, 2> expectedValues{
            {{1.0f, 0.5f, 0.25f, 0.0f}, {0.0f, 1.0f, 0.75f, 1.0f}}};
        for (unsigned i = 0u; i < 2u; ++i) {
            float stored;
            std::memcpy(&stored, bytes.data() + i * rowBytes, sizeof(stored));
            assert(stored == expectedStorage[i]);
            std::array<std::uint32_t, 7> actual;
            std::memcpy(
                actual.data(), bytes.data() + 2u * rowBytes + i * sizeof(actual), sizeof(actual));
            assert(actual[1u] == 2u && actual[3u] == 1u && actual[6u] == initialResults[6u]);
            constexpr std::array<unsigned, 4> valueWords{0u, 2u, 4u, 5u};
            for (unsigned j = 0u; j < valueWords.size(); ++j) {
                float value;
                std::memcpy(&value, &actual[valueWords[j]], sizeof(value));
                assert(value == expectedValues[i][j]);
            }
        }
        std::printf(
            "imageData: storage [0.75,1.75], independent nearest/linear and mip results, faultBits %u; typed completion passed\n",
            report.faultBits);
    }

private:
    void upload()
    {
        auto encoder = device_->createCommandEncoder({"ImageFixtureUpload"});
        assert(encoder);
        for (unsigned i = 0u; i < images_.size(); ++i) {
            check(
                encoder->transition(*images_[i], ResourceState::Undefined, ResourceState::CopyDst));
            check(encoder->transition(
                *outputs_[i], ResourceState::Undefined, ResourceState::CopyDst));
        }
        auto pass = encoder->beginTransferPass({"ImageFixtureUpload"});
        assert(pass);
        for (unsigned i = 0u; i < images_.size(); ++i) {
            for (unsigned mip = 0u; mip < 2u; ++mip) {
                std::array<std::uint8_t, kTextureUploadRowPitchAlignment> pixels{};
                pixels[0u] = mip == 0u ? (i == 0u ? 255u : 0u) : (i == 0u ? 0u : 255u);
                pixels[3u] = 255u;
                if (mip == 0u) {
                    pixels[4u] = i == 0u ? 0u : 255u;
                    pixels[7u] = 255u;
                }
                if (images_[i]->desc().format == TextureFormat::R32Float) {
                    const std::array<float, 2> values{i == 0u ? 1.0f : 0.0f, i == 0u ? 0.0f : 1.0f};
                    std::memcpy(
                        pixels.data(), values.data(), (mip == 0u ? 2u : 1u) * sizeof(float));
                }
                UploadTextureDesc upload;
                upload.label = "ImageFixtureMip";
                upload.destinationTexture = images_[i]->id();
                upload.format = images_[i]->desc().format;
                upload.extent = {mip == 0u ? 2u : 1u, 1u, 1u};
                upload.subresources.baseMipLevel = mip;
                upload.bytesPerRow = kTextureUploadRowPitchAlignment;
                upload.rowsPerImage = 1u;
                upload.pixelData = pixels.data();
                upload.pixelDataSize = pixels.size();
                check(pass->uploadTexture(upload));
            }
            std::array<std::uint8_t, kTextureUploadRowPitchAlignment> initial{};
            const float value = i == 0u ? 0.25f : 0.75f;
            std::memcpy(initial.data(), &value, sizeof(value));
            UploadTextureDesc upload;
            upload.label = "StorageFixtureInitialValue";
            upload.destinationTexture = outputs_[i]->id();
            upload.format = TextureFormat::R32Float;
            upload.extent = outputs_[i]->desc().extent;
            upload.bytesPerRow = kTextureUploadRowPitchAlignment;
            upload.rowsPerImage = 1u;
            upload.pixelData = initial.data();
            upload.pixelDataSize = initial.size();
            check(pass->uploadTexture(upload));
        }
        check(pass->end());
        for (unsigned i = 0u; i < images_.size(); ++i) {
            check(encoder->transition(
                *images_[i], ResourceState::CopyDst, ResourceState::ShaderRead));
            check(encoder->transition(
                *outputs_[i], ResourceState::CopyDst, ResourceState::ShaderWrite));
        }
        auto commands = encoder->finish();
        assert(commands);
        check(device_->queue().submit(*commands));
        device_->waitIdle();
    }

    static constexpr std::array<std::uint32_t, 7> initialResults{
        0u, 0u, 0u, 0u, 0u, 0u, 0xabcdef01u};
    core::UniquePtr<GpuDevice> device_;
    BindlessCrossExtension* extension_ = nullptr;
    std::shared_ptr<const BindlessShaderPackage> package_;
    core::UniquePtr<BindlessSnapshotPool> snapshots_;
    std::shared_ptr<BindlessSnapshot> snapshot_;
    std::shared_ptr<const BindlessAuthorization> authorization_;
    std::array<core::UniquePtr<GpuTexture>, 2> images_, outputs_;
    std::array<core::UniquePtr<GpuTextureView>, 2> imageViews_, outputViews_;
    std::array<core::UniquePtr<GpuSampler>, 2> samplers_;
    std::array<core::UniquePtr<GpuBuffer>, 2> results_;
    core::pmr::Vector<BindlessResourceAccess> accesses_;
    core::pmr::String diagnostic_;
};
} // namespace

int main(int argc, char** argv)
{
    core::pmr::String diagnostic;
    const bool captureColor =
        argc == 4 && std::string_view(argv[1]) == "--capture-original-completed-color";
    if ((argc == 3 && std::string_view(argv[1]) == "--original-completed-color") || captureColor) {
        auto color = BindlessShaderPackage::load(
            argv[2], {1024u * 1024u, 4u * 1024u * 1024u, 32u, 2u, 10u, 4u}, diagnostic);
        if (!color)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(color);
        MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice =
            desc.capabilityContract.queriedExtendedFeatures = true;
        desc.memoryBudgetBytes = 16u * 1024u * 1024u;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        desc.enableBindlessCompute = desc.enableBindlessComputeImages = true;
        core::UniquePtr<GpuDevice> captureDevice;
        if (captureColor) {
            captureDevice = createMetalRhiDevice(desc);
            assert(captureDevice);
            FrameCaptureRequest request;
            request.outputPath = argv[3];
            const auto started = captureDevice->beginFrameCapture(request);
            if (!started.success())
                std::fprintf(stderr, "%s\n", started.diagnostics.c_str());
            assert(started.success());
        }
        for (const bool fault : {false, true})
            for (unsigned terminal = 0u; terminal < 5u; ++terminal) {
                if (captureColor && terminal)
                    continue;
                test::MetalCompletedTextureRootFixture run;
                run.fault = fault;
                run.terminal = terminal;
                if (captureColor)
                    run.repeatAfterCompletion = false;
                run.arrayTextures =
                    color->artifact().physicalLayout.bindings.front().textureViewDimension
                    == TextureViewDimension::Texture2DArray;
                test::validateOriginalBindlessRootSource(color,
                    createMetalRhiDevice(desc),
                    fault ? "faultMain" : "cleanMain",
                    false,
                    run,
                    run);
            }
        if (captureColor) {
            const auto ended = captureDevice->endFrameCapture();
            assert(ended.success());
            std::printf("BINDLESS_METAL_PUBLIC_COLOR8_CAPTURE %s\n", argv[3]);
        }
        return 0;
    }
    auto package = BindlessShaderPackage::load(ARP_METAL_IMAGES_PUBLICATION,
        {1024u * 1024u, 4u * 1024u * 1024u, 32u, 2u, 10u, 4u},
        diagnostic);
    if (!package)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(package);
    assert(!validateMetalBindlessComputeArtifact(package->artifact()).success);
    check(validateMetalBindlessComputeArtifact(package->artifact(), true));
    if (argc == 2 && std::string_view(argv[1]) == "--original-image-root") {
        MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.memoryBudgetBytes = 16u * 1024u * 1024u;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        desc.enableBindlessCompute = desc.enableBindlessComputeImages = true;
        ImageExecutionFixture::verifySamplerNativeOwnerAdmission(desc);
        arp::test::validateOriginalBindlessRootSource(package,
            createMetalRhiDevice(desc),
            "cleanMain",
            false,
            arp::test::MetalOriginalBindlessRootFixture{});
        return 0;
    }
    for (unsigned mode = 0u; mode < 10u; ++mode) {
        auto invalid = package->artifact();
        auto& image = invalid.physicalLayout.bindings[0u];
        auto& sampler = invalid.physicalLayout.bindings[1u];
        auto& storage = invalid.physicalLayout.bindings[2u];
        if (mode == 0u)
            image.stages = ShaderStage::Fragment;
        if (mode == 1u)
            image.depthTexture = true;
        if (mode == 2u)
            image.textureViewDimension = TextureViewDimension::Texture2DArray;
        if (mode == 3u)
            image.textureViewDimensionConstrained = false;
        if (mode == 4u)
            sampler.comparisonSampler = true;
        if (mode == 5u)
            storage.storageTextureFormat = TextureFormat::Rgba16Float;
        if (mode == 6u)
            storage.storageTextureAccess = StorageTextureAccess::WriteOnly;
        if (mode == 7u)
            invalid.tables[0u].operations = BindlessOperation::SampleCompare;
        if (mode == 8u)
            invalid.tables[0u].samplerTableId = 4u;
        if (mode == 9u)
            invalid.tables[2u].samplerTableId = 2u;
        assert(!validateMetalBindlessComputeArtifact(invalid, true).success);
    }
    ImageExecutionFixture(package, true);
    ImageExecutionFixture(package, false, true);
    ImageExecutionFixture(package).run(false, false);
    ImageExecutionFixture(package).run(true, false);
    ImageExecutionFixture(package).run(false, true);
    ImageExecutionFixture(package).run(false, false, false);
    ImageExecutionFixture(package).run(false, false, false, false);
    ImageExecutionFixture(package).run(true, false, true, false);
}
