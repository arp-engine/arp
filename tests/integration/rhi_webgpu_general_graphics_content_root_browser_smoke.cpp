#include "../fixtures/original_general_finite_graphics_source_fixture.h"
#include "../fixtures/webgpu_original_device_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"

#ifdef NDEBUG
#error Graphics ContentPlan6 browser contracts require assertions
#endif

int main()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using namespace arp;
    assert(rhi_webgpu::webGpuNativeDeviceAvailable());
    const bool readOnly = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).get("general-content-buffer-type") ===
                   "readonly-storage";
        // clang-format on
    });
    const bool whole = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-source-whole");
        // clang-format on
    });
    const bool shaderBufferContent = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-shader-content");
        // clang-format on
    });
    const bool nativeCustody = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-custody");
        // clang-format on
    });
    const bool dependencyFreeze = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-dependency-freeze");
        // clang-format on
    });
    const bool nativePayload = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-payload");
        // clang-format on
    });
    const bool nativeCold = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-cold");
        // clang-format on
    });
    const bool nativeCommand = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-command");
        // clang-format on
    });
    const bool nativeCallback = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-callback");
        // clang-format on
    });
    const bool nativeIssuer = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-issuer");
        // clang-format on
    });
    const bool nativeRecording = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-recording");
        // clang-format on
    });
    assert(!nativeIssuer || nativeCallback);
    assert(!nativeRecording || nativeIssuer);
    // Reuse the authoritative real-device query helper; budgets remain authored
    // by this fixture and do not enable public execution or NativeSync.
    const auto desc = test::WebGpuOriginalDeviceFixture::describe(
        "Original Graphics ContentPlan6 root", 64u * 1024u * 1024u, 4096u, 4u * 1024u * 1024u);
    test::OriginalGeneralFiniteGraphicsContentContractFixture::verify();
    test::OriginalGeneralFiniteGraphicsSourceFixture::verifyActual(
        [&] {
            return rhi_webgpu::createWebGpuRhiDevice(desc);
        },
        readOnly,
        whole,
        true,
        true,
        true,
        true,
        true,
        true,
        shaderBufferContent,
        nativeCustody,
        dependencyFreeze,
        nativePayload,
        nativeCold,
        nativeCommand,
        nativeCallback,
        nativeIssuer,
        nativeRecording);
    if (nativeRecording && EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).has("general-graphics-physical-loss");
            // clang-format on
        }))
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_SCENARIO_OK original-resource-backed-root actual-device-loss no-completion-or-output public-closed");
    else if (nativeRecording && EM_ASM_INT({
                 // clang-format off
            return new URLSearchParams(location.search).has("general-graphics-submission-terminal");
                 // clang-format on
             }))
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_SCENARIO_OK original-resource-backed-root genuine-terminal-drain no-readback public-closed");
    else if (nativeRecording
        && EM_ASM_INT({
               // clang-format off
            return new URLSearchParams(location.search).has("general-graphics-native-submission");
               // clang-format on
           }))
        std::puts(EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-output-readback");
            // clang-format on
        })
                ? "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_SCENARIO_OK original-resource-backed-root genuine-queue-completion readback3 public-closed"
                : "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_SCENARIO_OK original-resource-backed-root genuine-queue-completion no-readback public-closed");
    else if (nativeRecording)
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_SCENARIO_OK original-resource-backed-root no-submit-or-output public-closed");
    else
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_SCENARIO_OK original-resource-backed-root no-native-draw-or-public-authority");
    return 0;
#else
    std::fprintf(stderr, "RHI-FINITE-GRAPHICS-CONTENT-NATIVE-FIXTURE-UNAVAILABLE\n");
    return 1;
#endif
}
