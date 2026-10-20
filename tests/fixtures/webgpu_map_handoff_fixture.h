#pragma once

#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "webgpu_async_wait_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>
#include <utility>

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
namespace arp::test {

// Retains the real native map promise; tests choose delivery order without
// synthesizing success. Error modes invoke invalid native operations.
template <std::uint32_t Domain> class WebGpuMapPromiseHandoffFixture final {
public:
    static void install(WGPUBuffer buffer, unsigned mode, bool hold)
    {
        EM_ASM(
            {
                // clang-format off
                    Module.arpMapHandoff = Module.arpMapHandoff || {};
                    const b = WebGPU.getJsObject(Number($0));
                    const c = ({
                        buffer : b,
                        map : b.mapAsync,
                        read : b.getMappedRange,
                        unmap : b.unmap,
                        releases : [],
                        calls : 0,
                        mode : $1,
                        hold : !!$2,
                        args : null
                    });
                    if (c.mode === 3)
                        Module.arpMappedRangeNativeExceptions = 0;
                    Module.arpMapHandoff[$3] = c;
                    b.mapAsync = function(mode, offset, size) {
                        c.calls++;
                        c.args = ([ mode, offset, size ]);
                        if (c.mode === 6)
                            Module.preinitializedWebGPUDevice.createBuffer({size : 4, usage : 0});
                        const p = c.map.call(this, mode, c.mode === 2 ? 1 : offset, size);
                        if (!c.hold)
                            return p;
                        return p.then(
                            function(value) {
                                return new Promise(function(resolve) {
                                    c.releases.push(function() {
                                        resolve(value);
                                    });
                                });
                            },
                            function(error) {
                                return new Promise(function(resolve, reject) {
                                    c.releases.push(function() {
                                        reject(error);
                                    });
                                });
                            });
                    };
                    if (c.mode === 3)
                        b.getMappedRange = function(offset, size) {
                            try {
                                return c.read.call(this, offset, size + 4);
                            } catch (ex) {
                                Module.arpMappedRangeNativeExceptions++;
                                Module.arpExpectedMappedRangeDiagnostic =
                                    "buffer.getMappedRange(" + offset + ", " + size + ") failed: " + ex;
                                Module.arpExpectedMappedRangeObserved = 0;
                                throw ex;
                            }
                        };
                    if (c.mode === 7)
                        b.unmap = function() {};
                // clang-format on
            },
            buffer,
            mode,
            hold,
            Domain);
    }

    static void restoreMap()
    {
        EM_ASM(
            {
                // clang-format off
                    const c = Module.arpMapHandoff[$0];
                    c.buffer.mapAsync = c.map;
                // clang-format on
            },
            Domain);
    }

    static void waitHeld()
    {
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT(
                       {
                           // clang-format off
                    return Module.arpMapHandoff[$0].releases.length;
                           // clang-format on
                       },
                       Domain)
                == 1;
        }));
    }

    static void release()
    {
        EM_ASM(
            {
                // clang-format off
                    const c = Module.arpMapHandoff[$0];
                    for (const f of c.releases)
                        f();
                    c.releases = [];
                // clang-format on
            },
            Domain);
    }

    static void verifyArguments(std::size_t bytes)
    {
        assert(EM_ASM_INT(
                   {
                       // clang-format off
                           const c = Module.arpMapHandoff[$0];
                           return c.calls === 1 && c.args[0] === GPUMapMode.READ && c.args[1] === 0 &&
                                  c.args[2] === Number($1);
                       // clang-format on
                   },
                   Domain,
                   bytes)
            == 1);
    }

    static void restoreAll()
    {
        EM_ASM(
            {
                // clang-format off
                    const c = Module.arpMapHandoff[$0];
                    c.buffer.mapAsync = c.map;
                    c.buffer.getMappedRange = c.read;
                    c.buffer.unmap = c.unmap;
                    if (c.mode === 7)
                        c.unmap.call(c.buffer);
                    delete Module.arpMapHandoff[$0];
                    if (c.mode === 3) {
                        delete Module.arpExpectedMappedRangeDiagnostic;
                        delete Module.arpExpectedMappedRangeObserved;
                        delete Module.arpMappedRangeNativeExceptions;
                    }
                // clang-format on
            },
            Domain);
    }
};

class WebGpuMapHandoffFixture final {
public:
    // Reusable native fixture: observe real Pending/Mapped states without
    // reading data or treating a newly created buffer as copied content.
    template <class Inspect>
    static void verifyMapStateSeparation(WGPUBuffer buffer, std::size_t bytes, Inspect inspect)
    {
        PendingMap map;
        map.start(buffer, bytes);
        inspect();
        map.waitReady();
        assert(wgpuBufferGetMapState(buffer) == WGPUBufferMapState_Mapped);
        inspect();
        wgpuBufferUnmap(buffer);
        assert(wgpuBufferGetMapState(buffer) == WGPUBufferMapState_Unmapped);
    }

    template <class Inspect>
    static void inspectMappedBuffer(WGPUBuffer buffer, std::size_t bytes, Inspect inspect)
    {
        PendingMap map;
        map.start(buffer, bytes);
        map.waitReady();
        assert(wgpuBufferGetMapState(buffer) == WGPUBufferMapState_Mapped);
        inspect();
        wgpuBufferUnmap(buffer);
        assert(wgpuBufferGetMapState(buffer) == WGPUBufferMapState_Unmapped);
    }

    static void verifyActual()
    {
        WebGpuMapHandoffFixture fixture;
        fixture.verifySynchronousTimingError();
        fixture.verifySynchronousNativeMapError();
        fixture.verifyAsynchronousNativeMapData();
        std::puts("BINDLESS_RHI_MAP_HANDOFF_OK 2 1");
    }

private:
    class PendingMap final {
    public:
        void start(WGPUBuffer buffer, std::size_t byteCount)
        {
            auto info = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            info.mode = WGPUCallbackMode_AllowSpontaneous;
            info.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* context, void*) {
                auto& self = *static_cast<PendingMap*>(context);
                self.done_ = true;
                self.status_ = status;
            };
            info.userdata1 = this;
            (void)wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0u, byteCount, info);
            assert(!done_ && wgpuBufferGetMapState(buffer) == WGPUBufferMapState_Pending);
        }

        void verifyOriginalAborted() const
        {
            // Actual native Destroy synchronously aborts the first original map
            // in the pinned bridge; no legacy global result queue is involved.
            assert(done_ && status_ == WGPUMapAsyncStatus_Aborted);
        }

        void waitReady() const
        {
            assert(WebGpuAsyncWaitFixture::until([&] {
                return done_;
            }));
            assert(status_ == WGPUMapAsyncStatus_Success);
        }

    private:
        bool done_ = false;
        WGPUMapAsyncStatus status_ = WGPUMapAsyncStatus_Force32;
    };

    WebGpuMapHandoffFixture()
        : device_(emscripten_webgpu_get_device()),
          quota_(core::makeShared<core::CpuMemoryQuota>(4096u))
    {
        assert(device_);
    }

    ~WebGpuMapHandoffFixture() { wgpuDeviceRelease(device_); }

    WGPUBuffer createBuffer(std::size_t bytes, WGPUBufferUsage usage)
    {
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = bytes;
        desc.usage = usage;
        const auto buffer = wgpuDeviceCreateBuffer(device_, &desc);
        assert(buffer);
        return buffer;
    }

    void verifySynchronousTimingError()
    {
        constexpr auto bytes = sizeof(std::uint64_t) * 2u;
        auto readback = createBuffer(bytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        const auto resolve = createBuffer(bytes, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        PendingMap original;
        original.start(readback, bytes);
        const auto serial = rhi_webgpu::webGpuNativeAllocateTimingFrameSerial();
        {
            core::CpuMemoryQuotaScope scope(quota_);
            rhi_webgpu::mapWebGpuNativeTimingReadbackBuffers(
                resolve, std::exchange(readback, nullptr), serial);
            // Duplicate native request delivers Error before the helper returns.
            // No query is executed or timestamp value synthesized by this test.
            original.verifyOriginalAborted();
            assert(!quota_->liveBytes());
        }
        const auto sample = rhi_webgpu::webGpuNativeLatestGpuTimingSample();
        assert(sample.availability == rhi::GpuTimingAvailability::Pending);
        assert(sample.completedFrameSerial != serial);
        assert(std::string_view(sample.diagnostic) == "WebGPU timestamp-query readback failed");
        std::puts("BINDLESS_RHI_MAP_TIMING_SYNCHRONOUS_ERROR_OK");
    }

    void verifySynchronousNativeMapError()
    {
        auto readback =
            createBuffer(sizeof(float), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        PendingMap original;
        original.start(readback, sizeof(float));

        struct Receipt {
            bool delivered = false;
            WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Force32;
        } receipt;

        auto info = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        info.mode = WGPUCallbackMode_AllowSpontaneous;
        info.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*) {
            auto& received = *static_cast<Receipt*>(userdata);
            received.delivered = true;
            received.status = status;
        };
        info.userdata1 = &receipt;
        (void)wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0u, sizeof(float), info);
        assert(receipt.delivered && receipt.status == WGPUMapAsyncStatus_Error);
        wgpuBufferDestroy(readback);
        original.verifyOriginalAborted();
        wgpuBufferRelease(readback);
        assert(!quota_->liveBytes());
        std::puts(
            "BINDLESS_RHI_MAP_NATIVE_SYNCHRONOUS_ERROR_OK retired-global-queue actual-duplicate-map-error original-aborted");
    }

    void verifyAsynchronousNativeMapData()
    {
        // WebGPU guarantees initialized buffer bytes. This is a raw native API
        // callback test fixture; it supplies no production completion evidence.
        auto readback =
            createBuffer(sizeof(float), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        PendingMap original;
        original.start(readback, sizeof(float));
        original.waitReady();
        float actual = 1.0f;
        assert(
            wgpuBufferReadMappedRange(readback, 0u, &actual, sizeof(actual)) == WGPUStatus_Success
            && actual == 0.0f);
        wgpuBufferUnmap(readback);
        wgpuBufferDestroy(readback);
        wgpuBufferRelease(readback);
        assert(!quota_->liveBytes());
        std::puts(
            "BINDLESS_RHI_MAP_NATIVE_ASYNCHRONOUS_DATA_OK retired-global-queue actual-buffer-zero map-read-unmap");
    }

    WGPUDevice device_ = nullptr;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
};

} // namespace arp::test
#endif
