#pragma once

// Poisson-disk sampling — deterministic, blue-noise point distribution.
//
// Unlike grid-jittered scatter, Poisson-disk sampling guarantees a minimum
// distance between any two points, producing natural-looking distributions
// free of grid artefacts.  The algorithm (Bridson, 2007) uses dart-throwing
// within an active-list, with O(n) complexity.
//
// The sampler is deterministic given a seed — same seed, same output.
//
// Usage:
//   PoissonDiskSampler sampler(w, h, minDist, seed);
//   while (auto pt = sampler.next()) {
//     // pt->x, pt->y are in [0, w-1], [0, h-1]
//   }

#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace arp::pcg {

struct PoissonPoint {
    float x, y;
};

class PoissonDiskSampler {
public:
    /// width, height: domain size.
    /// minDist: minimum distance between any two points.
    /// seed: deterministic seed for dart placement.
    /// maxAttempts: darts to throw per active point before giving up (default 30).
    PoissonDiskSampler(
        float width, float height, float minDist, std::uint32_t seed = 1337u, int maxAttempts = 30)
        : w_(width), h_(height), r_(minDist), attempts_(maxAttempts),
          cellSize_(minDist / 1.414213562f), cols_(static_cast<int>(std::ceil(width / cellSize_))),
          rows_(static_cast<int>(std::ceil(height / cellSize_))),
          grid_(static_cast<size_t>(cols_ * rows_), -1, &arp::core::defaultCpuMemoryResource()),
          points_(&arp::core::defaultCpuMemoryResource()),
          activePointIndices_(&arp::core::defaultCpuMemoryResource()), rngSeed_(seed),
          rngState_(seed)
    {
        // Place first point at center.
        float sx = width * 0.5f + randFloat() * minDist - minDist * 0.5f;
        float sy = height * 0.5f + randFloat() * minDist - minDist * 0.5f;
        addPoint(sx, sy);
    }

    /// Return the next generated point, or nullptr when the sampler is exhausted.
    const PoissonPoint* next()
    {
        while (!activePointIndices_.empty()) {
            // Pick a random active point.
            const size_t activeIndex =
                static_cast<size_t>(randFloat() * activePointIndices_.size());
            const PoissonPoint& activePoint = points_[activePointIndices_[activeIndex]];
            float ax = activePoint.x;
            float ay = activePoint.y;

            bool found = false;
            for (int k = 0; k < attempts_; ++k) {
                float angle = randFloat() * 6.283185307f;
                float dist = r_ + randFloat() * r_;
                float nx = ax + std::cos(angle) * dist;
                float ny = ay + std::sin(angle) * dist;

                if (nx >= 0.0f && nx < w_ && ny >= 0.0f && ny < h_ && !hasNeighbour(nx, ny)) {
                    addPoint(nx, ny);
                    found = true;
                    break;
                }
            }

            if (!found) {
                // Remove exhausted active point.
                activePointIndices_[activeIndex] = activePointIndices_.back();
                activePointIndices_.pop_back();
            } else {
                result_ = points_.back();
                return &result_;
            }
        }
        return nullptr;
    }

    /// Collect all points into a vector (may be large).
    arp::core::pmr::Vector<PoissonPoint> collectAll()
    {
        arp::core::pmr::Vector<PoissonPoint> pts{&arp::core::defaultCpuMemoryResource()};
        const PoissonPoint* p;
        while ((p = next()))
            pts.push_back(*p);
        return pts;
    }

private:
    float w_, h_, r_;
    int attempts_;
    float cellSize_;
    int cols_, rows_;
    arp::core::pmr::Vector<int> grid_;
    arp::core::pmr::Vector<PoissonPoint> points_;
    arp::core::pmr::Vector<size_t> activePointIndices_;
    PoissonPoint result_;
    std::uint32_t rngSeed_, rngState_;

    float randFloat()
    {
        // LCG matching the existing texture RNG for consistency.
        rngState_ = (rngState_ * 1103515245u + 12345u) & 0x7fffffff;
        return static_cast<float>(rngState_) / 2147483648.0f;
    }

    int cellIndex(float x, float y) const
    {
        int cx = static_cast<int>(x / cellSize_);
        int cy = static_cast<int>(y / cellSize_);
        return cy * cols_ + cx;
    }

    bool hasNeighbour(float x, float y) const
    {
        int cx = static_cast<int>(x / cellSize_);
        int cy = static_cast<int>(y / cellSize_);
        float r2 = r_ * r_;

        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || nx >= cols_ || ny < 0 || ny >= rows_)
                    continue;
                int idx = grid_[ny * cols_ + nx];
                if (idx < 0)
                    continue;

                const auto& pt = points_[static_cast<size_t>(idx)];
                float dx2 = pt.x - x, dy2 = pt.y - y;
                if (dx2 * dx2 + dy2 * dy2 < r2)
                    return true;
            }
        }
        return false;
    }

    void addPoint(float x, float y)
    {
        PoissonPoint pt{x, y};
        int idx = static_cast<int>(points_.size());
        points_.push_back(pt);
        activePointIndices_.push_back(static_cast<size_t>(idx));
        grid_[cellIndex(x, y)] = idx;
        result_ = pt;
    }
};

/// Convenience: generate N poisson-disk points in a domain.
/// Returns up to maxCount points; stops early if the domain is saturated.
inline arp::core::pmr::Vector<PoissonPoint> poissonDiskPoints(
    float width, float height, float minDist, std::uint32_t seed = 1337u, int maxCount = 100000)
{
    PoissonDiskSampler sampler(width, height, minDist, seed);
    arp::core::pmr::Vector<PoissonPoint> pts{&arp::core::defaultCpuMemoryResource()};
    pts.reserve(static_cast<size_t>(maxCount));
    const PoissonPoint* p;
    int count = 0;
    while ((p = sampler.next()) && count < maxCount) {
        pts.push_back(*p);
        ++count;
    }
    return pts;
}

} // namespace arp::pcg
