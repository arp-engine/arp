#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Versioned value-only decisions. Template results preserve existing public decision types.
class BindlessPreparationLowering final {
public:
    template <class Result>
    static constexpr Result BindlessIndirectPacketStorage_closedConsumerLowering(
        Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-CLOSED-CONSUMER-PACKET-ISSUANCE-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, true, false, "RHI-CLOSED-CONSUMER-PACKET-ISSUANCE-COLD-PROTOTYPE"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, false, "RHI-CLOSED-CONSUMER-PACKET-ISSUANCE-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static constexpr Result BindlessIndirectPacketStorage_coldLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-BINDLESS-COLD-PACKET-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, true, false, "RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-STORAGE-ONLY"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, false, "RHI-BINDLESS-COLD-PACKET-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static constexpr Result CanonicalZeroBaseIndirectConsumer_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-CANONICAL-INDIRECT-CONSUMER-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, false, "RHI-CANONICAL-INDIRECT-CONSUMER-EXECUTION-UNAVAILABLE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static constexpr Result CanonicalConsumerImageContent_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, false, "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-REVISION-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static constexpr Result CanonicalZeroBaseIndirectConsumerGeometry_coldLowering(
        Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-CANONICAL-CONSUMER-GEOMETRY-COLD-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, true, "RHI-CANONICAL-CONSUMER-GEOMETRY-COLD-PROTOTYPE"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, "RHI-CANONICAL-CONSUMER-GEOMETRY-COLD-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static constexpr Result CanonicalZeroBaseIndirectConsumerGeometry_lowering(
        Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-CANONICAL-CONSUMER-GEOMETRY-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, false, "RHI-CANONICAL-CONSUMER-GEOMETRY-EXECUTION-UNAVAILABLE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static constexpr Result CanonicalExclusiveConsumerImages_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-EXCLUSIVE-CONSUMER-IMAGES-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, true, false, "RHI-EXCLUSIVE-CONSUMER-IMAGES-COLD-PROTOTYPE"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, false, "RHI-EXCLUSIVE-CONSUMER-IMAGES-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static constexpr Result CanonicalClosedConsumerTarget_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-CLOSED-CONSUMER-TARGET-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, true, false, "RHI-CLOSED-CONSUMER-TARGET-PROTOTYPE-PREPARATION-ONLY"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, false, "RHI-CLOSED-CONSUMER-TARGET-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {false, false, false, "RHI-CLOSED-CONSUMER-TARGET-BACKEND-UNKNOWN"};
    }

    template <class Result, class Validator0>
    static constexpr Result CanonicalZeroBaseIndirectConsumerColdBinding_lowering(
        Backend backend, Validator0 executionDiagnostic) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-CANONICAL-CONSUMER-COLD-RESOURCES-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, true, false, executionDiagnostic()};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, false, "RHI-CANONICAL-CONSUMER-COLD-RESOURCES-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result, class Validator0>
    static constexpr Result CanonicalZeroBaseIndirectColdBinding_lowering(
        Backend backend, Validator0 executionDiagnostic) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-CANONICAL-COLD-BINDINGS-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, true, false, executionDiagnostic()};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, false, "RHI-CANONICAL-COLD-BINDINGS-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result, class PrivateDisposition>
    static Result CanonicalZeroBaseIndirectProducerRecording_privateRecordingLowering(
        Backend backend, std::uint32_t version) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {version,
                PrivateDisposition::ValidatorOnly,
                "RHI-CANONICAL-PRODUCER-PRIVATE-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {
                version, PrivateDisposition::Prototype, "RHI-CANONICAL-PRODUCER-PRIVATE-PROTOTYPE"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {version,
                PrivateDisposition::DiagnosticFailure,
                "RHI-CANONICAL-PRODUCER-PRIVATE-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static constexpr Result CanonicalZeroBaseIndirectProducerRecording_lowering(
        Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-CANONICAL-PRODUCER-SEQUENCE-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, false, "RHI-CANONICAL-PRODUCER-SEQUENCE-CONTRACT-MISSING"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, "RHI-CANONICAL-PRODUCER-SEQUENCE-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result, class PrivateDisposition>
    static Result CanonicalClosedConsumerRecording_privateRecordingLowering(
        Backend backend, std::uint32_t version) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {version,
                PrivateDisposition::ValidatorOnly,
                "RHI-CLOSED-CONSUMER-PRIVATE-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {
                version, PrivateDisposition::Prototype, "RHI-CLOSED-CONSUMER-PRIVATE-PROTOTYPE"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {version,
                PrivateDisposition::DiagnosticFailure,
                "RHI-CLOSED-CONSUMER-PRIVATE-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {version,
            PrivateDisposition::DiagnosticFailure,
            "RHI-CLOSED-CONSUMER-PRIVATE-BACKEND-UNKNOWN"};
    }

    template <class Result>
    static constexpr Result CanonicalClosedConsumerRecording_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-CLOSED-CONSUMER-RECORDING-VALIDATOR-ONLY"};
        case Backend::Metal:
            return {false, false, "RHI-CLOSED-CONSUMER-ISSUANCE-REVISION-PAYLOAD-MISSING"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, "RHI-CLOSED-CONSUMER-RECORDING-PAYLOAD-MISSING"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
