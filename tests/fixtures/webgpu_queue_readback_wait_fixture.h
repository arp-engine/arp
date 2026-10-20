#pragma once
#include "webgpu_async_wait_fixture.h"

#include <webgpu/webgpu.h>

namespace arp::test {
class WebGpuQueueReadbackWaitFixture final {
    struct Result {
        bool done = false, success = false;
    };

public:
    static bool queue(WGPUQueue queue)
    {
        Result result;
        auto callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &result;
        callback.callback =
            [](WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata, void*) {
                auto& result = *static_cast<Result*>(userdata);
                result.success = status == WGPUQueueWorkDoneStatus_Success;
                result.done = true;
            };
        wgpuQueueOnSubmittedWorkDone(queue, callback);
        return WebGpuAsyncWaitFixture::until([&] {
            return result.done;
        }) && result.success;
    }

    static bool map(WGPUBuffer buffer, std::size_t bytes)
    {
        Result result;
        auto callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &result;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*) {
            auto& result = *static_cast<Result*>(userdata);
            result.success = status == WGPUMapAsyncStatus_Success;
            result.done = true;
        };
        wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0u, bytes, callback);
        return WebGpuAsyncWaitFixture::until([&] {
            return result.done;
        }) && result.success;
    }
};
} // namespace arp::test
