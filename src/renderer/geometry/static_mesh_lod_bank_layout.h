#pragma once

#include "assets/asset_pipeline.h"
#include "core/algorithm/index_runs.h"
#include "core/diagnostics/diagnostic_error.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace arp::renderer {
// Independent bank partitioning shared by shadow and main-view selection.
// Input ranges must already have passed the asset's geometry/provenance checks.
class StaticMeshLodBankLayout final {
public:
    struct Section {
        unsigned level = 0;
        std::uint32_t parentSubmesh = 0, materialSlot = 0;
        assets::Submesh parent;
        core::IndexRun ordinals;
    };

    template <class Sources> void prepare(const Sources& sources, std::size_t sectionBudget)
    {
        clear();
        if (sources.empty() || sources.size() > UINT32_MAX || !sectionBudget)
            fail("STATIC-LOD-BANK-LAYOUT-BUDGET-INVALID");
        for (const auto& source : sources)
            for (const auto& range : source.ranges)
                if (!range.indices.indexCount || range.indices.indexCount % 3u
                    || range.indices.firstIndex > UINT32_MAX - range.indices.indexCount
                    || range.indices.materialSlot != source.ranges[0].indices.materialSlot)
                    fail("STATIC-LOD-BANK-LAYOUT-RANGE-INVALID");
        for (auto& order : order_) {
            order.resize(sources.size());
            for (std::uint32_t i = 0; i < order.size(); ++i)
                order[i] = i;
        }
        auto& identities = order_[0];
        std::sort(identities.begin(), identities.end(), [&](auto a, auto b) {
            return std::pair{sources[a].node, sources[a].primitive}
            < std::pair{sources[b].node, sources[b].primitive};
        });
        for (std::size_t i = 1; i < identities.size(); ++i)
            if (sources[identities[i - 1]].node == sources[identities[i]].node
                && sources[identities[i - 1]].primitive == sources[identities[i]].primitive)
                fail("STATIC-LOD-BANK-LAYOUT-IDENTITY-DUPLICATE");
        for (unsigned level = 0; level < 3; ++level) {
            auto& order = order_[level];
            std::sort(order.begin(), order.end(), [&](auto a, auto b) {
                const auto& x = sources[a].ranges[level];
                const auto& y = sources[b].ranges[level];
                return std::pair{x.parentSubmesh, x.indices.firstIndex}
                < std::pair{y.parentSubmesh, y.indices.firstIndex};
            });
            std::size_t sections = 0;
            for (std::size_t first = 0; first < order.size();) {
                const auto& initial = sources[order[first]].ranges[level];
                auto end = initial.indices.firstIndex;
                bool approximable = false;
                std::size_t last = first;
                while (last < order.size()) {
                    const auto& source = sources[order[last]];
                    const auto& range = source.ranges[level];
                    if (range.parentSubmesh != initial.parentSubmesh)
                        break;
                    if (range.indices.firstIndex != end
                        || range.indices.materialSlot != initial.indices.materialSlot)
                        fail("STATIC-LOD-BANK-LAYOUT-PARTITION-INVALID");
                    end += range.indices.indexCount;
                    approximable |= source.approximable;
                    ++last;
                }
                if (++sections > sectionBudget)
                    fail("STATIC-LOD-BANK-LAYOUT-SECTION-BUDGET");
                if (!level || approximable)
                    sections_.push_back({level,
                        initial.parentSubmesh,
                        initial.indices.materialSlot,
                        {initial.indices.firstIndex,
                            end - initial.indices.firstIndex,
                            initial.indices.materialSlot},
                        {first, last - first}});
                first = last;
            }
        }
    }

    const core::pmr::Vector<Section>& sections() const { return sections_; }

    const core::pmr::Vector<std::uint32_t>& order(unsigned level) const { return order_.at(level); }

    void clear()
    {
        sections_.clear();
        for (auto& order : order_)
            order.clear();
    }

private:
    [[noreturn]] static void fail(const char* diagnostic)
    {
        throw core::DiagnosticError(diagnostic);
    }

    core::pmr::Vector<Section> sections_{&core::defaultCpuMemoryResource()};
    std::array<core::pmr::Vector<std::uint32_t>, 3> order_{
        core::pmr::Vector<std::uint32_t>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<std::uint32_t>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<std::uint32_t>{&core::defaultCpuMemoryResource()}};
};
} // namespace arp::renderer
