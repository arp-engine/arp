#include "rhi/backends/webgpu/webgpu_bindless_device_limit_plan.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/gpu_bindless.h"

#include <cassert>
#include <charconv>
#include <cstdio>
#include <limits>
#include <string_view>

#ifdef NDEBUG
#error Pipeline demand tests require assertions
#endif

using namespace arp::rhi;

namespace {
// Authored complete layout, not a substitute for selected production binaries.
class DemandFixture final {
public:
    BindlessShaderArtifact artifact;

    explicit DemandFixture(BindlessExecution execution)
    {
        artifact.execution = execution;
        artifact.pipelineInterfaceContractVersion = 1u;
        artifact.physicalLayout.name = "material27-metadata-fault";
        auto append = [](DescriptorLayoutDesc& layout, DescriptorType type, unsigned count) {
            for (unsigned i = 0; i < count; ++i) {
                DescriptorBindingDesc binding;
                binding.binding = static_cast<std::uint32_t>(layout.bindings.size());
                binding.type = type;
                binding.stages = ShaderStage::Fragment;
                binding.textureViewDimensionConstrained = type == DescriptorType::SampledTexture;
                if (binding.textureViewDimensionConstrained)
                    binding.textureViewDimension = binding.binding == 24u || binding.binding == 25u
                        ? TextureViewDimension::Texture2DArray
                        : TextureViewDimension::Texture2D;
                layout.bindings.push_back(binding);
            }
        };
        append(artifact.physicalLayout, DescriptorType::SampledTexture, 27u);
        append(artifact.physicalLayout, DescriptorType::Sampler, 1u);
        append(artifact.physicalLayout, DescriptorType::ReadOnlyStorageBuffer, 1u);
        append(artifact.physicalLayout, DescriptorType::StorageBuffer, 1u);
        BindlessPipelineDescriptorDomain frame;
        frame.set = 1u;
        frame.layout.name = "ordinary-frame";
        append(frame.layout, DescriptorType::UniformBuffer, 9u);
        frame.layout.bindings.front().stages = ShaderStage::Vertex | ShaderStage::Fragment;
        artifact.additionalDomains.push_back(frame);
        BindlessPipelineDescriptorDomain reserved;
        reserved.set = 2u;
        reserved.layout.name = "shadow-ibl-gi";
        append(reserved.layout, DescriptorType::SampledTexture, 9u);
        append(reserved.layout, DescriptorType::Sampler, 3u);
        append(reserved.layout, DescriptorType::ReadOnlyStorageBuffer, 3u);
        artifact.additionalDomains.push_back(reserved);
        BindlessPipelineDescriptorDomain push;
        push.set = 3u;
        push.layout.name = "explicit-emulated-push";
        append(push.layout, DescriptorType::UniformBuffer, 1u);
        artifact.additionalDomains.push_back(push);
    }
};

bool empty(const BindlessPipelineInterfaceDemand& value)
{
    if (value.descriptorCount || value.domainCount || value.bindGroupSlots
        || value.bindingsPerGroup)
        return false;
    for (const auto& stage : value.perStage)
        if (stage.sampledTextures || stage.samplers || stage.storageTextures || stage.uniformBuffers
            || stage.storageBuffers)
            return false;
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    for (const auto execution :
        {BindlessExecution::Validator, BindlessExecution::Native, BindlessExecution::Emulated}) {
        DemandFixture f(execution);
        BindlessPipelineInterfaceDemand demand;
        const auto before = arp::core::defaultCpuAllocator().stats();
        assert(!inspectBindlessPipelineInterfaceDemand(f.artifact, demand));
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == before.allocationCount);
        assert(demand.descriptorCount == 55u && demand.domainCount == 4u
            && demand.bindGroupSlots == 4u && demand.bindingsPerGroup == 30u);
        assert(demand.perStage[0].uniformBuffers == 1u && demand.perStage[0].sampledTextures == 0u);
        assert(demand.perStage[1].sampledTextures == 36u && demand.perStage[1].samplers == 4u
            && demand.perStage[1].storageBuffers == 5u && demand.perStage[1].uniformBuffers == 10u);
        BindlessCapabilities caps;
        caps.limitsQueried = true;
        caps.maxBindGroups = 4u;
        caps.maxBindingsPerGroup = 30u;
        caps.stageLimits = demand.perStage;
        BindlessPipelineInterfaceSummary summary;
        assert(!inspectBindlessPipelineInterface(caps, f.artifact, summary));
        assert(summary.descriptorCount == demand.descriptorCount && !caps.executablePayload);
        // Exact capacity succeeds; the default WebGPU16 ceiling refuses the
        // complete36 layout even though its material subset is only27.
        caps.stageLimits[1].sampledTextures = 16u;
        assert(std::string_view(inspectBindlessPipelineInterface(caps, f.artifact, summary))
            == "RHI-BINDLESS-STAGE-LIMIT");
        assert(!summary.descriptorCount && !summary.bindGroupCount);
        caps.stageLimits[1].sampledTextures = 48u;
        assert(!inspectBindlessPipelineInterface(caps, f.artifact, summary));
        for (auto member : {&BindlessStageLimits::sampledTextures,
                 &BindlessStageLimits::samplers,
                 &BindlessStageLimits::uniformBuffers,
                 &BindlessStageLimits::storageBuffers}) {
            caps.stageLimits = demand.perStage;
            --(caps.stageLimits[1].*member);
            const auto* id = inspectBindlessPipelineInterface(caps, f.artifact, summary);
            assert(id && std::string_view(id) == "RHI-BINDLESS-STAGE-LIMIT");
            assert(!summary.descriptorCount && !summary.bindGroupCount);
        }
        const auto reject = [&](const char* expected) {
            demand.descriptorCount = 999u;
            const auto* id = inspectBindlessPipelineInterfaceDemand(f.artifact, demand);
            assert(id && std::string_view(id) == expected && empty(demand));
        };
        f.artifact.additionalDomains[2].set = 8u;
        assert(!inspectBindlessPipelineInterfaceDemand(f.artifact, demand)
            && demand.bindGroupSlots == 9u);
        f.artifact.additionalDomains[2].set = 3u;
        f.artifact.physicalLayout.bindings.back().binding = 127u;
        assert(!inspectBindlessPipelineInterfaceDemand(f.artifact, demand));
        assert(demand.bindingsPerGroup == (execution == BindlessExecution::Emulated ? 128u : 30u));
        f.artifact.physicalLayout.bindings.back().binding = 29u;
        f.artifact.additionalDomains[0].set = 0u;
        reject("RHI-BINDLESS-PIPELINE-DOMAIN-COLLISION");
        f.artifact.additionalDomains[0].set = 1u;
        f.artifact.reservedPerStage[1].sampledTextures = 1u;
        reject("RHI-BINDLESS-PIPELINE-RESERVATIONS-MISMATCH");
        f.artifact.reservedPerStage[1] = {};
        f.artifact.physicalLayout.bindings[1].binding = 0u;
        reject("RHI-BINDLESS-PHYSICAL-BINDING-COLLISION");
        f.artifact.physicalLayout.bindings[1].binding = 1u;
        f.artifact.physicalLayout.bindings[0].arrayCount =
            std::numeric_limits<std::uint32_t>::max();
        reject("RHI-BINDLESS-PIPELINE-DEMAND-OVERFLOW");
        f.artifact.physicalLayout.bindings[0].arrayCount = 1u;
        f.artifact.additionalDomains[1].layout.bindings.front().textureViewDimensionConstrained =
            false;
        reject("RHI-BINDLESS-PIPELINE-BINDING-SHAPE");
        f.artifact.additionalDomains[1].layout.bindings.front().textureViewDimensionConstrained =
            true;
        DescriptorBindingDesc storage;
        storage.type = DescriptorType::StorageTexture;
        storage.binding = 15u;
        storage.stages = ShaderStage::Compute;
        storage.textureViewDimensionConstrained = true;
        storage.textureViewDimension = TextureViewDimension::Texture2D;
        storage.storageTextureFormat = TextureFormat::Rgba16Float;
        storage.storageTextureAccess = StorageTextureAccess::WriteOnly;
        f.artifact.additionalDomains[1].layout.bindings.push_back(storage);
        assert(!inspectBindlessPipelineInterfaceDemand(f.artifact, demand)
            && demand.perStage[2].storageTextures == 1u
            && demand.perStage[1].storageTextures == 0u);
        f.artifact.additionalDomains.back().set = std::numeric_limits<std::uint32_t>::max();
        reject("RHI-BINDLESS-PIPELINE-DEMAND-OVERFLOW");
    }
    std::puts(
        "BINDLESS_PIPELINE_DEMAND_OK material27 reserved36 shared-storage5 sparse-slots overflow hot0 no-execution");
    using namespace arp::rhi_webgpu;
    DemandFixture f(BindlessExecution::Emulated);
    WebGpuBindlessAdapterCapacity adapter;
    adapter.contractVersion = 1u;
    adapter.limitsQueried = true;
    adapter.bindGroups = 4u;
    adapter.bindingsPerGroup = 30u;
    adapter.perStage = {36u, 4u, 1u, 10u, 5u};
    WebGpuBindlessDeviceLimitPlan plan;
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    assert(!WebGpuBindlessDeviceLimitPlanner::plan(f.artifact, adapter, plan));
    assert(plan.contractVersion == 1u && plan.count == 6u);
    assert(plan.demand.descriptorCount == 55u && plan.demand.perStage[1].sampledTextures == 36u);
    assert(std::string_view(plan.requiredLimits[2].name) == "maxSampledTexturesPerShaderStage"
        && plan.requiredLimits[2].value == 36u);
    assert(std::string_view(plan.requiredLimits[4].name) == "maxUniformBuffersPerShaderStage"
        && plan.requiredLimits[4].value == 10u);
    const auto reject = [&](const char* expected) {
        const auto* id = WebGpuBindlessDeviceLimitPlanner::plan(f.artifact, adapter, plan);
        assert(id && std::string_view(id) == expected && !plan.contractVersion && !plan.count
            && empty(plan.demand));
        for (const auto& entry : plan.requiredLimits)
            assert(!entry.name && !entry.value);
    };
    for (auto* ceiling : {&adapter.bindGroups,
             &adapter.bindingsPerGroup,
             &adapter.perStage.sampledTextures,
             &adapter.perStage.samplers,
             &adapter.perStage.uniformBuffers,
             &adapter.perStage.storageBuffers}) {
        --*ceiling;
        reject("RHI-WEBGPU-BINDLESS-ADAPTER-LIMIT");
        ++*ceiling;
    }
    adapter.contractVersion = 2u;
    reject("RHI-WEBGPU-BINDLESS-DEVICE-LIMIT-PLAN-VERSION");
    adapter.contractVersion = 1u;
    adapter.limitsQueried = false;
    reject("RHI-WEBGPU-BINDLESS-ADAPTER-LIMITS-UNAVAILABLE");
    adapter.limitsQueried = true;
    for (const auto execution : {BindlessExecution::DiagnosticFailure,
             BindlessExecution::Validator,
             BindlessExecution::Native}) {
        f.artifact.execution = execution;
        reject("RHI-WEBGPU-BINDLESS-DEVICE-LIMIT-PLAN-EXECUTION");
    }
    f.artifact.execution = BindlessExecution::Emulated;
    f.artifact.pipelineInterfaceContractVersion = 0u;
    reject("RHI-BINDLESS-PIPELINE-INTERFACE-VERSION");
    f.artifact.pipelineInterfaceContractVersion = 1u;
    f.artifact.additionalDomains.back().layout.bindings.front().stages = ShaderStage::Compute;
    f.artifact.additionalDomains.back().layout.bindings.front().arrayCount = 2u;
    reject("RHI-BINDLESS-PIPELINE-BINDING-INVALID");
    f.artifact.additionalDomains.back().layout.bindings.front().arrayCount = 1u;
    assert(!WebGpuBindlessDeviceLimitPlanner::plan(f.artifact, adapter, plan));
    assert(plan.demand.perStage[1].uniformBuffers == 9u
        && plan.demand.perStage[2].uniformBuffers == 1u);
    f.artifact.additionalDomains.back().layout.bindings.front().stages = ShaderStage::Fragment;
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);
    if (argc != 1) {
        std::shared_ptr<const BindlessShaderPackage> package;
        const BindlessShaderArtifact* artifact = &f.artifact;
        std::size_t argument = 1u;
        if (std::string_view(argv[argument]) == "--package") {
            assert(argc == 11);
            arp::core::pmr::String diagnostic;
            // Explicit corpus load budget; this does not assert GPU capacity.
            const BindlessPackageLoadBudget budget{
                4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 33u, 4u};
            package = BindlessShaderPackage::load(argv[argument + 1u], budget, diagnostic);
            if (!package)
                std::fprintf(stderr, "Pipeline demand package: %s\n", diagnostic.c_str());
            assert(package);
            artifact = &package->artifact();
            argument += 2u;
        }
        assert(argc == static_cast<int>(argument + 8u)
            && std::string_view(argv[argument]) == "--adapter");
        const std::array<std::uint32_t*, 7u> fields{&adapter.bindGroups,
            &adapter.bindingsPerGroup,
            &adapter.perStage.sampledTextures,
            &adapter.perStage.samplers,
            &adapter.perStage.storageTextures,
            &adapter.perStage.uniformBuffers,
            &adapter.perStage.storageBuffers};
        for (std::size_t i = 0u; i < fields.size(); ++i) {
            const std::string_view text(argv[i + argument + 1u]);
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), *fields[i]);
            assert(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size());
        }
        if (const auto* id = WebGpuBindlessDeviceLimitPlanner::plan(*artifact, adapter, plan)) {
            std::printf("BINDLESS_DEVICE_LIMIT_PLAN_REFUSED %s\n", id);
            return 2;
        }
        std::printf(
            "BINDLESS_DEVICE_LIMIT_PLAN_JSON {\"contractVersion\":1,\"scope\":\"%s\",\"requiredLimits\":{",
            package ? "sealed-package-device-request-only"
                    : "authored-complete-layout-device-request-only");
        for (std::uint32_t i = 0u; i < plan.count; ++i)
            std::printf("%s\"%s\":%u",
                i ? "," : "",
                plan.requiredLimits[i].name,
                plan.requiredLimits[i].value);
        std::puts("}}");
    }
    std::puts("BINDLESS_WEBGPU_DEVICE_LIMIT_PLAN_OK stage-max zero-on-failure hot0 no-execution");
}
