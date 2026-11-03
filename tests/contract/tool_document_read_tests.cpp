#include "tests/fixtures/unified_io_fixture.h"
#include "tools/asset_importer/document_import.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace {
using namespace arp;
using namespace tools::asset_importer;

void read(const std::filesystem::path& directory)
{
    DocumentImportOptions options;
    options.material = core::FilePath::utf8(directory / "material.txt");
    options.maxDocumentBytes = 4096;
    std::ostringstream output, error;
    assert(DocumentImporter{}.run(options, output, error) == 0);
    assert(output.str() == "material ok: RoutedMaterial\n" && error.str().empty());
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    workspace.write("material.txt",
        "\xef\xbb\xbf{\r\n\"type\":\"SceneStylizedPbr\",\"name\":\"RoutedMaterial\","
        "\"baseColorTexture\":\"declared.ktx2\"}\r\n");
    read(workspace.root);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = arp::core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            read(workspace.root);
        const auto after = arp::core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Document read core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else {
        DocumentImportOptions options;
        std::ostringstream output, error;
        assert(DocumentImporter{}.run(options, output, error) == 2);
        assert(error.str().find("TOOL-DOCUMENT-OPTIONS") != std::string::npos);
        options.material = core::FilePath::utf8(workspace.root / "missing.txt");
        assert(DocumentImporter{}.run(options, output, error) == 1);
        options.material.clear();
        options.legacyLayout = "not-read-without-opt-in";
        assert(DocumentImporter{}.run(options, output, error) == 2);
        assert(error.str().find("LEGACY-JSON-OPT-IN-REQUIRED") != std::string::npos);
    }
    std::puts("PASS original material parser via bounded FileService text and DLL allocator");
}
