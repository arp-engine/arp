#pragma once
#include "rhi/frontend/query_pool_arguments.h"
#include "rhi/semantics/webgpu_timestamp_command_point.h"

namespace arp::rhi::semantics {
// OriginalQuerySourceLowering/1: CPU declaration and actual API facts are
// prerequisites; neither can substitute for an original bounded native source.
class OriginalQuerySourceLowering final {
public:
    class Decision final {
        friend class OriginalQuerySourceLowering;

        Decision(std::optional<QueryPoolOriginalInputArguments> input,
            CapabilityMode mode,
            bool validator,
            const char* diagnostic)
            : input_(std::move(input)), mode_(mode), validator_(validator), diagnostic_(diagnostic)
        {}

    public:
        CapabilityMode mode() const noexcept { return mode_; }

        bool validatorOnly() const noexcept { return validator_; }

        bool nativeSourceSupported() const noexcept { return false; }

        const char* diagnostic() const noexcept { return diagnostic_; }

        const auto& originalInput() const noexcept { return input_; }

        bool matchesOriginal(const QueryPoolOriginalInputArguments& input) const noexcept
        {
            return input_ && input_->matches(input);
        }

    private:
        const std::optional<QueryPoolOriginalInputArguments> input_;
        const CapabilityMode mode_;
        const bool validator_;
        const char* const diagnostic_;
    };

    static Decision decide(Backend backend,
        std::uint32_t version,
        bool prototypeOptIn,
        const std::optional<QueryPoolOriginalInputArguments>& input,
        const WebGpuTimestampCommandPointFacts& facts = {}) noexcept
    {
        if (version != 1u)
            return {input, CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-SOURCE-VERSION"};
        if (!prototypeOptIn)
            return {input, CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-SOURCE-OPT-IN"};
        if (!input)
            return {input, CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-SOURCE-INPUT"};
        if (const auto* id = input->validateDeclaration())
            return {input, CapabilityMode::DiagnosticFailure, false, id};
        switch (backend) {
        case Backend::Null:
            return {
                input, CapabilityMode::DiagnosticFailure, true, "RHI-QUERY-SOURCE-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
            return {input,
                CapabilityMode::DiagnosticFailure,
                false,
                "RHI-QUERY-SOURCE-NATIVE-UNIMPLEMENTED"};
        case Backend::WebGPU:
            if (input->originalInput()->scalarFields().type() == QueryType::PipelineStatistics)
                return {input,
                    CapabilityMode::Degraded,
                    false,
                    "RHI-WEBGPU-ORIGINAL-QUERY-STATISTICS-UNSUPPORTED"};
            {
                const auto api = WebGpuTimestampCommandPointGate::decide(facts);
                if (!api.apiSupported)
                    return {input, api.mode, false, api.diagnosticId};
            }
            return {input,
                CapabilityMode::DiagnosticFailure,
                false,
                "RHI-WEBGPU-ORIGINAL-QUERY-PAYLOAD-UNIMPLEMENTED"};
        case Backend::WebGL:
            break;
        }
        return {
            input, CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-SOURCE-BACKEND-UNKNOWN"};
    }
};
} // namespace arp::rhi::semantics
