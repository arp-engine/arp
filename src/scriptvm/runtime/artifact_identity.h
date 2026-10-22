#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstdint>

namespace arp::scriptvm {

// Immutable build identity shared by Interpreter bytecode and AOT artifacts.
// Module id/version alone are intentionally insufficient for Hybrid selection.
struct ScriptArtifactIdentity {
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 0;
    std::uint32_t semanticAbiVersion = 0;
    std::uint32_t typedIrVersion = 0;
    core::pmr::String sourceHash{&core::defaultCpuMemoryResource()};
    core::pmr::String dependencyGraphHash{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
    core::pmr::String typedIrFingerprint{&core::defaultCpuMemoryResource()};

    bool complete() const
    {
        return !moduleId.empty() && moduleVersion != 0u && semanticAbiVersion != 0u
            && typedIrVersion != 0u && !sourceHash.empty() && !dependencyGraphHash.empty()
            && !reflectionRegistryHash.empty() && !typedIrFingerprint.empty();
    }

    bool matches(const ScriptArtifactIdentity& other) const
    {
        return complete() && other.complete() && moduleId == other.moduleId
            && moduleVersion == other.moduleVersion
            && semanticAbiVersion == other.semanticAbiVersion
            && typedIrVersion == other.typedIrVersion && sourceHash == other.sourceHash
            && dependencyGraphHash == other.dependencyGraphHash
            && reflectionRegistryHash == other.reflectionRegistryHash
            && typedIrFingerprint == other.typedIrFingerprint;
    }
};

} // namespace arp::scriptvm
