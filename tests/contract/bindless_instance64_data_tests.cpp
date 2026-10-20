#include "../fixtures/original_graphics_instance64_data_fixture.h"

#include <limits>

#ifdef NDEBUG
#error Instance64 tests require assertions
#endif

int main()
{
    using Data = arp::test::OriginalGraphicsInstance64DataFixture;
    const auto instances = Data::instances();
    const auto probe = Data::probe();
    const auto oracle = Data::expected();
    assert(Data::matches(oracle));
    // The sentinel cannot accidentally satisfy the nonzero-base workload.
    const auto sentinel = Data::position(instances[0], probe);
    const auto last = Data::position(instances[3], probe);
    assert(std::abs(sentinel.x - oracle[4]) > 1.f);
    assert(std::abs(last.x - oracle[0]) < Data::tolerance);
    // Check core composition against a hand-solvable right-angle transform.
    arp::pcg::InstanceDataGpu rightAngle;
    rightAngle.yawPhaseIdLod[0] = 1.5707963267948966f;
    rightAngle.extraTransform[0] = 1.5707963267948966f;
    rightAngle.extraTransform[1] = 1.5707963267948966f;
    const auto rotated = arp::core::transformVector(Data::rotation(rightAngle), {2, 3, 5});
    assert(std::abs(rotated.x + 2.f) < Data::tolerance
        && std::abs(rotated.y - 5.f) < Data::tolerance
        && std::abs(rotated.z - 3.f) < Data::tolerance);
    auto bad = oracle;
    // Ordinary arithmetic errors, dropped base and metadata conversion must fail.
    bad[4] += 0.1f;
    assert(!Data::matches(bad));
    bad = oracle;
    bad[4] = sentinel.x;
    assert(!Data::matches(bad));
    for (const auto lane : {16u, 17u, 18u, 19u}) {
        bad = oracle;
        const auto bits = arp::pcg::unpackInstanceMetadataFloat(bad[lane]);
        arp::pcg::packInstanceMetadataFloat(bad[lane], bits ^ 1u);
        assert(!Data::matches(bad));
    }
    bad = oracle;
    bad[12] = -bad[12];
    assert(!Data::matches(bad));
    for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
        const auto base = pixel * 44u;
        const auto& instance = instances[probe.selectors[0] + probe.offsets[pixel]];
        for (std::size_t lane = 0u; lane < 4u; ++lane)
            assert(arp::pcg::unpackInstanceMetadataFloat(oracle[base + 16u + lane])
                == arp::pcg::unpackInstanceMetadataFloat(instance.speciesLodState[lane]));
        const float lengthSquared = oracle[base + 8] * oracle[base + 8]
            + oracle[base + 9] * oracle[base + 9] + oracle[base + 10] * oracle[base + 10];
        assert(std::abs(lengthSquared - 1.f) < Data::tolerance);
        assert(oracle[base + 15] == -1.f);
        for (std::size_t lane = 0u; lane < 3u; ++lane) {
            const auto rest = oracle[base + 4u + lane];
            // Both profiles remain below saturation: the height ratio is 4.
            assert(std::abs((oracle[base + 28u + lane] - rest)
                       - 4.f * (oracle[base + 36u + lane] - rest))
                < 2e-5f);
            assert(oracle[base + 40u + lane] == rest);
        }
    }
    using Geometry = arp::pcg::FoliageGeometryContract;
    const auto grass = Geometry::grass(), canopy = Geometry::canopy();
    assert(!Geometry::validatePassFamily(grass, grass, grass));
    assert(!Geometry::validatePassFamily(canopy, canopy, canopy));
    assert(Geometry::validatePassFamily(grass, grass, canopy));
    for (const auto height : {0.f,
             -1.f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
             2.f}) {
        auto invalid = grass;
        invalid.heightReference = height;
        assert(Geometry::validate(invalid));
    }
    auto invalid = grass;
    invalid.contractVersion = 2u;
    assert(Geometry::validate(invalid));
    invalid = grass;
    invalid.family = static_cast<arp::pcg::FoliageGeometryFamily>(255u);
    assert(Geometry::validate(invalid));
    bad = oracle;
    bad[28] = oracle[36]; // Substituting the canopy wind must be observable.
    assert(!Data::matches(bad));
    bad = oracle;
    bad[32] = oracle[28]; // Previous and current wind are distinct inputs.
    assert(!Data::matches(bad));
    Data::verify(oracle);
    std::puts(
        "BINDLESS_INSTANCE64_DATA_OK actual-host64 fields0-16-32-48 core-oracle metadata-bit-refusals");
}
