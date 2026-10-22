#include "assets/glb_importer_common.h"
#include "assets/glb_static_mesh_importer.h"
#include "tests/fixtures/static_glb_geometry.h"

#include <chrono>
#include <cstdio>

int main(int argc, char** argv)
{
    using namespace arp;
    const auto path = argc == 2 ? std::filesystem::path(argv[1])
                                : std::filesystem::temp_directory_path()
            / ("arp-strict-glb-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + ".glb");
    tests::StaticGlbGeometryFixture::write(path);
    assets::StaticMeshSourceImportLimits limits{65536u, 3u, 3u, 1u, true, 16u, 512u};
    const auto imported = assets::importStaticMeshWithSourceIdentityFromGlbFile(path, limits);
    if (!imported.geometry.success)
        std::fprintf(stderr, "%s\n", imported.geometry.diagnostics.c_str());
    assert(imported.geometry.success && imported.identityVersion == 1u);
    assert(imported.geometry.mesh.vertices.size() == 3u
        && imported.geometry.mesh.indices.size() == 3u);
    assert(imported.geometry.mesh.vertices[0].position[0] == 1.25f
        && imported.geometry.mesh.vertices[2].position[2] == -0.75f);
    assert(imported.geometry.mesh.vertices[2].uv[1] == 1.0f
        && imported.geometry.mesh.vertices[1].tangent[3] == 1.0f);
    assert(imported.geometry.materials.empty() && imported.geometry.images.empty());
    if (argc == 2) {
        std::filesystem::remove(path);
        assert(imported.geometry.mesh.vertices[0].position[0] == 1.25f);
        return 0;
    }
    const auto reject = [&](std::string from, std::string to) {
        auto json = tests::StaticGlbGeometryFixture::document();
        json.replace(json.find(from), from.size(), to);
        tests::StaticGlbGeometryFixture::write(path, json);
        const auto failed = assets::importStaticMeshWithSourceIdentityFromGlbFile(path, limits);
        assert(!failed.geometry.success && !failed.geometry.diagnostics.empty());
    };
    reject("\"mesh\":0", "\"mesh\":0,\"translation\":[0,0,0]");
    reject("\"NORMAL\":1,", "");
    reject("\"TANGENT\":3", "\"COLOR_0\":3");
    reject("\"byteLength\":36", "\"byteLength\":35");
    reject("\"byteLength\":150", "\"byteLength\":150,\"uri\":\"external.bin\"");
    reject("\"count\":3", "\"count\":4294967295");
    reject("\"scene\":0", "\"scene\":0,\"scene\":0");
    reject("\"scene\":0", "\"scene\":0,\"skins\":[]");
    reject("\"external-slot-zero\"", "\"\\u0061\"");
    reject("\"componentType\":5126", "\"componentType\":5123");
    reject("\"buffer\":0", "\"buffer\":1");
    reject("\"material\":0", "\"material\":1");
    tests::StaticGlbGeometryFixture::write(path);
    for (auto field : {0u, 1u, 2u, 3u, 4u, 5u}) {
        auto small = limits;
        if (field == 0u)
            small.maxVertices = 2u;
        if (field == 1u)
            small.maxIndices = 2u;
        if (field == 2u)
            small.maxPrimitives = 0u;
        if (field == 3u)
            small.maxSourceBytes = 32u;
        if (field == 4u)
            small.maxJsonDepth = 2u;
        if (field == 5u)
            small.maxJsonValues = 2u;
        assert(
            !assets::importStaticMeshWithSourceIdentityFromGlbFile(path, small).geometry.success);
    }
    assets::detail::JsonValue value;
    for (const char* invalid : {"{\"x\":01}",
             "{\"x\":+1}",
             "{\"x\":.1}",
             "{\"x\":1e}",
             "{\"x\":nan}",
             "{\"x\":1,\"x\":2}",
             "\"\\"})
        assert(!assets::detail::parseJson(invalid, value, {16u, 512u, true}));
    assert(assets::detail::parseJson("{\"x\":1.25e2}", value, {16u, 512u, true}));
    std::filesystem::remove(path);
    std::puts("Strict GLB geometry/schema/byte ranges/budget contracts passed");
}
