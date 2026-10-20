// Procedural canvas textures — a faithful C++ port of the TS reference's
// render/textures.ts approach. The TS project draws every texture on a 2D
// <canvas> at runtime (no external image files); we replicate that math on a
// CPU RGBA8 pixel buffer so the same pixels can later be uploaded as a GPU
// texture (MaterialTextureSource) or baked to PNG. The deterministic LCG,
// drawing primitives, seamless-wrapping helper, and Sobel height->normal
// conversion all mirror the reference 1:1.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace arp::core::pgtex {

/// RGBA8 pixel canvas. Row-major, width*height*4 bytes.
struct Canvas {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // rgba8

    void init(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void getPixel(int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const;

    // Solid fill of the whole canvas.
    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    // Axis-aligned filled rectangle.
    void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    // Filled ellipse (rx/ry radii; a==0 means solid disc).
    void fillEllipse(int cx, int cy, int rx, int ry, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    // Antialiased-ish line via sampling.
    void strokeLine(int x0,
        int y0,
        int x1,
        int y1,
        uint8_t r,
        uint8_t g,
        uint8_t b,
        uint8_t a,
        float width = 1.0f);
    // Draw fn at the 9 wrap offsets (+-w, +-h) so blobs crossing an edge tile
    // seamlessly. Mirrors TS drawWrapped().
    void drawWrapped(const std::function<void(int, int)>& fn);
};

/// Deterministic linear-congruential PRNG matching the TS reference
/// (seedState = (seedState*1103515245 + 12345) & 0x7fffffff; /0x7fffffff).
class TextureRng {
public:
    explicit TextureRng(uint32_t seed = 12345u) : state_(seed) {}

    /// Float in [0,1).
    float next();

    /// Float in [min,max).
    float range(float min, float max) { return min + (max - min) * next(); }

private:
    uint32_t state_;
};

/// Convert a grayscale height canvas into a tangent-space normal map via Sobel
/// central differences, with wrap sampling. Mirrors TS heightToNormal().
Canvas heightToNormal(const Canvas& height, float strength = 2.0f);

/// Mottled detail texture multiplied over terrain vertex colors.
Canvas groundDetailTexture();

/// Four tiling albedo+normal pairs for the terrain splat (grass/dirt/rock/sand).
Canvas grassAlbedo();
Canvas dirtAlbedo();
Canvas rockAlbedo();
Canvas sandAlbedo();
/// Height-canvas producers (pair with heightToNormal() to get normals). Each
/// returns a grayscale canvas where brighter = higher relief.
Canvas grassHeight();
Canvas dirtHeight();
Canvas rockHeight();
Canvas sandHeight();

/// Road texture: blends grass (at the U-axis edges, u≈0 and u≈1) into dirt
/// (at the centre, u≈0.5) so the road mesh's left/right edges fade into the
/// surrounding grass terrain without a hard seam. 512², tiles along V (length).
Canvas roadAlbedo();
Canvas roadHeight();

/// Large-scale smooth value-noise grayscale map (Rgba8 linear / NoColorSpace).
/// Breaks up terrain texture tiling at distance: the splat shader samples its
/// .r at a few very-low frequencies for a brightness swing + a gentle hue drift.
/// 256², neutral #808080 base, ~160 wrapped radial grey blobs with soft alpha.
Canvas macroNoiseTexture();

/// Two differently-scaled blobby normal maps for the water shader (scrolled
/// against each other). Returns {normalA, normalB}.
void waterNormalMaps(Canvas& outA, Canvas& outB);

/// Vegetation / prop surface albedos.
Canvas barkAlbedo();
Canvas foliageCard(); // alpha leaf-cluster card
Canvas stoneAlbedo();
Canvas plankAlbedo();
Canvas thatchAlbedo();

/// Grass blade card texture: a crossed-blade shape with alpha transparency.
/// Used as the albedo texture for grass instanced card meshes. The alpha
/// channel defines the blade silhouette (opaque inside the blade, transparent
/// outside), enabling alpha-tested rendering so grass cards aren't solid quads.
Canvas grassBladeCard();

} // namespace arp::core::pgtex
