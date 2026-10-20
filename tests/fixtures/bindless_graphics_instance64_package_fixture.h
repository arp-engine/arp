#pragma once
#include "bindless_graphics_runtime_vector_package_fixture.h"

namespace arp::test {
class BindlessGraphicsInstance64PackageFixture final
    : public BindlessGraphicsRuntimeVectorPackageFixture {
public:
    explicit BindlessGraphicsInstance64PackageFixture(
        const std::filesystem::path& directory, bool native = false)
        : BindlessGraphicsRuntimeVectorPackageFixture(
              directory, {"GraphicsInstance64", 64u, 4u, 8u, 224u}, native)
    {}
};
} // namespace arp::test
