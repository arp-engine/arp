#pragma once
#include "rhi/backends/webgpu/native_validation.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"

#include <cassert>
#include <cstdint>

namespace arp::test {
// Reusable native invocation/receipt probes. Never replace GPU objects,
// native promises, validation results or completion outcomes with test facts.
class WebGpuNativeSubmissionProbeFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void beginColdCallObservation()
    {
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                const p = ({
                    device : d,
                    group : d.createBindGroup,
                    encoder : d.createCommandEncoder,
                    groups : 0,
                    encoders : 0
                });
                Module.arpColdCallProbe = p;
                d.createBindGroup = function(desc) {
                    ++p.groups;
                    return p.group.call(this, desc);
                };
                d.createCommandEncoder = function(desc) {
                    ++p.encoders;
                    return p.encoder.call(this, desc);
                };
            // clang-format on
        });
    }

    static void finishColdCallObservation()
    {
        assert(EM_ASM_INT({
            // clang-format off
                const p = Module.arpColdCallProbe;
                return p.groups === 0 && p.encoders === 0;
            // clang-format on
        }));
        EM_ASM({
            // clang-format off
                const p = Module.arpColdCallProbe;
                p.device.createBindGroup = p.group;
                p.device.createCommandEncoder = p.encoder;
                delete Module.arpColdCallProbe;
            // clang-format on
        });
    }
#endif
    enum class Probe {
        Success,
        ThrowBefore,
        ThrowAfter,
        NativeError,
        Reverse,
        PartialScope,
        Cancel,
        Drop,
        Shutdown,
        ChangedNative
    };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void instrumentRecording(WGPUDevice device, bool reverseScopes = false)
    {
        assert(EM_ASM_INT(
            {
                // clang-format off
                    const device = WebGPU.getJsObject($0);
                    if (!device || Module.canonicalRecordingProbe)
                        return 0;
                    const p = ({
                        device,
                        encoder : device.createCommandEncoder,
                        pop : device.popErrorScope,
                        count : 0,
                        received : 0,
                        receipts : new Array($2)
                    });
                    Module.canonicalRecordingProbe = p;
                    if (!$1)
                        device.createCommandEncoder = function(desc) {
                            const encoder = p.encoder.call(this, desc);
                            encoder.popDebugGroup();
                            return encoder;
                        };
                    else
                        device.popErrorScope = function() {
                            const index = p.count++;
                            const promise = p.pop.call(this);
                            return new Promise(function(resolve, reject) {
                                promise.then(function(value) {
                                    p.receipts[index] = ({value, resolve});
                                    if (++p.received === p.receipts.length)
                                        for (let i = p.receipts.length - 1; i >= 0; --i)
                                            p.receipts[i].resolve(p.receipts[i].value);
                                }, reject);
                            });
                        };
                    return 1;
                // clang-format on
            },
            device,
            reverseScopes,
            rhi_webgpu::WebGpuErrorScopeContract::count));
    }

    static void instrumentRecordingException(void* device)
    {
        assert(EM_ASM_INT(
            {
                // clang-format off
                    const d = WebGPU.getJsObject($0);
                    if (!d || Module.arpRecordingExceptionProbe)
                        return 0;
                    const p = ({device : d, encoder : d.createCommandEncoder});
                    Module.arpRecordingExceptionProbe = p;
                    d.createCommandEncoder = function(desc) {
                        p.encoder.call(this, desc);
                        throw new Error("authored throw after genuine encoder creation");
                    };
                    return 1;
                // clang-format on
            },
            device));
    }

    static void restoreRecordingException()
    {
        EM_ASM({
            // clang-format off
                const p = Module.arpRecordingExceptionProbe;
                p.device.createCommandEncoder = p.encoder;
                delete Module.arpRecordingExceptionProbe;
            // clang-format on
        });
    }

    // Unknown partial native push: throw only after an actual scope was pushed.
    static void instrumentScopePush(void* device)
    {
        assert(EM_ASM_INT(
            {
                // clang-format off
                    const d = WebGPU.getJsObject($0);
                    if (!d || Module.arpScopePushProbe)
                        return 0;
                    const p = ({device : d, push : d.pushErrorScope, count : 0});
                    Module.arpScopePushProbe = p;
                    d.pushErrorScope = function(filter) {
                        p.push.call(this, filter);
                        if (++p.count === 2)
                            throw new Error("authored partial native scope push");
                    };
                    return 1;
                // clang-format on
            },
            device));
    }

    static void restoreScopePush()
    {
        EM_ASM({
            // clang-format off
                const p = Module.arpScopePushProbe;
                p.device.pushErrorScope = p.push;
                delete Module.arpScopePushProbe;
            // clang-format on
        });
    }

    static void restoreRecording()
    {
        EM_ASM({
            // clang-format off
                const p = Module.canonicalRecordingProbe;
                p.device.createCommandEncoder = p.encoder;
                p.device.popErrorScope = p.pop;
                delete Module.canonicalRecordingProbe;
            // clang-format on
        });
    }

    // Instrument native methods, preserve real GPU objects and native promises.
    static void instrument(WGPUDevice device,
        WGPUQueue queue,
        Probe probe,
        void* context,
        void (*observe)(void*) noexcept)
    {
        assert(EM_ASM_INT({
            // clang-format off
                const device = WebGPU.getJsObject($0);
                const queue = WebGPU.getJsObject($1);
                if (Module.canonicalSubmissionProbe)
                    return 0;
                const p = ({
                    device,
                    queue,
                    submit : queue.submit,
                    pop : device.popErrorScope,
                    done : queue.onSubmittedWorkDone,
                    encoder : device.createCommandEncoder,
                    probe : $2,
                    context : $3,
                    observe : $4,
                    count : 0,
                    received : 0,
                    receipts : new Array(4)
                });
                Module.canonicalSubmissionProbe = p;
                queue.submit = function(buffers) {
                    getWasmTableEntry(p.observe)(p.context);
                    if (p.probe === 1)
                        throw new Error("test before native invocation");
                    if (p.probe === 3) {
                        // Submit the same genuine command twice: specified native
                        // GPUValidationError is captured by the submission scopes.
                        p.submit.call(this, buffers);
                        p.submit.call(this, buffers);
                        return;
                    }
                    p.submit.call(this, buffers);
                    if (p.probe === 2)
                        throw new Error("test after native invocation");
                };
                if (p.probe === 4) {
                    const delay = function(nativePromise, index) {
                        return new Promise(function(resolve, reject) {
                            nativePromise.then(function(value) {
                                p.receipts[index] = ({resolve, value});
                                if (++p.received === p.receipts.length)
                                    for (let i = p.receipts.length - 1; i >= 0; --i)
                                        p.receipts[i].resolve(p.receipts[i].value);
                            }, reject);
                        });
                    };
                    device.popErrorScope = function() {
                        return delay(p.pop.call(this), p.count++);
                    };
                    queue.onSubmittedWorkDone = function() {
                        return delay(p.done.call(this), p.count++);
                    };
                }
                if (p.probe === 5)
                    device.popErrorScope = function() {
                        const promise = p.pop.call(this);
                        if (p.count++ !== 0)
                            return promise;
                        return {
                            then : function(resolve, reject) {
                                promise.then(resolve, reject);
                                throw new Error("test partial scope registration");
                            }
                        };
                    };
                return 1;
    // clang-format on
        },device,queue,static_cast<unsigned>(probe),context,reinterpret_cast<std::uintptr_t>(observe)));
        }

        static void restore()
        {
            EM_ASM({
                // clang-format off
                    const p = Module.canonicalSubmissionProbe;
                    p.queue.submit = p.submit;
                    p.device.popErrorScope = p.pop;
                    p.queue.onSubmittedWorkDone = p.done;
                    delete Module.canonicalSubmissionProbe;
                // clang-format on
            });
        }

        static void instrumentLoss(
            void* device, WGPUQueue queue, bool uncertainQueue = false, bool uncertainScope = false)
        {
        assert(EM_ASM_INT({
        // clang-format off
                const device = WebGPU.getJsObject($0);
                const queue = WebGPU.getJsObject($1);
                if (Module.canonicalSubmissionLossProbe)
                    return 0;
                const p = ({
                    device,
                    queue,
                    pop : device.popErrorScope,
                    done : queue.onSubmittedWorkDone,
                    uncertain : $2,
                    scope : $3,
                    count : 0
                });
                Module.canonicalSubmissionLossProbe = p;
                const delay = function(promise) {
                    return promise.then(
                        function(value) {
                            return device.lost.then(function() {
                                return value;
                            });
                        },
                        function(error) {
                            return device.lost.then(function() {
                                throw error;
                            });
                        });
                };
                device.popErrorScope = function() {
                    const promise = p.pop.call(this);
                    if (p.scope && p.count++ === 0)
                        throw new Error("test after native scope pop");
                    return p.uncertain ? promise : delay(promise);
                };
                queue.onSubmittedWorkDone = function() {
                    const promise = p.done.call(this);
                    if (p.uncertain)
                        return {
                            then : function(resolve, reject) {
                                promise.then(resolve, reject);
                                throw new Error("test partial queue registration");
                            }
                        };
                    return delay(promise);
                };
                return 1;
        // clang-format on
        },device,queue,uncertainQueue,uncertainScope));
        }

        static void restoreLoss()
        {
            EM_ASM({
                // clang-format off
                    const p = Module.canonicalSubmissionLossProbe;
                    p.device.popErrorScope = p.pop;
                    p.queue.onSubmittedWorkDone = p.done;
                    delete Module.canonicalSubmissionLossProbe;
                // clang-format on
            });
        }
#endif
        }
        ;
        }
