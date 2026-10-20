#pragma once

#include "scriptvm/interpreter/pipeline.h"

namespace arp::scriptvm::test {

inline core::pmr::String fixtureHash(char digit)
{
    core::pmr::String value("sha256:", &core::defaultCpuMemoryResource());
    value.append(64u, digit);
    return value;
}

inline ScriptInterpreterBuildResult buildInterpreter(core::pmr::String source,
    core::pmr::String filename,
    core::pmr::String moduleId,
    ModuleRegistry* moduleRegistry = nullptr,
    core::pmr::String entryFunctionId = {})
{
    ScriptInterpreterBuildRequest request;
    request.source = std::move(source);
    request.filename = std::move(filename);
    request.moduleId = std::move(moduleId);
    request.moduleVersion = 1u;
    request.dependencyGraphHash = fixtureHash('0');
    request.reflectionRegistryHash = fixtureHash('1');
    request.entryFunctionId = std::move(entryFunctionId);
    request.moduleRegistry = moduleRegistry;
    return ScriptInterpreterPipeline::build(request);
}

inline ScriptInterpreterBuildResult buildInterpreter(const char* source,
    const char* filename,
    const char* moduleId,
    ModuleRegistry* moduleRegistry = nullptr,
    const char* entryFunctionId = "")
{
    auto& resource = core::defaultCpuMemoryResource();
    return buildInterpreter(core::pmr::String(source, &resource),
        core::pmr::String(filename, &resource),
        core::pmr::String(moduleId, &resource),
        moduleRegistry,
        core::pmr::String(entryFunctionId, &resource));
}

} // namespace arp::scriptvm::test
