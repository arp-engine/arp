#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_query_source_gate.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace arp::test {
class WebGpuOriginalQuerySourceGateFixture final {
    using Lowering = rhi::semantics::OriginalQuerySourceLowering;
    using Gate = rhi_webgpu::WebGpuOriginalQuerySourceGate;
    using Input = rhi::QueryPoolInputSnapshot;
    static constexpr std::uint64_t cpuBytes = 65536u;
    using Quotas = std::array<std::shared_ptr<core::CpuMemoryQuota>, 3>;

    static auto prepare(Quotas& quotas, rhi::QueryType type = rhi::QueryType::Timestamp)
    {
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        rhi::QueryPoolDesc d;
        d.type = type;
        d.count = 2u;
        if (type == rhi::QueryType::PipelineStatistics)
            d.pipelineStatisticsMask = rhi::kAllPipelineStatistics;
        const char* diagnostic = nullptr;
        auto input = Input::prepareOriginal(d,
            {1u, true, 0u, 0u, cpuBytes, cpuBytes, cpuBytes},
            quotas[0],
            quotas[1],
            quotas[2],
            diagnostic);
        assert(input);
        return input;
    }

public:
    static void verify(
        const std::array<bool rhi::semantics::WebGpuTimestampCommandPointFacts::*, 9>& fields)
    {
        using namespace rhi;
        using namespace rhi::semantics;
        static_assert(!std::is_default_constructible_v<Lowering::Decision>);
        Quotas q, otherQuotas, statisticsQuotas;
        auto input = prepare(q);
        auto other = prepare(otherQuotas);
        auto statisticsInput = prepare(statisticsQuotas, QueryType::PipelineStatistics);
        auto command = QueryPoolOriginalInputArguments::captureOriginal(
            CommandUsageKind::Timestamp, 1u, input);
        auto otherCommand = QueryPoolOriginalInputArguments::captureOriginal(
            CommandUsageKind::Timestamp, 1u, other);
        auto statistics = QueryPoolOriginalInputArguments::captureOriginal(
            CommandUsageKind::PipelineStatistics, 1u, statisticsInput);
        assert(command && statistics && otherCommand);
        auto forged = std::shared_ptr<const Input>(q[0], input.get());
        assert(!QueryPoolOriginalInputArguments::captureOriginal(
            CommandUsageKind::Timestamp, 1u, forged));
        forged.reset();
        const WebGpuTimestampCommandPointFacts complete{
            true, true, true, true, true, true, true, true, true};
        const auto old = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(std::string_view(
                   Lowering::decide(Backend::WebGPU, 0u, true, command, complete).diagnostic())
            == "RHI-QUERY-SOURCE-VERSION");
        assert(std::string_view(
                   Lowering::decide(Backend::WebGPU, 1u, false, command, complete).diagnostic())
            == "RHI-QUERY-SOURCE-OPT-IN");
        assert(
            std::string_view(Lowering::decide(Backend::WebGPU, 1u, true, {}, complete).diagnostic())
            == "RHI-QUERY-SOURCE-INPUT");
        for (const auto backend : {Backend::Null,
                 Backend::Vulkan,
                 Backend::Metal,
                 Backend::WebGPU,
                 Backend::WebGL,
                 static_cast<Backend>(254u),
                 static_cast<Backend>(255u)}) {
            const auto decision = Lowering::decide(backend, 1u, true, command, complete);
            assert(decision.mode() == CapabilityMode::DiagnosticFailure
                && !decision.nativeSourceSupported()
                && decision.validatorOnly() == (backend == Backend::Null));
            assert(decision.matchesOriginal(*command) && !decision.matchesOriginal(*otherCommand)
                && !decision.matchesOriginal(*statistics));
            assert(backend == Backend::WebGPU ? std::string_view(decision.diagnostic())
                        == "RHI-WEBGPU-ORIGINAL-QUERY-PAYLOAD-UNIMPLEMENTED"
                                              : decision.diagnostic() != nullptr);
        }
        for (const auto field : fields) {
            auto missing = complete;
            missing.*field = false;
            const auto decision = Lowering::decide(Backend::WebGPU, 1u, true, command, missing);
            const auto api = WebGpuTimestampCommandPointGate::decide(missing);
            assert(decision.mode() == CapabilityMode::Degraded && !decision.nativeSourceSupported()
                && decision.diagnostic() == api.diagnosticId);
        }
        {
            const auto unsupported =
                Lowering::decide(Backend::WebGPU, 1u, true, statistics, complete);
            assert(unsupported.mode() == CapabilityMode::Degraded
                && std::string_view(unsupported.diagnostic())
                    == "RHI-WEBGPU-ORIGINAL-QUERY-STATISTICS-UNSUPPORTED");
        }
        auto invalidIndex = QueryPoolOriginalInputArguments::captureOriginal(
            CommandUsageKind::Timestamp, 2u, input);
        auto invalidKind = QueryPoolOriginalInputArguments::captureOriginal(
            CommandUsageKind::PipelineStatistics, 0u, input);
        assert(std::string_view(
                   Lowering::decide(Backend::WebGPU, 1u, true, invalidIndex, complete).diagnostic())
            == "RHI-QUERY-COMMAND-INDEX");
        assert(std::string_view(
                   Lowering::decide(Backend::WebGPU, 1u, true, invalidKind, complete).diagnostic())
            == "RHI-QUERY-COMMAND-TYPE");
        assert(!Lowering::decide(static_cast<Backend>(UINT32_MAX), 1u, true, command, complete)
                .nativeSourceSupported());
        invalidIndex.reset();
        invalidKind.reset();
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(old);
        command.reset();
        otherCommand.reset();
        statistics.reset();
        input.reset();
        other.reset();
        statisticsInput.reset();
        for (const auto* quotas : {&q, &otherQuotas, &statisticsQuotas})
            for (const auto& quota : *quotas)
                assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_QUERY_SOURCE_GATE_CONTRACT_OK backends4 missing-facts9 api-only-closed exact-input version-optin kinds-index allocation0");
    }

    static void verifyActual()
    {
        Quotas q;
        auto input = prepare(q);
        auto command = rhi::QueryPoolOriginalInputArguments::captureOriginal(
            rhi::CommandUsageKind::Timestamp, 0u, input);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto decision = Gate::decideOriginal(1u, true, command);
        assert(!decision.nativeSourceSupported() && decision.matchesOriginal(*command)
            && Gate::admissionDiagnostic(decision));
        const auto api = rhi::semantics::WebGpuTimestampCommandPointGate::decide(
            rhi_webgpu::webGpuNativeTimestampCommandPointFacts());
        assert(decision.mode()
            == (api.apiSupported ? rhi::CapabilityMode::DiagnosticFailure
                                 : rhi::CapabilityMode::Degraded));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        command.reset();
        input.reset();
        // The value decision retains the exact original CPU input until it retires.
        std::printf(
            "BINDLESS_RHI_QUERY_SOURCE_GATE_ACTUAL_OK %s %s native-source-closed allocation0\n",
            api.apiSupported ? "DiagnosticFailure" : "Degraded",
            decision.diagnostic());
    }

    static void prepareActualLoss()
    {
        auto input = prepare(lossQuotas_);
        auto command = rhi::QueryPoolOriginalInputArguments::captureOriginal(
            rhi::CommandUsageKind::Timestamp, 0u, input);
        lossDecision_.emplace(Gate::decideOriginal(1u, true, command));
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost() && lossDecision_
            && !lossDecision_->nativeSourceSupported());
        assert(std::string_view(Gate::admissionDiagnostic(*lossDecision_))
            == "RHI-WEBGPU-ORIGINAL-QUERY-DEVICE-LOST");
        lossDecision_.reset();
        for (auto& q : lossQuotas_) {
            assert(!q->liveBytes());
            q.reset();
        }
        std::puts(
            "BINDLESS_RHI_QUERY_SOURCE_GATE_LOSS_OK original-decision-retained actual-loss admission-closed exact-retire");
    }

private:
    static inline Quotas lossQuotas_;
    static inline std::optional<Lowering::Decision> lossDecision_;
};
} // namespace arp::test
