#include "reflection/serialization.h"
#include "resource/vegetation_catalog_resource.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
class CatalogFixture final {
    std::filesystem::path folder_;
    bool owned_;

public:
    CatalogFixture()
        : folder_(std::filesystem::temp_directory_path()
              / ("arp-catalog-serialization-"
                  + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          owned_(std::filesystem::create_directory(folder_))
    {
        assert(owned_);
    }

    ~CatalogFixture()
    {
        if (owned_)
            std::filesystem::remove_all(folder_);
    }

    static std::string document(
        unsigned version, const std::string& assets, bool runtime = false, const char* extra = "")
    {
        return "type: VegetationSourceCatalog\nfields:\n  contractVersion: "
            + std::to_string(version) + "\n  runtimeCatalog: runtime.yaml\n  assets: " + assets
            + "\n"
            + (runtime ? "  imported:\n    type: VegetationCatalogImportedInfo\n"
                         "    fields: {format: glb, generatedBy: catalog-contract-test}\n"
                       : "")
            + extra;
    }

    auto load(const std::string& source, const std::string& runtime)
    {
        for (const auto& item :
            {std::pair{"source.yaml", &source}, std::pair{"runtime.yaml", &runtime}}) {
            std::ofstream file(folder_ / item.first);
            file << *item.second;
            assert(file.good());
        }
        arp::resource::ResourceManager manager;
        arp::resource::VegetationCatalogResourceLoadDesc desc;
        desc.catalogPath = folder_ / "source.yaml";
        desc.resourceId = "b63cee36-81cb-469f-a56e-0dc2921c0fb2";
        return arp::resource::loadVegetationCatalogResource(manager, desc);
    }
};

void fails(const arp::resource::VegetationCatalogResourceLoadResult& result, const char* diagnostic)
{
    assert(!result.success);
    assert(result.diagnostics.find(diagnostic) != std::string::npos);
}
} // namespace

int main()
{
    CatalogFixture fixture;
    const auto emptySource = CatalogFixture::document(2, "[]");
    const auto emptyRuntime = CatalogFixture::document(2, "[]", true);
    // This domain diagnostic proves both documents decoded despite omitted
    // optional metadata. It must not fail in Authoring reflection traversal.
    fails(fixture.load(emptySource, emptyRuntime), "catalog contains no assets");
    const auto authoring = arp::reflection::deserialize_from_yaml(emptySource);
    assert(!authoring.success);
    assert(authoring.diagnostics.find("unknown or non-serializable") != std::string::npos);
    fails(fixture.load(CatalogFixture::document(3, "[]"), emptyRuntime),
        "unsupported vegetation catalog contract version: 3");
    fails(fixture.load(emptySource, CatalogFixture::document(1, "[]", true)),
        "contract version mismatch");
    fails(fixture.load(CatalogFixture::document(2, "[]", false, "  typo: true\n"), emptyRuntime),
        "unknown or non-serializable");
    fails(fixture.load("type: VegetationCatalogAssetRecord\nfields: {}\n", emptyRuntime),
        "root type mismatch");
    const auto wrongRecord =
        CatalogFixture::document(2, "[{type: VegetationCatalogSourceInfo, fields: {}}]", true);
    fails(fixture.load(wrongRecord, wrongRecord), "assets require VegetationCatalogAssetRecord");
    std::string invalidScalar = emptySource;
    invalidScalar.replace(invalidScalar.find("contractVersion: 2"),
        std::string("contractVersion: 2").size(),
        "contractVersion: nope");
    fails(fixture.load(invalidScalar, emptyRuntime), "parse failed");
    const std::string asset = "[{type: VegetationCatalogAssetRecord, fields: {id: sample, "
                              "source: fixture.glb, role: canopy, category: canopy, "
                              "heightClass: tall}}]";
    fails(
        fixture.load(CatalogFixture::document(2, asset), CatalogFixture::document(2, asset, true)),
        "requires a valid templateCategory");
    std::string excessive = "[";
    for (unsigned n = 0; n < 4097; ++n) {
        if (n)
            excessive += ',';
        excessive += "{type: VegetationCatalogAssetRecord, fields: {}}";
    }
    excessive += ']';
    fails(fixture.load(CatalogFixture::document(2, excessive), emptyRuntime),
        "collection length exceeds budget");
}
