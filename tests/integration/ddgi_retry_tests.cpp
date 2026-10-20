#include "fixtures/recorded_resource_state_checks.h"
#include "rhi/backends/null/null_backend.h"
#include "tools/render_feature_lab/postprocess_fixture.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace arp;
    using namespace tools::render_feature_lab;
    {
        auto device = rhi::createNullDevice();
        tests::checkRecordedResourceStates(*device);
    }
    for (const auto fault : {DdgiFixtureFault::None,
             DdgiFixtureFault::AfterHistoryCopy,
             DdgiFixtureFault::AfterResolve}) {
        auto device = rhi::createNullDevice();
        std::cout << "Null DDGI retry fault=" << static_cast<int>(fault) << std::endl;
        try {
            const auto result =
                runDdgiFeatureFixtureFrames(*device, {32u, 32u}, 3u, nullptr, fault);
            if (!result.success)
                std::cerr << result.diagnostic << '\n';
            assert(result.success
                && result.ddgiRejectedFrames == (fault == DdgiFixtureFault::None ? 0u : 1u));
            // Null proves command validation and scheduler state only.
            assert(!result.ddgiReadbackAvailable);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
}
