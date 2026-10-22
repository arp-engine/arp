#include "renderer/foliage_deformation_envelope.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <string_view>

using namespace arp;

namespace {
using V = core::Vec3;

V operator*(V a, float b)
{
    return core::scale(a, b);
}

float sat(float a)
{
    return std::clamp(a, 0.f, 1.f);
}

// Independent evaluation of the existing Slang vertex path, not the interval
// construction. Exercise phases, LOD branches and nonlinear interaction here.
V shaderReference(const vegetation::InstanceVertex& v,
    const vegetation::InstanceDataGpu& i,
    const vegetation::WindFieldParameters& w)
{
    const float scale = i.positionScale[3];
    const float scaleY = i.extraTransform[2] > 0 ? i.extraTransform[2] : scale;
    V p{v.position[0] * scale, v.position[1] * scaleY, v.position[2] * scale};
    const auto cy = std::cos(i.yawPhaseIdLod[0]), sy = std::sin(i.yawPhaseIdLod[0]);
    p = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};
    if (std::abs(i.extraTransform[0]) > .001f || std::abs(i.extraTransform[1]) > .001f) {
        const auto cp = std::cos(i.extraTransform[0]), sp = std::sin(i.extraTransform[0]);
        p = {p.x, p.y * cp - p.z * sp, p.y * sp + p.z * cp};
        const auto cr = std::cos(i.extraTransform[1]), sr = std::sin(i.extraTransform[1]);
        p = {p.x * cr - p.y * sr, p.x * sr + p.y * cr, p.z};
    }
    const V origin{i.positionScale[0], i.positionScale[1], i.positionScale[2]};
    p = p + origin;
    p.y += i.extraTransform[3];
    const float lod = i.yawPhaseIdLod[3], phase = i.yawPhaseIdLod[1];
    if (lod >= 3 || v.stiffness == 1)
        return p;
    const float time = w.timeGustTurb[0] + phase;
    const float speed = std::max(w.directionSpeed[3], 3.f);
    V dir{w.directionSpeed[0], w.directionSpeed[1], w.directionSpeed[2]};
    dir = std::hypot(dir.x, dir.z) > .001f ? core::normalize(dir) : V{.8f, 0, .6f};
    const V cross{-dir.z, 0, dir.x};
    const float h = sat(std::abs(v.position[1]) / 2.f), flex = 1.f - v.stiffness;
    const float spatial = (origin.x * dir.x + origin.z * dir.z) * .70f;
    const float amplitude = v.primaryBend * flex * h * (.050f + speed * .040f);
    V d = dir * (std::sin(phase * 6.2831853f + spatial + w.timeGustTurb[0] * 1.65f) * amplitude);
    d = d
        + cross
            * (std::cos(phase * 6.2831853f + spatial * 1.37f + w.timeGustTurb[0] * 1.18f)
                * amplitude * .28f);
    if (lod <= 1) {
        const float freq = w.timeGustTurb[2];
        const float wave = std::sin((p.x * dir.x + p.z * dir.z) * freq + time * freq * 2.f);
        const float strength = std::max(w.timeGustTurb[1], .25f) * h * speed * .075f * flex;
        d = d + dir * (wave * strength) + cross * (wave * strength * (.45f + .25f * v.primaryBend));
    }
    if (lod <= 0) {
        const float noise = std::sin(p.x * 1.3f + time * 1.7f) * std::cos(p.z * 1.1f + time * 1.3f);
        d = d + cross * (noise * w.timeGustTurb[3] * h * speed * .08f * flex);
    }
    if (lod <= 0 && v.flutter > .001f) {
        const float wave = std::sin(time * std::max(w.flutterParams[0], 5.f) + phase * 6.28318f);
        d = d + cross * (wave * v.flutter * std::max(w.flutterParams[1], .35f) * h * .12f * flex);
        d.y += std::abs(wave) * v.flutter * h * .018f * flex;
    }
    if (w.flutterParams[2] > .001f) {
        V to{p.x - w.interactionCenter[0], 0, p.z - w.interactionCenter[2]};
        const float distance = length(to);
        if (distance < w.flutterParams[2]) {
            const float falloff = 1.f - sat(distance / w.flutterParams[2]);
            d = d
                + to
                    * (falloff * falloff * w.flutterParams[3] * h * .3f * flex
                        / std::max(distance, .001f));
        }
    }
    return p + d;
}

void contains(const renderer::FoliageDeformationEnvelope::Bounds& b, V p)
{
    assert(p.x >= b.minimum.x && p.x <= b.maximum.x);
    assert(p.y >= b.minimum.y && p.y <= b.maximum.y);
    assert(p.z >= b.minimum.z && p.z <= b.maximum.z);
}

template <class F> void rejected(F&& f, std::string_view expected)
{
    bool caught = false;
    try {
        f();
    } catch (const core::DiagnosticError& e) {
        caught = expected == e.what();
    }
    assert(caught);
}
} // namespace

int main()
{
    using I = core::ConservativeFloatInterval;
    const auto product = I::range(-3, 2) * I::range(-7, 5);
    assert(product.low <= -15 && product.high >= 21);
    const auto cancellation = I::point(1) + I::point(-1);
    assert(cancellation.low <= 0 && cancellation.high >= 0);
    rejected(
        [] {
            I::point(std::numeric_limits<float>::quiet_NaN());
        },
        "FLOAT-INTERVAL-INVALID");
    rejected(
        [] {
            (void)(I::point(std::numeric_limits<float>::max()) * I::point(2));
        },
        "FLOAT-INTERVAL-OVERFLOW");

    renderer::FoliageDeformationEnvelope envelope;
    vegetation::WindFieldParameters wind;
    rejected(
        [&] {
            envelope.windPadding(wind);
        },
        "FOLIAGE-ENVELOPE-EMPTY");
    std::array<vegetation::InstanceVertex, 12> vertices{};
    for (unsigned n = 0; n < vertices.size(); ++n) {
        auto& v = vertices[n];
        v.position[0] = float(int(n % 3) - 1) * 1.7f;
        v.position[1] = float(n) - 4;
        v.position[2] = float(int(n % 5) - 2) * .83f;
        v.primaryBend = float(n % 4) * .7f - .5f;
        v.flutter = float(n % 3) * .4f;
        v.stiffness = float(n % 5) * .3f;
        v.normal[0] = float(n % 3) - 1;
        v.normal[1] = float(n % 4) * .7f;
        v.normal[2] = float(n % 5) * .9f - 1;
        envelope.include(v);
    }
    for (unsigned state = 0; state < 8; ++state) {
        wind.directionSpeed[0] = state & 1 ? 0.f : .7f;
        wind.directionSpeed[1] = state & 2 ? .4f : 0.f;
        wind.directionSpeed[2] = state & 1 ? 0.f : -.8f;
        wind.directionSpeed[3] = float(state) * 3.f - 1;
        wind.timeGustTurb[1] = float(state) * .3f - 1;
        wind.timeGustTurb[3] = float(state) * .2f - .5f;
        wind.flutterParams[1] = float(state) * .3f - .5f;
        wind.flutterParams[2] = state & 1 ? 20.f : 0.f;
        wind.flutterParams[3] = state & 2 ? -4.f : 3.f;
        const float normalBias = state & 1 ? -.19f : .23f;
        const auto padding = envelope.shadowPadding(wind, normalBias);
        for (unsigned transform = 0; transform < 6; ++transform) {
            vegetation::InstanceDataGpu instance{};
            instance.positionScale[0] = 1.25f;
            instance.positionScale[1] = -2;
            instance.positionScale[2] = 3;
            instance.positionScale[3] = transform & 1 ? -2.f : .7f;
            instance.extraTransform[2] = transform & 2 ? 3.5f : 0.f;
            instance.extraTransform[0] = transform & 4 ? .8f : .0001f;
            instance.extraTransform[1] = transform & 4 ? -1.1f : -.0001f;
            instance.extraTransform[3] = -.73f;
            for (unsigned phase = 0; phase < 64; ++phase) {
                instance.yawPhaseIdLod[0] = phase * .17f;
                instance.yawPhaseIdLod[1] = phase * .017f;
                instance.yawPhaseIdLod[3] = float(phase % 4);
                wind.timeGustTurb[0] = phase * .33f;
                const auto b = envelope.worldBounds(instance, padding);
                for (const auto& v : vertices) {
                    const auto cy = std::cos(instance.yawPhaseIdLod[0]);
                    const auto sy = std::sin(instance.yawPhaseIdLod[0]);
                    const V normal{v.normal[0] * cy + v.normal[2] * sy,
                        v.normal[1],
                        -v.normal[0] * sy + v.normal[2] * cy};
                    contains(b, shaderReference(v, instance, wind) + normal * normalBias);
                }
            }
        }
    }
    wind.directionSpeed[3] = std::numeric_limits<float>::infinity();
    rejected(
        [&] {
            envelope.windPadding(wind);
        },
        "FOLIAGE-ENVELOPE-WIND-INVALID");

    renderer::FoliageDeformationEnvelope rigid;
    vegetation::InstanceVertex v{};
    rigid.include(v);
    std::array<vegetation::InstanceDataGpu, 7> records{};
    const V positions[] = {
        {0, 0, .5f}, {-3, 0, .5f}, {3, 0, .5f}, {0, -3, .5f}, {0, 3, .5f}, {0, 0, -3}, {0, 0, 3}};
    for (unsigned n = 0; n < records.size(); ++n) {
        records[n].positionScale[0] = positions[n].x;
        records[n].positionScale[1] = positions[n].y;
        records[n].positionScale[2] = positions[n].z;
        records[n].positionScale[3] = 1;
    }
    const auto before = records;
    renderer::FoliageCascadeSelection selection;
    selection.prepare(records.data(), records.size(), rigid, 0, records.size());
    const auto identity = core::Mat4::identity();
    const auto frustum = core::Frustum::fromRowMajorMatrix(identity.m.data());
    unsigned selected = 0;
    selection.forEach(frustum, [&](std::size_t i, const auto&) {
        assert(i == 0);
        ++selected;
    });
    assert(selected == 1 && std::memcmp(before.data(), records.data(), sizeof(records)) == 0);
    auto broad = identity;
    broad.m[0] = broad.m[5] = broad.m[10] = .01f;
    broad.m[11] = .5f;
    selected = 0;
    selection.forEach(core::Frustum::fromRowMajorMatrix(broad.m.data()), [&](auto, const auto&) {
        ++selected;
    });
    assert(selected == records.size());
    broad.m[3] = 100;
    selected = 0;
    selection.forEach(core::Frustum::fromRowMajorMatrix(broad.m.data()), [&](auto, const auto&) {
        ++selected;
    });
    assert(selected == 0);

    // A caster anchor outside the light volume still overlaps through its
    // geometry. Alternating accepted records must retain exact source order.
    renderer::FoliageDeformationEnvelope overlapping;
    v.position[0] = -.5f;
    overlapping.include(v);
    v.position[0] = .5f;
    overlapping.include(v);
    for (unsigned n = 0; n < records.size(); ++n) {
        records[n].positionScale[0] = n % 2 == 0 ? 1.2f : 3.f;
        records[n].positionScale[1] = 0;
        records[n].positionScale[2] = .5f;
        records[n].yawPhaseIdLod[2] = std::numeric_limits<float>::quiet_NaN();
    }
    const auto fragmentedBefore = records;
    selection.prepare(records.data(), records.size(), overlapping, 0, records.size());
    selected = 0;
    selection.forEach(frustum, [&](std::size_t i, const auto&) {
        assert(i == selected * 2);
        ++selected;
    });
    assert(selected == 4);
    assert(std::memcmp(fragmentedBefore.data(), records.data(), sizeof(records)) == 0);
    rejected(
        [&] {
            selection.prepare(records.data(), records.size(), rigid, 0, 1);
        },
        "FOLIAGE-CASCADE-SELECTION-BUDGET-INVALID");
    rejected(
        [&] {
            selection.forEach(core::Frustum{}, [](auto, auto&) {});
        },
        "FOLIAGE-CASCADE-FRUSTUM-INVALID");
}
