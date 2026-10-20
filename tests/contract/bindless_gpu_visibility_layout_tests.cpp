#include "core/file_io.h"
#include "gpu_visibility_frozen_inputs_fixture.h"
#include "gpu_visibility_indirect_fetch_fixture.h"
#include "gpu_visibility_root_producer_fixture.h"
#include "pcg/gpu_visibility_layout.h"
#include "pcg/gpu_visibility_root_consumer.h"
#include "pcg/gpu_visibility_root_readback.h"
#include "shader/shader_artifact_files.h"
#include "shader/spirv_buffer_declaration_layout.h"
#include "shader/wgsl_buffer_declaration_layout.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error GPU visibility layout tests require assertions
#endif

int main(int argc, char** argv)
{
    using namespace arp;
    {
        using Aliases = rhi::visibility::GpuVisibilityConsumerAliasContract;
        using Type = rhi::DescriptorType;
        using Access = rhi::ResourceAccess;
        using Stage = rhi::ShaderStage;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::size_t ordinal = 0u; ordinal < Aliases::roles.size(); ++ordinal) {
            const auto stride = Aliases::strides[ordinal];
            const auto bytes = stride * (ordinal == 3u ? 1u : 5u);
            rhi::ShaderBufferMemoryRequirement memory;
            memory.evidence =
                rhi::ShaderBufferMemoryRequirement::Evidence::ValidatedTargetDeclaration;
            memory.hasRuntimeArray = ordinal != 3u;
            memory.sourceOccupiedBytes = ordinal == 3u ? stride : 0u;
            memory.minimumBytes = memory.sourceMinimumBytes = stride;
            memory.sourceRuntimeArrayStrideBytes = ordinal == 3u ? 0u : stride;
            memory.targetRuntimeArrayStrideBytes = memory.sourceRuntimeArrayStrideBytes;
            memory.sourceElementCapacity = ordinal == 3u ? 0u : 5u;
            const auto type = ordinal == 3u ? Type::UniformBuffer : Type::ReadOnlyStorageBuffer;
            const auto inspect = [&](const auto& proof,
                                     bool emulated,
                                     std::size_t source,
                                     Type selectedType,
                                     Stage stages,
                                     Access access,
                                     std::uint64_t offset,
                                     std::uint64_t size) {
                return Aliases::inspect(ordinal,
                    source,
                    Aliases::roles[ordinal],
                    selectedType,
                    stages,
                    access,
                    offset,
                    size,
                    bytes,
                    proof,
                    emulated);
            };
            for (const auto emulated : {false, true}) {
                assert(!inspect(memory,
                    emulated,
                    Aliases::roles[ordinal],
                    type,
                    Stage::Vertex,
                    Access::ShaderRead,
                    0u,
                    bytes));
                assert(inspect(memory,
                    emulated,
                    SIZE_MAX,
                    type,
                    Stage::Vertex,
                    Access::ShaderRead,
                    0u,
                    bytes));
                assert(inspect(memory,
                    emulated,
                    Aliases::roles[ordinal],
                    Type::StorageBuffer,
                    Stage::Vertex,
                    Access::ShaderRead,
                    0u,
                    bytes));
                assert(inspect(memory,
                    emulated,
                    Aliases::roles[ordinal],
                    type,
                    Stage::Fragment,
                    Access::ShaderRead,
                    0u,
                    bytes));
                assert(inspect(memory,
                    emulated,
                    Aliases::roles[ordinal],
                    type,
                    Stage::Vertex,
                    Access::ShaderRead | Access::ShaderWrite,
                    0u,
                    bytes));
                assert(inspect(memory,
                    emulated,
                    Aliases::roles[ordinal],
                    type,
                    Stage::Vertex,
                    Access::ShaderRead,
                    stride,
                    bytes));
                assert(inspect(memory,
                    emulated,
                    Aliases::roles[ordinal],
                    type,
                    Stage::Vertex,
                    Access::ShaderRead,
                    0u,
                    bytes - 1u));
                auto bad = memory;
                bad.evidence = rhi::ShaderBufferMemoryRequirement::Evidence::None;
                assert(inspect(bad,
                    emulated,
                    Aliases::roles[ordinal],
                    type,
                    Stage::Vertex,
                    Access::ShaderRead,
                    0u,
                    bytes));
                bad = memory;
                if (ordinal == 3u)
                    --bad.sourceOccupiedBytes;
                else
                    --bad.sourceRuntimeArrayStrideBytes;
                assert(inspect(bad,
                    emulated,
                    Aliases::roles[ordinal],
                    type,
                    Stage::Vertex,
                    Access::ShaderRead,
                    0u,
                    bytes));
            }
            if (ordinal != 3u) {
                --memory.targetRuntimeArrayStrideBytes;
                assert(inspect(memory,
                    true,
                    Aliases::roles[ordinal],
                    type,
                    Stage::Vertex,
                    Access::ShaderRead,
                    0u,
                    bytes));
            }
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    test::GpuVisibilityFrozenInputsFixture::verify();
    test::GpuVisibilityIndirectFetchFixture::verify();
    test::GpuVisibilityRootProducerFixture::verify();
    using Contract = pcg::GpuVisibilityLayoutContract;
    std::array<pcg::GpuVisibilityDrawGroup, 3u> groups{};
    for (std::uint32_t i = 0u; i < groups.size(); ++i) {
        groups[i].globalFirstInstance = i == 2u ? 2u : 0u;
        groups[i].globalInstanceCount = 2u;
        groups[i].materialIndex = i;
        groups[i].localBoundsCenterRadius[3] = 1.f;
        groups[i].cullDistance = 100.f;
        groups[i].castsShadow = 1u;
        groups[i].indexCount = 6u;
    }
    groups[2].shadowCascade1TemplateId = 1u;
    groups[2].shadowCascade1FirstIndex = 6u;
    groups[2].shadowCascade1IndexCount = 6u;
    std::array<std::uint32_t, 4u> owners{0u, 0u, 2u, 2u};
    const std::array<std::uint32_t, 2u> indices{6u, 12u};
    Contract::Input input{groups.data(), owners.data(), indices.data(), 3u, 4u, 2u, 3u, 4u, 4u, 3u};
    Contract::Budget budget{1u, true, 16u, 11u, 3u};
    {
        using Readbacks = pcg::GpuVisibilityRootReadbackContract;
        std::array<std::uint8_t, 16u> control;
        std::array<std::uint8_t, 4u> fault;
        const auto encode = [&](std::array<std::uint32_t, 4u> words, std::uint32_t bits) {
            core::UnsignedElementSpan::encodeWordsLittleEndian(words, control);
            core::UnsignedElementSpan::encodeWordsLittleEndian(
                std::array<std::uint32_t, 1u>{bits}, fault);
        };
        const auto inspect = [&]() {
            return Readbacks::inspectCompletedBytes(
                control.data(), control.size(), fault.data(), fault.size(), input);
        };
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        encode({0u, 3u, 3u, 3u}, 0u);
        assert(!inspect());
        // The un-compacted physical table has three records, including holes.
        encode({0u, 2u, 3u, 3u}, 0u);
        assert(core::StringView(inspect()) == "PCG-GPU-VISIBILITY-READBACK-DRAW-COUNT");
        encode({1u, 0u, 0u, 0u}, 0u);
        assert(core::StringView(inspect()) == "PCG-GPU-VISIBILITY-READBACK-PRODUCER-FAULT");
        encode({1u, 0u, 3u, 0u}, 0u);
        assert(core::StringView(inspect()) == "PCG-GPU-VISIBILITY-READBACK-DRAW-COUNT");
        for (const auto counts : {0u, 3u}) {
            encode({0u, counts, counts, counts}, 1u);
            assert(core::StringView(inspect()) == "PCG-GPU-VISIBILITY-READBACK-RESOURCE-FAULT");
        }
        assert(Readbacks::inspectCompletedBytes(
            control.data(), control.size() - 4u, fault.data(), fault.size(), input));
        assert(Readbacks::inspectCompletedBytes(
            control.data(), control.size(), nullptr, fault.size(), input));
        auto single = input;
        single.cascades = 1u;
        encode({0u, 3u, 0u, 0u}, 0u);
        assert(!Readbacks::inspectCompletedBytes(
            control.data(), 8u, fault.data(), fault.size(), single));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    {
        using Consumer = pcg::GpuVisibilityRootConsumerContract;
        std::array<std::uint8_t, 16u> bytes;
        core::UnsignedElementSpan::encodeWordsLittleEndian(
            std::array<std::uint32_t, 4u>{2u, 1u, 0u, 0u}, bytes);
        pcg::GpuVisibilityDrawSelection selection;
        const auto offset = std::uint64_t(2u * input.groupCapacity + 1u) * 20u;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Consumer::inspectSelection(bytes.data(), bytes.size(), input, offset, 1u, selection)
            && selection.cascade == 2u && selection.group == 1u);
        assert(Consumer::inspectSelection(bytes.data(), bytes.size(), input, offset, 2u, selection)
            && !selection.cascade && !selection.group);
        assert(Consumer::inspectSelection(
            bytes.data(), bytes.size(), input, offset - 20u, 1u, selection));
        for (const auto words : {std::array<std::uint32_t, 4u>{3u, 1u, 0u, 0u},
                 std::array<std::uint32_t, 4u>{2u, input.groupCount, 0u, 0u},
                 std::array<std::uint32_t, 4u>{2u, 1u, 1u, 0u},
                 std::array<std::uint32_t, 4u>{2u, 1u, 0u, UINT32_MAX}}) {
            core::UnsignedElementSpan::encodeWordsLittleEndian(words, bytes);
            assert(
                Consumer::inspectSelection(bytes.data(), bytes.size(), input, offset, 1u, selection)
                && !selection.cascade && !selection.group);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    Contract::Report report;
    const auto baselineAllocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!Contract::inspect(input, budget, report));
    assert(report.inputInstanceBytes == 256u && report.drawGroupBytes == 336u
        && report.ownerMapBytes == 16u && report.visibleInstanceBytes == 768u
        && report.countBytes == 48u && report.indirectBytes == 240u && report.drawCountBytes == 12u
        && report.zeroBaseMappingBytes == 192u && report.groupVisits == 16u
        && report.instanceVisits == 11u && report.pairVisits == 3u);
    const auto reject = [&](const Contract::Input& in,
                            const Contract::Budget& b,
                            const char* expected) {
        report.visibleInstanceBytes = UINT64_MAX;
        const auto* id = Contract::inspect(in, b, report);
        if (!id || core::StringView(id) != expected)
            std::fprintf(stderr, "Visibility expected %s got %s\n", expected, id ? id : "success");
        assert(id && core::StringView(id) == expected);
        const Contract::Report empty{};
        assert(!std::memcmp(&report, &empty, sizeof(report)));
    };
    for (const auto version : {0u, 2u, UINT32_MAX}) {
        auto wrong = budget;
        wrong.contractVersion = version;
        reject(input, wrong, "PCG-GPU-VISIBILITY-VERSION");
    }
    auto wrongBudget = budget;
    wrongBudget.optIn = false;
    reject(input, wrongBudget, "PCG-GPU-VISIBILITY-OPT-IN");
    for (const auto member : {&Contract::Budget::maxGroupVisits,
             &Contract::Budget::maxInstanceVisits,
             &Contract::Budget::maxPairVisits}) {
        wrongBudget = budget;
        --(wrongBudget.*member);
        reject(input, wrongBudget, "PCG-GPU-VISIBILITY-VISIT-CAPACITY");
    }
    auto wrong = input;
    wrong.materialCapacity = 2u;
    reject(wrong, budget, "PCG-GPU-VISIBILITY-GROUP-RANGE");
    wrong = input;
    wrong.instanceCapacity = 3u;
    reject(wrong, budget, "PCG-GPU-VISIBILITY-INPUT");
    wrong = input;
    wrong.cascades = 2u;
    reject(wrong, budget, "PCG-GPU-VISIBILITY-CASCADES");
    wrong = input;
    wrong.instanceCapacity = UINT32_MAX;
    reject(wrong, budget, "PCG-GPU-VISIBILITY-INDEX-OVERFLOW");
    wrong = input;
    wrong.groupCapacity = UINT32_MAX / 5u + 1u;
    reject(wrong, budget, "PCG-GPU-VISIBILITY-INDEX-OVERFLOW");
    wrong = input;
    wrong.cascades = 3u;
    wrong.groupCapacity = UINT32_MAX / 15u + 1u;
    reject(wrong, budget, "PCG-GPU-VISIBILITY-INDEX-OVERFLOW");
    const auto original = groups;
    // Empty ranges at the end must fail before indexing the owner map.
    groups[1].globalFirstInstance = input.instanceCount;
    groups[1].globalInstanceCount = 0u;
    reject(input, budget, "PCG-GPU-VISIBILITY-GROUP-RANGE");
    groups = original;
    groups[1].indexCount = 0u;
    reject(input, budget, "PCG-GPU-VISIBILITY-GROUP-RANGE");
    groups = original;
    groups[1].globalFirstInstance = 1u;
    reject(input, budget, "PCG-GPU-VISIBILITY-PARTIAL-OVERLAP");
    groups = original;
    owners[1] = 1u;
    reject(input, budget, "PCG-GPU-VISIBILITY-SPLIT-OWNER");
    owners[1] = 0u;
    owners[3] = 0u;
    reject(input, budget, "PCG-GPU-VISIBILITY-OWNER-RANGE");
    owners[3] = 2u;
    owners[0] = 3u;
    reject(input, budget, "PCG-GPU-VISIBILITY-OWNER");
    owners[0] = 0u;
    groups[1].cullDistance = 50.f;
    reject(input, budget, "PCG-GPU-VISIBILITY-SECTION-SEMANTICS");
    groups = original;
    groups[1].localBoundsCenterRadius[0] = 1.f;
    reject(input, budget, "PCG-GPU-VISIBILITY-SECTION-SEMANTICS");
    groups = original;
    groups[2].shadowCascade1IndexCount = 7u;
    reject(input, budget, "PCG-GPU-VISIBILITY-GROUP-RANGE");
    groups = original;
    groups[0].localBoundsCenterRadius[3] = std::numeric_limits<float>::quiet_NaN();
    reject(input, budget, "PCG-GPU-VISIBILITY-BOUNDS");
    groups = original;
    groups[0].castsShadow = 2u;
    reject(input, budget, "PCG-GPU-VISIBILITY-FLAG");
    groups = original;
    input.cascades = 1u;
    assert(!Contract::inspect(input, budget, report) && report.indirectBytes == 80u
        && report.visibleInstanceBytes == 256u);
    assert(report.controlBytes == 8u && report.parameterBytes == 448u);
    pcg::GpuVisibilityParameters parameters{};
    parameters.counts[0] = input.instanceCount;
    parameters.counts[1] = input.groupCount;
    parameters.counts[2] = input.cascades;
    parameters.capacities[0] = input.instanceCapacity;
    parameters.capacities[1] = input.groupCapacity;
    parameters.capacities[2] = input.materialCapacity;
    parameters.capacities[3] = parameters.policy[3] = 1u;
    parameters.cameraAndEnvelope[3] = 0.35f; // Actual authored wind bound, not an engine default.
    parameters.selection[0] = 80.f;
    for (std::size_t plane = 0u; plane < 18u; ++plane) {
        parameters.frustumPlanes[plane][plane % 3u] = plane & 1u ? -1.f : 1.f;
        parameters.frustumPlanes[plane][3] = 10.f;
    }
    assert(!Contract::inspectParameters(input, parameters, false));
    const auto rejectParameters = [&](const pcg::GpuVisibilityParameters& bad,
                                      const char* expected) {
        const auto* id = Contract::inspectParameters(input, bad, false);
        assert(id && core::StringView(id) == expected);
    };
    auto badParameters = parameters;
    badParameters.counts[3] = 1u;
    rejectParameters(badParameters, "PCG-GPU-VISIBILITY-FIRST-INSTANCE-CAPABILITY");
    assert(!Contract::inspectParameters(input, badParameters, true));
    badParameters = parameters;
    badParameters.capacities[3] = 0u;
    rejectParameters(badParameters, "PCG-GPU-VISIBILITY-PARAMETER-VERSION");
    badParameters = parameters;
    badParameters.counts[1] += 1u;
    rejectParameters(badParameters, "PCG-GPU-VISIBILITY-PARAMETER-CORRESPONDENCE");
    badParameters = parameters;
    badParameters.cameraAndEnvelope[3] = -1.f;
    rejectParameters(badParameters, "PCG-GPU-VISIBILITY-PARAMETER-ENVELOPE");
    badParameters = parameters;
    badParameters.frustumPlanes[0][0] = 0.f;
    rejectParameters(badParameters, "PCG-GPU-VISIBILITY-PARAMETER-PLANE");
    badParameters = parameters;
    badParameters.frustumPlanes[0][0] = std::numeric_limits<float>::infinity();
    rejectParameters(badParameters, "PCG-GPU-VISIBILITY-PARAMETER-FINITE");
    badParameters = parameters;
    badParameters.policy[1] = 1u;
    rejectParameters(badParameters, "PCG-GPU-VISIBILITY-PARAMETER-POLICY");
    badParameters = parameters;
    badParameters.depthInfo[0] = 4u;
    rejectParameters(badParameters, "PCG-GPU-VISIBILITY-PYRAMID-DISABLED-SCHEMA");
    rhi::TextureDesc pyramid;
    pyramid.extent = {4u, 4u, 1u};
    pyramid.mipLevelCount = 3u;
    pyramid.format = rhi::TextureFormat::Rgba16Float;
    pyramid.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
    parameters.depthInfo[0] = parameters.depthInfo[1] = 4u;
    parameters.depthInfo[2] = 3u;
    parameters.depthInfo[3] = 1u;
    parameters.viewProjectionRows[0][0] = parameters.viewProjectionRows[1][1] = 1.f;
    parameters.viewProjectionRows[2][2] = parameters.viewProjectionRows[3][3] = 1.f;
    assert(!Contract::inspectParameters(input, parameters, false, &pyramid));
    --pyramid.mipLevelCount;
    assert(Contract::inspectParameters(input, parameters, false, &pyramid));
    ++pyramid.mipLevelCount;
    parameters.policy[0] = 1u;
    assert(Contract::inspectParameters(input, parameters, false, &pyramid));
    parameters.depthInfo[0] = parameters.depthInfo[1] = parameters.depthInfo[2] =
        parameters.depthInfo[3] = 0u;
    parameters.counts[2] = input.cascades = 3u;
    assert(!Contract::inspectParameters(input, parameters, false));
    using Launch = pcg::GpuVisibilityDispatchContract;
    pcg::GpuVisibilityOutputRanges outputRanges;
    assert(!Launch::outputRanges(input, outputRanges) && outputRanges.drawWords == 60u
        && outputRanges.baseSlots == 12u && outputRanges.controlWords == 4u
        && outputRanges.contractVersion == 1u);
    for (const auto phase :
        {Launch::Phase::Reset, Launch::Phase::Cull, Launch::Phase::Emit, Launch::Phase::Finalize}) {
        Launch::Dispatch dispatch;
        assert(!Launch::derive(input, phase, 3u, dispatch));
        assert(!Launch::validate(input, phase, 3u, dispatch.x, dispatch.y, dispatch.z));
        assert(Launch::validate(input, phase, 3u, dispatch.x + 1u, dispatch.y, dispatch.z));
        assert(Launch::validate(input, phase, 3u, dispatch.x, dispatch.y, dispatch.z + 1u));
        assert(Launch::derive(input, phase, 0u, dispatch) && !dispatch.x && !dispatch.y);
    }
    Launch::Dispatch dispatch;
    assert(!Launch::derive(input, Launch::Phase::Finalize, 3u, dispatch) && dispatch.x == 1u
        && dispatch.y == 1u && dispatch.logicalInvocations == 12u);
    auto wide = input;
    wide.groupCapacity = 65u;
    assert(!Launch::derive(wide, Launch::Phase::Finalize, 4u, dispatch) && dispatch.x == 4u
        && dispatch.y == 1u && dispatch.logicalInvocations == 195u);
    assert(Launch::derive(wide, Launch::Phase::Finalize, 3u, dispatch) && !dispatch.x);
    wide.cascades = wide.groupCapacity = wide.groupCount = wide.instanceCount = 1u;
    assert(!Launch::derive(wide, Launch::Phase::Finalize, 1u, dispatch)
        && dispatch.logicalInvocations == 2u);
    assert(Launch::derive(wide, static_cast<Launch::Phase>(255u), 1u, dispatch) && !dispatch.x);
    assert(core::defaultCpuAllocator().stats().allocationCount == baselineAllocations);
    if (argc == 3 || argc == 11) {
        const auto spv = core::readBinaryFile(argv[1], 1024u * 1024u);
        const auto wgsl = core::readBinaryFile(argv[2], 1024u * 1024u);
        assert(spv.success && wgsl.success);
        core::pmr::Vector<shader::SpirvBufferDeclarationLayout> source;
        core::pmr::Vector<shader::WgslBufferDeclarationLayout> target;
        assert(!shader::SpirvBufferDeclarationLayoutVerifier::verify(
            spv.bytes.data(), spv.bytes.size(), 4u, source));
        assert(!shader::WgslBufferDeclarationLayoutVerifier::verify(
            shader::shaderArtifactText(wgsl.bytes), 4u, target));
        assert(source.size() == 4u && target.size() == 4u);
        constexpr std::array<std::uint64_t, 4u> strides{112u, 20u, 16u, 16u};
        for (std::size_t binding = 0u; binding < strides.size(); ++binding) {
            std::size_t sourceMatches = 0u, targetMatches = 0u;
            for (const auto& declaration : source)
                if (declaration.set == 0u && declaration.binding == binding) {
                    assert(declaration.hasRuntimeArray
                        && declaration.runtimeArrayStrideBytes == strides[binding]);
                    ++sourceMatches;
                }
            for (const auto& declaration : target)
                if (declaration.group == 0u && declaration.binding == binding) {
                    assert(declaration.hasRuntimeArray
                        && declaration.runtimeArrayStrideBytes == strides[binding]);
                    ++targetMatches;
                }
            assert(sourceMatches == 1u && targetMatches == 1u);
        }
        std::puts(
            "BINDLESS_GPU_VISIBILITY_SHADER_ABI_OK actual-SPIRV-WGSL strides112-20-16-16 no-execution");
    } else
        assert(argc == 1);
    if (argc == 11) {
        using Types = shader::BufferDeclarationTypeLayouts;
        constexpr Types::Budget typeBudget{512u, 512u, 16u};
        constexpr std::array<std::uint64_t, 8u> offsets{0u, 16u, 32u, 48u, 64u, 352u, 416u, 432u};
        const auto uniform = [&](const Types& types, bool finalized, bool targetBindings) {
            std::size_t matches = 0u;
            std::size_t rangeMatches = 0u;
            for (const auto& root : types.roots()) {
                if (root.storage != Types::Storage::Uniform)
                    continue;
                auto node = types.nodes()[root.type];
                // WGSL compacts the active resources per entry. Compare
                // complete layouts; authored identities are checked in SPIR-V.
                if (node.occupiedExtentBytes == sizeof(pcg::GpuVisibilityOutputRanges)) {
                    ++rangeMatches;
                    assert(root.set == 0u && (targetBindings || root.binding == 10u)
                        && node.occupiedExtentBytes == sizeof(pcg::GpuVisibilityOutputRanges));
                    while (node.kind == Types::Kind::Struct && node.memberCount == 1u) {
                        const auto& member = types.members()[node.memberBegin];
                        assert(!member.offsetBytes);
                        node = types.nodes()[member.type];
                    }
                    assert(node.kind == Types::Kind::Vector && node.count == 4u);
                    const auto& scalar = types.nodes()[node.child];
                    assert(scalar.kind == Types::Kind::Scalar && scalar.scalarBits == 32u
                        && scalar.scalar == Types::Scalar::UnsignedInteger);
                    continue;
                }
                ++matches;
                assert(root.set == 0u && (targetBindings || root.binding == 8u));
                while (node.kind == Types::Kind::Struct && node.memberCount == 1u) {
                    const auto& member = types.members()[node.memberBegin];
                    assert(!member.offsetBytes);
                    node = types.nodes()[member.type];
                }
                assert(node.kind == Types::Kind::Struct && node.memberCount == offsets.size()
                    && node.occupiedExtentBytes == sizeof(pcg::GpuVisibilityParameters));
                for (std::size_t i = 0u; i < offsets.size(); ++i)
                    assert(types.members()[node.memberBegin + i].offsetBytes == offsets[i]);
            }
            assert(matches == 1u);
            assert(rangeMatches == std::size_t(finalized));
        };
        const std::array<core::pmr::Vector<std::uint64_t>, 4u> expected{{{4u, 4u, 16u, 20u},
            {4u, 4u, 4u, 64u, 64u, 112u},
            {4u, 4u, 4u, 16u, 20u, 112u},
            {4u, 16u, 20u}}};
        for (std::size_t phase = 0u; phase < 4u; ++phase) {
            const auto spv = core::readBinaryFile(argv[3u + phase * 2u], 1024u * 1024u);
            const auto wgsl = core::readBinaryFile(argv[4u + phase * 2u], 1024u * 1024u);
            assert(spv.success && wgsl.success);
            Types sourceTypes, targetTypes;
            core::pmr::Vector<shader::SpirvBufferDeclarationLayout> source;
            core::pmr::Vector<shader::WgslBufferDeclarationLayout> target;
            const auto* id = shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                spv.bytes.data(), spv.bytes.size(), 16u, typeBudget, source, sourceTypes);
            if (id)
                std::fprintf(stderr, "Visibility SPIRV phase%zu: %s\n", phase, id);
            assert(!id);
            id = shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
                shader::shaderArtifactText(wgsl.bytes), 16u, typeBudget, target, targetTypes);
            if (id)
                std::fprintf(stderr, "Visibility WGSL phase%zu: %s\n", phase, id);
            assert(!id);
            uniform(sourceTypes, phase == 3u, false);
            uniform(targetTypes, phase == 3u, true);
            core::pmr::Vector<std::uint64_t> sourceStrides, targetStrides;
            for (const auto& declaration : source)
                if (declaration.hasRuntimeArray)
                    sourceStrides.push_back(declaration.runtimeArrayStrideBytes);
            for (const auto& declaration : target)
                if (declaration.hasRuntimeArray)
                    targetStrides.push_back(declaration.runtimeArrayStrideBytes);
            std::sort(sourceStrides.begin(), sourceStrides.end());
            std::sort(targetStrides.begin(), targetStrides.end());
            assert(sourceStrides == expected[phase] && targetStrides == expected[phase]);
        }
        std::puts(
            "BINDLESS_GPU_VISIBILITY_KERNEL_ABI_OK reset-cull-emit-finalize Uniform448 actual-SPIRV-WGSL no-execution");
    }
    std::puts(
        "BINDLESS_GPU_VISIBILITY_LAYOUT_OK Instance64 DrawGroup112 Indexed20 alias-owner material-index exact-budgets zero-failure hot0 no-execution");
}
