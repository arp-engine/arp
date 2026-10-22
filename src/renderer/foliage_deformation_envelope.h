#pragma once

#include "3d/vegetation/instance_layout.h"
#include "3d/vegetation/wind_field_system.h"
#include "core/math/conservative_float_interval.h"
#include "core/math/frustum.h"
#include "core/math/math.h"

namespace arp::renderer {
// FoliageDeformationEnvelope/1. Covers all phases of the versioned shadow
// deformation contract; it never substitutes a camera visibility predicate.
class FoliageDeformationEnvelope final {
    using I = core::ConservativeFloatInterval;
    I local_[3]{};
    bool initialized_ = false, flexible_ = false;
    float primary_ = 0, flutter_ = 0, flex_ = 0, height_ = 0;
    float normalMaximum_ = 0;

public:
    struct Bounds {
        core::Vec3 minimum, maximum;
    };

    void include(const vegetation::InstanceVertex& vertex)
    {
        const float values[] = {vertex.position[0],
            vertex.position[1],
            vertex.position[2],
            vertex.primaryBend,
            vertex.flutter,
            vertex.stiffness,
            vertex.normal[0],
            vertex.normal[1],
            vertex.normal[2]};
        for (const auto value : values)
            if (!std::isfinite(value))
                throw core::DiagnosticError("FOLIAGE-ENVELOPE-VERTEX-INVALID");
        for (unsigned a = 0; a < 3; ++a) {
            const float value = vertex.position[a];
            local_[a] = initialized_
                ? I::range(std::min(local_[a].low, value), std::max(local_[a].high, value))
                : I::point(value);
        }
        initialized_ = true;
        normalMaximum_ = std::max(normalMaximum_,
            std::max(std::abs(vertex.normal[1]),
                (I::point(std::abs(vertex.normal[0])) + I::point(std::abs(vertex.normal[2])))
                    .high));
        if (vertex.stiffness == 1.f)
            return;
        flexible_ = true;
        primary_ = std::max(primary_, std::abs(vertex.primaryBend));
        flutter_ = std::max(flutter_, std::max(vertex.flutter, 0.f));
        flex_ = std::max(flex_, (I::point(1) - I::point(vertex.stiffness)).absoluteMaximum());
        height_ = std::max(
            height_, std::min(1.f, (I::point(std::abs(vertex.position[1])) * I::point(0.5f)).high));
    }

    float windPadding(const vegetation::WindFieldParameters& wind) const
    {
        if (!initialized_)
            throw core::DiagnosticError("FOLIAGE-ENVELOPE-EMPTY");
        for (const auto* values :
            {wind.directionSpeed, wind.timeGustTurb, wind.flutterParams, wind.interactionCenter})
            for (unsigned a = 0; a < 4; ++a)
                if (!std::isfinite(values[a]))
                    throw core::DiagnosticError("FOLIAGE-ENVELOPE-WIND-INVALID");
        if (!flexible_)
            return 0;
        const auto p = I::point(primary_), f = I::point(flex_), h = I::point(height_);
        const auto speed = I::point(std::max(wind.directionSpeed[3], 3.f));
        const auto clump = p * f * h * (I::point(.050f) + speed * I::point(.040f));
        auto displacement = clump + clump * I::point(.28f);
        const auto gust =
            I::point(std::max(wind.timeGustTurb[1], .25f)) * h * speed * I::point(.075f) * f;
        displacement = displacement + gust + gust * (I::point(.45f) + I::point(.25f) * p);
        displacement = displacement
            + I::point(std::abs(wind.timeGustTurb[3])) * h * speed * I::point(.08f) * f;
        const auto flutter = I::point(flutter_);
        displacement = displacement
            + flutter * I::point(std::max(wind.flutterParams[1], .35f)) * h * I::point(.12f) * f
            + flutter * h * I::point(.018f) * f;
        if (wind.flutterParams[2] > .001f)
            displacement =
                displacement + I::point(std::abs(wind.flutterParams[3])) * h * I::point(.3f) * f;
        return displacement.high;
    }

    float shadowPadding(const vegetation::WindFieldParameters& wind, float normalBias) const
    {
        return (
            I::point(windPadding(wind)) + I::point(normalMaximum_) * I::point(std::abs(normalBias)))
            .high;
    }

    Bounds worldBounds(const vegetation::InstanceDataGpu& instance, float padding) const
    {
        if (!initialized_ || !std::isfinite(padding) || padding < 0)
            throw core::DiagnosticError("FOLIAGE-ENVELOPE-BOUNDS-INVALID");
        for (unsigned a = 0; a < 4; ++a)
            if (!std::isfinite(instance.positionScale[a])
                || !std::isfinite(instance.extraTransform[a])
                || (a != 2 && !std::isfinite(instance.yawPhaseIdLod[a])))
                throw core::DiagnosticError("FOLIAGE-ENVELOPE-INSTANCE-INVALID");
        const auto scale = I::point(instance.positionScale[3]);
        const auto sy = I::point(instance.extraTransform[2] > 0 ? instance.extraTransform[2]
                                                                : instance.positionScale[3]);
        const auto trig = I::range(-1, 1);
        const auto sx = local_[0] * scale, sz = local_[2] * scale;
        I x = sx * trig + sz * trig, y = local_[1] * sy, z = sx * trig + sz * trig;
        if (std::abs(instance.extraTransform[0]) > .001f
            || std::abs(instance.extraTransform[1]) > .001f) {
            const auto py = y * trig + z * trig;
            const auto pz = y * trig + z * trig;
            const auto rx = x * trig + py * trig;
            y = x * trig + py * trig;
            x = rx;
            z = pz;
        }
        const auto displacement = I::range(-padding, padding);
        x = x + I::point(instance.positionScale[0]) + displacement;
        y = y + I::point(instance.positionScale[1]) + I::point(instance.extraTransform[3])
            + displacement;
        z = z + I::point(instance.positionScale[2]) + displacement;
        return {{x.low, y.low, z.low}, {x.high, y.high, z.high}};
    }
};

// Reusable, bounded scratch for selecting byte-preserving subsequences. Bounds
// are evaluated once per batch, then shared by every light cascade predicate.
class FoliageCascadeSelection final {
    core::pmr::Vector<FoliageDeformationEnvelope::Bounds> bounds_{
        &core::defaultCpuMemoryResource()};

public:
    void prepare(const vegetation::InstanceDataGpu* records,
        std::size_t count,
        const FoliageDeformationEnvelope& envelope,
        float padding,
        std::size_t budget)
    {
        if (count > budget || (count && !records))
            throw core::DiagnosticError("FOLIAGE-CASCADE-SELECTION-BUDGET-INVALID");
        bounds_.clear();
        bounds_.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            bounds_.push_back(envelope.worldBounds(records[i], padding));
    }

    template <class Sink> void forEach(const core::Frustum& frustum, Sink&& sink) const
    {
        for (const auto& plane : frustum.planes)
            if (!std::isfinite(plane.x) || !std::isfinite(plane.y) || !std::isfinite(plane.z)
                || !std::isfinite(plane.w) || (plane.x == 0 && plane.y == 0 && plane.z == 0))
                throw core::DiagnosticError("FOLIAGE-CASCADE-FRUSTUM-INVALID");
        for (std::size_t i = 0; i < bounds_.size(); ++i)
            if (frustum.intersectsAabbMinMax(bounds_[i].minimum, bounds_[i].maximum))
                sink(i, bounds_[i]);
    }
};
} // namespace arp::renderer
