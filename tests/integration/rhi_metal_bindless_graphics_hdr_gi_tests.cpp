#include "tests/fixtures/metal_graphics_gi_fixture.h"
#include "tests/fixtures/original_graphics_hdr_gi_data_fixture.h"

int main(int argc, char** argv)
{
    return arp::test::runMetalGraphicsGiFixture<arp::test::OriginalGraphicsHdrGiDataFixture, true>(
        argc, argv);
}
