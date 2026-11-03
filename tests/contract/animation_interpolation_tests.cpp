#include "animation/animation.h"
#include "assets/avatar_retargeter.h"
#include "core/diagnostics/diagnostic_error.h"
#include "core/io/file_io.h"
#include "core/math/math.h"
#include "tests/fixtures/prefab_runtime_package_fixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::tests::prefab_runtime_package_fixture;
using Mode = animation::ChannelInterpolation;

void near(float actual, float expected)
{
    assert(std::isfinite(actual) && std::abs(actual - expected) < .00001f);
}

animation::AnimationClip rotation(Mode mode = Mode::GltfLinear)
{
    auto asset = makeRuntimeAsset(Mode::GltfLinear);
    auto clip = std::move(asset.clips.front());
    clip.channels.front().interpolation = mode;
    return clip;
}

void mathContracts()
{
    const auto max = std::numeric_limits<float>::max();
    const auto tiny = std::numeric_limits<float>::denorm_min();
    core::Quat out{1, 2, 3, 4};
    for (const auto q :
        {core::Quat{0, 0, 0, 0}, core::Quat{NAN, 0, 0, 1}, core::Quat{0, INFINITY, 0, 1}}) {
        assert(!core::tryNormalize(q, out));
        assert(out.x == 1 && out.y == 2 && out.z == 3 && out.w == 4);
    }
    for (const auto size : {tiny, max}) {
        assert(core::tryNormalize({0, size, 0, size}, out));
        near(out.y, .70710678f);
        near(out.w, .70710678f);
    }
    near(core::lerpStable(-max, max, .5f), 0);
    assert(core::lerpStable(max, max, .75f) == max);
    assert(core::lerpStable(-max, max, 0) == -max);
    assert(core::lerpStable(-max, max, 1) == max);
}

void samplingContracts()
{
    auto gltf = rotation();
    auto legacy = rotation(Mode::LegacyLinear);
    core::pmr::Vector<animation::JointTransform> pose(1);
    assert(animation::sampleClip(legacy, .25f, pose));
    near(pose[0].rotation.y, .31622777f);
    const auto before = core::defaultCpuAllocator().stats();
    for (int step = 0; step <= 100; ++step) {
        const auto t = step / 100.f;
        assert(animation::sampleClip(gltf, t, pose));
        near(pose[0].rotation.y, std::sin(t * std::acos(-1.f) / 2));
        near(pose[0].rotation.w, std::cos(t * std::acos(-1.f) / 2));
    }
    assert(before.allocationCount == core::defaultCpuAllocator().stats().allocationCount);
    assert(animation::sampleClip(gltf, -10, pose));
    near(pose[0].rotation.w, 1);
    assert(animation::sampleClip(gltf, 10, pose));
    near(pose[0].rotation.y, 1);

    auto& keys = gltf.channels[0].keyframes;
    // Equivalent signs and non-unit magnitudes take the same shortest arc.
    keys[0].transform.rotation = {0, 0, 0, 2};
    keys[1].transform.rotation = {0, -2, 0, -2};
    assert(animation::sampleClip(gltf, .5f, pose));
    near(pose[0].rotation.y, .38268343f);
    keys[1].transform.rotation = {0, 0, 0, -2};
    assert(animation::sampleClip(gltf, .5f, pose));
    near(std::abs(pose[0].rotation.w), 1);
    for (const auto magnitude :
        {std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max()}) {
        keys[0].transform.rotation = {0, magnitude, 0, magnitude};
        keys[1].transform.rotation = keys[0].transform.rotation;
        assert(animation::sampleClip(gltf, 0, pose));
        near(pose[0].rotation.y, .70710678f);
        assert(animation::sampleClip(gltf, .5f, pose));
        near(pose[0].rotation.y, .70710678f);
    }
    gltf = rotation();
    auto translation = gltf.channels[0];
    translation.targetPath = "translation";
    translation.keyframes[0].transform.translation = {-8, 2, 4};
    translation.keyframes[1].transform.translation = {8, 6, 12};
    gltf.channels.push_back(translation);
    auto scale = translation;
    scale.targetPath = "scale";
    scale.keyframes[0].transform.scale = {1, 2, 3};
    scale.keyframes[1].transform.scale = {3, 6, 9};
    gltf.channels.push_back(scale);
    assert(animation::sampleClip(gltf, .25f, pose));
    near(pose[0].translation.x, -4);
    near(pose[0].translation.y, 3);
    near(pose[0].translation.z, 6);
    near(pose[0].scale.x, 1.5f);
    near(pose[0].scale.y, 3);
    near(pose[0].scale.z, 4.5f);
    near(pose[0].rotation.y, .38268343f);

    animation::AnimationStateMachine machine;
    assert(machine.play(gltf));
    machine.advance(.25f);
    near(machine.blendedPose()[0].rotation.y, .38268343f);
    auto invalid = gltf;
    invalid.channels.back().interpolation = static_cast<Mode>(255);
    assert(!machine.play(invalid) && !machine.transitionTo(invalid, 1));
    near(machine.currentTimeSeconds(), .25f);
    assert(machine.currentStateName() == gltf.name);
}

void failureContracts()
{
    auto base = rotation();
    core::pmr::Vector<animation::JointTransform> pose(1);
    pose[0].translation = {7, 8, 9};
    pose[0].rotation = {1, 2, 3, 4};
    auto fails = [&](const animation::AnimationClip& clip, const char* diagnostic) {
        assert(core::StringView(animation::validateClipSampling(clip)) == diagnostic);
        assert(!animation::sampleClip(clip, .5f, pose));
        assert(pose[0].translation.x == 7 && pose[0].rotation.w == 4);
    };
    auto invalid = base;
    invalid.channels[0].interpolation = static_cast<Mode>(255);
    fails(invalid, "ANIMATION-INTERPOLATION-UNKNOWN");
    for (const auto duration : {-1.f, float(INFINITY), float(NAN)}) {
        invalid = base;
        invalid.durationSeconds = duration;
        fails(invalid, "ANIMATION-GLTF-LINEAR-DURATION");
    }
    invalid = base;
    invalid.channels[0].targetPath = "weights";
    fails(invalid, "ANIMATION-GLTF-LINEAR-TARGET");
    invalid = base;
    invalid.channels[0].keyframes.clear();
    fails(invalid, "ANIMATION-GLTF-LINEAR-KEYFRAMES");
    for (const auto time : {-1.f, 0.f, 2.f, float(INFINITY), float(NAN)}) {
        invalid = base;
        invalid.channels[0].keyframes[1].timeSeconds = time;
        fails(invalid, "ANIMATION-GLTF-LINEAR-TIME");
    }
    for (const auto q : {animation::Quat{0, 0, 0, 0},
             animation::Quat{NAN, 0, 0, 1},
             animation::Quat{0, INFINITY, 0, 1}}) {
        invalid = base;
        invalid.channels[0].keyframes[1].transform.rotation = q;
        fails(invalid, "ANIMATION-GLTF-LINEAR-VALUE");
    }
    invalid = base;
    invalid.channels[0].targetPath = "translation";
    invalid.channels[0].keyframes[1].transform.translation.x = NAN;
    fails(invalid, "ANIMATION-GLTF-LINEAR-VALUE");
    for (const auto time : {float(NAN), float(INFINITY)})
        assert(!animation::sampleClip(base, time, pose) && pose[0].rotation.w == 4);
    // A late legacy out-of-range channel must not leave earlier glTF writes.
    invalid = base;
    invalid.channels.push_back(rotation(Mode::LegacyLinear).channels[0]);
    invalid.channels.back().jointIndex = 2;
    assert(!animation::sampleClip(invalid, .25f, pose) && pose[0].rotation.w == 4);
    invalid.channels[0].jointIndex = 2;
    assert(!animation::sampleClip(invalid, .25f, pose) && pose[0].rotation.w == 4);
}

void retargetContracts()
{
    const auto asset = makeRuntimeAsset();
    assets::AvatarAsset avatar;
    avatar.kind = assets::AvatarKind::Generic;
    auto retarget = [&](animation::AnimationClip clip) {
        return assets::retargetClipToSkeleton(std::move(clip),
            asset.skeleton,
            asset.bindPose,
            asset.skeleton,
            asset.bindPose,
            avatar);
    };
    for (const auto mode : {Mode::LegacyLinear, Mode::GltfLinear}) {
        auto result = retarget(rotation(mode));
        assert(!result.channels.empty());
        for (const auto& channel : result.channels)
            assert(channel.interpolation == mode);
        core::pmr::Vector<animation::JointTransform> pose(1);
        assert(animation::sampleClip(result, .25f, pose));
        near(pose[0].rotation.y, mode == Mode::GltfLinear ? .38268343f : .31622777f);
    }
    auto expectDiagnostic = [&](animation::AnimationClip clip, const char* diagnostic) {
        bool rejected = false;
        try {
            retarget(std::move(clip));
        } catch (const core::DiagnosticError& error) {
            rejected = core::StringView(error.what()) == diagnostic;
        }
        assert(rejected);
    };
    auto invalid = rotation();
    invalid.channels[0].interpolation = static_cast<Mode>(255);
    expectDiagnostic(invalid, "ANIMATION-INTERPOLATION-UNKNOWN");
    invalid = rotation();
    invalid.channels[0].jointIndex = 2;
    expectDiagnostic(invalid, "ANIMATION-RETARGET-JOINT");
    // Humanoid retargeting bakes multiple inputs into one output channel.
    avatar.kind = assets::AvatarKind::Humanoid;
    assets::HumanBoneMapping hips;
    hips.sourceBoneName = "root";
    hips.boneName = "root";
    hips.humanName = "Hips";
    avatar.humanDescription.humanBones.push_back(hips);
    assets::HumanSkeletonNode root;
    root.name = "root";
    avatar.humanDescription.skeletonNodes.push_back(root);
    for (const auto mode : {Mode::LegacyLinear, Mode::GltfLinear}) {
        const auto baked = retarget(rotation(mode));
        assert(!baked.channels.empty());
        for (const auto& channel : baked.channels)
            assert(channel.interpolation == mode);
        core::pmr::Vector<animation::JointTransform> pose(1);
        assert(animation::sampleClip(baked, .25f, pose));
        near(pose[0].rotation.y, mode == Mode::GltfLinear ? .38268343f : .31622777f);
    }
    invalid = rotation();
    invalid.channels.push_back(rotation(Mode::LegacyLinear).channels[0]);
    expectDiagnostic(invalid, "ANIMATION-RETARGET-MIXED-INTERPOLATION");
}

void packageContracts(const std::filesystem::path& root)
{
    auto asset = makeRuntimeAsset(Mode::GltfLinear);
    const auto controller = makeController();
    const auto written = resource::writePrefabRuntimePayload(describe(root, asset, controller));
    assert(written.success);
    auto bytes = core::readBinaryFile(root / "blobs/prefab-runtime.arpa.bin", 65536);
    assert(bytes.success && bytes.bytes[4] == 5);
    auto metadata = written.resource;
    const core::PoolHandle mount{&asset, 0, 1};
    const resource::ResourceFileOrigin origin{core::io::FilePathScope::MountedResource, mount};
    auto decode = [&](const core::pmr::Vector<std::uint8_t>& blob) {
        // Re-sign the envelope so malformed wire tests reach the decoder.
        std::uint32_t hash = 2166136261u;
        for (auto byte : blob) {
            hash ^= byte;
            hash *= 16777619u;
        }
        metadata.blobHash = static_cast<int>(hash);
        metadata.blobByteSize = static_cast<int>(blob.size());
        auto entry = makeEntry(kRuntimePayloadId,
            "PrefabRuntimePayloadResource",
            "runtime",
            "resources/runtime-payload.arpres.yaml",
            {},
            metadata);
        resource::ResourceSourceSet sources;
        resource::ResourceSourceBuffer source;
        source.path = "snapshot/blobs/prefab-runtime.arpa.bin";
        source.pathScope = origin.pathScope;
        source.mount = mount;
        source.bytes = blob;
        assert(sources.add(std::move(source)));
        resource::AssetPackageSourceContext context{{{}, &sources, true, 65536}, origin};
        return resource::PrefabRuntimePayloadSourceReader().readSources(entry, "snapshot", context);
    };
    const auto result = decode(bytes.bytes);
    assert(result.success && result.hasCompleteControllerData);
    assert(
        result.controllerAsset.states.size() == 2 && result.controllerAsset.parameters.size() == 1);
    assert(result.controllerAsset.transitions[0].conditions[0].parameter == "moving");
    core::pmr::Vector<animation::JointTransform> pose(1);
    assert(result.asset.clips[0].channels[0].interpolation == Mode::GltfLinear);
    assert(animation::sampleClip(result.asset.clips[0], .25f, pose));
    near(pose[0].rotation.y, .38268343f);

    for (auto& clip : asset.clips)
        for (auto& channel : clip.channels)
            channel.interpolation = Mode::LegacyLinear;
    assert(resource::writePrefabRuntimePayload(describe(root, asset, controller)).success);
    auto legacy = core::readBinaryFile(root / "blobs/prefab-runtime.arpa.bin", 65536);
    assert(legacy.success && legacy.bytes.size() == bytes.bytes.size());
    unsigned differences = 0;
    for (std::size_t i = 0; i < bytes.bytes.size(); ++i) {
        if (bytes.bytes[i] == legacy.bytes[i])
            continue;
        ++differences;
        assert(bytes.bytes[i] == 1 && legacy.bytes[i] == 0);
        auto malformed = bytes.bytes;
        malformed[i] = 255;
        const auto unknown = decode(malformed);
        assert(!unknown.success && unknown.diagnostics == "ANIMATION-INTERPOLATION-UNKNOWN");
        malformed.resize(i);
        assert(!decode(malformed).success);
    }
    assert(differences == asset.clips.size());
    for (unsigned version : {0u, 6u, 255u}) {
        auto malformed = bytes.bytes;
        malformed[4] = static_cast<std::uint8_t>(version);
        assert(!decode(malformed).success);
    }
    // Invalid write preparation leaves the existing blob untouched.
    asset.clips[0].channels[0].interpolation = static_cast<Mode>(255);
    const auto invalidWrite =
        resource::writePrefabRuntimePayload(describe(root, asset, controller));
    assert(!invalidWrite.success && invalidWrite.diagnostics == "ANIMATION-INTERPOLATION-UNKNOWN");
    assert(
        core::readBinaryFile(root / "blobs/prefab-runtime.arpa.bin", 65536).bytes == legacy.bytes);
}

void legacyFixture(const std::filesystem::path& root)
{
    const auto loaded = resource::loadPrefabRuntimePayloadFromPackage(root, text("prefab/runtime"));
    if (!loaded.success)
        std::fprintf(stderr, "%s\n", loaded.diagnostics.c_str());
    assert(loaded.success && loaded.hasCompleteControllerData);
    assert(
        loaded.controllerAsset.states.size() == 2 && loaded.controllerAsset.parameters.size() == 1);
    assert(loaded.controllerAsset.transitions[0].conditions[0].parameter == "moving");
    for (const auto& clip : loaded.asset.clips)
        for (const auto& channel : clip.channels)
            assert(channel.interpolation == Mode::LegacyLinear);
}
} // namespace

int main(int argc, char** argv)
{
    resource::registerPrefabAssetReflection();
    assert(argc >= 2);
    const auto legacy = std::filesystem::u8path(argv[1]);
    const auto temporary = std::filesystem::path(
        argc > 2 ? "animation-interpolation-smoke-output" : "animation-interpolation-output");
    mathContracts();
    samplingContracts();
    failureContracts();
    retargetContracts();
    legacyFixture(legacy);
    packageContracts(temporary);
    const auto before = core::defaultCpuAllocator().stats();
    const int cycles = argc > 2 ? 20 : 1;
    for (int i = 0; i < cycles; ++i) {
        samplingContracts();
        failureContracts();
        retargetContracts();
        packageContracts(temporary);
        legacyFixture(legacy);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::filesystem::remove_all(temporary);
    std::printf("interpolation: %d ordinary cycles, core bytes %llu -> %llu, live %llu -> %llu\n",
        cycles,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
