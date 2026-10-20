#include "scriptvm/interpreter/eval.h"
#include "scriptvm/interpreter/pipeline.h"

#include <cstdio>

int main()
{
    using namespace arp::scriptvm;
    ScriptInterpreterBuildRequest request;
    request.source = "var base: int = 40\n"
                     "def answer() -> int:\n"
                     "    return base + 2\n";
    request.filename = "interpreter-pipeline-smoke.arp";
    request.moduleId = "interpreter-pipeline-smoke";
    request.moduleVersion = 1u;
    request.dependencyGraphHash =
        "sha256:1111111111111111111111111111111111111111111111111111111111111111";
    request.reflectionRegistryHash =
        "sha256:2222222222222222222222222222222222222222222222222222222222222222";
    request.entryFunctionId = "arp-fn:interpreter-pipeline-smoke/answer";

    auto built = ScriptInterpreterPipeline::build(request);
    if (!built.ok()) {
        std::fprintf(stderr, "pipeline build failed: %s\n", built.error.message.c_str());
        return 1;
    }
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    const auto executed = runFunctionByName(built.program, "answer", args);
    int value = 0;
    if (!executed.ok() || !tryGetInt(executed.value, value) || value != 42) {
        std::fprintf(stderr, "pipeline execution failed: %s\n", executed.error.message.c_str());
        return 1;
    }
    std::puts("ScriptVM interpreter pipeline smoke passed");
    return 0;
}
