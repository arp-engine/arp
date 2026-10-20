#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_shader_validation.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_ordinary_graphics_triangle_range_fixture.h"
#include "webgpu_ordinary_scalar_compute_range_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class WebGpuOrdinaryShaderValidationFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        using Owner = WebGpuOrdinaryShaderSizeValidationOwner;
        WebGpuOrdinaryScalarComputeRangeFixture::verifyContract();
        WebGpuOrdinaryGraphicsTriangleRangeFixture::verifyContract();
        WebGpuOrdinaryShaderValidationBudget budget;
        assert(std::string_view(Owner::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-VALIDATION-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(Owner::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-VALIDATION-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(Owner::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-VALIDATION-BUDGET");
        budget.maxPendingProbes = 1u;
        budget.maxPendingCallbacks = Owner::scopeCount;
        budget.maxCpuMetadataBytes = metadataBytes;
        assert(!Owner::validateBudget(budget));
        --budget.maxPendingCallbacks;
        assert(std::string_view(Owner::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-VALIDATION-BUDGET");
        ++budget.maxPendingCallbacks;
        --budget.maxPendingProbes;
        assert(std::string_view(Owner::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-VALIDATION-BUDGET");
        ++budget.maxPendingProbes;
        const char* diagnostic = nullptr;
        assert(!Owner::prepare({}, budget, {}, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-QUOTA");
        assert(!Owner::callbackDiagnostic(true, true, nullptr));
        assert(std::string_view(Owner::callbackDiagnostic(false, true, nullptr))
            == "RHI-WEBGPU-ORDINARY-VALIDATION-CALLBACK-STATUS");
        assert(std::string_view(Owner::callbackDiagnostic(false, false, nullptr))
            == "RHI-WEBGPU-ORDINARY-VALIDATION-CALLBACK-STATUS");
        assert(std::string_view(Owner::callbackDiagnostic(true, false, nullptr))
            == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
        assert(std::string_view(Owner::callbackDiagnostic(true, true, "native-create-failed"))
            == "native-create-failed");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        assert(!Owner::prepare({}, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-UNAVAILABLE"
            && !quota->liveBytes());
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    struct LossProbe {
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryShaderSizeValidationOwner> owner;
        std::shared_ptr<core::CpuMemoryQuota> quota;
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryShaderSizeValidation> result;
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryShaderSizeValidation> completed;
    };

    static void verifyActualDeviceLoss()
    {
        for (auto& probe : lossProbes_) {
            assert(probe.owner && WebGpuAsyncWaitFixture::until([&] {
                return probe.owner->closed();
            }));
            assert(!probe.owner->pending());
            if (probe.result) {
                assert(probe.result->status()
                    == rhi_webgpu::WebGpuOrdinaryShaderSizeValidation::Status::DeviceLost);
                assert(std::string_view(probe.result->diagnostic())
                    == "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST");
            }
            if (probe.completed) {
                assert(probe.completed->status()
                    == rhi_webgpu::WebGpuOrdinaryShaderSizeValidation::Status::DeviceLost);
                assert(std::string_view(probe.completed->diagnostic())
                    == "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST");
            }
        }
        // Deliver the three actual native promises after exact-device loss.
        // Their stale tickets must not authorize or revive the retired root.
        EM_ASM({
            // clang-format off
                var pending = Module.arpHeldValidationScopes;
                if (pending)
                    for (var entry of pending)
                        entry.release();
            // clang-format on
        });
        for (auto& probe : lossProbes_) {
            std::weak_ptr<rhi_webgpu::WebGpuOrdinaryShaderSizeValidationOwner> weak = probe.owner;
            if (probe.result) {
                // Drive native callback delivery through the browser event loop.
                emscripten_sleep(0u);
                assert(probe.result->status()
                    == rhi_webgpu::WebGpuOrdinaryShaderSizeValidation::Status::DeviceLost);
                probe.result.reset();
            }
            probe.owner.reset();
            probe.completed.reset();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return weak.expired();
            }));
            weak.reset();
            // A retained scalar range owns its completed static result until
            // its own exact-loss check below releases the original graph.
            if (!probe.quota->liveBytes())
                probe.quota.reset();
        }
        WebGpuOrdinaryComputeRecordingFixture::verifyActualDeviceLoss();
        WebGpuOrdinaryScalarComputeRangeFixture::verifyActualDeviceLoss();
        WebGpuOrdinaryGraphicsRecordingFixture::verifyActualDeviceLoss();
        WebGpuOrdinaryGraphicsTriangleRangeFixture::verifyActualDeviceLoss();
        for (auto& probe : lossProbes_) {
            if (probe.quota) {
                assert(!probe.quota->liveBytes());
                probe.quota.reset();
            }
        }
        EM_ASM({
            // clang-format off
                delete Module.arpHeldValidationScopes;
            // clang-format on
        });
        std::puts("BINDLESS_RHI_ORDINARY_SHADER_VALIDATION_DEVICE_LOSS_OK 5");
    }

    static void verifyActual(
        std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryPipelineObservation> pipeline,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryPhysicalGroups>& groups,
        const rhi_webgpu::WebGpuRhiResourceRegistry& registry,
        rhi::GpuResourceId vertexId,
        const char* name,
        bool expectedNativeFailure = false,
        bool smallRange = false)
    {
        using namespace rhi_webgpu;
        using Owner = WebGpuOrdinaryShaderSizeValidationOwner;
        using Result = WebGpuOrdinaryShaderSizeValidation;
        const auto streams = pipeline->contract().graphics().vertexBufferCount;
        assert(streams <= 2u);
        std::array<rhi::GpuResourceId, 2> ids{vertexId, vertexId};
        WebGpuOrdinaryShaderValidationBudget budget{1u,
            true,
            1u,
            Owner::scopeCount,
            groups->groupCount(),
            streams,
            1u,
            1u,
            1u,
            1u,
            1u,
            static_cast<std::uint32_t>(pipeline->contract().graphics().colorFormats.size()),
            metadataBytes};
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto factory = [&] {
            try {
                auto unique = Owner::prepare(pipeline, budget, quota, diagnostic);
                core::CpuMemoryQuotaScope scope(quota);
                return Owner::shareOriginal(std::move(unique));
            } catch (const std::bad_alloc&) {
                diagnostic = "RHI-WEBGPU-ORDINARY-VALIDATION-ALLOCATION";
                return std::shared_ptr<Owner>{};
            }
        };
        auto expect = [&](const char* id) {
            const auto live = quota->liveBytes();
            assert(!factory() && std::string_view(diagnostic) == id && quota->liveBytes() == live);
        };
        budget.maxCpuMetadataBytes--;
        expect("RHI-WEBGPU-ORDINARY-VALIDATION-QUOTA");
        budget.maxCpuMetadataBytes++;
        if (budget.maxGroups) {
            --budget.maxGroups;
            expect("RHI-WEBGPU-ORDINARY-VALIDATION-CAPACITY");
            ++budget.maxGroups;
        }
        if (streams) {
            --budget.maxVertexStreams;
            expect("RHI-WEBGPU-ORDINARY-VALIDATION-CAPACITY");
            ++budget.maxVertexStreams;
        }
        if (budget.maxColorTargets) {
            --budget.maxColorTargets;
            expect("RHI-WEBGPU-ORDINARY-VALIDATION-CAPACITY");
            ++budget.maxColorTargets;
        }
        if (pipeline->compute()) {
            --budget.maxCommandEncoders;
            expect("RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-CAPACITY");
            ++budget.maxCommandEncoders;
            --budget.maxComputePasses;
            expect("RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-CAPACITY");
            ++budget.maxComputePasses;
            --budget.maxCommandBuffers;
            expect("RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-CAPACITY");
            ++budget.maxCommandBuffers;
        } else {
            --budget.maxBundleEncoders;
            expect("RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-CAPACITY");
            ++budget.maxBundleEncoders;
            --budget.maxBundles;
            expect("RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-CAPACITY");
            ++budget.maxBundles;
        }
        // Factory metadata is measured before loss registration (no native probe).
        CpuQuotaFactoryFixture admission(quota);
        const auto ownerPeak = admission.measure(factory);
        admission.verify(ownerPeak, factory);
        // Exact/one-less owner probes saturate that quota's historical peak.
        // Use a new original quota before independently measuring probe peaks.
        quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto owner = factory();
        assert(owner);
        auto vertexQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto vertices = [&]() -> core::UniquePtr<WebGpuOrdinaryResourceProjection> {
            if (!streams)
                return {};
            WebGpuOrdinaryResourceBudget resourceBudget{1u,
                true,
                {rhi::kCommandRetentionBudgetContractVersion, true, 3u, 0u, 0u, 0u, metadataBytes},
                1u,
                0u,
                0u,
                64u};
            auto result = WebGpuOrdinaryResourceProjection::prepare(
                registry, &vertexId, 1u, resourceBudget, vertexQuota, diagnostic);
            assert(result);
            return result;
        };
        auto probe = [&] {
            return Owner::probe(
                owner, groups, vertices(), streams ? ids.data() : nullptr, streams, diagnostic);
        };
        auto canonical = owner;
        owner = std::shared_ptr<Owner>(quota, canonical.get());
        assert(!probe() && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-OWNER");
        owner = canonical;
        canonical.reset();
        if (streams) {
            assert(!Owner::probe(owner, groups, vertices(), nullptr, streams, diagnostic));
            assert(
                std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-VERTEX-COVERAGE");
            ids[0] = {};
            assert(!probe()
                && std::string_view(diagnostic)
                    == "RHI-WEBGPU-ORDINARY-VALIDATION-VERTEX-RESOURCE");
            ids[0] = vertexId;
        }
        auto result = probe();
        assert(result);
        // The pinned browser delivers popErrorScope promises asynchronously;
        // use its real pending callback window to test bounded admission.
        assert(result->status() == Result::Status::Pending && owner->pending() == 1u);
        if (pipeline->compute())
            WebGpuOrdinaryScalarComputeRangeFixture::verifyActual(result, smallRange, name, true);
        else
            WebGpuOrdinaryGraphicsTriangleRangeFixture::verifyActual(result, name, true);
        assert(!probe()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-CALLBACK-CAPACITY");
        const auto callbackAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(WebGpuAsyncWaitFixture::until([&] {
            return result->status() != Result::Status::Pending;
        }));
        assert(core::defaultCpuAllocator().stats().allocationCount == callbackAllocations);
        auto assertResult = [&] {
            if (expectedNativeFailure) {
                assert(result->status() == Result::Status::Rejected);
                assert(std::string_view(result->diagnostic())
                    == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
            } else {
                if (const auto* failure = result->diagnostic())
                    std::printf("ORDINARY_VALIDATION_FAILURE %s %s\n", name, failure);
                assert(!result->diagnostic());
                assert(std::string_view(result->executionDiagnostic())
                    == "RHI-WEBGPU-ORDINARY-GROUP-RECORDING-UNPREPARED");
            }
            assert(!owner->pending());
        };
        assertResult();
        if (pipeline->compute()) {
            WebGpuOrdinaryScalarComputeRangeFixture::verifyActual(result, smallRange, name);
            if (!smallRange) {
                WebGpuOrdinaryComputeRecordingFixture::verifySubmissionActual(
                    WebGpuOrdinaryScalarComputeRangeFixture::originalRange());
                const auto original = pipeline->compute()->pipeline;
                pipeline->compute()->pipeline = nullptr;
                WebGpuOrdinaryScalarComputeRangeFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-PIPELINE-IDENTITY-CHANGED");
                WebGpuOrdinaryComputeRecordingFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-PIPELINE-IDENTITY-CHANGED");
                pipeline->compute()->pipeline = original;
            }
        } else {
            const bool triangle = std::string_view(name) == "ordinary-graphics";
            WebGpuOrdinaryGraphicsTriangleRangeFixture::verifyActual(result,
                name,
                false,
                expectedNativeFailure ? "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR"
                    : triangle        ? nullptr
                                      : "RHI-WEBGPU-ORDINARY-RANGE-LANGUAGE-UNSUPPORTED");
            if (triangle) {
                WebGpuOrdinaryGraphicsRecordingFixture::verifyActual(
                    WebGpuOrdinaryGraphicsTriangleRangeFixture::originalRange());
                WebGpuOrdinaryGraphicsRecordingFixture::verifySubmissionActual();
                const auto original = pipeline->graphics()->pipeline;
                pipeline->graphics()->pipeline = nullptr;
                WebGpuOrdinaryGraphicsTriangleRangeFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-PIPELINE-IDENTITY-CHANGED");
                WebGpuOrdinaryGraphicsRecordingFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-PIPELINE-IDENTITY-CHANGED");
                pipeline->graphics()->pipeline = original;
            }
        }
        result.reset();
        // Two graph slots with only one probe's callback allowance exercises
        // callback capacity independently of the retention-pool capacity.
        auto graphBoundOwner = owner;
        budget.maxPendingProbes = 2u;
        owner = factory();
        assert(owner);
        auto callbackBound = probe();
        assert(callbackBound && owner->pending() == 1u);
        assert(!probe()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-CALLBACK-CAPACITY");
        assert(WebGpuAsyncWaitFixture::until([&] {
            return callbackBound->status() != Result::Status::Pending;
        }));
        assert(!owner->pending());
        callbackBound.reset();
        owner = graphBoundOwner;
        graphBoundOwner.reset();
        budget.maxPendingProbes = 1u;
        auto completedProbe = [&] {
            auto prepared = probe();
            if (prepared)
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return prepared->status() != Result::Status::Pending;
                }));
            return prepared;
        };
        CpuQuotaFactoryFixture probeAdmission(quota);
        const auto probePeak = probeAdmission.measure(completedProbe);
        probeAdmission.verify(probePeak, completedProbe);
        const auto baseline = quota->liveBytes();
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - baseline);
            assert(pressure.charged());
            assert(!probe()
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-ALLOCATION");
        }
        assert(quota->liveBytes() == baseline);
        result = probe();
        assert(result);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return result->status() != Result::Status::Pending;
        }));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged());
            assertResult();
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        auto completed = streams ? result : nullptr;
        result.reset();
        assert(!owner->pending());
        if (!streams)
            assert(!vertexQuota->liveBytes());
        if (!streams && !expectedNativeFailure && pipeline->compute()) {
            // A real unrelated device loss cannot invalidate this owner.
            EM_ASM({
                // clang-format off
                    Module.arpForeignValidationLoss = 0;
                    navigator.gpu.requestAdapter()
                        .then(function(adapter) {
                            if (!adapter)
                                throw new Error("foreign adapter unavailable");
                            return adapter.requestDevice();
                        })
                        .then(function(device) {
                            device.destroy();
                            return device.lost;
                        })
                        .then(
                            function() {
                                Module.arpForeignValidationLoss = 1;
                            },
                            function() {
                                Module.arpForeignValidationLoss = -1;
                            });
                // clang-format on
            });
            assert(WebGpuAsyncWaitFixture::until([] {
                return EM_ASM_INT({
                    // clang-format off
                        return Module.arpForeignValidationLoss;
                    // clang-format on
                }) != 0;
            }));
            assert(EM_ASM_INT({
                // clang-format off
                        return Module.arpForeignValidationLoss;
                // clang-format on
            }) == 1
                && !owner->closed());
            EM_ASM({
                // clang-format off
                    delete Module.arpForeignValidationLoss;
                // clang-format on
            });
        }
        if (streams) {
            // Test-only bounded delivery gate: actual popErrorScope promises
            // are retained, never replaced by synthesized statuses. The native
            // probe still executes and releases its references immediately.
            EM_ASM({
                // clang-format off
                    var device = Module.preinitializedWebGPUDevice;
                    Module.arpHeldValidationScopes = [];
                    Module.arpOriginalValidationPop = device.popErrorScope;
                    device.popErrorScope = function() {
                        var original = Module.arpOriginalValidationPop.call(this);
                        return original.then(function(value) {
                            return new Promise(function(resolve) {
                                Module.arpHeldValidationScopes.push({
                                    release : function() {
                                        resolve(value);
                                    }
                                });
                            });
                        });
                    };
        // clang-format on
    });
    result = probe();
    assert(result && owner->pending() == 1u);
    assert(WebGpuAsyncWaitFixture::until([] {
        return EM_ASM_INT({
            // clang-format off
                return Module.arpHeldValidationScopes.length;
            // clang-format on
        }) == rhi_webgpu::WebGpuOrdinaryShaderSizeValidationOwner::scopeCount;
    }));
    EM_ASM({
        // clang-format off
            Module.preinitializedWebGPUDevice.popErrorScope = Module.arpOriginalValidationPop;
            delete Module.arpOriginalValidationPop;
        // clang-format on
    });
    assert(result->status() == Result::Status::Pending && owner->pending() == 1u);
} assert(lossCount_ < lossProbes_.size());
lossProbes_[lossCount_++] = {owner, quota, result, completed};
std::printf("BINDLESS_RHI_ORDINARY_SHADER_VALIDATION_OK %s %llu %llu %s\n",
    name,
    static_cast<unsigned long long>(ownerPeak),
    static_cast<unsigned long long>(probePeak),
    expectedNativeFailure ? "NativeError" : "NoError");
// Exact loss registration retains its quota-bound observer until the
// existing browser smoke destroys this original device at the end.
}
#endif
private:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
static inline std::array<LossProbe, 5> lossProbes_{};
static inline std::size_t lossCount_ = 0u;
#endif
static constexpr std::uint64_t metadataBytes = 65536u;
}
;
} // namespace arp::test
