#pragma once
#include "canonical_initialized_buffer_staging_contract_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "original_command_buffer_upload_inputs_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_canonical_upload_cold_fixture.h"
#include "webgpu_canonical_upload_joins_fixture.h"
#include "webgpu_canonical_upload_projection_fixture.h"

namespace arp::test {
class WebGpuCanonicalBufferUploadSourceFixture final {
    using Source = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
    static constexpr std::uint64_t metadataBytes = 65536u, dataBytes = 16u, frame = 99u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "canonical source diagnostic actual=%s expected=%s\n",
                actual ? actual : "<none>",
                expected);
        assert(actual && std::string_view(actual) == expected);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Context final {
    public:
        template <class Factory>
        explicit Context(Factory&& create, std::uint32_t buffers = 2u, unsigned profile = 0u)
            : device(create()), profile(profile)
        {
            assert(device);
            for (std::size_t i = 0u; i < quotas.size(); ++i)
                quotas[i] = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            assert(!device->prepareOriginalDeviceLifetime(
                {1u, true, 1u, 2u, metadataBytes}, quotas[5]));
            assert(!device->prepareOriginalDeviceNativeLoss(
                {{1u, true, 1u, 1u, metadataBytes}, 1u}, quotas[7]));
            const char* diagnostic = nullptr;
            auto observed = device->observeOriginalDeviceNativeLoss(diagnostic);
            assert(observed && !diagnostic);
            loss.emplace(*observed);
            assert(!device->prepareOriginalResourceRegistration(
                {1u,
                    true,
                    buffers,
                    0u,
                    0u,
                    rhi::OriginalResourceRegistrationContract::fixedOwnerCount
                        + (buffers)*rhi::OriginalResourceRegistrationContract::
                            perResourceOwnerCount,
                    metadataBytes},
                quotas[6]));
            for (std::size_t i = 0u; i < data.size(); ++i)
                data[i] = static_cast<std::uint8_t>(i + 1u);
            if (profile == 1u || profile == 2u) {
                data.fill(0u);
                const std::array<std::uint32_t, 8> indices{3u, 1u, 4u, 2u, 0u, 7u, 6u, 5u};
                const auto width = profile == 1u ? 2u : 4u;
                for (std::size_t i = 0u; i < data.size() / width; ++i)
                    for (unsigned byte = 0u; byte < width; ++byte)
                        data[i * width + byte] =
                            static_cast<std::uint8_t>(indices[i] >> (8u * byte));
            } else if (profile == 3u || profile == 4u) {
                // Explicit little-endian IEEE754 payload: vertex float2 records
                // or scalar instance records. No host byte-order assumption.
                const std::array<std::uint32_t, 4> components{
                    0xbf000000u, 0x3f000000u, 0x3f800000u, 0x40000000u};
                for (std::size_t i = 0u; i < components.size(); ++i)
                    for (unsigned byte = 0u; byte < 4u; ++byte)
                        data[i * 4u + byte] =
                            static_cast<std::uint8_t>(components[i] >> (8u * byte));
            }
        }

        void rebuild()
        {
            candidate.reset();
            rhi::BufferDesc desc;
            desc.sizeBytes = dataBytes;
            const auto selected = budget(profile);
            desc.usage =
                rhi::OriginalCommandInitializedBufferStagingContract::expectedUsage(selected);
            desc.memory = rhi::MemoryUsage::CpuToGpu;
            desc.initialState =
                rhi::OriginalCommandInitializedBufferStagingContract::expectedState(selected);
            core::CpuMemoryQuotaScope resource(quotas[2]);
            candidate = device->createBuffer(desc);
            assert(candidate);
        }

        auto make(const char*& diagnostic)
        {
            return Source::prepareOriginal(*device,
                candidate,
                *loss,
                data.data(),
                data.size(),
                frame,
                budget(profile),
                {quotas[0], quotas[1], quotas[2], quotas[3], quotas[4]},
                diagnostic);
        }

        void release()
        {
            candidate.reset();
            loss.reset();
            device.reset();
        }

        std::array<std::shared_ptr<core::CpuMemoryQuota>, 8> quotas;
        core::UniquePtr<rhi::GpuDevice> device;
        std::optional<rhi::OriginalDeviceNativeLossObservation> loss;
        core::UniquePtr<rhi::GpuBuffer> candidate;
        std::array<std::uint8_t, dataBytes> data;
        const unsigned profile;
    };

    static inline std::array<core::UniquePtr<Context>, 10> lossContexts_;
    static inline std::array<std::shared_ptr<const Source>, 10> lossSources_;
    static inline core::UniquePtr<Context> joinLossContext_;
    static inline std::shared_ptr<const rhi_webgpu::WebGpuCanonicalUploadJoins> lossJoin_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> joinLossQuotas_;
    static inline std::array<core::UniquePtr<Context>, 5> executionLossContexts_;
    static inline std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 3>, 5>
        executionLossQuotas_;

#endif
public:
    static Source::Budget budget(unsigned profile = 0u)
    {
        auto result = CanonicalInitializedBufferStagingContractFixture::budget();
        if (profile == 1u || profile == 2u) {
            result.contractVersion = 2u;
            result.indexContent = {1u,
                true,
                profile == 1u ? rhi::IndexFormat::Uint16 : rhi::IndexFormat::Uint32,
                core::UnsignedElementSpan::ByteOrder::LittleEndian,
                3u};
        } else if (profile == 3u || profile == 4u) {
            result.contractVersion = 3u;
            result.vertexContent = {1u,
                true,
                profile == 3u ? 8u : 4u,
                profile == 3u ? rhi::VertexInputRate::Vertex : rhi::VertexInputRate::Instance,
                profile == 3u ? 2u : 4u,
                core::UnsignedElementSpan::ByteOrder::LittleEndian};
        }
        return result;
    }

    template <class Factory, class QueueWait, class MapRead>
    static void verify(Factory&& create, QueueWait&& wait, MapRead&& map)
    {
        CanonicalInitializedBufferStagingContractFixture::verify();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (unsigned profile = 0u; profile < 5u; ++profile) {
            {
                Context context(create, 2u, profile);
                const char* diagnostic = nullptr;
                context.rebuild();
                const auto inspectRefusal = [&](const Source::Budget& b,
                                                const auto& quotas,
                                                const char* id) {
                    const auto* original = context.candidate.get();
                    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                    assert(!Source::prepareOriginal(*context.device,
                        context.candidate,
                        *context.loss,
                        context.data.data(),
                        context.data.size(),
                        frame,
                        b,
                        quotas,
                        diagnostic));
                    is(diagnostic, id);
                    assert(context.candidate.get() == original && !original->hostWritesSealed());
                    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                };
                const std::array sourceQuotas{context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4]};
                auto bad = budget(profile);
                --bad.maxRetainedNativeBytes;
                inspectRefusal(
                    bad, sourceQuotas, "RHI-INITIALIZED-CANONICAL-STAGING-BYTE-CAPACITY");
                if (profile >= 3u) {
                    bad = budget(profile);
                    ++bad.vertexContent.elementCount;
                    inspectRefusal(bad, sourceQuotas, "RHI-INITIALIZED-VERTEX-CONTENT-SIZE");
                    bad = budget(profile);
                    bad.contractVersion = 1u;
                    inspectRefusal(
                        bad, sourceQuotas, "RHI-INITIALIZED-CANONICAL-STAGING-VERTEX-SCHEMA");
                }
                bad = budget(profile);
                --bad.maxCallbacks;
                inspectRefusal(
                    bad, sourceQuotas, "RHI-INITIALIZED-CANONICAL-STAGING-OWNER-CAPACITY");
                bad = budget(profile);
                --bad.maxRetainedOwners;
                inspectRefusal(
                    bad, sourceQuotas, "RHI-INITIALIZED-CANONICAL-STAGING-OWNER-CAPACITY");
                auto aliases = sourceQuotas;
                aliases[4] = aliases[0];
                inspectRefusal(
                    budget(profile), aliases, "RHI-CANONICAL-UPLOAD-SOURCE-QUOTA-DOMAIN");
                aliases = sourceQuotas;
                aliases[4] = std::shared_ptr<core::CpuMemoryQuota>(aliases[0], aliases[4].get());
                inspectRefusal(
                    budget(profile), aliases, "RHI-CANONICAL-UPLOAD-SOURCE-QUOTA-DOMAIN");
                aliases = sourceQuotas;
                aliases[4] = std::shared_ptr<core::CpuMemoryQuota>(
                    std::shared_ptr<core::CpuMemoryQuota>{}, aliases[4].get());
                inspectRefusal(
                    budget(profile), aliases, "RHI-CANONICAL-UPLOAD-SOURCE-QUOTA-DOMAIN");
                aliases = sourceQuotas;
                aliases[2] = context.quotas[5];
                inspectRefusal(
                    budget(profile), aliases, "RHI-CANONICAL-UPLOAD-SOURCE-RESOURCE-QUOTA");
                const auto original = context.candidate.get();
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                const auto refusePayload = [&](const void* bytes,
                                               std::uint64_t size,
                                               const char* id) {
                    assert(!Source::prepareOriginal(*context.device,
                        context.candidate,
                        *context.loss,
                        bytes,
                        size,
                        frame,
                        budget(profile),
                        sourceQuotas,
                        diagnostic));
                    is(diagnostic, id);
                    assert(context.candidate.get() == original && !original->hostWritesSealed());
                    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                };
                refusePayload(nullptr, dataBytes, "RHI-CANONICAL-UPLOAD-SOURCE-PAYLOAD");
                refusePayload(
                    context.data.data(), dataBytes - 1u, "RHI-CANONICAL-UPLOAD-SOURCE-PAYLOAD");
                refusePayload(context.data.data(),
                    dataBytes - 4u,
                    profile >= 3u ? "RHI-INITIALIZED-VERTEX-CONTENT-SIZE"
                                  : "RHI-INITIALIZED-CANONICAL-STAGING-BYTE-CAPACITY");
                assert(!Source::prepareOriginal(*context.device,
                    context.candidate,
                    {},
                    context.data.data(),
                    dataBytes,
                    frame,
                    budget(profile),
                    sourceQuotas,
                    diagnostic));
                is(diagnostic, "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED");
                {
                    Context foreign(create);
                    assert(!Source::prepareOriginal(*context.device,
                        context.candidate,
                        *foreign.loss,
                        context.data.data(),
                        dataBytes,
                        frame,
                        budget(profile),
                        sourceQuotas,
                        diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
                    assert(!Source::prepareOriginal(*foreign.device,
                        context.candidate,
                        *foreign.loss,
                        context.data.data(),
                        dataBytes,
                        frame,
                        budget(profile),
                        sourceQuotas,
                        diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
                    foreign.release();
                    for (const auto& quota : foreign.quotas)
                        assert(!quota->liveBytes());
                }
                for (const auto domain : std::array<std::size_t, 4>{0u, 1u, 3u, 4u}) {
                    CpuQuotaFactoryFixture pressure(context.quotas[domain]);
                    const auto make = [&] {
                        return context.make(diagnostic);
                    };
                    const auto peak = pressure.measure(make);
                    assert(!context.candidate);
                    pressure.verifySingleUse(
                        peak,
                        [&] {
                            context.rebuild();
                        },
                        [&] {
                            const auto* original = context.candidate.get();
                            auto source = make();
                            if (!source) {
                                is(diagnostic,
                                    domain == 3u ? "RHI-ORIGINAL-SOURCE-LEASE-ALLOCATION"
                                                 : "RHI-CANONICAL-UPLOAD-SOURCE-ALLOCATION");
                                assert(context.candidate.get() == original
                                    && !original->hostWritesSealed());
                            }
                            return source;
                        });
                    std::printf(
                        "%s_COLD_OK domain%zu peak%llu exact-one-less-exhausted same-candidate-on-refusal no-seal-before-publication\n",
                        marker(profile),
                        domain,
                        static_cast<unsigned long long>(peak));
                }
                aliases = {};
                context.release();
                for (const auto& quota : context.quotas)
                    assert(!quota->liveBytes());
                std::printf(
                    "%s_ADMISSION_OK native-byte-callback-owner-limits aliased-forged-nonowning-quotas no-mutation allocation0\n",
                    marker(profile));
            }
            for (unsigned probe = 0u; probe < 7u; ++probe) {
                Context context(create, 2u, profile);
                const char* diagnostic = nullptr;
                context.rebuild();
                const auto id = context.candidate->id();
                auto source = context.make(diagnostic);
                if (!source)
                    std::fprintf(stderr, "canonical upload source diagnostic %s\n", diagnostic);
                assert(source && !context.candidate && source->originalLease()->resource() == id);
                is(source->readyDiagnostic(), "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY");
                const auto expected = context.data;
                context.data.fill(0xe7u);
                if (profile == 1u || profile == 2u) {
                    rhi::IndexInputConsumptionContract::DrawRange out;
                    is(Source::analyzeInitializedIndices(source,
                           source->originalLease(),
                           budget(profile).indexContent.format,
                           0u,
                           0u,
                           3u,
                           0,
                           out),
                        "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY");
                    assert(!out.indexCount);
                } else if (profile >= 3u) {
                    const auto intent = budget(profile).vertexContent;
                    const rhi::VertexInputConsumptionContract::Stream stream{
                        0u, intent.strideBytes, intent.rate, 0u, 1u, intent.strideBytes};
                    is(Source::validateInitializedVertexRange(
                           source, source->originalLease(), stream, 0u, 1u, 0u, 1u, 0u),
                        "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY");
                }
                auto native =
                    rhi_webgpu::webGpuNativePayloadFor(*source->originalLease()->originalObject());
                assert(native);
                auto forged = std::shared_ptr<const Source>(context.quotas[0], source.get());
                is(Source::initialize(forged), "RHI-CANONICAL-UPLOAD-SOURCE-OWNER");
                forged.reset();
                const auto probeQueue = wgpuDeviceGetQueue(native->ownerDevice);
                assert(probeQueue);
                if (probe == 1u || probe >= 5u)
                    EM_ASM(
                        {
                            // clang-format off
                                const queue = WebGPU.getJsObject($0);
                                Module.canonicalUploadOriginalWrite = queue.writeBuffer;
                                const probe = $1;
                                queue.writeBuffer = function(buffer, offset, data, start, size) {
                                    if (probe === 1)
                                        return Module.canonicalUploadOriginalWrite.call(this, buffer, 4, data, start, size);
                                    if (probe === 6)
                                        Module.canonicalUploadOriginalWrite.call(this, buffer, offset, data, start, size);
                                    throw new Error("canonical initialized buffer write uncertainty");
                                };
                            // clang-format on
                        },
                        probeQueue,
                        probe);
                std::weak_ptr<const rhi::OriginalCommandBufferLease> retained =
                    source->originalLease();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    context.quotas, [&]() noexcept -> const char* {
                        const auto* initialized = Source::initialize(source);
                        if (probe >= 5u)
                            is(initialized, "RHI-CANONICAL-UPLOAD-SOURCE-WRITE-UNCERTAIN");
                        else
                            assert(!initialized);
                        if (probe == 1u || probe >= 5u)
                            EM_ASM(
                                {
                                    // clang-format off
                                        const queue = WebGPU.getJsObject($0);
                                        queue.writeBuffer = Module.canonicalUploadOriginalWrite;
                                        delete Module.canonicalUploadOriginalWrite;
                                    // clang-format on
                                },
                                probeQueue);
                        if (probe == 2u)
                            assert(!Source::cancelOriginal(source));
                        if (probe == 3u) {
                            native.reset();
                            source.reset();
                            assert(!retained.expired());
                        }
                        if (probe == 4u)
                            context.device.reset();
                        if (probe == 3u)
                            assert(WebGpuAsyncWaitFixture::until([&] {
                                return retained.expired();
                            }));
                        else {
                            assert(WebGpuAsyncWaitFixture::until([&] {
                                return source->callbacksDrained();
                            }));
                            assert(source->queueCompletionObserved());
                            if (probe == 0u)
                                assert(!source->readyDiagnostic());
                            else if (probe == 1u)
                                is(source->readyDiagnostic(),
                                    "RHI-CANONICAL-UPLOAD-SOURCE-NATIVE-ERROR");
                            else if (probe == 2u)
                                is(source->readyDiagnostic(),
                                    "RHI-CANONICAL-UPLOAD-SOURCE-CANCELLED");
                            else if (probe == 4u)
                                is(source->readyDiagnostic(),
                                    "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                            else
                                is(source->readyDiagnostic(),
                                    "RHI-CANONICAL-UPLOAD-SOURCE-WRITE-UNCERTAIN");
                        }
                        return nullptr;
                    }));
                if (probe == 0u) {
                    if (!profile) {
                        core::UniquePtr<rhi::GpuBuffer> destination;
                        rhi::BufferDesc desc;
                        desc.sizeBytes = dataBytes;
                        desc.usage = rhi::ResourceUsage::CopyDst | rhi::ResourceUsage::CopySrc;
                        desc.initialState = rhi::ResourceState::CopyDst;
                        {
                            core::CpuMemoryQuotaScope resource(context.quotas[2]);
                            destination = context.device->createBuffer(desc);
                        }
                        assert(destination);
                        auto lease = rhi::OriginalCommandBufferLease::prepareOriginal(destination,
                            {1u, true, 2u, metadataBytes},
                            context.quotas[3],
                            diagnostic);
                        assert(lease);
                        rhi::UploadBufferDesc upload;
                        upload.sourceUploadBuffer = source->originalLease()->resource();
                        upload.destinationResource = lease->resource();
                        upload.sizeBytes = dataBytes;
                        upload.frameSerial = frame;
                        upload.stagingIntent = rhi::StagingIntent::Upload;
                        upload.backendOwnedStaging = true;
                        const auto make = [&] {
                            return rhi::OriginalCommandBufferUploadSource::prepareOriginal(upload,
                                source->originalLease(),
                                lease,
                                {1u,
                                    true,
                                    rhi::OriginalCommandBufferUploadSource::
                                        completeRetainedOwnerCount,
                                    0u,
                                    metadataBytes,
                                    metadataBytes},
                                context.quotas[3],
                                context.quotas[0],
                                diagnostic);
                        };
                        auto author = make();
                        assert(author && !source->compareOriginalUpload(author));
                        ++upload.frameSerial;
                        auto wrongFrame = make();
                        assert(wrongFrame);
                        is(source->compareOriginalUpload(wrongFrame),
                            "RHI-CANONICAL-UPLOAD-SOURCE-ORIGINAL-JOIN");
                        wrongFrame.reset();
                        --upload.frameSerial;
                        --upload.sizeBytes;
                        auto shortSize = make();
                        assert(shortSize);
                        is(source->compareOriginalUpload(shortSize),
                            "RHI-CANONICAL-UPLOAD-SOURCE-ORIGINAL-JOIN");
                        shortSize.reset();
                        auto fake = std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>(
                            context.quotas[0], author.get());
                        assert(source->compareOriginalUpload(fake));
                        fake.reset();
                        author.reset();
                        lease.reset();
                    } else if (profile < 3u)
                        verifyInitializedIndices(context, source);
                    else
                        verifyInitializedVertices(context, source);
                    const auto byteSize = native->sizeBytes;
                    --native->sizeBytes;
                    assert(source->readyDiagnostic());
                    native->sizeBytes = byteSize;
                    assert(!source->readyDiagnostic());
                    auto bd = WGPU_BUFFER_DESCRIPTOR_INIT;
                    bd.size = dataBytes;
                    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
                    const auto buffer = wgpuDeviceCreateBuffer(native->ownerDevice, &bd);
                    assert(buffer);
                    const auto encoder =
                        wgpuDeviceCreateCommandEncoder(native->ownerDevice, nullptr);
                    assert(encoder);
                    wgpuCommandEncoderCopyBufferToBuffer(
                        encoder, native->buffer, 0u, buffer, 0u, dataBytes);
                    const auto commands = wgpuCommandEncoderFinish(encoder, nullptr);
                    assert(commands);
                    const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
                    wgpuQueueSubmit(queue, 1u, &commands);
                    assert(wait(queue) && map(buffer, dataBytes));
                    const auto* bytes = wgpuBufferGetConstMappedRange(buffer, 0u, dataBytes);
                    assert(bytes && !std::memcmp(bytes, expected.data(), expected.size()));
                    wgpuBufferUnmap(buffer);
                    wgpuBufferRelease(buffer);
                    wgpuCommandEncoderRelease(encoder);
                    wgpuCommandBufferRelease(commands);
                    wgpuQueueRelease(queue);
                    std::printf(
                        "%s_OUTPUT_OK exact-original-canonical-object allocation-registration-generation preserved frozen-bytes16 genuine-native-readback no-recording-proof\n",
                        marker(profile));
                }
                wgpuQueueRelease(probeQueue);
                native.reset();
                source.reset();
                retained.reset();
                context.release();
                for (std::size_t i = 0u; i < context.quotas.size(); ++i) {
                    if (context.quotas[i]->liveBytes())
                        std::fprintf(stderr,
                            "canonical source probe%u quota%zu retains %llu bytes\n",
                            probe,
                            i,
                            static_cast<unsigned long long>(context.quotas[i]->liveBytes()));
                    assert(!context.quotas[i]->liveBytes());
                }
                std::printf(
                    "%s_LIFECYCLE_OK probe%u genuine-queue-scopes cancel-drop-shutdown errors-no-ready all-eight-quotas-zero hot-allocation0\n",
                    marker(profile),
                    probe);
            }
            for (unsigned probe = 0u; probe < 2u; ++probe) {
                Context context(create, 2u, profile);
                context.rebuild();
                const char* diagnostic = nullptr;
                auto source = context.make(diagnostic);
                assert(source && !context.candidate);
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    context.quotas, [&]() noexcept -> const char* {
                        if (!probe) {
                            assert(!Source::cancelOriginal(source));
                            is(Source::initialize(source), "RHI-CANONICAL-UPLOAD-SOURCE-CANCELLED");
                        }
                        source.reset();
                        context.release();
                        return nullptr;
                    }));
                for (const auto& quota : context.quotas)
                    assert(!quota->liveBytes());
            }
            std::printf(
                "%s_COLD_RETIRE_OK cancel-before-write drop-before-write no-native-work all-eight-quotas-zero hot-allocation0\n",
                marker(profile));
        }
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
                        root, *context.device, source, *context.loss, false, probe);
                });

        for (std::size_t i = 0u; i < lossSources_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>(create, 2u, static_cast<unsigned>(i / 2u));
            lossContexts_[i]->rebuild();
            const char* diagnostic = nullptr;
            lossSources_[i] = lossContexts_[i]->make(diagnostic);
            assert(lossSources_[i]);
        }
#else
        (void)create;
        (void)wait;
        (void)map;
#endif
    }

private:
    static const char* marker(unsigned profile) noexcept
    {
        return profile == 1u ? "BINDLESS_RHI_IMMUTABLE_INDEX16"
            : profile == 2u  ? "BINDLESS_RHI_IMMUTABLE_INDEX32"
            : profile == 3u  ? "BINDLESS_RHI_IMMUTABLE_VERTEX"
            : profile == 4u  ? "BINDLESS_RHI_IMMUTABLE_INSTANCE"
                             : "BINDLESS_RHI_CANONICAL_INITIALIZED_STAGING";
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyInitializedVertices(
        Context& context, const std::shared_ptr<const Source>& source)
    {
        const auto intent = budget(context.profile).vertexContent;
        const rhi::VertexInputConsumptionContract::Stream stream{
            3u, intent.strideBytes, intent.rate, 0u, 1u, intent.strideBytes};
        const char* diagnostic = nullptr;
        context.rebuild();
        auto foreign = rhi::OriginalCommandBufferLease::prepareOriginal(
            context.candidate, {1u, true, 2u, metadataBytes}, context.quotas[3], diagnostic);
        assert(foreign && !diagnostic);
        auto forgedLease = std::shared_ptr<const rhi::OriginalCommandBufferLease>(
            context.quotas[0], source->originalLease().get());
        auto forgedSource = std::shared_ptr<const Source>(context.quotas[0], source.get());
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                context.quotas, [&]() noexcept -> const char* {
                    const auto inspect = [&](const auto& actor,
                                             const auto& lease,
                                             const auto& layout,
                                             std::uint64_t offset,
                                             std::uint64_t count,
                                             std::uint64_t first) {
                        return Source::validateInitializedVertexRange(actor,
                            lease,
                            layout,
                            offset,
                            intent.rate == rhi::VertexInputRate::Vertex ? count : 1u,
                            intent.rate == rhi::VertexInputRate::Vertex ? first : 0u,
                            intent.rate == rhi::VertexInputRate::Instance ? count : 1u,
                            intent.rate == rhi::VertexInputRate::Instance ? first : 0u);
                    };
                    assert(!inspect(
                        source, source->originalLease(), stream, 0u, intent.elementCount, 0u));
                    assert(!inspect(source,
                        source->originalLease(),
                        stream,
                        intent.strideBytes,
                        intent.elementCount - 1u,
                        0u));
                    assert(!inspect(
                        source, source->originalLease(), stream, 0u, 1u, intent.elementCount - 1u));
                    is(inspect(source, foreign, stream, 0u, 1u, 0u),
                        "RHI-INITIALIZED-VERTEX-LEASE-JOIN");
                    assert(inspect(source, forgedLease, stream, 0u, 1u, 0u));
                    is(inspect(forgedSource, source->originalLease(), stream, 0u, 1u, 0u),
                        "RHI-CANONICAL-UPLOAD-SOURCE-OWNER");
                    auto changed = stream;
                    ++changed.strideBytes;
                    is(inspect(source, source->originalLease(), changed, 0u, 1u, 0u),
                        "RHI-INITIALIZED-VERTEX-STREAM-JOIN");
                    changed = stream;
                    changed.rate = intent.rate == rhi::VertexInputRate::Vertex
                        ? rhi::VertexInputRate::Instance
                        : rhi::VertexInputRate::Vertex;
                    is(inspect(source, source->originalLease(), changed, 0u, 1u, 0u),
                        "RHI-INITIALIZED-VERTEX-STREAM-JOIN");
                    changed = stream;
                    ++changed.fetchBytes;
                    is(inspect(source, source->originalLease(), changed, 0u, 1u, 0u),
                        "RHI-INITIALIZED-VERTEX-STREAM-JOIN");
                    is(inspect(source, source->originalLease(), stream, 1u, 1u, 0u),
                        "RHI-INITIALIZED-VERTEX-BINDING-OFFSET");
                    is(inspect(source, source->originalLease(), stream, dataBytes, 1u, 0u),
                        "RHI-VERTEX-INPUT-BUFFER-RANGE");
                    is(inspect(source,
                           source->originalLease(),
                           stream,
                           0u,
                           intent.elementCount + 1u,
                           0u),
                        "RHI-VERTEX-INPUT-FETCH-RANGE");
                    is(inspect(source, source->originalLease(), stream, 0u, 1u, UINT64_MAX),
                        "RHI-VERTEX-INPUT-FETCH-RANGE");
                    is(inspect(source, source->originalLease(), stream, 0u, 0u, 0u),
                        "RHI-VERTEX-INPUT-DRAW-EMPTY");
                    rhi::IndexInputConsumptionContract::DrawRange unusedIndex;
                    is(Source::analyzeInitializedIndices(source,
                           source->originalLease(),
                           rhi::IndexFormat::Uint16,
                           0u,
                           0u,
                           1u,
                           0,
                           unusedIndex),
                        "RHI-INITIALIZED-INDEX-CONTENT-PROFILE");
                    const auto* object = source->originalLease()->originalObject();
                    assert(object->hostWritesSealed() && object->gpuWritesSealed()
                        && object->gpuTransferWritesSealed());
                    assert(!source->nativeExecutionProven());
                    return nullptr;
                }));
        });
        forgedSource.reset();
        forgedLease.reset();
        foreign.reset();
        std::printf(
            "%s_ANALYSIS_OK same-original-source-lease stride-rate exact-tail offset count-overflow forged-foreign-refused frozen-seals hot-allocation0-query0 native-draw-closed\n",
            marker(context.profile));
    }

    static void verifyInitializedIndices(
        Context& context, const std::shared_ptr<const Source>& source)
    {
        using Index = rhi::IndexInputConsumptionContract;
        const auto format = budget(context.profile).indexContent.format;
        const auto width = Index::elementBytes(format);
        const char* diagnostic = nullptr;
        context.rebuild();
        auto foreign = rhi::OriginalCommandBufferLease::prepareOriginal(
            context.candidate, {1u, true, 2u, metadataBytes}, context.quotas[3], diagnostic);
        assert(foreign && !diagnostic);
        auto forgedLease = std::shared_ptr<const rhi::OriginalCommandBufferLease>(
            context.quotas[0], source->originalLease().get());
        auto forgedSource = std::shared_ptr<const Source>(context.quotas[0], source.get());
        const auto verifyHot = [&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                context.quotas, [&]() noexcept -> const char* {
                    Index::DrawRange out;
                    const auto inspect = [&](const auto& actor,
                                             const auto& lease,
                                             rhi::IndexFormat f,
                                             std::uint64_t offset,
                                             std::uint64_t first,
                                             std::uint64_t count,
                                             std::int32_t base) {
                        out = {1u, 1u, 1u, 1u, 1};
                        const auto* id = Source::analyzeInitializedIndices(
                            actor, lease, f, offset, first, count, base, out);
                        if (id)
                            assert(!out.firstIndex && !out.indexCount && !out.firstVertex
                                && !out.vertexCount && !out.baseVertex);
                        return id;
                    };
                    assert(!inspect(source, source->originalLease(), format, width, 0u, 3u, -1));
                    assert(out.indexCount == 3u && out.firstVertex == 0u && out.vertexCount == 4u
                        && out.baseVertex == -1);
                    assert(!inspect(source, source->originalLease(), format, 0u, 1u, 3u, 2));
                    assert(out.firstIndex == 1u && out.firstVertex == 3u && out.vertexCount == 4u);
                    is(inspect(source, foreign, format, 0u, 0u, 1u, 0),
                        "RHI-INITIALIZED-INDEX-LEASE-JOIN");
                    assert(inspect(source, forgedLease, format, 0u, 0u, 1u, 0));
                    is(inspect(forgedSource, source->originalLease(), format, 0u, 0u, 1u, 0),
                        "RHI-CANONICAL-UPLOAD-SOURCE-OWNER");
                    is(inspect(source,
                           source->originalLease(),
                           format == rhi::IndexFormat::Uint16 ? rhi::IndexFormat::Uint32
                                                              : rhi::IndexFormat::Uint16,
                           0u,
                           0u,
                           1u,
                           0),
                        "RHI-INITIALIZED-INDEX-FORMAT-JOIN");
                    is(inspect(source, source->originalLease(), format, 1u, 0u, 1u, 0),
                        "RHI-INDEX-INPUT-BINDING-OFFSET");
                    is(inspect(source, source->originalLease(), format, UINT64_MAX, 0u, 1u, 0),
                        "RHI-INDEX-INPUT-BINDING-OFFSET");
                    is(inspect(
                           source, source->originalLease(), format, dataBytes - width, 0u, 2u, 0),
                        "CORE-UNSIGNED-SPAN-RANGE-INVALID");
                    is(inspect(source, source->originalLease(), format, 0u, UINT64_MAX, 1u, 0),
                        "CORE-UNSIGNED-SPAN-RANGE-INVALID");
                    is(inspect(source, source->originalLease(), format, 0u, 0u, 4u, 0),
                        "RHI-INDEX-INPUT-ANALYSIS-CAPACITY");
                    is(inspect(source, source->originalLease(), format, 0u, 0u, 1u, -4),
                        "RHI-INDEX-INPUT-BASE-VERTEX-NEGATIVE");
                    const auto* object = source->originalLease()->originalObject();
                    assert(object->hostWritesSealed() && object->gpuWritesSealed()
                        && object->gpuTransferWritesSealed());
                    assert(!source->nativeExecutionProven());
                    return nullptr;
                }));
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(verifyHot);
        forgedSource.reset();
        forgedLease.reset();
        foreign.reset();
        std::printf(
            "%s_ANALYSIS_OK same-original-source-and-lease frozen-little-endian signed-base ranges exact-analysis-ceiling forged-foreign-refused output-cleared seals-preserved hot-allocation0-query0 native-draw-closed\n",
            marker(context.profile));
    }
#endif
    template <class Factory, class QueueWait, class MapRead>
    static void verifyInterleaved(Factory&& create, QueueWait&& wait, MapRead&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        Context context(create, 4u);
        const char* diagnostic = nullptr;
        std::array<std::shared_ptr<const Source>, 2> sources;
        for (std::size_t i = 0u; i < sources.size(); ++i) {
            context.rebuild();
            for (std::size_t byte = 0u; byte < context.data.size(); ++byte)
                context.data[byte] = static_cast<std::uint8_t>(i * 64u + byte + 1u);
            sources[i] = context.make(diagnostic);
            assert(sources[i]);
            assert(!Source::initialize(sources[i]));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return sources[i]->callbacksDrained();
            }));
            assert(!sources[i]->readyDiagnostic());
        }
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes),
             stateQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes),
             recordingQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const auto makeTarget = [&] {
            rhi::BufferDesc desc;
            desc.sizeBytes = dataBytes;
            desc.usage = rhi::ResourceUsage::CopyDst | rhi::ResourceUsage::CopySrc;
            desc.initialState = rhi::ResourceState::CopyDst;
            core::UniquePtr<rhi::GpuBuffer> candidate;
            {
                core::CpuMemoryQuotaScope resource(context.quotas[2]);
                candidate = context.device->createBuffer(desc);
            }
            assert(candidate);
            auto lease = rhi::OriginalCommandBufferLease::prepareOriginal(
                candidate, {1u, true, 2u, metadataBytes}, context.quotas[3], diagnostic);
            assert(lease);
            return lease;
        };
        auto target = makeTarget(), checkpoint = makeTarget();
        std::array<std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>, 2> uploads;
        for (std::size_t i = 0u; i < uploads.size(); ++i) {
            rhi::UploadBufferDesc upload;
            upload.sourceUploadBuffer = sources[i]->originalLease()->resource();
            upload.destinationResource = target->resource();
            upload.sizeBytes = dataBytes;
            upload.frameSerial = frame;
            uploads[i] = rhi::OriginalCommandBufferUploadSource::prepareOriginal(upload,
                sources[i]->originalLease(),
                target,
                {1u,
                    true,
                    rhi::OriginalCommandBufferUploadSource::completeRetainedOwnerCount,
                    0u,
                    metadataBytes,
                    metadataBytes},
                context.quotas[3],
                graphQuota,
                diagnostic);
            assert(uploads[i]);
        }
        auto inputs = OriginalCommandBufferUploadInputsFixture::prepareInterleaved(
            uploads, checkpoint, diagnostic);
        if (!inputs)
            std::fprintf(stderr, "interleaved inputs %s\n", diagnostic);
        assert(inputs);
        auto root = WebGpuCanonicalUploadJoinsFixture::prepareRoot(inputs,
            *context.device,
            stateQuota,
            recordingQuota,
            false,
            4u,
            4u,
            0u,
            {1u, true, 12u, 0u, 4u, 8u},
            diagnostic);
        if (!root)
            std::fprintf(stderr, "interleaved root %s\n", diagnostic);
        assert(root);
        WebGpuCanonicalUploadProjectionFixture::verifyInterleaved(
            root, *context.device, sources, *context.loss, checkpoint, wait, map);
        inputs.reset();
        uploads = {};
        target.reset();
        checkpoint.reset();
        sources = {};
        context.release();
        for (const auto& quota : context.quotas)
            assert(!quota->liveBytes());
        assert(
            !graphQuota->liveBytes() && !stateQuota->liveBytes() && !recordingQuota->liveBytes());
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
    static void verifyJoinsWithContinuation(
        Factory&& create, bool retainForLoss, unsigned coldProfile, Continuation&& continuation)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto context = core::makeUnique<Context>(create);
        context->rebuild();
        const char* diagnostic = nullptr;
        auto source = context->make(diagnostic);
        assert(source);
        assert(!Source::initialize(source));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return source->callbacksDrained();
        }));
        assert(!source->readyDiagnostic());
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        rhi::BufferDesc desc;
        desc.sizeBytes = dataBytes;
        desc.usage = rhi::ResourceUsage::CopyDst | rhi::ResourceUsage::CopySrc;
        desc.initialState = rhi::ResourceState::CopyDst;
        core::UniquePtr<rhi::GpuBuffer> candidate;
        {
            core::CpuMemoryQuotaScope resource(context->quotas[2]);
            candidate = context->device->createBuffer(desc);
        }
        assert(candidate);
        auto target = rhi::OriginalCommandBufferLease::prepareOriginal(
            candidate, {1u, true, 2u, metadataBytes}, context->quotas[3], diagnostic);
        assert(target);
        rhi::UploadBufferDesc upload;
        upload.sourceUploadBuffer = source->originalLease()->resource();
        upload.destinationResource = target->resource();
        upload.sizeBytes = dataBytes;
        upload.frameSerial = frame;
        upload.stagingIntent = rhi::StagingIntent::Upload;
        upload.backendOwnedStaging = true;
        auto original = rhi::OriginalCommandBufferUploadSource::prepareOriginal(upload,
            source->originalLease(),
            target,
            {1u,
                true,
                rhi::OriginalCommandBufferUploadSource::completeRetainedOwnerCount,
                0u,
                metadataBytes,
                metadataBytes},
            context->quotas[3],
            quotas[0],
            diagnostic);
        assert(original);
        for (unsigned probe = 0u; probe < 4u; ++probe) {
            auto inputs = OriginalCommandBufferUploadInputsFixture::prepareForSource(original,
                diagnostic,
                probe ? 2u : 1u,
                probe == 1u ? dataBytes - 1u : dataBytes,
                coldProfile != 0u);
            assert(inputs);
            auto root = WebGpuCanonicalUploadJoinsFixture::prepareRoot(inputs,
                *context->device,
                quotas[1],
                probe == 2u ? context->quotas[0] : quotas[2],
                false,
                diagnostic);
            assert(root);
            if (probe < 3u) {
                using Joins = rhi_webgpu::WebGpuCanonicalUploadJoins;
                const std::array<Joins::Input, 2> entries{{{1u, source}, {2u, source}}};
                assert(!Joins::prepareOriginal(root,
                    *context->device,
                    entries.data(),
                    2u,
                    WebGpuCanonicalUploadJoinsFixture::budget(*root, false),
                    diagnostic));
                is(diagnostic,
                    probe == 2u ? "RHI-WEBGPU-UPLOAD-JOIN-QUOTA-DOMAIN"
                        : probe ? "RHI-WEBGPU-UPLOAD-JOIN-ORIGINAL-STAGING-CAPACITY"
                                : "RHI-WEBGPU-UPLOAD-JOIN-ORIGINAL-PAYLOAD-CAPACITY");
            } else {
                Context foreign(create);
                using Joins = rhi_webgpu::WebGpuCanonicalUploadJoins;
                const std::array<Joins::Input, 2> entries{{{1u, source}, {2u, source}}};
                assert(!Joins::prepareOriginal(root,
                    *foreign.device,
                    entries.data(),
                    2u,
                    WebGpuCanonicalUploadJoinsFixture::budget(*root, false),
                    diagnostic));
                assert(diagnostic);
                foreign.release();
                for (const auto& q : foreign.quotas)
                    assert(!q->liveBytes());
                if (coldProfile >= 5u) {
                    continuation(root, *context, source, original);
                    if (coldProfile >= 6u) {
                        executionLossContexts_[coldProfile - 6u] = std::move(context);
                        executionLossQuotas_[coldProfile - 6u] = quotas;
                        return;
                    }
                    continue;
                }
                if (coldProfile == 4u) {
                    WebGpuCanonicalUploadProjectionFixture::verify(
                        root, *context->device, source, *context->loss, false);
                    continue;
                }
                if (coldProfile) {
                    auto aliasObject = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
                    const auto substituted = coldProfile == 2u ? context->quotas[4]
                        : coldProfile == 3u ? std::shared_ptr<core::CpuMemoryQuota>(
                                                  context->quotas[4], aliasObject.get())
                                            : std::shared_ptr<core::CpuMemoryQuota>{};
                    WebGpuCanonicalUploadColdFixture::verify(
                        root, *context->device, source, *context->loss, false, substituted);
                    continue;
                }
                auto joined = WebGpuCanonicalUploadJoinsFixture::verify(
                    root, *context->device, source, false, retainForLoss);
                if (retainForLoss) {
                    joinLossContext_ = std::move(context);
                    lossJoin_ = std::move(joined);
                    joinLossQuotas_ = quotas;
                    return;
                }
            }
        }
        original.reset();
        target.reset();
        source.reset();
        context->release();
        for (const auto& q : context->quotas)
            assert(!q->liveBytes());
        for (const auto& q : quotas)
            assert(!q->liveBytes());
#else
        (void)create;
        (void)retainForLoss;
        (void)coldProfile;
        (void)continuation;
#endif
    }

public:
    static void beginActualLossProbe()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WebGpuCanonicalUploadProjectionFixture::beginUploadLoss(false);
        for (std::size_t i = 0u; i < lossSources_.size(); ++i) {
            const auto native = rhi_webgpu::webGpuNativePayloadFor(
                *lossSources_[i]->originalLease()->originalObject());
            const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
            EM_ASM({
                // clang-format off
                    const device = WebGPU.getJsObject($0);
                    const queue = WebGPU.getJsObject($1);
                    const state =
                        ({device, queue, pop : device.popErrorScope, done : queue.onSubmittedWorkDone, probe : $2});
                    Module.canonicalUploadLossDelay = state;
                    const delay = function(promise) {
                        return promise.then(function(value) {
                            return device.lost.then(function() {
                                return value;
                            });
                        });
                    };
                    device.popErrorScope = function() {
                        return delay(state.pop.call(this));
                    };
                    queue.onSubmittedWorkDone = function() {
                        const promise = state.done.call(this);
                        if (state.probe === 1)
                            return {
                                then : function(resolve, reject) {
                                    promise.then(resolve, reject);
                                    throw new Error("source queue registration uncertainty");
                                }
                            };
                        return delay(promise);
                    };
        // clang-format on
    },native->ownerDevice,queue,i%2u);
    const auto* id = Source::initialize(lossSources_[i]);
    if (i % 2u)
        is(id, "RHI-CANONICAL-UPLOAD-SOURCE-REGISTRATION-FAILED");
    else
        assert(!id);
    EM_ASM({
        // clang-format off
            const p = Module.canonicalUploadLossDelay;
            p.device.popErrorScope = p.pop;
            p.queue.onSubmittedWorkDone = p.done;
            delete Module.canonicalUploadLossDelay;
        // clang-format on
    });
    wgpuQueueRelease(queue);
}
#endif
}

static void verifyActualLoss()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WebGpuCanonicalUploadProjectionFixture::verifyUploadLoss(false);
    for (unsigned probe = 0u; probe < executionLossContexts_.size(); ++probe) {
        auto& context = executionLossContexts_[probe];
        context->release();
        for (const auto& quota : context->quotas)
            assert(!quota->liveBytes());
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
    for (const auto& q : joinLossQuotas_)
        assert(!q->liveBytes());
    joinLossContext_.reset();
    joinLossQuotas_ = {};
    std::puts(
        "BINDLESS_RHI_CANONICAL_UPLOAD_JOIN_LOSS_OK buffer actual-physical-loss same-original-root refused all-quotas-zero");
    for (std::size_t i = 0u; i < lossSources_.size(); ++i) {
        const auto& source = lossSources_[i];
        assert(WebGpuAsyncWaitFixture::until([&] {
            return source->callbacksDrained();
        }));
        assert(source->readyDiagnostic() && !source->nativeExecutionProven());
        assert(source->callbacksOutstandingAtLoss() == (i % 2u ? 3u : 4u));
        assert(source->queueCompletionObserved() == (i % 2u == 0u));
    }
    for (std::size_t i = 0u; i < lossSources_.size(); ++i) {
        const auto quotas = lossContexts_[i]->quotas;
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                if (i / 2u && i / 2u < 3u) {
                    rhi::IndexInputConsumptionContract::DrawRange out;
                    assert(Source::analyzeInitializedIndices(lossSources_[i],
                               lossSources_[i]->originalLease(),
                               budget(static_cast<unsigned>(i / 2u)).indexContent.format,
                               0u,
                               0u,
                               1u,
                               0,
                               out)
                        && !out.indexCount);
                } else if (i / 2u >= 3u) {
                    const auto intent = budget(static_cast<unsigned>(i / 2u)).vertexContent;
                    const rhi::VertexInputConsumptionContract::Stream stream{
                        0u, intent.strideBytes, intent.rate, 0u, 1u, intent.strideBytes};
                    assert(Source::validateInitializedVertexRange(lossSources_[i],
                        lossSources_[i]->originalLease(),
                        stream,
                        0u,
                        1u,
                        0u,
                        1u,
                        0u));
                }
                lossSources_[i].reset();
                lossContexts_[i]->release();
                lossContexts_[i].reset();
                return nullptr;
            }));
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
    }
    for (unsigned profile = 0u; profile < 5u; ++profile)
        std::printf(
            "%s_LOSS_OK actual-physical-loss pending-and-suppressed-queue no-ready original-loss-domain callbacks-drain all-eight-quotas-zero hot-allocation0\n",
            marker(profile));
#endif
}
}
;
} // namespace arp::test
