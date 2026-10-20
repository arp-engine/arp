// Chebyshev upper-tail visibility weight unit test.
//
// Mirrors the shader contract from renderer/ddgi.slang and verifies that both
// DDGI sampling paths share the continuous low-visibility leak reduction.
//
// The shader is the single source of truth for the formula. This test
// re-declares the C++ port locally (no shared header exists between Slang
// and C++) and asserts its mathematical contract. If the shader formula
// changes, update this file in lockstep.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// Local C++ port of chebyshevWeight from ddgi.slang:451-459.
// Returns visibility in [0, 1]: 1.0 = unoccluded, <1.0 = occluded.
float chebyshevWeightCpp(float meanDepth, float meanDepthSq, float fragDist)
{
    if (fragDist <= meanDepth) {
        return 1.0f;
    }
    float variance = std::fmax(meanDepthSq - meanDepth * meanDepth, 0.0001f);
    float diff = fragDist - meanDepth;
    return variance / (variance + diff * diff);
}

float reduceVisibilityLeakCpp(float visibility)
{
    constexpr float threshold = 0.2f;
    if (visibility >= threshold)
        return visibility;
    const float normalized = visibility / threshold;
    return visibility * normalized * normalized;
}

std::string readText(const char* relativePath)
{
    const std::string path = std::string(ARP_SOURCE_DIR) + "/" + relativePath;
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

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
    // === Group 1: Early-out branch (fragDist <= meanDepth) ===
    // Fragment is at or in front of the mean: probe sees past it, no occlusion.
    requireNear("fragDist == meanDepth", chebyshevWeightCpp(5.0f, 25.0f, 5.0f), 1.0f);
    requireNear("fragDist < meanDepth", chebyshevWeightCpp(10.0f, 100.0f, 5.0f), 1.0f);
    requireNear("fragDist zero", chebyshevWeightCpp(1.0f, 1.0f, 0.0f), 1.0f);
    requireNear("both zero", chebyshevWeightCpp(0.0f, 0.0f, 0.0f), 1.0f);

    // === Group 2: Variance floor (epsilon = 0.0001) ===
    // When meanDepthSq - meanDepth^2 is tiny or negative (RG16F quantization,
    // hysteresis drift), variance is clamped to 0.0001 to avoid div-by-zero.
    {
        // Degenerate: meanDepthSq == meanDepth^2 (zero variance).
        // diff = 1.0, variance = max(0, 0.0001) = 0.0001
        // result = 0.0001 / (0.0001 + 1.0) = 0.0001/1.0001
        float r = chebyshevWeightCpp(5.0f, 25.0f, 6.0f);
        requireNear("zero variance floor", r, 0.0001f / 1.0001f, 0.001f);
    }
    {
        // Negative variance (RG16F hysteresis drift): meanDepthSq < meanDepth^2.
        // variance = max(-1.0, 0.0001) = 0.0001
        float r = chebyshevWeightCpp(5.0f, 24.0f, 6.0f); // 24 - 25 = -1
        requireNear("negative variance floor", r, 0.0001f / 1.0001f, 0.001f);
    }

    // === Group 3: Canonical Chebyshev bound ===
    // P_max = variance / (variance + (d - mean)^2)
    {
        // meanDepth=5, meanDepthSq=30 -> variance = 5
        // fragDist=7 -> diff=2, result = 5 / (5 + 4) = 5/9
        float r = chebyshevWeightCpp(5.0f, 30.0f, 7.0f);
        requireNear("canonical bound", r, 5.0f / 9.0f);
    }
    {
        // Larger variance -> less occlusion (closer to 1.0)
        // variance = 75, result = 75 / (75 + 4) = 75/79
        float r = chebyshevWeightCpp(5.0f, 100.0f, 7.0f);
        requireNear("large variance", r, 75.0f / 79.0f);
    }
    {
        // Small variance -> strong occlusion (closer to 0)
        // variance = 0.1, result = 0.1 / (0.1 + 4) = 0.1/4.1
        float r = chebyshevWeightCpp(5.0f, 25.1f, 7.0f);
        requireNear("small variance", r, 0.1f / 4.1f);
    }

    // === Group 4: Monotonicity (fragDist grows past mean -> visibility decreases) ===
    {
        const float meanDepth = 5.0f;
        const float meanDepthSq = 30.0f; // variance = 5
        float prev = 1.0f;               // at fragDist == meanDepth, early-out returns 1.0
        for (float d = meanDepth + 0.1f; d <= 50.0f; d += 0.5f) {
            float v = chebyshevWeightCpp(meanDepth, meanDepthSq, d);
            requireFinite("monotonicity finite", v);
            if (v > prev + 0.0001f) {
                std::printf("monotonicity violated at d=%.2f: prev=%.6f cur=%.6f\n", d, prev, v);
                std::exit(1);
            }
            prev = v;
        }
        // As fragDist -> infinity, visibility -> 0
        requireNear("far field approaches 0",
            chebyshevWeightCpp(meanDepth, meanDepthSq, 1000.0f),
            0.0f,
            0.01f);
    }

    // === Group 5: Range invariant (result in [0, 1]) ===
    {
        // Sweep plausible DDGI probe distances (0.1m to 500m) with various
        // moment configurations; all results must be in [0, 1].
        const float meanDepths[] = {0.5f, 1.0f, 5.0f, 20.0f, 100.0f, 300.0f};
        const float meanDepthSqs[] = {0.25f, 1.5f, 30.0f, 500.0f, 11000.0f, 95000.0f};
        const float fragDists[] = {0.0f, 0.1f, 1.0f, 5.0f, 10.0f, 50.0f, 200.0f, 500.0f};
        for (float md : meanDepths) {
            for (float mds : meanDepthSqs) {
                for (float fd : fragDists) {
                    float v = chebyshevWeightCpp(md, mds, fd);
                    if (!std::isfinite(v) || v < 0.0f || v > 1.0f) {
                        std::printf(
                            "range violated: md=%.2f mds=%.2f fd=%.2f -> %.6f\n", md, mds, fd, v);
                        std::exit(1);
                    }
                }
            }
        }
    }

    // === Group 6: Boundary continuity at fragDist == meanDepth ===
    {
        // Early-out branch and Chebyshev formula must agree at the boundary.
        float below = chebyshevWeightCpp(5.0f, 30.0f, 4.99f); // early-out
        float at = chebyshevWeightCpp(5.0f, 30.0f, 5.0f);     // early-out
        float above = chebyshevWeightCpp(5.0f, 30.0f, 5.01f); // Chebyshev bound
        requireNear("boundary below", below, 1.0f);
        requireNear("boundary at", at, 1.0f);
        // variance=5, diff=0.01, result = 5/(5+0.0001) = 5/5.0001
        requireNear("boundary above continuous", above, 5.0f / 5.0001f, 0.001f);
        // No discontinuity at the boundary
        if (std::fabs(at - above) > 0.01f) {
            std::printf("discontinuity at boundary: at=%.6f above=%.6f\n", at, above);
            std::exit(1);
        }
    }

    // === Group 7: Continuous low-visibility leak reduction ===
    requireNear("leak reduction zero", reduceVisibilityLeakCpp(0.0f), 0.0f);
    requireNear("leak reduction midpoint", reduceVisibilityLeakCpp(0.1f), 0.025f);
    requireNear("leak reduction threshold", reduceVisibilityLeakCpp(0.2f), 0.2f);
    requireNear("leak reduction passthrough", reduceVisibilityLeakCpp(0.75f), 0.75f);

    float previous = 0.0f;
    for (int sample = 0; sample <= 1000; ++sample) {
        const float visibility = static_cast<float>(sample) / 1000.0f;
        const float reduced = reduceVisibilityLeakCpp(visibility);
        requireFinite("leak reduction finite", reduced);
        if (reduced < previous || reduced < 0.0f || reduced > 1.0f) {
            std::printf("leak reduction invariant failed at %.6f -> %.6f\n", visibility, reduced);
            std::exit(1);
        }
        previous = reduced;
    }
    const float thresholdBelow = reduceVisibilityLeakCpp(0.2f - 1.0e-5f);
    const float thresholdAbove = reduceVisibilityLeakCpp(0.2f + 1.0e-5f);
    if (std::fabs(thresholdBelow - thresholdAbove) >= 0.0001f) {
        std::printf("leak reduction discontinuity: below=%.6f above=%.6f\n",
            thresholdBelow,
            thresholdAbove);
        std::exit(1);
    }

    const auto visibilityHelper = readText("shaders/renderer/ddgi_visibility.slang");
    const auto resolveShader = readText("shaders/renderer/ddgi.slang");
    const auto samplingShader = readText("shaders/renderer/ddgi_sampling.slang");
    if (visibilityHelper.find("visibility * normalized * normalized") == std::string::npos
        || resolveShader.find("#include \"renderer/ddgi_visibility.slang\"") == std::string::npos
        || samplingShader.find("#include \"renderer/ddgi_visibility.slang\"") == std::string::npos
        || resolveShader.find("if (ao < 0.2)") != std::string::npos
        || samplingShader.find("visibility < 0.2 ?") != std::string::npos) {
        std::printf("DDGI shaders do not share the continuous visibility helper\n");
        std::exit(1);
    }

    std::printf("ddgi_chebyshev_tests: all assertions passed\n");
    return 0;
}
