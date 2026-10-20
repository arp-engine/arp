#include "../fixtures/original_descriptor_depth_artifacts_fixture.h"

int main(int argc, char** argv)
{
    assert(argc == 3);
    arp::test::OriginalDescriptorDepthArtifactsFixture::verify(argv[1], argv[2]);
}
