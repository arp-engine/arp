#include "../fixtures/original_general_descriptor_binding_fixture.h"
#include "../fixtures/original_general_descriptor_source_fixture.h"
#include "../fixtures/original_general_finite_consumption_fixture.h"
#include "../fixtures/original_general_finite_content_plan_fixture.h"
#include "../fixtures/original_general_finite_initialized_content_fixture.h"
#include "../fixtures/original_general_finite_inputs_fixture.h"
#include "../fixtures/original_general_finite_native_closure_fixture.h"
#include "../fixtures/original_general_finite_native_projection_fixture.h"
#include "../fixtures/original_general_finite_native_recording_fixture.h"
#include "../fixtures/original_general_finite_state_stream_fixture.h"
#include "../fixtures/original_general_finite_usage_fixture.h"
#include "../fixtures/original_target_memory_interval_fixture.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>

int main()
{
    const std::array<std::shared_ptr<arp::core::CpuMemoryQuota>, 0u> noQuotas{};
    assert(!arp::test::CpuQuotaFactoryFixture::verifyHotNoAllocation(noQuotas, []() noexcept {
        arp::test::OriginalGeneralDescriptorBindingFixture::verifyContract();
        arp::test::OriginalGeneralDescriptorSourceFixture::verifyContract();
        arp::test::OriginalGeneralFiniteInitializedContentFixture::verifyContract();
        arp::test::OriginalGeneralFiniteInputsFixture::verifyContract();
        arp::test::OriginalGeneralFiniteConsumptionFixture::verifyContract();
        arp::test::OriginalGeneralFiniteStateStreamFixture::verifyContract();
        arp::test::OriginalGeneralFiniteUsageFixture::verifyContract();
        arp::test::OriginalGeneralFiniteContentPlanFixture::verifyContract();
        arp::test::OriginalGeneralFiniteNativeClosureFixture::verifyContract();
        arp::test::OriginalGeneralFiniteNativeProjectionFixture::verifyContract();
        arp::test::OriginalGeneralFiniteNativeRecordingFixture::verifyContract();
        return static_cast<const char*>(nullptr);
    }));
    // This fixture creates its quota cold, then pressures its own hot validator.
    arp::test::OriginalTargetMemoryIntervalFixture::verifyContract();
    auto device = arp::rhi::createNullDevice();
    assert(device);
    arp::test::OriginalGeneralDescriptorWriteFixture::verifyUnsupported(*device);
    arp::test::OriginalGeneralDescriptorSourceFixture::verifyUnsupported(*device);
    arp::test::OriginalGeneralFiniteInitializedContentFixture::verifyUnsupported(*device);
    arp::test::OriginalGeneralFiniteConsumptionFixture::verifyUnsupported(*device);
    arp::test::OriginalGeneralFiniteUsageFixture::verifyUnsupported(*device);
    return 0;
}
