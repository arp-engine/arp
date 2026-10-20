#pragma once
#include "original_runtime_length_fixture.h"
#include "rhi/frontend/descriptor_buffer_declaration_validation.h"
#include "rhi/frontend/pipeline_creation_validation.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"
#include "rhi/frontend/pipeline_state_copy.h"

namespace arp::test {
class OriginalRuntimeLengthPipelineInputFixture final {
public:
    template <class Artifact>
    using Input = std::conditional_t<std::is_same_v<Artifact, shader::SingleEntryShaderArtifact>,
        rhi::OriginalSingleEntryPipelineInput,
        rhi::OriginalGraphicsPipelineInput>;

    template <class Artifact>
    static void wrongProfile(const std::shared_ptr<const Artifact>& original)
    {
        using I = Input<Artifact>;
        constexpr std::uint64_t bytes = 64u * 1024u;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        assert(!I::prepare(original, {2u, bytes, 1u, true}, quota, diagnostic)
            && !quota->liveBytes()
            && diagnostic.find("AVAILABLE-TARGET-PROFILE") != diagnostic.npos);
        rhi::OriginalRuntimeLengthIntervalContract::Facts facts;
        facts.target.minimumBindingBytes = 99u;
        assert(rhi::OriginalRuntimeLengthIntervalContract::diagnose(
                   original, 0u, 0u, rhi::DescriptorType::StorageBuffer, facts)
            && !facts.target.minimumBindingBytes && !facts.stageMask);
    }

    template <class Artifact> static auto verify(const std::shared_ptr<const Artifact>& original)
    {
        using I = Input<Artifact>;
        constexpr std::uint64_t bytes = 64u * 1024u;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        typename I::Budget budget{2u, bytes, 1u, true};
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(256u);
        const auto factory = [&] {
            return I::prepare(original, budget, quota, diagnostic);
        };
        const auto reject = [&](const char* suffix) {
            const auto before = quota->liveBytes();
            auto value = factory();
            if (value || diagnostic.find(suffix) == diagnostic.npos)
                std::fprintf(
                    stderr, "Available target expected %s got %s\n", suffix, diagnostic.c_str());
            assert(!value && diagnostic.find(suffix) != diagnostic.npos
                && quota->liveBytes() == before);
            budget = {2u, bytes, 1u, true};
        };
        budget = {1u, bytes};
        reject("SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED");
        budget = {1u, bytes, 1u, true};
        reject("ROLE-POLICY-VERSION");
        budget.contractVersion = 99u;
        reject("INPUT-VERSION");
        budget.availableTargetContractVersion = 0u;
        reject("ROLE-POLICY-VERSION");
        budget.availableTargetContractVersion = 2u;
        reject("ROLE-POLICY-VERSION");
        budget.prototypeOptIn = false;
        reject("ROLE-POLICY-OPT-IN");
        budget.maxCpuMetadataBytes = 0u;
        reject("INPUT-QUOTA");
        auto forgedArtifact = std::shared_ptr<const Artifact>(original->quota(), original.get());
        assert(!I::prepare(forgedArtifact, budget, quota, diagnostic)
            && diagnostic.find("ORIGINAL-OWNER") != diagnostic.npos);
        assert(!I::prepare(std::shared_ptr<const Artifact>{}, budget, quota, diagnostic)
            && diagnostic.find("ORIGINAL-OWNER") != diagnostic.npos);
        auto sameBudget = budget;
        sameBudget.maxCpuMetadataBytes = original->quota()->limitBytes();
        assert(!I::prepare(original, sameBudget, original->quota(), diagnostic)
            && diagnostic.find("QUOTA-DOMAIN") != diagnostic.npos);
        assert(!I::prepare(original, budget, {}, diagnostic)
            && diagnostic.find("INPUT-QUOTA") != diagnostic.npos);
        OriginalRuntimeLengthFixture::allocations(quota, factory);
        auto input = factory();
        if (!input)
            std::fprintf(stderr, "Available target input: %s\n", diagnostic.c_str());
        assert(input);
        assert(input->package().artifacts.mslPath.empty() && !input->nativeExecutionProven());
        using Interval = rhi::OriginalRuntimeLengthIntervalContract;
        const auto bindingType = input->package().reflection.descriptorBindings[0].type;
        Interval::Facts intervalFacts;
        assert(!Interval::diagnose(original, 0u, 0u, bindingType, intervalFacts)
            && intervalFacts.target.hasRuntimeArray && intervalFacts.target.runtimeArrayStrideBytes
            && intervalFacts.stageMask);
        const auto outputType = input->package().reflection.descriptorBindings[1].type;
        assert(!Interval::diagnoseBinding(original, 0u, 1u, outputType, intervalFacts)
            && intervalFacts.target.minimumBindingBytes && intervalFacts.stageMask);
        if constexpr (std::is_same_v<Artifact, shader::SingleEntryShaderArtifact>) {
            assert(Interval::diagnose(original, 0u, 1u, outputType, intervalFacts)
                && !intervalFacts.target.minimumBindingBytes && !intervalFacts.stageMask);
        }
        for (const auto& candidate :
            std::array{std::shared_ptr<const Artifact>{}, forgedArtifact}) {
            intervalFacts.target.minimumBindingBytes = 99u;
            assert(Interval::diagnose(candidate, 0u, 0u, bindingType, intervalFacts)
                && !intervalFacts.target.minimumBindingBytes && !intervalFacts.stageMask);
            assert(Interval::diagnoseBinding(candidate, 0u, 1u, outputType, intervalFacts)
                && !intervalFacts.target.minimumBindingBytes && !intervalFacts.stageMask);
            std::size_t count = 99u;
            assert(Artifact::originalRuntimeLengthOperationCount(candidate, count) && !count);
        }
        for (const auto selected :
            std::array<std::pair<std::uint32_t, std::uint32_t>, 2>{{{1u, 0u}, {0u, 2u}}})
            assert(Interval::diagnose(
                       original, selected.first, selected.second, bindingType, intervalFacts)
                && !intervalFacts.target.minimumBindingBytes && !intervalFacts.stageMask);
        assert(
            Interval::diagnose(original, 0u, 0u, rhi::DescriptorType::UniformBuffer, intervalFacts)
            && !intervalFacts.target.minimumBindingBytes && !intervalFacts.stageMask);
        assert(!rhi::shaderStageFromCompilerStage(static_cast<shader::ShaderStage>(99u)));
        assert(!rhi::validateShaderPackage(input->package()).success);
        rhi::ShaderPackageDesc generic;
        generic.label = "uncleared output";
        if constexpr (std::is_same_v<Artifact, shader::SingleEntryShaderArtifact>)
            assert(!rhi::populateSingleEntryShaderInterface(*original, generic).success);
        else
            assert(!rhi::populateGraphicsModuleShaderInterface(*original, generic).success);
        assert(generic.entrypoints.empty() && generic.label.empty());
        generic.label = "uncleared output";
        assert(!rhi::populateRuntimeLengthShaderInterface(forgedArtifact, generic).success
            && generic.entrypoints.empty() && generic.label.empty());
        auto copied = rhi::ShaderPackageStateCopy::copy(input->package());
        assert(!rhi::validateShaderPackage(copied).success);
        assert(!I::packageDiagnostic(
            input, copied)); // equal metadata with genuine owner, not a new authority
        const auto changed = [&](auto edit) {
            auto altered = rhi::ShaderPackageStateCopy::copy(copied);
            edit(altered);
            assert(I::packageDiagnostic(input, altered));
        };
        changed([](auto& p) {
            p.label = "other";
        });
        changed([](auto& p) {
            p.artifacts.mslPath = "dummy.metal";
        });
        changed([](auto& p) {
            p.artifacts.wgslPath = "other.wgsl";
        });
        changed([](auto& p) {
            p.reflection.sourceSha256 = "other";
        });
        changed([](auto& p) {
            p.entrypoints[0].entryPoint = "other";
        });
        changed([](auto& p) {
            ++p.entrypoints[0].computeWorkgroupSizeX;
        });
        changed([](auto& p) {
            ++p.reflection.descriptorBindings[0].binding;
        });
        changed([](auto& p) {
            p.reflection.descriptorBindings[0].type = rhi::DescriptorType::UniformBuffer;
        });
        changed([](auto& p) {
            ++p.reflection.descriptorBindings[0].arrayCount;
        });
        changed([](auto& p) {
            p.reflection.descriptorBindings[0].stages = rhi::ShaderStage::None;
        });
        changed([](auto& p) {
            p.nativeHandleExposedToUpperLayer = true;
        });
        changed([](auto& p) {
            p.generatedReflectionHydrated = false;
        });
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto layout = rhi::buildPipelineLayoutDescFromShaderReflection(input->package().reflection);
        std::pmr::set_default_resource(previous);
        assert(layout.descriptorLayouts.size() == 1u
            && layout.descriptorLayouts[0].bindings.size() == 2u
            && layout.descriptorLayouts[0].name.get_allocator().resource()
                == &core::defaultCpuMemoryResource()
            && layout.descriptorLayouts[0].bindings.get_allocator().resource()
                == &core::defaultCpuMemoryResource());
        using Declarations = rhi::PipelineShaderDeclarationContract;
        constexpr bool graphics = std::is_same_v<Artifact, shader::GraphicsShaderModuleArtifact>;
        const auto kind =
            graphics ? Declarations::Kind::GraphicsPipeline : Declarations::Kind::ComputePipeline;
        const auto count = graphics ? 2u : 1u;
        const auto entryAt = [&](std::size_t i) -> const auto& {
            return input->package().entrypoints[i];
        };
        assert(Declarations::validate(layout, input->package(), count, kind, entryAt));
        assert(
            !Declarations::validateOriginal(input, layout, input->package(), count, kind, entryAt));
        assert(Declarations::validateOriginal(
            std::shared_ptr<const I>{}, layout, copied, count, kind, entryAt));
        auto forged = std::shared_ptr<const I>(quota, input.get());
        assert(Declarations::validateOriginal(forged, layout, copied, count, kind, entryAt));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, original->quota()}, [&]() noexcept -> const char* {
                std::size_t count = 0u;
                assert(!Artifact::originalRuntimeLengthOperationCount(original, count) && count);
                assert(!Interval::diagnose(original, 0u, 0u, bindingType, intervalFacts));
                for (const auto backend : std::array{rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto target = I::selectTarget(input, backend);
                    assert(target.validatorOnly == (backend == rhi::Backend::Null));
                    if (backend == rhi::Backend::Metal)
                        assert(target.diagnostic
                            && !std::strcmp(
                                target.diagnostic, "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"));
                    else
                        assert(!target.diagnostic);
                    if constexpr (graphics)
                        assert(target.sourceCount
                            == (backend == rhi::Backend::Vulkan       ? 2u
                                    : backend == rhi::Backend::WebGPU ? 1u
                                                                      : 0u));
                    else
                        assert(bool(target.bytes)
                            == (backend == rhi::Backend::Vulkan
                                || backend == rhi::Backend::WebGPU));
                }
                assert(I::packageDiagnostic(forged, copied));
                assert(!I::packageDiagnostic(input, copied));
                return I::validateOriginal(input);
            }));
        quota->setLimitBytes(bytes - 1u);
        assert(I::packageDiagnostic(input, copied));
        quota->setLimitBytes(bytes);
        const auto originalLimit = original->quota()->limitBytes();
        original->quota()->setLimitBytes(originalLimit - 1u);
        assert(I::packageDiagnostic(input, copied));
        original->quota()->setLimitBytes(originalLimit);
        budget = {99u, 0u, 99u, false};
        assert(!I::validateOriginal(input));
        std::puts(
            "BINDLESS_RHI_RUNTIME_LENGTH_AVAILABLE_INPUT_OK explicit-policy original-owner complete-definition generic-msl-closed targets4 cold-quota-hot0 native-interval-closed");
        return input;
    }

    static void real(const std::filesystem::path& path,
        bool graphics = false,
        bool deleted = false,
        bool ordinary = false)
    {
        OriginalRuntimeLengthFixture fixture;
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        if (graphics) {
            auto budget = fixture.moduleBudget();
            if (ordinary) {
                budget.targetMemory = nullptr;
                budget.runtimeLength = nullptr;
            }
            auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
            auto original =
                shader::GraphicsShaderModuleArtifact::load(path, budget, quota, diagnostic);
            assert(original);
            if (ordinary) {
                wrongProfile(original);
                return;
            }
            auto input = verify(original);
            original.reset();
            if (deleted)
                std::filesystem::remove_all(path.parent_path());
            assert(!rhi::OriginalGraphicsPipelineInput::validateOriginal(input));
        } else {
            auto budget = fixture.entryBudget();
            if (ordinary) {
                budget.targetMemory = nullptr;
                budget.runtimeLength = nullptr;
            }
            auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
            auto original =
                shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic);
            assert(original);
            if (ordinary) {
                wrongProfile(original);
                return;
            }
            auto input = verify(original);
            original.reset();
            if (deleted)
                std::filesystem::remove_all(path.parent_path());
            assert(!rhi::OriginalSingleEntryPipelineInput::validateOriginal(input));
        }
    }
};
} // namespace arp::test
