#pragma once

#include "query_command_points_fixture.h"
#include "query_pool_arguments_fixture.h"
#include "query_slot_declaration_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "webgpu_original_query_binding_fixture.h"
#include "webgpu_original_query_owner_fixture.h"
#include "webgpu_original_query_record_reservation_fixture.h"
#include "webgpu_original_query_slots_fixture.h"
#include "webgpu_original_query_source_gate_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {

class WebGpuTimestampCommandPointFixture final {
public:
    static void verifyContract()
    {
        QueryPoolArgumentsFixture::verify();
        QuerySlotDeclarationFixture::verify();
        QueryCommandPointsFixture::verify();
        WebGpuOriginalQueryOwnerFixture::verifyContract();
        WebGpuOriginalQueryBindingFixture::verifyContract();
        WebGpuOriginalQuerySlotsFixture::verifyContract();
        WebGpuOriginalQueryRecordReservationFixture::verifyContract();
        using namespace rhi::semantics;
        const auto missing = WebGpuTimestampCommandPointGate::decide({});
        assert(!missing.apiSupported && missing.mode == rhi::CapabilityMode::Degraded);
        assert(
            std::string_view(missing.diagnosticId) == "RHI-WEBGPU-TIMESTAMP-FEATURE-UNAVAILABLE");
        WebGpuTimestampCommandPointFacts complete{
            true, true, true, true, true, true, true, true, true};
        const auto ready = WebGpuTimestampCommandPointGate::decide(complete);
        assert(
            ready.apiSupported && ready.mode == rhi::CapabilityMode::Native && !ready.diagnosticId);
        const std::array<bool WebGpuTimestampCommandPointFacts::*, 9> fields{
            &WebGpuTimestampCommandPointFacts::deviceFeatureEnabled,
            &WebGpuTimestampCommandPointFacts::encoderWrite,
            &WebGpuTimestampCommandPointFacts::renderPassWrite,
            &WebGpuTimestampCommandPointFacts::computePassWrite,
            &WebGpuTimestampCommandPointFacts::resolve,
            &WebGpuTimestampCommandPointFacts::bufferCopy,
            &WebGpuTimestampCommandPointFacts::mapRead,
            &WebGpuTimestampCommandPointFacts::mappedRead,
            &WebGpuTimestampCommandPointFacts::unmap};
        for (std::size_t index = 0u; index < fields.size(); ++index) {
            auto incomplete = complete;
            incomplete.*fields[index] = false;
            const auto decision = WebGpuTimestampCommandPointGate::decide(incomplete);
            assert(!decision.apiSupported && decision.mode == rhi::CapabilityMode::Degraded);
            assert(std::string_view(decision.diagnosticId)
                == (index == 0u       ? "RHI-WEBGPU-TIMESTAMP-FEATURE-UNAVAILABLE"
                        : index <= 3u ? "RHI-WEBGPU-TIMESTAMP-COMMAND-API-UNAVAILABLE"
                                      : "RHI-WEBGPU-TIMESTAMP-READBACK-API-UNAVAILABLE"));
        }
        WebGpuOriginalQuerySourceGateFixture::verify(fields);
    }

    static void verifyActual()
    {
        using namespace rhi::semantics;
        const auto facts = rhi_webgpu::webGpuNativeTimestampCommandPointFacts();
        const auto decision = WebGpuTimestampCommandPointGate::decide(facts);
        const bool supported = rhi_webgpu::webGpuNativeTimestampQuerySupported();
        if (!decision.apiSupported)
            assert(!supported);
        std::printf("BINDLESS_RHI_TIMESTAMP_COMMAND_FACTS %u %u %u %u %u %u %u %u %u %u %s\n",
            facts.deviceFeatureEnabled ? 1u : 0u,
            facts.encoderWrite ? 1u : 0u,
            facts.renderPassWrite ? 1u : 0u,
            facts.computePassWrite ? 1u : 0u,
            facts.resolve ? 1u : 0u,
            facts.bufferCopy ? 1u : 0u,
            facts.mapRead ? 1u : 0u,
            facts.mappedRead ? 1u : 0u,
            facts.unmap ? 1u : 0u,
            supported ? 1u : 0u,
            decision.diagnosticId ? decision.diagnosticId : "NativeApiPrepared");
        std::puts("BINDLESS_RHI_TIMESTAMP_COMMAND_GATE_OK");
        WebGpuOriginalQuerySourceGateFixture::verifyActual();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WebGpuOriginalQueryOwnerFixture::verifyActual();
        WebGpuOriginalQueryBindingFixture::verifyActual();
        WebGpuOriginalQuerySlotsFixture::verifyActual();
        WebGpuOriginalQueryRecordReservationFixture::verifyActual();
#endif
    }
};

} // namespace arp::test
