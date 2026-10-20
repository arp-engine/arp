#pragma once
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif
#include <cassert>
#include <cstdint>

namespace arp::test {
// Authored native boundary faults. Promises always come from the real device;
// this fixture never invokes a C receipt, issues proof or edits bridge tables.
class WebGpuNativeMapExecutionProbeFixture final {
public:
    enum class Probe : std::uint32_t {
        Success,
        ReverseScopes,
        Cancel,
        NativeError,
        ReadError,
        ScopeRegistration,
        ThrowBefore,
        ThrowAfter,
        UnmapThrow,
        PendingLoss,
        ScopeNativeError
    };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void instrument(WGPUDevice device,
        WGPUBuffer buffer,
        Probe probe,
        void* context,
        void (*observe)(void*) noexcept)
    {
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                if (Module.canonicalMapProbe)
                    return 0;
                const device = WebGPU.getJsObject($0);
                const buffer = WebGPU.getJsObject($1);
                const p = ({
                    device,
                    buffer,
                    map : buffer.mapAsync,
                    unmap : buffer.unmap,
                    read : buffer.getMappedRange,
                    pop : device.popErrorScope,
                    probe : $2,
                    context : $3,
                    observe : $4,
                    active : false,
                    count : 0,
                    received : 0,
                    readFaultInvocations : 0,
                    unmapFaultInvocations : 0,
                    readDiagnosticKey : 'arpExpectedCanonicalMapExecutionReadDiagnostic',
                    scopes : []
                });
                Module.canonicalMapProbe = p;
                buffer.mapAsync = function(...args) {
                    p.active = true;
                    getWasmTableEntry(p.observe)(p.context);
                    if (p.probe === 6)
                        throw new Error('Authored canonical map before invocation');
                    if (p.probe === 3)
                        buffer.destroy();
                    if (p.probe === 10)
                        device.queue.writeBuffer(buffer, buffer.size, new Uint32Array(1));
                    const native = p.map.apply(this, args);
                    if (p.probe === 7) {
                        native.catch(function() {});
                        throw new Error('Authored canonical map after invocation');
                    }
                    if (p.probe === 9)
                        return native.then(function(value) {
                            return new Promise(function(resolve) {
                                p.pendingResolve = () => resolve(value);
                            });
                        });
                    return native;
                };
                buffer.unmap = function() {
                    if (p.active && p.probe === 8) {
                        ++p.unmapFaultInvocations;
                        throw new Error('Authored canonical unmap uncertainty');
                    }
                    return p.unmap.call(this);
                };
                buffer.getMappedRange = function(...args) {
                    if (p.active && p.probe === 4) {
                        ++p.readFaultInvocations;
                        p.readOffset = args[0];
                        p.readBytes = args[1];
                        Module[p.readDiagnosticKey] =
                            'buffer.getMappedRange(' + args[0] + ', ' + args[1] +
                            ') failed: RangeError: CanonicalMapExecution authored read failure';
                        throw new RangeError('CanonicalMapExecution authored read failure');
                    }
                    return p.read.apply(this, args);
                };
                device.popErrorScope = function() {
                    if (!p.active)
                        return p.pop.call(this);
                    const index = p.count++;
                    if (p.probe === 5 && index === 0)
                        throw new Error('Authored canonical map scope registration');
                    const native = p.pop.call(this);
                    if (p.probe !== 1)
                        return native;
                    return new Promise(function(resolve, reject) {
                        native.then(function(value) {
                            p.scopes[index] = ({resolve, value});
                            if (++p.received === 3)
                                for (let i = 2; i >= 0; --i)
                                    p.scopes[i].resolve(p.scopes[i].value);
                        }, reject);
                    });
                };
                return 1;
                // clang-format on
            },
            device,
            buffer,
            static_cast<std::uint32_t>(probe),
            context,
            reinterpret_cast<std::uintptr_t>(observe)));
        // clang-format on
    }

    static void verifyReadFault(std::uint32_t expectedBytes = 0u) noexcept
    {
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                const p = Module.canonicalMapProbe;
                return !!p && p.active && p.probe === 4 && p.readFaultInvocations === 1 &&
                       (!$0 || (p.readOffset === 0 && p.readBytes === $0));
                // clang-format on
            },
            expectedBytes));
        // clang-format on
    }

    static void usePublicationReadDiagnostic() noexcept
    {
        // clang-format off
        EM_ASM({
            // clang-format off
                const p = Module.canonicalMapProbe;
                if (p && p.probe === 4)
                    p.readDiagnosticKey = 'arpExpectedCanonicalPublicationReadDiagnostic';
            // clang-format on
        });
        // clang-format on
    }

    static void verifyUnmapFault() noexcept
    {
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                const p = Module.canonicalMapProbe;
                return !!p && p.active && p.probe === 8 && p.unmapFaultInvocations === 1 &&
                       p.buffer.mapState === 'mapped';
            // clang-format on
        }));
        // clang-format on
    }

    static void restore(bool retainPendingForLoss = false) noexcept
    {
        // clang-format off
        EM_ASM({
            // clang-format off
                const p = Module.canonicalMapProbe;
                if (!p)
                    return;
                p.buffer.mapAsync = p.map;
                p.buffer.unmap = p.unmap;
                p.buffer.getMappedRange = p.read;
                p.device.popErrorScope = p.pop;
                // A registration throw happened before native pop; restore the
                // test-owned scope stack explicitly, outside the runtime executor.
                if (p.probe === 5)
                    p.pop.call(p.device).catch(function() {});
                if ($0) {
                    Module.canonicalMapPendingLossProbe = p;
                    p.context = 0;
                    p.observe = 0;
                } else if (p.pendingResolve)
                    p.pendingResolve();
                delete Module.canonicalMapProbe;
                // clang-format on
            },
            retainPendingForLoss);
        // clang-format on
    }

    static void retirePendingAfterLoss() noexcept
    {
        // clang-format off
        EM_ASM({
            // clang-format off
                const p = Module.canonicalMapPendingLossProbe;
                if (!p)
                    return;
                if (p.pendingResolve)
                    p.pendingResolve();
                delete Module.canonicalMapPendingLossProbe;
            // clang-format on
        });
        // clang-format on
    }
#endif
};
} // namespace arp::test
