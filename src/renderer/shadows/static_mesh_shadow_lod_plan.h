#pragma once

#include "assets/mesh_shadow_lod_source.h"
#include "core/algorithm/index_runs.h"
#include "renderer/geometry/static_mesh_lod_bank_layout.h"
#include "renderer/shadows/orthographic_error_projection.h"

#include <cstring>

namespace arp::renderer {

// CPU-only StaticMeshShadowLodPlan/1. Input metadata must have passed asset
// identity/certificate admission. No GPU or ResourceManager lifetime is held.
class StaticMeshShadowLodPlan final {
public:
    struct Budget {
        std::size_t sources = 0, sections = 0, runs = 0;
    };

    // Explicit CPU selection policy; this changes neither error ceilings nor
    // L2 draw semantics. Index cohorts are not spatial clusters. A cohort uses
    // the coarsest level certified for EVERY member, separately per cascade.
    struct IndexCohortPolicy {
        std::uint32_t contractVersion = 1;
        std::uint32_t maxSources = 1;
    };

    struct Cascade {
        std::array<float, 16> viewProjection{};
        std::uint32_t width = 0, height = 0;
    };

    struct Carrier {
        unsigned level = 0;
        std::uint32_t parentSubmesh = 0, materialSlot = 0;
        assets::Submesh parent;
        std::array<core::IndexRun, 3> views{};
    };

    struct Statistics {
        std::array<std::array<std::uint32_t, 3>, 3> sources{};
        std::array<std::uint64_t, 3> triangles{};
        std::array<std::uint32_t, 3> runs{};
    };

    void prepare(const core::pmr::Vector<assets::MeshShadowLodSource>& sources,
        Budget budget,
        double texelCeiling,
        double depthCeiling)
    {
        prepare(sources, budget, texelCeiling, depthCeiling, IndexCohortPolicy{});
    }

    void prepare(const core::pmr::Vector<assets::MeshShadowLodSource>& sources,
        Budget budget,
        double texelCeiling,
        double depthCeiling,
        IndexCohortPolicy cohortPolicy)
    {
        clear();
        if (cohortPolicy.contractVersion != 1 || !cohortPolicy.maxSources)
            fail("SHADOW-LOD-PLAN-COHORT-POLICY-INVALID");
        if (sources.empty() || sources.size() > budget.sources || !budget.sections || !budget.runs
            || budget.runs > UINT32_MAX || sources.size() > UINT32_MAX)
            fail("SHADOW-LOD-PLAN-BUDGET-INVALID");
        // The projection utility owns validation of the quality ceilings.
        core::OrthographicErrorProjection::ProjectedError{}.fits(texelCeiling, depthCeiling);
        budget_ = budget;
        cohortPolicy_ = cohortPolicy;
        texelCeiling_ = texelCeiling;
        depthCeiling_ = depthCeiling;
        sources_.reserve(sources.size());
        for (const auto& source : sources) {
            sources_.push_back({source, {}, {}, {}});
            for (unsigned level = 0; level < 3; ++level) {
                const auto& range = source.ranges[level];
                if (!range.indices.indexCount || range.indices.indexCount % 3u
                    || range.indices.indexCount > UINT32_MAX - range.indices.firstIndex
                    || range.indices.materialSlot != source.ranges[0].indices.materialSlot)
                    fail("SHADOW-LOD-PLAN-SOURCE-INVALID");
                if (!source.approximable && source.surfaceError[level] != 0)
                    fail("SHADOW-LOD-PLAN-EXACT-ERROR-INVALID");
            }
        }
        layout_.prepare(sources, budget.sections);
        for (const auto& section : layout_.sections()) {
            carriers_.push_back(
                {section.level, section.parentSubmesh, section.materialSlot, section.parent, {}});
            carrierOrdinals_.push_back(section.ordinals);
        }
        runs_.reserve(budget.runs);
        prepared_ = true;
    }

    // True means rebuilt. Exact repeated input uses the previously validated
    // plan. Neither path allocates after prepare(). Invalid input fails closed.
    bool update(const std::array<Cascade, 3>& cascades, float normalBias)
    {
        if (!prepared_)
            fail("SHADOW-LOD-PLAN-NOT-PREPARED");
        if (valid_ && normalBias == normalBias_ && sameCascades(cascades, cascades_))
            return false;
        valid_ = false;
        if (!std::isfinite(normalBias))
            fail("SHADOW-LOD-PLAN-BIAS-INVALID");
        using Projection = core::OrthographicErrorProjection;
        for (const auto& cascade : cascades)
            Projection::project(cascade.viewProjection.data(), {}, cascade.width, cascade.height);
        if (!envelopesValid_ || normalBias != normalBias_) {
            envelopesValid_ = false;
            for (auto& source : sources_) {
                if (!source.asset.approximable)
                    continue;
                double originalNormal[3];
                for (unsigned a = 0; a < 3; ++a)
                    originalNormal[a] = source.asset.ranges[0].maxAbsNormal[a];
                for (unsigned level = 1; level < 3; ++level) {
                    double candidateNormal[3];
                    for (unsigned a = 0; a < 3; ++a)
                        candidateNormal[a] = source.asset.ranges[level].maxAbsNormal[a];
                    source.envelopes[level - 1] =
                        Projection::worldEnvelope(source.asset.worldAffine.data(),
                            source.asset.surfaceError[level],
                            source.asset.localAbsoluteSupport.data(),
                            originalNormal,
                            candidateNormal,
                            normalBias);
                }
            }
            normalBias_ = normalBias;
            envelopesValid_ = true;
        }
        statistics_ = {};
        for (auto& source : sources_) {
            source.chosen = {};
            source.eligibleMasks = {1, 1, 1};
            for (unsigned c = 0; c < 3; ++c) {
                if (source.asset.approximable)
                    for (unsigned level = 2; level > 0; --level) {
                        const auto error = Projection::project(cascades[c].viewProjection.data(),
                            source.envelopes[level - 1],
                            cascades[c].width,
                            cascades[c].height);
                        if (error.fits(texelCeiling_, depthCeiling_)) {
                            source.eligibleMasks[c] |= 1u << level;
                            if (!source.chosen[c])
                                source.chosen[c] = level;
                            // Preserve the independent policy's early exit.
                            if (cohortPolicy_.maxSources == 1)
                                break;
                        }
                    }
            }
        }
        if (cohortPolicy_.maxSources > 1) {
            const auto& order = layout_.order(0);
            for (std::size_t first = 0; first < order.size();) {
                const auto parent = sources_[order[first]].asset.ranges[0].parentSubmesh;
                std::size_t last = first;
                std::array<unsigned, 3> common{7, 7, 7};
                while (last < order.size() && last - first < cohortPolicy_.maxSources
                    && sources_[order[last]].asset.ranges[0].parentSubmesh == parent) {
                    for (unsigned c = 0; c < 3; ++c)
                        common[c] &= sources_[order[last]].eligibleMasks[c];
                    ++last;
                }
                for (std::size_t i = first; i < last; ++i)
                    for (unsigned c = 0; c < 3; ++c)
                        sources_[order[i]].chosen[c] = common[c] & 4u ? 2u
                            : common[c] & 2u                          ? 1u
                                                                      : 0u;
                first = last;
            }
        }
        for (const auto& source : sources_)
            for (unsigned c = 0; c < 3; ++c) {
                const auto chosen = source.chosen[c];
                ++statistics_.sources[c][chosen];
                statistics_.triangles[c] += source.asset.ranges[chosen].indices.indexCount / 3u;
            }
        runs_.clear();
        for (std::size_t i = 0; i < carriers_.size(); ++i) {
            auto& carrier = carriers_[i];
            const auto ordinals = carrierOrdinals_[i];
            for (unsigned c = 0; c < 3; ++c) {
                const auto start = runs_.size();
                core::forEachSelectedIndexRun(
                    ordinals.count,
                    [&](std::size_t n) {
                        return sources_[layout_.order(carrier.level)[ordinals.first + n]].chosen[c]
                            == carrier.level;
                    },
                    [&](core::IndexRun run) {
                        if (runs_.size() >= budget_.runs)
                            fail("SHADOW-LOD-PLAN-RUN-BUDGET");
                        const auto& first =
                            sources_[layout_.order(carrier.level)[ordinals.first + run.first]]
                                .asset.ranges[carrier.level]
                                .indices;
                        const auto& last =
                            sources_[layout_.order(
                                         carrier.level)[ordinals.first + run.first + run.count - 1]]
                                .asset.ranges[carrier.level]
                                .indices;
                        runs_.push_back({first.firstIndex,
                            std::size_t(last.firstIndex) + last.indexCount - first.firstIndex});
                    });
                carrier.views[c] = {start, runs_.size() - start};
                statistics_.runs[c] += static_cast<std::uint32_t>(carrier.views[c].count);
            }
        }
        cascades_ = cascades;
        valid_ = true;
        return true;
    }

    bool valid() const { return valid_; }

    const core::pmr::Vector<Carrier>& carriers() const
    {
        requireValid();
        return carriers_;
    }

    const core::pmr::Vector<core::IndexRun>& runs() const
    {
        requireValid();
        return runs_;
    }

    const Statistics& statistics() const
    {
        requireValid();
        return statistics_;
    }

    void clear()
    {
        valid_ = prepared_ = envelopesValid_ = false;
        sources_.clear();
        layout_.clear();
        carriers_.clear();
        carrierOrdinals_.clear();
        runs_.clear();
    }

private:
    struct Source {
        assets::MeshShadowLodSource asset;
        std::array<core::OrthographicErrorProjection::WorldEnvelope, 2> envelopes;
        std::array<unsigned, 3> chosen;
        std::array<unsigned, 3> eligibleMasks;
    };

    [[noreturn]] static void fail(const char* diagnostic)
    {
        throw core::DiagnosticError(diagnostic);
    }

    void requireValid() const
    {
        if (!valid_)
            fail("SHADOW-LOD-PLAN-NOT-VALID");
    }

    static bool sameCascades(const std::array<Cascade, 3>& a, const std::array<Cascade, 3>& b)
    {
        for (unsigned c = 0; c < 3; ++c)
            if (a[c].width != b[c].width || a[c].height != b[c].height
                || std::memcmp(
                    a[c].viewProjection.data(), b[c].viewProjection.data(), 16 * sizeof(float)))
                return false;
        return true;
    }

    Budget budget_;
    IndexCohortPolicy cohortPolicy_;
    double texelCeiling_ = 0, depthCeiling_ = 0;
    float normalBias_ = 0;
    bool prepared_ = false, valid_ = false, envelopesValid_ = false;
    std::array<Cascade, 3> cascades_{};
    Statistics statistics_;
    core::pmr::Vector<Source> sources_{&core::defaultCpuMemoryResource()};
    StaticMeshLodBankLayout layout_;
    core::pmr::Vector<Carrier> carriers_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::IndexRun> carrierOrdinals_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::IndexRun> runs_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::renderer
