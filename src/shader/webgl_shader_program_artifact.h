#pragma once

#include "shader/single_entry_shader_artifact.h"
#include "shader/webgl_shader_limits.h"

namespace arp::shader {

struct WebGlShaderLoadBudget {
    SingleEntryShaderLoadBudget original;
    std::uint32_t maxLeaves = 0, maxInterfaces = 0, maxTypeNodes = 0;
};

struct WebGlUniformLeaf {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    std::uint32_t offset = 0, size = 0, components = 0, columns = 0, matrixStride = 0;
    bool integer = false, required = true;
};

struct WebGlUniformBlock {
    bool push = false;
    std::uint32_t set = 0, binding = 0, size = 0;
    core::pmr::Vector<WebGlUniformLeaf> leaves{&core::defaultCpuMemoryResource()};
};

struct WebGlShaderInterface {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    std::uint32_t location = 0, components = 0;
};

struct WebGlSamplerPair {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    std::uint32_t textureSet = 0, textureBinding = 0, samplerSet = 0, samplerBinding = 0;
};

struct WebGlShaderTarget {
    std::shared_ptr<const SingleEntryShaderArtifact> original;
    core::ByteBuffer glsl{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGlUniformBlock> blocks{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGlShaderInterface> inputs{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGlShaderInterface> outputs{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGlSamplerPair> pairs{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> extensions{&core::defaultCpuMemoryResource()};
    std::uint32_t uniformVectors = 0;
};

class ARP_API WebGlShaderProgramArtifact final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGlShaderProgramArtifact;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    explicit WebGlShaderProgramArtifact(ConstructionKey) {}

    static std::shared_ptr<const WebGlShaderProgramArtifact> load(const std::filesystem::path&,
        const WebGlShaderLoadBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&);

    const WebGlShaderTarget& vertex() const noexcept { return stages_[0]; }

    const WebGlShaderTarget& fragment() const noexcept { return stages_[1]; }

    const auto& textureUnits() const noexcept { return textureUnits_; }

    const auto& identity() const noexcept { return identity_; }

    const char* assess(const WebGlShaderLimits&) const noexcept;
    static const char* validateOriginal(
        const std::shared_ptr<const WebGlShaderProgramArtifact>&) noexcept;

    bool nativeExecutionProven() const noexcept { return false; }

private:
    std::array<WebGlShaderTarget, 2> stages_;
    core::pmr::Vector<core::pmr::String> textureUnits_{&core::defaultCpuMemoryResource()};
    core::ByteBuffer identity_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::shader
