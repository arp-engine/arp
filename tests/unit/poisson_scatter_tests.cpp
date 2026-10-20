#include "pcg/poisson_scatter.h"

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char* label)
{
    if (!condition) {
        std::printf("failed: %s\n", label);
        std::exit(1);
    }
}

void verifyDeterministicAndSpaced(float width, float height, float minDistance, std::uint32_t seed)
{
    auto first = arp::pcg::poissonDiskPoints(width, height, minDistance, seed);
    auto second = arp::pcg::poissonDiskPoints(width, height, minDistance, seed);
    require(!first.empty(), "sampler produces points");
    require(first.size() == second.size(), "same seed produces same point count");

    const float minDistanceSquared = minDistance * minDistance;
    for (size_t i = 0; i < first.size(); ++i) {
        require(first[i].x >= 0.0f && first[i].x < width, "point x remains in domain");
        require(first[i].y >= 0.0f && first[i].y < height, "point y remains in domain");
        require(first[i].x == second[i].x && first[i].y == second[i].y,
            "same seed produces the same sequence");
        for (size_t j = 0; j < i; ++j) {
            const float dx = first[i].x - first[j].x;
            const float dy = first[i].y - first[j].y;
            require(dx * dx + dy * dy >= minDistanceSquared,
                "accepted points preserve the minimum distance");
        }
    }
}

} // namespace

int main()
{
    verifyDeterministicAndSpaced(256.0f, 256.0f, 2.0f, 1u);
    verifyDeterministicAndSpaced(1024.0f, 512.0f, 4.0f, 0xC0FFEEu);
    return 0;
}
