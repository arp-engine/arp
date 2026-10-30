#include "scriptvm/plugin/aot_dynamic_package.h"

#include "core/hash/sha256.h"
#include "core/system/process_identity_lock.h"
#include "core/system/process_runner.h"
#include "core/text/number_format.h"
#include "core/text/string_utils.h"
#include "scriptvm/aot/aot_manifest_runtime.h"
#include "scriptvm/plugin/compiler_toolchain_layout.h"
#include "scriptvm/plugin/immutable_build_input_snapshot.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#endif

namespace arp::scriptvm {
namespace {

#if defined(_WIN32) || defined(__CYGWIN__)
bool loadedModulePathMatches(HMODULE module, const std::filesystem::path& expected)
{
    core::pmr::Vector<wchar_t> buffer(512u, L'\0', &core::defaultCpuMemoryResource());
    for (;;) {
        const auto length =
            GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0u)
            return false;
        if (length < buffer.size() - 1u) {
            std::error_code actualError;
            std::error_code expectedError;
            auto actual =
                std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()), actualError)
                    .generic_wstring();
            auto wanted =
                std::filesystem::weakly_canonical(expected, expectedError).generic_wstring();
            if (actualError || expectedError)
                return false;
            const auto lower = [](wchar_t value) {
                return static_cast<wchar_t>(std::towlower(value));
            };
            std::transform(actual.begin(), actual.end(), actual.begin(), lower);
            std::transform(wanted.begin(), wanted.end(), wanted.begin(), lower);
            return actual == wanted;
        }
        if (buffer.size() >= 32768u)
            return false;
        buffer.resize(buffer.size() * 2u);
    }
}
#endif

ScriptResult failure(const char* message)
{
    return ScriptResult::failure(ScriptDiagnosticCode::AotArtifactInvalid,
        core::pmr::String(message, &core::defaultCpuMemoryResource()));
}

ScriptResult consistencyFailure(const char* message, const core::pmr::String& diagnostics)
{
    core::pmr::String text(message, &core::defaultCpuMemoryResource());
    if (!diagnostics.empty()) {
        text += ": ";
        text += diagnostics;
    }
    return failure(text.c_str());
}

ScriptResult processFailure(const char* operation,
    const std::filesystem::path& logPath,
    const core::ProcessRunResult& process,
    const PluginHostTextBudget& budget)
{
    core::pmr::String message(operation, &core::defaultCpuMemoryResource());
    message += " (exit=";
    core::appendUnsigned(message, process.exitCode);
    message += ")";
    if (!process.diagnostic.empty()) {
        message += ": ";
        message += process.diagnostic;
    }
    const auto log = PluginHostTextReader(budget).diagnostic(logPath);
    if (log.success && !log.text.empty()) {
        // Keep diagnostics bounded while retaining the compiler's first
        // error after the staging directory is reclaimed.
        message += "\n";
        message += log.text;
        if (log.truncated)
            message += "\n[process log truncated]";
    } else if (!log.success) {
        message += "\n[process log unavailable: ";
        message += log.diagnosticId;
        message += "]";
    }
    return ScriptResult::failure(ScriptDiagnosticCode::AotArtifactInvalid, std::move(message));
}

void addArgument(core::ProcessRunRequest& request, std::string_view value)
{
    request.arguments.emplace_back(value.data(), value.size());
}

#if defined(_WIN32) || defined(__CYGWIN__)
core::pmr::String fileContentFingerprint(const std::filesystem::path& path)
{
    core::ByteBuffer digest{&core::defaultCpuMemoryResource()};
    if (!core::Sha256::digestFile(path, digest))
        return {};
    static constexpr char digits[] = "0123456789abcdef";
    core::pmr::String result("sha256:", &core::defaultCpuMemoryResource());
    for (const auto byte : digest) {
        result.push_back(digits[(byte >> 4u) & 0x0fu]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

core::pmr::String completionMarkerText(
    std::string_view identity, const std::filesystem::path& artifact)
{
    core::pmr::String text("type: arp.script.aot-cache-completion\n"
                           "contractVersion: 1\nidentity: ",
        &core::defaultCpuMemoryResource());
    text.append(identity.data(), identity.size());
    text += "\nartifactSha256: ";
    text += fileContentFingerprint(artifact);
    text += "\n";
    return text;
}

bool publishCompletionMarker(const std::filesystem::path& marker,
    std::string_view identity,
    const std::filesystem::path& artifact)
{
    core::pmr::String temporary{marker.string().c_str(), &core::defaultCpuMemoryResource()};
    temporary += ".tmp-";
    core::appendUnsigned(temporary, GetCurrentProcessId());
    temporary.push_back('-');
    core::appendUnsigned(temporary, GetCurrentThreadId());
    temporary.push_back('-');
    core::appendUnsigned(temporary, GetTickCount64());
    const auto markerPath = marker.string();
    const auto content = completionMarkerText(identity, artifact);
    if (content.empty())
        return false;
    HANDLE file = CreateFileA(temporary.c_str(),
        GENERIC_WRITE,
        0u,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0u;
    const BOOL wrote =
        WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!wrote || written != content.size()) {
        DeleteFileA(temporary.c_str());
        return false;
    }
    if (!MoveFileExA(temporary.c_str(),
            markerPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temporary.c_str());
        return false;
    }
    return true;
}

bool completionMarkerMatches(const std::filesystem::path& marker,
    const PluginHostTextBudget& budget,
    std::string_view identity,
    const std::filesystem::path& artifact)
{
    const auto read = PluginHostTextReader(budget).read(marker, PluginHostTextKind::Metadata);
    if (!read.success)
        return false;
    const auto expected = completionMarkerText(identity, artifact);
    return std::string_view(read.text.data(), read.text.size()) == expected;
}

#endif

bool removeDirectoryTree(const std::filesystem::path& path, std::error_code& error)
{
    error.clear();
    if (path.empty())
        return true;
    // Windows file scanners and compiler helpers can briefly retain a handle
    // after the child process has exited. Retry only while the tree still
    // exists; a bounded failure remains a hard diagnostic below.
    // Antivirus/indexer handles can outlive the child process by more than a
    // single scheduler tick. Cleanup is off the execution hot path, so allow
    // a bounded deadline before reporting a deterministic hard failure.
    constexpr auto kStagingCleanupRetryWindow = std::chrono::seconds(10);
    constexpr auto kStagingCleanupRetryDelay = std::chrono::milliseconds(100);
    const auto deadline = std::chrono::steady_clock::now() + kStagingCleanupRetryWindow;
    do {
        error.clear();
        std::filesystem::remove_all(path, error);
        std::error_code existsError;
        const bool stillExists = std::filesystem::exists(path, existsError);
        if (!error || (!stillExists && !existsError)) {
            error.clear();
            return true;
        }
        std::this_thread::sleep_for(kStagingCleanupRetryDelay);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

class StagingDirectoryScope final {
public:
    explicit StagingDirectoryScope(std::filesystem::path path) : path_(std::move(path)) {}

    void rebind(std::filesystem::path path) noexcept { path_ = std::move(path); }

    ~StagingDirectoryScope()
    {
        std::error_code ignored;
        removeDirectoryTree(path_, ignored);
    }

private:
    std::filesystem::path path_;
};

core::pmr::String lowerAscii(std::string_view value)
{
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    result.reserve(value.size());
    for (const unsigned char ch : value)
        result.push_back(static_cast<char>(std::tolower(ch)));
    return result;
}

ScriptResult auditArtifact(
    const WindowsDynamicScriptAotPackageDesc& desc, const std::filesystem::path& artifact)
{
    auto symbolsPath = artifact;
    symbolsPath += ".symbols.log";
    core::ProcessRunRequest symbolRequest;
    symbolRequest.executable = desc.symbolInspector;
    symbolRequest.outputFile = symbolsPath;
    addArgument(symbolRequest, "-C");
    addArgument(symbolRequest, artifact.string());
    if (!core::ProcessRunner().run(symbolRequest).ok())
        return failure("dynamic AOT package symbol audit failed");
    const auto symbols =
        PluginHostTextReader(desc.hostTextBudget).read(symbolsPath, PluginHostTextKind::Audit);
    if (!symbols.success)
        return failure(symbols.diagnosticId);
    constexpr std::string_view forbiddenSymbols[] = {
        "arp::scriptvm::Lexer",
        "arp::scriptvm::Parser",
        "arp::scriptvm::Compiler",
        "arp::scriptvm::Vm::",
        "executeAot"
        "Function",
        "executeAotNativeValueFrameMachine",
        "IrExecutorPrototypeLegacy",
        "LegacyAotFunctionExecutionKind",
    };
    for (const auto pattern : forbiddenSymbols) {
        if (symbols.text.find(pattern) != core::pmr::String::npos)
            return failure("dynamic AOT package contains a forbidden runtime symbol");
    }
    constexpr std::string_view registrationEntry = " T arp_register_script_aot_package_v5";
    std::string_view symbolLines(symbols.text.data(), symbols.text.size());
    std::size_t registrationCount = 0u;
    while (!symbolLines.empty()) {
        const auto end = symbolLines.find_first_of("\r\n");
        const auto line = symbolLines.substr(0u, end);
        if (line.size() >= registrationEntry.size()
            && line.substr(line.size() - registrationEntry.size()) == registrationEntry) {
            ++registrationCount;
        }
        if (end == std::string_view::npos)
            break;
        symbolLines.remove_prefix(end + 1u);
    }
    if (registrationCount != 1u) {
        return failure("dynamic AOT package must export exactly one v5 registration entry");
    }

    auto importsPath = artifact;
    importsPath += ".imports.log";
    core::ProcessRunRequest importRequest;
    importRequest.executable = desc.objectInspector;
    importRequest.outputFile = importsPath;
    addArgument(importRequest, "-p");
    addArgument(importRequest, artifact.string());
    if (!core::ProcessRunner().run(importRequest).ok())
        return failure("dynamic AOT package import audit failed");
    const auto imports =
        PluginHostTextReader(desc.hostTextBudget).read(importsPath, PluginHostTextKind::Audit);
    if (!imports.success)
        return failure(imports.diagnosticId);
    std::string_view remaining(imports.text.data(), imports.text.size());
    constexpr std::string_view prefix = "DLL Name: ";
    constexpr std::string_view allowedImports[] = {
        "libgcc_s_seh-1.dll",
        "libstdc++-6.dll",
        "libwinpthread-1.dll",
        "kernel32.dll",
        "msvcrt.dll",
    };
    const auto allowedHost = lowerAscii(desc.hostLibrary.filename().string());
    std::size_t importCount = 0u;
    while (true) {
        const auto position = remaining.find(prefix);
        if (position == std::string_view::npos)
            break;
        remaining.remove_prefix(position + prefix.size());
        const auto end = remaining.find_first_of("\r\n");
        const auto name = lowerAscii(remaining.substr(0u, end));
        const auto allowed = name == allowedHost || name.rfind("api-ms-win-crt-", 0u) == 0u
            || std::any_of(std::begin(allowedImports),
                std::end(allowedImports),
                [&name](std::string_view candidate) {
                    return name == candidate;
                });
        if (!allowed)
            return failure("dynamic AOT package imports a non-AotOnly DLL");
        ++importCount;
        if (end == std::string_view::npos)
            break;
        remaining.remove_prefix(end + 1u);
    }
    if (importCount == 0u)
        return failure("dynamic AOT package import audit found no PE dependencies");
    return ScriptResult::success();
}

core::pmr::String generatedPackageIdentity(const std::filesystem::path& manifest,
    const PluginHostTextBudget& budget,
    const char*& diagnostic)
{
    const auto read = PluginHostTextReader(budget).read(manifest, PluginHostTextKind::Metadata);
    if (!read.success) {
        diagnostic = read.diagnosticId;
        return {};
    }
    AotPackageManifestV5 metadata;
    if (!parseAotPackageManifestV5(read.text, metadata, AotPlatform::Windows).ok()) {
        diagnostic = "ARP-AOT-GENERATED-PACKAGE-IDENTITY";
        return {};
    }
    diagnostic = nullptr;
    return std::move(metadata.packageIdentity);
}

core::pmr::String fileFingerprint(const std::filesystem::path& path)
{
    core::ByteBuffer digest{&core::defaultCpuMemoryResource()};
    if (!core::Sha256::digestFile(path, digest))
        return {};
    core::pmr::String fingerprint("sha256:", &core::defaultCpuMemoryResource());
    static constexpr char digits[] = "0123456789abcdef";
    for (const auto byte : digest) {
        fingerprint.push_back(digits[(byte >> 4u) & 0x0fu]);
        fingerprint.push_back(digits[byte & 0x0fu]);
    }
    return fingerprint;
}

bool addFileContract(core::ImmutableBuildInputContract& contract,
    std::string_view logicalName,
    const std::filesystem::path& path,
    core::pmr::String& diagnostics)
{
    const auto fingerprint = fileFingerprint(path);
    if (fingerprint.empty())
        return false;
    // Logical names, not physical paths, define relocation-stable identity.
    return contract.add(logicalName, fingerprint, diagnostics);
}

std::filesystem::path hostImportLibraryPath(const std::filesystem::path& hostLibrary)
{
    auto name = hostLibrary.stem().string();
    return hostLibrary.parent_path() / ("lib" + name + ".dll.a");
}

core::pmr::String dynamicArtifactIdentity(std::string_view packageIdentity,
    const WindowsDynamicScriptAotPackageDesc& desc,
    const std::filesystem::path& hostLinkInput = {})
{
    core::ImmutableBuildInputContract contract;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    if (!contract.add("contract", "dynamic-package-abi-v5", diagnostics)
        || !contract.add("package-identity", packageIdentity, diagnostics)) {
        return {};
    }
    // packageIdentity intentionally captures semantic source/contracts but
    // does not include every build control (notably execution budgets). The
    // manifest itself is therefore part of the content identity; changing a
    // budget or any other lowering input must never reuse an old DLL.
    if (!addFileContract(contract, "manifest", desc.manifest, diagnostics)
        || !addFileContract(contract, "host-library", desc.hostLibrary, diagnostics)
        || !addFileContract(contract, "cxx-compiler", desc.cxxCompiler, diagnostics)
        || !addFileContract(contract, "aot-compiler", desc.aotCompiler, diagnostics)
        || !addFileContract(contract, "symbol-inspector", desc.symbolInspector, diagnostics)
        || !addFileContract(contract, "object-inspector", desc.objectInspector, diagnostics)) {
        return {};
    }
    const auto linkInput =
        hostLinkInput.empty() ? hostImportLibraryPath(desc.hostLibrary) : hostLinkInput;
    if (!addFileContract(contract, "host-link-input", linkInput, diagnostics)) {
        return {};
    }
    core::pmr::Vector<std::filesystem::path> includeFiles{&core::defaultCpuMemoryResource()};
    std::error_code iteratorError;
    for (std::filesystem::recursive_directory_iterator iterator(desc.includeRoot, iteratorError),
        end;
        !iteratorError && iterator != end;
        iterator.increment(iteratorError)) {
        // The include contract is intentionally content-complete.  Headers
        // can include .inl/.inc/.def or generated fragments, so filtering by
        // a guessed extension would permit stale DLL reuse after a real
        // compile input changed.
        if (iterator->is_regular_file())
            includeFiles.push_back(iterator->path());
    }
    if (iteratorError)
        return {};
    std::sort(includeFiles.begin(), includeFiles.end());
    for (const auto& path : includeFiles) {
        const auto relative = path.lexically_relative(desc.includeRoot);
        if (relative.empty())
            return {};
        core::pmr::String logicalName("include/", &core::defaultCpuMemoryResource());
        logicalName += relative.generic_string();
        if (!addFileContract(contract, logicalName, path, diagnostics))
            return {};
    }
    auto digest = contract.fingerprint();
    if (digest.rfind("sha256:", 0u) == 0u)
        digest.erase(0u, 7u);
    return digest;
}

core::pmr::String snapshotArtifactIdentity(
    std::string_view inputIdentity, std::string_view snapshotFingerprint)
{
    core::ImmutableBuildInputContract contract;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    if (!contract.add("input-identity", inputIdentity, diagnostics)
        || !contract.add("snapshot", snapshotFingerprint, diagnostics)) {
        return {};
    }
    auto digest = contract.fingerprint();
    if (digest.rfind("sha256:", 0u) == 0u)
        digest.erase(0u, 7u);
    return digest;
}

struct DynamicPackageLifetime final {
#if defined(_WIN32) || defined(__CYGWIN__)
    HMODULE host = nullptr;
    HMODULE package = nullptr;
#endif
    std::filesystem::path artifact;

    ~DynamicPackageLifetime()
    {
#if defined(_WIN32) || defined(__CYGWIN__)
        if (package != nullptr)
            FreeLibrary(package);
        if (host != nullptr)
            FreeLibrary(host);
#endif
    }
};

} // namespace

WindowsDynamicScriptAotPackageResult WindowsDynamicScriptAotPackageLoader::load(
    const WindowsDynamicScriptAotPackageDesc& desc, ScriptAotPackageRegistry& registry) const
{
    WindowsDynamicScriptAotPackageResult result;
    if (!PluginHostTextReader::validBudget(desc.hostTextBudget)) {
        result.status = failure("ARP-PLUGIN-HOST-TEXT-CONFIG");
        return result;
    }
#if !defined(_WIN32) && !defined(__CYGWIN__)
    (void)desc;
    (void)registry;
    result.status = failure("dynamic AOT package loading is available on Windows only");
    return result;
#else
    static thread_local std::uint32_t consistencyAttempt = 0u;
    constexpr std::uint32_t kMaxConsistencyAttempts = 3u;
    const auto hostLinkInput = hostImportLibraryPath(desc.hostLibrary);
    if (desc.manifest.empty() || desc.cacheRoot.empty() || desc.aotCompiler.empty()
        || desc.cxxCompiler.empty() || desc.symbolInspector.empty() || desc.objectInspector.empty()
        || desc.includeRoot.empty() || desc.hostLibrary.empty()
        || !std::filesystem::is_regular_file(desc.manifest)
        || !std::filesystem::is_regular_file(desc.aotCompiler)
        || !std::filesystem::is_regular_file(desc.cxxCompiler)
        || !std::filesystem::is_regular_file(desc.symbolInspector)
        || !std::filesystem::is_regular_file(desc.objectInspector)
        || !std::filesystem::is_regular_file(desc.hostLibrary)
        || !std::filesystem::is_regular_file(hostLinkInput)) {
        result.status = failure("dynamic AOT package paths are incomplete");
        return result;
    }

    const auto stagingRoot = desc.cacheRoot / "staging";
    std::error_code directoryError;
    std::filesystem::create_directories(stagingRoot, directoryError);
    if (directoryError) {
        result.status = failure("failed to create dynamic AOT package cache");
        return result;
    }
    const auto compilerLayout = core::CompilerToolchainLayout().inspect(desc.cxxCompiler);
    if (!compilerLayout.success) {
        result.status = ScriptResult::failure(
            ScriptDiagnosticCode::AotArtifactInvalid, compilerLayout.diagnostics);
        return result;
    }
    const auto packageDirectory = desc.cacheRoot / "packages";
    std::filesystem::create_directories(packageDirectory, directoryError);
    if (directoryError) {
        result.status = failure("failed to create dynamic AOT artifact cache");
        return result;
    }
    const auto preliminaryDigest = dynamicArtifactIdentity({}, desc);
    if (preliminaryDigest.empty()) {
        result.status = failure("dynamic AOT package content contract is unavailable");
        return result;
    }
    const auto lockPath =
        packageDirectory / (".arp-script-aot-v5-input-" + preliminaryDigest + ".lock");
    auto artifactLock = core::makeUnique<core::ProcessIdentityLock>(lockPath, preliminaryDigest);
    if (!artifactLock->locked()) {
        result.status = failure("dynamic AOT package cache lock failed");
        return result;
    }
    const auto expectedHostHash = fileFingerprint(desc.hostLibrary);
    if (expectedHostHash.empty()) {
        result.status = failure("dynamic AOT host identity is unavailable");
        return result;
    }
    // The full SHA-256 is already carried by the lock, marker, and published
    // artifact. Keep transient names to process/thread/attempt identity so a
    // caller's cache root does not push deep include snapshots over MAX_PATH.
    core::pmr::String stagingName{&core::defaultCpuMemoryResource()};
    core::appendUnsigned(stagingName, GetCurrentProcessId());
    stagingName.push_back('-');
    core::appendUnsigned(stagingName, GetCurrentThreadId());
    stagingName.push_back('-');
    core::appendUnsigned(stagingName, GetTickCount64());
    auto stagingDirectory = stagingRoot / std::filesystem::path(stagingName.c_str());
    StagingDirectoryScope stagingScope(stagingDirectory);
    if (!removeDirectoryTree(stagingDirectory, directoryError)) {
        result.status = failure("failed to clear identity-scoped dynamic AOT staging directory");
        return result;
    }
    directoryError.clear();
    std::filesystem::create_directories(stagingDirectory, directoryError);
    if (directoryError) {
        result.status = failure("failed to create identity-scoped dynamic AOT staging directory");
        return result;
    }
    auto generatedDirectory = stagingDirectory / "generated";

    const auto snapshotRoot = stagingDirectory / "i";
    core::ImmutableBuildInputSnapshot snapshot(snapshotRoot);
    core::pmr::String snapshotDiagnostics{&core::defaultCpuMemoryResource()};
    const auto manifestRoot = desc.manifest.parent_path();
    const auto manifestRelative = desc.manifest.filename().generic_string();
    const auto manifestLogicalName =
        core::concatPmr({"manifest-root/", manifestRelative}, &core::defaultCpuMemoryResource());
    if (!snapshot.captureDirectory("manifest-root", manifestRoot, snapshotDiagnostics)
        || !snapshot.captureDirectory("include-root", desc.includeRoot, snapshotDiagnostics)
        || !snapshot.captureFile("host/link-input", hostLinkInput, snapshotDiagnostics)
        || !snapshot.captureExecutable("tools/aot-compiler", desc.aotCompiler, snapshotDiagnostics)
        || !snapshot.captureExecutable(
            "tools/symbol-inspector", desc.symbolInspector, snapshotDiagnostics)
        || !snapshot.captureExecutable(
            "tools/object-inspector", desc.objectInspector, snapshotDiagnostics)) {
        result.status = ScriptResult::failure(
            ScriptDiagnosticCode::AotArtifactInvalid, std::move(snapshotDiagnostics));
        return result;
    }
    const auto compilerSnapshot =
        core::CompilerToolchainLayout().captureSnapshot(snapshot, "tc", desc.cxxCompiler);
    if (!compilerSnapshot.success) {
        result.status = ScriptResult::failure(
            ScriptDiagnosticCode::AotArtifactInvalid, compilerSnapshot.diagnostics);
        return result;
    }

    std::filesystem::remove_all(generatedDirectory, directoryError);
    directoryError.clear();
    std::filesystem::create_directories(generatedDirectory, directoryError);
    if (directoryError) {
        result.status = failure("failed to reset snapshot-generated AOT directory");
        return result;
    }
    const auto snapshotManifest = snapshot.path(manifestLogicalName);
    const auto snapshotGenerationLog = stagingDirectory / "aotc-snapshot.log";
    core::ProcessRunRequest snapshotPackageRequest;
    snapshotPackageRequest.executable = snapshot.path("tools/aot-compiler");
    snapshotPackageRequest.outputFile = snapshotGenerationLog;
    addArgument(snapshotPackageRequest, "package");
    addArgument(snapshotPackageRequest, snapshotManifest.string());
    addArgument(snapshotPackageRequest, "--output");
    addArgument(snapshotPackageRequest, generatedDirectory.string());
    const auto snapshotPackageProcess = core::ProcessRunner().run(snapshotPackageRequest);
    if (!snapshotPackageProcess.ok()) {
        result.status = processFailure("snapshot AOT package generation failed",
            snapshotGenerationLog,
            snapshotPackageProcess,
            desc.hostTextBudget);
        return result;
    }
    const char* identityDiagnostic = nullptr;
    const auto snapshotPackageIdentity = generatedPackageIdentity(
        generatedDirectory / "package.aot-v5.yaml", desc.hostTextBudget, identityDiagnostic);
    if (snapshotPackageIdentity.empty()) {
        result.status = failure(identityDiagnostic);
        return result;
    }
    WindowsDynamicScriptAotPackageDesc snapshotDesc = desc;
    snapshotDesc.manifest = snapshotManifest;
    snapshotDesc.includeRoot = snapshot.path("include-root");
    snapshotDesc.aotCompiler = snapshot.path("tools/aot-compiler");
    snapshotDesc.cxxCompiler = compilerSnapshot.compilerExecutable;
    snapshotDesc.symbolInspector = snapshot.path("tools/symbol-inspector");
    snapshotDesc.objectInspector = snapshot.path("tools/object-inspector");
    // Keep the original host module: loading a copied host would create a
    // second engine registry and allocator state. Its content is still part
    // of dynamicArtifactIdentity and verified before/after LoadLibrary.
    snapshotDesc.hostLibrary = desc.hostLibrary;
    const auto snapshotHostLinkInput = snapshot.path("host/link-input");
    const auto snapshotArtifactDigest =
        dynamicArtifactIdentity(snapshotPackageIdentity, snapshotDesc, snapshotHostLinkInput);
    const auto snapshotInputDigest =
        dynamicArtifactIdentity({}, snapshotDesc, snapshotHostLinkInput);
    if (snapshotPackageIdentity.empty() || snapshotArtifactDigest.empty()
        || snapshotInputDigest != preliminaryDigest
        || fileFingerprint(desc.hostLibrary) != expectedHostHash) {
        artifactLock.reset();
        if (++consistencyAttempt >= kMaxConsistencyAttempts) {
            --consistencyAttempt;
            result.status =
                failure("dynamic AOT inputs did not stabilize within the consistency policy");
            return result;
        }
        auto retry = WindowsDynamicScriptAotPackageLoader().load(desc, registry);
        --consistencyAttempt;
        return retry;
    }
    result.packageIdentity = snapshotPackageIdentity;
    const auto artifactDigest =
        snapshotArtifactIdentity(snapshotArtifactDigest, snapshot.fingerprint());
    // artifactDigest already commits to the package identity as well as the
    // compiler, host ABI, inspectors, and include contract. Do not duplicate
    // the package digest in the filename: the resulting marker path can
    // exceed the Win32 MAX_PATH limit under a normal temp directory.
    result.artifactPath = packageDirectory / ("arp-script-aot-v5-" + artifactDigest + ".dll");
    auto completionMarker = result.artifactPath;
    completionMarker += ".complete";
    result.cacheHit = std::filesystem::is_regular_file(result.artifactPath)
        && completionMarkerMatches(
            completionMarker, desc.hostTextBudget, artifactDigest, result.artifactPath);
    bool cacheArtifactAudited = false;
    if (result.cacheHit) {
        // A completion marker is only a publication hint.  Validate the
        // artifact before exposing a cache hit so a truncated DLL cannot
        // survive a crashed compile or an interrupted atomic publish.
        const auto cachedAudit = auditArtifact(snapshotDesc, result.artifactPath);
        if (cachedAudit.ok()) {
            cacheArtifactAudited = true;
        } else {
            std::error_code removeError;
            std::filesystem::remove(result.artifactPath, removeError);
            std::filesystem::remove(completionMarker, removeError);
            result.cacheHit = false;
        }
    }
    if (!result.cacheHit) {
        core::pmr::Vector<std::filesystem::path> generatedSources{
            &core::defaultCpuMemoryResource()};
        for (const auto& entry : std::filesystem::directory_iterator(generatedDirectory)) {
            const auto filename = entry.path().filename();
            if (entry.is_regular_file() && filename.extension() == ".cpp"
                && filename.stem().extension() == ".gen") {
                generatedSources.push_back(entry.path());
            }
        }
        std::sort(generatedSources.begin(), generatedSources.end());
        if (generatedSources.empty()) {
            result.status = failure("dynamic AOT package generated no C++ sources");
            return result;
        }
        const auto stagedArtifact = stagingDirectory / "package.dll";
        const auto compileLog = stagingDirectory / "compile.log";
        core::ProcessRunRequest compileRequest;
        compileRequest.executable = snapshotDesc.cxxCompiler;
        compileRequest.outputFile = compileLog;
        core::CompilerToolchainLayout::appendCompilerArguments(
            compilerSnapshot.layout, compileRequest);
        addArgument(compileRequest, "-std=c++17");
        addArgument(compileRequest, "-O3");
        addArgument(compileRequest, "-shared");
        addArgument(compileRequest, "-Wa,-mbig-obj");
        addArgument(compileRequest, "-I" + snapshotDesc.includeRoot.string());
        addArgument(compileRequest, "-I" + generatedDirectory.string());
        for (const auto& source : generatedSources) {
            addArgument(compileRequest, source.string());
        }
        addArgument(compileRequest, snapshotHostLinkInput.string());
        addArgument(compileRequest, "-o");
        addArgument(compileRequest, stagedArtifact.string());
        const auto compileProcess = core::ProcessRunner().run(compileRequest);
        if (!compileProcess.ok() || !std::filesystem::is_regular_file(stagedArtifact)) {
            result.status = processFailure("dynamic AOT package compilation failed",
                compileLog,
                compileProcess,
                desc.hostTextBudget);
            return result;
        }
        result.status = auditArtifact(snapshotDesc, stagedArtifact);
        if (!result.status.ok())
            return result;
        if (!snapshot.verifySourcesUnchanged(snapshotDiagnostics)
            || fileFingerprint(desc.hostLibrary) != expectedHostHash) {
            artifactLock.reset();
            if (++consistencyAttempt >= kMaxConsistencyAttempts) {
                --consistencyAttempt;
                result.status = consistencyFailure(
                    "dynamic AOT inputs changed during every consistency attempt",
                    snapshotDiagnostics);
                return result;
            }
            auto retry = WindowsDynamicScriptAotPackageLoader().load(desc, registry);
            --consistencyAttempt;
            return retry;
        }
        if (!MoveFileExA(stagedArtifact.string().c_str(),
                result.artifactPath.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            result.status = failure("dynamic AOT package atomic publish failed");
            return result;
        }
        if (!publishCompletionMarker(completionMarker, artifactDigest, result.artifactPath)) {
            result.status = failure("dynamic AOT package completion marker failed");
            return result;
        }
    }

    if (!cacheArtifactAudited) {
        result.status = auditArtifact(snapshotDesc, result.artifactPath);
        if (!result.status.ok())
            return result;
    }

    if (!snapshot.verifySourcesUnchanged(snapshotDiagnostics)
        || fileFingerprint(desc.hostLibrary) != expectedHostHash) {
        artifactLock.reset();
        if (++consistencyAttempt >= kMaxConsistencyAttempts) {
            --consistencyAttempt;
            result.status = consistencyFailure(
                "dynamic AOT inputs changed during every consistency attempt", snapshotDiagnostics);
            return result;
        }
        auto retry = WindowsDynamicScriptAotPackageLoader().load(desc, registry);
        --consistencyAttempt;
        return retry;
    }

    if (!completionMarkerMatches(
            completionMarker, desc.hostTextBudget, artifactDigest, result.artifactPath)
        || fileFingerprint(desc.hostLibrary) != expectedHostHash
        || dynamicArtifactIdentity(result.packageIdentity, desc) != snapshotArtifactDigest) {
        result.status = failure("dynamic AOT host or artifact identity changed before DLL load");
        return result;
    }

    auto owner = core::makeShared<DynamicPackageLifetime>();
    owner->artifact = result.artifactPath;
    owner->host = LoadLibraryA(desc.hostLibrary.string().c_str());
    owner->package = LoadLibraryA(result.artifactPath.string().c_str());
    if (owner->host == nullptr || owner->package == nullptr) {
        result.status = failure("dynamic AOT package DLL load failed");
        return result;
    }
    if (!loadedModulePathMatches(owner->host, desc.hostLibrary)
        || !loadedModulePathMatches(owner->package, result.artifactPath)
        || !completionMarkerMatches(
            completionMarker, desc.hostTextBudget, artifactDigest, result.artifactPath)
        || fileFingerprint(desc.hostLibrary) != expectedHostHash
        || dynamicArtifactIdentity(result.packageIdentity, desc) != snapshotArtifactDigest) {
        result.status = failure("dynamic AOT loaded DLL identity changed during module load");
        return result;
    }
    using Entry = int (*)(AotRegistrationHostV5*);
    const auto entry = reinterpret_cast<Entry>(
        GetProcAddress(owner->package, "arp_register_script_aot_package_v5"));
    if (entry == nullptr) {
        result.status = failure("dynamic AOT package registration entry is missing");
        return result;
    }
    const AotPackageDescriptorV5* package = nullptr;
    AotRegistrationHostV5 capture{kAotContractVersion,
        [](const AotPackageDescriptorV5* value, void* userData) {
            *static_cast<const AotPackageDescriptorV5**>(userData) = value;
            return value != nullptr;
        },
        &package};
    if (entry(&capture) != 1 || package == nullptr
        || result.packageIdentity != package->packageIdentity) {
        result.status = failure("dynamic AOT package registration identity mismatch");
        return result;
    }
    result.status = registry.registerPackage(*package, owner);
    return result;
#endif
}

} // namespace arp::scriptvm
