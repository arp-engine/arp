#include "renderer/geometry/perspective_surface_error_projection.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"

#include <cassert>
#include <random>

using Projection = arp::renderer::PerspectiveSurfaceErrorProjection;
using Matrix = std::array<float, 16>;

namespace {
constexpr Matrix camera{1.3f, .1f, .2f, -.3f, -.1f, 1.7f, .05f, .2f, 0, 0, .9f, -.1f, 0, 0, 1, 0};

template <class Function> void rejects(Function&& function)
{
    bool rejected = false;
    try {
        function();
    } catch (const arp::core::DiagnosticError&) {
        rejected = true;
    }
    assert(rejected);
}

std::array<double, 3> project(const Matrix& matrix, const std::array<double, 3>& position)
{
    std::array<double, 4> clip{};
    for (unsigned row = 0; row < 4; ++row) {
        clip[row] = matrix[row * 4 + 3];
        for (unsigned axis = 0; axis < 3; ++axis)
            clip[row] += double(matrix[row * 4 + axis]) * position[axis];
    }
    return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
}
} // namespace

int main()
{
    Projection::Envelope world{{-3, -2, 5}, {3, 2, 12}, {.025, .04, .03}};
    const auto error = Projection::project(camera.data(), world, 2560, 1440);
    assert(error.bounded && error.pixelsX > 0 && error.pixelsY > 0 && error.normalizedDepth > 0);
    // Independent sampled reference includes off-axis perspective magnification,
    // asymmetric projection, shear, and depth deviation. No shader/GPU claim.
    std::mt19937_64 random(0x20261027);
    std::uniform_real_distribution<double> unit(0, 1);
    for (unsigned sample = 0; sample < 100000; ++sample) {
        std::array<double, 3> a{}, b{};
        for (unsigned axis = 0; axis < 3; ++axis) {
            const double minimum = world.minimum[axis], maximum = world.maximum[axis];
            const double delta = world.deviation[axis];
            a[axis] = minimum + delta + (maximum - minimum - 2 * delta) * unit(random);
            b[axis] = a[axis] + (2 * unit(random) - 1) * delta;
        }
        const auto x = project(camera, a), y = project(camera, b);
        assert(std::abs(x[0] - y[0]) * 1280 <= error.pixelsX);
        assert(std::abs(x[1] - y[1]) * 720 <= error.pixelsY);
        assert(std::abs(x[2] - y[2]) <= error.normalizedDepth);
    }
    const auto small = Projection::project(camera.data(), world, 1280, 720);
    assert(small.pixelsX * 2 == error.pixelsX && small.pixelsY * 2 == error.pixelsY);
    auto farther = world;
    farther.minimum[2] += 100;
    farther.maximum[2] += 100;
    const auto distant = Projection::project(camera.data(), farther, 2560, 1440);
    assert(distant.pixelsX < error.pixelsX && distant.pixelsY < error.pixelsY);
    auto eye = world;
    eye.minimum[2] = -1;
    assert(!Projection::project(camera.data(), eye, 2560, 1440).bounded);
    auto near = world;
    near.minimum[2] = .05f;
    assert(!Projection::project(camera.data(), near, 2560, 1440).bounded);
    const Projection::Error fits{true, .25, .4, .00001};
    const Projection::Error misses{true, 2, .4, .00001};
    assert(Projection::select(
               std::array<Projection::Error, 3>{Projection::Error{}, fits, misses}, 1, .001)
        == 1);
    assert(Projection::select(
               std::array<Projection::Error, 3>{Projection::Error{}, misses, fits}, 1, .001)
        == 2);
    assert(Projection::select(std::array<Projection::Error, 3>{}, 1, .001) == 0);
    rejects([&] {
        Projection::select(std::array<Projection::Error, 3>{}, 0, .001);
    });
    rejects([&] {
        Projection::project(nullptr, world, 2560, 1440);
    });
    rejects([&] {
        Projection::project(camera.data(), world, 0, 1440);
    });
    auto invalid = world;
    invalid.minimum[0] = invalid.maximum[0] + 1;
    rejects([&] {
        Projection::project(camera.data(), invalid, 2560, 1440);
    });
    invalid = world;
    invalid.deviation[0] = -1;
    rejects([&] {
        Projection::project(camera.data(), invalid, 2560, 1440);
    });
    auto matrix = camera;
    matrix[0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] {
        Projection::project(matrix.data(), world, 2560, 1440);
    });
    auto device = arp::rhi::createNullDevice();
    arp::rhi::NullHiddenFrameSmokeRunner runner(*device);
    const auto smoke = runner.run({});
    assert(smoke.success && smoke.submittedFrameCount == 2);
}
