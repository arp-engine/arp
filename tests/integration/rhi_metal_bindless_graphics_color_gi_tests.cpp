#include "tests/fixtures/metal_graphics_gi_fixture.h"

int main(int argc, char** argv)
{
    return arp::test::runMetalGraphicsGiFixture<arp::test::OriginalGraphicsColorGiDataFixture,
        false>(argc, argv);
}
