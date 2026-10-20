#pragma once

// TerrainStreamer — chunk-based terrain rendering scheduler.
//
// This module manages which chunks of a bounded world should be meshed and
// rendered. It does NOT generate the world — it samples a pre-existing global
// heightfield via an injected HeightSampler callback. The world is finite:
// [-worldHalfExtent, +worldHalfExtent] in XZ.
//
// Design (rewritten, Route A — finite-world rendering scheduler):
//   - Engine module (src/pcg/) — no game-specific coupling. The game layer
//     injects a HeightSampler and receives ChunkGenRequest / ChunkUnloadEvent.
//   - Bounded: chunk coordinates are clamped to the world extent.
//   - Camera-driven: update(camX, camZ) per frame computes desired set, diffs
//     against active set, and produces load/unload/LOD-change queues.
//   - 4-level LOD with hysteresis to prevent flickering at distance boundaries.
//   - Skirt generation for LOD seam hiding.
//   - Frustum-aware: chunks behind the camera are deprioritised.
//   - No GPU — generates MeshAssets on demand; the game layer manages GPU uploads.
//
// Usage (game layer):
//   TerrainStreamerDesc desc;
//   desc.worldHalfExtent = 4000.0f;
//   desc.heightSampler = [&hf](float wx, float wz) { return hf.sample(wx, wz); };
//   streamer.initialize(desc);
//   // per frame:
//   streamer.update(cameraX, cameraZ, deltaTime);
//   for (auto& req : streamer.genRequests())  { ... async mesh gen ... }
//   for (auto& ev  : streamer.unloadEvents()) { ... release GPU ... }

#include "assets/asset_pipeline.h"
#include "core/deterministic_hash.h"
#include "pcg/lod_system.h" // P4: unified LodProfile for chunk LOD resolution
#include "terrain/terrain_surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace arp::pcg {

// ─── Height sampler callback ────────────────────────────────────────────────
using HeightSampler = std::function<float(float worldX, float worldZ)>;

// ─── Chunk coordinate ───────────────────────────────────────────────────────
struct ChunkCoord {
    int x = 0;
    int z = 0;

    bool operator==(const ChunkCoord& o) const noexcept { return x == o.x && z == o.z; }
};

} // namespace arp::pcg

// Hash specialization so ChunkCoord can be used as an unordered_map key.
namespace std {
template <> struct hash<arp::pcg::ChunkCoord> {
    std::size_t operator()(const arp::pcg::ChunkCoord& c) const noexcept
    {
        auto imul32 = [](std::uint32_t a, std::uint32_t b) -> std::uint32_t {
            return a * b;
        };
        std::uint32_t h = static_cast<std::uint32_t>(c.x);
        h = imul32(h ^ imul32(static_cast<std::uint32_t>(c.z), 374761393u), 668265263u);
        h ^= h >> 13;
        h = imul32(h, 1274126177u);
        h ^= h >> 16;
        return static_cast<std::size_t>(h);
    }
};
} // namespace std

namespace arp::pcg {

// ─── LOD level descriptor ───────────────────────────────────────────────────
/// P4: The maxChunkDist field is retained for documentation but is no longer
/// used for LOD resolution -- thresholds now live in terrainLodProfile()
/// (lod_system.h), which converts them to world distances and applies
/// unified hysteresis via LodProfile::resolveTier(). samplesPerEdge is derived
/// from TerrainStreamerDesc::samplesPerChunk during initialize(), so an asset's
/// declared height-grid resolution remains the source of truth for LOD 0.
struct LodLevel {
    float maxChunkDist =
        0.0f; // [deprecated] historical chunk-distance threshold; see terrainLodProfile()
    std::uint32_t samplesPerEdge = 33u; // vertices per edge at this LOD
};

// ─── Chunk generation request ───────────────────────────────────────────────
/// Emitted by update() for chunks that need mesh generation. The game layer
/// picks these up and feeds them to a worker thread (Phase 3).
struct ChunkGenRequest {
    ChunkCoord coord;
    int lodLevel = 0;
    // Bit layout: 1=bottom(-Z), 2=top(+Z), 4=left(-X), 8=right(+X).
    // Only streaming/coarser-neighbor edges receive skirts.
    std::uint8_t skirtEdgeMask = 0u;
    // Bit layout identical to skirtEdgeMask. Set for edges whose neighbour
    // chunk is at a coarser LOD. The fine-side chunk snaps those edge
    // vertices onto the coarse neighbour's linear edge so no T-junction gap
    // remains between the two chunks. See edgeStitch() for details.
    std::uint8_t stitchEdgeMask = 0u;
    float priority = 0.0f; // lower = more urgent (negative for front-of-queue)
};

// ─── Chunk unload event ─────────────────────────────────────────────────────
/// Emitted by update() for chunks that have left the active radius.
struct ChunkUnloadEvent {
    ChunkCoord coord;
};

// ─── Active chunk record ────────────────────────────────────────────────────
struct TerrainChunk {
    ChunkCoord coord;
    assets::MeshAsset mesh; // last generated mesh (may be stale if genPending)
    // Monotonically advances when a freshly generated mesh replaces `mesh`.
    // LOD alone is not a content identity: stitch/skirt masks may regenerate
    // a chunk at the same LOD.
    std::uint64_t meshRevision = 0u;
    int lodLevel = 0;
    std::uint8_t skirtEdgeMask = 0u;
    std::uint8_t stitchEdgeMask = 0u;
    bool genPending = false;         // true while waiting for async generation
    float lastHysteresisDist = 0.0f; // for LOD hysteresis tracking
};

// ─── Configuration ──────────────────────────────────────────────────────────
struct TerrainStreamerDesc {
    // World extent. Valid chunk coords are in [-N, N] where N = ceil(worldHalfExtent / chunkSize).
    float worldHalfExtent = 4000.0f;

    // Optional asset-authoritative world bounds. When present, chunks are
    // indexed from this minimum corner and the extent must align exactly to
    // chunkSize. The legacy symmetric worldHalfExtent path remains available
    // only for callers without an asset surface.
    terrain::TerrainBounds worldBounds;

    // Size of each chunk in world units. Default 64 m = Valheim zone.
    float chunkSize = 64.0f;

    // Samples per chunk edge at LOD 0 (full detail).
    std::uint32_t samplesPerChunk = 33u;

    // Number of chunk rings to keep loaded around the camera. radius=12 → ~25×25 square
    // (~1600m), comfortably covered by fog at ~700m.
    std::uint32_t activeRadius = 12u;

    // Distance at which fog fully obscures the world (>activeRadius * chunkSize).
    float fogDistance = 700.0f;

    // Base height offset applied to all terrain vertices.
    float baseHeight = -0.5f;

    // Skirt depth — vertical drop for edge skirts (seam hiding). Set to a value
    // larger than the maximum height differential between any two adjacent chunks.
    float skirtDepth = 20.0f;

    // Height sampler callback. Must be set before initialize().
    HeightSampler heightSampler;
};

// ─── Default LOD table (4 levels) ───────────────────────────────────────────
/// P4: This table supplies only the default 33/17/9/5 tessellation for legacy
/// callers. initialize() deterministically derives the same halving sequence
/// from samplesPerChunk for every TerrainAsset-backed runtime.
inline const std::array<LodLevel, 4>& defaultLodTable()
{
    static const std::array<LodLevel, 4> table = {{
        {2.0f, 33u}, // LOD 0: 0-2 chunks (128m), full detail
        {5.0f, 17u}, // LOD 1: 2-5 chunks (320m), half detail
        {10.0f, 9u}, // LOD 2: 5-10 chunks (640m), quarter detail
        {50.0f, 5u}, // LOD 3: 10+ (3200m), coarsest (fills to fog edge)
    }};
    return table;
}

/// P4: hysteresis is now applied via LodProfile::resolveTier() in lod_system.h
/// (terrainLodProfile converts 0.3 chunk units to 0.3 * chunkSize meters).
/// Retained for backward compatibility / external reference.
inline constexpr float kDefaultHysteresisMargin = 0.3f; // chunk units

// ─── Streamer ───────────────────────────────────────────────────────────────
class TerrainStreamer {
public:
    TerrainStreamer() = default;
    ~TerrainStreamer() = default;
    TerrainStreamer(const TerrainStreamer&) = delete;
    TerrainStreamer& operator=(const TerrainStreamer&) = delete;
    TerrainStreamer(TerrainStreamer&&) = default;
    TerrainStreamer& operator=(TerrainStreamer&&) = default;

    /// Initialize the streamer. Must be called once with a valid heightSampler.
    bool initialize(const TerrainStreamerDesc& desc);

    /// True if the streamer has been successfully initialized.
    bool isInitialized() const noexcept { return initialized_; }

    /// Release all state.
    void shutdown();

    /// Per-frame update. Computes the desired chunk set based on camera position,
    /// applies LOD with hysteresis, and populates genRequests_ / unloadEvents_.
    /// @param cameraX  Camera world X
    /// @param cameraZ  Camera world Z
    /// @param cameraForwardX  Camera forward direction X (for frustum prioritisation)
    /// @param cameraForwardZ  Camera forward direction Z
    /// @param dt       Delta time (for time-budget awareness in Phase 3)
    void update(float cameraX, float cameraZ, float cameraForwardX, float cameraForwardZ, float dt);

    // ── Accessors ─────────────────────────────────────────────────────────

    const TerrainStreamerDesc& desc() const noexcept { return desc_; }

    /// Chunks that need mesh generation this frame (new or LOD changed).
    /// Sorted by priority (lowest = most urgent first).
    const std::vector<ChunkGenRequest>& genRequests() const noexcept { return genRequests_; }

    /// Chunks that should be unloaded this frame.
    const std::vector<ChunkUnloadEvent>& unloadEvents() const noexcept { return unloadEvents_; }

    /// All currently tracked chunks. Use for iteration over active set.
    const std::unordered_map<ChunkCoord, TerrainChunk>& activeChunks() const noexcept
    {
        return chunks_;
    }

    /// Mark a chunk's generation as complete. Call after async worker finishes.
    /// Updates the chunk's mesh and clears genPending flag.
    void onChunkGenerated(ChunkCoord coord, assets::MeshAsset mesh, int lodLevel);

    /// Generate the MeshAsset for one chunk at the given LOD level.
    /// Public so the async worker can call it. Thread-safe (const, reads only desc_ + sampler).
    /// @param skirtEdgeMask  edges that get a vertical drop-down skirt because
    ///                       no active neighbour is available to close the boundary.
    /// @param stitchEdgeMask edges whose vertices are snapped onto the coarser neighbour's
    ///                       linear edge to eliminate T-junction cracks (see edgeStitch()).
    assets::MeshAsset generateChunkMesh(ChunkCoord c,
        int lod,
        std::uint8_t skirtEdgeMask = 0u,
        std::uint8_t stitchEdgeMask = 0u) const;

    // ── World bounds ──────────────────────────────────────────────────────

    /// Shared mesh/decoration footprint. Requires initialization and an
    /// in-bounds coordinate; preserves legacy signed coordinates when no
    /// authoritative world bounds were supplied (TerrainChunkWorldBounds/1).
    terrain::TerrainBounds chunkWorldBounds(ChunkCoord c) const noexcept
    {
        const float ox = (desc_.worldBounds.valid() ? desc_.worldBounds.minX : 0.0f)
            + static_cast<float>(c.x) * desc_.chunkSize;
        const float oz = (desc_.worldBounds.valid() ? desc_.worldBounds.minZ : 0.0f)
            + static_cast<float>(c.z) * desc_.chunkSize;
        return {ox, ox + desc_.chunkSize, oz, oz + desc_.chunkSize};
    }

    /// Maximum axis chunk count used by LOD residency decisions.
    int chunkExtent() const noexcept
    {
        if (desc_.worldBounds.valid()) {
            const float width = desc_.worldBounds.maxX - desc_.worldBounds.minX;
            const float depth = desc_.worldBounds.maxZ - desc_.worldBounds.minZ;
            return std::max(static_cast<int>(std::round(width / desc_.chunkSize)),
                static_cast<int>(std::round(depth / desc_.chunkSize)));
        }
        return static_cast<int>(std::ceil(desc_.worldHalfExtent / desc_.chunkSize));
    }

    /// True if the chunk coordinate falls within the world bounds.
    bool isInBounds(ChunkCoord c) const noexcept
    {
        if (desc_.worldBounds.valid()) {
            const int countX = static_cast<int>(
                std::round((desc_.worldBounds.maxX - desc_.worldBounds.minX) / desc_.chunkSize));
            const int countZ = static_cast<int>(
                std::round((desc_.worldBounds.maxZ - desc_.worldBounds.minZ) / desc_.chunkSize));
            return c.x >= 0 && c.x < countX && c.z >= 0 && c.z < countZ;
        }
        const int n = chunkExtent();
        return c.x >= -n && c.x <= n && c.z >= -n && c.z <= n;
    }

private:
    // ── Helpers ───────────────────────────────────────────────────────────

    /// World position → chunk coordinate.
    ChunkCoord worldToChunk(float wx, float wz) const noexcept;

    /// Compute LOD level for a chunk, with hysteresis.
    int lodForChunk(ChunkCoord c, ChunkCoord camChunk, TerrainChunk* existing) const noexcept;

    /// Compute priority for a gen request (lower = more urgent).
    float computePriority(
        ChunkCoord c, int lod, float distSq, float camFx, float camFz) const noexcept;

    /// Append skirt vertices to a chunk mesh to hide LOD seams.
    void appendSkirt(assets::MeshAsset& mesh,
        ChunkCoord coord,
        float ox,
        float oz,
        std::uint32_t vertsX,
        std::uint32_t vertsZ,
        float minH,
        float maxH,
        bool skirtBottomEdge,
        bool skirtTopEdge,
        bool skirtLeftEdge,
        bool skirtRightEdge) const;

    /// Snap edge vertices onto the coarser neighbour's linear edge.
    /// For each flagged edge, every odd-indexed edge vertex has its height
    /// (position.y) and normal replaced by the linear interpolation of its
    /// two even-indexed neighbours. This makes the fine chunk's edge
    /// geometrically identical to what the coarse neighbour renders, closing
    /// T-junction cracks. Vertex index = zi * vertsX + xi (row-major).
    /// Edge bit layout: 1=bottom(zi=0), 2=top(zi=vertsZ-1), 4=left(xi=0), 8=right(xi=vertsX-1).
    void edgeStitch(assets::MeshAsset& mesh,
        std::uint32_t vertsX,
        std::uint32_t vertsZ,
        std::uint8_t stitchEdgeMask) const;

    // ── State ─────────────────────────────────────────────────────────────
    TerrainStreamerDesc desc_;
    std::array<LodLevel, 4> lodTable_ = defaultLodTable();
    float hysteresisMargin_ = kDefaultHysteresisMargin;

    std::unordered_map<ChunkCoord, TerrainChunk> chunks_;
    std::vector<ChunkGenRequest> genRequests_;
    std::vector<ChunkUnloadEvent> unloadEvents_;
    ChunkCoord cameraChunk_{0, 0};
    bool initialized_ = false;
};

// ─── Inline implementations ─────────────────────────────────────────────────

inline bool TerrainStreamer::initialize(const TerrainStreamerDesc& desc)
{
    if (!desc.heightSampler)
        return false;
    if (desc.chunkSize <= 0.0f)
        return false;
    if (desc.samplesPerChunk < 2u)
        return false;
    if (desc.worldBounds.valid()) {
        constexpr float kGridAlignmentTolerance = 0.0001f;
        const float chunkCountX = (desc.worldBounds.maxX - desc.worldBounds.minX) / desc.chunkSize;
        const float chunkCountZ = (desc.worldBounds.maxZ - desc.worldBounds.minZ) / desc.chunkSize;
        if (chunkCountX < 1.0f || chunkCountZ < 1.0f
            || std::fabs(chunkCountX - std::round(chunkCountX)) > kGridAlignmentTolerance
            || std::fabs(chunkCountZ - std::round(chunkCountZ)) > kGridAlignmentTolerance) {
            return false;
        }
    }
    desc_ = desc;
    // TerrainAsset samplesPerEdge is an explicit mesh-resolution contract.
    // Keep each LOD on the same globally aligned halving lattice, clamping
    // only after the finest grid has no more subdividable intervals. For
    // example, 129 samples per edge becomes 129/65/33/17, while legacy 33
    // remains 33/17/9/5. Small test assets (2 samples) retain a valid 2x2
    // mesh at every tier instead of manufacturing a zero-cell LOD.
    const std::uint32_t baseIntervals = desc.samplesPerChunk - 1u;
    for (std::size_t tier = 0; tier < lodTable_.size(); ++tier) {
        const auto intervals = baseIntervals >> tier;
        lodTable_[tier].samplesPerEdge = std::max(2u, intervals + 1u);
    }
    chunks_.clear();
    genRequests_.clear();
    unloadEvents_.clear();
    initialized_ = true;
    return true;
}

inline void TerrainStreamer::shutdown()
{
    chunks_.clear();
    genRequests_.clear();
    unloadEvents_.clear();
    initialized_ = false;
}

inline void TerrainStreamer::onChunkGenerated(
    ChunkCoord coord, assets::MeshAsset mesh, int lodLevel)
{
    auto it = chunks_.find(coord);
    if (it == chunks_.end())
        return;
    it->second.mesh = std::move(mesh);
    ++it->second.meshRevision;
    it->second.lodLevel = lodLevel;
    it->second.genPending = false;
}

inline ChunkCoord TerrainStreamer::worldToChunk(float wx, float wz) const noexcept
{
    const float cs = desc_.chunkSize;
    if (desc_.worldBounds.valid()) {
        return {
            static_cast<int>(std::floor((wx - desc_.worldBounds.minX) / cs)),
            static_cast<int>(std::floor((wz - desc_.worldBounds.minZ) / cs)),
        };
    }
    return {
        static_cast<int>(std::floor(wx / cs)),
        static_cast<int>(std::floor(wz / cs)),
    };
}

inline int TerrainStreamer::lodForChunk(
    ChunkCoord c, ChunkCoord camChunk, TerrainChunk* existing) const noexcept
{
    // If the bounded world already fits entirely inside the active chunk set,
    // mixing LODs only creates visible per-chunk topology bands with no real
    // streaming benefit. In that regime the correct contract is a uniform
    // highest-detail terrain mesh across the whole world.
    if (static_cast<std::uint32_t>(chunkExtent()) <= desc_.activeRadius) {
        return 0;
    }

    const float cs = desc_.chunkSize;
    const float dcx = static_cast<float>(c.x - camChunk.x);
    const float dcz = static_cast<float>(c.z - camChunk.z);
    const float chunkDist = std::sqrt(dcx * dcx + dcz * dcz);

    // P4: Use the unified LodProfile framework for tier resolution + hysteresis.
    // The terrain profile converts the existing 2/5/10/50 chunk-distance
    // thresholds to world distances (chunkDist * chunkSize), so the resolution
    // is bit-identical to the previous inline logic. The hysteresis margin
    // (0.3 chunk units -> 0.3 * chunkSize meters) is also preserved.
    //
    // The lodTable_ is now used only for its samplesPerEdge field (terrain-
    // specific mesh tessellation detail, not a general LOD concept). The
    // maxChunkDist thresholds have moved to terrainLodProfile().
    const LodProfile profile = terrainLodProfile(cs);
    const float worldDist = chunkDist * cs;
    const int currentTier = (existing != nullptr) ? existing->lodLevel : -1;
    return profile.resolveTier(worldDist, currentTier);
}

inline float TerrainStreamer::computePriority(
    ChunkCoord c, int lod, float distSq, float camFx, float camFz) const noexcept
{
    // Priority formula:
    //   base = lod * 100.0f + distance (coarser = less urgent)
    //   behind = dot(forward, dirToChunk) — negative if behind camera
    //   behind chunks get +200 penalty.  Keep distance as a small fractional
    //   term so a closer chunk at one LOD is never displaced by a farther
    //   chunk at the same LOD, while the LOD tier remains the primary order.
    float base = static_cast<float>(lod) * 100.0f + distSq * 0.01f;
    const float dcx = static_cast<float>(c.x - cameraChunk_.x);
    const float dcz = static_cast<float>(c.z - cameraChunk_.z);
    const float dot = dcx * camFx + dcz * camFz;
    if (dot < 0.0f)
        base += 200.0f; // behind camera → low priority
    return base;
}

inline assets::MeshAsset TerrainStreamer::generateChunkMesh(
    ChunkCoord c, int lod, std::uint8_t skirtEdgeMask, std::uint8_t stitchEdgeMask) const
{
    const float cs = desc_.chunkSize;
    const auto bounds = chunkWorldBounds(c);
    const float ox = bounds.minX;
    const float oz = bounds.minZ;

    const auto& lodDef = lodTable_[static_cast<std::size_t>(lod)];
    const std::uint32_t samples = lodDef.samplesPerEdge - 1u; // verts = samples+1
    if (samples < 1u)
        return {};
    const std::uint32_t vertsX = samples + 1u;
    const std::uint32_t vertsZ = samples + 1u;

    assets::MeshAsset mesh;
    float hmin = 1e9f, hmax = -1e9f;

    for (std::uint32_t zi = 0u; zi < vertsZ; ++zi) {
        const float tz = static_cast<float>(zi) / static_cast<float>(samples);
        const float wz = oz + tz * cs;
        for (std::uint32_t xi = 0u; xi < vertsX; ++xi) {
            const float tx = static_cast<float>(xi) / static_cast<float>(samples);
            const float wx = ox + tx * cs;

            assets::MeshVertex v;
            v.position[0] = wx;
            const float sampledHeight = desc_.heightSampler(wx, wz);
            if (!std::isfinite(sampledHeight))
                return {};
            v.position[1] = sampledHeight + desc_.baseHeight;
            v.position[2] = wz;
            if (v.position[1] < hmin)
                hmin = v.position[1];
            if (v.position[1] > hmax)
                hmax = v.position[1];

            // Normal and tangent from heightfield finite differences. Sample at
            // the spacing represented by this LOD: a fixed sub-texel epsilon
            // captures height variation that the mesh cannot represent and
            // turns it into interpolated triangle-shaped lighting patches.
            // Use central differences, not forward differences, otherwise the
            // terrain picks up a directional lighting bias aligned to the grid
            // (+X/+Z), which can show up as every cell/chunk leaning along the
            // same diagonal.  The tangent follows the actual +X heightfield
            // derivative instead of staying fixed at world +X.  A fixed tangent
            // becomes parallel to the normal on steep X-facing slopes; the TBN
            // orthogonalisation in the terrain shader then collapses and can
            // produce NaN-driven triangular specular highlights.
            {
                const float eps = std::max(cs / static_cast<float>(samples), 0.1f);
                float hx0 = desc_.heightSampler(wx - eps, wz);
                float hx1 = desc_.heightSampler(wx + eps, wz);
                float hz0 = desc_.heightSampler(wx, wz - eps);
                float hz1 = desc_.heightSampler(wx, wz + eps);
                // TerrainSurface deliberately rejects out-of-bounds queries.
                // At the declared mesh perimeter, use the valid centre sample
                // for a deterministic one-sided derivative. This never invents
                // a height and keeps all boundary vertices finite.
                const bool hasHx0 = std::isfinite(hx0);
                const bool hasHx1 = std::isfinite(hx1);
                const bool hasHz0 = std::isfinite(hz0);
                const bool hasHz1 = std::isfinite(hz1);
                if (!hasHx0 && !hasHx1)
                    return {};
                if (!hasHz0 && !hasHz1)
                    return {};
                if (!hasHx0)
                    hx0 = sampledHeight;
                if (!hasHx1)
                    hx1 = sampledHeight;
                if (!hasHz0)
                    hz0 = sampledHeight;
                if (!hasHz1)
                    hz1 = sampledHeight;
                hx0 += desc_.baseHeight;
                hx1 += desc_.baseHeight;
                hz0 += desc_.baseHeight;
                hz1 += desc_.baseHeight;
                const float dxSpan = hasHx0 && hasHx1 ? 2.0f * eps : eps;
                const float dzSpan = hasHz0 && hasHz1 ? 2.0f * eps : eps;
                const float dhdx = (hx1 - hx0) / dxSpan;
                const float dhdz = (hz1 - hz0) / dzSpan;
                float nx = -dhdx;
                float nz = -dhdz;
                float ny = 1.0f;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 0.0001f) {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }
                v.normal[0] = nx;
                v.normal[1] = ny;
                v.normal[2] = nz;

                float tx = 1.0f;
                float ty = dhdx;
                const float tangentLen = std::sqrt(tx * tx + ty * ty);
                tx /= tangentLen;
                ty /= tangentLen;
                v.tangent[0] = tx;
                v.tangent[1] = ty;
                v.tangent[2] = 0.0f;
                // Preserve the terrain normal-map convention: cross(N, T)
                // points toward world -Z on a flat heightfield.
                v.tangent[3] = 1.0f;
            }

            v.uv[0] = tx;
            v.uv[1] = tz;

            mesh.vertices.push_back(v);
        }
    }

    // ── Edge stitching: eliminate T-junction cracks at LOD boundaries ──────
    // When a neighbour chunk is at a coarser LOD, that neighbour renders its
    // edge as a *linear* interpolation between its (2x sparser) edge vertices.
    // Our fine edge has the true heightfield sample at every vertex, which at
    // the odd-indexed midpoints differs from the coarse linear edge and opens
    // a sub-metre lateral crack the vertical skirt cannot cover.
    //
    // Fix: on stitched edges, snap each odd-indexed edge vertex's height and
    // normal to the linear interpolation of its two even-indexed neighbours.
    // This makes our edge geometrically identical to the coarse neighbour's
    // rendered edge, so the two meshes meet with zero gap. Corner vertices
    // (even indices, shared by both chunks) are never touched, so the four
    // chunk corners stay anchored to the true heightfield. Interior vertices
    // are untouched - only the single edge row/column loses sub-grid detail.
    if (stitchEdgeMask != 0u && samples >= 2u) {
        edgeStitch(mesh, vertsX, vertsZ, stitchEdgeMask);
    }

    // Indices: two triangles per grid cell.
    for (std::uint32_t zi = 0u; zi < samples; ++zi) {
        for (std::uint32_t xi = 0u; xi < samples; ++xi) {
            const std::uint32_t a = zi * vertsX + xi;
            const std::uint32_t b = a + 1u;
            const std::uint32_t c = a + vertsX;
            const std::uint32_t d = c + 1u;
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }

    appendSkirt(mesh,
        c,
        ox,
        oz,
        vertsX,
        vertsZ,
        hmin,
        hmax,
        (skirtEdgeMask & 0x1u) != 0u,
        (skirtEdgeMask & 0x2u) != 0u,
        (skirtEdgeMask & 0x4u) != 0u,
        (skirtEdgeMask & 0x8u) != 0u);

    mesh.submeshes.push_back({0u, static_cast<std::uint32_t>(mesh.indices.size()), 0u});
    return mesh;
}

inline void TerrainStreamer::appendSkirt(assets::MeshAsset& mesh,
    ChunkCoord coord,
    float ox,
    float oz,
    std::uint32_t vertsX,
    std::uint32_t vertsZ,
    float minH,
    float maxH,
    bool skirtBottomEdge,
    bool skirtTopEdge,
    bool skirtLeftEdge,
    bool skirtRightEdge) const
{
    const float skirtBottom = minH - desc_.skirtDepth - (maxH - minH);
    const std::uint32_t samples = vertsX - 1u;
    if (samples < 1u)
        return;

    const std::uint32_t skirtEdgeCount = static_cast<std::uint32_t>(skirtBottomEdge)
        + static_cast<std::uint32_t>(skirtTopEdge) + static_cast<std::uint32_t>(skirtLeftEdge)
        + static_cast<std::uint32_t>(skirtRightEdge);
    mesh.vertices.reserve(
        mesh.vertices.size() + static_cast<std::size_t>(skirtEdgeCount) * samples * 4u);
    mesh.indices.reserve(
        mesh.indices.size() + static_cast<std::size_t>(skirtEdgeCount) * samples * 6u);

    auto addSkirtEdge = [&](std::uint32_t a, std::uint32_t b, float outwardX, float outwardZ) {
        // Copy before push_back: references into mesh.vertices would become
        // dangling if reserve policy changes or a reallocation occurs.
        assets::MeshVertex upperA = mesh.vertices[a];
        assets::MeshVertex upperB = mesh.vertices[b];
        assets::MeshVertex lowerA = upperA;
        assets::MeshVertex lowerB = upperB;
        lowerA.position[1] = skirtBottom;
        lowerB.position[1] = skirtBottom;

        const float edgeX = upperB.position[0] - upperA.position[0];
        const float edgeY = upperB.position[1] - upperA.position[1];
        const float edgeZ = upperB.position[2] - upperA.position[2];
        const float edgeLength = std::sqrt(edgeX * edgeX + edgeY * edgeY + edgeZ * edgeZ);
        const float invEdgeLength = edgeLength > 0.0001f ? 1.0f / edgeLength : 1.0f;
        const float tangentX = edgeX * invEdgeLength;
        const float tangentY = edgeY * invEdgeLength;
        const float tangentZ = edgeZ * invEdgeLength;

        // Every duplicated skirt vertex needs the wall's shading frame.  Reusing
        // the upward terrain frame lights the vertical seam as a glossy ground
        // triangle.  The handedness is chosen so the derived bitangent points
        // down the skirt, giving all four edge orientations the same TBN basis.
        const float crossY = outwardZ * tangentX - outwardX * tangentZ;
        const float handedness = crossY <= 0.0f ? 1.0f : -1.0f;
        auto setSkirtFrame = [&](assets::MeshVertex& vertex) {
            vertex.normal[0] = outwardX;
            vertex.normal[1] = 0.0f;
            vertex.normal[2] = outwardZ;
            vertex.tangent[0] = tangentX;
            vertex.tangent[1] = tangentY;
            vertex.tangent[2] = tangentZ;
            vertex.tangent[3] = handedness;
        };
        setSkirtFrame(upperA);
        setSkirtFrame(upperB);
        setSkirtFrame(lowerA);
        setSkirtFrame(lowerB);

        const std::uint32_t baseIdx = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(upperA);
        mesh.vertices.push_back(upperB);
        mesh.vertices.push_back(lowerA);
        mesh.vertices.push_back(lowerB);

        // The input edge order is shared by all four chunk boundaries, but its
        // natural triangle normal points outward only on bottom/right.  Select
        // winding from the actual face normal so top/left skirts are not
        // accidentally back-facing under the terrain CullMode::Back contract.
        const float dropY = lowerA.position[1] - upperA.position[1];
        const float faceX = -edgeZ * dropY;
        const float faceZ = edgeX * dropY;
        const bool currentWindingFacesOut = faceX * outwardX + faceZ * outwardZ > 0.0f;
        if (currentWindingFacesOut) {
            mesh.indices.push_back(baseIdx);
            mesh.indices.push_back(baseIdx + 1u);
            mesh.indices.push_back(baseIdx + 2u);
            mesh.indices.push_back(baseIdx + 1u);
            mesh.indices.push_back(baseIdx + 3u);
            mesh.indices.push_back(baseIdx + 2u);
        } else {
            mesh.indices.push_back(baseIdx);
            mesh.indices.push_back(baseIdx + 2u);
            mesh.indices.push_back(baseIdx + 1u);
            mesh.indices.push_back(baseIdx + 1u);
            mesh.indices.push_back(baseIdx + 2u);
            mesh.indices.push_back(baseIdx + 3u);
        }
    };

    // Bottom edge (z=0): left-to-right pairs.
    if (skirtBottomEdge) {
        for (std::uint32_t xi = 0u; xi < samples; ++xi) {
            addSkirtEdge(xi, xi + 1u, 0.0f, -1.0f);
        }
    }
    // Top edge (z=max): left-to-right pairs.
    const std::uint32_t topRow = (vertsZ - 1u) * vertsX;
    if (skirtTopEdge) {
        for (std::uint32_t xi = 0u; xi < samples; ++xi) {
            addSkirtEdge(topRow + xi, topRow + xi + 1u, 0.0f, 1.0f);
        }
    }
    // Left edge (x=0): bottom-to-top pairs.
    if (skirtLeftEdge) {
        for (std::uint32_t zi = 0u; zi < samples; ++zi) {
            addSkirtEdge(zi * vertsX, (zi + 1u) * vertsX, -1.0f, 0.0f);
        }
    }
    // Right edge (x=max): bottom-to-top pairs.
    if (skirtRightEdge) {
        for (std::uint32_t zi = 0u; zi < samples; ++zi) {
            addSkirtEdge(
                zi * vertsX + (vertsX - 1u), (zi + 1u) * vertsX + (vertsX - 1u), 1.0f, 0.0f);
        }
    }
}

inline void TerrainStreamer::edgeStitch(assets::MeshAsset& mesh,
    std::uint32_t vertsX,
    std::uint32_t vertsZ,
    std::uint8_t stitchEdgeMask) const
{
    // Vertex index = zi * vertsX + xi (row-major, matches generateChunkMesh).
    // For each flagged edge, snap odd-indexed edge vertices to the linear
    // interpolation of their two even-indexed neighbours. The LOD table is a
    // strict 2x subdivision chain (33/17/9/5 -> samples 32/16/8/4), so the
    // coarse neighbour's edge vertices land exactly on our even indices and
    // the odd indices are the T-junction midpoints. Corner vertices (index 0
    // and max on each axis) are even and shared by both chunks, so they stay
    // anchored to the true heightfield and are never modified.

    // Helper: snap one vertex's height + normal toward the linear edge.
    auto snap = [&mesh](std::uint32_t idx, std::uint32_t evenA, std::uint32_t evenB, float t) {
        auto lerp1 = [](float a, float b, float s) {
            return a + (b - a) * s;
        };
        auto& v = mesh.vertices[idx];
        const auto& va = mesh.vertices[evenA];
        const auto& vb = mesh.vertices[evenB];
        v.position[1] = lerp1(va.position[1], vb.position[1], t);
        v.normal[0] = lerp1(va.normal[0], vb.normal[0], t);
        v.normal[1] = lerp1(va.normal[1], vb.normal[1], t);
        v.normal[2] = lerp1(va.normal[2], vb.normal[2], t);
        // Re-normalise the interpolated normal so shading stays valid.
        float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 0.0001f) {
            v.normal[0] = nx / len;
            v.normal[1] = ny / len;
            v.normal[2] = nz / len;
        }
        // Rebuild a tangent that is guaranteed orthogonal to the stitched
        // normal.  Keeping the pre-stitch tangent can make it nearly parallel
        // to the interpolated normal on steep LOD boundaries.
        float tx = v.normal[1];
        float ty = -v.normal[0];
        float tz = 0.0f;
        float tangentLen = std::sqrt(tx * tx + ty * ty);
        if (tangentLen <= 0.0001f) {
            tx = 1.0f;
            ty = 0.0f;
            tz = 0.0f;
            tangentLen = 1.0f;
        }
        v.tangent[0] = tx / tangentLen;
        v.tangent[1] = ty / tangentLen;
        v.tangent[2] = tz;
        v.tangent[3] = 1.0f;
    };

    const std::uint32_t lastX = vertsX - 1u;
    const std::uint32_t lastZ = vertsZ - 1u;

    // Bottom edge (zi=0): walk xi=1,3,5... snap to (xi-1, xi+1) on the same row.
    if (stitchEdgeMask & 0x1u) {
        for (std::uint32_t xi = 1u; xi < lastX; xi += 2u) {
            snap(xi, xi - 1u, xi + 1u, 0.5f);
        }
    }
    // Top edge (zi=lastZ): index = lastZ*vertsX + xi.
    if (stitchEdgeMask & 0x2u) {
        const std::uint32_t row = lastZ * vertsX;
        for (std::uint32_t xi = 1u; xi < lastX; xi += 2u) {
            snap(row + xi, row + xi - 1u, row + xi + 1u, 0.5f);
        }
    }
    // Left edge (xi=0): index = zi*vertsX. Walk zi=1,3,5...
    if (stitchEdgeMask & 0x4u) {
        for (std::uint32_t zi = 1u; zi < lastZ; zi += 2u) {
            const std::uint32_t idx = zi * vertsX;
            snap(idx, (zi - 1u) * vertsX, (zi + 1u) * vertsX, 0.5f);
        }
    }
    // Right edge (xi=lastX): index = zi*vertsX + lastX.
    if (stitchEdgeMask & 0x8u) {
        for (std::uint32_t zi = 1u; zi < lastZ; zi += 2u) {
            const std::uint32_t idx = zi * vertsX + lastX;
            snap(idx, (zi - 1u) * vertsX + lastX, (zi + 1u) * vertsX + lastX, 0.5f);
        }
    }
}

inline void TerrainStreamer::update(
    float cameraX, float cameraZ, float cameraForwardX, float cameraForwardZ, float /*dt*/)
{
    if (!initialized_)
        return;

    genRequests_.clear();
    unloadEvents_.clear();

    const float cs = desc_.chunkSize;
    const ChunkCoord camChunk = worldToChunk(cameraX, cameraZ);
    const std::uint32_t r = desc_.activeRadius;
    const int ri = static_cast<int>(r);
    const int extent = chunkExtent();
    // A small bounded world can fit entirely inside the active set.  In that
    // mode lodForChunk() already returns LOD0 for every chunk, so no LOD seam
    // exists to hide.  Emitting boundary skirts anyway creates very tall
    // vertical faces (the skirt bottom is intentionally far below the local
    // terrain) which can be seen through mountain slopes as black strips.
    const bool wholeWorldResident = static_cast<std::uint32_t>(extent) <= r;

    // ── 1. Compute desired chunk set ───────────────────────────────────────
    // Active area: circular, bounded by world extent.
    struct DesiredEntry {
        ChunkCoord coord;
        float distSq;
    };

    std::vector<DesiredEntry> desired;
    desired.reserve((2u * r + 1u) * (2u * r + 1u));

    for (int dz = -ri; dz <= ri; ++dz) {
        for (int dx = -ri; dx <= ri; ++dx) {
            ChunkCoord cc{camChunk.x + dx, camChunk.z + dz};
            if (!isInBounds(cc))
                continue;

            const float dcx = static_cast<float>(dx);
            const float dcz = static_cast<float>(dz);
            const float distSq = (dcx * dcx + dcz * dcz);
            const float maxR = static_cast<float>(r);
            if (distSq > maxR * maxR)
                continue;

            desired.push_back({cc, distSq});
        }
    }

    // ── 2. Diff: active - desired = unload ─────────────────────────────────
    for (auto& [coord, chunk] : chunks_) {
        bool inDesired = false;
        for (auto& d : desired) {
            if (d.coord == coord) {
                inDesired = true;
                break;
            }
        }
        if (!inDesired) {
            unloadEvents_.push_back({coord});
        }
    }
    for (auto& ev : unloadEvents_) {
        chunks_.erase(ev.coord);
    }

    // ── 3. Diff: desired - active = load; LOD change = regenerate ──────────
    // Solve a continuous LOD field before requesting meshes. Adjacent chunks
    // differ by one level at most, so their common world-space samples align.
    std::unordered_map<ChunkCoord, int> desiredLods;
    desiredLods.reserve(desired.size());
    for (const auto& d : desired) {
        const auto it = chunks_.find(d.coord);
        desiredLods.emplace(
            d.coord, lodForChunk(d.coord, camChunk, it != chunks_.end() ? &it->second : nullptr));
    }
    bool relaxed = true;
    while (relaxed) {
        relaxed = false;
        for (auto& [coord, lod] : desiredLods) {
            constexpr std::array<ChunkCoord, 4> kNeighbors = {{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};
            for (const auto& delta : kNeighbors) {
                const auto neighbor = desiredLods.find({coord.x + delta.x, coord.z + delta.z});
                if (neighbor != desiredLods.end() && lod > neighbor->second + 1) {
                    lod = neighbor->second + 1;
                    relaxed = true;
                }
            }
        }
    }
    auto skirtMaskFor = [&](ChunkCoord coord, int /*lod*/) {
        if (wholeWorldResident) {
            return static_cast<std::uint8_t>(0u);
        }
        constexpr std::array<ChunkCoord, 4> kNeighbors = {{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};
        constexpr std::array<std::uint8_t, 4> kBits = {{0x1u, 0x2u, 0x4u, 0x8u}};
        std::uint8_t mask = 0u;
        for (std::size_t edge = 0; edge < kNeighbors.size(); ++edge) {
            const ChunkCoord neighbor{coord.x + kNeighbors[edge].x, coord.z + kNeighbors[edge].z};
            const auto n = desiredLods.find(neighbor);
            // Active LOD transitions are closed geometrically by edgeStitch().
            // Emitting a skirt there adds a second vertical surface along the
            // exact same boundary. On steep terrain that surface is visible as
            // a long white/grey strip even though no crack exists. Skirts are
            // reserved for true streaming/world boundaries with no neighbour.
            if (!isInBounds(neighbor) || n == desiredLods.end()) {
                mask |= kBits[edge];
            }
        }
        return mask;
    };
    // Stitch mask: only edges with an *active* neighbour at a strictly coarser
    // LOD. Out-of-bounds / unloaded neighbours have no rendered geometry to
    // match, so the skirt alone handles those (no stitch). The LOD relaxation
    // guarantees a coarser neighbour is at most one level coarser, which the
    // 2x-subdivision LOD table turns into a clean 2:1 edge snap.
    auto stitchMaskFor = [&](ChunkCoord coord, int lod) {
        constexpr std::array<ChunkCoord, 4> kNeighbors = {{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};
        constexpr std::array<std::uint8_t, 4> kBits = {{0x1u, 0x2u, 0x4u, 0x8u}};
        std::uint8_t mask = 0u;
        for (std::size_t edge = 0; edge < kNeighbors.size(); ++edge) {
            const ChunkCoord neighbor{coord.x + kNeighbors[edge].x, coord.z + kNeighbors[edge].z};
            const auto n = desiredLods.find(neighbor);
            if (n != desiredLods.end() && n->second > lod) {
                mask |= kBits[edge];
            }
        }
        return mask;
    };

    for (auto& d : desired) {
        auto it = chunks_.find(d.coord);
        TerrainChunk* existing = (it != chunks_.end()) ? &it->second : nullptr;
        const int newLod = desiredLods.at(d.coord);
        const std::uint8_t newSkirtMask = skirtMaskFor(d.coord, newLod);
        const std::uint8_t newStitchMask = stitchMaskFor(d.coord, newLod);

        if (existing == nullptr) {
            // New chunk - create entry and queue generation.
            TerrainChunk tc;
            tc.coord = d.coord;
            tc.lodLevel = newLod;
            tc.skirtEdgeMask = newSkirtMask;
            tc.stitchEdgeMask = newStitchMask;
            tc.genPending = true;
            chunks_[d.coord] = tc;

            ChunkGenRequest req;
            req.coord = d.coord;
            req.lodLevel = newLod;
            req.skirtEdgeMask = newSkirtMask;
            req.stitchEdgeMask = newStitchMask;
            req.priority =
                computePriority(d.coord, newLod, d.distSq, cameraForwardX, cameraForwardZ);
            genRequests_.push_back(req);
        } else if ((existing->lodLevel != newLod || existing->skirtEdgeMask != newSkirtMask
                       || existing->stitchEdgeMask != newStitchMask)
            && !existing->genPending) {
            // LOD changed - regenerate.
            existing->lodLevel = newLod;
            existing->skirtEdgeMask = newSkirtMask;
            existing->stitchEdgeMask = newStitchMask;
            existing->genPending = true;

            ChunkGenRequest req;
            req.coord = d.coord;
            req.lodLevel = newLod;
            req.skirtEdgeMask = newSkirtMask;
            req.stitchEdgeMask = newStitchMask;
            req.priority =
                computePriority(d.coord, newLod, d.distSq, cameraForwardX, cameraForwardZ)
                - 50.0f; // LOD change is more urgent than new load
            genRequests_.push_back(req);
        }
    }

    // ── 4. Re-queue chunks still pending from a previous frame ──────────
    // A chunk enters genPending when first requested or when its LOD changes.
    // If the game layer imposes a per-frame generation budget (it does - see
    // TerrainChunkedMeshComponent::update), unprocessed requests are dropped at
    // the end of the frame because genRequests_ is rebuilt from scratch each
    // update(). Without re-queuing here, a chunk that missed its slot would
    // stay genPending forever and never appear in the visible mesh list, which
    // manifests as missing terrain. Re-emit any genPending chunk that is still
    // in the desired set and was not already (re-)queued by the load/LOD pass
    // above.
    {
        std::unordered_map<ChunkCoord, bool> queued;
        queued.reserve(genRequests_.size());
        for (const auto& rq : genRequests_)
            queued[rq.coord] = true;
        for (auto& [coord, chunk] : chunks_) {
            if (!chunk.genPending)
                continue;
            if (desiredLods.find(coord) == desiredLods.end())
                continue;
            if (queued.find(coord) != queued.end())
                continue;
            const float dcx = static_cast<float>(coord.x - camChunk.x);
            const float dcz = static_cast<float>(coord.z - camChunk.z);
            const float distSq = dcx * dcx + dcz * dcz;
            ChunkGenRequest req;
            req.coord = coord;
            req.lodLevel = chunk.lodLevel;
            req.skirtEdgeMask = chunk.skirtEdgeMask;
            req.stitchEdgeMask = chunk.stitchEdgeMask;
            req.priority =
                computePriority(coord, chunk.lodLevel, distSq, cameraForwardX, cameraForwardZ);
            genRequests_.push_back(req);
        }
    }

    // ── 5. Sort gen requests by priority (lowest first) ────────────────────
    std::sort(genRequests_.begin(),
        genRequests_.end(),
        [](const ChunkGenRequest& a, const ChunkGenRequest& b) {
            if (a.priority != b.priority)
                return a.priority < b.priority;
            // The priority float intentionally has only a small distance
            // term.  Resolve any exact ties by coordinate so the first
            // generation batch is reproducible across startup paths and
            // standard-library sort implementations.
            if (a.coord.x != b.coord.x)
                return a.coord.x < b.coord.x;
            return a.coord.z < b.coord.z;
        });

    cameraChunk_ = camChunk;
}

} // namespace arp::pcg
