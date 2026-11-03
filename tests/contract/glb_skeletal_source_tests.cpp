#include "assets/animated_mesh_coordinate_conversion.h"
#include "assets/glb_animated_mesh_importer.h"
#include "assets/mesh/glb_skeletal_source.h"
#include "tests/fixtures/animated_glb_source_fixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {
using namespace arp;
using namespace assets::detail;
using namespace tests::animated_glb_source_fixture;
constexpr GlbSkeletalSource::Limits sourceLimits{32, 8, 16, 32, 4096, 65536, .00001f};

void near(float a, float b)
{
    assert(std::isfinite(a) && std::abs(a - b) <= .00001f);
}

void matrixContracts()
{
    for (const auto axis : {core::Vec3{1, 0, 0}, core::Vec3{0, 1, 0}, core::Vec3{0, 0, 1}})
        for (const auto angle : {0.f, .8f, 2.8f, 3.14159265f})
            for (const auto sx : {-2.f, 2.f}) {
                const auto matrix = core::multiply(
                    core::multiply(core::Mat4::translation({5, 6, 7}),
                        core::Mat4::rotation(core::Quat::fromAxisAngle(axis, angle))),
                    core::Mat4::scale({sx, 3, 4}));
                core::AffineTrs output;
                assert(core::tryDecomposeAffineTrs(matrix, .00001f, output));
                const auto rebuilt =
                    core::multiply(core::multiply(core::Mat4::translation(output.translation),
                                       core::Mat4::rotation(output.rotation)),
                        core::Mat4::scale(output.scale));
                for (unsigned i = 0; i < 16; ++i)
                    near(matrix.m[i], rebuilt.m[i]);
            }
    core::AffineTrs sentinel;
    sentinel.translation.x = 123;
    auto fails = [&](const core::Mat4& value, float error = .00001f) {
        assert(!core::tryDecomposeAffineTrs(value, error, sentinel));
        assert(sentinel.translation.x == 123);
    };
    auto bad = core::Mat4::identity();
    bad.m[1] = .25f;
    fails(bad);
    for (const auto amount : {core::Vec3{0, 0, 0},
             core::Vec3{0, 1, 1},
             core::Vec3{1, 0, 1},
             core::Vec3{1, 1, 0},
             core::Vec3{0, 0, -2},
             core::Vec3{0, -2, 0},
             core::Vec3{-2, 0, 0},
             core::Vec3{2, -3, 4},
             core::Vec3{2, 3, -4},
             core::Vec3{-2, -3, -4}}) {
        const auto value =
            core::multiply(core::Mat4::rotation(core::Quat::fromAxisAngle({1, 2, 3}, .8f)),
                core::Mat4::scale(amount));
        core::AffineTrs result;
        assert(core::tryDecomposeAffineTrs(value, .00001f, result));
        const auto rebuilt =
            core::multiply(core::Mat4::rotation(result.rotation), core::Mat4::scale(result.scale));
        for (unsigned i = 0; i < 16; ++i)
            near(value.m[i], rebuilt.m[i]);
    }
    bad = core::Mat4::identity();
    bad.m[12] = 1;
    fails(bad);
    bad = core::Mat4::identity();
    bad.m[4] = NAN;
    fails(bad);
    bad = core::Mat4::identity();
    bad.m[4] = INFINITY;
    fails(bad);
    fails(core::Mat4::identity(), -1);
    fails(core::Mat4::identity(), NAN);
    fails(core::Mat4::identity(), INFINITY);
}

class SourceFixture final : public TrackFixture {
public:
    GlbAnimationTrackTable tracks;

    SourceFixture()
    {
        // Both skins reference the same node; one has a non-symmetric IBM and
        // the other uses the spec-defined absent/identity IBM. Animate its
        // non-joint ancestor rather than baking/dropping that motion.
        member(skin(1), "joints") = parse("[1]");
        member(skin(1), "skeleton") = parse("0");
        set(node(1), "translation", parse("[2,3,4]"));
        member(target(), "node") = parse("0");
        member(target(), "path") = parse("\"translation\"");
        member(member(root, "accessors").arrayValue[2], "type") = parse("\"VEC3\"");
        constexpr float keys[]{10, 20, 30, 14, 28, 42};
        for (unsigned i = 0; i < 6; ++i)
            real(16 + i * 4, keys[i]);
        constexpr float matrix[]{2, 2, 3, 0, 4, 5, 6, 0, 7, 8, 10, 0, 10, 20, 30, 1};
        for (unsigned i = 0; i < 16; ++i)
            writeFloat(binary, i * 4, matrix[i]);
        // A matrix-authored static node also tests the explicit column -> row
        // boundary and the runtime's existing column-major toMatrix output.
        set(node(4), "matrix", parse("[0,2,0,0,-3,0,0,0,0,0,4,0,5,6,7,1]"));
        prepare();
    }

    void prepare()
    {
        rebuild();
        const char* diagnostic = nullptr;
        assert(graph.prepare(root, accessors, 0, limits, diagnostic));
        assert(tracks.prepare(root, accessors, graph, {4, 4, 4, 16, 16, 64, 4096}, diagnostic));
    }
};

void checkPose(const GlbSkeletalSource& source)
{
    assert(source.skeleton().valid() && source.skeleton().jointCount() == 7);
    assert(source.nodeJoint(1) != 1 && source.nodeJoint(99) == GlbSkeletalSource::absent);
    assert(source.paletteJoint(0, 0) != source.paletteJoint(1, 0));
    assert(source.paletteJoint(0, 1) == GlbSkeletalSource::absent);
    assert(source.paletteJoint(2, 0) == GlbSkeletalSource::absent);
    const auto& a = source.skeleton().joints[source.paletteJoint(0, 0)];
    const auto& b = source.skeleton().joints[source.paletteJoint(1, 0)];
    assert(a.parentIndex == b.parentIndex && a.parentIndex == int(source.nodeJoint(1)));
    assert(source.clips().size() == 1
        && source.clips()[0].channels[0].jointIndex == source.nodeJoint(0));
    assert(
        source.clips()[0].channels[0].interpolation == animation::ChannelInterpolation::GltfLinear);
    core::pmr::Vector<animation::JointTransform> sampled(source.bindPose().size());
    for (std::size_t i = 0; i < sampled.size(); ++i) {
        const auto& bind = source.bindPose()[i];
        sampled[i] = {bind.translation, bind.rotation, bind.scale};
    }
    assert(animation::sampleClip(source.clips()[0], .5f, sampled));
    auto local = source.bindPose();
    for (std::size_t i = 0; i < local.size(); ++i)
        local[i] = {sampled[i].translation, sampled[i].rotation, sampled[i].scale};
    const auto model = animation::buildModelPose(source.skeleton(), local);
    const auto matrices = animation::buildSkinMatrices(source.skeleton(), model);
    auto point = animation::transformPoint(matrices[source.paletteJoint(0, 0)], {2, 3, 4});
    near(point.x, 68);
    near(point.y, 98);
    near(point.z, 134);
    point = animation::transformPoint(matrices[source.paletteJoint(1, 0)], {2, 3, 4});
    near(point.x, 16);
    near(point.y, 30);
    near(point.z, 44);
    point = animation::transformPoint(model[source.nodeJoint(4)], {1, 2, 3});
    near(point.x, -1);
    near(point.y, 8);
    near(point.z, 19);
}

void contracts()
{
    SourceFixture input;
    GlbSkeletalSource source;
    const char* diagnostic = nullptr;
    assert(source.prepare(
        input.root, input.graph, input.accessors, input.tracks, sourceLimits, diagnostic));
    checkPose(source);
    const auto* retained = source.skeleton().joints.data();
    const auto decoded = source.decodedBytes();
    auto fails = [&](auto mutate, const char* expected) {
        SourceFixture bad;
        mutate(bad);
        bad.prepare();
        assert(!source.prepare(
            bad.root, bad.graph, bad.accessors, bad.tracks, sourceLimits, diagnostic));
        assert(core::StringView(diagnostic) == expected
            && source.skeleton().joints.data() == retained);
        checkPose(source);
    };
    fails(
        [](auto& f) {
            Fixture::writeFloat(f.binary, 12, 1);
        },
        "ASSET-GLB-SKELETON-INVERSE-BIND");
    fails(
        [](auto& f) {
            Fixture::writeFloat(f.binary, 0, NAN);
        },
        "ASSET-GLB-SKELETON-INVERSE-BIND");
    fails(
        [](auto& f) {
            member(f.node(4), "matrix") = parse("[1,0,0,0,0.5,1,0,0,0,0,1,0,0,0,0,1]");
        },
        "ASSET-GLB-SKELETON-MATRIX-TRS-UNREPRESENTABLE");
    fails(
        [](auto& f) {
            Fixture::set(f.node(0), "name", parse("\"\""));
        },
        "ASSET-GLB-SKELETON-NAME");
    auto limited = sourceLimits;
    limited.maxJoints = 6;
    assert(!source.prepare(
        input.root, input.graph, input.accessors, input.tracks, limited, diagnostic));
    assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-JOINT-BUDGET");
    for (auto field : {&GlbSkeletalSource::Limits::maxClips,
             &GlbSkeletalSource::Limits::maxChannels,
             &GlbSkeletalSource::Limits::maxKeyframes,
             &GlbSkeletalSource::Limits::maxJoints}) {
        limited = sourceLimits;
        limited.*field = 0;
        assert(!source.prepare(
            input.root, input.graph, input.accessors, input.tracks, limited, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-LIMITS");
    }
    for (auto field :
        {&GlbSkeletalSource::Limits::maxNameBytes, &GlbSkeletalSource::Limits::maxDecodedBytes}) {
        limited = sourceLimits;
        limited.*field = 1;
        assert(!source.prepare(
            input.root, input.graph, input.accessors, input.tracks, limited, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-BUDGET");
    }
    limited = sourceLimits;
    limited.maxKeyframes = 1;
    assert(!source.prepare(
        input.root, input.graph, input.accessors, input.tracks, limited, diagnostic));
    assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-BUDGET");
    limited = sourceLimits;
    limited.maxJoints = std::uint32_t(UINT16_MAX) + 2;
    assert(!source.prepare(
        input.root, input.graph, input.accessors, input.tracks, limited, diagnostic));
    assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-LIMITS");
    for (const auto error : {-1.f, float(INFINITY), float(NAN)}) {
        limited = sourceLimits;
        limited.matrixElementError = error;
        assert(!source.prepare(
            input.root, input.graph, input.accessors, input.tracks, limited, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-LIMITS");
    }
    SourceFixture duplicate;
    Fixture::set(duplicate.animation(), "name", parse("\"duplicate\""));
    member(duplicate.root, "animations").arrayValue.push_back(duplicate.animation());
    duplicate.prepare();
    assert(!source.prepare(duplicate.root,
        duplicate.graph,
        duplicate.accessors,
        duplicate.tracks,
        sourceLimits,
        diagnostic));
    assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-CLIP-NAME-DUPLICATE");
    TrackFixture outside;
    assert(outside.graph.prepare(outside.root, outside.accessors, 1, limits, diagnostic));
    GlbAnimationTrackTable outsideTracks;
    assert(outsideTracks.prepare(
        outside.root, outside.accessors, outside.graph, {4, 4, 4, 16, 16, 64, 4096}, diagnostic));
    assert(!source.prepare(
        outside.root, outside.graph, outside.accessors, outsideTracks, sourceLimits, diagnostic));
    assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-OUTSIDE-SCENE-TRACK");
    limited = sourceLimits;
    limited.maxDecodedBytes = decoded - 1;
    assert(!source.prepare(
        input.root, input.graph, input.accessors, input.tracks, limited, diagnostic));
    assert(source.skeleton().joints.data() == retained && source.decodedBytes() == decoded);
    limited.maxDecodedBytes = decoded;
    assert(source.prepare(
        input.root, input.graph, input.accessors, input.tracks, limited, diagnostic));
    auto tinyQuota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(tinyQuota);
        assert(!source.prepare(
            input.root, input.graph, input.accessors, input.tracks, sourceLimits, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-ALLOCATION");
    }
    assert(!tinyQuota->liveBytes());
    checkPose(source);
    auto unlimited = core::makeShared<core::CpuMemoryQuota>();
    {
        core::CpuMemoryQuotaScope scope(unlimited);
        assert(!source.prepare(
            input.root, input.graph, input.accessors, input.tracks, sourceLimits, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-QUOTA-REQUIRED");
    }
    assert(!unlimited->liveBytes());
    // Complete RH->LH conversion must keep the same matrix/point equation.
    assets::AnimatedMeshAsset mirrored;
    mirrored.skeleton = source.skeleton();
    mirrored.bindPose = source.bindPose();
    mirrored.clips = source.clips();
    assets::convertAnimatedMeshRightHandedToLeftHanded(mirrored);
    auto locals = mirrored.bindPose;
    core::pmr::Vector<animation::JointTransform> pose(locals.size());
    for (std::size_t i = 0; i < locals.size(); ++i)
        pose[i] = {locals[i].translation, locals[i].rotation, locals[i].scale};
    assert(animation::sampleClip(mirrored.clips[0], .5f, pose));
    for (std::size_t i = 0; i < locals.size(); ++i)
        locals[i] = {pose[i].translation, pose[i].rotation, pose[i].scale};
    const auto matrices = animation::buildSkinMatrices(
        mirrored.skeleton, animation::buildModelPose(mirrored.skeleton, locals));
    const auto point = animation::transformPoint(matrices[source.paletteJoint(0, 0)], {-2, 3, 4});
    near(point.x, -68);
    near(point.y, 98);
    near(point.z, 134);
}
} // namespace

int main(int argc, char**)
{
    matrixContracts();
    {
        SourceFixture input;
        GlbSkeletalSource source;
        const char* diagnostic = nullptr;
        assert(!source.prepare(
            input.root, input.graph, input.accessors, input.tracks, sourceLimits, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-SKELETON-QUOTA-REQUIRED");
    }
    const auto initial = core::defaultCpuAllocator().stats();
    auto quota = core::makeShared<core::CpuMemoryQuota>(8 * 1024 * 1024);
    const auto before = core::defaultCpuAllocator().stats();
    {
        core::CpuMemoryQuotaScope scope(quota);
        const auto cycles = argc > 1 ? 20 : 1;
        for (int i = 0; i < cycles; ++i)
            contracts();
        std::printf("skeletal source: %d ordinary cycles, quota live %llu, peak %llu\n",
            cycles,
            (unsigned long long)quota->liveBytes(),
            (unsigned long long)quota->peakBytes());
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(!quota->liveBytes() && before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::printf("core bytes %llu -> %llu; live %llu -> %llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    quota.reset();
    const auto retired = core::defaultCpuAllocator().stats();
    assert(initial.liveBytes == retired.liveBytes);
    assert(initial.allocationCount - initial.deallocationCount
        == retired.allocationCount - retired.deallocationCount);
    std::printf("quota owner retired: core bytes %llu -> %llu, live %llu -> %llu\n",
        (unsigned long long)initial.liveBytes,
        (unsigned long long)retired.liveBytes,
        (unsigned long long)(initial.allocationCount - initial.deallocationCount),
        (unsigned long long)(retired.allocationCount - retired.deallocationCount));
}
