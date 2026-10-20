// Implementation of the procedural canvas textures (see header).
// Phase 1a rewrite — replaces random-ellipse generators with multi-layer
// Simplex / Voronoi / DomainWarp noise for natural, structured textures.
#include "pcg/procedural_textures.h"

#include "pcg/domain_warp.h"
#include "pcg/simplex_noise.h"
#include "pcg/voronoi_noise.h"

#include <algorithm>
#include <cmath>

namespace arp::core::pgtex {

namespace {
inline int wrap(int v, int m)
{
    v %= m;
    if (v < 0)
        v += m;
    return v;
}

inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline uint8_t toByte(float v)
{
    return static_cast<uint8_t>(clampf(v, 0.0f, 255.0f));
}

// ─── Noise-backed pixel helpers ─────────────────────────────────────────
// Paint a noise value (sample 0..1) into a canvas pixel with given base
// colour and alpha.  `r`, `g`, `b` are the base; the noise value modulates
// brightness around the base.

inline void paintNoise(
    Canvas& c, int x, int y, float val, uint8_t br, uint8_t bg, uint8_t bb, uint8_t ba)
{
    // val in [0,1] → brightness factor ~0.8..1.2 around base
    float f = 0.8f + val * 0.4f;
    c.setPixel(x,
        y,
        toByte(static_cast<float>(br) * f),
        toByte(static_cast<float>(bg) * f),
        toByte(static_cast<float>(bb) * f),
        ba);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Canvas (unchanged infrastructure)
// ---------------------------------------------------------------------------
void Canvas::init(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    width = w;
    height = h;
    pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    fill(r, g, b, a);
}

void Canvas::setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0 || y < 0 || x >= width || y >= height)
        return;
    const size_t i =
        (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
    const float sa = a / 255.0f;
    const float da = pixels[i + 3] / 255.0f;
    const float oa = sa + da * (1.0f - sa);
    if (oa <= 0.0f)
        return;
    pixels[i + 0] = toByte((r * sa + pixels[i + 0] * da * (1.0f - sa)) / oa);
    pixels[i + 1] = toByte((g * sa + pixels[i + 1] * da * (1.0f - sa)) / oa);
    pixels[i + 2] = toByte((b * sa + pixels[i + 2] * da * (1.0f - sa)) / oa);
    pixels[i + 3] = toByte(oa * 255.0f);
}

void Canvas::getPixel(int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const
{
    x = wrap(x, width);
    y = wrap(y, height);
    const size_t i =
        (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
    r = pixels[i + 0];
    g = pixels[i + 1];
    b = pixels[i + 2];
    a = pixels[i + 3];
}

void Canvas::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            setPixel(x, y, r, g, b, a);
}

void Canvas::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const int x1 = clampi(x, 0, width), y1 = clampi(y, 0, height);
    const int x2 = clampi(x + w, 0, width), y2 = clampi(y + h, 0, height);
    for (int yy = y1; yy < y2; ++yy)
        for (int xx = x1; xx < x2; ++xx)
            setPixel(xx, yy, r, g, b, a);
}

void Canvas::fillEllipse(int cx, int cy, int rx, int ry, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (rx <= 0)
        rx = 1;
    if (ry <= 0)
        ry = 1;
    const int x0 = clampi(cx - rx, 0, width), y0 = clampi(cy - ry, 0, height);
    const int x1 = clampi(cx + rx + 1, 0, width), y1 = clampi(cy + ry + 1, 0, height);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            const float dx = (xx - cx) / static_cast<float>(rx);
            const float dy = (yy - cy) / static_cast<float>(ry);
            if (dx * dx + dy * dy <= 1.0f)
                setPixel(xx, yy, r, g, b, a);
        }
}

void Canvas::strokeLine(
    int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float w)
{
    const float dx = static_cast<float>(x1 - x0), dy = static_cast<float>(y1 - y0);
    const float len = std::sqrt(dx * dx + dy * dy);
    const int steps = std::max(1, static_cast<int>(len * 2.0f));
    const float hw = w * 0.5f;
    for (int s = 0; s <= steps; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(steps);
        const float px = x0 + dx * t, py = y0 + dy * t;
        const int ww = static_cast<int>(std::ceil(hw));
        for (int oy = -ww; oy <= ww; ++oy)
            for (int ox = -ww; ox <= ww; ++ox)
                if (ox * ox + oy * oy <= hw * hw + 1)
                    setPixel(static_cast<int>(std::round(px)) + ox,
                        static_cast<int>(std::round(py)) + oy,
                        r,
                        g,
                        b,
                        a);
    }
}

void Canvas::drawWrapped(const std::function<void(int, int)>& fn)
{
    for (int oy : {-height, 0, height})
        for (int ox : {-width, 0, width})
            fn(ox, oy);
}

// ---------------------------------------------------------------------------
// PRNG + heightToNormal (unchanged)
// ---------------------------------------------------------------------------
float TextureRng::next()
{
    state_ = (state_ * 1103515245u + 12345u) & 0x7fffffff;
    return static_cast<float>(state_) / static_cast<float>(0x7fffffff);
}

Canvas heightToNormal(const Canvas& height, float strength)
{
    Canvas out;
    out.init(height.width, height.height, 128, 128, 255, 255);
    const int s = height.width;
    auto h = [&](int x, int y) -> float {
        uint8_t r, g, b, a;
        height.getPixel(x, y, r, g, b, a);
        return r / 255.0f;
    };
    for (int y = 0; y < height.height; ++y)
        for (int x = 0; x < s; ++x) {
            const float dx = (h(x - 1, y) - h(x + 1, y)) * strength;
            const float dy = (h(x, y - 1) - h(x, y + 1)) * strength;
            const float inv = 1.0f / std::sqrt(dx * dx + dy * dy + 1.0f);
            out.setPixel(x,
                y,
                toByte((dx * inv * 0.5f + 0.5f) * 255.0f),
                toByte((dy * inv * 0.5f + 0.5f) * 255.0f),
                toByte((inv * 0.5f + 0.5f) * 255.0f),
                255);
        }
    return out;
}

// ─── Terrain texture resolution ─────────────────────────────────────────────
inline constexpr int kTerrainTexSize = 512; // upgraded from 256
inline constexpr int kPropTexSize = 256;    // upgraded from 128

// ─── Noise-backed texture generators (Phase 1a) ─────────────────────────────

// Helper: fill a canvas with multi-octave noise, modulated around a base tone.
static void fillNoiseLayer(Canvas& c,
    std::uint32_t seed,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    float freq,
    float amplitude,
    uint8_t alpha)
{
    const int w = c.width, h = c.height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(w);
            float nz = static_cast<float>(y) / static_cast<float>(h);
            float val = arp::pcg::simplex2D(nx * freq * 4.0f, nz * freq * 4.0f, seed);
            float f = 1.0f + (val - 0.5f) * amplitude;
            c.setPixel(x,
                y,
                toByte(static_cast<float>(r) * f),
                toByte(static_cast<float>(g) * f),
                toByte(static_cast<float>(b) * f),
                alpha);
        }
    }
}

// ─── Terrain splat textures ─────────────────────────────────────────────────

Canvas grassAlbedo()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 95, 128, 68, 255); // green base

    // Layer 1: broad colour variation (low freq, high amplitude)
    fillNoiseLayer(c, 777u, 90, 140, 60, 0.3f, 0.25f, 200);
    // Layer 2: medium detail — blade-like streaks via domain warp
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float dw = arp::pcg::domainWarp2D(nx * 3.0f, ny * 3.0f, 778u, 0.4f);
            float f = 0.85f + dw * 0.3f;
            c.setPixel(x, y, toByte(85.0f * f), toByte(135.0f * f), toByte(55.0f * f), 140);
        }
    }
    // Layer 3: fine grain
    fillNoiseLayer(c, 779u, 75, 125, 45, 1.5f, 0.12f, 100);
    return c;
}

Canvas grassHeight()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 128, 128, 128, 255);

    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);

            // Voronoi cells for directionality + FBM for organic variation
            float v = arp::pcg::voronoi2D(nx * 5.0f, ny * 5.0f, 780u);
            float f = arp::pcg::fbmSimplex(nx * 3.0f, ny * 3.0f, 781u, 3, 2.0f, 0.5f);
            float val = v * 0.6f + f * 0.4f;
            uint8_t b = toByte(val * 200.0f + 28.0f);
            c.setPixel(x, y, b, b, b, 255);
        }
    }
    return c;
}

Canvas dirtAlbedo()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 130, 110, 80, 255); // brown base

    // Layer 1: coarse FBM structure
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float fbm = arp::pcg::fbmSimplex(nx * 4.0f, ny * 4.0f, 909u, 4, 2.0f, 0.5f);
            float f = 0.85f + fbm * 0.3f;
            c.setPixel(x, y, toByte(140.0f * f), toByte(115.0f * f), toByte(75.0f * f), 255);
        }
    }
    // Layer 2: Voronoi cracks (dark lines)
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float ve = arp::pcg::voronoiEdge2D(nx * 6.0f, ny * 6.0f, 910u);
            if (ve < 0.08f) {
                float dark = 0.5f + ve * 6.0f;
                c.setPixel(
                    x, y, toByte(60.0f * dark), toByte(40.0f * dark), toByte(20.0f * dark), 180);
            }
        }
    }
    // Layer 3: fine sand grain
    fillNoiseLayer(c, 911u, 140, 120, 85, 2.0f, 0.08f, 80);
    return c;
}

Canvas dirtHeight()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 110, 110, 110, 255);

    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            // Multi-scale FBM: broad lumps + medium grain + fine dust
            float f1 = arp::pcg::fbmSimplex(nx * 2.0f, ny * 2.0f, 912u, 3, 2.5f, 0.4f);
            float f2 = arp::pcg::simplex2D(nx * 8.0f, ny * 8.0f, 913u);
            float val = f1 * 0.7f + f2 * 0.3f;
            uint8_t b = toByte(val * 160.0f + 48.0f);
            c.setPixel(x, y, b, b, b, 255);
        }
    }
    return c;
}

Canvas roadAlbedo()
{
    // Blend grass (at U-axis edges) into dirt (at centre) so road edges fade
    // into the surrounding terrain. U maps across the road width; V tiles
    // along the road length (getPixel wraps, so V is seamless).
    Canvas grass = grassAlbedo();
    Canvas dirt = dirtAlbedo();
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 0, 0, 0, 255);

    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(c.width);
            // w = 0 at edges (grass), 1 at centre (dirt).
            // Two smoothstep ramps: [0.00, 0.25] and [0.75, 1.00].
            float w;
            if (u < 0.5f) {
                float t = clampf(u / 0.25f, 0.0f, 1.0f);
                w = t * t * (3.0f - 2.0f * t); // smoothstep
            } else {
                float t = clampf((1.0f - u) / 0.25f, 0.0f, 1.0f);
                w = t * t * (3.0f - 2.0f * t); // smoothstep
            }
            // Add a touch of noise to the blend boundary so it isn't a
            // perfectly straight line.
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float n = arp::pcg::fbmSimplex(u * 8.0f, ny * 8.0f, 7777u, 3, 2.0f, 0.5f);
            w = clampf(w + (n - 0.5f) * 0.25f, 0.0f, 1.0f);

            uint8_t gr, gg, gb, ga;
            uint8_t dr, dg, db, da;
            grass.getPixel(x, y, gr, gg, gb, ga);
            dirt.getPixel(x, y, dr, dg, db, da);
            c.setPixel(x,
                y,
                toByte(static_cast<float>(gr) * (1.0f - w) + static_cast<float>(dr) * w),
                toByte(static_cast<float>(gg) * (1.0f - w) + static_cast<float>(dg) * w),
                toByte(static_cast<float>(gb) * (1.0f - w) + static_cast<float>(db) * w),
                255);
        }
    }
    return c;
}

Canvas roadHeight()
{
    // Matching height blend: grass height at edges, dirt height at centre.
    Canvas grass = grassHeight();
    Canvas dirt = dirtHeight();
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 0, 0, 0, 255);

    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(c.width);
            float w;
            if (u < 0.5f) {
                float t = clampf(u / 0.25f, 0.0f, 1.0f);
                w = t * t * (3.0f - 2.0f * t);
            } else {
                float t = clampf((1.0f - u) / 0.25f, 0.0f, 1.0f);
                w = t * t * (3.0f - 2.0f * t);
            }
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float n = arp::pcg::fbmSimplex(u * 8.0f, ny * 8.0f, 7777u, 3, 2.0f, 0.5f);
            w = clampf(w + (n - 0.5f) * 0.25f, 0.0f, 1.0f);

            uint8_t gr, gg, gb, ga;
            uint8_t dr, dg, db, da;
            grass.getPixel(x, y, gr, gg, gb, ga);
            dirt.getPixel(x, y, dr, dg, db, da);
            // Height is in the red channel.
            c.setPixel(x,
                y,
                toByte(static_cast<float>(gr) * (1.0f - w) + static_cast<float>(dr) * w),
                toByte(static_cast<float>(gg) * (1.0f - w) + static_cast<float>(dg) * w),
                toByte(static_cast<float>(gb) * (1.0f - w) + static_cast<float>(db) * w),
                255);
        }
    }
    return c;
}

Canvas rockAlbedo()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 140, 138, 130, 255); // grey base

    // Voronoi polygons for rock facets + domain warp for cracks
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);

            // Warped voronoi for natural rock fracture patterns
            float v = arp::pcg::voronoi2D(nx * 5.0f, ny * 5.0f, 333u);
            float dw = arp::pcg::domainWarp2D(nx * 3.0f, ny * 3.0f, 334u, 0.35f);

            float f = 0.9f + v * 0.15f + dw * 0.1f;
            c.setPixel(x, y, toByte(145.0f * f), toByte(142.0f * f), toByte(135.0f * f), 255);
        }
    }
    // Voronoi edge lines for cracks
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float ve = arp::pcg::voronoiEdge2D(nx * 4.0f, ny * 4.0f, 335u);
            if (ve < 0.05f) {
                float d = 0.4f + ve * 12.0f;
                c.setPixel(x, y, toByte(70.0f * d), toByte(68.0f * d), toByte(65.0f * d), 180);
            }
        }
    }
    // Surface roughness
    fillNoiseLayer(c, 336u, 135, 133, 125, 3.0f, 0.06f, 90);
    return c;
}

Canvas rockHeight()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 100, 100, 100, 255);

    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);

            // Strong voronoi edges → sharp rock ridges
            float ve = arp::pcg::voronoiEdge2D(nx * 4.0f, ny * 4.0f, 337u);
            float fbm = arp::pcg::fbmSimplex(nx * 6.0f, ny * 6.0f, 338u, 3, 2.0f, 0.5f);
            float val = ve * 0.5f + fbm * 0.5f;
            uint8_t b = toByte(val * 180.0f + 38.0f);
            c.setPixel(x, y, b, b, b, 255);
        }
    }
    return c;
}

Canvas sandAlbedo()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 185, 170, 130, 255); // warm sand

    // Dune waves via domain-warped FBM (horizontal direction bias)
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            // Stretch Y to create dune-like horizontal ridges
            float dw = arp::pcg::domainWarp2D(nx * 2.0f, ny * 0.5f, 555u, 0.5f);
            float f = 0.85f + dw * 0.3f;
            c.setPixel(x, y, toByte(195.0f * f), toByte(178.0f * f), toByte(135.0f * f), 255);
        }
    }
    // Fine grain
    fillNoiseLayer(c, 556u, 175, 160, 120, 3.0f, 0.07f, 100);
    return c;
}

Canvas sandHeight()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 128, 128, 128, 255);

    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);

            // Low-freq dunes (stretched Y) + mid-freq ripple
            float dune = arp::pcg::fbmSimplex(nx * 2.0f, ny * 0.4f, 557u, 3, 2.0f, 0.4f);
            float ripple = arp::pcg::simplex2D(nx * 8.0f, ny * 1.0f, 558u);
            float val = dune * 0.7f + ripple * 0.3f;
            uint8_t b = toByte(val * 160.0f + 48.0f);
            c.setPixel(x, y, b, b, b, 255);
        }
    }
    return c;
}

// ─── Macro noise (breaks up tiling at distance) ────────────────────────────
Canvas macroNoiseTexture()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 128, 128, 128, 255);

    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            // Very low frequency FBM for smooth large-scale variation
            float v = arp::pcg::fbmSimplex(nx * 1.5f, ny * 1.5f, 13579u, 3, 2.0f, 0.5f);
            uint8_t b = toByte(128.0f + (v - 0.5f) * 200.0f);
            c.setPixel(x, y, b, b, b, 255);
        }
    }
    return c;
}

// ─── Water normal maps ──────────────────────────────────────────────────────
void waterNormalMaps(Canvas& outA, Canvas& outB)
{
    auto blobbyHeight = [](int w, int h, std::uint32_t seed, float freq, int octaves) {
        Canvas ch;
        ch.init(w, h, 128, 128, 128, 255);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float nx = static_cast<float>(x) / static_cast<float>(w);
                float ny = static_cast<float>(y) / static_cast<float>(h);
                float v = arp::pcg::fbmSimplex(nx * freq, ny * freq, seed, octaves, 2.0f, 0.5f);
                uint8_t b = toByte(v * 200.0f + 28.0f);
                ch.setPixel(x, y, b, b, b, 255);
            }
        }
        return ch;
    };
    // Two layers at different scales for scrolling normal map effect
    outA = heightToNormal(blobbyHeight(256, 256, 4242u, 3.0f, 4), 3.0f);
    outB = heightToNormal(blobbyHeight(256, 256, 4243u, 5.0f, 3), 3.5f);
}

// ─── Ground detail ──────────────────────────────────────────────────────────
Canvas groundDetailTexture()
{
    Canvas c;
    c.init(kTerrainTexSize, kTerrainTexSize, 184, 184, 184, 255);
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float v = arp::pcg::fbmSimplex(nx * 8.0f, ny * 8.0f, 12345u, 4, 2.0f, 0.5f);
            float f = 0.9f + v * 0.2f;
            uint8_t b = toByte(184.0f * f);
            c.setPixel(x, y, b, b, b, 100);
        }
    }
    return c;
}

// ─── Vegetation / prop textures (unchanged structure, noise-enhanced) ───────

Canvas barkAlbedo()
{
    Canvas c;
    c.init(kPropTexSize, kPropTexSize, 107, 74, 43, 255);

    // Vertical bark stripes (unchanged core pattern)
    TextureRng rng(2024u);
    for (int x = 0; x < c.width; x += 4 + static_cast<int>(rng.next() * 6.0f)) {
        const int w = 2 + static_cast<int>(rng.next() * 3.0f);
        if (rng.next() > 0.5f)
            c.fillRect(x, 0, w, c.height, 40, 24, 12, 128);
        else
            c.fillRect(x, 0, w, c.height, 120, 90, 55, 115);
    }
    // Wood grain detail via Voronoi edge
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float ve = arp::pcg::voronoiEdge2D(nx * 3.0f, ny * 8.0f, 2025u);
            if (ve < 0.06f) {
                c.setPixel(x, y, 50, 30, 15, 60);
            }
        }
    }
    return c;
}

Canvas foliageCard()
{
    // Unchanged — radial leaf cluster, already looks fine.
    Canvas c;
    c.init(kPropTexSize, kPropTexSize, 0, 0, 0, 0);
    TextureRng rng(31337u);
    const int cx = c.width / 2, cy = c.height / 2;
    for (int i = 0; i < 360; ++i) {
        const float a = rng.next() * 6.283185307f;
        const float d = std::pow(rng.next(), 0.6f) * static_cast<float>(c.width) * 0.44f;
        const int x = cx + static_cast<int>(std::cos(a) * d);
        const int y = cy + static_cast<int>(std::sin(a) * d);
        const float fade = 1.0f - d / static_cast<float>(c.width * 0.5f);
        const int g = 80 + static_cast<int>(rng.next() * 80.0f);
        const int alpha = static_cast<int>((0.5f + rng.next() * 0.5f) * fade * 255.0f);
        c.fillEllipse(x,
            y,
            2 + static_cast<int>(rng.next() * 4.0f),
            4 + static_cast<int>(rng.next() * 7.0f),
            30 + static_cast<int>(rng.next() * 35.0f),
            g,
            28 + static_cast<int>(rng.next() * 25.0f),
            alpha);
    }
    return c;
}

Canvas grassBladeCard()
{
    // A grass blade silhouette texture with alpha. The UV space maps [0,1]x[0,1]
    // onto the card quad: U = horizontal (blade width), V = vertical (0=tip,
    // 1=base, matching the card baker's UV convention). We draw several blades
    // fanning from the base center, tapering to points at the tip. The alpha
    // channel defines the blade silhouette for alpha-tested rendering.
    Canvas c;
    c.init(kPropTexSize, kPropTexSize, 0, 0, 0, 0); // transparent background
    TextureRng rng(42424u);

    const int w = c.width;
    const int h = c.height;
    const int cx = w / 2;

    // Draw 3-5 blades fanning from bottom center upward.
    const int bladeCount = 4;
    for (int b = 0; b < bladeCount; ++b) {
        const float angle =
            (static_cast<float>(b) - (bladeCount - 1) * 0.5f) * 0.25f; // fan spread ~±0.35 rad
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        // Blade parameters with variation.
        const float maxBladeWidth = 0.12f + rng.next() * 0.04f; // fraction of tex width
        const float tipY = 0.05f + rng.next() * 0.08f;          // V at tip (near top)
        const float baseY = 0.95f;                              // V at base (near bottom)
        const float hueShift = rng.next() * 0.2f - 0.1f;

        // Sample the blade as a series of horizontal slices from base to tip.
        const int slices = 60;
        for (int s = 0; s < slices; ++s) {
            const float t = static_cast<float>(s) / (slices - 1); // 0=base, 1=tip
            const float v = baseY + (tipY - baseY) * t;           // V coordinate

            // Blade width tapers from full at base to 0 at tip.
            const float widthFrac = maxBladeWidth * (1.0f - t * t);

            // The blade center shifts horizontally based on the fan angle.
            // As we go from base (V=1) to tip (V=0), the center moves in the
            // angle direction. The center X in UV space:
            const float centerU = 0.5f + angle * 0.15f * t;

            // Convert UV to pixel coordinates.
            const int py = static_cast<int>((1.0f - v) * h); // V=0 -> y=top, V=1 -> y=bottom
            const int halfPx = static_cast<int>(widthFrac * w * 0.5f);
            const int centerPx = static_cast<int>(centerU * w);

            if (halfPx < 1)
                continue;

            // Color: darker at base, lighter at tip, with green variation.
            const int baseG = 100;
            const int tipG = 160;
            const int g = static_cast<int>(baseG + (tipG - baseG) * t + hueShift * 40);
            const int r = static_cast<int>(60 + t * 20 + hueShift * 20);
            const int bl = static_cast<int>(35 + t * 10);

            // Draw a horizontal segment of the blade at this height.
            // Alpha is higher in the blade center, softer at edges.
            for (int dx = -halfPx; dx <= halfPx; ++dx) {
                const int px = centerPx + dx;
                if (px < 0 || px >= w || py < 0 || py >= h)
                    continue;

                const float edgeFrac = 1.0f - std::abs(static_cast<float>(dx)) / halfPx;
                const int alpha = static_cast<int>(edgeFrac * 230.0f + 25.0f);

                // Blend with existing pixel (for overlapping blades).
                // Simple additive alpha blend: take max alpha, average color.
                uint8_t oldR, oldG, oldB, oldA;
                c.getPixel(px, py, oldR, oldG, oldB, oldA);
                if (oldA == 0) {
                    c.setPixel(px,
                        py,
                        static_cast<uint8_t>(r),
                        static_cast<uint8_t>(g),
                        static_cast<uint8_t>(bl),
                        static_cast<uint8_t>(alpha));
                } else {
                    // Blend: weighted average by alpha.
                    const int totalA = alpha + oldA;
                    if (totalA > 0) {
                        const int nr = (r * alpha + oldR * oldA) / totalA;
                        const int ng = (g * alpha + oldG * oldA) / totalA;
                        const int nb = (bl * alpha + oldB * oldA) / totalA;
                        const int na = (totalA > 255) ? 255 : totalA;
                        c.setPixel(px,
                            py,
                            static_cast<uint8_t>(nr),
                            static_cast<uint8_t>(ng),
                            static_cast<uint8_t>(nb),
                            static_cast<uint8_t>(na));
                    }
                }
            }
        }
    }

    // Add a subtle center vein (slightly lighter) on each blade.
    // Simple approach: brighten pixels near the horizontal center of each row.
    for (int y = 0; y < h; ++y) {
        // Find the alpha-weighted center of mass for this row.
        int totalA = 0, weightedX = 0;
        for (int x = 0; x < w; ++x) {
            uint8_t r, g, b, a;
            c.getPixel(x, y, r, g, b, a);
            if (a > 30) {
                totalA += a;
                weightedX += x * a;
            }
        }
        if (totalA > 0) {
            const int centerX = weightedX / totalA;
            for (int x = centerX - 1; x <= centerX + 1; ++x) {
                if (x < 0 || x >= w)
                    continue;
                uint8_t r, g, b, a;
                c.getPixel(x, y, r, g, b, a);
                if (a > 30) {
                    c.setPixel(x,
                        y,
                        static_cast<uint8_t>(std::min(255, r + 15)),
                        static_cast<uint8_t>(std::min(255, g + 25)),
                        static_cast<uint8_t>(std::min(255, b + 10)),
                        a);
                }
            }
        }
    }

    return c;
}

Canvas stoneAlbedo()
{
    Canvas c;
    c.init(kPropTexSize, kPropTexSize, 145, 143, 136, 255);

    // Voronoi cells for stone block shapes
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float v = arp::pcg::voronoi2D(nx * 3.0f, ny * 3.0f, 1212u);
            float f = 0.9f + v * 0.2f;
            c.setPixel(x, y, toByte(148.0f * f), toByte(145.0f * f), toByte(138.0f * f), 255);
        }
    }
    // Mortar lines via Voronoi edges
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float ve = arp::pcg::voronoiEdge2D(nx * 3.0f, ny * 3.0f, 1213u);
            if (ve < 0.04f) {
                c.setPixel(x, y, 80, 78, 72, 200);
            }
        }
    }
    return c;
}

Canvas plankAlbedo()
{
    Canvas c;
    c.init(kPropTexSize, kPropTexSize, 94, 66, 38, 255);

    // Row-based planks (unchanged structure)
    TextureRng rng(88u);
    const int rows = 6, rh = c.height / rows;
    for (int r = 0; r < rows; ++r) {
        const int y = r * rh;
        const int v = 118 + static_cast<int>(rng.next() * 38.0f);
        c.fillRect(0,
            y,
            c.width,
            rh - 2,
            toByte(v),
            toByte(static_cast<int>(v * 0.72f)),
            toByte(static_cast<int>(v * 0.46f)),
            255);
    }
    // Wood grain via FBM noise overlay
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float grain = arp::pcg::fbmSimplex(nx * 12.0f, ny * 2.5f, 89u, 3, 2.0f, 0.5f);
            float f = 0.92f + grain * 0.16f;
            uint8_t r, g, b, a;
            c.getPixel(x, y, r, g, b, a);
            c.setPixel(x, y, toByte(r * f), toByte(g * f), toByte(b * f), 180);
        }
    }
    return c;
}

Canvas thatchAlbedo()
{
    Canvas c;
    c.init(kPropTexSize, kPropTexSize, 156, 127, 66, 255);

    // Horizontal bands (unchanged)
    for (int y = 0; y < c.height; y += 16)
        c.fillRect(0, y + 13, c.width, 3, 58, 42, 18, 102);

    // Vertical straw strokes
    TextureRng rng(616u);
    for (int i = 0; i < 1800; ++i) {
        const int x = static_cast<int>(rng.next() * c.width);
        const int y = static_cast<int>(rng.next() * c.height);
        const int len = 6 + static_cast<int>(rng.next() * 12.0f);
        const int v = 140 + static_cast<int>(rng.next() * 80.0f);
        c.strokeLine(x,
            y,
            x + static_cast<int>((rng.next() - 0.5f) * 3.0f),
            y + len,
            toByte(v),
            toByte(static_cast<int>(v * 0.78f)),
            toByte(static_cast<int>(v * 0.36f)),
            128);
    }
    // Fine detail noise
    for (int y = 0; y < c.height; ++y) {
        for (int x = 0; x < c.width; ++x) {
            float nx = static_cast<float>(x) / static_cast<float>(c.width);
            float ny = static_cast<float>(y) / static_cast<float>(c.height);
            float v = arp::pcg::simplex2D(nx * 10.0f, ny * 3.0f, 617u);
            float f = 0.94f + v * 0.12f;
            uint8_t r, g, b, a;
            c.getPixel(x, y, r, g, b, a);
            c.setPixel(x, y, toByte(r * f), toByte(g * f), toByte(b * f), 100);
        }
    }
    return c;
}

} // namespace arp::core::pgtex
