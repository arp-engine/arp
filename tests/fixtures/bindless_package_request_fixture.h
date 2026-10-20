#pragma once
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/descriptor_set_validation.h"

#include <cassert>

namespace arp::test {
class BindlessPackageRequestFixture final {
public:
    static rhi::BindlessRequirements make(
        const rhi::BindlessShaderArtifact& artifact, std::uint64_t deviceIdentity)
    {
        rhi::BindlessRequirements request;
        request.optIn = true;
        request.allowEmulated = true;
        request.deviceIdentity = deviceIdentity;
        request.shaderPackageIdentity = artifact.shaderPackageIdentity;
        std::uint32_t descriptors = 0u;
        assert(rhi::countDescriptorElements(artifact.physicalLayout, descriptors).success);
        request.budget = {1u, descriptors, artifact.snapshotBytes, artifact.switchCases};
        for (const auto& table : artifact.tables) {
            const auto* binding =
                rhi::findDescriptorSetBinding(artifact.physicalLayout, table.firstBinding);
            assert(binding);
            request.tables.push_back({table.tableId,
                *binding,
                table.capacity,
                table.operations,
                table.nonUniform,
                table.bufferLayout});
        }
        return request;
    }
};
} // namespace arp::test
