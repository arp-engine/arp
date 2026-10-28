#pragma once

#include "renderer/foliage_deformation_envelope.h"
#include "renderer/vegetation_instance_batcher.h"

namespace arp::renderer {
// VegetationShadowLodBatches/1. One independent light view. The caller selects
// each caster's LOD before append; this helper only groups identical prototype,
// selected geometry and history while retaining record bytes and bound unions.
// Record/key budgets are bounded by the caller's resident scene metadata.
class VegetationShadowLodBatches final {
public:
    using Key = VegetationInstanceBatcher::Key;
    using Bounds = FoliageDeformationEnvelope::Bounds;

    void reset(std::size_t instanceBudget, std::size_t prototypeCount)
    {
        constexpr std::size_t keysPerPrototype = 4u * 2u;
        if (!prototypeCount || prototypeCount > UINT32_MAX
            || prototypeCount > SIZE_MAX / keysPerPrototype)
            throw core::DiagnosticError("VEGETATION-SHADOW-LOD-PROTOTYPE-BUDGET-INVALID");
        prototypeCount_ = prototypeCount;
        const auto keyBudget = prototypeCount * keysPerPrototype;
        bounds_.resize(keyBudget);
        std::fill(bounds_.begin(), bounds_.end(), Entry{});
        batcher_.reset(instanceBudget, keyBudget);
    }

    void append(Key key, const vegetation::InstanceDataGpu& instance, const Bounds& bounds)
    {
        const auto index = keyIndex(key);
        const float minimum[]{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
        const float maximum[]{bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis])
                || minimum[axis] > maximum[axis])
                throw core::DiagnosticError("VEGETATION-SHADOW-LOD-BOUNDS-INVALID");
        // Validate before modifying either collection. The existing batcher
        // enforces the total instance budget and copies opaque metadata bytes.
        batcher_.append(key, &instance, 1);
        auto& entry = bounds_[index];
        if (!entry.valid)
            entry.bounds = bounds;
        else {
            entry.bounds.minimum.x = std::min(entry.bounds.minimum.x, bounds.minimum.x);
            entry.bounds.minimum.y = std::min(entry.bounds.minimum.y, bounds.minimum.y);
            entry.bounds.minimum.z = std::min(entry.bounds.minimum.z, bounds.minimum.z);
            entry.bounds.maximum.x = std::max(entry.bounds.maximum.x, bounds.maximum.x);
            entry.bounds.maximum.y = std::max(entry.bounds.maximum.y, bounds.maximum.y);
            entry.bounds.maximum.z = std::max(entry.bounds.maximum.z, bounds.maximum.z);
        }
        entry.valid = true;
    }

    void orderFrontToBack(core::Vec3 axis) { batcher_.orderFrontToBack(axis); }

    template <class Sink> void forEach(Sink&& sink) const
    {
        for (const auto& batch : batcher_.batches()) {
            if (batch.instances.empty())
                continue;
            const auto& entry = bounds_[keyIndex(batch.key)];
            if (!entry.valid)
                throw core::DiagnosticError("VEGETATION-SHADOW-LOD-BOUNDS-MISSING");
            sink(batch, entry.bounds);
        }
    }

private:
    struct Entry {
        Bounds bounds{};
        bool valid = false;
    };

    std::size_t keyIndex(Key key) const
    {
        if (key.prototype >= prototypeCount_ || key.lod >= 4)
            throw core::DiagnosticError("VEGETATION-SHADOW-LOD-KEY-INVALID");
        return (std::size_t(key.prototype) * 4u + key.lod) * 2u + unsigned(key.historyValid);
    }

    std::size_t prototypeCount_ = 0;
    VegetationInstanceBatcher batcher_;
    core::pmr::Vector<Entry> bounds_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::renderer
