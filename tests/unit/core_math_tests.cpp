#include "core/int32_divisor.h"
#include "core/math.h"
#include "core/mesh_translation.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace {

bool near(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

void requireNear(const char* label, float lhs, float rhs, float epsilon = 0.0001f)
{
    if (!near(lhs, rhs, epsilon)) {
        std::printf("%s expected %.6f got %.6f\n", label, rhs, lhs);
        std::exit(1);
    }
}

void requireFinite(const char* label, float value)
{
    if (!std::isfinite(value)) {
        std::printf("%s expected finite got %.6f\n", label, value);
        std::exit(1);
    }
}

} // namespace

int main()
{
    const std::int32_t divisionEdges[]{std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
        -1000000007,
        -65537,
        -7,
        -3,
        -2,
        -1,
        0,
        1,
        2,
        3,
        7,
        65537,
        1000000007};
    std::mt19937 divisionRandom(20261014);
    const auto checkDivision = [](std::int32_t numerator, std::int32_t denominator) {
        const arp::core::Int32Divisor plan(denominator);
        if (denominator == 0) {
            if (plan.valid())
                std::exit(1);
            return;
        }
        const std::int64_t n = numerator, d = denominator;
        if (!plan.valid() || plan.quotient(numerator) != n / d
            || plan.remainder(numerator) != n % d) {
            std::printf("fixed division mismatch: %d / %d\n", numerator, denominator);
            std::exit(1);
        }
    };
    for (auto denominator : divisionEdges) {
        for (auto numerator : divisionEdges)
            checkDivision(numerator, denominator);
        for (int pass = 0; pass != 10000; ++pass)
            checkDivision(static_cast<std::int32_t>(static_cast<std::int64_t>(divisionRandom())
                              + std::numeric_limits<std::int32_t>::min()),
                denominator);
    }
    for (int pass = 0; pass != 100000; ++pass) {
        const auto numerator = static_cast<std::int32_t>(
            static_cast<std::int64_t>(divisionRandom()) + std::numeric_limits<std::int32_t>::min());
        const auto denominator = static_cast<std::int32_t>(
            static_cast<std::int64_t>(divisionRandom()) + std::numeric_limits<std::int32_t>::min());
        checkDivision(numerator, denominator);
    }

    struct TestVertex {
        float position[3];
        float normal[3];
    };

    struct TestMesh {
        std::array<TestVertex, 2> vertices;
    };

    const TestMesh local{std::array<TestVertex, 2>{
        TestVertex{{-1, 0, 1}, {0, 1, 0}}, TestVertex{{1, 2, -1}, {0, 1, 0}}}};
    auto raster = local;
    arp::core::MeshTranslation::apply(raster, {62, 3, -85});
    requireNear("marker world x", raster.vertices[0].position[0], 61);
    requireNear("marker world z", raster.vertices[1].position[2], -86);
    requireNear("local bounds preserved", local.vertices[0].position[0], -1);
    requireNear("normal preserved", raster.vertices[0].normal[1], 1);
    bool rejected = false;
    try {
        arp::core::MeshTranslation::apply(raster, {NAN, 0, 0});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected)
        return 1;
    requireNear("invalid placement leaves staging intact", raster.vertices[0].position[0], 61);
    // An asymmetric point must keep its screen side; both view conventions
    // must still map near/far to the same [0,1] depth interval.
    const auto rhView = arp::core::lookAtRightHanded({0, 0, 10}, {0, 0, 0});
    const auto lhView = arp::core::lookAt({0, 0, 10}, {0, 0, 0});
    const auto rhProjection = arp::core::perspectiveRightHanded(60, 1.5f, 0.1f, 100);
    const auto lhProjection = arp::core::perspective(60, 1.5f, 0.1f, 100);
    const auto rhVp = arp::core::multiply(rhProjection, rhView);
    const auto lhVp = arp::core::multiply(lhProjection, lhView);
    const auto rhPoint = arp::core::transformPoint(rhVp, {2, 1, 0});
    const auto lhPoint = arp::core::transformPoint(lhVp, {2, 1, 0});
    requireNear("right-hand screen x", rhPoint.x, -lhPoint.x);
    requireNear("handedness screen y", rhPoint.y, lhPoint.y);
    requireNear("handedness depth", rhPoint.z, lhPoint.z);
    requireNear("RH view right", arp::core::transformPoint(rhView, {2, 1, 0}).x, 2);
    for (const auto projection :
        {rhProjection, arp::core::orthographicRightHanded(10, 10, 0.1f, 100)}) {
        requireNear("RH near depth", arp::core::transformPoint(projection, {0, 0, -0.1f}).z, 0);
        requireNear("RH far depth", arp::core::transformPoint(projection, {0, 0, -100}).z, 1);
    }
    const arp::core::Vec2 uvA{0.25f, 0.5f};
    const arp::core::Vec2 uvB{0.75f, -0.25f};
    const auto uvSum = uvA + uvB;
    requireNear("Vec2 add x", uvSum.x, 1.0f);
    requireNear("Vec2 add y", uvSum.y, 0.25f);
    requireNear("Vec2 dot", arp::core::dot(uvA, uvB), 0.0625f);

    const arp::core::Vec3 xAxis{1.0f, 0.0f, 0.0f};
    const arp::core::Vec3 yAxis{0.0f, 1.0f, 0.0f};
    const auto zAxis = arp::core::cross(xAxis, yAxis);
    requireNear("Vec3 cross x", zAxis.x, 0.0f);
    requireNear("Vec3 cross y", zAxis.y, 0.0f);
    requireNear("Vec3 cross z", zAxis.z, 1.0f);
    requireNear("Vec3 dot", arp::core::dot(xAxis, yAxis), 0.0f);

    const auto normalizedZero = arp::core::normalize(arp::core::Vec3{});
    requireNear("Vec3 normalize zero x", normalizedZero.x, 0.0f);
    requireNear("Vec3 normalize zero y", normalizedZero.y, 0.0f);
    requireNear("Vec3 normalize zero z", normalizedZero.z, 1.0f);
    const auto normalizedFallback =
        arp::core::normalizeOr(arp::core::Vec3{}, arp::core::Vec3{1.0f, 0.0f, 0.0f});
    requireNear("Vec3 normalize fallback x", normalizedFallback.x, 1.0f);
    requireNear("Vec3 normalize fallback y", normalizedFallback.y, 0.0f);
    requireNear("Vec3 normalize fallback z", normalizedFallback.z, 0.0f);

    const arp::core::Vec4 color{0.1f, 0.2f, 0.3f, 0.4f};
    const auto boosted = arp::core::scale(color, 2.0f);
    requireNear("Vec4 scale x", boosted.x, 0.2f);
    requireNear("Vec4 scale y", boosted.y, 0.4f);
    requireNear("Vec4 scale z", boosted.z, 0.6f);
    requireNear("Vec4 scale w", boosted.w, 0.8f);

    const auto identityPoint =
        arp::core::transformPoint(arp::core::Mat4::identity(), arp::core::Vec3{2.0f, 3.0f, 4.0f});
    requireNear("Mat4 identity point x", identityPoint.x, 2.0f);
    requireNear("Mat4 identity point y", identityPoint.y, 3.0f);
    requireNear("Mat4 identity point z", identityPoint.z, 4.0f);

    const auto homogeneousMatrix = arp::core::Mat4::translation({5.0f, -2.0f, 1.0f});
    const auto homogeneousPoint = arp::core::transformHomogeneous(
        homogeneousMatrix, arp::core::Vec4{2.0f, 3.0f, 4.0f, 1.0f});
    requireNear("Mat4 homogeneous point x", homogeneousPoint.x, 7.0f);
    requireNear("Mat4 homogeneous point y", homogeneousPoint.y, 1.0f);
    requireNear("Mat4 homogeneous point z", homogeneousPoint.z, 5.0f);
    requireNear("Mat4 homogeneous point w", homogeneousPoint.w, 1.0f);
    const auto homogeneousVector = arp::core::transformHomogeneous(
        homogeneousMatrix, arp::core::Vec4{2.0f, 3.0f, 4.0f, 0.0f});
    requireNear("Mat4 homogeneous vector x", homogeneousVector.x, 2.0f);
    requireNear("Mat4 homogeneous vector y", homogeneousVector.y, 3.0f);
    requireNear("Mat4 homogeneous vector z", homogeneousVector.z, 4.0f);
    requireNear("Mat4 homogeneous vector w", homogeneousVector.w, 0.0f);

    const auto translatedPoint = arp::core::transformPoint(
        arp::core::Mat4::translation({5.0f, -2.0f, 1.0f}), arp::core::Vec3{2.0f, 3.0f, 4.0f});
    requireNear("Mat4 translation point x", translatedPoint.x, 7.0f);
    requireNear("Mat4 translation point y", translatedPoint.y, 1.0f);
    requireNear("Mat4 translation point z", translatedPoint.z, 5.0f);

    const auto scaledVector = arp::core::transformVector(
        arp::core::Mat4::scale({2.0f, 3.0f, 4.0f}), arp::core::Vec3{2.0f, 3.0f, 4.0f});
    requireNear("Mat4 scale vector x", scaledVector.x, 4.0f);
    requireNear("Mat4 scale vector y", scaledVector.y, 9.0f);
    requireNear("Mat4 scale vector z", scaledVector.z, 16.0f);

    const auto composed = arp::core::multiply(arp::core::Mat4::translation({1.0f, 2.0f, 3.0f}),
        arp::core::Mat4::scale({2.0f, 2.0f, 2.0f}));
    const auto composedPoint = arp::core::transformPoint(composed, {3.0f, 4.0f, 5.0f});
    requireNear("Mat4 composed point x", composedPoint.x, 7.0f);
    requireNear("Mat4 composed point y", composedPoint.y, 10.0f);
    requireNear("Mat4 composed point z", composedPoint.z, 13.0f);

    const auto quarterTurn =
        arp::core::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 3.1415926535f * 0.5f);
    const auto rotated = arp::core::rotate(quarterTurn, xAxis);
    requireNear("Quat rotate x", rotated.x, 0.0f);
    requireNear("Quat rotate y", rotated.y, 1.0f);
    requireNear("Quat rotate z", rotated.z, 0.0f);
    const auto trs = arp::core::multiply(arp::core::Mat4::translation({5.0f, -2.0f, 1.0f}),
        arp::core::multiply(
            arp::core::Mat4::rotation(quarterTurn), arp::core::Mat4::scale({2.0f, 3.0f, 4.0f})));
    const auto transformedTrsPoint = arp::core::transformPoint(trs, {1.0f, 0.0f, 0.0f});
    requireNear("Mat4 TRS rotated/scaled point x", transformedTrsPoint.x, 5.0f);
    requireNear("Mat4 TRS rotated/scaled point y", transformedTrsPoint.y, 0.0f);
    requireNear("Mat4 TRS rotated/scaled point z", transformedTrsPoint.z, 1.0f);
    const auto normalizedQuat = arp::core::normalize(arp::core::Quat{0.0f, 0.0f, 0.0f, 2.0f});
    requireNear("Quat normalize x", normalizedQuat.x, 0.0f);
    requireNear("Quat normalize y", normalizedQuat.y, 0.0f);
    requireNear("Quat normalize z", normalizedQuat.z, 0.0f);
    requireNear("Quat normalize w", normalizedQuat.w, 1.0f);

    arp::core::Camera camera;
    camera.position = arp::core::Vec3{0.0f, 0.0f, -5.0f};
    camera.setPerspective(70.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    const auto view = arp::core::lookAt(camera.position, {});
    const auto projection = arp::core::perspective(
        camera.verticalFovDegrees, camera.aspectRatio, camera.nearPlane, camera.farPlane);
    const auto clip =
        arp::core::transformPoint(arp::core::multiply(projection, view), arp::core::Vec3{});
    requireFinite("Camera clip x", clip.x);
    requireFinite("Camera clip y", clip.y);
    requireFinite("Camera clip z", clip.z);

    // inverse: M * inverse(M) == identity (Gauss-Jordan with partial pivot).
    // Validates the row-major 4x4 inverse used by DDGI resolve to reconstruct
    // world position from clip space. Tests perspective*view (the actual
    // camera matrix shape) and a known translation/scale composite.
    const auto viewProj = arp::core::multiply(projection, view);
    const auto invViewProj = arp::core::inverse(viewProj);
    const auto roundTrip = arp::core::multiply(viewProj, invViewProj);
    requireNear("Mat4 inverse round-trip m0", roundTrip.m[0], 1.0f, 0.001f);
    requireNear("Mat4 inverse round-trip m5", roundTrip.m[5], 1.0f, 0.001f);
    requireNear("Mat4 inverse round-trip m10", roundTrip.m[10], 1.0f, 0.001f);
    requireNear("Mat4 inverse round-trip m15", roundTrip.m[15], 1.0f, 0.001f);
    requireNear("Mat4 inverse round-trip off-diag m1", roundTrip.m[1], 0.0f, 0.001f);
    requireNear("Mat4 inverse round-trip off-diag m3", roundTrip.m[3], 0.0f, 0.001f);
    requireNear("Mat4 inverse round-trip off-diag m7", roundTrip.m[7], 0.0f, 0.001f);
    requireNear("Mat4 inverse round-trip off-diag m11", roundTrip.m[11], 0.0f, 0.001f);

    // World->clip->world position reconstruction through inverse (the DDGI
    // resolve path): a known world point, transformed by viewProj to clip,
    // then by invViewProj back to world, must recover the original point.
    const arp::core::Vec3 worldPoint{3.0f, 5.0f, -7.0f};
    const auto clipFromWorld = arp::core::transformPoint(viewProj, worldPoint);
    const auto worldRecovered = arp::core::transformPoint(invViewProj, clipFromWorld);
    requireNear("Mat4 inverse world recovery x", worldRecovered.x, worldPoint.x, 0.01f);
    requireNear("Mat4 inverse world recovery y", worldRecovered.y, worldPoint.y, 0.01f);
    requireNear("Mat4 inverse world recovery z", worldRecovered.z, worldPoint.z, 0.01f);

    // Translation+scale composite inverse (non-degenerate, easy to reason about).
    const auto ts = arp::core::multiply(arp::core::Mat4::translation({2.0f, 4.0f, 6.0f}),
        arp::core::Mat4::scale({2.0f, 4.0f, 8.0f}));
    const auto invTs = arp::core::inverse(ts);
    const auto tsPoint = arp::core::transformPoint(invTs, arp::core::Vec3{6.0f, 20.0f, 70.0f});
    requireNear("Mat4 inverse translate+scale x", tsPoint.x, 2.0f, 0.001f);
    requireNear("Mat4 inverse translate+scale y", tsPoint.y, 4.0f, 0.001f);
    requireNear("Mat4 inverse translate+scale z", tsPoint.z, 8.0f, 0.001f);

    // Singular matrix returns identity (no NaN/Inf).
    arp::core::Mat4 singular{};
    singular.m[0] = 1.0f;
    singular.m[1] = 2.0f;
    singular.m[2] = 3.0f;
    singular.m[3] = 0.0f;
    singular.m[4] = 0.0f;
    singular.m[5] = 0.0f;
    singular.m[6] = 0.0f;
    singular.m[7] = 0.0f;
    singular.m[8] = 0.0f;
    singular.m[9] = 0.0f;
    singular.m[10] = 0.0f;
    singular.m[11] = 0.0f;
    singular.m[12] = 0.0f;
    singular.m[13] = 0.0f;
    singular.m[14] = 0.0f;
    singular.m[15] = 1.0f;
    const auto invSingular = arp::core::inverse(singular);
    requireNear("Mat4 inverse singular returns identity m0", invSingular.m[0], 1.0f);
    requireNear("Mat4 inverse singular returns identity m5", invSingular.m[5], 1.0f);
    requireNear("Mat4 inverse singular returns identity m10", invSingular.m[10], 1.0f);
    requireNear("Mat4 inverse singular returns identity m15", invSingular.m[15], 1.0f);

    // -- Vec3 lerp / distance (IK & blend primitives) --
    const arp::core::Vec3 lerpA{0.0f, 10.0f, -4.0f};
    const arp::core::Vec3 lerpB{10.0f, -10.0f, 6.0f};
    const auto lerpStart = arp::core::lerp(lerpA, lerpB, 0.0f);
    requireNear("Vec3 lerp t=0 x", lerpStart.x, 0.0f);
    requireNear("Vec3 lerp t=0 y", lerpStart.y, 10.0f);
    requireNear("Vec3 lerp t=0 z", lerpStart.z, -4.0f);
    const auto lerpEnd = arp::core::lerp(lerpA, lerpB, 1.0f);
    requireNear("Vec3 lerp t=1 x", lerpEnd.x, 10.0f);
    requireNear("Vec3 lerp t=1 y", lerpEnd.y, -10.0f);
    requireNear("Vec3 lerp t=1 z", lerpEnd.z, 6.0f);
    const auto lerpMid = arp::core::lerp(lerpA, lerpB, 0.5f);
    requireNear("Vec3 lerp t=0.5 x", lerpMid.x, 5.0f);
    requireNear("Vec3 lerp t=0.5 y", lerpMid.y, 0.0f);
    requireNear("Vec3 lerp t=0.5 z", lerpMid.z, 1.0f);
    // t is clamped to [0,1].
    const auto lerpUnder = arp::core::lerp(lerpA, lerpB, -2.0f);
    requireNear("Vec3 lerp clamp t<0 x", lerpUnder.x, 0.0f);
    const auto lerpOver = arp::core::lerp(lerpA, lerpB, 5.0f);
    requireNear("Vec3 lerp clamp t>1 x", lerpOver.x, 10.0f);
    requireNear("Vec3 distance", arp::core::distance(lerpA, lerpB), 24.494897f, 0.001f);

    // -- quatSlerp --
    const auto slerpIdent = arp::core::Quat::identity();
    const auto slerp90 = arp::core::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 3.14159265f * 0.5f);
    // Endpoints: t=0 -> a, t=1 -> b.
    const auto s0 = arp::core::quatSlerp(slerpIdent, slerp90, 0.0f);
    requireNear("quatSlerp t=0 w", s0.w, slerpIdent.w, 0.0001f);
    requireNear("quatSlerp t=0 x", s0.x, slerpIdent.x, 0.0001f);
    const auto s1 = arp::core::quatSlerp(slerpIdent, slerp90, 1.0f);
    requireNear("quatSlerp t=1 w", s1.w, slerp90.w, 0.0001f);
    requireNear("quatSlerp t=1 y", s1.y, slerp90.y, 0.0001f);
    // Midpoint: 45 degrees about Y.
    const auto sMid = arp::core::quatSlerp(slerpIdent, slerp90, 0.5f);
    const auto slerp45 = arp::core::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 3.14159265f * 0.25f);
    requireNear("quatSlerp t=0.5 w", sMid.w, slerp45.w, 0.0001f);
    requireNear("quatSlerp t=0.5 y", sMid.y, slerp45.y, 0.0001f);
    // Shortest-arc: negating b must give the same result (path stays short).
    const arp::core::Quat negB{-slerp90.x, -slerp90.y, -slerp90.z, -slerp90.w};
    const auto sNeg = arp::core::quatSlerp(slerpIdent, negB, 0.5f);
    // Rotating by sMid then by sNeg inverse should be ~identity (same orientation).
    const auto sMidRot = arp::core::rotate(sMid, arp::core::Vec3{0.0f, 0.0f, 1.0f});
    const auto sNegRot = arp::core::rotate(sNeg, arp::core::Vec3{0.0f, 0.0f, 1.0f});
    requireNear("quatSlerp shortest-arc x", sMidRot.x, sNegRot.x, 0.0001f);
    requireNear("quatSlerp shortest-arc z", sMidRot.z, sNegRot.z, 0.0001f);
    // Same-input -> same output.
    const auto sSame = arp::core::quatSlerp(slerpIdent, slerpIdent, 0.3f);
    requireNear("quatSlerp same w", sSame.w, 1.0f, 0.0001f);

    // -- quatFromToRotation --
    const arp::core::Vec3 fromAxis{1.0f, 0.0f, 0.0f};
    const arp::core::Vec3 toAxis{0.0f, 1.0f, 0.0f};
    const auto f2t = arp::core::quatFromToRotation(fromAxis, toAxis);
    // Rotating `from` by result must land on `to`.
    const auto f2tResult = arp::core::rotate(f2t, fromAxis);
    requireNear("quatFromTo x->y x", f2tResult.x, 0.0f, 0.0001f);
    requireNear("quatFromTo x->y y", f2tResult.y, 1.0f, 0.0001f);
    requireNear("quatFromTo x->y z", f2tResult.z, 0.0f, 0.0001f);
    // Parallel vectors -> identity.
    const auto f2tParallel = arp::core::quatFromToRotation(fromAxis, fromAxis);
    requireNear("quatFromTo parallel w", f2tParallel.w, 1.0f, 0.0001f);
    // Anti-parallel -> 180-degree rotation about an orthogonal axis; result
    // maps `from` to `-from`.
    const auto f2tAnti =
        arp::core::quatFromToRotation(fromAxis, arp::core::Vec3{-1.0f, 0.0f, 0.0f});
    const auto f2tAntiResult = arp::core::rotate(f2tAnti, fromAxis);
    requireNear("quatFromTo anti-parallel x", f2tAntiResult.x, -1.0f, 0.0001f);
    requireFinite("quatFromTo anti-parallel w", f2tAnti.w);

    // -- quatLookAt --
    // forward = +Z, up = +Y should yield identity (local +Z already aligned).
    const auto lookIdent = arp::core::quatLookAt({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    requireNear("quatLookAt +Z identity w", lookIdent.w, 1.0f, 0.001f);
    requireNear("quatLookAt +Z identity x", lookIdent.x, 0.0f, 0.001f);
    // forward = +X: a +90-degree rotation about Y (local +Z -> +X).
    const auto lookX = arp::core::quatLookAt({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const auto lookXResult = arp::core::rotate(lookX, {0.0f, 0.0f, 1.0f});
    requireNear("quatLookAt +X x", lookXResult.x, 1.0f, 0.001f);
    requireNear("quatLookAt +X z", lookXResult.z, 0.0f, 0.001f);

    return 0;
}
