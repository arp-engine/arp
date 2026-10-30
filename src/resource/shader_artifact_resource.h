#pragma once

#include "resource/resource_manager.h"
#include "shader/generated_shader_artifact_set.h"
#include "shader/graphics_shader_module_artifact.h"
#include "shader/original_depth_table_expansion_artifact.h"
#include "shader/webgl_shader_program_artifact.h"

#include <variant>

namespace arp::resource {

using ShaderArtifactResourceBudget = std::variant<shader::SingleEntryShaderLoadBudget,
    shader::GraphicsShaderModuleLoadBudget,
    shader::WebGlShaderLoadBudget,
    shader::OriginalDepthTableExpansionArtifact::Budget,
    shader::GeneratedShaderArtifactSetBudget>;

struct ShaderArtifactResourceDesc {
    std::filesystem::path publication;
    ResourceFileOrigin origin;
    ShaderArtifactResourceBudget budget;
    // Required when a budget refers to authored proof requests. The owner must
    // own every referenced request and its nested data; retained by the resource.
    core::SharedPtr<const void> configurationOwner;
};

// CPU acquisition and immutable artifact custody only. Consumers still pass
// the original artifact to the existing RHI contract and execution gates.
class ARP_API ShaderArtifactResource final : public ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept { return "ShaderArtifactResource"; }

    ShaderArtifactResource(ResourceReference reference, ShaderArtifactResourceDesc desc);

    ResourceLoadResult load(ResourceManager&, const ResourceLoadContext&) override;
    bool planFileReadIntents(ResourceManager&,
        const ResourceLoadContext&,
        core::pmr::Vector<ResourceFileReadIntent>&,
        core::pmr::String&) const override;
    std::size_t processedByteCount() const noexcept override;
    void appendContentFingerprint(core::StableHash64&) const noexcept override;

    const auto& singleEntry() const noexcept { return prepared_.single; }

    const auto& graphicsModule() const noexcept { return prepared_.graphics; }

    const auto& webGlProgram() const noexcept { return prepared_.webgl; }

    const auto& depthTableExpansion() const noexcept { return prepared_.depth; }

    const auto& generatedSet() const noexcept { return prepared_.generated; }

private:
    struct Prepared {
        core::SharedPtr<const shader::SingleEntryShaderArtifact> single;
        core::SharedPtr<const shader::GraphicsShaderModuleArtifact> graphics;
        core::SharedPtr<const shader::WebGlShaderProgramArtifact> webgl;
        core::SharedPtr<const shader::OriginalDepthTableExpansionArtifact> depth;
        core::SharedPtr<const shader::GeneratedShaderArtifactSet> generated;

        bool ready() const noexcept { return single || graphics || webgl || depth || generated; }
    };

    Prepared prepare(const ResourceLoadContext&,
        core::pmr::Vector<ResourceFileReadIntent>*,
        core::pmr::String&) const;
    ShaderArtifactResourceDesc desc_;
    Prepared prepared_;
};

} // namespace arp::resource
