#pragma once
#include "canonical_initialized_buffer_staging_contract_fixture.h"
#include "original_command_texture_upload_inputs_fixture.h"
#include "original_command_texture_upload_packing_source_fixture.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_canonical_upload_cold_fixture.h"
#include "webgpu_canonical_upload_joins_fixture.h"
#include "webgpu_canonical_upload_projection_fixture.h"

namespace arp::test {
class WebGpuCanonicalTextureUploadSourceFixture final {
    using Source = rhi_webgpu::WebGpuCanonicalTextureUploadSource;
    using Buffer = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
    using Binding = rhi::OriginalCommandTextureUploadPackingSource;
    static constexpr std::uint64_t metadataBytes = 65536u,
                                   packedBytes = 3u * rhi::kTextureUploadRowPitchAlignment,
                                   frame = 99u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "canonical texture source actual=%s expected=%s\n",
                actual ? actual : "<none>",
                expected);
        assert(actual && std::string_view(actual) == expected);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Context final {
    public:
        template <class Factory>
        explicit Context(Factory&& create,
            bool content = false,
            bool originalStagingSource = false,
            std::uint32_t buffers = 1u,
            bool sampledInput = false,
            bool completedDescriptor = false)
            : device(create())
        {
            assert(device);
            for (auto& quota : quotas)
                quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            assert(!device->prepareOriginalDeviceLifetime(
                {1u, true, 1u, 2u, metadataBytes}, quotas[6]));
            assert(!device->prepareOriginalDeviceNativeLoss(
                {{1u, true, 1u, 1u, metadataBytes}, 1u}, quotas[8]));
            const char* diagnostic = nullptr;
            auto observed = device->observeOriginalDeviceNativeLoss(diagnostic);
            assert(observed);
            loss.emplace(*observed);
            const auto views = sampledInput ? 2u : 0u;
            rhi::OriginalResourceRegistrationBudget registration{1u,
                true,
                buffers,
                1u,
                views,
                rhi::OriginalResourceRegistrationContract::fixedOwnerCount
                    + (buffers + 1u + views)
                        * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount,
                metadataBytes};
            if (completedDescriptor) {
                registration.contractVersion = 9u;
                registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
                registration.maxRetainedOwners += 2u
                    * (rhi::OriginalResourceRegistrationContract::perResourceOwnerCount
                        + rhi::OriginalResourceRegistrationContract::perDescriptorOwnerCount);
                assert(!device->prepareOriginalDescriptorNativeLossRegistration(
                    {3u, true}, registration, quotas[7], *loss));
            } else
                assert(!device->prepareOriginalResourceRegistration(registration, quotas[7]));
            prepareTexture(content, originalStagingSource, sampledInput, quotas[5]);
        }

        // Reuse the original upload assembly on a caller-owned registered
        // device. This constructor never creates, registers or retires a device.
        Context(rhi::GpuDevice& supplied,
            const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
            const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota,
            std::uint32_t layers = 1u,
            rhi::TextureFormat format = rhi::TextureFormat::Rgba8Unorm,
            bool cube = false)
            : borrowedDevice_(&supplied), layers_(layers), format_(format), cube_(cube)
        {
            for (auto& quota : quotas)
                quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            quotas[5] = sourceQuota;
            quotas[11] = resourceQuota;
            const char* diagnostic = nullptr;
            auto observed = supplied.observeOriginalDeviceNativeLoss(diagnostic);
            assert(observed && !diagnostic);
            loss.emplace(*observed);
            prepareTexture(true, true, true, resourceQuota);
        }

        rhi::GpuDevice& gpu() const
        {
            assert(device || borrowedDevice_);
            return device ? *device : *borrowedDevice_;
        }

        Context(rhi::GpuDevice& supplied,
            const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
            const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota,
            rhi::TextureDesc desc,
            rhi::UploadTextureDesc upload)
            : borrowedDevice_(&supplied), layers_(desc.arrayLayerCount), format_(desc.format),
              cube_(desc.cubeCompatible)
        {
            for (auto& quota : quotas)
                quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            quotas[5] = sourceQuota;
            quotas[11] = resourceQuota;
            const char* diagnostic = nullptr;
            auto observed = supplied.observeOriginalDeviceNativeLoss(diagnostic);
            assert(observed && !diagnostic);
            loss.emplace(*observed);
            desc.initialState = rhi::ResourceState::CopyDst;
            desc.usage = desc.usage | rhi::TextureUsage::CopyDst | rhi::TextureUsage::CopySrc
                | rhi::TextureUsage::Sampled;
            contentQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            core::UniquePtr<rhi::GpuTexture> texture;
            {
                core::CpuMemoryQuotaScope scope(resourceQuota);
                texture = gpu().createOriginalContentTexture(desc,
                    {1u, true, 1u, 1u, upload.pixelDataSize, 4u, metadataBytes},
                    contentQuota,
                    diagnostic);
            }
            if (diagnostic)
                std::fprintf(stderr, "authored texture creation: %s\n", diagnostic);
            assert(texture && !diagnostic);
            auto target = rhi::OriginalCommandTextureLease::prepareOriginal(
                texture, {1u, true, 2u, sourceQuota->limitBytes()}, sourceQuota, diagnostic);
            assert(target && !diagnostic);
            upload.destinationTexture = target->resource();
            prepareUploadBinding(target, std::move(upload), true, true);
        }

        void prepareTexture(bool content,
            bool originalStagingSource,
            bool sampledInput,
            const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota)
        {
            const char* diagnostic = nullptr;
            rhi::TextureDesc desc;
            desc.extent = {2u, 2u, 1u};
            desc.format = format_;
            desc.mipLevelCount = 2u;
            desc.arrayLayerCount = layers_;
            desc.cubeCompatible = cube_;
            desc.usage = rhi::TextureUsage::CopyDst | rhi::TextureUsage::CopySrc;
            desc.initialState = rhi::ResourceState::CopyDst;
            if (sampledInput)
                desc.usage = desc.usage | rhi::TextureUsage::Sampled;
            core::UniquePtr<rhi::GpuTexture> texture;
            if (content)
                contentQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            {
                core::CpuMemoryQuotaScope resource(resourceQuota);
                texture = content ? gpu().createOriginalContentTexture(desc,
                                        {1u,
                                            true,
                                            1u,
                                            1u,
                                            5u * rhi::bytesPerPixel(format_) * layers_,
                                            4u,
                                            metadataBytes},
                                        contentQuota,
                                        diagnostic)
                                  : gpu().createTexture(desc);
            }
            if (!texture && diagnostic)
                std::fprintf(stderr, "canonical texture creation: %s\n", diagnostic);
            assert(texture);
            auto target = rhi::OriginalCommandTextureLease::prepareOriginal(texture,
                {1u,
                    true,
                    rhi::OriginalCommandTextureLease::retainedOwnerCount,
                    quotas[5]->limitBytes()},
                quotas[5],
                diagnostic);
            if (diagnostic)
                std::fprintf(stderr, "canonical texture lease: %s\n", diagnostic);
            assert(target && !diagnostic);
            prepareBinding(target, 0u, originalStagingSource);
        }

        void prepareBinding(const std::shared_ptr<const rhi::OriginalCommandTextureLease>& target,
            std::uint8_t offset,
            bool originalStagingSource,
            void (*initializePixels)(
                std::uint32_t, std::uint32_t, core::pmr::Vector<std::uint8_t>&) = nullptr,
            std::uint32_t image = 0u)
        {
            const char* diagnostic = nullptr;
            const auto& desc = target->originalObject()->desc();
            assert(desc.extent.width == 2u && desc.extent.height == 2u && desc.extent.depth == 1u
                && desc.mipLevelCount == 2u && desc.arrayLayerCount == layers_
                && (desc.format == rhi::TextureFormat::Rgba8Unorm
                    || desc.format == rhi::TextureFormat::Rgba16Float));
            const auto pixelBytes = static_cast<std::uint32_t>(rhi::bytesPerPixel(desc.format));
            core::pmr::Vector<std::uint8_t> pixels(5u * pixelBytes * layers_);
            for (std::size_t i = 0u; i < pixels.size(); ++i)
                pixels[i] = static_cast<std::uint8_t>(offset + i + 1u);
            if (initializePixels)
                initializePixels(image, layers_, pixels);
            rhi::UploadTextureDesc upload;
            upload.destinationTexture = target->resource();
            upload.extent = target->originalObject()->desc().extent;
            upload.format = target->originalObject()->desc().format;
            upload.bytesPerRow = 2u * pixelBytes;
            upload.rowsPerImage = 2u;
            upload.subresources.mipLevelCount = 2u;
            upload.subresources.arrayLayerCount = layers_;
            upload.pixelData = pixels.data();
            upload.pixelDataSize = pixels.size();
            upload.frameSerial = frame;
            upload.resourceGeneration = 29u;
            upload.mipLayouts = {{0u, {2u, 2u, 1u}, 2u * pixelBytes, 2u, 0u},
                {1u, {1u, 1u, 1u}, pixelBytes, 1u, 4u * pixelBytes * layers_}};
            prepareUploadBinding(target, std::move(upload), originalStagingSource);
            std::fill(pixels.begin(), pixels.end(), 0xe7u);
        }

        void prepareUploadBinding(
            const std::shared_ptr<const rhi::OriginalCommandTextureLease>& target,
            rhi::UploadTextureDesc upload,
            bool originalStagingSource,
            bool verifyBudgetBoundaries = false)
        {
            const char* diagnostic = nullptr;
            const auto mipCount = target->originalObject()->desc().mipLevelCount;
            const auto pixelBytes = upload.pixelDataSize;
            const auto pixelCpuBytes = quotas[5]->limitBytes();
            upload.frameSerial = frame;
            upload.resourceGeneration = 29u;
            const rhi::TextureUploadInputSnapshotBudget inputBudget{
                1u, true, mipCount, 0u, pixelBytes, metadataBytes, metadataBytes, pixelCpuBytes};
            if (verifyBudgetBoundaries) {
                auto shortBudget = inputBudget;
                --shortBudget.maxPixelBytes;
                const auto baseline = quotas[5]->liveBytes();
                assert(!rhi::TextureUploadInputSnapshot::prepareOriginal(
                    upload, shortBudget, quotas[4], quotas[4], quotas[5], diagnostic));
                assert(diagnostic && !quotas[4]->liveBytes() && quotas[5]->liveBytes() == baseline);
            }
            auto input = rhi::TextureUploadInputSnapshot::prepareOriginal(
                upload, inputBudget, quotas[4], quotas[4], quotas[5], diagnostic);
            if (diagnostic)
                std::fprintf(stderr, "authored texture input: %s\n", diagnostic);
            assert(input);
            auto original = rhi::OriginalCommandTextureUploadSource::prepareOriginal(
                input, target, {1u, true, 5u, quotas[5]->limitBytes()}, quotas[5], diagnostic);
            assert(original);
            std::uint64_t packedSize = 0u;
            assert(!rhi::semantics::TextureUploadLayoutValidation::inspectPackedBytes(*input,
                rhi::TextureUploadPacking::Destination::capture(target->originalObject()->desc()),
                packedSize));
            const auto packedCpuBytes = packedSize + metadataBytes;
            quotas[3] = core::makeShared<core::CpuMemoryQuota>(packedCpuBytes);
            quotas[10] = core::makeShared<core::CpuMemoryQuota>(packedCpuBytes);
            const rhi::TextureUploadPackingBudget packingBudget{
                1u, true, mipCount, packedSize, metadataBytes, packedCpuBytes};
            if (verifyBudgetBoundaries) {
                auto shortBudget = packingBudget;
                --shortBudget.maxPackedBytes;
                assert(!rhi::TextureUploadPacking::prepareOriginal(input,
                    target->originalObject()->desc(),
                    shortBudget,
                    quotas[2],
                    quotas[3],
                    diagnostic));
                assert(diagnostic && !quotas[2]->liveBytes() && !quotas[3]->liveBytes());
            }
            auto packing = rhi::TextureUploadPacking::prepareOriginal(input,
                target->originalObject()->desc(),
                packingBudget,
                quotas[2],
                quotas[3],
                diagnostic);
            assert(packing);
            binding = Binding::prepareOriginal(original,
                packing,
                {1u,
                    true,
                    mipCount,
                    Binding::retainedOwnerCount,
                    pixelBytes,
                    packedSize,
                    metadataBytes},
                quotas[1],
                diagnostic);
            assert(binding);
            rhi::BufferDesc bufferDesc;
            bufferDesc.sizeBytes = packedSize;
            bufferDesc.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
            bufferDesc.memory = rhi::MemoryUsage::CpuToGpu;
            bufferDesc.initialState = rhi::ResourceState::CopySrc;
            core::UniquePtr<rhi::GpuBuffer> candidate;
            {
                core::CpuMemoryQuotaScope resource(quotas[11]);
                candidate = gpu().createBuffer(bufferDesc);
            }
            assert(candidate);
            const Buffer::Budget bufferBudget{1u,
                true,
                1u,
                rhi::OriginalCommandInitializedBufferStagingContract::callbackCount,
                rhi::OriginalCommandInitializedBufferStagingContract::retainedOwnerBound,
                packedSize,
                metadataBytes,
                packedCpuBytes,
                metadataBytes,
                quotas[originalStagingSource ? 5u : 12u]->limitBytes(),
                metadataBytes};
            buffer = Buffer::prepareOriginal(gpu(),
                candidate,
                *loss,
                packing->packedBytes().data(),
                packedSize,
                frame,
                bufferBudget,
                {quotas[9],
                    quotas[10],
                    quotas[11],
                    quotas[originalStagingSource ? 5u : 12u],
                    quotas[13]},
                diagnostic);
            assert(buffer && !candidate);
            if (verifyBudgetBoundaries)
                std::printf(
                    "BINDLESS_WEBGPU_AUTHORED_UPLOAD_BUDGET_OK mips%u layers%u pixels%llu packed%llu exact-less no-authority-grant\n",
                    mipCount,
                    layers_,
                    static_cast<unsigned long long>(pixelBytes),
                    static_cast<unsigned long long>(packedSize));
        }

        auto make(const char*& diagnostic)
        {
            auto b = budget();
            b.maxPackedBytes = binding->packing()->packedBytes().size();
            b.maxDestinationTexelBytes = binding->originalInput()->pixelBytes().size();
            return Source::prepareOriginal(gpu(), binding, buffer, *loss, b, quotas[0], diagnostic);
        }

        void release()
        {
            binding.reset();
            buffer.reset();
            loss.reset();
            device.reset();
        }

        std::array<std::shared_ptr<core::CpuMemoryQuota>, 14> quotas;
        std::shared_ptr<core::CpuMemoryQuota> contentQuota;
        core::UniquePtr<rhi::GpuDevice> device;
        std::optional<rhi::OriginalDeviceNativeLossObservation> loss;
        std::shared_ptr<const Binding> binding;
        std::shared_ptr<const Buffer> buffer;

    private:
        rhi::GpuDevice* borrowedDevice_ = nullptr;
        std::uint32_t layers_ = 1u;
        rhi::TextureFormat format_ = rhi::TextureFormat::Rgba8Unorm;
        bool cube_ = false;
    };

    static inline core::UniquePtr<Context> lossContext_;
    static inline std::shared_ptr<const Source> lossSource_;
    static inline core::UniquePtr<Context> contentLossContext_;
    static inline std::shared_ptr<const rhi_webgpu::WebGpuCanonicalTextureContentUploadSource>
        contentLossSource_;
    static inline std::shared_ptr<core::CpuMemoryQuota> contentLossQuota_;
    static inline core::UniquePtr<Context> joinLossContext_;
    static inline std::shared_ptr<const rhi_webgpu::WebGpuCanonicalUploadJoins> lossJoin_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> joinLossQuotas_;
    static inline std::array<core::UniquePtr<Context>, 5> executionLossContexts_;
    static inline std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 3>, 5>
        executionLossQuotas_;

#endif
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    using BorrowedContext = Context;
#endif
    template <class Factory, class Continuation>
    static void verifyCompletedDescriptorPublication(Factory&& create, Continuation&& continuation)
    {
        verifyJoinsWithContinuation(
            create, false, 5u, std::forward<Continuation>(continuation), true);
    }

    static Source::Budget budget()
    {
        return {1u, true, Source::retainedOwnerBound, 1u, packedBytes, 20u, metadataBytes};
    }

    template <class Factory, class QueueWait, class MapRead>
    static void verifyCompletedShaderTextureInput(Factory&& create, QueueWait&& wait, MapRead&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        verifyJoinsWithContinuation(
            create, false, 5u, [&](auto& root, auto& context, auto& source, auto& original) {
                WebGpuCanonicalUploadProjectionFixture::verifyExecution(root,
                    *context.device,
                    source,
                    *context.loss,
                    original,
                    wait,
                    map,
                    0u,
                    &context.device);
            });
        std::puts(
            "BINDLESS_RHI_COMPLETED_SHADER_TEXTURE_INPUT_RETIRE_OK original-upload resources-quota-zero source-caller-drop completion-shader-authority-closed");
#else
        (void)create;
        (void)wait;
        (void)map;
#endif
    }

    template <class Factory, class QueueWait, class MapRead>
    static void verify(Factory&& create, QueueWait&& wait, MapRead&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        Context context(create);
        const char* diagnostic = nullptr;
        const auto make = [&] {
            return context.make(diagnostic);
        };
        CpuQuotaFactoryFixture pressure(context.quotas[0]);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        {
            core::CpuMemoryQuotaLease full(context.quotas[0],
                context.quotas[0]->limitBytes() - context.quotas[0]->liveBytes());
            assert(full.charged());
            assert(!make());
            is(diagnostic, "RHI-CANONICAL-TEXTURE-SOURCE-ALLOCATION");
        }
        auto source = make();
        assert(source);
        assert(source->destinationStateExpectation().resourceGeneration()
            == context.binding->originalDestinationLease()
                ->originalObject()
                ->originalResourceRegistration()
                .generation());
        std::array<std::uint8_t, packedBytes> altered;
        std::memcpy(
            altered.data(), context.binding->packing()->packedBytes().data(), altered.size());
        ++altered.back();
        is(context.buffer->compareFrozenPayload(altered.data(), altered.size(), frame),
            "RHI-CANONICAL-UPLOAD-SOURCE-PAYLOAD-JOIN");
        is(context.buffer->compareFrozenPayload(
               context.binding->packing()->packedBytes().data(), packedBytes, frame + 1u),
            "RHI-CANONICAL-UPLOAD-SOURCE-PAYLOAD-JOIN");
        is(source->stagingReadyDiagnostic(), "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY");
        rhi::semantics::TextureUploadPackedCopy output;
        output.contentGeneration = 123u;
        is(Source::mapPackedCopyOriginal(source, context.binding, 0u, output),
            "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY");
        assert(output.contentGeneration == 123u);
        const auto originalAllocations = core::defaultCpuAllocator().stats().allocationCount;
        auto bad = budget();
        --bad.maxPackedBytes;
        assert(!Source::prepareOriginal(*context.device,
            context.binding,
            context.buffer,
            *context.loss,
            bad,
            context.quotas[0],
            diagnostic));
        is(diagnostic, "RHI-CANONICAL-TEXTURE-SOURCE-BYTE-CAPACITY");
        bad = budget();
        --bad.maxDestinationTexelBytes;
        assert(!Source::prepareOriginal(*context.device,
            context.binding,
            context.buffer,
            *context.loss,
            bad,
            context.quotas[0],
            diagnostic));
        is(diagnostic, "RHI-CANONICAL-TEXTURE-SOURCE-TARGET-BYTE-CAPACITY");
        for (const auto domain : {3u, 5u, 9u, 10u, 11u, 12u, 13u}) {
            assert(!Source::prepareOriginal(*context.device,
                context.binding,
                context.buffer,
                *context.loss,
                budget(),
                context.quotas[domain],
                diagnostic));
            is(diagnostic, "RHI-CANONICAL-TEXTURE-SOURCE-QUOTA-DOMAIN");
        }
        auto fake = std::shared_ptr<const Buffer>(context.quotas[0], context.buffer.get());
        assert(!Source::prepareOriginal(*context.device,
            context.binding,
            fake,
            *context.loss,
            budget(),
            context.quotas[0],
            diagnostic));
        is(diagnostic, "RHI-CANONICAL-TEXTURE-SOURCE-BUFFER-OWNER");
        fake.reset();
        assert(core::defaultCpuAllocator().stats().allocationCount == originalAllocations);
        {
            Context foreign(create);
            assert(!Source::prepareOriginal(*context.device,
                context.binding,
                context.buffer,
                *foreign.loss,
                budget(),
                context.quotas[0],
                diagnostic));
            is(diagnostic, "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
            assert(!Source::prepareOriginal(*foreign.device,
                context.binding,
                context.buffer,
                *foreign.loss,
                budget(),
                context.quotas[0],
                diagnostic));
            is(diagnostic, "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
            foreign.release();
            for (const auto& quota : foreign.quotas)
                assert(!quota->liveBytes());
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            context.quotas, [&]() noexcept -> const char* {
                if (const auto* id = Buffer::initialize(context.buffer))
                    return id;
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return context.buffer->callbacksDrained();
                }));
                if (const auto* id = source->stagingReadyDiagnostic())
                    return id;
                is(source->executionDiagnostic(),
                    "RHI-CANONICAL-TEXTURE-STAGING-CONTENT-EXPECTATION-MISSING");
                for (std::size_t i = 0u; i < 2u; ++i) {
                    if (const auto* id =
                            Source::mapPackedCopyOriginal(source, context.binding, i, output))
                        return id;
                    assert(output.bufferOffsetBytes
                        == (i ? 2u * rhi::kTextureUploadRowPitchAlignment : 0u));
                    assert(output.packedFootprintBytes
                        == (i ? 1u : 2u) * rhi::kTextureUploadRowPitchAlignment);
                    assert(output.contentGeneration == 29u && !source->nativeExecutionProven());
                }
                auto forged =
                    std::shared_ptr<const Binding>(context.quotas[0], context.binding.get());
                is(Source::mapPackedCopyOriginal(source, forged, 0u, output),
                    "RHI-CANONICAL-TEXTURE-SOURCE-ORIGINAL-JOIN");
                assert(Source::mapPackedCopyOriginal(source, context.binding, 2u, output));
                return nullptr;
            }));
        auto native =
            rhi_webgpu::webGpuNativePayloadFor(*context.buffer->originalLease()->originalObject());
        assert(native);
        const auto& expected = context.binding->packing()->packedBytes();
        std::array<std::uint8_t, packedBytes> normative{};
        for (std::size_t i = 0u; i < 8u; ++i) {
            normative[i] = static_cast<std::uint8_t>(i + 1u);
            normative[rhi::kTextureUploadRowPitchAlignment + i] = static_cast<std::uint8_t>(i + 9u);
        }
        for (std::size_t i = 0u; i < 4u; ++i)
            normative[2u * rhi::kTextureUploadRowPitchAlignment + i] =
                static_cast<std::uint8_t>(i + 17u);
        assert(!std::memcmp(expected.data(), normative.data(), normative.size()));
        auto bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.size = packedBytes;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        const auto readback = wgpuDeviceCreateBuffer(native->ownerDevice, &bd);
        assert(readback);
        const auto encoder = wgpuDeviceCreateCommandEncoder(native->ownerDevice, nullptr);
        assert(encoder);
        wgpuCommandEncoderCopyBufferToBuffer(
            encoder, native->buffer, 0u, readback, 0u, packedBytes);
        const auto commands = wgpuCommandEncoderFinish(encoder, nullptr);
        const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
        assert(commands && queue);
        wgpuQueueSubmit(queue, 1u, &commands);
        assert(wait(queue) && map(readback, packedBytes));
        const auto* bytes = wgpuBufferGetConstMappedRange(readback, 0u, packedBytes);
        assert(bytes && !std::memcmp(bytes, expected.data(), expected.size()));
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        wgpuCommandEncoderRelease(encoder);
        wgpuCommandBufferRelease(commands);
        wgpuQueueRelease(queue);
        auto target = rhi_webgpu::webGpuNativePayloadFor(
            *context.binding->originalDestinationLease()->originalObject());
        assert(target);
        ++target->extent.width;
        assert(source->stagingReadyDiagnostic());
        --target->extent.width;
        assert(!source->stagingReadyDiagnostic());
        {
            core::MutationJournal journal;
            assert(!target->recordState(rhi::ResourceState::CopySrc, &journal));
            is(source->stagingReadyDiagnostic(), "RHI-ORIGINAL-TEXTURE-STATE-PENDING");
            journal.rollback();
            // Rollback consumes a new revision; restored state cannot revive an
            // earlier immutable expectation. Rebinding is an explicit cold call.
            is(source->stagingReadyDiagnostic(), "RHI-ORIGINAL-TEXTURE-STATE-STALE");
        }
        auto rebound = make();
        assert(rebound && !rebound->stagingReadyDiagnostic());
        assert(!target->recordState(rhi::ResourceState::CopySrc));
        is(rebound->stagingReadyDiagnostic(), "RHI-ORIGINAL-TEXTURE-STATE-STALE");
        target.reset();
        native.reset();
        rebound.reset();
        source.reset();
        context.release();
        for (const auto& quota : context.quotas)
            assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_CANONICAL_TEXTURE_STAGING_OUTPUT_OK peak%llu packed768 mips2 pixels20 padding-zero exact-original-source-buffer actual-native-readback pending-stale-target-refused hot-allocation0 all-fourteen-quotas-zero target-content-execution-closed\n",
            static_cast<unsigned long long>(peak));
        verifyContentBinding(create);
        verifyJoins(create, false);
        verifyJoins(create, true);
        for (unsigned cold = 1u; cold < 5u; ++cold)
            verifyJoins(create, false, cold);
        for (unsigned probe = 0u; probe < 12u; ++probe)
            verifyJoinsWithContinuation(
                create, false, 5u, [&](auto& root, auto& context, auto& source, auto& original) {
                    WebGpuCanonicalUploadProjectionFixture::verifyExecution(root,
                        *context.device,
                        source,
                        *context.loss,
                        original,
                        wait,
                        map,
                        probe,
                        &context.device);
                });
        verifyInterleaved(create, wait, map);
        for (unsigned probe = 0u; probe < 5u; ++probe)
            verifyJoinsWithContinuation(
                create, false, 6u + probe, [&](auto& root, auto& context, auto& source, auto&) {
                    WebGpuCanonicalUploadProjectionFixture::prepareLossExecution(
                        root, *context.device, source, *context.loss, true, probe);
                });

        lossContext_ = core::makeUnique<Context>(create);
        lossSource_ = lossContext_->make(diagnostic);
        assert(lossSource_);
#else
        (void)create;
        (void)wait;
        (void)map;
#endif
    }

private:
    template <class Factory, class QueueWait, class MapRead>
    static void verifyInterleaved(Factory&& create, QueueWait&& wait, MapRead&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Content = rhi_webgpu::WebGpuCanonicalTextureContentUploadSource;
        Context context(create, true, true, 4u);
        const char* diagnostic = nullptr;
        std::array<std::shared_ptr<const Content>, 2> proofs;
        std::array<std::shared_ptr<const rhi::OriginalCommandTextureUploadSource>, 2> uploads;
        std::array<std::shared_ptr<const rhi::OriginalCommandBufferLease>, 2> staging;
        auto target = context.binding->originalDestinationLease();
        auto expected = target->originalObject()->observeOriginalContent();
        assert(expected);
        auto contentProofQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        for (std::size_t i = 0u; i < proofs.size(); ++i) {
            if (i)
                context.prepareBinding(target, 64u, true);
            assert(!Buffer::initialize(context.buffer));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return context.buffer->callbacksDrained();
            }));
            auto source = context.make(diagnostic);
            assert(source);
            proofs[i] = Content::prepareOriginal(*context.device,
                source,
                *expected,
                {1u, true, Content::retainedOwnerBound, metadataBytes},
                contentProofQuota,
                diagnostic);
            assert(proofs[i]);
            uploads[i] = context.binding->originalSource();
            staging[i] = context.buffer->originalLease();
        }
        rhi::BufferDesc desc;
        desc.sizeBytes = 16u;
        desc.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.initialState = rhi::ResourceState::CopySrc;
        core::UniquePtr<rhi::GpuBuffer> candidate;
        {
            core::CpuMemoryQuotaScope resource(context.quotas[11]);
            candidate = context.device->createBuffer(desc);
        }
        assert(candidate);
        std::array<std::uint8_t, 16> data;
        for (std::size_t i = 0u; i < data.size(); ++i)
            data[i] = static_cast<std::uint8_t>(i + 1u);
        auto bufferBudget = CanonicalInitializedBufferStagingContractFixture::budget();
        auto copySource = Buffer::prepareOriginal(*context.device,
            candidate,
            *context.loss,
            data.data(),
            data.size(),
            frame,
            bufferBudget,
            {context.quotas[9],
                context.quotas[10],
                context.quotas[11],
                context.quotas[5],
                context.quotas[13]},
            diagnostic);
        assert(copySource);
        assert(!Buffer::initialize(copySource));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return copySource->callbacksDrained();
        }));
        desc.memory = rhi::MemoryUsage::GpuOnly;
        desc.initialState = rhi::ResourceState::CopyDst;
        {
            core::CpuMemoryQuotaScope resource(context.quotas[11]);
            candidate = context.device->createBuffer(desc);
        }
        assert(candidate);
        auto copyDestination = rhi::OriginalCommandBufferLease::prepareOriginal(
            candidate, {1u, true, 2u, metadataBytes}, context.quotas[5], diagnostic);
        assert(copyDestination);
        auto inputs = OriginalCommandTextureUploadInputsFixture::prepareInterleaved(
            uploads, staging, copySource->originalLease(), copyDestination, diagnostic);
        if (!inputs)
            std::fprintf(stderr, "texture interleaved inputs %s\n", diagnostic);
        assert(inputs);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes),
             recordingQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto root = WebGpuCanonicalUploadJoinsFixture::prepareRoot(inputs,
            *context.device,
            stateQuota,
            recordingQuota,
            true,
            5u,
            6u,
            2u,
            {1u, true, 6u, 0u, 8u, 6u},
            diagnostic);
        if (!root)
            std::fprintf(stderr, "texture interleaved root %s\n", diagnostic);
        assert(root);
        WebGpuCanonicalUploadProjectionFixture::verifyInterleaved(
            root, *context.device, proofs, *context.loss, copyDestination, wait, map);
        inputs.reset();
        copyDestination.reset();
        copySource.reset();
        proofs = {};
        uploads = {};
        staging = {};
        expected.reset();
        target.reset();
        context.release();
        for (const auto& quota : context.quotas)
            assert(!quota->liveBytes());
        for (const auto& quota :
            std::array{stateQuota, recordingQuota, contentProofQuota, context.contentQuota})
            assert(!quota->liveBytes());
#else
        (void)create;
        (void)wait;
        (void)map;
#endif
    }

    template <class Factory>
    static void verifyJoins(Factory&& create, bool retainForLoss, unsigned coldProfile = 0u)
    {
        verifyJoinsWithContinuation(
            create, retainForLoss, coldProfile, [](auto&, auto&, auto&, auto&) {});
    }

    template <class Factory, class Continuation>
    static void verifyJoinsWithContinuation(Factory&& create,
        bool retainForLoss,
        unsigned coldProfile,
        Continuation&& continuation,
        bool completedDescriptor = false)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Content = rhi_webgpu::WebGpuCanonicalTextureContentUploadSource;
        using Joins = rhi_webgpu::WebGpuCanonicalUploadJoins;
        auto context = core::makeUnique<Context>(
            create, true, true, 1u, coldProfile == 5u, completedDescriptor);
        const char* diagnostic = nullptr;
        auto staging = context->make(diagnostic);
        assert(staging);
        assert(!Buffer::initialize(context->buffer));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return context->buffer->callbacksDrained();
        }));
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto expected = context->binding->originalDestinationLease()
                            ->originalObject()
                            ->observeOriginalContent();
        assert(expected);
        auto source = Content::prepareOriginal(*context->device,
            staging,
            *expected,
            {1u, true, Content::retainedOwnerBound, metadataBytes},
            quotas[0],
            diagnostic);
        assert(source);
        for (unsigned probe = 0u; probe < 5u; ++probe) {
            auto inputs = OriginalCommandTextureUploadInputsFixture::prepareForSource(
                context->binding->originalSource(),
                diagnostic,
                probe ? context->buffer->originalLease()
                      : std::shared_ptr<const rhi::OriginalCommandBufferLease>{},
                probe == 1u ? 1u : 2u,
                probe == 2u ? packedBytes - 1u : packedBytes,
                coldProfile != 0u);
            assert(inputs);
            // A source-less root has only the two original texture observations.
            if (!probe) {
                const rhi::OriginalCommandStateAdmissionBudget b{
                    1u, true, 1u, 1u, 2u, 2u, 17u, metadataBytes};
                auto graph = rhi::OriginalCommandResourceStates::prepareOriginal(
                    inputs, *context->device, b, quotas[1], diagnostic);
                assert(graph);
                auto root = rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
                    graph, *context->device, b, quotas[2], diagnostic);
                assert(root);
                assert(!root->materializeOriginalPrototypePlan(
                    {1u, true, 5u, 0u, 4u, 4u}, {1u, true, 1u, 2u}));
                const std::array<Joins::Input, 2> entries{{{1u, source}, {2u, source}}};
                assert(!Joins::prepareOriginal(root,
                    *context->device,
                    entries.data(),
                    2u,
                    WebGpuCanonicalUploadJoinsFixture::budget(*root, true),
                    diagnostic));
                is(diagnostic, "RHI-WEBGPU-UPLOAD-JOIN-STAGING-NOT-IN-ORIGINAL-SOURCE");
                continue;
            }
            auto root = WebGpuCanonicalUploadJoinsFixture::prepareRoot(inputs,
                *context->device,
                quotas[1],
                probe == 3u ? quotas[0] : quotas[2],
                true,
                diagnostic);
            assert(root);
            if (probe < 4u) {
                const std::array<Joins::Input, 2> entries{{{1u, source}, {2u, source}}};
                assert(!Joins::prepareOriginal(root,
                    *context->device,
                    entries.data(),
                    2u,
                    WebGpuCanonicalUploadJoinsFixture::budget(*root, true),
                    diagnostic));
                is(diagnostic,
                    probe == 3u       ? "RHI-WEBGPU-UPLOAD-JOIN-QUOTA-DOMAIN"
                        : probe == 1u ? "RHI-WEBGPU-UPLOAD-JOIN-ORIGINAL-PAYLOAD-CAPACITY"
                                      : "RHI-WEBGPU-UPLOAD-JOIN-ORIGINAL-STAGING-CAPACITY");
            } else {
                Context foreign(create);
                const std::array<Joins::Input, 2> entries{{{1u, source}, {2u, source}}};
                assert(!Joins::prepareOriginal(root,
                    *foreign.device,
                    entries.data(),
                    2u,
                    WebGpuCanonicalUploadJoinsFixture::budget(*root, true),
                    diagnostic));
                assert(diagnostic);
                foreign.release();
                for (const auto& q : foreign.quotas)
                    assert(!q->liveBytes());
                if (coldProfile >= 5u) {
                    continuation(root, *context, source, context->binding->originalSource());
                    if (coldProfile >= 6u) {
                        executionLossContexts_[coldProfile - 6u] = std::move(context);
                        executionLossQuotas_[coldProfile - 6u] = quotas;
                        return;
                    }
                    continue;
                }
                if (coldProfile == 4u) {
                    WebGpuCanonicalUploadProjectionFixture::verify(
                        root, *context->device, source, *context->loss, true);
                    continue;
                }
                if (coldProfile) {
                    auto aliasObject = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
                    const auto substituted = coldProfile == 2u ? context->quotas[13]
                        : coldProfile == 3u ? std::shared_ptr<core::CpuMemoryQuota>(
                                                  context->quotas[13], aliasObject.get())
                                            : std::shared_ptr<core::CpuMemoryQuota>{};
                    WebGpuCanonicalUploadColdFixture::verify(
                        root, *context->device, source, *context->loss, true, substituted);
                    continue;
                }
                auto joined = WebGpuCanonicalUploadJoinsFixture::verify(
                    root, *context->device, source, true, retainForLoss);
                if (retainForLoss) {
                    joinLossContext_ = std::move(context);
                    lossJoin_ = std::move(joined);
                    joinLossQuotas_ = quotas;
                    return;
                }
            }
        }
        source.reset();
        expected.reset();
        staging.reset();
        context->release();
        for (const auto& q : context->quotas)
            assert(!q->liveBytes());
        assert(!context->contentQuota->liveBytes());
        for (const auto& q : quotas)
            assert(!q->liveBytes());
#else
        (void)create;
        (void)retainForLoss;
        (void)coldProfile;
        (void)continuation;
#endif
    }

    template <class Factory> static void verifyContentBinding(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Content = rhi_webgpu::WebGpuCanonicalTextureContentUploadSource;
        Context context(create, true);
        const char* diagnostic = nullptr;
        auto staging = context.make(diagnostic);
        assert(staging);
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto expected =
            context.binding->originalDestinationLease()->originalObject()->observeOriginalContent();
        assert(expected);
        const Content::Budget local{1u, true, Content::retainedOwnerBound, metadataBytes};
        const auto make = [&] {
            return Content::prepareOriginal(
                *context.device, staging, *expected, local, quota, diagnostic);
        };
        assert(!make());
        is(diagnostic, "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY");
        assert(!Buffer::initialize(context.buffer));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return context.buffer->callbacksDrained();
        }));
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        {
            core::CpuMemoryQuotaLease full(quota, metadataBytes);
            assert(full.charged());
            assert(!make());
            is(diagnostic, "RHI-CANONICAL-CONTENT-STAGING-ALLOCATION");
        }
        auto source = make();
        assert(source && !source->validate() && !source->nativeExecutionProven()
            && core::SharedOwnerProjection::sameOwner(source->originalStaging(), staging)
            && source->destinationContentExpectation().contentGeneration() == 0u
            && source->destinationContentExpectation().revision() == expected->revision());
        for (const auto& domain : context.quotas) {
            assert(!Content::prepareOriginal(
                *context.device, staging, *expected, local, domain, diagnostic));
            is(diagnostic, "RHI-CANONICAL-CONTENT-STAGING-QUOTA-DOMAIN");
        }
        assert(!Content::prepareOriginal(
            *context.device, staging, *expected, local, context.contentQuota, diagnostic));
        is(diagnostic, "RHI-CANONICAL-CONTENT-STAGING-QUOTA-DOMAIN");
        auto forged = std::shared_ptr<const Source>(quota, staging.get());
        assert(!Content::prepareOriginal(
            *context.device, forged, *expected, local, quota, diagnostic));
        is(diagnostic, "RHI-CANONICAL-CONTENT-STAGING-OWNER");
        forged.reset();
        auto bad = local;
        --bad.maxRetainedOwners;
        assert(
            !Content::prepareOriginal(*context.device, staging, *expected, bad, quota, diagnostic));
        is(diagnostic, "RHI-CANONICAL-CONTENT-STAGING-BUDGET");
        bad = local;
        bad.contractVersion = 0u;
        assert(
            !Content::prepareOriginal(*context.device, staging, *expected, bad, quota, diagnostic));
        is(diagnostic, "RHI-CANONICAL-CONTENT-STAGING-VERSION");
        bad = local;
        bad.prototypeOptIn = false;
        assert(
            !Content::prepareOriginal(*context.device, staging, *expected, bad, quota, diagnostic));
        is(diagnostic, "RHI-CANONICAL-CONTENT-STAGING-OPT-IN");
        quota->setLimitBytes(metadataBytes - 1u);
        assert(!Content::prepareOriginal(
            *context.device, staging, *expected, local, quota, diagnostic));
        is(diagnostic, "RHI-CANONICAL-CONTENT-STAGING-QUOTA");
        quota->setLimitBytes(metadataBytes);
        {
            Context foreign(create, true);
            auto other = foreign.binding->originalDestinationLease()
                             ->originalObject()
                             ->observeOriginalContent();
            assert(other);
            assert(!Content::prepareOriginal(
                *context.device, staging, *other, local, quota, diagnostic));
            is(diagnostic, "RHI-CANONICAL-CONTENT-EXPECTATION-FOREIGN");
            assert(!Content::prepareOriginal(
                *foreign.device, staging, *expected, local, quota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
            other.reset();
            foreign.release();
            for (const auto& q : foreign.quotas)
                assert(!q->liveBytes());
            assert(!foreign.contentQuota->liveBytes());
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, context.contentQuota}, [&]() noexcept -> const char* {
                if (const auto* id = source->validate())
                    return id;
                is(source->executionDiagnostic(),
                    "RHI-CANONICAL-TEXTURE-UPLOAD-ORDERED-EXECUTION-MISSING");
                return nullptr;
            }));
        auto inputs = OriginalCommandTextureUploadInputsFixture::prepareForSource(
            context.binding->originalSource(), diagnostic);
        assert(inputs);
        OriginalCommandTextureContentPlanFixture::verify(inputs,
            *context.device,
            *context.binding->originalDestinationLease()->originalObject(),
            context.contentQuota,
            [&](bool pending) noexcept {
                is(source->validate(),
                    pending ? "RHI-CANONICAL-CONTENT-CONTROL-PENDING"
                            : "RHI-CANONICAL-CONTENT-EXPECTATION-STALE");
            });
        inputs.reset();
        auto native = rhi_webgpu::webGpuNativePayloadFor(
            *context.binding->originalDestinationLease()->originalObject());
        {
            core::MutationJournal journal;
            assert(!native->recordState(rhi::ResourceState::CopySrc, &journal));
            assert(source->validate());
            journal.rollback();
            assert(source->validate());
        }
        native.reset();
        source.reset();
        staging.reset();
        expected.reset();
        context.release();
        for (const auto& q : context.quotas)
            assert(!q->liveBytes());
        assert(!quota->liveBytes() && !context.contentQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_CANONICAL_CONTENT_STAGING_OK peak%llu original-initialized-staging exact-content0 state-content-distinct cold-exact-less-exhausted foreign-forged-CPU-domains-refused hot-allocation0 sixteen-quotas-zero ordered-execution-closed\n",
            static_cast<unsigned long long>(peak));
        contentLossContext_ = core::makeUnique<Context>(create, true);
        auto lossStaging = contentLossContext_->make(diagnostic);
        assert(lossStaging);
        assert(!Buffer::initialize(contentLossContext_->buffer));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return contentLossContext_->buffer->callbacksDrained();
        }));
        auto lossExpected = contentLossContext_->binding->originalDestinationLease()
                                ->originalObject()
                                ->observeOriginalContent();
        assert(lossExpected);
        contentLossQuota_ = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        contentLossSource_ = Content::prepareOriginal(*contentLossContext_->device,
            lossStaging,
            *lossExpected,
            local,
            contentLossQuota_,
            diagnostic);
        assert(contentLossSource_);
#else
        (void)create;
#endif
    }

public:
    static void beginActualLossProbe()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WebGpuCanonicalUploadProjectionFixture::beginUploadLoss(true);
        const auto native = rhi_webgpu::webGpuNativePayloadFor(
            *lossContext_->buffer->originalLease()->originalObject());
        const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
        EM_ASM(
            {
                // clang-format off
                    const device = WebGPU.getJsObject($0);
                    const queue = WebGPU.getJsObject($1);
                    const p = ({device, queue, pop : device.popErrorScope, done : queue.onSubmittedWorkDone});
                    Module.canonicalTextureStagingLoss = p;
                    const delay = function(promise) {
                        return promise.then(function(value) {
                            return device.lost.then(function() {
                                return value;
                            });
                        });
                    };
                    device.popErrorScope = function() {
                        return delay(p.pop.call(this));
                    };
                    queue.onSubmittedWorkDone = function() {
                        return delay(p.done.call(this));
                    };
                // clang-format on
            },
            native->ownerDevice,
            queue);
        assert(!Buffer::initialize(lossContext_->buffer));
        EM_ASM({
            // clang-format off
                const p = Module.canonicalTextureStagingLoss;
                p.device.popErrorScope = p.pop;
                p.queue.onSubmittedWorkDone = p.done;
                delete Module.canonicalTextureStagingLoss;
            // clang-format on
        });
        wgpuQueueRelease(queue);
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WebGpuCanonicalUploadProjectionFixture::verifyUploadLoss(true);
        for (unsigned probe = 0u; probe < executionLossContexts_.size(); ++probe) {
            auto& context = executionLossContexts_[probe];
            context->release();
            for (const auto& quota : context->quotas)
                assert(!quota->liveBytes());
            assert(!context->contentQuota->liveBytes());
            context.reset();
            for (const auto& quota : executionLossQuotas_[probe])
                assert(!quota->liveBytes());
            executionLossQuotas_[probe] = {};
        }

        assert(lossJoin_->validate());
        lossJoin_.reset();
        joinLossContext_->release();
        for (const auto& q : joinLossContext_->quotas)
            assert(!q->liveBytes());
        assert(!joinLossContext_->contentQuota->liveBytes());
        for (const auto& q : joinLossQuotas_)
            assert(!q->liveBytes());
        joinLossContext_.reset();
        joinLossQuotas_ = {};
        std::puts(
            "BINDLESS_RHI_CANONICAL_UPLOAD_JOIN_LOSS_OK texture actual-physical-loss same-original-root refused all-quotas-zero");
        assert(WebGpuAsyncWaitFixture::until([&] {
            return lossContext_->buffer->callbacksDrained();
        }));
        assert(lossContext_->buffer->callbacksOutstandingAtLoss() == 4u
            && lossSource_->stagingReadyDiagnostic() && lossSource_->executionDiagnostic());
        const auto quotas = lossContext_->quotas;
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                lossSource_.reset();
                lossContext_->release();
                lossContext_.reset();
                return nullptr;
            }));
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_CANONICAL_TEXTURE_STAGING_LOSS_OK actual-physical-loss initialized-buffer-receipts4 no-content-promotion all-fourteen-quotas-zero hot-allocation0");
        assert(contentLossSource_->validate() && contentLossSource_->executionDiagnostic());
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 16> contentQuotas;
        for (std::size_t i = 0u; i < 14u; ++i)
            contentQuotas[i] = contentLossContext_->quotas[i];
        contentQuotas[14] = contentLossContext_->contentQuota;
        contentQuotas[15] = contentLossQuota_;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            contentQuotas, [&]() noexcept -> const char* {
                contentLossSource_.reset();
                contentLossContext_->release();
                contentLossContext_.reset();
                contentLossQuota_.reset();
                return nullptr;
            }));
        for (const auto& q : contentQuotas)
            assert(!q->liveBytes());
        std::puts(
            "BINDLESS_RHI_CANONICAL_CONTENT_STAGING_LOSS_OK physical-loss exact-content-refused sixteen-quotas-zero hot-allocation0 ordered-execution-closed");
#endif
    }
};
} // namespace arp::test
