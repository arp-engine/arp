#pragma once
#include "core/cpu_allocator.h"
#include "core/diagnostic_error.h"
#include "pcg/foliage_instance_batch.h"

#include <algorithm>

namespace arp::renderer {
// Merge only instances with identical prototype, geometry LOD and temporal
// validity. Full InstanceDataGpu bytes (including transition side/time/id)
// remain intact. Main-view batches must never replace caster/DDGI sources.
class VegetationInstanceBatcher final {
public:
    struct Key {
        std::uint32_t prototype = 0, lod = 0;
        bool historyValid = false;

        bool operator==(const Key& other) const
        {
            return prototype == other.prototype && lod == other.lod
                && historyValid == other.historyValid;
        }
    };

    struct Batch {
        Key key;
        core::pmr::Vector<pcg::InstanceDataGpu> instances{&core::defaultCpuMemoryResource()};
    };

    void reset(std::size_t instanceBudget, std::size_t batchBudget)
    {
        for (auto& b : batches_)
            b.instances.clear();
        used_ = 0;
        instanceBudget_ = instanceBudget;
        batchBudget_ = batchBudget;
    }

    template <class Instances> void append(Key key, const Instances& instances)
    {
        append(key, instances.data(), instances.size());
    }

    void append(Key key, const pcg::InstanceDataGpu* instances, std::size_t count)
    {
        if (count == 0)
            return;
        if (!instances)
            throw core::DiagnosticError("VEGETATION-BATCH-SPAN-INVALID");
        if (count > instanceBudget_ - used_)
            throw core::DiagnosticError("VEGETATION-BATCH-INSTANCE-BUDGET-EXCEEDED");
        auto it = std::find_if(batches_.begin(), batches_.end(), [&](const auto& b) {
            return b.key == key;
        });
        if (it == batches_.end()) {
            // Reuse an empty cached key before growing. Every active key is
            // determined by resident authored prototypes, LODs and history.
            it = std::find_if(batches_.begin(), batches_.end(), [](const auto& b) {
                return b.instances.empty();
            });
            if (it == batches_.end()) {
                if (batches_.size() >= batchBudget_)
                    throw core::DiagnosticError("VEGETATION-BATCH-KEY-BUDGET-EXCEEDED");
                batches_.push_back({});
                it = std::prev(batches_.end());
            }
            it->key = key;
        }
        it->instances.insert(it->instances.end(), instances, instances + count);
        used_ += count;
    }

    const auto& batches() const { return batches_; }

private:
    core::pmr::Vector<Batch> batches_{&core::defaultCpuMemoryResource()};
    std::size_t used_ = 0, instanceBudget_ = 0, batchBudget_ = 0;
};
} // namespace arp::renderer
