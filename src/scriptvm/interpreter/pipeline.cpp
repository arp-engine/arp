#include "scriptvm/interpreter/pipeline.h"

#include "core/sha256.h"

namespace arp::scriptvm {

namespace {

core::pmr::String sha256(const core::pmr::String& text)
{
    return core::Sha256::hexDigest(core::StringView(text.data(), text.size()));
}

bool validSha256(const core::pmr::String& value)
{
    if (value.size() != 71u || value.rfind("sha256:", 0u) != 0u)
        return false;
    for (std::size_t index = 7u; index < value.size(); ++index) {
        const char c = value[index];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

ScriptDiagnostic invalid(const char* message)
{
    return ScriptDiagnostic{ScriptDiagnosticCode::TypedIrInvalid,
        core::pmr::String(message, &core::defaultCpuMemoryResource())};
}

} // namespace

ScriptInterpreterBuildResult ScriptInterpreterPipeline::build(
    const ScriptInterpreterBuildRequest& request)
{
    ScriptInterpreterBuildResult result;
    if (request.moduleVersion == 0u || !validSha256(request.dependencyGraphHash)
        || !validSha256(request.reflectionRegistryHash)) {
        result.error =
            invalid("interpreter build requires versioned dependency and reflection hashes");
        return result;
    }
    auto frontend = ScriptFrontend::compile(
        request.source, request.filename, request.moduleId, request.moduleRegistry);
    if (!frontend.ok()) {
        result.error = std::move(frontend.error);
        return result;
    }
    ScriptInterpreterPreparedBuildRequest prepared;
    prepared.source = request.source;
    prepared.module = std::move(frontend.module);
    prepared.moduleVersion = request.moduleVersion;
    prepared.dependencyGraphHash = request.dependencyGraphHash;
    prepared.reflectionRegistryHash = request.reflectionRegistryHash;
    prepared.entryFunctionId = request.entryFunctionId;
    return buildPrepared(prepared);
}

ScriptInterpreterBuildResult ScriptInterpreterPipeline::buildPrepared(
    const ScriptInterpreterPreparedBuildRequest& request)
{
    ScriptInterpreterBuildResult result;
    if (request.moduleVersion == 0u || !validSha256(request.dependencyGraphHash)
        || !validSha256(request.reflectionRegistryHash) || !request.module.valid()) {
        result.error =
            invalid("prepared interpreter build requires a typed module and versioned hashes");
        return result;
    }
    auto lowered = ScriptLowerer::lower(request.module);
    if (!lowered.ok()) {
        result.error = std::move(lowered.error);
        return result;
    }
    lowered.module.moduleVersion = request.moduleVersion;
    lowered.module.fingerprint = computeTypedIrFingerprint(lowered.module);
    const auto validation = validateTypedIr(lowered.module);
    if (!validation.ok()) {
        result.error = validation.diagnostic();
        return result;
    }

    InterpreterBytecodeOptions options;
    options.artifactIdentity.moduleId = lowered.module.moduleId;
    options.artifactIdentity.moduleVersion = lowered.module.moduleVersion;
    options.artifactIdentity.semanticAbiVersion = lowered.module.semanticAbiVersion;
    options.artifactIdentity.typedIrVersion = lowered.module.typedIrVersion;
    options.artifactIdentity.sourceHash = sha256(request.source);
    options.artifactIdentity.dependencyGraphHash = request.dependencyGraphHash;
    options.artifactIdentity.reflectionRegistryHash = request.reflectionRegistryHash;
    options.artifactIdentity.typedIrFingerprint = lowered.module.fingerprint;
    options.entryFunctionId = request.entryFunctionId;
    auto bytecode = InterpreterBytecodeLowerer::lower(lowered.module, options);
    if (!bytecode.ok()) {
        result.error = std::move(bytecode.error);
        return result;
    }
    result.typedIr = std::move(lowered.module);
    result.program = std::move(bytecode.program);
    result.receipt = std::move(bytecode.receipt);
    return result;
}

} // namespace arp::scriptvm
