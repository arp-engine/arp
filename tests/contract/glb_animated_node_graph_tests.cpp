#include "assets/mesh/glb_animated_node_graph.h"
#include "assets/mesh/glb_animation_track_table.h"
#include "assets/mesh/glb_scene_source_contract.h"
#include "assets/mesh/glb_skeletal_source.h"
#include "core/io/file_io.h"
#include "tests/fixtures/animated_glb_source_fixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace assets::detail;
using namespace tests::animated_glb_source_fixture;

void verify(const Graph& graph)
{
    assert(graph.nodes().size() == 5 && graph.order().size() == 5);
    assert(graph.skins().size() == 2 && graph.joints().size() == 2);
    assert(graph.nodes()[0].mesh == 0 && graph.nodes()[0].skin == 0);
    assert(graph.nodes()[2].mesh == 0 && graph.nodes()[2].skin == 1);
    assert(graph.nodes()[4].mesh == 0 && graph.nodes()[4].skin == Graph::absent);
    assert(graph.nodes()[1].parent == 0 && graph.nodes()[3].parent == 2);
    assert(graph.skins()[0].inverseBindMatrices == 0);
    assert(graph.skins()[1].inverseBindMatrices == Graph::absent);
    assert(graph.skins()[0].commonRoot == 0 && graph.skins()[1].commonRoot == 2);
    for (const auto& node : graph.nodes())
        assert(node.selected);
    core::pmr::Vector<bool> visited(5);
    for (const auto index : graph.order()) {
        assert(!visited[index]);
        assert(
            graph.nodes()[index].parent == Graph::absent || visited[graph.nodes()[index].parent]);
        visited[index] = true;
    }
}

void contracts(bool verbose = false)
{
    Fixture valid;
    Graph graph;
    const char* diagnostic = nullptr;
    assert(graph.prepare(valid.root, valid.accessors, 0, limits, diagnostic));
    verify(graph);
    const auto* retained = graph.nodes().data();
    unsigned negatives = 0;
    auto reject = [&](auto mutate, const char* expected) {
        Fixture input;
        mutate(input);
        assert(!graph.prepare(input.root, input.accessors, 0, limits, diagnostic));
        assert(core::StringView(diagnostic) == expected);
        assert(graph.nodes().data() == retained);
        verify(graph);
        ++negatives;
    };
    reject(
        [](auto& f) {
            member(f.node(0), "children") = parse("[9]");
        },
        "ASSET-GLB-NODE-CHILD");
    reject(
        [](auto& f) {
            member(f.node(0), "children") = parse("[0]");
        },
        "ASSET-GLB-NODE-CHILD");
    reject(
        [](auto& f) {
            member(f.node(0), "children") = parse("[1,1]");
        },
        "ASSET-GLB-NODE-MULTIPLE-PARENTS");
    reject(
        [](auto& f) {
            member(f.node(2), "children") = parse("[1]");
        },
        "ASSET-GLB-NODE-MULTIPLE-PARENTS");
    reject(
        [](auto& f) {
            Fixture::set(f.node(1), "children", parse("[0]"));
        },
        "ASSET-GLB-NODE-CYCLE");
    reject(
        [](auto& f) {
            member(f.node(0), "mesh") = parse("1");
        },
        "ASSET-GLB-NODE-MESH");
    reject(
        [](auto& f) {
            member(f.node(0), "skin") = parse("2");
        },
        "ASSET-GLB-NODE-SKIN");
    reject(
        [](auto& f) {
            Fixture::set(f.node(1), "skin", parse("0"));
        },
        "ASSET-GLB-NODE-SKIN");
    reject(
        [](auto& f) {
            member(f.skin(0), "joints") = parse("[]");
        },
        "ASSET-GLB-NODE-JOINTS");
    reject(
        [](auto& f) {
            member(f.skin(0), "joints") = parse("[9]");
        },
        "ASSET-GLB-NODE-JOINTS");
    reject(
        [](auto& f) {
            member(f.skin(1), "joints") = parse("[3,3]");
        },
        "ASSET-GLB-NODE-JOINTS");
    reject(
        [](auto& f) {
            f.skin(1) = parse(R"({"joints":[3,1]})");
        },
        "ASSET-GLB-NODE-JOINT-ROOT");
    reject(
        [](auto& f) {
            member(f.skin(0), "skeleton") = parse("9");
        },
        "ASSET-GLB-NODE-SKELETON");
    reject(
        [](auto& f) {
            member(f.skin(0), "skeleton") = parse("3");
        },
        "ASSET-GLB-NODE-SKELETON");
    reject(
        [](auto& f) {
            member(f.skin(0), "inverseBindMatrices") = parse("1");
        },
        "ASSET-GLB-NODE-INVERSE-BIND");
    reject(
        [](auto& f) {
            member(f.skin(0), "joints") = parse("[0,1]");
        },
        "ASSET-GLB-NODE-INVERSE-BIND");
    reject(
        [](auto& f) {
            member(member(f.root, "scenes").arrayValue[0], "nodes") = parse("[0,0]");
        },
        "ASSET-GLB-NODE-SCENE-ROOT");
    reject(
        [](auto& f) {
            member(member(f.root, "scenes").arrayValue[0], "nodes") = parse("[1]");
        },
        "ASSET-GLB-NODE-SCENE-ROOT");
    reject(
        [](auto& f) {
            member(member(f.root, "scenes").arrayValue[0], "nodes") = parse("[9]");
        },
        "ASSET-GLB-NODE-SCENE-ROOT");
    reject(
        [](auto& f) {
            Fixture::set(f.root, "scene", parse("9"));
        },
        "ASSET-GLB-NODE-SCENE");
    reject(
        [](auto& f) {
            member(f.skin(0), "joints") = parse("[3]");
            member(f.skin(0), "skeleton") = parse("2");
            member(member(f.root, "scenes").arrayValue[0], "nodes") = parse("[0]");
        },
        "ASSET-GLB-NODE-SKIN-SCENE");
    reject(
        [](auto& f) {
            member(f.node(1), "rotation") = parse("[0,0,0,0]");
        },
        "ASSET-GLB-NODE-TRANSFORM");
    reject(
        [](auto& f) {
            member(f.node(1), "rotation") = parse("[0,0,1]");
        },
        "ASSET-GLB-NODE-TRANSFORM");
    reject(
        [](auto& f) {
            Fixture::set(f.node(1), "translation", parse("[1e40,0,0]"));
        },
        "ASSET-GLB-NODE-TRANSFORM");
    reject(
        [](auto& f) {
            Fixture::set(f.node(1), "matrix", parse("[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]"));
        },
        "ASSET-GLB-NODE-TRANSFORM");
    for (auto field : {&Graph::Limits::maxNodes,
             &Graph::Limits::maxEdges,
             &Graph::Limits::maxScenes,
             &Graph::Limits::maxSceneRoots,
             &Graph::Limits::maxSkins,
             &Graph::Limits::maxJoints,
             &Graph::Limits::maxMeshInstances,
             &Graph::Limits::maxWorkingBytes}) {
        auto bad = limits;
        bad.*field = 0;
        assert(!graph.prepare(valid.root, valid.accessors, 0, bad, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-NODE-LIMITS");
    }
    for (auto field : {&Graph::Limits::maxNodes,
             &Graph::Limits::maxEdges,
             &Graph::Limits::maxScenes,
             &Graph::Limits::maxSceneRoots,
             &Graph::Limits::maxSkins,
             &Graph::Limits::maxJoints,
             &Graph::Limits::maxMeshInstances,
             &Graph::Limits::maxWorkingBytes}) {
        auto bad = limits;
        bad.*field = 1;
        assert(!graph.prepare(valid.root, valid.accessors, 0, bad, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-NODE-BUDGET");
    }
    // Exact retained+candidate+marker storage succeeds; one byte less fails.
    auto exact = limits;
    exact.maxWorkingBytes = graph.ownedBytes() * 2 + graph.nodes().size() * sizeof(std::uint32_t);
    --exact.maxWorkingBytes;
    assert(!graph.prepare(valid.root, valid.accessors, 0, exact, diagnostic));
    assert(graph.nodes().data() == retained);
    ++exact.maxWorkingBytes;
    assert(graph.prepare(valid.root, valid.accessors, 0, exact, diagnostic));
    verify(graph);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!graph.prepare(valid.root, valid.accessors, 0, limits, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-NODE-ALLOCATION");
    }
    assert(!quota->liveBytes());
    verify(graph);
    assert(graph.prepare(valid.root, valid.accessors, 1, limits, diagnostic));
    assert(!graph.nodes()[0].selected && !graph.nodes()[1].selected);
    assert(graph.nodes()[2].selected && graph.nodes()[3].selected && !graph.nodes()[4].selected);
    assert(!graph.prepare(valid.root, valid.accessors, 2, limits, diagnostic));
    assert(core::StringView(diagnostic) == "ASSET-GLB-NODE-SCENE");
    // glTF permits excess inverse-bind matrices; only a short accessor fails.
    Fixture extraMatrices;
    member(member(extraMatrices.root, "buffers").arrayValue[0], "byteLength") = parse("128");
    member(member(extraMatrices.root, "bufferViews").arrayValue[0], "byteLength") = parse("128");
    member(member(extraMatrices.root, "accessors").arrayValue[0], "count") = parse("2");
    extraMatrices.binary.resize(128);
    assert(extraMatrices.accessors.prepare(
        extraMatrices.root, extraMatrices.binary, {128, 1, 1, 256, 2, 32}, diagnostic));
    assert(graph.prepare(extraMatrices.root, extraMatrices.accessors, 0, limits, diagnostic));
    verify(graph);
    member(member(extraMatrices.root, "accessors").arrayValue[0], "type") = parse("\"VEC4\"");
    assert(extraMatrices.accessors.prepare(
        extraMatrices.root, extraMatrices.binary, {128, 1, 1, 256, 2, 32}, diagnostic));
    assert(!graph.prepare(extraMatrices.root, extraMatrices.accessors, 0, limits, diagnostic));
    assert(core::StringView(diagnostic) == "ASSET-GLB-NODE-INVERSE-BIND");
    verify(graph);
    if (verbose)
        std::printf(
            "animated graph: %u malformed inputs, 16 limit/budget cases, exact peak and quota rollback PASS\n",
            negatives);
}

void trackContracts(bool verbose = false)
{
    using Table = GlbAnimationTrackTable;
    const Table::Limits trackLimits{4, 4, 4, 16, 16, 64, 4096};
    TrackFixture valid;
    Table table;
    const char* diagnostic = nullptr;
    assert(table.prepare(valid.root, valid.accessors, valid.graph, trackLimits, diagnostic));
    const auto* retained = table.tracks().data();
    auto verifyTracks = [&] {
        assert(table.clips().size() == 1 && table.tracks().size() == 1);
        assert(table.clips()[0].durationSeconds == 1 && table.clips()[0].trackCount == 1);
        assert(table.tracks()[0].node == 1 && table.tracks()[0].input == 1
            && table.tracks()[0].output == 2
            && table.tracks()[0].target == Table::Target::Rotation);
    };
    unsigned negatives = 0;
    auto reject = [&](auto mutate, const char* expected) {
        TrackFixture input;
        mutate(input);
        assert(!table.prepare(input.root, input.accessors, input.graph, trackLimits, diagnostic));
        assert(core::StringView(diagnostic) == expected && table.tracks().data() == retained);
        verifyTracks();
        ++negatives;
    };
    for (const auto* mode : {"\"STEP\"", "\"CUBICSPLINE\""})
        reject(
            [&](auto& f) {
                Fixture::set(f.sampler(), "interpolation", parse(mode));
            },
            "ASSET-GLB-ANIMATION-INTERPOLATION-UNSUPPORTED");
    for (const auto* field : {"input", "output"})
        for (const auto* index : {"0", "9"})
            reject(
                [&](auto& f) {
                    member(f.sampler(), field) = parse(index);
                },
                "ASSET-GLB-ANIMATION-ACCESSOR");
    reject(
        [](auto& f) {
            member(f.channel(), "sampler") = parse("1");
        },
        "ASSET-GLB-ANIMATION-SAMPLER");
    reject(
        [](auto& f) {
            member(f.target(), "node") = parse("9");
        },
        "ASSET-GLB-ANIMATION-NODE");
    reject(
        [](auto& f) {
            member(f.target(), "path") = parse("\"weights\"");
        },
        "ASSET-GLB-ANIMATION-TARGET-UNSUPPORTED");
    reject(
        [](auto& f) {
            Fixture::set(f.node(1), "matrix", parse("[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]"));
        },
        "ASSET-GLB-ANIMATION-MATRIX-TARGET");
    reject(
        [](auto& f) {
            member(f.animation(), "channels").arrayValue.push_back(f.channel());
        },
        "ASSET-GLB-ANIMATION-DUPLICATE-TARGET");
    for (const auto* field : {"samplers", "channels"})
        reject(
            [&](auto& f) {
                member(f.animation(), field).arrayValue.clear();
            },
            "ASSET-GLB-ANIMATION-SCHEMA");
    reject(
        [](auto& f) {
            member(member(f.root, "accessors").arrayValue[2], "count") = parse("1");
            f.rebuild();
        },
        "ASSET-GLB-ANIMATION-ACCESSOR");
    reject(
        [](auto& f) {
            member(member(f.root, "accessors").arrayValue[2], "type") = parse("\"VEC3\"");
            f.rebuild();
        },
        "ASSET-GLB-ANIMATION-ACCESSOR");
    for (const auto time : {-1.f, 0.f, float(INFINITY), float(NAN)})
        reject(
            [&](auto& f) {
                f.real(4, time);
            },
            "ASSET-GLB-ANIMATION-TIME");
    reject(
        [](auto& f) {
            auto& output = member(f.root, "accessors").arrayValue[2];
            member(output, "componentType") = parse("5123");
            Fixture::set(output, "normalized", parse("true"));
            f.rebuild();
        },
        "ASSET-GLB-ANIMATION-ACCESSOR");
    reject(
        [](auto& f) {
            f.real(0, 2);
        },
        "ASSET-GLB-ANIMATION-TIME");
    for (const auto value : {float(INFINITY), float(NAN)})
        reject(
            [&](auto& f) {
                f.real(36, value);
            },
            "ASSET-GLB-ANIMATION-VALUE");
    reject(
        [](auto& f) {
            f.real(36, 0);
        },
        "ASSET-GLB-ANIMATION-VALUE");
    TrackFixture twoClips;
    member(twoClips.root, "animations").arrayValue.push_back(twoClips.animation());
    for (auto field : {&Table::Limits::maxClips,
             &Table::Limits::maxSamplers,
             &Table::Limits::maxTracks,
             &Table::Limits::maxTimeKeys,
             &Table::Limits::maxOutputKeys,
             &Table::Limits::maxValueScalars,
             &Table::Limits::maxWorkingBytes}) {
        auto bad = trackLimits;
        bad.*field = 0;
        assert(!table.prepare(valid.root, valid.accessors, valid.graph, bad, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-ANIMATION-LIMITS");
        bad.*field = 1;
        assert(!table.prepare(twoClips.root, twoClips.accessors, twoClips.graph, bad, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-ANIMATION-BUDGET");
    }
    auto exact = trackLimits;
    exact.maxWorkingBytes = table.ownedBytes() * 2 + sizeof(std::uint64_t);
    --exact.maxWorkingBytes;
    assert(!table.prepare(valid.root, valid.accessors, valid.graph, exact, diagnostic));
    assert(table.tracks().data() == retained);
    ++exact.maxWorkingBytes;
    assert(table.prepare(valid.root, valid.accessors, valid.graph, exact, diagnostic));
    verifyTracks();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!table.prepare(valid.root, valid.accessors, valid.graph, trackLimits, diagnostic));
        assert(core::StringView(diagnostic) == "ASSET-GLB-ANIMATION-ALLOCATION");
    }
    assert(!quota->liveBytes());
    verifyTracks();
    Fixture::set(valid.sampler(), "interpolation", parse("\"LINEAR\""));
    assert(table.prepare(valid.root, valid.accessors, valid.graph, trackLimits, diagnostic));
    // Distinct targets and single-frame clips are legal source plans.
    member(valid.animation(), "channels").arrayValue.push_back(valid.channel());
    member(member(member(valid.animation(), "channels").arrayValue[1], "target"), "node") =
        parse("3");
    assert(table.prepare(valid.root, valid.accessors, valid.graph, trackLimits, diagnostic));
    assert(table.tracks().size() == 2);
    member(member(valid.root, "accessors").arrayValue[1], "count") = parse("1");
    member(member(valid.root, "accessors").arrayValue[2], "count") = parse("1");
    valid.rebuild();
    assert(table.prepare(valid.root, valid.accessors, valid.graph, trackLimits, diagnostic));
    assert(table.clips()[0].durationSeconds == 0);
    assert(
        table.prepare(twoClips.root, twoClips.accessors, twoClips.graph, trackLimits, diagnostic));
    assert(table.clips().size() == 2 && table.tracks().size() == 2);
    // Vector targets consume VEC3/FLOAT, independently of quaternion validation.
    member(member(valid.root, "accessors").arrayValue[2], "type") = parse("\"VEC3\"");
    valid.rebuild();
    for (const auto* path : {"\"translation\"", "\"scale\""}) {
        for (auto& channel : member(valid.animation(), "channels").arrayValue)
            member(member(channel, "target"), "path") = parse(path);
        assert(table.prepare(valid.root, valid.accessors, valid.graph, trackLimits, diagnostic));
        assert(table.tracks()[0].target != Table::Target::Rotation);
    }
    if (verbose)
        std::printf(
            "animation tracks: %u malformed cases, 14 limit/budget cases, rollback and LINEAR default/explicit PASS\n",
            negatives);
}

void corpus(const std::filesystem::path& path)
{
    auto quota = core::makeShared<core::CpuMemoryQuota>(64 * 1024 * 1024);
    {
        core::CpuMemoryQuotaScope scope(quota);
        const auto bytes = core::readBinaryFile(path, 2 * 1024 * 1024);
        assert(bytes.success);
        GlbChunks chunks;
        core::pmr::String diagnostic;
        assert(
            readGlbChunks({reinterpret_cast<const char*>(bytes.bytes.data()), bytes.bytes.size()},
                chunks,
                diagnostic));
        JsonValue root;
        assert(parseJson(chunks.json, root, {32, 262144, false}));
        const auto surfaceIssue =
            GlbSceneSourceContract::validate(root, chunks.bin, false, GlbLayoutProfile::Animated);
        if (surfaceIssue)
            std::fprintf(stderr, "animated surface: %s\n", surfaceIssue);
        assert(!surfaceIssue);
        assert(GlbSceneSourceContract::validate(root, chunks.bin, false) != nullptr);
        auto& primitive = member(member(root, "meshes").arrayValue[0], "primitives").arrayValue[0];
        auto& attributes = member(primitive, "attributes");
        const auto jointsBinding = member(attributes, "JOINTS_0");
        const auto weightsBinding = member(attributes, "WEIGHTS_0");
        member(attributes, "JOINTS_0") = parse("4294967295");
        assert(
            GlbSceneSourceContract::validate(root, chunks.bin, false, GlbLayoutProfile::Animated));
        member(attributes, "JOINTS_0") = weightsBinding;
        assert(
            GlbSceneSourceContract::validate(root, chunks.bin, false, GlbLayoutProfile::Animated));
        member(attributes, "JOINTS_0") = jointsBinding;
        member(attributes, "WEIGHTS_0") = jointsBinding;
        assert(
            GlbSceneSourceContract::validate(root, chunks.bin, false, GlbLayoutProfile::Animated));
        member(attributes, "WEIGHTS_0") = weightsBinding;
        const auto indicesBinding = member(primitive, "indices");
        auto& firstAnimation = member(root, "animations").arrayValue[0];
        auto& firstSampler = member(firstAnimation, "samplers").arrayValue[0];
        member(primitive, "indices") = member(firstSampler, "input");
        assert(
            GlbSceneSourceContract::validate(root, chunks.bin, false, GlbLayoutProfile::Animated));
        member(primitive, "indices") = indicesBinding;
        assert(
            !GlbSceneSourceContract::validate(root, chunks.bin, false, GlbLayoutProfile::Animated));
        GlbAccessorTable accessors;
        const char* issue = nullptr;
        assert(accessors.prepare(root,
            chunks.bin,
            {2 * 1024 * 1024, 3000, 3000, 3000 * sizeof(GlbAccessorSpan), 200000, 1000000},
            issue));
        Graph graph;
        // Explicit fixture profile: 67 nodes, 62 joints and four mesh instances.
        assert(graph.prepare(root, accessors, 0, {128, 128, 2, 16, 4, 256, 16, 65536}, issue));
        assert(
            graph.nodes().size() == 67 && graph.skins().size() == 1 && graph.joints().size() == 62);
        std::size_t instances = 0;
        for (const auto& node : graph.nodes()) {
            assert(node.selected);
            if (node.mesh != Graph::absent) {
                ++instances;
                assert(node.skin == 0);
            }
        }
        assert(instances == 4 && graph.order().front() == 66);
        GlbAnimationTrackTable tracks;
        assert(tracks.prepare(
            root, accessors, graph, {32, 3000, 3000, 65536, 65536, 262144, 256 * 1024}, issue));
        assert(tracks.clips().size() == 24 && tracks.tracks().size() == 2696);
        GlbSkeletalSource decoded;
        assert(decoded.prepare(root,
            graph,
            accessors,
            tracks,
            {256, 32, 3000, 65536, 4 * 1024 * 1024, 8 * 1024 * 1024, .00001f},
            issue));
        assert(decoded.skeleton().jointCount() == 129 && decoded.clips().size() == 24);
        for (const auto& clip : decoded.clips()) {
            core::pmr::Vector<animation::JointTransform> sampled(decoded.bindPose().size());
            auto local = decoded.bindPose();
            for (std::size_t i = 0; i < local.size(); ++i)
                sampled[i] = {local[i].translation, local[i].rotation, local[i].scale};
            assert(animation::sampleClip(clip, clip.durationSeconds * .25f, sampled));
            for (std::size_t i = 0; i < local.size(); ++i)
                local[i] = {sampled[i].translation, sampled[i].rotation, sampled[i].scale};
            const auto palette = animation::buildSkinMatrices(
                decoded.skeleton(), animation::buildModelPose(decoded.skeleton(), local));
            for (const auto& matrix : palette)
                for (const auto value : matrix.m)
                    assert(std::isfinite(value));
        }
        std::printf(
            "actual skeletal source: 129 runtime joints, 24 decoded clips, logical %zu bytes\n",
            decoded.decodedBytes());
        std::printf("actual character tracks: 24 clips, 2696 tracks, owned %zu bytes\n",
            tracks.ownedBytes());
        std::printf(
            "actual character graph: 67 nodes, 62 joints, 4 instances, owned %zu bytes, core peak %llu\n",
            graph.ownedBytes(),
            (unsigned long long)quota->peakBytes());
    }
    assert(!quota->liveBytes());
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && core::StringView(argv[1]) == "--corpus") {
        corpus(std::filesystem::u8path(argv[2]));
        return 0;
    }
    contracts(true);
    trackContracts(true);
    const auto before = core::defaultCpuAllocator().stats();
    const auto cycles = argc > 1 ? 20 : 1;
    for (int i = 0; i < cycles; ++i) {
        contracts();
        trackContracts();
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::printf("ordinary %d graph cycles: core bytes %llu -> %llu, live %llu -> %llu\n",
        cycles,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
