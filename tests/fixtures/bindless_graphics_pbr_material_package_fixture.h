#pragma once
#include "bindless_graphics_runtime_vector_package_fixture.h"

namespace arp::test {
class BindlessGraphicsPbrMaterialPackageFixture final
    : public BindlessGraphicsRuntimeVectorPackageFixture {
public:
    explicit BindlessGraphicsPbrMaterialPackageFixture(
        const std::filesystem::path& directory, bool native = false)
        : BindlessGraphicsRuntimeVectorPackageFixture(directory,
              {"GraphicsPbrMaterial",
                  96u,
                  6u,
                  1u,
                  16u,
                  Msl::RootForm::Value,
                  Wgsl::RootForm::UniformValue},
              native)
    {}
};
} // namespace arp::test
