#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_target_memory_interval_fixture.h"
#include "shader/graphics_shader_module_artifact.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace arp::test {
// Authored policies for the existing fixtures, never a production plan selector.
class TargetMemoryRequestFixture final {
    using Artifact = shader::SingleEntryShaderArtifact;
    using Graph = shader::BufferDeclarationTypeLayouts;
    using Request = shader::TargetBufferMemoryLoadRequest;

public:
    static constexpr Graph::Budget graphBudget{512u, 512u, 17u};

    void prepare(const std::shared_ptr<const Artifact>& original, bool probe = false)
    {
        request_ = {1u, graphBudget, graphBudget, 512u, 128u};
        plans_.clear();
        for (const auto& b : original->module().reflection.bindings)
            if (shader::TargetBufferMemoryDeclarations::isBufferBinding(b.type)) {
                const auto form = b.type == shader::BindingType::UniformBuffer
                    ? (probe ? Request::Lowering::RootForm::UniformValue
                             : Request::Lowering::RootForm::BlockStruct)
                    : Request::Lowering::RootForm::StorageRuntimeArray;
                plans_.push_back({b.set, b.binding, form, {nullptr, 0u}});
            }
        request_.bindings = plans_.data();
        request_.bindingCount = plans_.size();
        request_.push = original->module().reflection.pushConstantSizeBytes != 0u;
        if (probe) {
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> source{
                &core::defaultCpuMemoryResource()};
            core::pmr::Vector<shader::WgslBufferDeclarationLayout> target{
                &core::defaultCpuMemoryResource()};
            Graph s, t;
            shader::SpirvPushConstantDeclarationLayout push;
            const auto& bytes = original->bytes(shader::ShaderArtifactRole::Spirv);
            assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(
                       bytes.data(), bytes.size(), 3u, graphBudget, source, s, push)
                && !push.present);
            // Authored fixture declaration order fixes the wrapper pair even
            // when hostile tests mutate/remove/add the original target roots.
            constexpr core::StringView probeTarget =
                R"(@binding(2) @group(0) var<storage, read_write> Output_0 : array<u32>;
@binding(0) @group(0) var<uniform> Scalar_0 : u32;
struct LayoutElement_std430_0 { @align(16) position_0 : vec3<f32>, @align(4) marker_0 : u32, @align(16) pairs_0 : array<vec2<f32>, i32(3)>, };
@binding(1) @group(0) var<storage, read> Input_0 : array<LayoutElement_std430_0>;)";
            assert(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
                probeTarget, 3u, graphBudget, target, t));
            std::size_t sr = Graph::absent, tr = Graph::absent;
            for (const auto& root : s.roots())
                if (root.set == 0u && root.binding == 1u)
                    sr = root.type;
            for (const auto& root : t.roots())
                if (root.set == 0u && root.binding == 1u)
                    tr = root.type;
            assert(sr != Graph::absent && tr != Graph::absent);
            const auto& block = s.nodes()[sr];
            const auto& array = s.nodes()[s.members()[block.memberBegin].type];
            const auto& element = s.nodes()[array.child];
            const auto& targetElement = t.nodes()[t.nodes()[tr].child];
            assert(element.memberCount == 3u && targetElement.memberCount == 3u);
            wrapper_ = {s.members()[element.memberBegin + 2u].type,
                t.members()[targetElement.memberBegin + 2u].type};
            for (auto& plan : plans_)
                if (plan.set == 0u && plan.binding == 1u)
                    plan.arrays = {&wrapper_, 1u};
        }
    }

    auto& request() noexcept { return request_; }

    auto& plans() noexcept { return plans_; }

private:
    core::pmr::Vector<Request::BindingPlan> plans_{&core::defaultCpuMemoryResource()};
    Request::Lowering::ArrayValueWrapper wrapper_;
    Request request_;
};

class OriginalTargetMemoryFixture final {
    using Artifact = shader::SingleEntryShaderArtifact;
    using Module = shader::GraphicsShaderModuleArtifact;

public:
    static shader::SingleEntryShaderLoadBudget entryBudget() noexcept
    {
        return {1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 16u};
    }

    static shader::GraphicsShaderModuleLoadBudget moduleBudget() noexcept
    {
        return {1024u * 1024u, 16u * 1024u * 1024u, 4u * 1024u * 1024u, 4u, 4u, 256u, 2u};
    }

    // Startup-only synchronous preparation. Keep request/graph locals outside
    // an asynchronous browser harness frame; no continuation budget change.
    static std::shared_ptr<const Module> loadModuleWithTargetMemory(
        const std::filesystem::path& path,
        shader::GraphicsShaderModuleLoadBudget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        core::pmr::String& diagnostic)
    {
        assert(!budget.targetMemory && !budget.runtimeLength);
        const auto baseline = quota->liveBytes();
        auto bare = Module::load(path, budget, quota, diagnostic);
        assert(bare);
        for (const auto& binding : bare->module().reflection.bindings)
            if (shader::TargetBufferMemoryDeclarations::isBufferBinding(binding.type)) {
                shader::GraphicsTargetBufferMemoryLayout out;
                out.target.minimumBindingBytes = UINT64_MAX;
                const auto* id =
                    Module::originalTargetBufferMemory(bare, binding.set, binding.binding, out);
                assert(id && std::string_view(id) == "SHADER-MODULE-TARGET-MEMORY-NOT-REQUESTED"
                    && !out.target.minimumBindingBytes);
            }
        TargetMemoryRequestFixture vertex, fragment;
        vertex.prepare(bare->vertex());
        fragment.prepare(bare->fragment());
        shader::GraphicsTargetMemoryLoadRequest request{1u,
            vertex.request(),
            fragment.request(),
            TargetMemoryRequestFixture::graphBudget,
            TargetMemoryRequestFixture::graphBudget,
            512u};
        bare.reset();
        assert(quota->liveBytes() == baseline);
        budget.targetMemory = &request;
        auto value = Module::load(path, budget, quota, diagnostic);
        if (!value)
            std::fprintf(stderr, "graphics target-memory load: %s\n", diagnostic.c_str());
        assert(value);
        for (const auto& binding : value->module().reflection.bindings)
            if (shader::TargetBufferMemoryDeclarations::isBufferBinding(binding.type)) {
                shader::GraphicsTargetBufferMemoryLayout out;
                assert(!Module::originalTargetBufferMemory(value, binding.set, binding.binding, out)
                    && out.type == binding.type && out.stageMask == binding.stageMask
                    && out.target.minimumBindingBytes >= out.combinedTargetDeclarationBytes);
            }
        return value;
    }

    static void hot(const std::shared_ptr<const Artifact>& original)
    {
        assert(!Artifact::validateOriginal(original));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{original->quota()}, [&]() noexcept -> const char* {
                for (const auto& b : original->module().reflection.bindings)
                    if (shader::TargetBufferMemoryDeclarations::isBufferBinding(b.type)) {
                        shader::TargetBufferMemoryLayout out;
                        out.target.minimumBindingBytes = UINT64_MAX;
                        const auto* id =
                            Artifact::originalTargetBufferMemory(original, b.set, b.binding, out);
                        assert(!id);
                        const auto type = rhi::descriptorTypeFromShaderBinding(b.type);
                        assert(type);
                        OriginalTargetMemoryIntervalFixture::verifyDeclaration(
                            original, b.set, b.binding, *type);
                        {
                            assert(out.set == b.set && out.binding == b.binding
                                && out.type == b.type && out.target.minimumBindingBytes);
                            const auto expected = out;
                            out.target.minimumBindingBytes = UINT64_MAX;
                            assert(!Artifact::originalTargetBufferMemory(
                                original, b.set, b.binding, out));
                            assert(out.target.minimumBindingBytes
                                    == expected.target.minimumBindingBytes
                                && out.source.matches(expected.source));
                        }
                    }
                shader::TargetPushMemoryLayout push;
                push.targetDeclarationBytes = UINT64_MAX;
                const auto* id = Artifact::originalTargetPushMemory(original, push);
                if (id)
                    assert(!push.targetDeclarationBytes && !push.sourceOccupiedBytes);
                else
                    assert(push.targetDeclarationBytes
                            == original->module().reflection.pushConstantSizeBytes
                        && push.sourceOccupiedBytes <= push.targetDeclarationBytes);
                auto forged = std::shared_ptr<const Artifact>(original->quota(), original.get());
                for (const auto& owner : std::array{std::shared_ptr<const Artifact>{}, forged}) {
                    shader::TargetBufferMemoryLayout facts;
                    facts.target.minimumBindingBytes = UINT64_MAX;
                    const auto* failure =
                        Artifact::originalTargetBufferMemory(owner, 0u, 0u, facts);
                    assert(failure && !std::strcmp(failure, "SHADER-ENTRY-ORIGINAL-OWNER")
                        && !facts.target.minimumBindingBytes);
                    push.targetDeclarationBytes = UINT64_MAX;
                    failure = Artifact::originalTargetPushMemory(owner, push);
                    assert(failure && !std::strcmp(failure, "SHADER-ENTRY-ORIGINAL-OWNER")
                        && !push.targetDeclarationBytes);
                }
                return Artifact::validateOriginal(original);
            }));
        const auto quota = original->quota();
        const auto limit = quota->limitBytes();
        quota->setLimitBytes(limit - 1u);
        shader::TargetBufferMemoryLayout facts;
        facts.target.minimumBindingBytes = UINT64_MAX;
        const auto* id = Artifact::originalTargetBufferMemory(original, 0u, 0u, facts);
        assert(id && !std::strcmp(id, "SHADER-ENTRY-ORIGINAL-QUOTA-LIMIT")
            && !facts.target.minimumBindingBytes);
        quota->setLimitBytes(limit);
    }

    static std::shared_ptr<const Artifact> entry(
        const std::filesystem::path& path, bool probe = false, bool deleteInputs = false)
    {
        auto budget = entryBudget();
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(256u);
        auto bare = Artifact::load(path, budget, quota, diagnostic);
        assert(bare);
        TargetMemoryRequestFixture plans;
        plans.prepare(bare, probe);
        rhi::OriginalTargetBufferDeclarationContract::Facts missing;
        assert(rhi::OriginalTargetBufferDeclarationContract::diagnose(
                   bare, 0u, 0u, rhi::DescriptorType::StorageBuffer, missing)
            && !missing.target.minimumBindingBytes && !missing.stageMask);
        if (probe) {
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> source{
                &core::defaultCpuMemoryResource()};
            const auto& binary = bare->bytes(shader::ShaderArtifactRole::Spirv);
            assert(!shader::SpirvBufferDeclarationLayoutVerifier::verify(
                binary.data(), binary.size(), 16u, source));
            shader::TargetBufferMemoryDeclarations processor;
            const auto build = [&](core::StringView target) {
                return processor.build(
                    binary, target, bare->module().reflection, source, 16u, plans.request());
            };
            const auto target =
                shader::shaderArtifactText(bare->bytes(shader::ShaderArtifactRole::Wgsl));
            assert(!build(target));
            assert(!build(target) && processor.records().size() == 3u);
            auto saved = plans.request();
            plans.request().maxPairs = 1u;
            assert(build(target) && processor.records().empty()
                && processor.sourceTypes().nodes().empty()
                && processor.targetTypes().nodes().empty());
            plans.request() = saved;
            assert(!build(target));
            assert(!build("@group(0) @binding(0) var<uniform> value:UnknownType;")
                && processor.diagnostic());
            shader::TargetBufferMemoryLayout out;
            out.target.minimumBindingBytes = UINT64_MAX;
            assert(processor.lookup(0u, 0u, out) && !out.target.minimumBindingBytes
                && processor.records().empty());
            assert(!build(target) && !processor.lookup(0u, 0u, out)
                && out.target.minimumBindingBytes == 4u);
        }
        bare.reset();
        assert(!quota->liveBytes());
        budget.targetMemory = &plans.request();
        const auto factory = [&] {
            return Artifact::load(path, budget, quota, diagnostic);
        };
        auto value = factory();
        if (!value)
            std::fprintf(stderr, "Original target entry: %s\n", diagnostic.c_str());
        assert(value);
        CpuQuotaFactoryFixture quotaFixture(quota);
        const auto peak = quotaFixture.measure(factory);
        quotaFixture.verify(peak, factory);
        hot(value);
        if (value->module().reflection.pushConstantSizeBytes) {
            shader::TargetPushMemoryLayout push;
            assert(!Artifact::originalTargetPushMemory(value, push)
                && push.targetDeclarationBytes == 32u && push.sourceOccupiedBytes == 32u);
        }
        if (probe) {
            for (std::uint32_t binding = 0u; binding < 3u; ++binding) {
                shader::TargetBufferMemoryLayout facts;
                assert(!Artifact::originalTargetBufferMemory(value, 0u, binding, facts));
                assert(facts.target.minimumBindingBytes == (binding == 1u ? 48u : 4u));
            }
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto hostile = factory();
        std::pmr::set_default_resource(previous);
        if (!hostile)
            std::fprintf(stderr, "Original target hostile PMR: %s\n", diagnostic.c_str());
        assert(hostile);
        hostile.reset();
        const auto before = quota->liveBytes();
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - before);
            assert(!factory() && quota->liveBytes() == quota->limitBytes());
        }
        assert(quota->liveBytes() == before);
        plans.request().contractVersion = 99u;
        assert(!factory() && diagnostic.find("REQUEST-VERSION") != diagnostic.npos);
        plans.request().contractVersion = 1u;
        const auto saved = plans.request();
        const auto rejects = [&](const char* expected) {
            assert(!factory() && diagnostic.find(expected) != diagnostic.npos
                && quota->liveBytes() == before);
            plans.request() = saved;
        };
        plans.request().sourceTypes.maxNodes = 1u;
        rejects("NODE-BUDGET");
        plans.request().targetTypes.maxNodes = 1u;
        rejects("NODE-BUDGET");
        plans.request().maxPairs = 1u;
        rejects("PAIR-BUDGET");
        plans.request().push = !saved.push;
        rejects("PLAN-COVERAGE");
        if (probe) {
            plans.request().bindingCount = 2u;
            rejects("PLAN-COVERAGE");
            plans.request().maxArrayRules = 0u;
            rejects("RULE-BUDGET");
            const auto first = plans.plans()[0];
            plans.plans()[0] = plans.plans()[1];
            rejects("PLAN-DUPLICATE");
            plans.plans()[0] = first;
            plans.plans()[0].form =
                shader::SpirvWgslBufferMemoryLowering::RootForm::StorageRuntimeArray;
            rejects("ROOT");
            plans.plans()[0] = first;
        }
        // Caller request mutation after load cannot alter frozen original facts.
        plans.request().bindingCount = SIZE_MAX;
        plans.request().bindings = nullptr;
        hot(value);
        if (deleteInputs) {
            std::filesystem::remove_all(path.parent_path());
            hot(value);
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_TARGET_MEMORY_ENTRY_OK exact-owner byte-map-types frozen-request hostile-pmr quota-cleanup hot0 length-native-closed");
        return value;
    }

    static std::shared_ptr<const Module> module(
        const std::filesystem::path& path, bool deleteInputs = false)
    {
        auto budget = moduleBudget();
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(256u);
        auto bare = Module::load(path, budget, quota, diagnostic);
        assert(bare);
        TargetMemoryRequestFixture vertex, fragment;
        vertex.prepare(bare->vertex());
        fragment.prepare(bare->fragment());
        shader::GraphicsTargetMemoryLoadRequest request{1u,
            vertex.request(),
            fragment.request(),
            TargetMemoryRequestFixture::graphBudget,
            TargetMemoryRequestFixture::graphBudget,
            512u};
        bare.reset();
        assert(!quota->liveBytes());
        budget.targetMemory = &request;
        auto value = Module::load(path, budget, quota, diagnostic);
        if (!value)
            std::fprintf(stderr, "Original target module: %s\n", diagnostic.c_str());
        assert(value);
        const auto retainedBytes = quota->liveBytes();
        auto isolated = loadModuleWithTargetMemory(path, moduleBudget(), quota, diagnostic);
        assert(isolated && isolated->identity() == value->identity());
        isolated.reset();
        assert(quota->liveBytes() == retainedBytes);
        const auto factory = [&] {
            return Module::load(path, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture quotaFixture(quota);
        const auto peak = quotaFixture.measure(factory);
        quotaFixture.verify(peak, factory);
        hot(value->vertex());
        hot(value->fragment());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto& b : value->module().reflection.bindings)
                    if (shader::TargetBufferMemoryDeclarations::isBufferBinding(b.type)) {
                        shader::GraphicsTargetBufferMemoryLayout facts;
                        facts.target.minimumBindingBytes = UINT64_MAX;
                        const auto* id =
                            Module::originalTargetBufferMemory(value, b.set, b.binding, facts);
                        assert(!id && facts.stageMask == b.stageMask && facts.type == b.type
                            && facts.target.minimumBindingBytes
                                >= facts.combinedTargetDeclarationBytes);
                        const auto type = rhi::descriptorTypeFromShaderBinding(b.type);
                        assert(type);
                        OriginalTargetMemoryIntervalFixture::verifyDeclaration(
                            value, b.set, b.binding, *type);
                        std::uint64_t minimum = facts.combinedTargetDeclarationBytes;
                        for (const auto& child : std::array{value->vertex(), value->fragment()}) {
                            for (const auto& binding : child->module().reflection.bindings)
                                if (binding.set == b.set && binding.binding == b.binding) {
                                    shader::TargetBufferMemoryLayout selected;
                                    assert(!Artifact::originalTargetBufferMemory(
                                        child, b.set, b.binding, selected));
                                    minimum =
                                        std::max(minimum, selected.target.minimumBindingBytes);
                                }
                        }
                        assert(facts.target.minimumBindingBytes == minimum);
                    }
                auto forged = std::shared_ptr<const Module>(quota, value.get());
                shader::GraphicsTargetBufferMemoryLayout out;
                out.target.minimumBindingBytes = UINT64_MAX;
                assert(Module::originalTargetBufferMemory(forged, 0u, 0u, out)
                    && !out.target.minimumBindingBytes);
                shader::TargetPushMemoryLayout push;
                push.targetDeclarationBytes = UINT64_MAX;
                const auto* id = Module::originalTargetPushMemory(value, push);
                if (value->module().reflection.pushConstantSizeBytes)
                    assert(!id && push.targetDeclarationBytes == 32u
                        && push.sourceOccupiedBytes == 32u);
                else
                    assert(id && !std::strcmp(id, "SHADER-MODULE-TARGET-PUSH-ABSENT")
                        && !push.targetDeclarationBytes);
                return Module::validateOriginal(value);
            }));
        const auto before = quota->liveBytes();
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto hostile = Module::load(path, budget, quota, diagnostic);
        std::pmr::set_default_resource(previous);
        if (!hostile)
            std::fprintf(stderr, "Original target module hostile PMR: %s\n", diagnostic.c_str());
        assert(hostile);
        hostile.reset();
        assert(quota->liveBytes() == before);
        const auto limit = quota->limitBytes();
        quota->setLimitBytes(limit - 1u);
        shader::GraphicsTargetBufferMemoryLayout facts;
        facts.target.minimumBindingBytes = UINT64_MAX;
        const auto* id = Module::originalTargetBufferMemory(value, 0u, 0u, facts);
        assert(id && !std::strcmp(id, "SHADER-MODULE-ORIGINAL-QUOTA-LIMIT")
            && !facts.target.minimumBindingBytes);
        quota->setLimitBytes(limit);
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - before);
            assert(!Module::load(path, budget, quota, diagnostic));
        }
        assert(quota->liveBytes() == before);
        if (deleteInputs) {
            std::filesystem::remove_all(path.parent_path());
            for (const auto& b : value->module().reflection.bindings)
                if (shader::TargetBufferMemoryDeclarations::isBufferBinding(b.type))
                    assert(!Module::originalTargetBufferMemory(value, b.set, b.binding, facts));
            hot(value->vertex());
            hot(value->fragment());
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_TARGET_MEMORY_GRAPHICS_OK original-children2 combined-binary-target complete-stage-shapes hot0 length-native-closed");
        return value;
    }
};
} // namespace arp::test
