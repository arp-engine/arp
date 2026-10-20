#pragma once
#include "rhi/backends/webgpu/webgpu_native_error_scope_batch.h"
#include "webgpu_async_wait_fixture.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
EM_ASYNC_JS(int,
    arpWebGpuFixtureObserveScopes,
    (std::uintptr_t handle, std::uint32_t count, std::uint32_t expectValidation),
    {
        // clang-format off
        const device = WebGPU.getJsObject(handle);
        const errors =
            await Promise.all(Array.from({length: count}, () => device.popErrorScope()));
        const rejected = errors.filter(Boolean);
        if (expectValidation) {
            if (rejected.length === 1 && rejected[0] instanceof GPUValidationError) {
                Module.print('RHI-WEBGPU-FIXTURE-EXPECTED-VALIDATION ' + rejected[0].message);
                return 1;
            }
            return 0;
        }
        if (rejected.length) {
            Module.printErr(
                'RHI-WEBGPU-FIXTURE-NATIVE-SCOPE ' + rejected.map(e => e.message).join('; '));
            return 0;
        }
        return 1;
        // clang-format on
    });
#endif
namespace arp::test {
// Actual native promises are retained; scopes still execute on the real device.
template <std::uint32_t Domain> class WebGpuErrorScopeHandoffFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void holdScopes(bool diagnoseErrors = false)
    {
        EM_ASM(
            {
                // clang-format off
                    Module.arpCreationScopeHeld = Module.arpCreationScopeHeld || {};
                    Module.arpCreationScopeHeld[$0] = [];
                    Module.arpCreationScopeOriginal = Module.preinitializedWebGPUDevice.popErrorScope;
                    Module.preinitializedWebGPUDevice.popErrorScope = function() {
                        return Module.arpCreationScopeOriginal.call(this).then(function(v) {
                            if ($1 && v)
                                Module.printErr('RHI-WEBGPU-FIXTURE-HELD-SCOPE-ERROR domain=' + $0 + ' ' +
                                                v.message);
                            return new Promise(function(resolve) {
                                Module.arpCreationScopeHeld[$0].push(function() {
                                    resolve(v);
                                });
                            });
                        });
                    };
                // clang-format on
            },
            Domain,
            diagnoseErrors);
    }

    static void restoreScopes()
    {
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.popErrorScope = Module.arpCreationScopeOriginal;
                delete Module.arpCreationScopeOriginal;
            // clang-format on
        });
    }

    static void waitHeldScopes(std::uint32_t count = rhi_webgpu::WebGpuNativeErrorScopeBatch::count)
    {
        assert(count && count <= rhi_webgpu::WebGpuNativeErrorScopeBatch::count);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return EM_ASM_INT(
                       {
                           // clang-format off
                    return Module.arpCreationScopeHeld[$0].length;
                           // clang-format on
                       },
                       Domain)
                == count;
        }));
    }

    static void releaseScopes()
    {
        EM_ASM(
            {
                // clang-format off
                    for (var release of Module.arpCreationScopeHeld[$0])
                        if (release)
                            release();
                    Module.arpCreationScopeHeld[$0] = [];
                // clang-format on
            },
            Domain);
    }

    static void releaseScope(std::uint32_t index)
    {
        EM_ASM(
            {
                // clang-format off
                    var release = Module.arpCreationScopeHeld[$0][$1];
                    if (release) {
                        Module.arpCreationScopeHeld[$0][$1] = null;
                        release();
                    }
                // clang-format on
            },
            Domain,
            index);
    }
#endif
};
} // namespace arp::test
