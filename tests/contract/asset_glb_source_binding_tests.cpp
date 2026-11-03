#include "modules/asset/asset_module.h"
#include "modules/io/io_module.h"
#include "tests/fixtures/animated_mesh_source_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
namespace binding = scriptvm::plugins::asset;
namespace io = scriptvm::plugins::io;

binding::GlbSourceOptions options()
{
    // One explicit test profile. Native and script profiles have no defaults.
    const auto n = tests::AnimatedMeshSourceFixture::limits();
    binding::GlbSourceOptions o;
    o.maxSourceBytes = n.maxSourceBytes;
    o.maxImportBytes = n.maxImportBytes;
    o.maxDecodedBytes = n.maxDecodedBytes;
    o.maxSourceLabelBytes = 64;
    o.maxJsonDepth = n.maxJsonDepth;
    o.maxJsonValues = n.maxJsonValues;
    o.selectedScene = n.selectedScene;
    o.maxBufferViews = n.layout.maxBufferViews;
    o.maxAccessors = n.layout.maxAccessors;
    o.maxElements = n.layout.maxElements;
    o.maxScalars = n.layout.maxScalars;
    o.maxDescriptorBytes = n.layout.maxDescriptorBytes;
    o.maxNodes = n.scene.maxNodes;
    o.maxEdges = n.scene.maxEdges;
    o.maxScenes = n.scene.maxScenes;
    o.maxSceneRoots = n.scene.maxSceneRoots;
    o.maxSkins = n.scene.maxSkins;
    o.maxSkinJoints = n.scene.maxSkinJoints;
    o.maxMeshInstances = n.scene.maxMeshInstances;
    o.maxGraphWorkingBytes = n.scene.maxWorkingBytes;
    o.maxClips = n.animation.maxClips;
    o.maxSamplers = n.animation.maxSamplers;
    o.maxChannels = n.animation.maxChannels;
    o.maxTimeKeys = n.animation.maxTimeKeys;
    o.maxOutputKeys = n.animation.maxOutputKeys;
    o.maxValueScalars = n.animation.maxValueScalars;
    o.maxAnimationWorkingBytes = n.animation.maxWorkingBytes;
    o.maxJoints = n.skeleton.maxJoints;
    o.maxKeyframes = n.skeleton.maxKeyframes;
    o.maxNameBytes = n.skeleton.maxNameBytes;
    o.matrixElementError = n.skeleton.matrixElementError;
    o.maxVertices = n.geometry.maxVertices;
    o.maxIndices = n.geometry.maxIndices;
    o.maxPrimitives = n.geometry.maxPrimitives;
    o.colorPolicy = "RejectColor0";
    o.tangentPolicy = "AuthoredOrMaterialUnused";
    o.unitVectorError = n.geometry.unitVectorError;
    o.orthogonalityError = n.geometry.orthogonalityError;
    o.weightSumError = n.geometry.weightSumError;
    o.maxMaterials = n.materials.maxMaterials;
    o.maxTextures = n.materials.maxTextures;
    o.maxImages = n.materials.maxImages;
    o.maxMaterialMetadataBytes = n.materials.maxMetadataBytes;
    o.maxEncodedImageBytes = n.materials.maxEncodedImageBytes;
    return o;
}

void cycle()
{
    auto o = options();
    assert(o.validate().empty());
    assets::AnimatedMeshSourceImportLimits native;
    assert(!o.project(native) && native.scene.maxSkinJoints == 16
        && native.geometry.maxVertices == 64);
    const auto rejected = [&](binding::GlbSourceOptions invalid) {
        assert(invalid.project(native));
        assert(native.scene.maxSkinJoints == 16 && native.geometry.maxVertices == 64);
    };
    rejected({});
    o.selectedScene = -1;
    rejected(o);
    o = options();
    o.maxSourceBytes = -1;
    rejected(o);
    o = options();
    o.maxDecodedBytes = 0;
    rejected(o);
    o = options();
    o.tangentPolicy = "Guess";
    rejected(o);
    o = options();
    o.colorPolicy = "Guess";
    rejected(o);
    o = options();
    o.unitVectorError = INFINITY;
    rejected(o);
    o = options();
    o.maxSourceLabelBytes = o.maxImportBytes + 1;
    rejected(o);
    o = options();
    o.maxDescriptorBytes = INT64_MAX;
    if (sizeof(std::size_t) == 4)
        rejected(o);
    o = options();

    auto source = scriptvm::makeBytes(tests::AnimatedMeshSourceFixture::bytes());
    auto imported = binding::importGlbBytes("not-a-host-file", source, o);
    assert(imported.success && imported.asset.mesh.vertices.size() == 9
        && imported.asset.skeleton.jointCount() == 7 && !imported.sourceDiagnosticId);
    const auto point = imported.sampledVertex(0, .5f, 0);
    assert(point.available && std::abs(point.positionX + 26.5f) < .001f);
    assert(core::inspectCpuAllocationCharge(imported.asset.mesh.vertices.data()).quota);
    assert(!binding::importGlbBytes("x", scriptvm::makeInt(1), o).success);
    scriptvm::ScriptBytes borrowed(tests::AnimatedMeshSourceFixture::bytes());
    auto bad = binding::importGlbBytes("x", scriptvm::Value::native_reference(&borrowed), o);
    assert(!bad.success && bad.diagnosticId() == "SCRIPT-GLB-SOURCE-BYTES-TYPE");
    o.maxImportBytes = 1;
    o.maxSourceLabelBytes = 1;
    bad = binding::importGlbBytes("x", source, o);
    assert(!bad.success && bad.diagnosticId() == "ASSET-ANIMATED-SOURCE-IMPORT-BUDGET");
    o = options();
    auto quota = core::makeShared<core::CpuMemoryQuota>(o.maxImportBytes + 1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        auto failure = binding::importGlbBytes("x", source, o);
        assert(!failure.success && failure.diagnosticId() == "ASSET-ANIMATED-SOURCE-QUOTA-LIMIT");
    }
    assert(!quota->liveBytes());
}

void acquisition()
{
    unified_io_fixture::Workspace workspace;
    const auto bytes = tests::AnimatedMeshSourceFixture::bytes();
    workspace.write(
        u8"模型.glb", core::pmr::String(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const auto path = core::FilePath::utf8(workspace.root / std::filesystem::u8path(u8"模型.glb"));
    auto acquired = io::readBinaryLocal(path, 65536, 131072);
    assert(acquired.success && acquired.byteCount() == static_cast<std::int64_t>(bytes.size()));
    auto retained = acquired.data();
    acquired = {};
    const auto* owner = scriptvm::checkedOwnedValuePayload<scriptvm::ScriptBytes>(retained);
    assert(owner && owner->data == bytes);
    assert(core::inspectCpuAllocationCharge(owner->data.data()).quota);
    auto imported = binding::importGlbBytes("retained-source", retained, options());
    assert(imported.success);
    for (auto cap : {std::int64_t(-1), std::int64_t(0), std::int64_t(1)}) {
        acquired = io::readBinaryLocal(path, cap, 131072);
        assert(!acquired.success && !acquired.byteCount() && !acquired.diagnostics.empty());
    }
    acquired = io::readBinaryLocal(path, 65536, 1);
    assert(!acquired.success && !acquired.byteCount() && !acquired.diagnostics.empty());
    const core::pmr::String embeddedNull("valid\0suffix", 12);
    acquired = io::readBinaryLocal(embeddedNull, 65536, 131072);
    assert(!acquired.success && acquired.diagnostics == "SCRIPT-IO-BINARY-PATH");
    const core::pmr::String invalidUtf8(1, char(0xff));
    acquired = io::readBinaryLocal(invalidUtf8, 65536, 131072);
    assert(!acquired.success && acquired.diagnostics == "SCRIPT-IO-BINARY-PATH");
}
} // namespace

int main(int argc, char**)
{
    cycle();
    acquisition();
    const auto before = core::defaultCpuAllocator().stats();
    for (int i = 0; i < (argc > 1 ? 20 : 1); ++i) {
        cycle();
        acquisition();
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Script binding ordinary cycles: core bytes %llu -> %llu, live %llu -> %llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
