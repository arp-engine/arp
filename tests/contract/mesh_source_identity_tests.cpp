#include "assets/mesh_section_grouping.h"
#include "assets/mesh_source_identity.h"
#include "assets/triangle_surface_digest.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>

namespace {
using namespace arp;

template <class F> void rejects(F&& function, const char* diagnostic)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::exception& error) {
        rejected = std::strstr(error.what(), diagnostic) != nullptr;
    }
    assert(rejected);
}

class Fixture {
public:
    Fixture()
        : path(std::filesystem::temp_directory_path()
              / ("arp-source-identity-"
                  + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                  + ".glb"))
    {}

    ~Fixture() { std::filesystem::remove(path); }

    void write(bool duplicateOccurrence = false, bool changedCount = false)
    {
        std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":60}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
{"buffer":0,"byteOffset":36,"byteLength":24}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"},
{"bufferView":1,"componentType":5125,"count":6,"type":"SCALAR"}],
"materials":[{"name":"zero"},{"name":"one"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":)";
        json += changedCount ? "2" : "1";
        json += R"(,"material":1},{"attributes":{"POSITION":0},"indices":1,"material":0}]}],
"nodes":[{"translation":[5,7,11],"children":[1,2]},
{"name":"asymmetric","mesh":0,"matrix":[0,2,0,0,-3,0,0,0,0,0,-4,0,13,17,19,1]},
{"name":"second-instance","mesh":0,"translation":[-2,0,1]}],"scenes":[{"nodes":[0)";
        json += duplicateOccurrence ? ",1" : "";
        json += "]}],\"scene\":0}";
        while (json.size() % 4)
            json += ' ';
        std::ofstream out(path, std::ios::binary);
        const auto u32 = [&](std::uint32_t value) {
            const char bytes[] = {static_cast<char>(value),
                static_cast<char>(value >> 8),
                static_cast<char>(value >> 16),
                static_cast<char>(value >> 24)};
            out.write(bytes, sizeof(bytes));
        };
        u32(0x46546c67);
        u32(2);
        u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + 60));
        u32(static_cast<std::uint32_t>(json.size()));
        u32(0x4e4f534a);
        out.write(json.data(), json.size());
        u32(60);
        u32(0x004e4942);
        for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) {
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            u32(bits);
        }
        for (auto value : {0u, 1u, 2u, 0u, 1u, 2u})
            u32(value);
        assert(out.good());
    }

    std::filesystem::path path;
};

void digestContract()
{
    float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0};
    std::uint32_t indices[] = {0, 1, 2};
    const auto digest = assets::triangleSurfaceDigest(positions, 4, indices, 3);
    // Independently serialized with Python struct.pack('<I9f', ...).
    assert(core::Sha256::matchesRawHexDigest(
        digest, "78a348d3b618ebab50f796eeee1bdb317b4d1750bfa2e6cf9b9f3fea1f7a0b3d"));
    indices[1] = 3;
    assert(assets::triangleSurfaceDigest(positions, 4, indices, 3) == digest);
    indices[2] = 0;
    assert(assets::triangleSurfaceDigest(positions, 4, indices, 3) != digest);
    indices[2] = 4;
    rejects(
        [&] {
            assets::triangleSurfaceDigest(positions, 4, indices, 3);
        },
        "INDEX-INVALID");
    indices[2] = 2;
    positions[0] = std::numeric_limits<float>::infinity();
    rejects(
        [&] {
            assets::triangleSurfaceDigest(positions, 4, indices, 3);
        },
        "NONFINITE");
    rejects(
        [&] {
            assets::triangleSurfaceDigest(nullptr, 4, indices, 3);
        },
        "INPUT-INVALID");
    rejects(
        [&] {
            assets::triangleSurfaceDigest(positions, 4, indices, 2);
        },
        "INPUT-INVALID");
}

void importAndBind()
{
    Fixture fixture;
    fixture.write();
    auto identified = assets::importStaticMeshWithSourceIdentityFromGlbFile(fixture.path);
    assert(identified.geometry.success && identified.identityVersion == 1);
    assert(identified.identities.size() == 4);
    core::ByteBuffer fileDigest;
    assert(core::Sha256::digestFile(fixture.path, fileDigest));
    assert(
        std::equal(fileDigest.begin(), fileDigest.end(), identified.sourceContentDigest.begin()));
    const assets::StaticMeshSourceImportLimits validLimits{
        std::filesystem::file_size(fixture.path), 12, 12, 4};
    assert(assets::importStaticMeshWithSourceIdentityFromGlbFile(fixture.path, validLimits)
            .geometry.success);
    const auto rejectLimit = [&](assets::StaticMeshSourceImportLimits limits,
                                 const char* diagnostic) {
        const auto result =
            assets::importStaticMeshWithSourceIdentityFromGlbFile(fixture.path, limits);
        assert(!result.geometry.success && result.identities.empty() && !result.identityVersion);
        assert(result.geometry.diagnostics == diagnostic);
    };
    auto limited = validLimits;
    --limited.maxSourceBytes;
    rejectLimit(limited, "GLB-SOURCE-IDENTITY-SOURCE-READ-FAILED");
    limited = validLimits;
    --limited.maxVertices;
    rejectLimit(limited, "GLB-SOURCE-IDENTITY-GEOMETRY-BUDGET-EXCEEDED");
    limited = validLimits;
    --limited.maxIndices;
    rejectLimit(limited, "GLB-SOURCE-IDENTITY-GEOMETRY-BUDGET-EXCEEDED");
    limited = validLimits;
    --limited.maxPrimitives;
    rejectLimit(limited, "GLB-SOURCE-IDENTITY-PRIMITIVE-BUDGET-EXCEEDED");
    const auto legacy = assets::importStaticMeshFromGlbFile(fixture.path);
    assert(legacy.success);
    assert(legacy.mesh.indices == identified.geometry.mesh.indices);
    assert(legacy.mesh.vertices.size() == identified.geometry.mesh.vertices.size());
    for (std::size_t i = 0; i < legacy.mesh.vertices.size(); ++i)
        assert(std::memcmp(&legacy.mesh.vertices[i],
                   &identified.geometry.mesh.vertices[i],
                   sizeof(assets::MeshVertex))
            == 0);
    const float affine[] = {0, -3, 0, 18, 2, 0, 0, 24, 0, 0, -4, 30, 0, 0, 0, 1};
    for (unsigned i = 0; i < 16; ++i)
        assert(identified.identities[0].worldAffine[i] == affine[i]);
    const auto& identity = identified.identities[0];
    assert(identity.node == 1 && identity.mesh == 0 && identity.primitive == 0);
    assert(identity.localAbsolutePosition[0] == 1 && identity.localAbsolutePosition[1] == 1
        && identity.localAbsolutePosition[2] == 0);
    assert(identified.geometry.mesh.vertices[1].position[0] == 18);
    assert(identified.geometry.mesh.vertices[1].position[1] == 26);
    assert(identified.geometry.mesh.vertices[2].position[0] == 15);
    auto& mesh = identified.geometry.mesh;
    assets::groupMeshSectionsByMaterial(mesh, true);
    const auto binding = assets::bindMeshSourceIdentities(mesh, identified.identities);
    assert((binding == core::pmr::Vector<std::uint32_t>{1, 3, 0, 2}));
    auto repacked = mesh;
    std::reverse(repacked.vertices.begin(), repacked.vertices.end());
    for (auto& index : repacked.indices)
        index = static_cast<std::uint32_t>(repacked.vertices.size() - 1 - index);
    for (std::size_t i = 0; i < mesh.sourceRanges.size(); ++i)
        assets::validateExactMeshSourceRangeAttributes(
            mesh, mesh.sourceRanges[i], repacked, repacked.sourceRanges[i]);
    repacked.vertices[repacked.indices[0]].uv[0] += 1;
    rejects(
        [&] {
            assets::validateExactMeshSourceRangeAttributes(
                mesh, mesh.sourceRanges[0], repacked, repacked.sourceRanges[0]);
        },
        "ATTRIBUTES-MISMATCH");
    auto wrongCount = repacked.sourceRanges[0];
    wrongCount.indices.indexCount += 3;
    rejects(
        [&] {
            assets::validateExactMeshSourceRangeAttributes(
                mesh, mesh.sourceRanges[0], repacked, wrongCount);
        },
        "COUNT-MISMATCH");
    auto malformed = identified.identities;
    malformed[1].node = malformed[0].node;
    malformed[1].primitive = malformed[0].primitive;
    rejects(
        [&] {
            assets::bindMeshSourceIdentities(mesh, malformed);
        },
        "PARTITION-INVALID");
    malformed = identified.identities;
    malformed[1].sourceIndices.firstIndex += 3;
    rejects(
        [&] {
            assets::bindMeshSourceIdentities(mesh, malformed);
        },
        "PARTITION-INVALID");
    malformed = identified.identities;
    malformed[0].sourceIndices.materialSlot = 0;
    rejects(
        [&] {
            assets::bindMeshSourceIdentities(mesh, malformed);
        },
        "RANGE-MISMATCH");
    malformed.pop_back();
    rejects(
        [&] {
            assets::bindMeshSourceIdentities(mesh, malformed);
        },
        "COUNT-INVALID");
    mesh.sourceRanges[0].sourceFirstIndex = mesh.sourceRanges[1].sourceFirstIndex;
    rejects(
        [&] {
            assets::bindMeshSourceIdentities(mesh, identified.identities);
        },
        "RANGE-MISSING");

    fixture.write(false, true);
    auto changed = assets::importStaticMeshWithSourceIdentityFromGlbFile(fixture.path);
    assert(changed.geometry.success);
    assets::groupMeshSectionsByMaterial(changed.geometry.mesh, true);
    const auto changedBinding =
        assets::bindMeshSourceIdentities(changed.geometry.mesh, changed.identities);
    assert(changedBinding == binding);
    assert(changed.identities[1].sourceIndices.firstIndex == 6);
    assert(identified.identities[1].sourceIndices.firstIndex == 3);
    assert(assets::meshSourceIdentityKey(changed.identities[1])
        == assets::meshSourceIdentityKey(identified.identities[1]));

    fixture.write(true);
    const auto repeated = assets::importStaticMeshWithSourceIdentityFromGlbFile(fixture.path);
    assert(!repeated.geometry.success && repeated.identities.empty() && !repeated.identityVersion);
    assert(repeated.geometry.diagnostics == "GLB-SOURCE-IDENTITY-REPEATED-OCCURRENCE");
    assert(assets::importStaticMeshFromGlbFile(fixture.path).success);
}
} // namespace

int main()
{
    digestContract();
    importAndBind();
}
