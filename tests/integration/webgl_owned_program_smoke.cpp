#include "rhi/backends/webgl/webgl_program.h"

#include <emscripten.h>

using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi::semantics;

namespace {
EM_JS(void, failOriginalSourceOnce, (), {
    // clang-format off
    const gl=GL.currentContext.GLctx;
    const original=gl.shaderSource.bind(gl);
    gl.shaderSource=(shader,source)=>{gl.shaderSource=original;original(shader,source+'\n@');};
    // clang-format on
});

EM_JS(void, observeProgramCalls, (), {
    // clang-format off
    const gl=GL.currentContext.GLctx;
        const state=Module.ownedProgramEvidence={created:0,deleted:0,compiled:0,linked:0,live:new Set(),hostile:false,
            version:gl.getParameter(gl.VERSION)};
    for(const [name,key] of [['createProgram','created'],['deleteProgram','deleted'],['compileShader','compiled'],['linkProgram','linked']]) {
        const original=gl[name].bind(gl);
        gl[name]=(...args)=>{const result=original(...args);++state[key];
            if(name==='createProgram' && result)state.live.add(result);
            if(name==='deleteProgram')state.live.delete(args[0]);return result;};
    }
    const active=gl.getActiveUniform.bind(gl);
    gl.getActiveUniform=(...args)=>{const value=active(...args);
        if(state.hostile && value)return {name:value.name,size:value.size,type:gl.BOOL};return value;};
    // clang-format on
});

EM_JS(void, hostileInterface, (int enabled), {
    // clang-format off
    Module.ownedProgramEvidence.hostile=!!enabled;
    // clang-format on
});

EM_JS(int, livePrograms, (), {
    // clang-format off
    return Module.ownedProgramEvidence.live.size;
    // clang-format on
});

EM_JS(void, report, (int success, const char* diagnostic), {
    // clang-format off
    const e=Module.ownedProgramEvidence;
    fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'WebGlOwnedProgramSmoke',contractVersion:1,success:!!success,diagnostic:UTF8ToString(diagnostic),
            native:e?{created:e.created,deleted:e.deleted,compiled:e.compiled,linked:e.linked,live:e.live.size}:null,
            webGlVersion:e?e.version:null,
        webGpuUnavailable:navigator.gpu===undefined,subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        productionBackendValidated:false,visualAcceptanceClaimed:false,gpuCompletionClaimed:false})});
    // clang-format on
});

class ProgramSmoke final {
public:
    bool check(bool pass, const char* id)
    {
        if (!pass)
            failure = id;
        return pass;
    }

    bool run();
    const char* failure = "";
};

#define REQUIRE(e, id)                                                                             \
    do {                                                                                           \
        if (!check(bool(e), id))                                                                   \
            return false;                                                                          \
    } while (false)

bool ProgramSmoke::run()
{
    shader::WebGlShaderLoadBudget load{
        {4 * 1024 * 1024, 32 * 1024 * 1024, 32 * 1024 * 1024, 32}, 256, 32, 1024};
    auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(load.original.maxCpuMetadataBytes);
    core::pmr::String message{&core::defaultCpuMemoryResource()};
    auto artifact = shader::WebGlShaderProgramArtifact::load(
        "/artifacts/webgl1_graphics.webgl-program.yaml", load, artifactQuota, message);
    if (!artifact) {
        std::fprintf(stderr, "artifact: %s\n", message.c_str());
    }
    REQUIRE(artifact, "PROGRAM-ORIGINAL-LOAD");
    WebGlContextOwnershipRequest context;
    context.contractVersion = context.context.contractVersion = 1;
    context.canvasSelector = "#canvas";
    context.colorSpace = WebGlCanvasColorSpace::SRgb;
    context.maxCpuMetadataBytes = 256 * 1024;
    context.maxIdentityFieldBytes = 4096;
    auto contextQuota = core::makeShared<core::CpuMemoryQuota>(context.maxCpuMetadataBytes);
    const char* diagnostic = nullptr;
    auto runtime = webgl::WebGlRuntime::create(context, contextQuota, diagnostic);
    REQUIRE(runtime, diagnostic ? diagnostic : "PROGRAM-CONTEXT");
    WebGlResourceOwnershipRequest ownership;
    ownership.contractVersion = ownership.budget.contractVersion = 1;
    ownership.budget.capacity.cpuMetadataBytes = 2 * 1024 * 1024;
    ownership.budget.capacity.cpuUploadBytes = 1024;
    ownership.budget.capacity.objectCount = 4;
    auto metadata =
        core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuMetadataBytes);
    auto uploads = core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuUploadBytes);
    auto resources =
        webgl::WebGlResources::create(std::move(runtime), ownership, metadata, uploads, diagnostic);
    REQUIRE(resources, diagnostic ? diagnostic : "PROGRAM-RESOURCE-DOMAIN");
    observeProgramCalls();
    WebGlProgramCreationRequest request{1, 1024, 4096};
    REQUIRE(!resources->createProgram({}, request) && !resources->counters().objectCount
            && !livePrograms(),
        "PROGRAM-MISSING-ORIGINAL-REJECTED");
    auto narrow = request;
    narrow.maxInterfaceNameBytes = 1;
    REQUIRE(!resources->createProgram(artifact, narrow) && !resources->counters().objectCount
            && !livePrograms(),
        "PROGRAM-NAME-BUDGET-REJECTED");
    hostileInterface(1);
    REQUIRE(!resources->createProgram(artifact, request) && !resources->counters().objectCount
            && !livePrograms() && resources->diagnostic()
            && std::string_view(resources->diagnostic()) == "SHADER-WEBGL1-ACTIVE-UNIFORM",
        "PROGRAM-HOSTILE-DRIVER-INTERFACE-REJECTED");
    hostileInterface(0);
    failOriginalSourceOnce();
    REQUIRE(!resources->createProgram(artifact, request) && !resources->counters().objectCount
            && !livePrograms()
            && std::string_view(resources->diagnostic()) == "SHADER-WEBGL1-COMPILE"
            && resources->nativeDiagnostic().find("programIdentitySha256=")
                != std::string_view::npos
            && resources->nativeDiagnostic().find("vertexSourceSha256=") != std::string_view::npos
            && resources->nativeDiagnostic().find("fragmentSourceSha256=") != std::string_view::npos
            && resources->nativeDiagnostic().find("stage=vertex\nlog=") != std::string_view::npos
            && resources->nativeDiagnostic().size() <= request.maxDiagnosticBytes,
        "PROGRAM-REAL-COMPILE-FAILURE-IDENTITY-LOG");
    narrow = request;
    narrow.maxDiagnosticBytes = 1;
    failOriginalSourceOnce();
    REQUIRE(!resources->createProgram(artifact, narrow) && !livePrograms()
            && std::string_view(resources->diagnostic()) == "SHADER-WEBGL1-DIAGNOSTIC-BUDGET"
            && resources->nativeDiagnostic().empty(),
        "PROGRAM-FAILURE-DIAGNOSTIC-BUDGET");
    auto program = resources->createProgram(artifact, request);
    REQUIRE(program && !program->currentDiagnostic() && resources->counters().objectCount == 1
            && livePrograms() == 1,
        resources->diagnostic() ? resources->diagnostic() : "PROGRAM-NATIVE-LINKED");
    const auto old = program->id();
    REQUIRE(program->artifact() == artifact, "PROGRAM-RETAINS-EXACT-PUBLICATION");
    artifact.reset();
    REQUIRE(artifactQuota->liveBytes(), "PROGRAM-ARTIFACT-QUOTA-RETAINED");
    program.reset();
    REQUIRE(!resources->counters().objectCount && !livePrograms() && !artifactQuota->liveBytes(),
        "PROGRAM-LAST-OWNER-RETIREMENT");
    artifact = shader::WebGlShaderProgramArtifact::load(
        "/artifacts/webgl1_graphics.webgl-program.yaml", load, artifactQuota, message);
    program = resources->createProgram(artifact, request);
    REQUIRE(program && program->id() != old, "PROGRAM-NO-ID-REUSE");
    resources.reset();
    REQUIRE(program->currentDiagnostic()
            && std::string_view(program->currentDiagnostic())
                == "RHI-WEBGL1-RESOURCE-DOMAIN-RETIRED",
        "PROGRAM-RETIRED-DOMAIN-FAILS-CLOSED");
    program.reset();
    artifact.reset();
    REQUIRE(!metadata->liveBytes() && !uploads->liveBytes() && !contextQuota->liveBytes()
            && !artifactQuota->liveBytes() && !livePrograms(),
        "PROGRAM-ALL-ORIGINAL-OWNERS-RETIRED");
    return true;
}
} // namespace

int main()
{
    ProgramSmoke test;
    const auto success = test.run();
    report(success, test.failure);
    return success ? 0 : 1;
}
