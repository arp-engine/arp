#include "core/io/file_io.h"
#include "tests/fixtures/animated_mesh_source_fixture.h"
#include "tools/asset_importer/script_runner.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv)
{
    using namespace arp;
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto executableDirectory = std::filesystem::absolute(argv[0]).parent_path();
    const auto scratch = executableDirectory / "script-animated-source-fixture";
    std::filesystem::create_directories(scratch);
    const auto sourcePath = scratch / "model.glb";
    {
        const auto bytes = tests::AnimatedMeshSourceFixture::bytes();
        std::ofstream file(sourcePath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        assert(file.good());
    }
    tools::asset_importer::ScriptToolRunner runner(
        root, root / "modules", scratch / "plugin-cache", executableDirectory / "arp_engine.dll");
    core::pmr::Vector<scriptvm::Value> arguments{&core::defaultCpuMemoryResource()};
    arguments.emplace_back(scriptvm::makeString(sourcePath.generic_string()));
    arguments.emplace_back(scriptvm::makeString(root.generic_string()));
    const auto cycle = [&](const char* entry) {
        auto result = runner.runFileFunction(
            root / "tests/fixtures/asset_importer/script_glb_source.arp", entry, arguments);
        if (!result.success)
            std::fprintf(stderr, "Interpreter source: %s\n", result.diagnostics.c_str());
        assert(result.success);
        int status = -1;
        assert(scriptvm::tryGetInt(result.value, status));
        if (status)
            std::fprintf(stderr, "Interpreter source status %d\n", status);
        assert(status == 0);
    };
    cycle("main");
    // Exercise the actual tool class and its report path, rather than only
    // invoking the native functions from a fixture script.
    auto exportArguments = arguments;
    const auto reportPath = scratch / "report.yaml";
    exportArguments.emplace_back(scriptvm::makeString(reportPath.generic_string()));
    auto exported = runner.runFileFunction(
        root / "tests/fixtures/asset_importer/script_unity_import.arp", "export", exportArguments);
    if (!exported.success)
        std::fprintf(stderr, "production GLB export: %s\n", exported.diagnostics.c_str());
    assert(exported.success);
    int exportStatus = -1;
    assert(scriptvm::tryGetInt(exported.value, exportStatus) && exportStatus == 0);
    const auto report = core::readTextFile(reportPath, 65536);
    assert(report.success && report.text.find("vertexCount: 9") != core::pmr::String::npos
        && report.text.find("jointCount: 7") != core::pmr::String::npos);
    const auto before = core::defaultCpuAllocator().stats();
    cycle(argc > 1 ? "smoke" : "main");
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Interpreter GLB ordinary cycles: core bytes %llu -> %llu, live %llu -> %llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
