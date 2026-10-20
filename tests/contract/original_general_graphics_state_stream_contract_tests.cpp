#include "../fixtures/original_general_finite_graphics_state_stream_fixture.h"
#include "../fixtures/original_general_finite_graphics_usage_fixture.h"

#ifdef NDEBUG
#error Finite Graphics State/Stream contracts require assertions
#endif

int main()
{
    arp::test::OriginalGeneralFiniteGraphicsStateStreamFixture::verifyContract();
    arp::test::OriginalGeneralFiniteGraphicsUsageFixture::verifyContract();
}
