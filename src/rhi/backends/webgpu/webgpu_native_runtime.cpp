#include "rhi/backends/webgpu/webgpu_native_runtime.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_attachment_operation_lowering.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_completion_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_invocation.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#include "rhi/backends/webgpu/webgpu_original_device_native_loss.h"
#include "rhi/frontend/transfer_readback_statistics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

#if defined(__EMSCRIPTEN__) && defined(__has_include)
#if __has_include(<webgpu/webgpu.h>)
#define ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE 1
#include <emscripten/emscripten.h>
#include <emscripten/val.h>
#include <webgpu/webgpu.h>
#endif
#endif

#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)

EM_JS_DEPS(arpWebGpuNativeLossObserver, "$getWasmTableEntry");
EM_JS_DEPS(arpWebGpuOriginalLossRegistration, "$Emval,$getWasmTableEntry");
EM_JS_DEPS(arpWebGpuNativeCompletionReceipt, "$Emval,$getWasmTableEntry");
EM_JS_DEPS(arpWebGpuNativeMapReceipt, "$stackSave,$stackRestore,$getWasmTableEntry");
#endif

namespace arp::rhi_webgpu {

WebGpuOriginalDeviceLossSource::WebGpuOriginalDeviceLossSource()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (webGpuNativeDeviceAvailable())
        device_ = emscripten_webgpu_get_device();
#endif
}

const char* WebGpuNativeBufferMapReceipt::prepareCold(
    void* buffer, std::size_t bytes, void* userdata, Callback callback) noexcept
{
    return prepareColdRange(buffer, {bytes, 0u, bytes}, userdata, callback);
}

const char* WebGpuNativeBufferMapReceipt::prepareColdRange(
    void* buffer, const Range& range, void* userdata, Callback callback) noexcept
{
    if (phase_ != Phase::Empty)
        return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-ONE-SHOT";
    if (!buffer || !userdata || !callback || range.sizeBytes > SIZE_MAX)
        return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-BINDING";
    if (const auto* id = range.diagnostic())
        return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    const auto native = static_cast<WGPUBuffer>(buffer);
    if (wgpuBufferGetSize(native) != range.allocationBytes
        || wgpuBufferGetUsage(native) != (WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst))
        return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-LAYOUT";
    if (!WebGpuNativeBufferMapState::matches(native, WGPUBufferMapState_Unmapped))
        return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-STATE";
    buffer_ = buffer;
    range_ = range;
    userdata_ = userdata;
    callback_ = callback;
    phase_ = Phase::Prepared;
    return nullptr;
#else
    return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNAVAILABLE";
#endif
}

const char* WebGpuNativeBufferMapReceipt::validateCold() const noexcept
{
    if (phase_ != Phase::Prepared || !buffer_ || range_.diagnostic() || range_.sizeBytes > SIZE_MAX
        || !userdata_ || !callback_)
        return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-BINDING";
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    const auto native = static_cast<WGPUBuffer>(buffer_);
    return wgpuBufferGetSize(native) == range_.allocationBytes
            && wgpuBufferGetUsage(native) == (WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst)
            && WebGpuNativeBufferMapState::matches(native, WGPUBufferMapState_Unmapped)
        ? nullptr
        : "RHI-WEBGPU-NATIVE-MAP-RECEIPT-STATE";
#else
    return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNAVAILABLE";
#endif
}

const char* WebGpuNativeBufferMapReceipt::validateRetainedBinding(
    void* buffer, std::size_t bytes) const noexcept
{
    return validateRetainedRange(buffer, {bytes, 0u, bytes});
}

const char* WebGpuNativeBufferMapReceipt::validateRetainedRange(
    void* buffer, const Range& range) const noexcept
{
    if (buffer_ != buffer || !range_.matches(range) || !buffer || range.diagnostic()
        || range.sizeBytes > SIZE_MAX || phase_ == Phase::Empty)
        return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-BINDING";
    if (invocationUncertain_)
        return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNPROVEN";
    if (phase_ == Phase::Prepared)
        return validateCold();
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!futureIssued_ || (phase_ == Phase::Delivered && !success_))
        return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNPROVEN";
    const auto native = static_cast<WGPUBuffer>(buffer_);
    if (wgpuBufferGetSize(native) != range_.allocationBytes
        || wgpuBufferGetUsage(native) != (WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst))
        return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-LAYOUT";
    const auto expected = phase_ == Phase::Pending ? WGPUBufferMapState_Pending
        : unmapProven_                             ? WGPUBufferMapState_Unmapped
                                                   : WGPUBufferMapState_Mapped;
    return WebGpuNativeBufferMapState::matches(native, expected)
        ? nullptr
        : "RHI-WEBGPU-NATIVE-MAP-RECEIPT-STATE";
#else
    return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNAVAILABLE";
#endif
}

bool WebGpuNativeInvocation::guarded(void (*invoke)(void*) noexcept, void* self) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    // The C thunk has only scalar/POD locals, so a foreign exception cannot
    // bypass owned-object destructors. It never invokes JS-only mapAsync.
    return EM_ASM_INT(
               {
                   // clang-format off
                   const saved = stackSave();
                   try {
                       getWasmTableEntry($0)($1);
                       return 1;
                   } catch (error) {
                       stackRestore(saved);
                       return 0;
                   }
                   // clang-format on
               },
               reinterpret_cast<std::uintptr_t>(invoke),
               self)
        != 0;
#else
    (void)invoke;
    (void)self;
    return false;
#endif
}

void WebGpuNativeBufferMapReceipt::invokeMap(void* userdata) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    auto& self = *static_cast<WebGpuNativeBufferMapReceipt*>(userdata);
    auto info = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.callback = &completeMap;
    info.userdata1 = &self;
    self.futureIssued_ = wgpuBufferMapAsync(static_cast<WGPUBuffer>(self.buffer_),
                             WGPUMapMode_Read,
                             self.range_.offsetBytes,
                             static_cast<std::size_t>(self.range_.sizeBytes),
                             info)
                             .id
        != 0u;
#else
    (void)userdata;
#endif
}

void WebGpuNativeBufferMapReceipt::invokeRead(void* userdata) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    auto& self = *static_cast<WebGpuNativeBufferMapReceipt*>(userdata);
    self.readSucceeded_ = wgpuBufferReadMappedRange(static_cast<WGPUBuffer>(self.buffer_),
                              self.range_.offsetBytes,
                              self.destination_,
                              static_cast<std::size_t>(self.range_.sizeBytes))
        == WGPUStatus_Success;
#else
    (void)userdata;
#endif
}

void WebGpuNativeBufferMapReceipt::invokeUnmap(void* userdata) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    auto& self = *static_cast<WebGpuNativeBufferMapReceipt*>(userdata);
    wgpuBufferUnmap(static_cast<WGPUBuffer>(self.buffer_));
#else
    (void)userdata;
#endif
}

const char* WebGpuNativeBufferMapReceipt::start() noexcept
{
    if (const auto* id = validateCold())
        return id;
    phase_ = Phase::Pending;
    if (!WebGpuNativeInvocation::guarded(&invokeMap, this)) {
        invocationUncertain_ = true;
        return "RHI-WEBGPU-NATIVE-MAP-INVOCATION-UNCERTAIN";
    }
    if (!futureIssued_) {
        invocationUncertain_ = true;
        return "RHI-WEBGPU-NATIVE-MAP-FUTURE-UNPROVEN";
    }
    return nullptr;
}

void WebGpuNativeBufferMapReceipt::invokeDestroy(void* userdata) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    wgpuBufferDestroy(
        static_cast<WGPUBuffer>(static_cast<WebGpuNativeBufferMapReceipt*>(userdata)->buffer_));
#else
    (void)userdata;
#endif
}

const char* WebGpuNativeBufferMapReceipt::destroyAndDrainAfterNativeLoss() noexcept
{
    if (phase_ != Phase::Pending && phase_ != Phase::Delivered)
        return "RHI-WEBGPU-NATIVE-MAP-DESTROY-STATUS";
    if (!WebGpuNativeInvocation::guarded(&invokeDestroy, this))
        return "RHI-WEBGPU-NATIVE-MAP-DESTROY-UNCERTAIN";
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    unmapProven_ = phase_ == Phase::Delivered
        && WebGpuNativeBufferMapState::matches(
            static_cast<WGPUBuffer>(buffer_), WGPUBufferMapState_Unmapped);
    return unmapProven_ ? nullptr : "RHI-WEBGPU-NATIVE-MAP-DESTROY-DRAIN-UNPROVEN";
#else
    return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNAVAILABLE";
#endif
}

const char* WebGpuNativeBufferMapReceipt::abortAndDrain() noexcept
{
    if (phase_ != Phase::Pending && phase_ != Phase::Delivered)
        return "RHI-WEBGPU-NATIVE-MAP-UNMAP-STATUS";
    if (!WebGpuNativeInvocation::guarded(&invokeUnmap, this)) {
        invocationUncertain_ = true;
        return "RHI-WEBGPU-NATIVE-MAP-UNMAP-UNCERTAIN";
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    unmapProven_ = WebGpuNativeBufferMapState::matches(
        static_cast<WGPUBuffer>(buffer_), WGPUBufferMapState_Unmapped);
    return unmapProven_ ? nullptr : "RHI-WEBGPU-NATIVE-MAP-UNMAP-UNPROVEN";
#else
    return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNAVAILABLE";
#endif
}

const char* WebGpuNativeBufferMapReceipt::readAndUnmap(
    void* destination, std::size_t bytes) noexcept
{
    if (phase_ != Phase::Delivered || !success_ || invocationUncertain_)
        return "RHI-WEBGPU-NATIVE-MAP-READ-STATUS";
    if (!destination || bytes != range_.sizeBytes)
        return "RHI-WEBGPU-NATIVE-MAP-READ-RANGE";
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!WebGpuNativeBufferMapState::matches(
            static_cast<WGPUBuffer>(buffer_), WGPUBufferMapState_Mapped))
        return "RHI-WEBGPU-NATIVE-MAP-READ-STATE";
    destination_ = destination;
    const auto read = WebGpuNativeInvocation::guarded(&invokeRead, this);
    destination_ = nullptr;
    const char* failure = nullptr;
    if (!read) {
        invocationUncertain_ = true;
        failure = "RHI-WEBGPU-NATIVE-MAP-READ-UNCERTAIN";
    } else if (!readSucceeded_)
        failure = "RHI-WEBGPU-NATIVE-MAP-READ-NATIVE-ERROR";
    if (const auto* id = abortAndDrain())
        return id;
    return failure;
#else
    return "RHI-WEBGPU-NATIVE-MAP-RECEIPT-UNAVAILABLE";
#endif
}
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
void WebGpuNativeBufferMapReceipt::completeMap(
    WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*) noexcept
{
    auto& self = *static_cast<WebGpuNativeBufferMapReceipt*>(userdata);
    if (self.phase_ != Phase::Pending)
        return;
    self.success_ = status == WGPUMapAsyncStatus_Success
        && WebGpuNativeBufferMapState::matches(
            static_cast<WGPUBuffer>(self.buffer_), WGPUBufferMapState_Mapped);
    self.phase_ = Phase::Delivered;
    // All receipt mutation precedes delivery; consumer retains its original slot.
    self.callback_(self.userdata_, self.success_ ? Outcome::Success : Outcome::NativeError);
}
#endif

const char* WebGpuNativeCompletionReceipt::inspect(void* device, void* queue) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    const auto result = EM_ASM_INT(
        {
            // clang-format off
            try {
                var device = WebGPU.getJsObject($0);
                var queue = WebGPU.getJsObject($1);
                if (!device || !queue || queue !== device.queue || typeof queue.submit !== "function" ||
                    typeof queue.onSubmittedWorkDone !== "function" ||
                    typeof device.pushErrorScope !== "function" ||
                    typeof device.popErrorScope !== "function")
                    return 0;
                return device.arpCanonicalScopeStackUnproven ? -1 : 1;
            } catch (error) {
                return 0;
            }
            // clang-format on
        },
        device,
        queue);
    if (result < 0)
        return "RHI-WEBGPU-CANONICAL-SCOPE-STACK-UNPROVEN";
    return result == 1 ? nullptr : "RHI-WEBGPU-CANONICAL-COMPLETION-API-UNAVAILABLE";
#else
    (void)device;
    (void)queue;
    return "RHI-WEBGPU-CANONICAL-COMPLETION-NATIVE-UNAVAILABLE";
#endif
}

WebGpuNativeCompletionReceipt::Prepared WebGpuNativeCompletionReceipt::prepare(
    void* device, void* queue, Kind kind, void* userdata, Callback complete) noexcept
{
    if (!userdata || !complete || inspect(device, queue))
        return {};
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return Prepared(static_cast<std::uintptr_t>(EM_ASM_INT(
        {
            // clang-format off
            var device = WebGPU.getJsObject($0);
            var queue = WebGPU.getJsObject($1);
            try {
                // NativeCompletionReceipt/1 phases are the passed enum constants.
                // Foreign state and exactly two listeners are allocated cold.
                if (device.arpCanonicalScopeStackUnproven === undefined)
                    Object.defineProperty(device, "arpCanonicalScopeStackUnproven",
                                          {value : false, writable : true});
                var property = Object.getOwnPropertyDescriptor(device, "arpCanonicalScopeStackUnproven");
                if (!property || !property.writable || property.get || property.set)
                    return 0;
                var state = ({
                    device : device,
                    queue : queue,
                    scope : $2,
                    userdata : $3,
                    complete : $4,
                    phase : $8,
                    prepared : $8,
                    bound : $9,
                    delivered : $10,
                    suppressed : $11,
                    success : $5,
                    error : $6,
                    rejected : $7,
                    delivering : false,
                    deliveryUserdata : 0,
                    deliveryCallback : 0,
                    deliveryOutcome : 0
                });
                state.resolved = function(value) {
                    if (state.phase !== state.bound)
                        return;
                    var outcome = state.scope ? (value === null ? state.success : state.error)
                                              : (value === undefined ? state.success : state.error);
                    var data = state.userdata;
                    var callback = state.complete;
                    state.phase = state.delivered;
                    state.userdata = 0;
                    state.complete = 0;
                    state.delivering = true;
                    state.deliveryUserdata = data;
                    state.deliveryCallback = callback;
                    state.deliveryOutcome = outcome;
                    try {
                        getWasmTableEntry(callback)(data, outcome);
                    } finally {
                        state.delivering = false;
                        state.deliveryUserdata = 0;
                        state.deliveryCallback = 0;
                        state.deliveryOutcome = 0;
                    }
                };
                state.failed = function() {
                    if (state.phase !== state.bound)
                        return;
                    var data = state.userdata;
                    var callback = state.complete;
                    state.phase = state.delivered;
                    state.userdata = 0;
                    state.complete = 0;
                    state.delivering = true;
                    state.deliveryUserdata = data;
                    state.deliveryCallback = callback;
                    state.deliveryOutcome = state.rejected;
                    try {
                        getWasmTableEntry(callback)(data, state.rejected);
                    } finally {
                        state.delivering = false;
                        state.deliveryUserdata = 0;
                        state.deliveryCallback = 0;
                        state.deliveryOutcome = 0;
                    }
                };
                return Emval.toHandle(state);
            } catch (error) {
                return 0;
            }
            // clang-format on
        },
        device,
        queue,
        kind == Kind::Scope,
        userdata,
        reinterpret_cast<std::uintptr_t>(complete),
        static_cast<std::uint32_t>(Outcome::Success),
        static_cast<std::uint32_t>(Outcome::NativeError),
        static_cast<std::uint32_t>(Outcome::Rejected),
        static_cast<std::uint32_t>(Phase::Prepared),
        static_cast<std::uint32_t>(Phase::Bound),
        static_cast<std::uint32_t>(Phase::Delivered),
        static_cast<std::uint32_t>(Phase::Suppressed))));
#else
    (void)kind;
    return {};
#endif
}

bool WebGpuNativeCompletionReceipt::claimScopeTransaction(void* device, const void* owner) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return device && owner
        && EM_ASM_INT(
            {
                // clang-format off
        const device = WebGPU.getJsObject($0);
        if (!device || device.arpSharedScopeOwner) return 0;
        device.arpSharedScopeOwner = $1;
        return 1;
                // clang-format on
            },
            device,
            owner);
#else
    (void)device;
    (void)owner;
    return false;
#endif
}

bool WebGpuNativeCompletionReceipt::releaseScopeTransaction(
    void* device, const void* owner) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return device && owner
        && EM_ASM_INT(
            {
                // clang-format off
        const device = WebGPU.getJsObject($0);
        if (!device || device.arpSharedScopeOwner !== $1) return 0;
        device.arpSharedScopeOwner = 0;
        return 1;
                // clang-format on
            },
            device,
            owner);
#else
    (void)device;
    (void)owner;
    return false;
#endif
}

bool WebGpuNativeCompletionReceipt::pushScopes(void* device) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return EM_ASM_INT(
               {
                   // clang-format off
                   var device = WebGPU.getJsObject($0);
                   if (!device)
                       return 0;
                   try {
                       // ErrorScopeContract/1: Internal, OutOfMemory, Validation (LIFO pop).
                       device.pushErrorScope("internal");
                       device.pushErrorScope("out-of-memory");
                       device.pushErrorScope("validation");
                       return 1;
                   } catch (error) {
                       device.arpCanonicalScopeStackUnproven = true;
                       return 0;
                   }
                   // clang-format on
               },
               device)
        != 0;
#else
    (void)device;
    return false;
#endif
}

bool WebGpuNativeCompletionReceipt::invoke(void* queue, void* commandBuffer) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return EM_ASM_INT(
               {
                   // clang-format off
                   var queue = WebGPU.getJsObject($0);
                   var buffer = WebGPU.getJsObject($1);
                   if (!queue || !buffer)
                       return 0;
                   try {
                       queue.submit([ buffer ]);
                       return 1;
                   } catch (error) {
                       return 0;
                   }
                   // clang-format on
               },
               queue,
               commandBuffer)
        != 0;
#else
    (void)queue;
    (void)commandBuffer;
    return false;
#endif
}

bool WebGpuNativeCompletionReceipt::writeBuffer(
    void* queue, void* buffer, const void* bytes, std::uint64_t size) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!queue || !buffer || !bytes || !size || size > SIZE_MAX)
        return false;
    return EM_ASM_INT(
               {
                   // clang-format off
                   const queue = WebGPU.getJsObject($0);
                   const buffer = WebGPU.getJsObject($1);
                   if (!queue || !buffer)
                       return 0;
                   try {
                       queue.writeBuffer(buffer, 0, HEAPU8.subarray($2, $2 + $3), 0, $3);
                       return 1;
                   } catch (error) {
                       return 0;
                   }
                   // clang-format on
               },
               queue,
               buffer,
               bytes,
               static_cast<std::size_t>(size))
        != 0;
#else
    (void)queue;
    (void)buffer;
    (void)bytes;
    (void)size;
    return false;
#endif
}

bool WebGpuNativeCompletionReceipt::bind(std::uintptr_t receipt) noexcept
{
    if (!receipt)
        return false;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return EM_ASM_INT(
               {
                   // clang-format off
                   var state = Emval.toValue($0);
                   if (!state || state.phase !== state.prepared)
                       return 0;
                   state.phase = state.bound;
                   var popped = false;
                   try {
                       var promise =
                           state.scope ? state.device.popErrorScope() : state.queue.onSubmittedWorkDone();
                       popped = state.scope;
                       if (!promise || typeof promise.then !== "function") {
                           state.phase = state.suppressed;
                           state.userdata = 0;
                           state.complete = 0;
                           return 0;
                       }
                       promise.then(state.resolved, state.failed);
                       return 1;
                   } catch (error) {
                       // Even a partially attached listener now sees cleared userdata.
                       // A thrown pop has unknown stack effects; further canonical work
                       // fails closed until the physical device is replaced.
                       if (state.scope && !popped)
                           state.device.arpCanonicalScopeStackUnproven = true;
                       state.phase = state.suppressed;
                       state.userdata = 0;
                       state.complete = 0;
                       return 0;
                   }
                   // clang-format on
               },
               receipt)
        != 0;
#else
    return false;
#endif
}

bool WebGpuNativeCompletionReceipt::prepared(std::uintptr_t receipt) noexcept
{
    if (!receipt)
        return false;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return EM_ASM_INT(
               {
                   // clang-format off
                   var state = Emval.toValue($0);
                   return !!state && state.phase === state.prepared;
                   // clang-format on
               },
               receipt)
        != 0;
#else
    return false;
#endif
}

bool WebGpuNativeCompletionReceipt::matchesPrepared(std::uintptr_t receipt,
    void* device,
    void* queue,
    Kind kind,
    void* userdata,
    Callback callback) noexcept
{
    if (!receipt || !device || !queue || !userdata || !callback)
        return false;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return EM_ASM_INT(
               {
                   // clang-format off
                   var state = Emval.toValue($0);
                   return !!state && state.phase === state.prepared && state.device === WebGPU.getJsObject($1) &&
                          state.queue === WebGPU.getJsObject($2) && state.scope === $3 && state.userdata === $4 &&
                          state.complete === $5;
                   // clang-format on
               },
               receipt,
               device,
               queue,
               kind == Kind::Scope,
               userdata,
               reinterpret_cast<std::uintptr_t>(callback))
        != 0;
#else
    (void)kind;
    return false;
#endif
}

bool WebGpuNativeCompletionReceipt::matchesIssued(std::uintptr_t receipt,
    void* device,
    void* queue,
    Kind kind,
    void* userdata,
    Callback callback) noexcept
{
    if (!receipt || !device || !queue || !userdata || !callback)
        return false;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return EM_ASM_INT(
               {
                   // clang-format off
                   const s = Emval.toValue($0);
                   return !!s && s.device === WebGPU.getJsObject($1) && s.queue === WebGPU.getJsObject($2) &&
                          s.scope === $3 &&
                          ((s.phase === s.bound && s.userdata === $4 && s.complete === $5) ||
                           (s.phase === s.delivered && !s.userdata && !s.complete));
                   // clang-format on
               },
               receipt,
               device,
               queue,
               kind == Kind::Scope,
               userdata,
               reinterpret_cast<std::uintptr_t>(callback))
        != 0;
#else
    (void)kind;
    return false;
#endif
}

bool WebGpuNativeCompletionReceipt::matchesDelivery(std::uintptr_t receipt,
    void* device,
    void* queue,
    Kind kind,
    void* userdata,
    Callback callback,
    Outcome outcome) noexcept
{
    if (!matchesIssued(receipt, device, queue, kind, userdata, callback))
        return false;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return EM_ASM_INT(
               {
                   // clang-format off
                   const s = Emval.toValue($0);
                   return s.phase === s.delivered && s.delivering && s.deliveryUserdata === $1 &&
                          s.deliveryCallback === $2 && s.deliveryOutcome === $3;
                   // clang-format on
               },
               receipt,
               userdata,
               reinterpret_cast<std::uintptr_t>(callback),
               static_cast<std::uint32_t>(outcome))
        != 0;
#else
    (void)outcome;
    return false;
#endif
}

void WebGpuNativeCompletionReceipt::release(std::uintptr_t receipt) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (receipt) {
        EM_ASM(
            {
                // clang-format off
                var state = Emval.toValue($0);
                state.phase = state.suppressed;
                state.userdata = 0;
                state.complete = 0;
                // clang-format on
            },
            receipt);
        auto released =
            emscripten::val::take_ownership(reinterpret_cast<emscripten::EM_VAL>(receipt));
    }
#else
    (void)receipt;
#endif
}

std::uintptr_t prepareWebGpuNativeDeviceLossRegistration(
    void* nativeDevice, void* userdata, void (*complete)(void*)) noexcept
{
    if (!nativeDevice || !userdata || !complete)
        return 0u;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    const auto state = static_cast<std::uintptr_t>(EM_ASM_INT(
        {
            // clang-format off
            var device = WebGPU.getJsObject($0);
            if (!device || !device.lost || typeof device.lost.then !== 'function')
                return 0;
            var state = ({phase : 1, userdata : $1, callback : $2, observed : false, deliveries : 0});
            // Allocate the foreign reference before binding the promise. A thrown
            // binding suppresses captured userdata before releasing the reference.
            var handle = Emval.toHandle(state);
            try {
                device.lost.then(function(info) {
                    state.observed = true;
                    if (state.phase !== 1)
                        return;
                    var context = state.userdata;
                    var callback = state.callback;
                    state.phase = 2;
                    state.userdata = 0;
                    state.callback = 0;
                    ++state.deliveries;
                    if (device === Module.preinitializedWebGPUDevice)
                        Module.webGpuDeviceLostInfo =
                            String(info.reason) + ": " + String(info.message || "");
                    getWasmTableEntry(callback)(context);
                });
            } catch (error) {
                state.phase = 3;
                state.userdata = 0;
                state.callback = 0;
                return handle;
            }
            return handle;
            // clang-format on
        },
        nativeDevice,
        userdata,
        reinterpret_cast<std::uintptr_t>(complete)));
    if (state
        && EM_ASM_INT(
            {
                // clang-format off
                return Emval.toValue($0).phase === 1;
                // clang-format on
            },
            state))
        return state;
    releaseWebGpuNativeDeviceLossRegistration(state);
    return 0u;
#else
    return 0u;
#endif
}

bool detachWebGpuNativeDeviceLossRegistration(std::uintptr_t state) noexcept
{
    if (!state)
        return false;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return EM_ASM_INT(
               {
                   // clang-format off
                   var state = Emval.toValue($0);
                   if (!state || state.phase !== 1)
                       return 0;
                   state.phase = 3;
                   state.userdata = 0;
                   state.callback = 0;
                   return 1;
                   // clang-format on
               },
               state)
        != 0;
#else
    return false;
#endif
}

void releaseWebGpuNativeDeviceLossRegistration(std::uintptr_t state) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (state) {
        // Use the supported val ownership API rather than internal decref.
        auto released =
            emscripten::val::take_ownership(reinterpret_cast<emscripten::EM_VAL>(state));
    }
#else
    (void)state;
#endif
}

bool observeWebGpuNativeDeviceLoss(
    void* nativeDevice, void* userdata, void (*complete)(void*)) noexcept
{
    if (!nativeDevice || !userdata || !complete)
        return false;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return EM_ASM_INT(
               {
                   // clang-format off
                   var device = WebGPU.getJsObject($2);
                   if (!device || !device.lost || typeof device.lost.then !== 'function')
                       return 0;
                   var context = $0;
                   var complete = $1;
                   device.lost.then(function(info) {
                       if (device === Module.preinitializedWebGPUDevice)
                           Module.webGpuDeviceLostInfo = String(info.reason) + ": " + String(info.message || "");
                       getWasmTableEntry(complete)(context);
                   });
                   return 1;
                   // clang-format on
               },
               userdata,
               reinterpret_cast<std::uintptr_t>(complete),
               nativeDevice)
        != 0;
#else
    return false;
#endif
}

void WebGpuNativeCompletionState::reset() noexcept
{
    signal_.reset();
}

void WebGpuNativeCompletionState::complete() noexcept
{
    signal_.signalFromHost();
    condition.notify_all();
}

bool WebGpuNativeCompletionState::isComplete() const noexcept
{
    return signal_.isComplete();
}

bool WebGpuNativeCompletionState::completeSignal(SignalTicket ticket) noexcept
{
    if (!signal_.complete(ticket))
        return false;
    condition.notify_all();
    return true;
}

const char* WebGpuNativeCompletionState::validateBudget(
    const WebGpuOrdinaryCompletionSignalBudget& budget) noexcept
{
    if (budget.contractVersion != 1u)
        return "RHI-WEBGPU-ORDINARY-SIGNAL-VERSION";
    if (!budget.prototypeOptIn)
        return "RHI-WEBGPU-ORDINARY-SIGNAL-OPT-IN";
    if (!budget.maxStates || !budget.maxCpuMetadataBytes)
        return "RHI-WEBGPU-ORDINARY-SIGNAL-BUDGET";
    return nullptr;
}

std::shared_ptr<WebGpuNativeCompletionState> WebGpuNativeCompletionState::prepareOrdinarySignal(
    const WebGpuOrdinaryCompletionSignalBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-SIGNAL-QUOTA";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto state = shareOriginal(core::makeUnique<WebGpuNativeCompletionState>());
        state->ordinarySignalPrepared_ = true;
        return state;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-SIGNAL-ALLOCATION";
        return {};
    }
}

void WebGpuNativeCompletionState::wait()
{
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [this] {
        return signal_.isComplete();
    });
}

#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
extern "C" EMSCRIPTEN_KEEPALIVE void arpWebGpuReleaseDeviceHandle(std::uintptr_t deviceHandle)
{
    if (deviceHandle != 0u) {
        wgpuDeviceRelease(reinterpret_cast<WGPUDevice>(deviceHandle));
    }
}
#endif

namespace {

#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
struct GpuTimingReadbackState {
    WGPUBuffer resolveBuffer = nullptr;
    WGPUBuffer readbackBuffer = nullptr;
    std::uint64_t frameSerial = 0u;
};

struct CompletedGpuTiming {
    std::atomic<std::uint64_t> frameSerial{0u};
    std::atomic<std::uint64_t> elapsedNanoseconds{0u};
    std::atomic<bool> readbackFailed{false};
};

CompletedGpuTiming& completedGpuTiming()
{
    static CompletedGpuTiming timing;
    return timing;
}

void publishGpuTimingReadback(WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*)
{
    auto* state = static_cast<GpuTimingReadbackState*>(userdata1);
    bool published = false;
    if (state && state->readbackBuffer && status == WGPUMapAsyncStatus_Success) {
        std::array<std::uint64_t, 2> timestamps{};
        published = wgpuBufferReadMappedRange(
                        state->readbackBuffer, 0u, timestamps.data(), sizeof(timestamps))
                == WGPUStatus_Success
            && timestamps[1] >= timestamps[0];
        if (published) {
            auto& timing = completedGpuTiming();
            timing.elapsedNanoseconds.store(
                timestamps[1] - timestamps[0], std::memory_order_relaxed);
            timing.frameSerial.store(state->frameSerial, std::memory_order_release);
            timing.readbackFailed.store(false, std::memory_order_release);
        }
        wgpuBufferUnmap(state->readbackBuffer);
    }
    if (!published) {
        completedGpuTiming().readbackFailed.store(true, std::memory_order_release);
    }
    if (state) {
        if (state->resolveBuffer) {
            wgpuBufferDestroy(state->resolveBuffer);
            wgpuBufferRelease(state->resolveBuffer);
        }
        if (state->readbackBuffer) {
            wgpuBufferDestroy(state->readbackBuffer);
            wgpuBufferRelease(state->readbackBuffer);
        }
        core::destroyCpuAllocatedUnique<GpuTimingReadbackState>(state);
    }
}
#endif

bool validExtent(const rhi::Extent2D& extent)
{
    return extent.width > 0 && extent.height > 0;
}

#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
void releaseBuffer(WGPUBuffer& buffer)
{
    if (buffer) {
        if (!webGpuNativeDeviceLost()) {
            wgpuBufferDestroy(buffer);
        }
        wgpuBufferRelease(buffer);
        buffer = nullptr;
    }
}

void releaseTexture(WGPUTexture& texture)
{
    if (texture) {
        if (!webGpuNativeDeviceLost())
            wgpuTextureRelease(texture);
        texture = nullptr;
    }
}

void releaseTextureView(WGPUTextureView& view)
{
    if (view) {
        if (!webGpuNativeDeviceLost())
            wgpuTextureViewRelease(view);
        view = nullptr;
    }
}

void releaseSampler(WGPUSampler& sampler)
{
    if (sampler) {
        if (!webGpuNativeDeviceLost())
            wgpuSamplerRelease(sampler);
        sampler = nullptr;
    }
}

void releaseBindGroupLayout(WGPUBindGroupLayout& layout)
{
    if (layout) {
        if (!webGpuNativeDeviceLost())
            wgpuBindGroupLayoutRelease(layout);
        layout = nullptr;
    }
}

void releaseBindGroup(WGPUBindGroup& group)
{
    if (group) {
        if (!webGpuNativeDeviceLost())
            wgpuBindGroupRelease(group);
        group = nullptr;
    }
}

void releaseShaderModule(WGPUShaderModule& module)
{
    if (module) {
        if (!webGpuNativeDeviceLost())
            wgpuShaderModuleRelease(module);
        module = nullptr;
    }
}

void releasePipelineLayout(WGPUPipelineLayout& layout)
{
    if (layout) {
        if (!webGpuNativeDeviceLost())
            wgpuPipelineLayoutRelease(layout);
        layout = nullptr;
    }
}

void releaseComputePipeline(WGPUComputePipeline& pipeline)
{
    if (pipeline) {
        if (!webGpuNativeDeviceLost())
            wgpuComputePipelineRelease(pipeline);
        pipeline = nullptr;
    }
}

void releaseCommandEncoder(WGPUCommandEncoder& encoder)
{
    if (encoder) {
        if (!webGpuNativeDeviceLost())
            wgpuCommandEncoderRelease(encoder);
        encoder = nullptr;
    }
}

void releaseComputePassEncoder(WGPUComputePassEncoder& pass)
{
    if (pass) {
        if (!webGpuNativeDeviceLost())
            wgpuComputePassEncoderRelease(pass);
        pass = nullptr;
    }
}

void releaseRenderPassEncoder(WGPURenderPassEncoder& pass)
{
    if (pass) {
        if (!webGpuNativeDeviceLost())
            wgpuRenderPassEncoderRelease(pass);
        pass = nullptr;
    }
}

void releaseCommandBuffer(WGPUCommandBuffer& commandBuffer)
{
    if (commandBuffer) {
        if (!webGpuNativeDeviceLost())
            wgpuCommandBufferRelease(commandBuffer);
        commandBuffer = nullptr;
    }
}

void releaseQueue(WGPUQueue& queue)
{
    if (queue) {
        if (!webGpuNativeDeviceLost())
            wgpuQueueRelease(queue);
        queue = nullptr;
    }
}

void releaseDevice(WGPUDevice& device)
{
    if (device) {
        wgpuDeviceRelease(device);
        device = nullptr;
    }
}

void releaseSurface(WGPUSurface& surface)
{
    if (surface) {
        if (!webGpuNativeDeviceLost())
            wgpuSurfaceRelease(surface);
        surface = nullptr;
    }
}

void releaseInstance(WGPUInstance& instance)
{
    if (instance) {
        if (!webGpuNativeDeviceLost())
            wgpuInstanceRelease(instance);
        instance = nullptr;
    }
}

bool browserWebGpuEnvironmentAvailable()
{
    return emscripten_run_script_int("typeof navigator !== 'undefined' && !!navigator.gpu"
                                     "&& typeof Module !== 'undefined'"
                                     "&& !!Module.preinitializedWebGPUDevice"
                                     "&& !Module.webGpuDeviceLostInfo")
        != 0;
}

bool browserWebGpuTimestampQueryApiAvailable()
{
    return rhi::semantics::WebGpuTimestampCommandPointGate::decide(
        webGpuNativeTimestampCommandPointFacts())
        .apiSupported;
}

struct AsyncReadbackState {
    WGPUBuffer buffer = nullptr;
    std::uint32_t rowPitch = 256u;
    std::array<std::uint8_t, 4> expectedPixel{32u, 64u, 127u, 255u};
    float expectedDepth = 0.25f;
};

void publishAsyncReadbacks(WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*)
{
    auto* state = static_cast<AsyncReadbackState*>(userdata1);
    bool colorPassed = status == WGPUMapAsyncStatus_Success && state != nullptr;
    bool depthPassed = colorPassed;
    std::array<std::uint8_t, 4> observedPixel{0u, 0u, 0u, 0u};
    std::array<std::uint8_t, 4> mismatchPixel{0u, 0u, 0u, 0u};
    std::uint32_t mismatchX = 0u;
    std::uint32_t mismatchY = 0u;
    bool mappedAvailable = false;
    if (colorPassed) {
        std::array<std::uint8_t, 16u> rowBytes{};
        bool colorRowsReadable = true;
        for (std::uint32_t y = 0; y < 4u && colorRowsReadable; ++y) {
            colorRowsReadable = wgpuBufferReadMappedRange(state->buffer,
                                    static_cast<std::size_t>(y) * state->rowPitch,
                                    rowBytes.data(),
                                    rowBytes.size())
                == WGPUStatus_Success;
            if (!colorRowsReadable)
                break;
            if (y == 0u) {
                std::memcpy(observedPixel.data(), rowBytes.data(), observedPixel.size());
            }
            for (std::uint32_t x = 0; x < 4u; ++x) {
                const auto* pixel = rowBytes.data() + x * 4u;
                if (std::memcmp(pixel, state->expectedPixel.data(), 4u) != 0 && colorPassed) {
                    std::memcpy(mismatchPixel.data(), pixel, mismatchPixel.size());
                    mismatchX = x;
                    mismatchY = y;
                    colorPassed = false;
                }
            }
        }
        colorPassed = colorPassed && colorRowsReadable;
        bool depthRowsReadable = true;
        for (std::uint32_t y = 0; y < 4u && depthRowsReadable; ++y) {
            depthRowsReadable = wgpuBufferReadMappedRange(state->buffer,
                                    static_cast<std::size_t>(4u + y) * state->rowPitch,
                                    rowBytes.data(),
                                    rowBytes.size())
                == WGPUStatus_Success;
            if (!depthRowsReadable)
                break;
            for (std::uint32_t x = 0; x < 4u; ++x) {
                float observedDepth = 0.0f;
                const auto* depth = rowBytes.data() + x * sizeof(float);
                std::memcpy(&observedDepth, depth, sizeof(observedDepth));
                if (std::memcmp(&observedDepth, &state->expectedDepth, sizeof(observedDepth))
                    != 0) {
                    depthPassed = false;
                }
            }
        }
        depthPassed = depthPassed && depthRowsReadable;
        mappedAvailable = colorRowsReadable && depthRowsReadable;
    }

    if (state != nullptr) {
        if (state->buffer && status == WGPUMapAsyncStatus_Success && mappedAvailable) {
            wgpuBufferUnmap(state->buffer);
        }
        if (state->buffer) {
            wgpuBufferDestroy(state->buffer);
            wgpuBufferRelease(state->buffer);
            state->buffer = nullptr;
        }
    }

    char script[640] = {};
    std::snprintf(script,
        sizeof(script),
        "document.body.dataset.woccReadback='%s';"
        "document.body.dataset.woccDepthReadback='%s';"
        "document.body.dataset.woccReadbackPixel='%u,%u,%u,%u';"
        "document.body.dataset.woccReadbackMismatch='%u,%u:%u,%u,%u,%u';"
        "console.%s('[webgpu-rhi-smoke] async readbacks color=%s depth=%s pixel=%u,%u,%u,%u "
        "mismatch=%u,%u:%u,%u,%u,%u');",
        colorPassed ? "passed" : "failed",
        depthPassed ? "passed" : "failed",
        observedPixel[0],
        observedPixel[1],
        observedPixel[2],
        observedPixel[3],
        mismatchX,
        mismatchY,
        mismatchPixel[0],
        mismatchPixel[1],
        mismatchPixel[2],
        mismatchPixel[3],
        colorPassed && depthPassed ? "log" : "error",
        colorPassed ? "passed" : "failed",
        depthPassed ? "passed" : "failed",
        observedPixel[0],
        observedPixel[1],
        observedPixel[2],
        observedPixel[3],
        mismatchX,
        mismatchY,
        mismatchPixel[0],
        mismatchPixel[1],
        mismatchPixel[2],
        mismatchPixel[3]);
    emscripten_run_script(script);
    core::destroyCpuAllocatedUnique<AsyncReadbackState>(state);
}
#endif

} // namespace

namespace {

#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
class NativeRuntimeSmokeSlice final {
public:
    explicit NativeRuntimeSmokeSlice(const WebGpuNativeRuntimeSmokeDesc& desc) : desc_(desc) {}

    WebGpuNativeRuntimeReport run()
    {
        WebGpuNativeRuntimeReport report;
        if (!initializeDevice(report) || !createResources(report) || !createDescriptorGroup(report)
            || !createPipeline(report) || !configureAndAcquireCanvas(report)
            || !encodeAndSubmit(report) || !presentCanvas(report) || !recordResize(report)) {
            return report;
        }

        report.success = report.nativeDeviceAvailable && report.nativeBufferCreated
            && report.nativeReadbackBufferCreated && report.nativeTextureCreated
            && report.nativeTextureViewCreated && report.nativeDepthTextureCreated
            && report.nativeDepthTextureViewCreated && report.nativeSamplerCreated
            && report.nativeDescriptorGroupCreated && report.nativeShaderModuleCreated
            && report.nativePipelineCreated && report.nativeCommandEncoderCreated
            && report.nativeCommandBufferCreated && report.queueSubmitted
            && report.textureReadbackScheduled && report.depthReadbackScheduled
            && report.canvasAcquired && report.canvasPresentedByBrowser && report.resizeHandled
            && !report.perFramePipelineRebuild && !report.nativeHandleExposedToUpperLayer;
        return report;
    }

private:
    bool initializeDevice(WebGpuNativeRuntimeReport& report)
    {
        if (!browserWebGpuEnvironmentAvailable()) {
            report.diagnostics = "Emscripten WebGPU device bootstrap is not ready";
            return false;
        }
        device_ = emscripten_webgpu_get_device();
        if (!device_) {
            report.diagnostics = "Emscripten WebGPU device is unavailable";
            return false;
        }
        queue_ = wgpuDeviceGetQueue(device_);
        report.nativeDeviceAvailable = queue_ != nullptr;
        if (!report.nativeDeviceAvailable) {
            report.diagnostics = "Emscripten WebGPU queue is unavailable";
        }
        return report.nativeDeviceAvailable;
    }

    bool createResources(WebGpuNativeRuntimeReport& report)
    {
        auto bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.label = webGpuNativeStringView("RHI WebGPU smoke storage buffer");
        bufferDesc.size = 256;
        bufferDesc.usage =
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        storageBuffer_ = wgpuDeviceCreateBuffer(device_, &bufferDesc);
        report.nativeBufferCreated = storageBuffer_ != nullptr;

        auto readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readbackDesc.label = webGpuNativeStringView("RHI WebGPU smoke texture readback buffer");
        // 4 rows at WebGPU's 256-byte alignment for each color and depth
        // source. Keep both copies in one MapRead allocation.
        readbackDesc.size = 2048u;
        readbackDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        readbackBuffer_ = wgpuDeviceCreateBuffer(device_, &readbackDesc);
        report.nativeReadbackBufferCreated = readbackBuffer_ != nullptr;

        auto textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.label = webGpuNativeStringView("RHI WebGPU smoke texture");
        textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment
            | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        textureDesc.dimension = WGPUTextureDimension_2D;
        textureDesc.size = WGPUExtent3D{4u, 4u, 1u};
        textureDesc.format = nativeTextureFormat(desc_.colorFormat);
        textureDesc.mipLevelCount = 1;
        textureDesc.sampleCount = 1;
        texture_ = textureDesc.format == WGPUTextureFormat_Undefined
            ? nullptr
            : wgpuDeviceCreateTexture(device_, &textureDesc);
        report.nativeTextureCreated = texture_ != nullptr;

        if (texture_) {
            auto viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            viewDesc.label = webGpuNativeStringView("RHI WebGPU smoke texture view");
            viewDesc.format = textureDesc.format;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.aspect = WGPUTextureAspect_All;
            textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
        }
        report.nativeTextureViewCreated = textureView_ != nullptr;

        auto depthDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        depthDesc.label = webGpuNativeStringView("RHI WebGPU smoke depth texture");
        depthDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        depthDesc.dimension = WGPUTextureDimension_2D;
        depthDesc.size = WGPUExtent3D{4u, 4u, 1u};
        depthDesc.format = WGPUTextureFormat_Depth32Float;
        depthDesc.mipLevelCount = 1;
        depthDesc.sampleCount = 1;
        depthTexture_ = wgpuDeviceCreateTexture(device_, &depthDesc);
        report.nativeDepthTextureCreated = depthTexture_ != nullptr;
        if (depthTexture_) {
            auto depthViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            depthViewDesc.label = webGpuNativeStringView("RHI WebGPU smoke depth texture view");
            depthViewDesc.format = WGPUTextureFormat_Depth32Float;
            depthViewDesc.dimension = WGPUTextureViewDimension_2D;
            depthViewDesc.aspect = WGPUTextureAspect_DepthOnly;
            depthTextureView_ = wgpuTextureCreateView(depthTexture_, &depthViewDesc);
        }
        report.nativeDepthTextureViewCreated = depthTextureView_ != nullptr;

        auto samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
        samplerDesc.label = webGpuNativeStringView("RHI WebGPU smoke sampler");
        samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        samplerDesc.minFilter = WGPUFilterMode_Linear;
        samplerDesc.magFilter = WGPUFilterMode_Linear;
        sampler_ = wgpuDeviceCreateSampler(device_, &samplerDesc);
        report.nativeSamplerCreated = sampler_ != nullptr;

        if (!report.nativeBufferCreated || !report.nativeReadbackBufferCreated
            || !report.nativeTextureCreated || !report.nativeTextureViewCreated
            || !report.nativeDepthTextureCreated || !report.nativeDepthTextureViewCreated
            || !report.nativeSamplerCreated) {
            report.diagnostics = "RHI WebGPU native resource creation failed";
            return false;
        }
        return true;
    }

    bool createDescriptorGroup(WebGpuNativeRuntimeReport& report)
    {
        auto layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = WGPUBufferBindingType_Storage;
        layoutEntry.buffer.minBindingSize = 4;

        auto layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.label = webGpuNativeStringView("RHI WebGPU smoke descriptor layout");
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &layoutEntry;
        bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &layoutDesc);

        auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = storageBuffer_;
        entry.offset = 0;
        entry.size = 256;

        auto bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindGroupDesc.label = webGpuNativeStringView("RHI WebGPU smoke descriptor group");
        bindGroupDesc.layout = bindGroupLayout_;
        bindGroupDesc.entryCount = 1;
        bindGroupDesc.entries = &entry;
        bindGroup_ =
            bindGroupLayout_ ? wgpuDeviceCreateBindGroup(device_, &bindGroupDesc) : nullptr;
        report.nativeDescriptorGroupCreated = bindGroup_ != nullptr;
        if (!report.nativeDescriptorGroupCreated) {
            report.diagnostics = "RHI WebGPU descriptor group creation failed";
        }
        return report.nativeDescriptorGroupCreated;
    }

    bool createPipeline(WebGpuNativeRuntimeReport& report)
    {
        constexpr const char* shaderSource =
            "@group(0) @binding(0) var<storage, read_write> data : array<u32>;\n"
            "@compute @workgroup_size(1)\n"
            "fn main() { data[0u] = 7u; }\n";

        auto wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgsl.code = webGpuNativeStringView(shaderSource);
        auto shaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        shaderDesc.label = webGpuNativeStringView("RHI WebGPU smoke compute shader");
        shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
        shaderModule_ = wgpuDeviceCreateShaderModule(device_, &shaderDesc);
        report.nativeShaderModuleCreated = shaderModule_ != nullptr;
        if (!shaderModule_) {
            report.diagnostics = "RHI WebGPU shader module creation failed";
            return false;
        }

        auto layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.label = webGpuNativeStringView("RHI WebGPU smoke pipeline layout");
        layoutDesc.bindGroupLayoutCount = 1;
        layoutDesc.bindGroupLayouts = &bindGroupLayout_;
        pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &layoutDesc);
        if (!pipelineLayout_) {
            report.diagnostics = "RHI WebGPU pipeline layout creation failed";
            return false;
        }

        auto pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.label = webGpuNativeStringView("RHI WebGPU smoke compute pipeline");
        pipelineDesc.layout = pipelineLayout_;
        pipelineDesc.compute.module = shaderModule_;
        pipelineDesc.compute.entryPoint = webGpuNativeStringView("main");
        computePipeline_ = wgpuDeviceCreateComputePipeline(device_, &pipelineDesc);
        report.nativePipelineCreated = computePipeline_ != nullptr;
        report.pipelineCreateCount = computePipeline_ ? 1u : 0u;
        if (!report.nativePipelineCreated) {
            report.diagnostics = "RHI WebGPU compute pipeline creation failed";
        }
        return report.nativePipelineCreated;
    }

    bool configureAndAcquireCanvas(WebGpuNativeRuntimeReport& report)
    {
        if (!validExtent(desc_.extent) || desc_.canvasSelector.empty()) {
            report.diagnostics =
                "RHI WebGPU canvas smoke requires a canvas selector and valid extent";
            return false;
        }

        auto instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
        instance_ = wgpuCreateInstance(&instanceDesc);
        if (!instance_) {
            report.diagnostics = "RHI WebGPU instance creation failed";
            return false;
        }

        auto canvasSource = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
        canvasSource.selector = webGpuNativeStringView(desc_.canvasSelector);
        auto surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
        surfaceDesc.label = webGpuNativeStringView(desc_.canvasSelector);
        surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasSource);
        surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
        if (!surface_) {
            report.diagnostics = "RHI WebGPU canvas surface creation failed";
            return false;
        }

        auto config = WGPU_SURFACE_CONFIGURATION_INIT;
        config.device = device_;
        config.format = nativeTextureFormat(desc_.colorFormat);
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.width = desc_.extent.width;
        config.height = desc_.extent.height;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        config.presentMode = WGPUPresentMode_Fifo;
        if (config.format == WGPUTextureFormat_Undefined) {
            report.diagnostics = "RHI WebGPU canvas format is unsupported";
            return false;
        }
        wgpuSurfaceConfigure(surface_, &config);
        surfaceConfigured_ = true;

        auto surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);
        surfaceTexture_ = surfaceTexture.texture;
        const auto acquired = surfaceTexture_ != nullptr
            && (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
                || surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);
        report.canvasAcquired = acquired;
        if (!acquired) {
            report.diagnostics = "RHI WebGPU canvas acquire failed";
            return false;
        }

        auto viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.label = webGpuNativeStringView("RHI WebGPU smoke canvas view");
        viewDesc.format = config.format;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.aspect = WGPUTextureAspect_All;
        surfaceView_ = wgpuTextureCreateView(surfaceTexture_, &viewDesc);
        if (!surfaceView_) {
            report.diagnostics = "RHI WebGPU canvas texture view creation failed";
            return false;
        }
        return true;
    }

    bool encodeAndSubmit(WebGpuNativeRuntimeReport& report)
    {
        auto encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        encoderDesc.label = webGpuNativeStringView("RHI WebGPU smoke command encoder");
        commandEncoder_ = wgpuDeviceCreateCommandEncoder(device_, &encoderDesc);
        report.nativeCommandEncoderCreated = commandEncoder_ != nullptr;
        if (!commandEncoder_) {
            report.diagnostics = "RHI WebGPU command encoder creation failed";
            return false;
        }

        auto computeDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        computeDesc.label = webGpuNativeStringView("RHI WebGPU smoke compute pass");
        computePass_ = wgpuCommandEncoderBeginComputePass(commandEncoder_, &computeDesc);
        if (!computePass_) {
            report.diagnostics = "RHI WebGPU compute pass creation failed";
            return false;
        }
        wgpuComputePassEncoderSetPipeline(computePass_, computePipeline_);
        wgpuComputePassEncoderSetBindGroup(computePass_, 0, bindGroup_, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(computePass_, 1, 1, 1);
        wgpuComputePassEncoderEnd(computePass_);
        releaseComputePassEncoder(computePass_);

        auto textureAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        textureAttachment.view = textureView_;
        textureAttachment.loadOp = WGPULoadOp_Clear;
        textureAttachment.storeOp = WGPUStoreOp_Store;
        textureAttachment.clearValue = WGPUColor{0.5, 0.25, 0.125, 1.0};
        auto textureRenderDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        textureRenderDesc.label = webGpuNativeStringView("RHI WebGPU smoke texture clear pass");
        textureRenderDesc.colorAttachmentCount = 1;
        textureRenderDesc.colorAttachments = &textureAttachment;
        auto textureRenderPass =
            wgpuCommandEncoderBeginRenderPass(commandEncoder_, &textureRenderDesc);
        if (!textureRenderPass) {
            report.diagnostics = "RHI WebGPU texture readback render pass creation failed";
            return false;
        }
        wgpuRenderPassEncoderEnd(textureRenderPass);
        releaseRenderPassEncoder(textureRenderPass);

        auto depthAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        rhi::RenderPassDepthStencilAttachmentDesc depthIntent;
        depthIntent.format = rhi::TextureFormat::D32Float;
        depthIntent.depthLoadOp = rhi::LoadOp::Clear;
        depthIntent.depthStoreOp = rhi::StoreOp::Store;
        depthIntent.depthClearValue = 0.25f;
        if (const auto* id = WebGpuAttachmentOperationLowering::depthStencil(
                depthIntent, depthIntent.format, depthTextureView_, depthAttachment)) {
            report.diagnostics = id;
            return false;
        }
        auto depthRenderDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        depthRenderDesc.label = webGpuNativeStringView("RHI WebGPU smoke depth clear pass");
        depthRenderDesc.depthStencilAttachment = &depthAttachment;
        auto depthRenderPass = wgpuCommandEncoderBeginRenderPass(commandEncoder_, &depthRenderDesc);
        if (!depthRenderPass) {
            report.diagnostics = "RHI WebGPU depth readback render pass creation failed";
            return false;
        }
        wgpuRenderPassEncoderEnd(depthRenderPass);
        releaseRenderPassEncoder(depthRenderPass);

        auto colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = surfaceView_;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{0.08, 0.10, 0.14, 1.0};

        auto renderDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        renderDesc.label = webGpuNativeStringView("RHI WebGPU smoke canvas clear pass");
        renderDesc.colorAttachmentCount = 1;
        renderDesc.colorAttachments = &colorAttachment;
        renderPass_ = wgpuCommandEncoderBeginRenderPass(commandEncoder_, &renderDesc);
        if (!renderPass_) {
            report.diagnostics = "RHI WebGPU render pass creation failed";
            return false;
        }
        wgpuRenderPassEncoderEnd(renderPass_);
        releaseRenderPassEncoder(renderPass_);

        auto sourceInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        sourceInfo.texture = texture_;
        sourceInfo.mipLevel = 0u;
        sourceInfo.origin = WGPUOrigin3D{0u, 0u, 0u};
        sourceInfo.aspect = WGPUTextureAspect_All;
        auto destinationInfo = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destinationInfo.buffer = readbackBuffer_;
        destinationInfo.layout.offset = 0u;
        destinationInfo.layout.bytesPerRow = 256u;
        destinationInfo.layout.rowsPerImage = 4u;
        const auto copyExtent = WGPUExtent3D{4u, 4u, 1u};
        wgpuCommandEncoderCopyTextureToBuffer(
            commandEncoder_, &sourceInfo, &destinationInfo, &copyExtent);

        sourceInfo.texture = depthTexture_;
        sourceInfo.aspect = WGPUTextureAspect_DepthOnly;
        destinationInfo.layout.offset = 1024u;
        wgpuCommandEncoderCopyTextureToBuffer(
            commandEncoder_, &sourceInfo, &destinationInfo, &copyExtent);

        auto commandBufferDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        commandBufferDesc.label = webGpuNativeStringView("RHI WebGPU smoke command buffer");
        commandBuffer_ = wgpuCommandEncoderFinish(commandEncoder_, &commandBufferDesc);
        report.nativeCommandBufferCreated = commandBuffer_ != nullptr;
        if (!commandBuffer_) {
            report.diagnostics = "RHI WebGPU command buffer creation failed";
            return false;
        }
        wgpuQueueSubmit(queue_, 1, &commandBuffer_);
        report.queueSubmitted = true;
        report.queueSubmitCount = 1;

        auto readbackState = core::makeUnique<AsyncReadbackState>();
        readbackState->buffer = std::exchange(readbackBuffer_, nullptr);
        auto callbackInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        callbackInfo.callback = &publishAsyncReadbacks;
        const auto readback = readbackState->buffer;
        // The callback owns the buffer before registration; it may run inside
        // MapAsync. The runtime slice must never destroy that same object.
        callbackInfo.userdata1 = readbackState.release();
        (void)wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0u, 2048u, callbackInfo);
        report.textureReadbackScheduled = true;
        report.depthReadbackScheduled = true;
        return true;
    }

    bool presentCanvas(WebGpuNativeRuntimeReport& report)
    {
        report.canvasPresentedByBrowser = surface_ != nullptr && surfaceConfigured_
            && surfaceTexture_ != nullptr && report.queueSubmitted;
        return report.canvasPresentedByBrowser;
    }

    bool recordResize(WebGpuNativeRuntimeReport& report)
    {
        if (!desc_.exerciseResize) {
            report.resizeHandled = true;
            return true;
        }
        auto config = WGPU_SURFACE_CONFIGURATION_INIT;
        config.device = device_;
        config.format = nativeTextureFormat(desc_.colorFormat);
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.width = desc_.extent.width + 1u;
        config.height = desc_.extent.height + 1u;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface_, &config);
        report.resizeHandled = true;
        return true;
    }

    const WebGpuNativeRuntimeSmokeDesc& desc_;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUBuffer storageBuffer_ = nullptr;
    WGPUBuffer readbackBuffer_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthTextureView_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUComputePipeline computePipeline_ = nullptr;
    WGPUInstance instance_ = nullptr;
    WGPUSurface surface_ = nullptr;
    bool surfaceConfigured_ = false;
    WGPUTexture surfaceTexture_ = nullptr;
    WGPUTextureView surfaceView_ = nullptr;
    WGPUCommandEncoder commandEncoder_ = nullptr;
    WGPUComputePassEncoder computePass_ = nullptr;
    WGPURenderPassEncoder renderPass_ = nullptr;
    WGPUCommandBuffer commandBuffer_ = nullptr;

public:
    ~NativeRuntimeSmokeSlice()
    {
        releaseCommandBuffer(commandBuffer_);
        releaseCommandEncoder(commandEncoder_);
        releaseTextureView(surfaceView_);
        releaseTexture(surfaceTexture_);
        if (surface_ && surfaceConfigured_) {
            wgpuSurfaceUnconfigure(surface_);
        }
        releaseSurface(surface_);
        releaseInstance(instance_);
        releaseComputePipeline(computePipeline_);
        releasePipelineLayout(pipelineLayout_);
        releaseShaderModule(shaderModule_);
        releaseBindGroup(bindGroup_);
        releaseBindGroupLayout(bindGroupLayout_);
        releaseSampler(sampler_);
        releaseTextureView(depthTextureView_);
        releaseTexture(depthTexture_);
        releaseTextureView(textureView_);
        releaseTexture(texture_);
        releaseBuffer(storageBuffer_);
        releaseBuffer(readbackBuffer_);
        releaseQueue(queue_);
        releaseDevice(device_);
    }
};
#endif

} // namespace

rhi::semantics::WebGpuTimestampCommandPointFacts webGpuNativeTimestampCommandPointFacts() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable())
        return {};
    // NativeTimestampApiFacts/1: bit order matches the nine fields of the
    // value-only facts struct. This reads exact device/prototype properties;
    // it neither creates an encoder nor records a substitute timestamp pass.
    const auto mask = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var d = Module.preinitializedWebGPUDevice;
        var e = typeof GPUCommandEncoder !== 'undefined' ? GPUCommandEncoder.prototype : null;
        var r = typeof GPURenderPassEncoder !== 'undefined' ? GPURenderPassEncoder.prototype : null;
        var c = typeof GPUComputePassEncoder !== 'undefined' ? GPUComputePassEncoder.prototype : null;
        var b = typeof GPUBuffer !== 'undefined' ? GPUBuffer.prototype : null;
        return (d && d.features && d.features.has('timestamp-query') ? 1 << 0 : 0) |
               (e && typeof e.writeTimestamp === 'function' ? 1 << 1 : 0) |
               (r && typeof r.writeTimestamp === 'function' ? 1 << 2 : 0) |
               (c && typeof c.writeTimestamp === 'function' ? 1 << 3 : 0) |
               (e && typeof e.resolveQuerySet === 'function' ? 1 << 4 : 0) |
               (e && typeof e.copyBufferToBuffer === 'function' ? 1 << 5 : 0) |
               (b && typeof b.mapAsync === 'function' ? 1 << 6 : 0) |
               (b && typeof b.getMappedRange === 'function' ? 1 << 7 : 0) |
               (b && typeof b.unmap === 'function' ? 1 << 8 : 0);
        // clang-format on
    }));
    return {bool(mask & (1u << 0u)),
        bool(mask & (1u << 1u)),
        bool(mask & (1u << 2u)),
        bool(mask & (1u << 3u)),
        bool(mask & (1u << 4u)),
        bool(mask & (1u << 5u)),
        bool(mask & (1u << 6u)),
        bool(mask & (1u << 7u)),
        bool(mask & (1u << 8u))};
#else
    return {};
#endif
}

bool webGpuNativeTimestampQuerySupported() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return false;
    }
    if (!browserWebGpuTimestampQueryApiAvailable()) {
        return false;
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device || !wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery)) {
        if (device)
            wgpuDeviceRelease(device);
        return false;
    }
    auto desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    desc.label = webGpuNativeStringView("RHI WebGPU timestamp-query capability probe");
    desc.type = WGPUQueryType_Timestamp;
    desc.count = 2u;
    auto querySet = wgpuDeviceCreateQuerySet(device, &desc);
    if (!querySet) {
        wgpuDeviceRelease(device);
        return false;
    }

    auto resolveDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    resolveDesc.label =
        webGpuNativeStringView("RHI WebGPU timestamp capability probe resolve buffer");
    resolveDesc.size = sizeof(std::uint64_t) * 2u;
    resolveDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
    auto resolveBuffer = wgpuDeviceCreateBuffer(device, &resolveDesc);

    auto readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    readbackDesc.label =
        webGpuNativeStringView("RHI WebGPU timestamp capability probe readback buffer");
    readbackDesc.size = sizeof(std::uint64_t) * 2u;
    readbackDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    auto readbackBuffer = wgpuDeviceCreateBuffer(device, &readbackDesc);

    const bool payloadSupported = resolveBuffer != nullptr && readbackBuffer != nullptr;
    if (resolveBuffer) {
        wgpuBufferDestroy(resolveBuffer);
        wgpuBufferRelease(resolveBuffer);
    }
    if (readbackBuffer) {
        wgpuBufferDestroy(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
    }
    wgpuQuerySetDestroy(querySet);
    wgpuQuerySetRelease(querySet);
    wgpuDeviceRelease(device);
    return payloadSupported;
#else
    return false;
#endif
}

std::uint64_t webGpuNativeAllocateTimingFrameSerial() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    static std::atomic<std::uint64_t> nextSerial{1u};
    return nextSerial.fetch_add(1u, std::memory_order_relaxed);
#else
    return 0u;
#endif
}

void mapWebGpuNativeTimingReadbackBuffers(
    void* resolveBuffer, void* readbackBuffer, std::uint64_t frameSerial) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    auto* resolve = reinterpret_cast<WGPUBuffer>(resolveBuffer);
    auto* readback = reinterpret_cast<WGPUBuffer>(readbackBuffer);
    if (!resolve || !readback || frameSerial == 0u) {
        if (resolve) {
            wgpuBufferDestroy(resolve);
            wgpuBufferRelease(resolve);
        }
        if (readback) {
            wgpuBufferDestroy(readback);
            wgpuBufferRelease(readback);
        }
        completedGpuTiming().readbackFailed.store(true, std::memory_order_release);
        return;
    }
    auto state = core::makeUnique<GpuTimingReadbackState>();
    state->resolveBuffer = resolve;
    state->readbackBuffer = readback;
    state->frameSerial = frameSerial;
    auto callbackInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = &publishGpuTimingReadback;
    callbackInfo.userdata1 = state.release();
    // Valid explicit AllowSpontaneous transfers event ownership before return;
    // only publishGpuTimingReadback may release it, even on synchronous Error.
    (void)wgpuBufferMapAsync(
        readback, WGPUMapMode_Read, 0u, sizeof(std::uint64_t) * 2u, callbackInfo);
#else
    (void)resolveBuffer;
    (void)readbackBuffer;
    (void)frameSerial;
#endif
}

rhi::GpuTimingSample webGpuNativeLatestGpuTimingSample() noexcept
{
    rhi::GpuTimingSample sample;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    const auto serial = completedGpuTiming().frameSerial.load(std::memory_order_acquire);
    if (serial == 0u) {
        sample.availability = rhi::GpuTimingAvailability::Pending;
        sample.diagnostic = completedGpuTiming().readbackFailed.load(std::memory_order_acquire)
            ? "WebGPU timestamp-query readback failed"
            : "WebGPU timestamp-query result is pending asynchronous map";
        return sample;
    }
    sample.availability = rhi::GpuTimingAvailability::Available;
    sample.completedFrameSerial = serial;
    sample.scopeCount = 1u;
    sample.scopes[0].scopeId = 0u;
    sample.scopes[0].elapsedMs = static_cast<float>(
        static_cast<double>(completedGpuTiming().elapsedNanoseconds.load(std::memory_order_relaxed))
        / 1000000.0);
    sample.diagnostic = "WebGPU timestamp query";
#else
    sample.availability = rhi::GpuTimingAvailability::Unsupported;
    sample.diagnostic = "WebGPU native timestamp-query payload is unavailable";
#endif
    return sample;
}

bool webGpuNativeDeviceAvailable()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return false;
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device) {
        return false;
    }
    wgpuDeviceRelease(device);
    return true;
#else
    return false;
#endif
}

bool webGpuNativeDeviceLost() noexcept
{
#if defined(__EMSCRIPTEN__)
    return EM_ASM_INT({
        // clang-format off
        return typeof Module !== 'undefined' && Module.webGpuDeviceLostInfo ? 1 : 0;
        // clang-format on
    }) != 0;
#else
    return false;
#endif
}

rhi::PrototypeCommandBatchCapabilities webGpuNativePrototypeBatchCapabilities() noexcept
{
    rhi::PrototypeCommandBatchCapabilities caps;
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost())
        return caps;
    auto device = emscripten_webgpu_get_device();
    const bool receipts = device
        && EM_ASM_INT(
            {
                // clang-format off
        const d = WebGPU.getJsObject($0);
        return d && d.queue && typeof d.createCommandEncoder === 'function'
            && typeof d.queue.submit === 'function'
            && typeof d.queue.onSubmittedWorkDone === 'function'
            && typeof d.pushErrorScope === 'function' && typeof d.popErrorScope === 'function'
            && d.lost && typeof d.lost.then === 'function'
            && typeof queueMicrotask === 'function' && typeof Asyncify !== 'undefined';
                // clang-format on
            },
            device);
    if (device)
        wgpuDeviceRelease(device);
    if (receipts) {
        caps.contractVersion = rhi::PrototypeCommandBatchContract::version;
        caps.mode = rhi::PrototypeCommandBatchMode::Prototype;
        // Versioned implementation guarantees, gated by the actual API facts.
        caps.boundedRecording = true;
        caps.nativeCompletion = true;
        caps.passBoundaryOrdering = true;
        caps.acquiredImageLeaseValidation = true;
    }
#endif
    return caps;
}

std::uint32_t webGpuNativeAcquireExecutionTurn() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        if (typeof queueMicrotask !== 'function') return 0;
        const state = Module.arpRhiExecutionTurn
            || (Module.arpRhiExecutionTurn = {epoch: 0, active: false});
        if (!state.active) {
            if (state.epoch === 0xffffffff) return 0;
            ++state.epoch;
            state.active = true;
            queueMicrotask(() => { state.active = false; });
        }
        return state.epoch;
        // clang-format on
    }));
#else
    return 0;
#endif
}

bool webGpuNativeExecutionTurnMatches(std::uint32_t epoch) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return epoch
        && EM_ASM_INT(
            {
                // clang-format off
        const state = Module.arpRhiExecutionTurn;
        return !!state && state.active && state.epoch === ($0 >>> 0);
                // clang-format on
            },
            epoch);
#else
    (void)epoch;
    return false;
#endif
}

bool webGpuNativeDeviceSupportsStorageTexture(rhi::TextureFormat format)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return false;
    }
#if defined(__EMSCRIPTEN__)
    if (format == rhi::TextureFormat::Rg16Float || format == rhi::TextureFormat::Rgba16Float) {
        // Optional float storage formats are probed asynchronously by the
        // browser bootstrap inside an error scope. Reading these results
        // avoids issuing an invalid synchronous createTexture call that would
        // escape as an uncaptured validation error. The values are
        // browser-owned and capability-only; no native device handle crosses
        // the RHI boundary.
        return EM_ASM_INT(
                   {
                       // clang-format off
                       if (typeof Module === 'undefined')
                           return 0;
                       return $0 === 0 ? (Module.webGpuRg16FloatStorage ? 1 : 0)
                                       : (Module.webGpuRgba16FloatStorage ? 1 : 0);
                       // clang-format on
                   },
                   format == rhi::TextureFormat::Rg16Float ? 0 : 1)
            != 0;
    }
#endif
    auto* device = emscripten_webgpu_get_device();
    const auto nativeFormat = nativeTextureFormat(format);
    if (!device || nativeFormat == WGPUTextureFormat_Undefined) {
        if (device) {
            wgpuDeviceRelease(device);
        }
        return false;
    }

    auto desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = webGpuNativeStringView("RHI WebGPU storage-format capability probe");
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = WGPUExtent3D{1u, 1u, 1u};
    desc.format = nativeFormat;
    desc.mipLevelCount = 1u;
    desc.sampleCount = 1u;
    desc.usage = WGPUTextureUsage_StorageBinding;
    auto texture = wgpuDeviceCreateTexture(device, &desc);
    if (!texture) {
        wgpuDeviceRelease(device);
        return false;
    }
    wgpuTextureRelease(texture);
    wgpuDeviceRelease(device);
    return true;
#else
    (void)format;
    return false;
#endif
}

bool webGpuNativeDeviceSupportsIndirectFirstInstance() noexcept
{
#if defined(__EMSCRIPTEN__)
    return EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        return !!(device && device.features && typeof device.features.has === 'function' &&
                  device.features.has('indirect-first-instance'));
        // clang-format on
    }) != 0;
#else
    return false;
#endif
}

WebGpuNativeDeviceLimits webGpuNativeDeviceLimits() noexcept
{
    WebGpuNativeDeviceLimits limits;
#if defined(__EMSCRIPTEN__)
    // BindlessCross/1: the created device is the sole execution authority.
    // Adapter limits/bootstrap dictionaries may only plan requestDevice.
    // Missing, malformed or unrepresentable values remain unavailable.
    limits.maxBufferSize = static_cast<std::uint64_t>(EM_ASM_DOUBLE({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxBufferSize;
        return Number.isSafeInteger(value) && value > 0 ? value : 0;
        // clang-format on
    }));
    limits.maxTexture3DSize = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxTextureDimension3D;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxTextureArrayLayers = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxTextureArrayLayers;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxTexture2DSize = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxTextureDimension2D;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxColorAttachments = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxColorAttachments;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxVertexBuffers = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxVertexBuffers;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxVertexAttributes = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxVertexAttributes;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxVertexBufferArrayStride = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxVertexBufferArrayStride;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxDescriptorBindings = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxBindingsPerBindGroup;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxSampledTexturesPerShaderStage = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxSampledTexturesPerShaderStage;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxSamplersPerShaderStage = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxSamplersPerShaderStage;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxUniformBuffersPerShaderStage = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxUniformBuffersPerShaderStage;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxStorageTexturesPerShaderStage = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxStorageTexturesPerShaderStage;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxBindGroups = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxBindGroups;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.uniformBufferOffsetAlignment = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.minUniformBufferOffsetAlignment;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.storageBufferOffsetAlignment = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.minStorageBufferOffsetAlignment;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxUniformBufferBindingSize = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxUniformBufferBindingSize;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxStorageBufferBindingSize = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxStorageBufferBindingSize;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxComputeInvocationsPerWorkgroup = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxComputeInvocationsPerWorkgroup;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxComputeWorkgroupSizeX = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxComputeWorkgroupSizeX;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxComputeWorkgroupSizeY = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxComputeWorkgroupSizeY;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxComputeWorkgroupStorageSize = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxComputeWorkgroupStorageSize;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxComputeWorkgroupSizeZ = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxComputeWorkgroupSizeZ;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxComputeWorkgroupsPerDimension = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxComputeWorkgroupsPerDimension;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
    limits.maxStorageBuffersPerShaderStage = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        var device = typeof Module === 'undefined' ? null : Module.preinitializedWebGPUDevice;
        var value = device && device.limits && device.limits.maxStorageBuffersPerShaderStage;
        return Number.isInteger(value) && value > 0 && value <= 4294967295 ? value : 0;
        // clang-format on
    }));
#endif
    return limits;
}

bool webGpuNativeBindGroupCountSupported(const std::uint32_t requiredCount) noexcept
{
    if (requiredCount == 0u || requiredCount <= 4u) {
        return true;
    }
    const auto limits = webGpuNativeDeviceLimits();
    return limits.maxBindGroups >= requiredCount;
}

bool webGpuNativeDeviceSupportsStorageBuffer() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return false;
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device) {
        return false;
    }

    auto bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.label = webGpuNativeStringView("RHI WebGPU storage-buffer capability probe");
    bufferDesc.size = 4u;
    bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    auto buffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    layoutEntry.binding = 0u;
    layoutEntry.visibility = WGPUShaderStage_Compute;
    layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    layoutEntry.buffer.type = WGPUBufferBindingType_Storage;
    layoutEntry.buffer.minBindingSize = 4u;

    auto layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = webGpuNativeStringView("RHI WebGPU storage-buffer binding capability probe");
    layoutDesc.entryCount = 1u;
    layoutDesc.entries = &layoutEntry;
    auto layout = buffer ? wgpuDeviceCreateBindGroupLayout(device, &layoutDesc) : nullptr;

    WGPUBindGroupEntry bindEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bindEntry.binding = 0u;
    bindEntry.buffer = buffer;
    bindEntry.offset = 0u;
    bindEntry.size = 4u;
    auto bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.label =
        webGpuNativeStringView("RHI WebGPU storage-buffer binding capability probe");
    bindGroupDesc.layout = layout;
    bindGroupDesc.entryCount = 1u;
    bindGroupDesc.entries = &bindEntry;
    auto bindGroup = layout ? wgpuDeviceCreateBindGroup(device, &bindGroupDesc) : nullptr;

    WGPUShaderModule shaderModule = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPUComputePipeline computePipeline = nullptr;
    if (bindGroup) {
        constexpr const char* probeShader =
            "@group(0) @binding(0) var<storage, read_write> data : array<u32>;\n"
            "@compute @workgroup_size(1)\n"
            "fn main() { data[0u] = 1u; }\n";
        auto wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgsl.code = webGpuNativeStringView(probeShader);
        auto shaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        shaderDesc.label =
            webGpuNativeStringView("RHI WebGPU storage-buffer shader capability probe");
        shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
        shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

        auto pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pipelineLayoutDesc.label =
            webGpuNativeStringView("RHI WebGPU storage-buffer pipeline-layout capability probe");
        pipelineLayoutDesc.bindGroupLayoutCount = 1u;
        pipelineLayoutDesc.bindGroupLayouts = &layout;
        pipelineLayout =
            shaderModule ? wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc) : nullptr;

        auto pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.label =
            webGpuNativeStringView("RHI WebGPU storage-buffer pipeline capability probe");
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = shaderModule;
        pipelineDesc.compute.entryPoint = webGpuNativeStringView("main");
        computePipeline =
            pipelineLayout ? wgpuDeviceCreateComputePipeline(device, &pipelineDesc) : nullptr;
    }

    const bool supported = buffer != nullptr && layout != nullptr && bindGroup != nullptr
        && shaderModule != nullptr && pipelineLayout != nullptr && computePipeline != nullptr;
    if (computePipeline) {
        wgpuComputePipelineRelease(computePipeline);
    }
    if (pipelineLayout) {
        wgpuPipelineLayoutRelease(pipelineLayout);
    }
    if (shaderModule) {
        wgpuShaderModuleRelease(shaderModule);
    }
    if (bindGroup) {
        wgpuBindGroupRelease(bindGroup);
    }
    if (layout) {
        wgpuBindGroupLayoutRelease(layout);
    }
    if (buffer) {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
    }
    wgpuDeviceRelease(device);
    return supported;
#else
    return false;
#endif
}

bool webGpuNativeDeviceSupportsSceneGeometryRayQuery() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return false;
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device) {
        return false;
    }

    auto nodeBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    nodeBufferDesc.label = webGpuNativeStringView("RHI WebGPU DDGI BVH node capability probe");
    nodeBufferDesc.size = 32u;
    nodeBufferDesc.usage = WGPUBufferUsage_Storage;
    auto nodeBuffer = wgpuDeviceCreateBuffer(device, &nodeBufferDesc);

    auto leafBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    leafBufferDesc.label = webGpuNativeStringView("RHI WebGPU DDGI BVH leaf capability probe");
    leafBufferDesc.size = 96u;
    leafBufferDesc.usage = WGPUBufferUsage_Storage;
    auto leafBuffer = wgpuDeviceCreateBuffer(device, &leafBufferDesc);

    WGPUBindGroupLayoutEntry layoutEntries[2] = {
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    };
    layoutEntries[0].binding = 0u;
    layoutEntries[0].visibility = WGPUShaderStage_Compute;
    layoutEntries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    layoutEntries[0].buffer.minBindingSize = 32u;
    layoutEntries[1].binding = 1u;
    layoutEntries[1].visibility = WGPUShaderStage_Compute;
    layoutEntries[1].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    layoutEntries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    layoutEntries[1].buffer.minBindingSize = 96u;

    auto layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = webGpuNativeStringView("RHI WebGPU DDGI BVH binding capability probe");
    layoutDesc.entryCount = 2u;
    layoutDesc.entries = layoutEntries;
    auto layout =
        nodeBuffer && leafBuffer ? wgpuDeviceCreateBindGroupLayout(device, &layoutDesc) : nullptr;

    WGPUBindGroupEntry bindEntries[2] = {
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    };
    bindEntries[0].binding = 0u;
    bindEntries[0].buffer = nodeBuffer;
    bindEntries[0].offset = 0u;
    bindEntries[0].size = 32u;
    bindEntries[1].binding = 1u;
    bindEntries[1].buffer = leafBuffer;
    bindEntries[1].offset = 0u;
    bindEntries[1].size = 96u;
    auto bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.label = webGpuNativeStringView("RHI WebGPU DDGI BVH bind-group capability probe");
    bindGroupDesc.layout = layout;
    bindGroupDesc.entryCount = 2u;
    bindGroupDesc.entries = bindEntries;
    auto bindGroup = layout ? wgpuDeviceCreateBindGroup(device, &bindGroupDesc) : nullptr;

    WGPUShaderModule shaderModule = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPUComputePipeline computePipeline = nullptr;
    if (bindGroup) {
        constexpr const char* probeShader =
            "struct BvhNode { minPoint : vec3<f32>, leftChild : u32, "
            "maxPoint : vec3<f32>, rightChild : u32 };\n"
            "struct BvhLeaf { v0 : vec3<f32>, pad0 : u32, "
            "v1 : vec3<f32>, pad1 : u32, v2 : vec3<f32>, "
            "pad2 : u32, normal : vec3<f32>, materialIdx : u32, "
            "uv0 : vec2<f32>, uv1 : vec2<f32>, uv2 : vec2<f32>, "
            "uvPad : vec2<f32> };\n"
            "@group(0) @binding(0) var<storage, read> nodes : array<BvhNode>;\n"
            "@group(0) @binding(1) var<storage, read> leaves : array<BvhLeaf>;\n"
            "@compute @workgroup_size(1)\n"
            "fn main() {\n"
            "  let nodeValue = nodes[0u].leftChild;\n"
            "  let leafValue = leaves[0u].materialIdx;\n"
            "  if ((nodeValue ^ leafValue) == 0xffffffffu) { return; }\n"
            "}\n";
        auto wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgsl.code = webGpuNativeStringView(probeShader);
        auto shaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        shaderDesc.label = webGpuNativeStringView("RHI WebGPU DDGI BVH shader capability probe");
        shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
        shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

        auto pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pipelineLayoutDesc.label =
            webGpuNativeStringView("RHI WebGPU DDGI BVH pipeline capability probe");
        pipelineLayoutDesc.bindGroupLayoutCount = 1u;
        pipelineLayoutDesc.bindGroupLayouts = &layout;
        pipelineLayout =
            shaderModule ? wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc) : nullptr;

        auto pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.label = webGpuNativeStringView("RHI WebGPU DDGI BVH compute capability probe");
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = shaderModule;
        pipelineDesc.compute.entryPoint = webGpuNativeStringView("main");
        computePipeline =
            pipelineLayout ? wgpuDeviceCreateComputePipeline(device, &pipelineDesc) : nullptr;
    }

    const bool supported = nodeBuffer != nullptr && leafBuffer != nullptr && layout != nullptr
        && bindGroup != nullptr && shaderModule != nullptr && pipelineLayout != nullptr
        && computePipeline != nullptr;
    if (computePipeline)
        wgpuComputePipelineRelease(computePipeline);
    if (pipelineLayout)
        wgpuPipelineLayoutRelease(pipelineLayout);
    if (shaderModule)
        wgpuShaderModuleRelease(shaderModule);
    if (bindGroup)
        wgpuBindGroupRelease(bindGroup);
    if (layout)
        wgpuBindGroupLayoutRelease(layout);
    if (leafBuffer) {
        wgpuBufferDestroy(leafBuffer);
        wgpuBufferRelease(leafBuffer);
    }
    if (nodeBuffer) {
        wgpuBufferDestroy(nodeBuffer);
        wgpuBufferRelease(nodeBuffer);
    }
    wgpuDeviceRelease(device);
    return supported;
#else
    return false;
#endif
}

bool webGpuNativeDeviceSupportsTextureCompressionBc() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE) && defined(__EMSCRIPTEN__)
    if (!browserWebGpuEnvironmentAvailable()) {
        return false;
    }
    return EM_ASM_INT({
        // clang-format off
        return typeof Module !== 'undefined' && Module.webGpuTextureCompressionBc ? 1 : 0;
        // clang-format on
    }) != 0;
#else
    return false;
#endif
}

bool webGpuNativeDeviceSupportsComputeDispatch() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return false;
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device) {
        return false;
    }

    constexpr const char* probeShader = "@compute @workgroup_size(1)\n"
                                        "fn main() { }\n";
    auto wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = webGpuNativeStringView(probeShader);
    auto shaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shaderDesc.label = webGpuNativeStringView("RHI WebGPU compute capability probe");
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
    auto shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // emdawn requires an explicit pipeline layout for createComputePipeline;
    // the empty layout is the correct contract for this bindless-free probe.
    auto pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.label =
        webGpuNativeStringView("RHI WebGPU compute pipeline-layout capability probe");
    pipelineLayoutDesc.bindGroupLayoutCount = 0u;
    pipelineLayoutDesc.bindGroupLayouts = nullptr;
    auto pipelineLayout =
        shaderModule ? wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc) : nullptr;
    auto pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.label = webGpuNativeStringView("RHI WebGPU compute pipeline capability probe");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = shaderModule;
    pipelineDesc.compute.entryPoint = webGpuNativeStringView("main");
    auto pipeline =
        pipelineLayout ? wgpuDeviceCreateComputePipeline(device, &pipelineDesc) : nullptr;
    const bool supported =
        shaderModule != nullptr && pipelineLayout != nullptr && pipeline != nullptr;
    if (pipeline)
        wgpuComputePipelineRelease(pipeline);
    if (pipelineLayout)
        wgpuPipelineLayoutRelease(pipelineLayout);
    if (shaderModule)
        wgpuShaderModuleRelease(shaderModule);
    wgpuDeviceRelease(device);
    return supported;
#else
    return false;
#endif
}

bool webGpuNativeDeviceSupportsStorageTextureBinding() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return false;
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device) {
        return false;
    }

    auto textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.label = webGpuNativeStringView("RHI WebGPU storage-texture capability probe");
    textureDesc.dimension = WGPUTextureDimension_2D;
    textureDesc.size = WGPUExtent3D{1u, 1u, 1u};
    textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
    textureDesc.mipLevelCount = 1u;
    textureDesc.sampleCount = 1u;
    textureDesc.usage = WGPUTextureUsage_StorageBinding;
    auto texture = wgpuDeviceCreateTexture(device, &textureDesc);
    auto view = texture ? wgpuTextureCreateView(texture, nullptr) : nullptr;

    auto layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    layoutEntry.binding = 0u;
    layoutEntry.visibility = WGPUShaderStage_Compute;
    layoutEntry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
    layoutEntry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    layoutEntry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
    layoutEntry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
    auto layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = webGpuNativeStringView("RHI WebGPU storage-texture binding layout probe");
    layoutDesc.entryCount = 1u;
    layoutDesc.entries = &layoutEntry;
    auto layout = view ? wgpuDeviceCreateBindGroupLayout(device, &layoutDesc) : nullptr;

    auto bindEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bindEntry.binding = 0u;
    bindEntry.textureView = view;
    auto bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.label = webGpuNativeStringView("RHI WebGPU storage-texture binding group probe");
    bindGroupDesc.layout = layout;
    bindGroupDesc.entryCount = 1u;
    bindGroupDesc.entries = &bindEntry;
    auto bindGroup = layout ? wgpuDeviceCreateBindGroup(device, &bindGroupDesc) : nullptr;

    constexpr const char* probeShader =
        "@group(0) @binding(0) var outputTexture : texture_storage_2d<rgba8unorm, write>;\n"
        "@compute @workgroup_size(1)\n"
        "fn main() { textureStore(outputTexture, vec2<i32>(0, 0), vec4<f32>(0.0)); }\n";
    auto wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = webGpuNativeStringView(probeShader);
    auto shaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shaderDesc.label = webGpuNativeStringView("RHI WebGPU storage-texture shader probe");
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
    auto shaderModule = bindGroup ? wgpuDeviceCreateShaderModule(device, &shaderDesc) : nullptr;

    auto pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.label =
        webGpuNativeStringView("RHI WebGPU storage-texture pipeline layout probe");
    pipelineLayoutDesc.bindGroupLayoutCount = 1u;
    pipelineLayoutDesc.bindGroupLayouts = &layout;
    auto pipelineLayout =
        shaderModule ? wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc) : nullptr;
    auto pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.label = webGpuNativeStringView("RHI WebGPU storage-texture pipeline probe");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = shaderModule;
    pipelineDesc.compute.entryPoint = webGpuNativeStringView("main");
    auto pipeline =
        pipelineLayout ? wgpuDeviceCreateComputePipeline(device, &pipelineDesc) : nullptr;

    const bool supported = texture != nullptr && view != nullptr && layout != nullptr
        && bindGroup != nullptr && shaderModule != nullptr && pipelineLayout != nullptr
        && pipeline != nullptr;
    if (pipeline)
        wgpuComputePipelineRelease(pipeline);
    if (pipelineLayout)
        wgpuPipelineLayoutRelease(pipelineLayout);
    if (shaderModule)
        wgpuShaderModuleRelease(shaderModule);
    if (bindGroup)
        wgpuBindGroupRelease(bindGroup);
    if (layout)
        wgpuBindGroupLayoutRelease(layout);
    if (view)
        wgpuTextureViewRelease(view);
    if (texture) {
        wgpuTextureDestroy(texture);
        wgpuTextureRelease(texture);
    }
    wgpuDeviceRelease(device);
    return supported;
#else
    return false;
#endif
}

WebGpuNativeRuntimeReport runWebGpuNativeRuntimeSmoke(const WebGpuNativeRuntimeSmokeDesc& desc)
{
    WebGpuNativeRuntimeReport report;
    if (!validExtent(desc.extent)) {
        report.diagnostics = "RHI WebGPU native runtime smoke requires a valid extent";
        return report;
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    return NativeRuntimeSmokeSlice(desc).run();
#else
    report.diagnostics = "RHI WebGPU native runtime requires an Emscripten emdawnwebgpu build";
    return report;
#endif
}

#if ARP_WEBGPU_VALIDATION_ENABLED
void setupWebGpuUncapturedErrorCallback()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        std::fprintf(
            stderr, "[WebGPU] validation callback setup skipped until device bootstrap is ready\n");
        return;
    }
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    WGPUDevice device = emscripten_webgpu_get_device();
    if (!device) {
        std::fprintf(stderr, "[WebGPU] setupWebGpuUncapturedErrorCallback: no device available\n");
        return;
    }

    (void)device;
    std::fprintf(stderr,
        "[WebGPU] validation enabled; current Emscripten WebGPU C API exposes "
        "uncaptured errors through the browser console for the default device\n");
    wgpuDeviceRelease(device);
#endif // ARP_RHI_WEBGPU_NATIVE_RUNTIME_HAS_NATIVE
}
#endif // ARP_WEBGPU_VALIDATION_ENABLED

} // namespace arp::rhi_webgpu
