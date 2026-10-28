#pragma once

#include "core/math/conservative_clip_volume.h"
#include "core/ownership/monotonic_id_sequence.h"
#include "renderer/geometry/perspective_surface_error_projection.h"
#include "renderer/geometry/static_mesh_lod_bank_layout.h"

#include <optional>

namespace arp::renderer {
// CPU geometry selection only. Caller separately admits appearance/provenance.
// Depth and color must consume the same plan. Optional source visibility is
// explicit and uses the selected bank's bounds; shadow policy stays separate.
class StaticMeshSurfaceLodPlan final {
public:
    struct Source {
        std::uint32_t node = 0, primitive = 0;
        bool approximable = false;
        std::array<assets::MeshSourceRange, 3> ranges{};
        std::array<PerspectiveSurfaceErrorProjection::Envelope, 2> envelopes{};
    };

    struct Policy {
        double pixels = 0, normalizedDepth = 0;
        std::uint32_t cohortSources = 0;
    };

    struct Budget {
        std::size_t sources = 0, sections = 0, runs = 0;
    };

    struct View {
        std::array<float, 16> viewProjection{};
        std::uint32_t width = 0, height = 0;
        // Exact raster matrix, including jitter. Absent preserves full sources.
        std::optional<std::array<float, 16>> sourceClip{};
    };

    struct Carrier {
        StaticMeshLodBankLayout::Section section;
        core::IndexRun runs;
        std::uint64_t revision = 1;
    };

    struct Statistics {
        std::array<std::uint32_t, 3> sources{};
        std::uint64_t triangles = 0;
        std::uint32_t changedSources = 0;
        std::uint32_t culledSources = 0;
        std::uint32_t projectedSources = 0;
    };

    void prepare(const core::pmr::Vector<Source>& sources, Budget budget, Policy policy)
    {
        clear();
        PerspectiveSurfaceErrorProjection::Error{}.fits(policy.pixels, policy.normalizedDepth);
        if (!policy.cohortSources || sources.size() > budget.sources || !budget.runs
            || budget.runs > UINT32_MAX)
            fail("SURFACE-LOD-PLAN-BUDGET-INVALID");
        layout_.prepare(sources, budget.sections);
        sources_ = sources;
        // Validate every envelope even if the first camera crosses its near plane.
        constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        for (const auto& source : sources_)
            for (const auto& envelope : source.envelopes)
                PerspectiveSurfaceErrorProjection::project(identity.data(), envelope, 1, 1);
        masks_.resize(sources.size());
        chosen_.resize(sources.size());
        previous_.resize(sources.size());
        for (const auto& section : layout_.sections())
            carriers_.push_back({section, {}, 1});
        runs_.reserve(budget.runs);
        policy_ = policy;
        budget_ = budget;
        owner_ = ownerIds_.tryAcquire();
        if (!owner_)
            fail("SURFACE-LOD-PLAN-OWNER-EXHAUSTED");
        prepared_ = true;
    }

    bool update(const View& view)
    {
        if (!prepared_)
            fail("SURFACE-LOD-PLAN-NOT-PREPARED");
        const bool sameProjection = valid_ && view.width == view_.width
            && view.height == view_.height && view.viewProjection == view_.viewProjection;
        if (sameProjection && view.sourceClip == view_.sourceClip)
            return false;
        const bool hadPrevious = hasSelection_;
        valid_ = false;
        PerspectiveSurfaceErrorProjection::project(
            view.viewProjection.data(), {}, view.width, view.height);
        std::optional<core::ConservativeClipVolume> volume;
        if (view.sourceClip)
            volume.emplace(view.sourceClip->data());
        statistics_ = {};
        // Raster jitter changes visibility without changing geometric error.
        // Keep eligibility masks independent of that per-frame clip matrix.
        if (!sameProjection) {
            for (std::size_t i = 0; i < sources_.size(); ++i) {
                ++statistics_.projectedSources;
                masks_[i] = 1;
                if (sources_[i].approximable)
                    for (unsigned level = 1; level < 3; ++level)
                        if (PerspectiveSurfaceErrorProjection::project(view.viewProjection.data(),
                                sources_[i].envelopes[level - 1],
                                view.width,
                                view.height)
                                .fits(policy_.pixels, policy_.normalizedDepth))
                            masks_[i] |= 1u << level;
            }
        }
        const auto& order = layout_.order(0);
        for (std::size_t first = 0; first < order.size();) {
            const auto parent = sources_[order[first]].ranges[0].parentSubmesh;
            std::size_t last = first;
            unsigned common = 7;
            while (last < order.size() && last - first < policy_.cohortSources
                && sources_[order[last]].ranges[0].parentSubmesh == parent) {
                common &= masks_[order[last]];
                ++last;
            }
            for (auto i = first; i < last; ++i) {
                const auto id = order[i];
                chosen_[id] = common & 4u ? 2u : common & 2u ? 1u : 0u;
                const auto& range = sources_[id].ranges[chosen_[id]];
                if (volume) {
                    if (!range.bounds.valid)
                        fail("SURFACE-LOD-PLAN-SOURCE-BOUNDS-MISSING");
                    if (!volume->intersects(range.bounds.min, range.bounds.max))
                        chosen_[id] = 3u;
                }
                if (chosen_[id] == 3u) {
                    ++statistics_.culledSources;
                } else {
                    ++statistics_.sources[chosen_[id]];
                    statistics_.triangles += range.indices.indexCount / 3u;
                }
                statistics_.changedSources += hadPrevious && previous_[id] != chosen_[id];
            }
            first = last;
        }
        runs_.clear();
        for (auto& carrier : carriers_) {
            const auto& section = carrier.section;
            const auto& bankOrder = layout_.order(section.level);
            bool changed = false;
            for (std::size_t n = 0; n < section.ordinals.count; ++n) {
                const auto id = bankOrder[section.ordinals.first + n];
                changed |= hadPrevious && previous_[id] != chosen_[id]
                    && (previous_[id] == section.level || chosen_[id] == section.level);
            }
            if (changed && ++carrier.revision == 0)
                fail("SURFACE-LOD-PLAN-REVISION-EXHAUSTED");
            const auto start = runs_.size();
            core::forEachSelectedIndexRun(
                section.ordinals.count,
                [&](std::size_t n) {
                    return chosen_[bankOrder[section.ordinals.first + n]] == section.level;
                },
                [&](core::IndexRun run) {
                    if (runs_.size() >= budget_.runs)
                        fail("SURFACE-LOD-PLAN-RUN-BUDGET");
                    const auto& a = sources_[bankOrder[section.ordinals.first + run.first]]
                                        .ranges[section.level]
                                        .indices;
                    const auto& b =
                        sources_[bankOrder[section.ordinals.first + run.first + run.count - 1]]
                            .ranges[section.level]
                            .indices;
                    runs_.push_back(
                        {a.firstIndex, std::size_t(b.firstIndex) + b.indexCount - a.firstIndex});
                });
            carrier.runs = {start, runs_.size() - start};
        }
        std::copy(chosen_.begin(), chosen_.end(), previous_.begin());
        view_ = view;
        hasSelection_ = true;
        valid_ = true;
        return true;
    }

    bool prepared() const { return prepared_; }

    // Immutable binding layout is available before the first camera selection.
    const auto& sections() const
    {
        if (!prepared_)
            fail("SURFACE-LOD-PLAN-NOT-PREPARED");
        return layout_.sections();
    }

    std::uint64_t owner() const
    {
        requireValid();
        return owner_;
    }

    const auto& carriers() const
    {
        requireValid();
        return carriers_;
    }

    const auto& runs() const
    {
        requireValid();
        return runs_;
    }

    const auto& statistics() const
    {
        requireValid();
        return statistics_;
    }

    void clear()
    {
        prepared_ = valid_ = hasSelection_ = false;
        owner_ = 0;
        sources_.clear();
        masks_.clear();
        chosen_.clear();
        previous_.clear();
        layout_.clear();
        carriers_.clear();
        runs_.clear();
    }

private:
    [[noreturn]] static void fail(const char* diagnostic)
    {
        throw core::DiagnosticError(diagnostic);
    }

    void requireValid() const
    {
        if (!valid_)
            fail("SURFACE-LOD-PLAN-NOT-VALID");
    }

    inline static core::MonotonicIdSequence ownerIds_;
    StaticMeshLodBankLayout layout_;
    core::pmr::Vector<Source> sources_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<unsigned> masks_{&core::defaultCpuMemoryResource()},
        chosen_{&core::defaultCpuMemoryResource()}, previous_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Carrier> carriers_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::IndexRun> runs_{&core::defaultCpuMemoryResource()};
    Policy policy_;
    Budget budget_;
    View view_;
    Statistics statistics_;
    std::uint64_t owner_ = 0;
    bool prepared_ = false, valid_ = false, hasSelection_ = false;
};
} // namespace arp::renderer
