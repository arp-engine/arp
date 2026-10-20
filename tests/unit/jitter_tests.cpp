#include "core/jitter.h"
#include "renderer/postprocess/taa_camera_history.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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

void require(bool condition, const char* message)
{
    if (!condition) {
        std::printf("ASSERT FAILED: %s\n", message);
        std::exit(1);
    }
}

// Halton (2,3) reference values for the first 4 indices, computed by hand via
// the radical-inverse formula. These pin the sequence so any future change to
// halton23() that breaks determinism is caught here rather than producing
// silently wrong jitter.
//
// index 0: x = 2*0 - 1 = -1, y = 2*0 - 1 = -1
// index 1: x = 2*0.5 - 1 = 0,  y = 2*(1/3) - 1 = -1/3
// index 2: x = 2*0.25 - 1 = -0.5, y = 2*(2/3) - 1 = 1/3
// index 3: x = 2*0.75 - 1 = 0.5,  y = 2*(1/9) - 1 = -7/9
void halton_sequence_is_deterministic_and_matches_reference()
{
    const auto v0 = arp::core::halton23(0);
    requireNear("halton[0].x", v0.x, -1.0f);
    requireNear("halton[0].y", v0.y, -1.0f);

    const auto v1 = arp::core::halton23(1);
    requireNear("halton[1].x", v1.x, 0.0f);
    requireNear("halton[1].y", v1.y, -1.0f / 3.0f);

    const auto v2 = arp::core::halton23(2);
    requireNear("halton[2].x", v2.x, -0.5f);
    requireNear("halton[2].y", v2.y, 1.0f / 3.0f);

    const auto v3 = arp::core::halton23(3);
    requireNear("halton[3].x", v3.x, 0.5f);
    requireNear("halton[3].y", v3.y, -7.0f / 9.0f);
}

// All Halton points must lie in [-1, 1]^2 - a jitter outside this range would
// sample beyond the pixel footprint and break the sub-pixel accumulation
// assumption. Checking a longer run also verifies no NaN/Inf sneaks in via the
// radical-inverse loop for larger indices.
void halton_points_stay_in_unit_range()
{
    for (std::uint32_t i = 0; i < 256u; ++i) {
        const auto v = arp::core::halton23(i);
        require(v.x >= -1.0f && v.x <= 1.0f, "halton x out of [-1,1]");
        require(v.y >= -1.0f && v.y <= 1.0f, "halton y out of [-1,1]");
        require(std::isfinite(v.x) && std::isfinite(v.y), "halton non-finite");
    }
}

// applyProjectionJitter must fold the offset into the perspective z column
// (m[2], m[6] in row-major storage) without touching the translation column.
// Constructing a known perspective matrix and applying jitter=(1,1) on a
// 2x2 target: the half-pixel NDC radius is 1/2, so m[2] and m[6] each gain
// exactly +0.5.
void jitter_folds_into_perspective_z_column_only()
{
    arp::core::Mat4 proj = arp::core::perspective(90.0f, 1.0f, 0.1f, 100.0f);
    const float m0Before = proj.m[0];
    const float m5Before = proj.m[5];
    const float m3Before = proj.m[3];
    const float m7Before = proj.m[7];
    const float m10Before = proj.m[10];
    const float m14Before = proj.m[14];

    // Jitter (+1, +1) on a 2x2 target reaches the half-pixel bound: 1/2 NDC.
    const auto jittered =
        arp::core::applyProjectionJitter(proj, arp::core::Vec2{1.0f, 1.0f}, 2.0f, 2.0f);

    requireNear("jitter m[2] += offset", jittered.m[2], proj.m[2] + 0.5f);
    requireNear("jitter m[6] += offset", jittered.m[6], proj.m[6] + 0.5f);
    // Scale + depth rows untouched.
    requireNear("jitter m[0] unchanged", jittered.m[0], m0Before);
    requireNear("jitter m[5] unchanged", jittered.m[5], m5Before);
    requireNear("jitter m[3] unchanged", jittered.m[3], m3Before);
    requireNear("jitter m[7] unchanged", jittered.m[7], m7Before);
    requireNear("jitter m[10] unchanged", jittered.m[10], m10Before);
    requireNear("jitter m[14] unchanged", jittered.m[14], m14Before);
    // Original projection is not mutated (value semantics).
    requireNear("original m[2] unchanged", proj.m[2], 0.0f);
}

// Zero jitter must be a no-op: the returned matrix equals the input. This is
// the path TAA takes when disabled or on the first frame with no jitter.
void zero_jitter_is_noop()
{
    arp::core::Mat4 proj = arp::core::perspective(60.0f, 16.0f / 9.0f, 0.5f, 500.0f);
    const auto jittered =
        arp::core::applyProjectionJitter(proj, arp::core::Vec2{0.0f, 0.0f}, 1920.0f, 1080.0f);
    for (std::uint32_t i = 0; i < 16u; ++i) {
        requireNear("zero jitter identity", jittered.m[i], proj.m[i]);
    }
}

// Zero extent must be a safe no-op (the function guards against divide-by-zero
// and leaves the matrix untouched). A renderer that calls this before the first
// resize must not crash or produce NaN.
void zero_extent_is_safe_noop()
{
    arp::core::Mat4 proj = arp::core::perspective(60.0f, 1.0f, 0.1f, 100.0f);
    const auto jittered =
        arp::core::applyProjectionJitter(proj, arp::core::Vec2{0.5f, -0.5f}, 0.0f, 0.0f);
    for (std::uint32_t i = 0; i < 16u; ++i) {
        requireNear("zero extent noop", jittered.m[i], proj.m[i]);
    }
}

// End-to-end: the same perspective jitter must produce the same NDC offset at
// different depths. A fixed translation in m[3]/m[7] passes at z=1 but shrinks
// at z=10, which makes model edges visibly change jitter amplitude with depth.
void jitter_ndc_shift_is_depth_independent()
{
    const float width = 100.0f;
    const float height = 100.0f;
    arp::core::Mat4 proj = arp::core::perspective(90.0f, 1.0f, 0.1f, 100.0f);
    // A point at +Z in view space (view = identity, left-handed +Z forward).
    // perspective() sets m[14]=1 so clip.w = m[14]*z = +z (positive, no flip).
    const auto jittered =
        arp::core::applyProjectionJitter(proj, arp::core::Vec2{1.0f, 1.0f}, width, height);

    const arp::core::Vec3 nearPoint{0.0f, 0.0f, 1.0f};
    const arp::core::Vec3 farPoint{0.0f, 0.0f, 10.0f};
    const auto nearBefore = arp::core::transformPoint(proj, nearPoint);
    const auto nearAfter = arp::core::transformPoint(jittered, nearPoint);
    const auto farBefore = arp::core::transformPoint(proj, farPoint);
    const auto farAfter = arp::core::transformPoint(jittered, farPoint);

    const float nearShiftX = nearAfter.x - nearBefore.x;
    const float nearShiftY = nearAfter.y - nearBefore.y;
    const float farShiftX = farAfter.x - farBefore.x;
    const float farShiftY = farAfter.y - farBefore.y;
    requireNear("near NDC x shifted", nearShiftX, 0.01f);
    requireNear("near NDC y shifted", nearShiftY, 0.01f);
    requireNear("far NDC x shifted", farShiftX, 0.01f);
    requireNear("far NDC y shifted", farShiftY, 0.01f);
    requireNear("NDC x shift depth independent", farShiftX, nearShiftX);
    requireNear("NDC y shift depth independent", farShiftY, nearShiftY);
    requireNear("near depth unchanged", nearAfter.z, nearBefore.z);
    requireNear("far depth unchanged", farAfter.z, farBefore.z);
}

// The production sequence must never move a raster sample farther than half a
// pixel from the pixel center. This guards the contract between halton23(),
// which returns [-1, 1), and applyProjectionJitter(), which converts that
// normalized sample into NDC.
void halton_jitter_stays_within_half_pixel_radius()
{
    constexpr float width = 1920.0f;
    constexpr float height = 1080.0f;
    const auto projection = arp::core::perspective(60.0f, width / height, 0.1f, 1000.0f);
    const arp::core::Vec3 viewPoint{0.0f, 0.0f, 10.0f};
    const auto ndcBefore = arp::core::transformPoint(projection, viewPoint);

    for (std::uint32_t frame = 0u; frame < 16u; ++frame) {
        const auto jittered =
            arp::core::applyProjectionJitter(projection, arp::core::halton23(frame), width, height);
        const auto ndcAfter = arp::core::transformPoint(jittered, viewPoint);
        const float pixelOffsetX = (ndcAfter.x - ndcBefore.x) * width * 0.5f;
        const float pixelOffsetY = (ndcAfter.y - ndcBefore.y) * height * 0.5f;

        require(std::isfinite(pixelOffsetX) && std::isfinite(pixelOffsetY),
            "jitter pixel offset non-finite");
        require(std::fabs(pixelOffsetX) <= 0.5001f, "jitter x exceeds half-pixel radius");
        require(std::fabs(pixelOffsetY) <= 0.5001f, "jitter y exceeds half-pixel radius");
    }
}

// TaaCameraHistory: freshly constructed history reports uninitialized; after
// advanceTaaCameraHistory it carries the current VP as previous and marks
// itself initialized; resetTaaCameraHistory flips back to uninitialized.
void camera_history_advance_and_reset()
{
    arp::renderer::TaaCameraHistory history;
    require(!history.initialized, "fresh history should be uninitialized");

    arp::core::Mat4 cur;
    cur.m = {2.0f, 0, 0, 0, 0, 2.0f, 0, 0, 0, 0, 2.0f, 0, 1, 2, 3, 1};
    arp::renderer::advanceTaaCameraHistory(history, cur);
    require(history.initialized, "history should be initialized after advance");
    for (std::uint32_t i = 0; i < 16u; ++i) {
        requireNear("history prevViewProjection matches current",
            history.prevViewProjection.m[i],
            cur.m[i]);
    }

    arp::renderer::resetTaaCameraHistory(history);
    require(!history.initialized, "history should be uninitialized after reset");
    // After reset, prev matrices revert to identity.
    for (std::uint32_t i = 0; i < 16u; ++i) {
        const float identityExpected = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0f : 0.0f;
        requireNear("history prevViewProjection identity after reset",
            history.prevViewProjection.m[i],
            identityExpected);
    }
}

} // namespace

int main()
{
    halton_sequence_is_deterministic_and_matches_reference();
    halton_points_stay_in_unit_range();
    jitter_folds_into_perspective_z_column_only();
    zero_jitter_is_noop();
    zero_extent_is_safe_noop();
    jitter_ndc_shift_is_depth_independent();
    halton_jitter_stays_within_half_pixel_radius();
    camera_history_advance_and_reset();
    return 0;
}
