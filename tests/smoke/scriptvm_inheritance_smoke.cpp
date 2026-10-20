// ArpScript class inheritance smoke test.
//
// Exercises inherited fields and methods, overriding, derived-to-base
// assignment, and runtime method dispatch through the full script pipeline.

#include "scriptvm/interpreter/eval.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cstdio>

using namespace arp::scriptvm;

int main()
{
    const char* sourceText = "class Importer:\n"
                             "    sourceValue: int\n"
                             "    def __init__(self, value: int):\n"
                             "        self.sourceValue = value\n"
                             "    def source_value(self) -> int:\n"
                             "        return self.sourceValue\n"
                             "    def read(self) -> int:\n"
                             "        return self.sourceValue\n"
                             "class ModelImporter(Importer):\n"
                             "    localValue: int\n"
                             "    def __init__(self, value: int):\n"
                             "        self.sourceValue = value\n"
                             "        self.localValue = 5\n"
                             "    def read(self) -> int:\n"
                             "        return self.sourceValue + self.localValue\n"
                             "    def total(self) -> int:\n"
                             "        return self.source_value() + self.read()\n"
                             "def test() -> int:\n"
                             "    var base: Importer = ModelImporter(37)\n"
                             "    var model: ModelImporter = ModelImporter(10)\n"
                             "    return base.read() + model.total()\n";

    auto compiled = arp::scriptvm::test::buildInterpreter(
        sourceText, "scriptvm_inheritance_smoke.arp", "inheritance-smoke");
    if (!compiled.ok()) {
        std::printf("SMOKE FAIL: compile error: %s (line %d, col %d)\n",
            compiled.error.message.c_str(),
            compiled.error.line,
            compiled.error.column);
        return 1;
    }

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    auto result = runFunctionByName(compiled.program, "test", args);
    if (!result.ok()) {
        std::printf("SMOKE FAIL: VM error: %s\n", result.error.message.c_str());
        return 1;
    }

    int value = 0;
    if (!tryGetInt(result.value, value) || value != 67) {
        std::printf("SMOKE FAIL: inheritance result=%d expected=67\n", value);
        return 1;
    }

    std::printf("SMOKE OK: inheritance result=%d\n", value);
    return 0;
}
