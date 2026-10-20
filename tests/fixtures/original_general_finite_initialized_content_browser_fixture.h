#pragma once
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_general_finite_initialized_content_fixture.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"

namespace arp::test {
class OriginalGeneralFiniteInitializedContentBrowserFixture final {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Initialized = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
    using Content = rhi::OriginalGeneralFiniteInitializedBufferContentContract;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "finite initialized content: %s\n", id);
        assert(!id);
    }

public:
    enum class InitializationTerminal {
        None,
        CancelBefore,
        CancelPending,
        NativeError,
        PendingLoss,
        CancelDropPending,
        PendingDropLoss
    };
    using TerminalScopes = WebGpuErrorScopeHandoffFixture<719u>;

    static bool callbackOnly(InitializationTerminal value) noexcept
    {
        return value == InitializationTerminal::CancelDropPending
            || value == InitializationTerminal::PendingDropLoss;
    }

    static bool physicalLoss(InitializationTerminal value) noexcept
    {
        return value == InitializationTerminal::PendingLoss
            || value == InitializationTerminal::PendingDropLoss;
    }

    static InitializationTerminal selectedTerminal()
    {
        const auto value = EM_ASM_INT({
            // clang-format off
                const v = new URLSearchParams(location.search).get('general-content-initialization-terminal');
                return v === null                    ? 0
                       : v === 'cancel-before'       ? 1
                       : v === 'cancel-pending'      ? 2
                       : v === 'native-error'        ? 3
                       : v === 'pending-loss'        ? 4
                       : v === 'cancel-drop-pending' ? 5
                       : v === 'pending-drop-loss'   ? 6
                                                     : 7;
            // clang-format on
        });
        assert(value >= 0 && value <= 6);
        return static_cast<InitializationTerminal>(value);
    }

    static const char* terminalName(InitializationTerminal value) noexcept
    {
        switch (value) {
        case InitializationTerminal::CancelBefore:
            return "cancel-before";
        case InitializationTerminal::CancelPending:
            return "cancel-pending";
        case InitializationTerminal::NativeError:
            return "native-error";
        case InitializationTerminal::PendingLoss:
            return "pending-loss";
        case InitializationTerminal::CancelDropPending:
            return "cancel-drop-pending";
        case InitializationTerminal::PendingDropLoss:
            return "pending-drop-loss";
        default:
            return "none";
        }
    }

    template <class Validate>
    static void verifyInitializationTerminal(InitializationTerminal terminal,
        const std::shared_ptr<const Initialized>& initialized,
        bool& attempted,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u>& quotas,
        Validate&& valid)
    {
        using Scopes = TerminalScopes;
        const auto* expected = physicalLoss(terminal) ? "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST"
            : terminal == InitializationTerminal::NativeError
            ? "RHI-CANONICAL-UPLOAD-SOURCE-NATIVE-ERROR"
            : "RHI-CANONICAL-UPLOAD-SOURCE-CANCELLED";
        if (!attempted) {
            assert(core::StringView(valid()) == "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY");
            if (terminal == InitializationTerminal::CancelBefore) {
                require(Initialized::cancelOriginal(initialized));
                assert(initialized->callbacksDrained());
            } else {
                Scopes::holdScopes();
                if (terminal == InitializationTerminal::NativeError) {
                    // Change only the destination offset of the real write. The
                    // genuine GPU validation scope, not a fabricated receipt,
                    // reports its required four-byte alignment violation.
                    EM_ASM({
                        // clang-format off
                            const q = Module.preinitializedWebGPUDevice.queue;
                            Module.arpFiniteContentWrite = q.writeBuffer;
                            Module.arpFiniteContentInvalidWrites = 0;
                            q.writeBuffer = function(b, o, d, s, n) {
                                ++Module.arpFiniteContentInvalidWrites;
                                return Module.arpFiniteContentWrite.call(this, b, 1, d, s, n);
                            };
                        // clang-format on
                    });
                }
                require(Initialized::initialize(initialized));
                if (terminal == InitializationTerminal::NativeError) {
                    assert(EM_ASM_INT({
                        // clang-format off
                            return Module.arpFiniteContentInvalidWrites === 1;
                        // clang-format on
                    }));
                    EM_ASM({
                        // clang-format off
                            Module.preinitializedWebGPUDevice.queue.writeBuffer = Module.arpFiniteContentWrite;
                            delete Module.arpFiniteContentWrite;
                            delete Module.arpFiniteContentInvalidWrites;
                        // clang-format on
                    });
                }
                Scopes::restoreScopes();
                Scopes::waitHeldScopes();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return initialized->queueCompletionObserved();
                }));
                assert(!initialized->callbacksDrained());
                assert(core::StringView(valid()) == "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY");
                if (terminal == InitializationTerminal::CancelPending
                    || terminal == InitializationTerminal::CancelDropPending)
                    require(Initialized::cancelOriginal(initialized));
                if (physicalLoss(terminal)) {
                    EM_ASM({
                        // clang-format off
                            Module.preinitializedWebGPUDevice.destroy();
                        // clang-format on
                    });
                    assert(WebGpuAsyncWaitFixture::until([] {
                        return rhi_webgpu::webGpuNativeDeviceLost();
                    }));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return initialized->callbacksOutstandingAtLoss() == 3u;
                    }));
                }
                if (terminal != InitializationTerminal::NativeError)
                    assert(core::StringView(valid()) == expected);
                // Held native callbacks still own the original initializer
                // storage. Neither cancellation nor actual loss drains them.
                assert(!initialized->callbacksDrained());
                for (const auto index : {0u, 1u, 4u})
                    assert(quotas[index]->liveBytes());
                std::printf(
                    "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_INITIALIZATION_PENDING_OK %s genuine-held-scopes3 queue1 original-quotas-live undrained\n",
                    terminalName(terminal));
                if (!callbackOnly(terminal)) {
                    Scopes::releaseScope(0u);
                    emscripten_sleep(0u);
                    assert(!initialized->callbacksDrained());
                    Scopes::releaseScopes();
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return initialized->callbacksDrained();
                    }));
                }
            }
            attempted = true;
            assert(initialized->readyDiagnostic());
            assert(Initialized::initialize(initialized));
            if (!callbackOnly(terminal))
                std::printf(
                    "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_INITIALIZATION_DRAIN_OK %s original-scopes queue-custody no-ready retry-refused\n",
                    terminalName(terminal));
        }
        assert(initialized->callbacksDrained() != callbackOnly(terminal));
        assert(core::StringView(valid()) == expected);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
            return core::StringView(valid()) == expected ? nullptr
                                                         : "unexpected terminal association";
        }));
        std::printf(
            "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_INITIALIZATION_REFUSAL_OK %s %s original-Source5 no-ready no-execution\n",
            terminalName(terminal),
            expected);
    }

    static void finishCallbackOnlyDrain(InitializationTerminal terminal,
        std::shared_ptr<const Initialized>& initialized,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u>& quotas)
    {
        assert(callbackOnly(terminal) && initialized && !initialized->callbacksDrained());
        assert(initialized->readyDiagnostic() && Initialized::initialize(initialized));
        std::weak_ptr<const Initialized> wrapper = initialized;
        initialized.reset();
        assert(wrapper.expired());
        // The observation's weak control block is itself charged to metadata.
        // Release it before measuring retirement of the callback-owned State.
        wrapper.reset();
        // No caller wrapper can now keep State alive. The three original
        // callbacks must retain its original allocations and resource lease.
        for (const auto index : {0u, 1u, 4u})
            assert(quotas[index]->liveBytes());
        TerminalScopes::releaseScope(0u);
        emscripten_sleep(0u);
        for (const auto index : {0u, 1u, 4u})
            assert(quotas[index]->liveBytes());
        TerminalScopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return !quotas[0]->liveBytes() && !quotas[1]->liveBytes() && !quotas[4]->liveBytes();
        }));
        std::printf(
            "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_CALLBACK_ONLY_OK %s expired-wrapper original-pending-quotas partial-retained genuine-drain-quotas-zero\n",
            terminalName(terminal));
        std::printf(
            "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_INITIALIZATION_DRAIN_OK %s original-scopes queue-custody no-ready retry-refused\n",
            terminalName(terminal));
    }

    static void verifyActual(rhi::GpuDevice& device,
        const std::shared_ptr<const Initialized>& initialized,
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        const core::ByteBuffer& payload,
        bool& attempted,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u>& quotas)
    {
        const Content::Request request{
            1u, true, Initialized::initializedIndexOwnerBound, payload.size(), payload.size()};
        const auto& frozen = descriptor->frozenElements()[0u];
        const auto& e = frozen.declaration;
        const bool readOnly = e.write.type == rhi::DescriptorType::ReadOnlyStorageBuffer;
        assert(readOnly || e.write.type == rhi::DescriptorType::UniformBuffer);
        std::printf(
            "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_BUFFER_TYPE_OK %s original-Staging4 Source5 frozen-type complete-charge\n",
            readOnly ? "readonly-storage" : "uniform");
        const auto check = [&](const auto& req,
                               const auto& upload,
                               const auto& set,
                               std::size_t ordinal,
                               const void* bytes,
                               std::uint64_t count,
                               std::uint64_t frame) noexcept {
            Content::Resolved output{123u, 456u};
            const auto* id = Initialized::validateGeneralFiniteInitializedContent(
                device, req, upload, set, ordinal, bytes, count, frame, output);
            if (id)
                assert(!output.offsetBytes && !output.sizeBytes);
            else
                assert(output.offsetBytes == e.bufferAccessOffset
                    && output.sizeBytes == e.bufferAccessBytes);
            return id;
        };
        const auto valid = [&]() noexcept {
            return check(request, initialized, descriptor, 0u, payload.data(), payload.size(), 1u);
        };
        const auto terminal = selectedTerminal();
        if (terminal != InitializationTerminal::None) {
            verifyInitializationTerminal(terminal, initialized, attempted, quotas, valid);
            return;
        }
        if (!attempted) {
            assert(core::StringView(valid()) == "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY");
            require(Initialized::initialize(initialized));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return initialized->callbacksDrained();
            }));
            require(initialized->readyDiagnostic());
            attempted = true;
            std::puts(
                "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_INITIALIZED_OK complete-backing Staging4 original-write scopes3 queue1 authentic-ready pending-refused execution-closed");
        }
        require(valid());
        assert(check(request,
            initialized,
            descriptor,
            descriptor->frozenElements().size(),
            payload.data(),
            payload.size(),
            1u));
        assert(check(request, initialized, descriptor, 1u, payload.data(), payload.size(), 1u));
        assert(
            check(request, initialized, descriptor, 0u, payload.data(), payload.size() - 1u, 1u));
        assert(check(request, initialized, descriptor, 0u, payload.data(), payload.size(), 2u));
        assert(check(request, initialized, descriptor, 0u, nullptr, payload.size(), 1u));
        assert(check(request,
            std::shared_ptr<const Initialized>{},
            descriptor,
            0u,
            payload.data(),
            payload.size(),
            1u));
        assert(check(request,
            initialized,
            std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>{},
            0u,
            payload.data(),
            payload.size(),
            1u));
        assert(check(request,
            std::shared_ptr<const Initialized>(quotas[0], initialized.get()),
            descriptor,
            0u,
            payload.data(),
            payload.size(),
            1u));
        assert(check(request,
            initialized,
            std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>(
                quotas[0], descriptor.get()),
            0u,
            payload.data(),
            payload.size(),
            1u));
        auto bad = request;
        bad.contractVersion = 2u;
        assert(check(bad, initialized, descriptor, 0u, payload.data(), payload.size(), 1u));
        bad = request;
        bad.prototypeOptIn = false;
        assert(check(bad, initialized, descriptor, 0u, payload.data(), payload.size(), 1u));
        bad = request;
        --bad.maxRetainedOwners;
        assert(check(bad, initialized, descriptor, 0u, payload.data(), payload.size(), 1u));
        bad = request;
        --bad.maxInitializedBytes;
        assert(check(bad, initialized, descriptor, 0u, payload.data(), payload.size(), 1u));
        bad = request;
        --bad.maxRetainedNativeBytes;
        assert(check(bad, initialized, descriptor, 0u, payload.data(), payload.size(), 1u));
        // Reject mutations outside a finite interval as well as selected bytes:
        // complete initializer provenance must not become a view-sized proof.
        {
            core::CpuMemoryQuotaScope scope(quotas[2]);
            core::ByteBuffer mutated(&core::defaultCpuMemoryResource());
            mutated.resize(payload.size());
            std::memcpy(mutated.data(), payload.data(), payload.size());
            mutated[e.bufferAccessOffset ? 0u : payload.size() - 1u] ^= 1u;
            assert(check(request, initialized, descriptor, 0u, mutated.data(), mutated.size(), 1u));
            std::memcpy(mutated.data(), payload.data(), payload.size());
            mutated[e.bufferAccessOffset] ^= 1u;
            assert(check(request, initialized, descriptor, 0u, mutated.data(), mutated.size(), 1u));
        }
        // The old whole validator still refuses a finite selected interval.
        const auto* old = Initialized::validateInitializedShaderBufferWithReady(initialized,
            initialized->originalLease(),
            e.write.type,
            e.bufferAccessOffset,
            e.bufferAccessBytes,
            [](const auto& source) noexcept {
                return source.readyDiagnostic();
            });
        if (e.write.bufferBindingRange.contractVersion == 2u)
            assert(old);
        else
            require(old);
        auto native =
            rhi_webgpu::webGpuNativePayloadFor(*initialized->originalLease()->originalObject());
        const auto buffer = native->buffer;
        native->buffer = nullptr;
        assert(valid());
        native->buffer = buffer;
        require(valid());
        const auto ownerDevice = native->ownerDevice;
        native->ownerDevice = nullptr;
        assert(valid());
        native->ownerDevice = ownerDevice;
        require(valid());
        // Force the genuine native GPUBuffer's getters to fail. Source and
        // association must use authenticated cold facts without querying them.
        EM_ASM({
            // clang-format off
                const b = Module.arpFiniteContentBuffer;
                if (!b)
                    throw new Error('missing original finite content buffer');
                Module.arpFiniteContentReads = 0;
                Module.arpFiniteContentOwn = {};
                for (const key of ['size', 'usage']) {
                    Module.arpFiniteContentOwn[key] = Object.getOwnPropertyDescriptor(b, key);
                    Object.defineProperty(b, key, {
                        configurable : true,
                        get() {
                            ++Module.arpFiniteContentReads;
                            throw new Error('hot buffer getter forbidden');
                        }
                    });
                }

    // clang-format on
        });
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, valid));
        });
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpFiniteContentReads === 0;
            // clang-format on
        }));
        EM_ASM({
            // clang-format off
                const b = Module.arpFiniteContentBuffer;
                for (const key of ['size', 'usage']) {
                    const own = Module.arpFiniteContentOwn[key];
                    if (own)
                        Object.defineProperty(b, key, own);
                    else
                        delete b[key];
                }
                delete Module.arpFiniteContentOwn;
                delete Module.arpFiniteContentReads;
            // clang-format on
        });
        std::printf(
            "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_OK %s backing%llu offset%llu selected%llu original-Source5 frozen-bytes same-lease authentic-completion exact-less owner-native-mutation hot0-query0 old-whole-isolated execution-closed\n",
            e.write.bufferBindingRange.contractVersion == 2u ? "finite" : "whole",
            static_cast<unsigned long long>(payload.size()),
            static_cast<unsigned long long>(e.bufferAccessOffset),
            static_cast<unsigned long long>(e.bufferAccessBytes));
        }
#endif
        }
        ;
        } // namespace arp::test
