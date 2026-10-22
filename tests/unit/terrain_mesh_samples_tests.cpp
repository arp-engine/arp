#include "3d/terrain/terrain_mesh_samples.h"

#include <cassert>
#include <limits>

namespace {
class HeightSource final {
public:
    float offset = 0.0f;

    float sample(float x, float z) const { return offset + 2.0f * x + 3.0f * z; }
};
} // namespace

int main()
{
    HeightSource source;
    const auto grid = arp::terrain::TerrainMeshSamples::build(source, 2u, 2u, 4.0f, 0.25f);
    assert(grid.valid() && grid.samples().size() == 9u);
    const auto center = grid.samples()[4];
    assert(center.height == 0.0f);
    assert(center.offsetHeightX == 0.5f && center.offsetHeightZ == 0.75f);
    source.offset = 20.0f;
    assert(grid.samples()[4].height == 0.0f);
    assert(!arp::terrain::TerrainMeshSamples::build(source, 0u, 2u, 4.0f, 0.25f).valid());
    source.offset = std::numeric_limits<float>::quiet_NaN();
    const auto invalid = arp::terrain::TerrainMeshSamples::build(source, 2u, 2u, 4.0f, 0.25f);
    assert(!invalid.valid() && invalid.samples().empty());
    assert(invalid.diagnostic() != nullptr);
}
