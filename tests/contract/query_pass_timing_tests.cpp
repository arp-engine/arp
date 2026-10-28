#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/frontend/command_render_pass_preparation.h"
#include "rhi/frontend/query_pass_timing.h"
#include "rhi/frontend/query_recording_storage.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>

#ifdef NDEBUG
#error Pass timing tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using Contract = OriginalPassTimingContract;
using A = CommandScalarArguments;
using K = CommandUsageKind;
using Stream = CommandVariableArguments;
using Points = QueryCommandPoints;

class Fixture final {
public:
    static constexpr std::uint64_t cpuBytes = 65536u;
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u> quotas;
    core::pmr::Vector<Stream::Input> commands;
    core::pmr::Vector<Points::Input> bindings;
    std::shared_ptr<const QueryPoolInputSnapshot> pool;
    std::shared_ptr<const Stream> stream;
    const char* diagnostic = nullptr;
    OriginalPassTimingBudget budget{1u, true, 3u, 6u, 3u, 21u, 64u};

    explicit Fixture(bool empty = false)
    {
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const std::array<std::uint64_t, 3u> ids{11u, 23u, 37u};
        pool = snapshot(ids.data(), ids.size());
        for (std::uint32_t pass = 0u; pass < 3u; ++pass) {
            if (pass == 2u) {
                append(K::BeginTransferPass);
                append(K::EndTransferPass);
            }
            const bool graphics = pass == 1u;
            append(graphics ? K::BeginRenderPass : K::BeginComputePass);
            query(pass * 2u, pool);
            if (!empty) {
                append(graphics ? K::BindGraphicsPipeline : K::BindComputePipeline);
                commands.push_back({graphics ? *A::capture(K::Draw, A::Draw{3u, 1u, 0u, 0u})
                                             : *A::capture(K::Dispatch, A::Dispatch{1u, 1u, 1u})});
            }
            query(pass * 2u + 1u, pool);
            append(graphics ? K::EndRenderPass : K::EndComputePass);
        }
        append(K::Finish);
    }

    std::shared_ptr<const QueryPoolInputSnapshot> snapshot(
        const std::uint64_t* ids, std::size_t count, QueryType type = QueryType::Timestamp)
    {
        QueryPoolDesc desc;
        desc.label = "PassTiming.Original";
        desc.type = type;
        desc.count = type == QueryType::Timestamp ? static_cast<std::uint32_t>(count * 2u) : 1u;
        desc.timingScopeIds = ids;
        desc.timingScopeIdCount = static_cast<std::uint32_t>(count);
        if (type == QueryType::PipelineStatistics)
            desc.pipelineStatisticsMask = kAllPipelineStatistics;
        const QueryPoolInputSnapshotBudget b{
            1u, true, 4u, desc.label.size(), cpuBytes, cpuBytes, cpuBytes};
        auto result = QueryPoolInputSnapshot::prepareOriginal(
            desc, b, quotas[0], quotas[1], quotas[2], diagnostic);
        assert(result && !diagnostic);
        return result;
    }

    void replace(std::size_t point,
        std::uint32_t slot,
        const std::shared_ptr<const QueryPoolInputSnapshot>& owner,
        K kind = K::Timestamp)
    {
        auto& binding = bindings[point];
        binding.original = *QueryPoolOriginalInputArguments::captureOriginal(kind, slot, owner);
        commands[binding.commandOrdinal].scalar = binding.original.scalar();
    }

    std::shared_ptr<const Points> prepare()
    {
        const CommandVariableArgumentsBudget b{1u, true, 21u, 0u, 0u, 0u, cpuBytes};
        stream =
            Stream::prepareOriginal(commands.data(), commands.size(), b, quotas[3], diagnostic);
        assert(stream && !diagnostic);
        const QueryCommandPointsBudget q{
            1u, true, 21u, 4u, 6u, 0u, 0u, 0u, 0u, 48u, 24u, 128u, cpuBytes};
        auto result = Points::prepareOriginal(
            stream, bindings.data(), bindings.size(), q, quotas[4], diagnostic);
        if (!result)
            std::fprintf(stderr,
                "Query points preparation: %s\n",
                diagnostic ? diagnostic : "missing diagnostic");
        assert(result && !diagnostic);
        return result;
    }

    void reject(const char* expected)
    {
        const auto points = prepare();
        const auto result = Contract::inspect(points, stream, budget);
        assert(result.diagnostic && std::string_view(result.diagnostic) == expected);
        std::size_t calls = 0u;
        const auto visited = Contract::visit(
            points, stream, budget, [&](const Contract::ScopeView&) noexcept -> const char* {
                ++calls;
                return nullptr;
            });
        assert(visited.diagnostic && std::string_view(visited.diagnostic) == expected && !calls);
    }

private:
    void append(K kind) { commands.push_back({*A::capture(kind, A::None{})}); }

    void query(std::uint32_t slot, const std::shared_ptr<const QueryPoolInputSnapshot>& owner)
    {
        const auto original =
            *QueryPoolOriginalInputArguments::captureOriginal(K::Timestamp, slot, owner);
        bindings.push_back({commands.size(), original});
        commands.push_back({original.scalar()});
    }
};

void verifyContract()
{
    Fixture f;
    const auto points = f.prepare();
    const auto result = Contract::inspect(points, f.stream, f.budget);
    assert(!result.diagnostic && result.poolInputs == 1u && result.timestampWrites == 6u
        && result.publishedScopes == 3u && result.pointVisits > 0u);
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto decision = Contract::lowering(backend, f.budget);
        assert(!decision.recordingAvailable && decision.diagnostic);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(decision.inspectionAvailable == (backend != Backend::WebGL));
    }
    assert(!Contract::lowering(static_cast<Backend>(255u), f.budget).inspectionAvailable);
    auto invalid = f.budget;
    for (const auto version : {0u, 2u}) {
        invalid.contractVersion = version;
        assert(std::string_view(Contract::validateIntent(invalid)) == "RHI-PASS-TIMING-VERSION");
    }
    invalid = f.budget;
    invalid.prototypeOptIn = false;
    assert(std::string_view(Contract::validateIntent(invalid)) == "RHI-PASS-TIMING-OPT-IN");
    for (auto member : {&OriginalPassTimingBudget::maxPoolInputs,
             &OriginalPassTimingBudget::maxTimestampWrites,
             &OriginalPassTimingBudget::maxPublishedScopes}) {
        invalid = f.budget;
        invalid.*member = 0u;
        assert(Contract::validateIntent(invalid));
        for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
            assert(!Contract::lowering(backend, invalid).inspectionAvailable);
    }
    invalid = f.budget;
    invalid.maxPublishedScopes = GpuTimingSample::kMaxScopes + 1u;
    assert(Contract::validateIntent(invalid));
    const auto rejectBudget = [&](const OriginalPassTimingBudget& b, const char* id) {
        const auto checked = Contract::inspect(points, f.stream, b);
        assert(checked.diagnostic && std::string_view(checked.diagnostic) == id);
    };
    invalid = f.budget;
    --invalid.maxStreamCommands;
    rejectBudget(invalid, "RHI-PASS-TIMING-COMMAND-CAPACITY");
    invalid = f.budget;
    --invalid.maxTimestampWrites;
    rejectBudget(invalid, "RHI-PASS-TIMING-WRITE-CAPACITY");
    invalid = f.budget;
    --invalid.maxPublishedScopes;
    rejectBudget(invalid, "RHI-PASS-TIMING-PUBLICATION-CAPACITY");
    invalid = f.budget;
    invalid.maxPointVisits = result.pointVisits - 1u;
    rejectBudget(invalid, "RHI-PASS-TIMING-POINT-CAPACITY");
    f.budget.maxPointVisits = result.pointVisits;
    assert(!Contract::inspect(points, f.stream, f.budget).diagnostic);
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        f.quotas, [&]() noexcept -> const char* {
            std::size_t calls = 0u;
            const auto visited = Contract::visit(points,
                f.stream,
                f.budget,
                [&](const Contract::ScopeView& scope) noexcept -> const char* {
                    constexpr std::array<std::uint64_t, 3u> ids{11u, 23u, 37u};
                    constexpr std::array<std::size_t, 3u> passes{0u, 1u, 3u};
                    assert(scope.scopeId == ids[calls] && scope.passIndex == passes[calls]
                        && scope.beginPointIndex == calls * 2u
                        && scope.endPointIndex == calls * 2u + 1u);
                    ++calls;
                    return nullptr;
                });
            assert(!visited.diagnostic && calls == 3u && visited.pointVisits == result.pointVisits);
            assert(!Contract::inspect(points, f.stream, f.budget).diagnostic);
            return nullptr;
        }));
    const auto oldStream = f.stream;
    auto foreign = f.prepare();
    assert(Contract::inspect(points, f.stream, f.budget).diagnostic);
    auto alias = std::shared_ptr<const Points>(f.quotas[4], points.get());
    assert(Contract::inspect(alias, oldStream, f.budget).diagnostic);
    assert(Contract::inspect({}, oldStream, f.budget).diagnostic);
    assert(Contract::inspect(points, {}, f.budget).diagnostic);
    const auto refusedVisitor =
        Contract::visit(points, oldStream, f.budget, [](const Contract::ScopeView&) noexcept {
            return "TEST-VISITOR-REFUSAL";
        });
    assert(std::string_view(refusedVisitor.diagnostic) == "TEST-VISITOR-REFUSAL");
    // The pass declaration must not extend whole-command profiling /1.
    assert(!CommandProfilingContract::validateIntent({1u, true, 1u, 2u, 1u}));
    assert(CommandProfilingContract::validateIntent({2u, true, 1u, 2u, 1u}));
    std::puts(
        "PASS_TIMING_CONTRACT pairs3 pools1 render-compute exact-stream budgets allocation0 PASS");
}

void verifyMalformedPairs()
{
    {
        Fixture f;
        f.replace(0u, 1u, f.pool);
        f.replace(1u, 0u, f.pool);
        f.reject("RHI-PASS-TIMING-SLOT-PAIR");
    }
    {
        Fixture f;
        const std::array<std::uint64_t, 3u> ids{11u, 23u, 37u};
        auto sameFields = f.snapshot(ids.data(), ids.size());
        f.replace(1u, 1u, sameFields);
        f.reject("RHI-PASS-TIMING-SLOT-PAIR");
    }
    {
        Fixture f;
        f.replace(2u, 0u, f.pool);
        f.replace(3u, 1u, f.pool);
        f.reject("RHI-PASS-TIMING-SCOPE-IDENTITY");
    }
    {
        Fixture f;
        const std::array<std::uint64_t, 4u> ids{11u, 23u, 37u, 41u};
        auto partial = f.snapshot(ids.data(), ids.size());
        for (std::size_t i = 0u; i < f.bindings.size(); ++i)
            f.replace(i, static_cast<std::uint32_t>(i), partial);
        f.reject("RHI-PASS-TIMING-POOL-COVERAGE");
    }
    {
        Fixture f;
        const std::array<std::uint64_t, 3u> ids{0u, 23u, 37u};
        auto reserved = f.snapshot(ids.data(), ids.size());
        for (std::size_t i = 0u; i < f.bindings.size(); ++i)
            f.replace(i, static_cast<std::uint32_t>(i), reserved);
        f.reject("RHI-PASS-TIMING-SCOPE-IDENTITY");
    }
    {
        Fixture f;
        std::swap(f.commands[1u], f.commands[2u]);
        f.bindings[0u].commandOrdinal = 2u;
        f.reject("RHI-PASS-TIMING-PASS-BOUNDARY");
    }
    {
        Fixture f(true);
        f.reject("RHI-PASS-TIMING-EMPTY-INTERVAL");
    }
    {
        Fixture f;
        auto stats = f.snapshot(nullptr, 0u, QueryType::PipelineStatistics);
        f.replace(0u, 0u, stats, K::PipelineStatistics);
        f.reject("RHI-PASS-TIMING-QUERY-KIND");
    }
    {
        Fixture f;
        f.commands[f.bindings.back().commandOrdinal].scalar =
            *A::capture(K::RetainTexture, A::None{});
        f.bindings.pop_back();
        f.reject("RHI-PASS-TIMING-PAIR-COVERAGE");
    }
    {
        Fixture f;
        const auto marker = *A::capture(K::RetainTexture, A::None{});
        f.commands[f.bindings[1u].commandOrdinal].scalar = marker;
        f.commands[f.bindings[2u].commandOrdinal].scalar = marker;
        f.bindings.erase(f.bindings.begin() + 1u, f.bindings.begin() + 3u);
        const std::array<std::uint64_t, 2u> ids{11u, 23u};
        auto pool = f.snapshot(ids.data(), ids.size());
        for (std::size_t i = 0u; i < f.bindings.size(); ++i)
            f.replace(i, static_cast<std::uint32_t>(i), pool);
        f.reject("RHI-PASS-TIMING-PASS-PAIR");
    }
    {
        Fixture f;
        for (std::size_t i = 0u; i < 3u; ++i) {
            const std::uint64_t id = 41u + i;
            auto pool = f.snapshot(&id, 1u);
            f.replace(i * 2u, 0u, pool);
            f.replace(i * 2u + 1u, 1u, pool);
        }
        auto points = f.prepare();
        const auto checked = Contract::inspect(points, f.stream, f.budget);
        assert(!checked.diagnostic && checked.poolInputs == 3u);
        --f.budget.maxPoolInputs;
        f.reject("RHI-PASS-TIMING-POOL-CAPACITY");
        ++f.budget.maxPoolInputs;
        const std::uint64_t collision = 41u;
        auto pool = f.snapshot(&collision, 1u);
        f.replace(2u, 0u, pool);
        f.replace(3u, 1u, pool);
        f.reject("RHI-PASS-TIMING-SCOPE-IDENTITY");
    }
    std::puts(
        "PASS_TIMING_NEGATIVES placement pairs slots scope-identities full-pools no-partial-visits PASS");
}

void verifyMixedInputsSchema()
{
    Fixture f;
    const auto points = f.prepare();
    OriginalCommandInputsBudget budget;
    budget.contractVersion = 11u;
    budget.prototypeOptIn = true;
    budget.maxPipelineBindings = 3u;
    budget.maxDescriptorBindings = 3u;
    budget.maxQueryBindings = 6u;
    budget.queryPoints = points;
    budget.passTiming = f.budget;
    using InputsContract = OriginalCommandPipelineBindingContract;
    assert(!InputsContract::validateBudget(budget));
    const auto reject = [&](const OriginalCommandInputsBudget& b, const char* id) {
        const auto* diagnostic = InputsContract::validateBudget(b);
        assert(diagnostic && std::string_view(diagnostic) == id);
    };
    for (std::uint32_t version = 1u; version <= 10u; ++version) {
        auto previous = budget;
        previous.contractVersion = version;
        reject(previous, "RHI-ORIGINAL-PASS-TIMING-PROFILE");
    }
    auto invalid = budget;
    invalid.passTiming.reset();
    reject(invalid, "RHI-ORIGINAL-PASS-TIMING-PROFILE");
    invalid = budget;
    invalid.passTiming->prototypeOptIn = false;
    reject(invalid, "RHI-PASS-TIMING-OPT-IN");
    invalid = budget;
    --invalid.maxQueryBindings;
    reject(invalid, "RHI-ORIGINAL-PASS-TIMING-WRITE-BUDGET");
    invalid = budget;
    invalid.queryPoints.reset();
    reject(invalid, "RHI-ORIGINAL-QUERY-POSITIONAL-BUDGET");
    for (auto field : {&OriginalCommandInputsBudget::maxVertexBindings,
             &OriginalCommandInputsBudget::maxIndexBindings}) {
        invalid = budget;
        invalid.*field = 1u;
        reject(invalid, "RHI-ORIGINAL-INPUTS-VERSION");
    }
    invalid = budget;
    invalid.maxTableBindings = 1u;
    reject(invalid, "RHI-ORIGINAL-INPUTS-GENERAL-FINITE-SCHEMA");
    invalid = budget;
    invalid.maxDescriptorBindings = 0u;
    reject(invalid, "RHI-ORIGINAL-INPUTS-GENERAL-FINITE-SCHEMA");
    invalid = budget;
    invalid.prototypeOptIn = false;
    reject(invalid, "RHI-ORIGINAL-TYPED-INPUT-OPT-IN");
    for (const auto sourceVersion : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) {
        const auto* diagnostic =
            OriginalCommandDescriptorBindingContract::validateSetProfile(11u, sourceVersion);
        assert(bool(diagnostic) == (sourceVersion != 5u && sourceVersion != 7u));
    }
    assert(OriginalCommandDescriptorConsumptionContract::inputsProfileMatches(4u, 2u, true, 11u));
    assert(!OriginalCommandDescriptorConsumptionContract::inputsProfileMatches(3u, 1u, true, 11u));
    assert(!OriginalCommandDescriptorConsumptionContract::inputsProfileMatches(4u, 2u, false, 11u));
    assert(OriginalQueryRecordingStorageContract::inspectPassTiming({}, {}, {}).diagnostic);
    assert(OriginalQueryRecordingStorageContract::inspect({}, {}, {}).diagnostic);
    using Sources = OriginalCommandResourceSources;
    for (const auto sourceVersion : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u})
        assert(Sources::allowsDescriptorSourceVersion(9u, sourceVersion)
            == (sourceVersion == 5u || sourceVersion == 7u));
    assert(!Sources::allowsDescriptorSourceVersion(7u, 7u));
    assert(Sources::allowsDescriptorSourceVersion(8u, 7u));
    OriginalCommandAdmissionBudget admission;
    admission.contractVersion = 1u;
    admission.prototypeOptIn = true;
    admission.maxCommands = 1u;
    admission.maxGraphOwners = 1u;
    admission.maxCpuBytes.fill(Fixture::cpuBytes);
    const auto quota = core::makeShared<core::CpuMemoryQuota>(Fixture::cpuBytes);
    const char* diagnostic = nullptr;
    assert(!Sources::prepareColdOriginalTyped(
        nullptr, 0u, admission, {9u, true, 1u}, quota, diagnostic));
    assert(diagnostic
        && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-ASSEMBLY-PASS-TIMING-PROFILE");
    admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Query)] = 1u;
    for (const auto domain :
        {CommandReferenceDomain::DescriptorTable, CommandReferenceDomain::BindlessRoot}) {
        auto forbidden = admission;
        forbidden.maxSourceOwners[static_cast<std::size_t>(domain)] = 1u;
        assert(!Sources::prepareColdOriginalTyped(
            nullptr, 0u, forbidden, {9u, true, 2u}, quota, diagnostic));
        assert(diagnostic
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-ASSEMBLY-PASS-TIMING-PROFILE");
    }
    assert(!quota->liveBytes());
    std::puts(
        "PASS_TIMING_INPUTS11_SCHEMA explicit-profile original5-and7 descriptors old-profiles-closed PASS");
}

void verifyMixedRecordingContract()
{
    OriginalRenderPassPreparationRequest request{8u,
        true,
        {1u, true, 2u, 2u, 4u, {1u, true, 2u, 0u, 0u, 0u, 0u, Fixture::cpuBytes}},
        8u,
        2u,
        OriginalRenderPassContentBudget{5u, true, 4u, 9u, 16u, 20u, 32u},
        OriginalRenderPassRecordingBudget{5u, true, 28u, 52u}};
    using C = OriginalRenderPassPreparationContract;
    assert(C::validate(request));
    request.passTimingRetention =
        CommandRetentionBudget{1u, true, 0u, 3u, 6u, 0u, Fixture::cpuBytes};
    assert(C::validate(request));
    request.passTimingRecordingQuota = core::makeShared<core::CpuMemoryQuota>(Fixture::cpuBytes);
    assert(C::validate(request));
    request.passTimingFrameSerial = 31u;
    assert(!C::validate(request));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = C::lowering(backend, request);
        assert(decision.recordingAvailable == (backend == Backend::Vulkan)
            && decision.privatePreparationAvailable == (backend == Backend::Vulkan)
            && decision.validatorOnly == (backend == Backend::Null)
            && bool(decision.diagnostic) == (backend != Backend::Vulkan));
    }
    for (const auto version : {5u, 6u, 7u}) {
        auto bad = request;
        bad.contractVersion = version;
        bad.content->contractVersion = version - 3u;
        bad.recording->contractVersion = version - 3u;
        assert(C::validate(bad));
    }
    auto bad = request;
    bad.passTimingFrameSerial = 0u;
    assert(std::string_view(C::validate(bad)) == "RHI-MIXED-PASS-TIMING-FRAME-SERIAL");
    bad = request;
    bad.prototypeOptIn = false;
    assert(C::validate(bad));
    bad = request;
    bad.passTimingRetention->optIn = false;
    assert(C::validate(bad));
    bad = request;
    --bad.passTimingRetention->maxCpuMetadataBytes;
    assert(C::validate(bad));
    bad = request;
    bad.recording->dispatchDomainVersion = 1u;
    bad.recording->dispatchDomainOptIn = true;
    assert(C::validate(bad));
    std::puts(
        "MIXED_TIMING_RECORDING_CONTRACT Preparation8 explicit-retention old-profiles-closed four-backend-gates PASS");
}
} // namespace

int main(int argc, char** argv)
{
    verifyContract();
    verifyMalformedPairs();
    verifyMixedInputsSchema();
    verifyMixedRecordingContract();
    if (argc == 2 && std::string_view(argv[1]) == "--null-hidden-frame") {
        auto device = createNullDevice();
        assert(device);
        NullHiddenFrameSmokeRunner runner(*device);
        const auto report = runner.run({});
        if (!report.success)
            std::fprintf(stderr, "%s\n", report.diagnostics.c_str());
        assert(report.success && report.submittedFrameCount == 2u
            && report.descriptorCompletenessValidated && !report.nativeHandleExposedToUpperLayer
            && !report.resourceStats.transientResourceCount
            && !report.resourceStats.pendingDestroyCount);
        std::puts("PASS_TIMING_NULL_SMOKE hidden-frames2 validator-only no-native-timing PASS");
    }
}
