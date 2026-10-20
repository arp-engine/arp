#pragma once
#include "core/external_object_slot.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "webgpu_async_wait_fixture.h"

namespace arp::test {
// Real unrelated native device for source-identity contract negatives. Scoped
// substitution changes both Emscripten's cached device and its JS query source.
class WebGpuForeignDeviceFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WebGpuForeignDeviceFixture() : device_(&release)
    {
        EM_ASM({
            // clang-format off
                Module.arpForeignDeviceFixtureReady = 0;
                const original = Module.preinitializedWebGPUDevice;
                navigator.gpu.requestAdapter()
                    .then(function(adapter) {
                        if (!adapter)
                            throw new Error('Foreign device adapter unavailable');
                        const requiredFeatures = Array.from(original.features);
                        for (const feature of requiredFeatures)
                            if (!adapter.features.has(feature))
                                throw new Error('Foreign device feature unavailable: ' + feature);
                        return adapter.requestDevice({
                            requiredFeatures : requiredFeatures,
                            requiredLimits : {
                                maxStorageBuffersPerShaderStage :
                                    original.limits.maxStorageBuffersPerShaderStage
                            }
                        });
                    })
                    .then(
                        function(device) {
                            Module.arpForeignDeviceFixture = device;
                            Module.arpForeignDeviceFixtureReady = 1;
                        },
                        function(error) {
                            Module.arpForeignDeviceFixtureError = String(error);
                            Module.arpForeignDeviceFixtureReady = -1;
                        });
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpForeignDeviceFixtureReady;
                // clang-format on
            }) != 0;
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpForeignDeviceFixtureReady;
            // clang-format on
        }) == 1);
        // The configured emdawnwebgpu port uses this exact import bridge in
        // emscripten_webgpu_get_device. It issues one real owned device ref.
        auto native = static_cast<WGPUDevice>(EM_ASM_PTR({
            // clang-format off
                    return WebGPU.importJsDevice(Module.arpForeignDeviceFixture);
            // clang-format on
        }));
        assert(native && device_.adopt(native));
    }

    ~WebGpuForeignDeviceFixture()
    {
        device_.reset();
        EM_ASM({
            // clang-format off
                delete Module.arpForeignDeviceFixture;
                delete Module.arpForeignDeviceFixtureReady;
                delete Module.arpForeignDeviceFixtureError;
            // clang-format on
        });
    }

    WebGpuForeignDeviceFixture(const WebGpuForeignDeviceFixture&) = delete;

    class Scope final {
    public:
        explicit Scope(const WebGpuForeignDeviceFixture& foreign)
        {
            EM_ASM(
                {
                    // clang-format off
                        if (Module.arpForeignDeviceFixtureOriginal !== undefined)
                            throw new Error('Foreign device scope already active');
                        Module.arpForeignDeviceFixtureOriginal = Module.preinitializedWebGPUDevice;
                        Module.arpForeignDeviceFixtureOriginalId = WebGPU.preinitializedDeviceId;
                        Module.preinitializedWebGPUDevice = WebGPU.getJsObject(Number($0));
                        WebGPU.preinitializedDeviceId = Number($0);
                    // clang-format on
                },
                foreign.device_.get());
        }

        ~Scope()
        {
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice = Module.arpForeignDeviceFixtureOriginal;
                    WebGPU.preinitializedDeviceId = Module.arpForeignDeviceFixtureOriginalId;
                    delete Module.arpForeignDeviceFixtureOriginal;
                    delete Module.arpForeignDeviceFixtureOriginalId;
                // clang-format on
            });
        }

        Scope(const Scope&) = delete;
    };

private:
    static void release(void* object) noexcept
    {
        const auto native = static_cast<WGPUDevice>(object);
        // emdawn marks imported devices IsImported with no C++ loss future.
        // GPUDevice.destroy performs real native loss; release retires the
        // imported host ref without invoking an unregistered C++ loss event.
        EM_ASM(
            {
                // clang-format off
                WebGPU.getJsObject(Number($0)).destroy();
                // clang-format on
            },
            native);
        wgpuDeviceRelease(native);
    }

    core::ExternalObjectSlot device_;
#endif
};
} // namespace arp::test
