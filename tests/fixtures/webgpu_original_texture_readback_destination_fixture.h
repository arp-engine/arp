#pragma once
#include "rhi/backends/webgpu/webgpu_readback_preparation.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_original_texture_readback_device_staging_budget_fixture.h"
#include "webgpu_original_texture_readback_pair_fixture.h"
#include "webgpu_original_texture_readback_source_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackDestinationFixture final {
    using Destination = rhi_webgpu::WebGpuOriginalTextureReadbackDestination;
    using Budget = rhi_webgpu::WebGpuOriginalTextureReadbackDestinationBudget;
    using Source = rhi_webgpu::WebGpuOriginalTextureReadbackSource;
    using Authoring = WebGpuOriginalTextureReadbackSourceFixture::Context;
    using Promises = WebGpuErrorScopeHandoffFixture<1u>;
    static constexpr std::uint64_t cpuBytes = 65536u, textureBytes = 16u, bufferBytes = 512u;

    static Budget budget()
    {
        return {1u,
            true,
            4u,
            Destination::callbackCount,
            textureBytes,
            bufferBytes,
            cpuBytes,
            cpuBytes,
            cpuBytes};
    }

public:
    class Context final {
    public:
        Authoring authoring;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        std::shared_ptr<const Source> source;

        explicit Context(std::uint64_t bytes = bufferBytes,
            std::string_view label =
                "original readback destination exact label bytes retained after caller drop")
            : authoring(bytes, label)
        {
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        }

        void bindSource()
        {
            authoring.makeTexture(true);
            const char* diagnostic = nullptr;
            source = authoring.prepare({1u, true, 3u, 1u, textureBytes, cpuBytes}, diagnostic);
            assert(source);
        }

        auto prepare(const Budget& b, const char*& diagnostic)
        {
            return Destination::prepareOriginal(
                source, b, quotas[0], quotas[1], quotas[2], diagnostic);
        }

        void coldEmpty() const
        {
            for (const auto& q : quotas)
                if (q)
                    assert(!q->liveBytes());
        }

        void retire()
        {
            source.reset();
            authoring.retire();
            coldEmpty();
        }
    };

    static void verifyContract()
    {
        WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture::verifyContract();
        Budget b;
        assert(
            std::string_view(Destination::validateBudget(b)) == "RHI-READBACK-DESTINATION-VERSION");
        b.contractVersion = 1u;
        assert(
            std::string_view(Destination::validateBudget(b)) == "RHI-READBACK-DESTINATION-OPT-IN");
        b.prototypeOptIn = true;
        assert(
            std::string_view(Destination::validateBudget(b)) == "RHI-READBACK-DESTINATION-BUDGET");
        b = budget();
        assert(!Destination::validateBudget(b));
        --b.maxCallbacks;
        assert(Destination::validateBudget(b));
        ++b.maxCallbacks;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto result = Destination::lowering(backend);
            assert(result.validatorOnly == (backend == rhi::Backend::Null));
            assert(result.nativeCreationImplemented == (backend == rhi::Backend::WebGPU)
                && result.diagnostic);
        }
        assert(!Destination::lowering(static_cast<rhi::Backend>(99)).nativeCreationImplemented);
        Context c;
        const char* diagnostic = nullptr;
        assert(!c.prepare(b, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-DESTINATION-SOURCE-OWNER");
        --b.maxCpuMetadataBytes;
        assert(!c.prepare(b, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-DESTINATION-QUOTA");
        ++b.maxCpuMetadataBytes;
        assert(std::string_view(Destination::initialize({})) == "RHI-READBACK-DESTINATION-OWNER");
        c.retire();
        std::puts(
            "BINDLESS_RHI_READBACK_DESTINATION_CONTRACT_OK four-backend private-staging creation-only publication-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual()
    {
        WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture::verifyActual();
        verifyCold();
        verifyReady();
        verifyErrors();
        verifyPendingDrop();
        verifyBounds();
        std::puts(
            "BINDLESS_RHI_READBACK_DESTINATION_NATIVE_OK scopes3 private-copydst-mapread exact-query identity caller-drop allocation0 unpublished");
    }

    static void prepareActualLoss()
    {
        WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture::prepareActualLoss();
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>();
            lossContexts_[i]->bindSource();
            const char* diagnostic = nullptr;
            lossObjects_[i] = lossContexts_[i]->prepare(budget(), diagnostic);
            assert(lossObjects_[i]);
        }
        assert(!Destination::initialize(lossObjects_[0]));
        waitReady(lossObjects_[0]);
        Promises::holdScopes();
        assert(!Destination::initialize(lossObjects_[1]));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(lossObjects_[1]->status() == Destination::Status::Pending
            && lossObjects_[2]->status() == Destination::Status::Prepared);
    }

    static void verifyActualDeviceLoss()
    {
        WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture::verifyActualDeviceLoss();
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        for (const auto& object : lossObjects_) {
            assert(
                object->status() == Destination::Status::DeviceLost && !object->resultPublished());
            assert(std::string_view(object->validate()) == "RHI-READBACK-DESTINATION-DEVICE-LOST");
            assert(std::string_view(Destination::initialize(object))
                == "RHI-READBACK-DESTINATION-DEVICE-LOST");
        }
        std::weak_ptr<Destination> pending = lossObjects_[1];
        for (auto& object : lossObjects_)
            object.reset();
        assert(!pending.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return pending.expired();
        }));
        pending.reset();
        for (auto& context : lossContexts_) {
            context->retire();
            context.reset();
        }
        std::puts(
            "BINDLESS_RHI_READBACK_DESTINATION_LOSS_OK prepared-pending-ready reject callbacks-retire exact-native-cpu-budgets unpublished");
    }

private:
    static void waitReady(const std::shared_ptr<Destination>& object)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object->status() != Destination::Status::Pending;
        }));
        assert(object->status() == Destination::Status::CreationReady
            && object->creationScopesObserved() && !object->validate());
    }

    static void verifyCold()
    {
        Context c;
        c.bindSource();
        auto b = budget();
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return c.prepare(b, diagnostic);
        };
        std::array<std::uint64_t, 3> peaks{};
        for (std::size_t i = 0u; i < peaks.size(); ++i) {
            CpuQuotaFactoryFixture admission(c.quotas[i]);
            peaks[i] = admission.measure(factory);
            admission.verify(peaks[i], factory);
            c.coldEmpty();
            {
                core::CpuMemoryQuotaLease full(c.quotas[i], cpuBytes);
                assert(full.charged() && !factory());
            }
            c.coldEmpty();
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id);
            c.coldEmpty();
        };
        --b.maxSourceOwners;
        reject("RHI-READBACK-DESTINATION-CAPACITY");
        ++b.maxSourceOwners;
        --b.maxRetainedSourceBytes;
        reject("RHI-READBACK-DESTINATION-CAPACITY");
        ++b.maxRetainedSourceBytes;
        --b.maxGpuBufferBytes;
        reject("RHI-READBACK-DESTINATION-CAPACITY");
        ++b.maxGpuBufferBytes;
        b.maxCpuResultBytes = bufferBytes - 1u;
        c.quotas[2] = core::makeShared<core::CpuMemoryQuota>(b.maxCpuResultBytes);
        reject("RHI-READBACK-DESTINATION-CAPACITY");
        b.maxCpuResultBytes = cpuBytes;
        c.quotas[2] = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto original = c.source;
        c.source = std::shared_ptr<const Source>(c.quotas[0], original.get());
        reject("RHI-READBACK-DESTINATION-SOURCE-OWNER");
        c.source = std::move(original);
        for (std::size_t i = 0u; i < c.quotas.size(); ++i) {
            auto originalQuota = c.quotas[i];
            c.quotas[i].reset();
            reject("RHI-READBACK-DESTINATION-QUOTA");
            c.quotas[i] = std::move(originalQuota);
        }
        auto object = factory();
        assert(object && object->status() == Destination::Status::Prepared
            && !object->nativeResource());
        assert(
            object->unpublishedResultCapacityBytes() == bufferBytes && !object->resultPublished());
        assert(object->memoryStats().allocatedBytes == bufferBytes
            && object->memoryStats().liveResourceCount == 1u);
        object.reset();
        c.retire();
        std::printf(
            "BINDLESS_RHI_READBACK_DESTINATION_COLD_OK %llu %llu %llu quotas3 exact-less-exhausted source16 gpu512 result512\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]));
    }

    static void verifyReady()
    {
        Context c;
        c.bindSource();
        const char* diagnostic = nullptr;
        auto object = c.prepare(budget(), diagnostic);
        assert(object);
        auto forged = std::shared_ptr<Destination>(c.quotas[0], object.get());
        assert(
            std::string_view(Destination::initialize(forged)) == "RHI-READBACK-DESTINATION-OWNER");
        forged.reset();
        EM_ASM({
            // clang-format off
                Module.arpReadbackDestinationCreated = 0;
                Module.arpReadbackDestinationWork = 0;
                Module.arpReadbackDestinationMethods = {};
                for (let name of ['createBuffer', 'createCommandEncoder']) {
                    Module.arpReadbackDestinationMethods[name] = Module.preinitializedWebGPUDevice[name];
                    Module.preinitializedWebGPUDevice[name] = function(d) {
                        if (name === 'createBuffer') {
                            Module.arpReadbackDestinationCreated++;
                            Module.arpReadbackDestinationDesc = d;
                        } else
                            Module.arpReadbackDestinationWork++;
                        return Module.arpReadbackDestinationMethods[name].apply(this, arguments);
                    };
                }
                Module.arpReadbackDestinationSubmit = Module.preinitializedWebGPUDevice.queue.submit;
                Module.arpReadbackDestinationWrite = Module.preinitializedWebGPUDevice.queue.writeBuffer;
                Module.preinitializedWebGPUDevice.queue.submit = function() {
                    Module.arpReadbackDestinationWork++;
                    return Module.arpReadbackDestinationSubmit.apply(this, arguments);
                };
                Module.preinitializedWebGPUDevice.queue.writeBuffer = function() {
                    Module.arpReadbackDestinationWork++;
                    return Module.arpReadbackDestinationWrite.apply(this, arguments);
                };
            // clang-format on
        });
        Promises::holdScopes();
        const auto initializationAllocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease a(c.quotas[0], cpuBytes - c.quotas[0]->liveBytes()),
                b(c.quotas[1], cpuBytes - c.quotas[1]->liveBytes()),
                d(c.quotas[2], cpuBytes - c.quotas[2]->liveBytes());
            assert(a.charged() && b.charged() && d.charged());
            assert(!Destination::initialize(object));
        }
        assert(initializationAllocations == core::defaultCpuAllocator().stats().allocationCount);
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(object->status() == Destination::Status::Pending
            && std::string_view(object->validate()) == "RHI-READBACK-DESTINATION-PENDING"
            && !object->resultPublished());
        assert(EM_ASM_INT({
            // clang-format off
                var d = Module.arpReadbackDestinationDesc;
                return Module.arpReadbackDestinationCreated === 1 && Module.arpReadbackDestinationWork === 0 &&
                       d.label ===
                           'original readback destination exact label bytes retained after caller drop' &&
                       d.size === 512 && d.usage === (GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ) &&
                       !d.mappedAtCreation;
            // clang-format on
        }) == 1);
        Promises::releaseScopes();
        waitReady(object);
        WebGpuOriginalTextureReadbackPairFixture::verifyNotReady(
            object, "RHI-READBACK-PAIR-SOURCE-PROFILE");
        assert(object->source() == c.source
            && object->descriptor().usage == rhi::ResourceUsage::CopyDst
            && object->descriptor().memory == rhi::MemoryUsage::GpuToCpu);
        assert(!c.authoring.registry->find(object->resourceId()));
        auto& native = *object->nativeResource();
        const auto size = native.sizeBytes;
        ++native.sizeBytes;
        assert(object->validate());
        native.sizeBytes = size;
        const auto handle = native.buffer;
        native.buffer = nullptr;
        assert(object->validate());
        native.buffer = handle;
        const auto device = native.ownerDevice;
        native.ownerDevice = nullptr;
        assert(object->validate());
        native.ownerDevice = device;
        auto token = native.allocationReleaseToken;
        native.allocationReleaseToken = core::makeShared<std::uint64_t>(bufferBytes);
        assert(object->validate());
        native.allocationReleaseToken = token;
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease a(c.quotas[0], cpuBytes - c.quotas[0]->liveBytes()),
                b(c.quotas[1], cpuBytes - c.quotas[1]->liveBytes()),
                d(c.quotas[2], cpuBytes - c.quotas[2]->liveBytes());
            assert(a.charged() && b.charged() && d.charged());
            assert(!object->validate()
                && std::string_view(object->executionDiagnostic())
                    == "RHI-READBACK-DESTINATION-EXECUTION-UNPROVEN"
                && !object->resultPublished());
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        EM_ASM({
            // clang-format off
                for (var name of Object.keys(Module.arpReadbackDestinationMethods))
                    Module.preinitializedWebGPUDevice[name] = Module.arpReadbackDestinationMethods[name];
                Module.preinitializedWebGPUDevice.queue.submit = Module.arpReadbackDestinationSubmit;
                Module.preinitializedWebGPUDevice.queue.writeBuffer = Module.arpReadbackDestinationWrite;
                delete Module.arpReadbackDestinationMethods;
                delete Module.arpReadbackDestinationSubmit;
                delete Module.arpReadbackDestinationWrite;
            // clang-format on
        });
        c.source.reset();
        c.authoring.layout.reset();
        c.authoring.registry.reset();
        assert(!object->validate());
        object.reset();
        token.reset();
        c.retire();
    }

    static void verifyErrors()
    {
        for (const std::uint32_t kind : {0u, 1u, 2u, 3u}) {
            Context c;
            c.bindSource();
            const char* diagnostic = nullptr;
            auto object = c.prepare(budget(), diagnostic);
            assert(object);
            if (kind != 1u)
                EM_ASM(
                    {
                        // clang-format off
                            Module.arpReadbackBadCreate = Module.preinitializedWebGPUDevice.createBuffer;
                            Module.preinitializedWebGPUDevice.createBuffer = function(d) {
                                var changes = $0 === 0   ? {usage : 0}
                                              : $0 === 2 ? {usage : GPUBufferUsage.COPY_DST}
                                                         : {size : d.size + 4};
                                return Module.arpReadbackBadCreate.call(this, Object.assign({}, d, changes));
                            };
                        // clang-format on
                    },
                    kind);
            if (kind == 1u) {
                core::CpuMemoryQuotaLease full(c.quotas[1], cpuBytes - c.quotas[1]->liveBytes());
                assert(full.charged());
                const auto a = core::defaultCpuAllocator().stats().allocationCount;
                assert(!Destination::initialize(object)
                    && a == core::defaultCpuAllocator().stats().allocationCount);
            } else {
                assert(!Destination::initialize(object));
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.createBuffer = Module.arpReadbackBadCreate;
                        delete Module.arpReadbackBadCreate;
                    // clang-format on
                });
            }
            assert(WebGpuAsyncWaitFixture::until([&] {
                return object->creationScopesObserved();
            }));
            if (kind == 1u)
                assert(object->status() == Destination::Status::CreationReady && !object->validate()
                    && !object->resultPublished());
            else {
                assert(object->status() == Destination::Status::Rejected
                    && !object->resultPublished());
                assert(std::string_view(object->validate())
                    == (kind == 0u ? "RHI-READBACK-DESTINATION-NATIVE-ERROR"
                                   : "RHI-READBACK-DESTINATION-NATIVE-LAYOUT"));
            }
            object.reset();
            c.retire();
        }
        Context c;
        c.bindSource();
        const char* diagnostic = nullptr;
        auto object = c.prepare(budget(), diagnostic);
        assert(object);
        Promises::holdScopes();
        assert(!Destination::initialize(object));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(!c.source->resource().texture->recordState(rhi::ResourceState::CopyDst));
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object->creationScopesObserved();
        }));
        assert(object->status() == Destination::Status::Rejected && !object->resultPublished());
        object.reset();
        c.retire();
    }

    static void verifyPendingDrop()
    {
        Context c;
        c.bindSource();
        const char* diagnostic = nullptr;
        auto object = c.prepare(budget(), diagnostic);
        assert(object);
        Promises::holdScopes();
        assert(!Destination::initialize(object));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        std::weak_ptr<Destination> weak = object;
        object.reset();
        c.source.reset();
        assert(!weak.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        c.retire();
    }

    static void verifyBounds()
    {
        for (const auto label :
            {std::string_view("bad\0label", 9u), std::string_view("\xc0\xaf", 2u)}) {
            Context invalid(bufferBytes, label);
            invalid.bindSource();
            const char* diagnostic = nullptr;
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!invalid.prepare(budget(), diagnostic)
                && std::string_view(diagnostic) == "RHI-READBACK-DESTINATION-LABEL"
                && core::defaultCpuAllocator().stats().allocationCount == before);
            invalid.retire();
        }
        Context c(bufferBytes + 1u);
        c.bindSource();
        const char* diagnostic = nullptr;
        auto b = budget();
        ++b.maxGpuBufferBytes;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!c.prepare(b, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-DESTINATION-MAP-ALIGNMENT"
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        c.retire();
        auto limits = WGPU_LIMITS_INIT;
        Context nativeLimits;
        nativeLimits.bindSource();
        assert(wgpuDeviceGetLimits(nativeLimits.source->resource().texture->ownerDevice, &limits)
            == WGPUStatus_Success);
        const auto capacity = limits.maxBufferSize + Destination::mapSizeAlignment;
        nativeLimits.retire();
        Context huge(capacity);
        huge.bindSource();
        b = budget();
        b.maxGpuBufferBytes = capacity;
        b.maxCpuResultBytes = capacity;
        huge.quotas[2] = core::makeShared<core::CpuMemoryQuota>(capacity);
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!huge.prepare(b, diagnostic)
            && std::string_view(diagnostic)
                == (capacity > std::numeric_limits<std::size_t>::max()
                        ? "RHI-READBACK-DESTINATION-CPU-SIZE"
                        : "RHI-READBACK-DESTINATION-NATIVE-LIMITS")
            && core::defaultCpuAllocator().stats().allocationCount == before);
        huge.retire();
    }

    static inline std::array<core::UniquePtr<Context>, 3> lossContexts_;
    static inline std::array<std::shared_ptr<Destination>, 3> lossObjects_;
#endif
};
} // namespace arp::test
