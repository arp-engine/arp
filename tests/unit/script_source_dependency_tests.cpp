#include "core/io/file_io.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tools/asset_importer/script_runner.h"
#include "tools/asset_importer/tool_script_source_host.h"

#include <iostream>
#include <stdexcept>

#ifndef ARP_SOURCE_DIR
#define ARP_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const char* diagnostic)
{
    if (!value)
        throw std::runtime_error(diagnostic);
}

auto leaf(int value)
{
    std::string source;
    for (int i = 0; i < 1000; ++i)
        source += "const ID_" + std::to_string(i) + ": int = " + std::to_string(i) + "\n";
    source +=
        "\nclass SharedValue:\n    value: int\n\n    def __init__(self):\n        self.value = "
        + std::to_string(value)
        + "\n\n    def read(self) -> int:\n        return self.value\n\n"
          "def read_value() -> int:\n    return SharedValue().read()\n";
    return source;
}
} // namespace

int main()
{
    try {
        unified_io_fixture::Workspace workspace;
        workspace.write("leaf.arp", leaf(7));
        // A Fibonacci-shaped import DAG has exponentially many paths but
        // only 22 source modules. Repeated class/constant declarations must
        // still have one identity in the resulting program.
        for (int i = 0; i < 21; ++i) {
            std::string source =
                i == 0 ? "import leaf\n" : "import branch_" + std::to_string(i - 1) + "\n";
            if (i > 1)
                source += "import branch_" + std::to_string(i - 2) + "\n";
            source += "\nclass Node_" + std::to_string(i)
                + ":\n    value: int\n\n    def __init__(self):\n        self.value = 0\n";
            if (i == 20)
                source += "\ndef graph_value() -> int:\n    return SharedValue().read()\n";
            workspace.write("branch_" + std::to_string(i) + ".arp", source);
        }
        workspace.write("entry.arp",
            "import branch_20\n\ndef main() -> int:\n    return branch_20.graph_value()\n");
        const std::filesystem::path engine(ARP_SOURCE_DIR);
        arp::tools::asset_importer::ScriptToolRunner runner(
            workspace.root, engine / "modules", workspace.root / "cache", {}, {}, workspace.root);
        require(!arp::tools::asset_importer::ToolScriptSourceHost::prepareNative({}, workspace.root)
                    .sources
                && !arp::tools::asset_importer::ToolScriptSourceHost::prepareNative(
                    workspace.root / "entry.arp", {})
                    .sources,
            "Empty native source bindings must not select the process working directory");
        const auto run = [&](int expected) {
            const auto result = runner.runFile(workspace.root / "entry.arp");
            require(result.success, result.diagnostics.c_str());
            int actual = 0;
            require(arp::scriptvm::tryGetInt(result.value, actual) && actual == expected,
                "Shared dependency returned stale or duplicate class state");
        };
        run(7);
        const auto prepared = arp::tools::asset_importer::ToolScriptSourceHost::prepareNative(
            workspace.root / "entry.arp", workspace.root);
        require(bool(prepared.sources), prepared.diagnostics.c_str());
        workspace.write("leaf.arp", leaf(11));
        const auto held = runner.runPreparedFunction(*prepared.sources, "main");
        int original = 0;
        require(held.success && arp::scriptvm::tryGetInt(held.value, original) && original == 7,
            "Prepared source reopened a changed dependency after IO retirement");
        run(11);
        workspace.write("branch_0.arp", "import branch_20\n");
        const auto cycle = runner.runFile(workspace.root / "entry.arp");
        require(!cycle.success
                && cycle.diagnostics.find("circular source module import") != std::string::npos,
            "Prepared dependency cache masked an import cycle");
        workspace.write("branch_0.arp", "import leaf\n\nclass Node_0:\n    value: int\n");
        workspace.write("leaf.arp", "class Broken {\n");
        require(!runner.runFile(workspace.root / "entry.arp").success,
            "Prepared dependency cache masked a parser failure");
        workspace.write("leaf.arp", leaf(13));
        run(13);
        workspace.write(
            "missing_entry.arp", "import ghost\n\ndef main() -> int:\n    return ghost.answer()\n");
        const auto absent = arp::tools::asset_importer::ToolScriptSourceHost::prepareNative(
            workspace.root / "missing_entry.arp", workspace.root);
        require(absent.sources && absent.sources->find("ghost")->absent,
            "Missing source was not an explicit Absent observation");
        workspace.write("ghost.arp", "def answer() -> int:\n    return 31\n");
        require(!runner.runPreparedFunction(*absent.sources, "main").success,
            "Prepared Absent observation reopened a newly created source");
        const auto fresh = runner.runFile(workspace.root / "missing_entry.arp");
        int freshValue = 0;
        require(
            fresh.success && arp::scriptvm::tryGetInt(fresh.value, freshValue) && freshValue == 31,
            "Next source operation did not observe the newly created file");
        const auto small = arp::tools::asset_importer::ToolScriptSourceHost::prepareNative(
            workspace.root / "missing_entry.arp", workspace.root);
        require(bool(small.sources), small.diagnostics.c_str());
        const auto preparedRun = [&] {
            const auto result = runner.runPreparedFunction(*small.sources, "main");
            require(result.success, result.diagnostics.c_str());
        };
        preparedRun();
        const auto before = arp::core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            preparedRun();
        const auto after = arp::core::defaultCpuAllocator().stats();
        require(before.liveBytes == after.liveBytes
                && before.allocationCount - before.deallocationCount
                    == after.allocationCount - after.deallocationCount,
            "Prepared tool Interpreter lifecycle did not return to the core baseline");
        std::cout << "Prepared tool core " << before.liveBytes << "/"
                  << before.allocationCount - before.deallocationCount << " -> " << after.liveBytes
                  << "/" << after.allocationCount - after.deallocationCount << '\n';
        std::cout
            << "Source dependency graph: shared DAG, source changes, cycles and recovery passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
