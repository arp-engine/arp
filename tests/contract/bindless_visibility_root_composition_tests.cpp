#include "../fixtures/original_bindless_visibility_roots_fixture.h"

namespace arp::test {
void validateOriginalBindlessVisibilityRoots(
    const std::shared_ptr<const rhi::BindlessShaderPackage>& package)
{
    OriginalBindlessVisibilityRootsFixture::verify(package);
    OriginalBindlessVisibilityRootsFixture::verify(package, true);
}
} // namespace arp::test
