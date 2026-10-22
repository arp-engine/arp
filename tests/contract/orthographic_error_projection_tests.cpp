#include "renderer/shadows/orthographic_error_projection.h"

#include <array>
#include <cassert>
#include <cstring>
#include <random>

using Projection = arp::core::OrthographicErrorProjection;
using Matrix = std::array<float, 16>;
constexpr Matrix identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void positiveSuccessorMatchesStandard()
{
    using Rounding = arp::renderer::detail::ProjectionRounding;
    const auto check = [](double value) {
        const double expected = std::nextafter(value, std::numeric_limits<double>::infinity());
        const double actual = Rounding::upward(value);
        assert(std::memcmp(&expected, &actual, sizeof(actual)) == 0);
    };
    for (const double value : {0.0,
             -0.0,
             std::numeric_limits<double>::denorm_min(),
             std::nextafter(std::numeric_limits<double>::min(), 0.0),
             std::numeric_limits<double>::min(),
             1.0,
             std::nextafter(std::numeric_limits<double>::max(), 0.0)})
        check(value);
    std::mt19937_64 random(0x5c1ad0b7u);
    for (unsigned i = 0; i < 200000; ++i) {
        const std::uint64_t bits = random() & UINT64_C(0x7fffffffffffffff);
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        if (std::isfinite(value) && value != std::numeric_limits<double>::max())
            check(value);
    }
    bool rejected = false;
    try {
        Rounding::upward(std::numeric_limits<double>::max());
    } catch (const arp::core::DiagnosticError& error) {
        rejected = std::strstr(error.what(), "ARITHMETIC-OVERFLOW");
    }
    assert(rejected);
}

template <class Function> void rejects(Function&& function, const char* diagnostic)
{
    bool rejected = false;
    try {
        function();
    } catch (const arp::core::DiagnosticError& error) {
        rejected = std::strstr(error.what(), diagnostic) != nullptr;
    }
    assert(rejected);
}

float evaluate(const Matrix& matrix, unsigned row, const float* position, bool fused)
{
    const float* m = matrix.data() + row * 4;
    if (fused)
        return std::fma(
            m[0], position[0], std::fma(m[1], position[1], std::fma(m[2], position[2], m[3])));
    volatile float a = m[0] * position[0], b = m[1] * position[1], c = m[2] * position[2];
    volatile float ab = a + b, abc = ab + c;
    return abc + m[3];
}

void arithmeticEnclosure()
{
    const Matrix affine = {
        -2.3f, .7f, .4f, 138.4f, -.2f, 1.3f, .6f, -39.7f, .1f, -.4f, 3.1f, 201.8f, 0, 0, 0, 1};
    const Matrix vp = {.013f,
        -.018f,
        .025f,
        -.4f,
        -.021f,
        -.011f,
        .007f,
        .6f,
        .0011f,
        .0016f,
        .0007f,
        .3f,
        0,
        0,
        0,
        1};
    const double support[] = {5, 5, 5}, normals[] = {1, 1, 1};
    const auto envelope =
        Projection::worldEnvelope(affine.data(), .03125, support, normals, normals, .02f);
    const auto bound = Projection::project(vp.data(), envelope, 2048, 512);
    std::mt19937 random(0x6c0ffee);
    std::uniform_real_distribution<float> positions(-4, 4), direction(-1, 1);
    for (unsigned sample = 0; sample < 12000; ++sample) {
        float p[3], q[3], delta[3];
        double length = 0;
        for (unsigned a = 0; a < 3; ++a) {
            p[a] = positions(random);
            delta[a] = direction(random);
            length += double(delta[a]) * delta[a];
        }
        for (unsigned a = 0; a < 3; ++a)
            q[a] = p[a] + float(delta[a] * .03 / std::max(std::sqrt(length), 1e-9));
        for (bool affineFused : {false, true}) {
            float worldP[3], worldQ[3];
            for (unsigned a = 0; a < 3; ++a) {
                const float n0 = direction(random), n1 = direction(random);
                const float t0 = evaluate(affine, a, p, affineFused),
                            t1 = evaluate(affine, a, q, !affineFused);
                volatile float offset0 = .02f * n0, offset1 = .02f * n1;
                worldP[a] = t0 + offset0;
                worldQ[a] = t1 + offset1;
                assert(std::abs(double(worldP[a]) - worldQ[a]) <= envelope.deviation[a]);
                assert(std::abs(double(worldP[a])) <= envelope.absoluteSupport[a]);
                assert(std::abs(double(worldQ[a])) <= envelope.absoluteSupport[a]);
            }
            for (bool projectionFused : {false, true}) {
                const double observed[] = {std::abs(double(evaluate(vp, 0, worldP, projectionFused))
                                               - evaluate(vp, 0, worldQ, !projectionFused))
                        * 1024,
                    std::abs(double(evaluate(vp, 1, worldP, projectionFused))
                        - evaluate(vp, 1, worldQ, !projectionFused))
                        * 256,
                    std::abs(double(evaluate(vp, 2, worldP, projectionFused))
                        - evaluate(vp, 2, worldQ, !projectionFused))};
                assert(observed[0] <= bound.pixelsX && observed[1] <= bound.pixelsY
                    && observed[2] <= bound.normalizedDepth);
            }
        }
    }
}

int main()
{
    Projection::WorldEnvelope world{{.125, .25, .0625}, {5, 5, 5}};
    const auto bound = Projection::project(identity.data(), world, 200, 100);
    assert(bound.pixelsX >= 12.5 && bound.pixelsX < 12.501);
    assert(bound.pixelsY >= 12.5 && bound.pixelsY < 12.501);
    assert(bound.normalizedDepth >= .0625 && bound.normalizedDepth < .0626);
    assert(bound.fits(std::max(bound.pixelsX, bound.pixelsY), bound.normalizedDepth));
    assert(!bound.fits(
        std::nextafter(std::max(bound.pixelsX, bound.pixelsY), 0.0), bound.normalizedDepth));
    auto wider = Projection::project(identity.data(), world, 400, 100);
    assert(wider.pixelsX >= 2 * bound.pixelsX && wider.pixelsY == bound.pixelsY);
    const double support[] = {1, 1, 1}, original[] = {1, 2, 3}, candidate[] = {4, 5, 6};
    auto biased =
        Projection::worldEnvelope(identity.data(), 0, support, original, candidate, -.02f);
    assert(biased.deviation[0] >= double(.02f) * 5 && biased.deviation[2] >= double(.02f) * 9);
    Matrix perspective = identity;
    perspective[14] = -1;
    rejects(
        [&] {
            Projection::project(perspective.data(), world, 1, 1);
        },
        "AFFINE-W-ONE");
    rejects(
        [&] {
            Projection::project(nullptr, world, 1, 1);
        },
        "MATRIX-MISSING");
    rejects(
        [&] {
            Projection::project(identity.data(), world, 0, 1);
        },
        "VIEWPORT-INVALID");
    rejects(
        [&] {
            bound.fits(0, .001);
        },
        "BUDGET-INVALID");
    rejects(
        [&] {
            Projection::ProjectedError{-1, 0, 0}.fits(1, 1);
        },
        "ENVELOPE-INVALID");
    auto invalid = world;
    invalid.deviation[1] = -1;
    rejects(
        [&] {
            Projection::project(identity.data(), invalid, 1, 1);
        },
        "ENVELOPE-INVALID");
    invalid = world;
    invalid.absoluteSupport[0] = std::numeric_limits<double>::infinity();
    rejects(
        [&] {
            Projection::project(identity.data(), invalid, 1, 1);
        },
        "ENVELOPE-INVALID");
    Matrix overflow = identity;
    overflow[0] = std::numeric_limits<float>::max();
    rejects(
        [&] {
            Projection::project(overflow.data(), world, 1, 1);
        },
        "FLOAT-EVALUATION-OVERFLOW");
    Matrix nonfinite = identity;
    nonfinite[0] = std::numeric_limits<float>::quiet_NaN();
    rejects(
        [&] {
            Projection::project(nonfinite.data(), world, 1, 1);
        },
        "MATRIX-NONFINITE");
    positiveSuccessorMatchesStandard();
    arithmeticEnclosure();
}
