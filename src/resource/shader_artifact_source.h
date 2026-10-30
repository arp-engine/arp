#pragma once

#include "resource/resource_source_io.h"
#include "shader/shader_artifact_files.h"

namespace arp::resource {

// Owner-borrowed projection shared by resource and renderer artifact loaders.
// Reads are pure; missing bytes become explicit FileService intents.
class AcquiredShaderArtifactSource final : public shader::ShaderArtifactSource {
public:
    AcquiredShaderArtifactSource(const ResourceLoadContext& context,
        ResourceFileOrigin origin,
        core::pmr::Vector<ResourceFileReadIntent>* pending)
        : context_(context), origin_(origin), pending_(pending)
    {}

    shader::ShaderArtifactSourceResult read(
        const std::filesystem::path& path, std::uint64_t maxBytes) const override
    {
        shader::ShaderArtifactSourceResult result;
        const auto name = core::FilePath::utf8(path);
        core::io::FileReadRequest request;
        request.contractVersion = 1;
        request.path = name;
        request.pathScope = origin_.pathScope;
        request.mount = origin_.mount;
        request.maxBytes = maxBytes;
        if (core::io::FileReadContract::validate(request).status
            != core::io::FileIoStatus::Success) {
            result.diagnostic = "SHADER-IO-SOURCE-PATH";
            return result;
        }
        const auto* source = context_.sources ? context_.sources->find(name) : nullptr;
        if (!source) {
            if (pending_) {
                core::pmr::String diagnostic;
                if (!appendResourceReadIntent(name, maxBytes, *pending_, diagnostic, origin_)) {
                    result.diagnostic = "SHADER-IO-SOURCE-PATH";
                    return result;
                }
                pending_->back().role = "shader-artifact";
            }
            result.diagnostic = "SHADER-IO-SOURCE-MISSING";
        } else if (source->pathScope != origin_.pathScope || !(source->mount == origin_.mount))
            result.diagnostic = "SHADER-IO-SOURCE-LOCATION";
        else if (source->bytes.empty() || source->bytes.size() > maxBytes)
            result.diagnostic = "SHADER-IO-SOURCE-BUDGET";
        else
            result.bytes = source->bytes;
        return result;
    }

private:
    const ResourceLoadContext& context_;
    ResourceFileOrigin origin_;
    core::pmr::Vector<ResourceFileReadIntent>* pending_ = nullptr;
};

} // namespace arp::resource
