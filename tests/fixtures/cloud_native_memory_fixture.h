#pragma once
#include "shader/graphics_shader_module_artifact.h"
#include "shader/spirv_image_declaration.h"

#include <cassert>
#include <cstring>
#include <string_view>

namespace arp::test {
// Authored CloudGpuConstants/1 paths and scalar foreground ABI. This fixture
// inspects the actual binary to name source nodes; it grants no native authority.
class CloudNativeMemoryFixture final {
public:
    enum class Profile { Cloud, Foreground, Probe };
    using Request = shader::NativeBufferMemoryLoadRequest;
    using Graph = shader::BufferDeclarationTypeLayouts;
    using Path = shader::BufferDeclarationTypePath;
    using Lowering = Request::Lowering;
    static constexpr Graph::Budget graphBudget{4096u, 4096u, 64u};

    explicit CloudNativeMemoryFixture(
        const std::shared_ptr<const shader::SingleEntryShaderArtifact>& artifact, Profile profile)
    {
        assert(!shader::SingleEntryShaderArtifact::validateOriginal(artifact));
        const auto& binary = artifact->bytes(shader::ShaderArtifactRole::Spirv);
        Graph source;
        core::pmr::Vector<shader::SpirvBufferDeclarationLayout> declarations;
        assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
            binary.data(), binary.size(), 16u, graphBudget, declarations, source));
        for (const auto& binary : declarations) {
            auto root = Graph::absent;
            for (const auto& candidate : source.roots())
                if (candidate.set == binary.set && candidate.binding == binary.binding) {
                    assert(root == Graph::absent);
                    root = candidate.type;
                }
            assert(root != Graph::absent);
            if (profile == Profile::Cloud && binary.set == 0u && binary.binding == 0u) {
                const auto& structure = source.nodes()[root];
                assert(structure.kind == Graph::Kind::Struct && structure.memberCount == 17u);
                for (std::size_t m = 0u; m < 2u; ++m) {
                    const auto type = source.members()[structure.memberBegin + m].type;
                    assert(source.nodes()[type].kind == Graph::Kind::Matrix);
                    bool declared = false;
                    for (std::size_t i = 0u; i < matrixCount_; ++i)
                        declared |= matrices_[i].sourceType == type;
                    if (!declared) {
                        steps_[matrixCount_] = {Path::Kind::Member, m};
                        matrices_[matrixCount_] = {type, {&steps_[matrixCount_], 1u}};
                        ++matrixCount_;
                    }
                }
            }
            const auto matrixCount =
                profile == Profile::Cloud && binary.binding == 0u ? matrixCount_ : 0u;
            bindings.push_back({binary.set,
                binary.binding,
                graphBudget,
                {2u,
                    4096u,
                    0u,
                    matrixCount,
                    nullptr,
                    0u,
                    nullptr,
                    0u,
                    binary.hasRuntimeArray               ? Lowering::RootForm::RuntimeElement
                        : profile == Profile::Foreground ? Lowering::RootForm::Value
                                                         : Lowering::RootForm::Block},
                matrixCount ? matrices_.data() : nullptr,
                matrixCount,
                matrixCount});
        }
        request = {3u, graphBudget, bindings.data(), bindings.size()};
    }

    const Request* get() const noexcept { return bindings.empty() ? nullptr : &request; }

    void wrongTargetMatrixPath() { steps_[0].member = 2u; }

    static auto loadSingle(const std::filesystem::path& path,
        Profile profile,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        core::pmr::String& diagnostic,
        bool wrongTarget = false)
    {
        shader::SingleEntryShaderLoadBudget budget{
            quota->limitBytes(), quota->limitBytes() * 8u, quota->limitBytes(), 16u};
        const auto baseline =
            shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic);
        assert(baseline);
        CloudNativeMemoryFixture plan(baseline, profile);
        if (wrongTarget)
            plan.wrongTargetMatrixPath();
        budget.nativeMemory = plan.get();
        return shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic);
    }

    static auto loadGraphics(const std::filesystem::path& path,
        Profile profile,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        core::pmr::String& diagnostic,
        bool wrongTarget = false)
    {
        shader::GraphicsShaderModuleLoadBudget budget{quota->limitBytes(),
            quota->limitBytes() * 8u,
            quota->limitBytes(),
            16u,
            16u,
            4096u,
            32u};
        const auto baseline =
            shader::GraphicsShaderModuleArtifact::load(path, budget, quota, diagnostic);
        assert(baseline);
        CloudNativeMemoryFixture vertex(baseline->vertex(), profile),
            fragment(baseline->fragment(), profile);
        if (wrongTarget)
            fragment.wrongTargetMatrixPath();
        budget.nativeMemory = {vertex.get(), fragment.get()};
        return shader::GraphicsShaderModuleArtifact::load(path, budget, quota, diagnostic);
    }

    static void verifyReadOnlyAccess(
        const std::shared_ptr<const shader::SingleEntryShaderArtifact>& original)
    {
        using Artifact = shader::SingleEntryShaderArtifact;
        using Verifier = shader::SpirvBufferDeclarationLayoutVerifier;
        for (const auto& binding : original->module().reflection.bindings) {
            if (binding.type != shader::BindingType::ReadOnlyStorageBuffer)
                continue;
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Artifact::originalReadOnlyStorageDiagnostic(
                original, binding.set, binding.binding));
            assert(before == core::defaultCpuAllocator().stats().allocationCount);
            const auto fake = std::shared_ptr<const Artifact>(original.get(), [](const auto*) {});
            assert(Artifact::originalReadOnlyStorageDiagnostic(fake, binding.set, binding.binding));
            shader::SpirvBufferDeclarationLayout facts;
            assert(!Artifact::originalBufferDeclarationLayout(
                original, binding.set, binding.binding, facts));
            const auto& binary = original->bytes(shader::ShaderArtifactRole::Spirv);
            const shader::SpirvInstructionView view(binary.data(), binary.size());
            core::pmr::Vector<std::byte> missing(binary.size()), unreadable(binary.size()),
                duplicate(binary.size());
            std::memcpy(missing.data(), binary.data(), binary.size());
            std::memcpy(unreadable.data(), binary.data(), binary.size());
            std::memcpy(duplicate.data(), binary.data(), binary.size());
            bool changed = false, appended = false;
            for (auto offset = shader::SpirvInstructionView::headerWords;
                offset < view.sizeWords();) {
                shader::SpirvInstructionView::Instruction i;
                assert(view.read(offset, i));
                const auto start = offset;
                offset = i.next();
                const bool member = i.opcode() == 72u && i.operand(0) == facts.blockTypeId;
                const bool variable = i.opcode() == 71u
                    && (i.operand(0) == facts.spirvId || i.operand(0) == facts.blockTypeId);
                const auto operand = member ? 2u : 1u;
                if ((!member && !variable) || i.operand(operand) != 24u)
                    continue;
                const auto byte = (start + 1u + operand) * sizeof(std::uint32_t);
                for (unsigned j = 0u; j < 4u; ++j) {
                    missing[byte + j] = std::byte{0u};
                    unreadable[byte + j] = std::byte(j ? 0u : 25u);
                }
                if (!appended) {
                    const auto* begin =
                        reinterpret_cast<const std::byte*>(binary.data()) + start * 4u;
                    duplicate.insert(duplicate.end(), begin, begin + (offset - start) * 4u);
                    appended = true;
                }
                changed = true;
            }
            assert(changed && appended);
            for (const auto* bytes : {&missing, &unreadable, &duplicate})
                assert(Verifier::inspectReadOnlyStorage(bytes->data(), bytes->size(), facts));
        }
    }

    static void verifyDepthTypes(
        const std::shared_ptr<const shader::SingleEntryShaderArtifact>& original)
    {
        assert(!shader::SingleEntryShaderArtifact::validateOriginal(original));
        for (const auto& binding : original->module().reflection.bindings) {
            if (!binding.depthTexture)
                continue;
            assert(binding.type == shader::BindingType::SampledTexture);
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            const auto& binary = original->bytes(shader::ShaderArtifactRole::Spirv);
            shader::SpirvImageDeclaration facts;
            assert(!shader::SpirvImageDeclarationVerifier::inspect(
                binary.data(), binary.size(), binding.set, binding.binding, facts));
            assert(facts.descriptorCount == 1u && facts.dimension == 1u && facts.depth == 1u
                && !facts.arrayed && !facts.multisampled && facts.sampled == 1u && facts.floatScalar
                && facts.scalarWidth == 32u);
            for (const auto role :
                {shader::ShaderArtifactRole::Metal, shader::ShaderArtifactRole::Wgsl}) {
                const auto& bytes = original->bytes(role);
                const std::string_view text(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size());
                assert(text.find(role == shader::ShaderArtifactRole::Metal ? "depth2d<float"
                                                                           : "texture_depth_2d")
                    != std::string_view::npos);
            }
            assert(before == core::defaultCpuAllocator().stats().allocationCount);
        }
    }

    static void verifyLoad(const std::filesystem::path& directory)
    {
        constexpr std::uint64_t bytes = 4u * 1024u * 1024u;
        const auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        core::pmr::String diagnostic;
        const auto path =
            directory / "shaders/original-cloud-compute/cloud_raymarch.computeMain.entry.yaml";
        {
            shader::SingleEntryShaderLoadBudget budget{bytes, bytes * 8u, bytes, 16u};
            const auto baseline =
                shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic);
            assert(baseline);
            const shader::NativeBufferMemoryDeclarations* memory = nullptr;
            assert(shader::SingleEntryShaderArtifact::originalNativeBufferMemory(baseline, memory));
            assert(!memory);
            CloudNativeMemoryFixture plan(baseline, Profile::Cloud);
            budget.nativeMemory = plan.get();
            auto original =
                shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic);
            assert(original
                && !shader::SingleEntryShaderArtifact::originalNativeBufferMemory(
                    original, memory));
            assert(memory && memory->lookup(0u, 0u));
            plan.wrongTargetMatrixPath();
            assert(memory->lookup(0u, 0u)->matrixPaths[0].steps[0].member == 0u);
            auto counterfeit = std::shared_ptr<const shader::SingleEntryShaderArtifact>(
                original.get(), [](const auto*) {});
            assert(
                shader::SingleEntryShaderArtifact::originalNativeBufferMemory(counterfeit, memory));
            assert(!memory);
            original.reset();
            plan.request.bindingCount -= 1u;
            assert(!shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic));
            assert(diagnostic == "SHADER-ENTRY-NATIVE-MEMORY-COVERAGE");
            plan.request.bindingCount += 1u;
            plan.request.sourceTypes.maxNodes = 1u;
            assert(!shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic));
            plan.request.sourceTypes = graphBudget;
            plan.bindings[0].matrixPathCount = 0u;
            plan.bindings[0].matrixPaths = nullptr;
            assert(!shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic));
            assert(diagnostic == "SHADER-NATIVE-MEMORY-MATRIX-PROOF");
        }
        assert(!quota->liveBytes());
        for (const auto* name : {"cloud_raymarch", "cloud_temporal", "cloud_spatial_resolve"}) {
            const auto publication = directory / "shaders/original-cloud-compute"
                / (std::string(name) + ".computeMain.entry.yaml");
            const auto original = loadSingle(publication, Profile::Cloud, quota, diagnostic);
            assert(original && original->nativeBufferMemoryRequested());
            verifyReadOnlyAccess(original);
            verifyDepthTypes(original);
        }
        for (const auto* name :
            {"cloud_composite", "cloud_spatial_composite", "cloud_foreground"}) {
            const bool foreground = std::string_view(name) == "cloud_foreground";
            const auto publication = directory
                / (foreground ? "shaders/cloud-foreground" : "shaders/original-cloud-graphics")
                / (std::string(name) + ".graphics.module.yaml");
            const auto original = loadGraphics(
                publication, foreground ? Profile::Foreground : Profile::Cloud, quota, diagnostic);
            assert(original && original->fragment()->nativeBufferMemoryRequested());
            verifyReadOnlyAccess(original->fragment());
            verifyDepthTypes(original->fragment());
        }
        assert(!quota->liveBytes());
        {
            const auto original = loadSingle(path, Profile::Cloud, quota, diagnostic);
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            const shader::NativeBufferMemoryDeclarations* memory = nullptr;
            quota->setLimitBytes(bytes - 1u);
            assert(shader::SingleEntryShaderArtifact::originalNativeBufferMemory(original, memory));
            assert(!memory && before == core::defaultCpuAllocator().stats().allocationCount);
            quota->setLimitBytes(bytes);
            assert(
                !shader::SingleEntryShaderArtifact::originalNativeBufferMemory(original, memory));
        }
        assert(!quota->liveBytes());
    }

    Request request;
    core::pmr::Vector<Request::Binding> bindings;

private:
    std::array<Path::Step, 2u> steps_;
    std::array<Request::MatrixTargetPath, 2u> matrices_;
    std::size_t matrixCount_ = 0u;
};
} // namespace arp::test
