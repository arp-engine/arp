#pragma once
#include "bindless_graphics_runtime_vector_package_fixture.h"

namespace arp::test {
class BindlessGraphicsTerrain27PackageFixture final
    : public BindlessGraphicsRuntimeVectorPackageFixture {
public:
    explicit BindlessGraphicsTerrain27PackageFixture(
        const std::filesystem::path& directory, bool native = false)
        : BindlessGraphicsRuntimeVectorPackageFixture(directory,
              {"GraphicsTerrain27",
                  32u,
                  2u,
                  1u,
                  16u,
                  Msl::RootForm::Value,
                  Wgsl::RootForm::UniformValue},
              native)
    {}
};
} // namespace arp::test
