#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/system/process_identity_lock.h"
#include "core/system/process_runner.h"
#include "scriptvm/plugin/compiler_toolchain_layout.h"
#include "scriptvm/plugin/immutable_build_input_snapshot.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

// Contract tests must evaluate side-effectful expressions in Release builds.
#ifdef NDEBUG
#undef assert
#define assert(expression)                                                                         \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr,                                                                   \
                "Core process contract failed: %s (%s:%d)\n",                                      \
                #expression,                                                                       \
                __FILE__,                                                                          \
                __LINE__);                                                                         \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
#endif

#ifndef ARP_TEST_CXX_COMPILER
#define ARP_TEST_CXX_COMPILER ""
#endif

int main()
{
    using arp::core::ProcessRunner;
    using arp::core::ProcessRunRequest;

    arp::core::Sha256 incrementalHash;
    incrementalHash.update("a");
    incrementalHash.update("bc");
    const auto incrementalDigest = incrementalHash.finalize();
    const auto singleDigest = arp::core::Sha256::digest("abc");
    if (incrementalDigest != singleDigest || singleDigest.size() != 32u)
        std::abort();
    static constexpr std::uint8_t expectedDigest[32]{
        0xba,
        0x78,
        0x16,
        0xbf,
        0x8f,
        0x01,
        0xcf,
        0xea,
        0x41,
        0x41,
        0x40,
        0xde,
        0x5d,
        0xae,
        0x22,
        0x23,
        0xb0,
        0x03,
        0x61,
        0xa3,
        0x96,
        0x17,
        0x7a,
        0x9c,
        0xb4,
        0x10,
        0xff,
        0x61,
        0xf2,
        0x00,
        0x15,
        0xad,
    };
    if (!std::equal(singleDigest.begin(), singleDigest.end(), expectedDigest))
        std::abort();

    const auto output = std::filesystem::temp_directory_path() / "arp_process_runner_contract.txt";
    std::error_code cleanupError;
    std::filesystem::remove(output, cleanupError);

    ProcessRunRequest request;
#if defined(_WIN32) || defined(__CYGWIN__)
    request.executable = "cmd.exe";
    request.arguments.push_back(
        arp::core::pmr::String("/C", &arp::core::defaultCpuMemoryResource()));
    request.arguments.push_back(
        arp::core::pmr::String("echo", &arp::core::defaultCpuMemoryResource()));
    request.arguments.push_back(arp::core::pmr::String(
        "process runner & argument", &arp::core::defaultCpuMemoryResource()));
#else
    // macOS ships printf in /usr/bin while common Linux layouts also expose
    // it through /bin. Use the POSIX location present on both without relying
    // on shell PATH resolution; this test exercises an explicit executable.
    request.executable = "/usr/bin/printf";
    request.arguments.push_back(
        arp::core::pmr::String("%s", &arp::core::defaultCpuMemoryResource()));
    request.arguments.push_back(arp::core::pmr::String(
        "process runner & argument", &arp::core::defaultCpuMemoryResource()));
#endif
    request.outputFile = output;

    const auto success = ProcessRunner().run(request);
    if (!success.ok())
        std::printf("process runner failed: launched=%d exit=%u diagnostic=%s\n",
            success.launched ? 1 : 0,
            success.exitCode,
            success.diagnostic.c_str());
    assert(success.ok());
    const auto text = arp::core::readTextFile(output);
    assert(text.success);
    assert(text.text.find("process runner & argument") != arp::core::pmr::String::npos);

    request.executable = output.parent_path() / "arp-process-runner-missing";
    const auto failure = ProcessRunner().run(request);
    assert(!failure.ok());
    assert(!failure.launched);
    assert(!failure.diagnostic.empty());

    const auto lockRoot =
        std::filesystem::temp_directory_path() / "arp-process-identity-lock-contract";
    std::filesystem::create_directories(lockRoot);
    const auto lockPath = lockRoot / "shared.lock";
    auto firstLock =
        arp::core::makeUnique<arp::core::ProcessIdentityLock>(lockPath, "first-identity");
    assert(firstLock->locked());
    std::atomic<bool> secondAcquired{false};
    std::thread contender([&] {
        arp::core::ProcessIdentityLock secondLock(lockPath, "different-identity");
        secondAcquired.store(secondLock.locked(), std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!secondAcquired.load(std::memory_order_acquire));
    firstLock.reset();
    contender.join();
    assert(secondAcquired.load(std::memory_order_acquire));

    const auto snapshotSource = lockRoot / "snapshot-source";
    const auto snapshotRoot = lockRoot / "snapshot";
    std::filesystem::create_directories(snapshotSource / "nested");
    {
        std::ofstream file(
            snapshotSource / "nested" / "input.txt", std::ios::binary | std::ios::trunc);
        file << "stable-input";
    }
    {
        // A forwarding header can share a stem with its implementation package.
        // Path-component order and generic-string order differ for this pair.
        std::ofstream file(snapshotSource / "nested.h", std::ios::binary | std::ios::trunc);
        file << "forwarding-header";
    }
    arp::core::ImmutableBuildInputSnapshot snapshot(snapshotRoot);
    arp::core::pmr::String snapshotDiagnostics{&arp::core::defaultCpuMemoryResource()};
    assert(snapshot.captureDirectory("include", snapshotSource, snapshotDiagnostics));
    assert(snapshot.verifySourcesUnchanged(snapshotDiagnostics));

    arp::core::ImmutableBuildInputContract contractA;
    arp::core::ImmutableBuildInputContract contractB;
    assert(contractA.add("source", "alpha", snapshotDiagnostics));
    assert(contractA.add("tool", "beta", snapshotDiagnostics));
    assert(contractB.add("tool", "beta", snapshotDiagnostics));
    assert(contractB.add("source", "alpha", snapshotDiagnostics));
    assert(contractA.canonicalText() == contractB.canonicalText());
    assert(contractA.fingerprint() == contractB.fingerprint());
    arp::core::ImmutableBuildInputContract changedContract;
    assert(changedContract.add("source", "changed", snapshotDiagnostics));
    assert(changedContract.add("tool", "beta", snapshotDiagnostics));
    assert(changedContract.fingerprint() != contractA.fingerprint());
    assert(!contractB.add("source", "changed", snapshotDiagnostics));
    const std::filesystem::path executableSource = ARP_TEST_CXX_COMPILER;
    const auto capturedCompilerSource = std::filesystem::canonical(executableSource);
    assert(std::filesystem::is_regular_file(executableSource));
    const auto compilerLayout = arp::core::CompilerToolchainLayout().inspect(executableSource);
    const auto relocatedCompiler =
        executableSource.parent_path() / "." / executableSource.filename();
    const auto relocatedCompilerLayout =
        arp::core::CompilerToolchainLayout().inspect(relocatedCompiler);
    if (!compilerLayout.success || !relocatedCompilerLayout.success
        || compilerLayout.programSearchDirectory != relocatedCompilerLayout.programSearchDirectory
        || compilerLayout.systemIncludeDirectories
            != relocatedCompilerLayout.systemIncludeDirectories
        || compilerLayout.libraryDirectories != relocatedCompilerLayout.libraryDirectories) {
        std::abort();
    }
    assert(snapshot.captureExecutable(
        "tools/snapshot-tool.bin", capturedCompilerSource, snapshotDiagnostics));
    const auto reverseSnapshotRoot = lockRoot / "snapshot-reverse";
    arp::core::ImmutableBuildInputSnapshot reverseSnapshot(reverseSnapshotRoot);
    assert(reverseSnapshot.captureExecutable(
        "tools/snapshot-tool.bin", capturedCompilerSource, snapshotDiagnostics));
    assert(reverseSnapshot.captureDirectory("include", snapshotSource, snapshotDiagnostics));
    assert(reverseSnapshot.fingerprint() == snapshot.fingerprint());
    assert(reverseSnapshot.verifySourcesUnchanged(snapshotDiagnostics));
#if !defined(_WIN32) && !defined(__CYGWIN__)
    const auto resolvedRoot = lockRoot / "resolved-input-v1";
    std::filesystem::remove_all(resolvedRoot);
    std::filesystem::create_directories(resolvedRoot / "headers");
    {
        std::ofstream file(resolvedRoot / "headers" / "value.h");
        file << "resolved-input-v1";
    }
    std::filesystem::create_directory_symlink("headers", resolvedRoot / "alias");
    std::filesystem::create_directory_symlink(".", resolvedRoot / "headers/self");
    std::filesystem::create_symlink("headers/value.h", resolvedRoot / "value.h");
    arp::core::ImmutableBuildInputSnapshot resolvedSnapshot(lockRoot / "resolved-snapshot-v1");
    assert(resolvedSnapshot.captureResolvedDirectory("sdk", resolvedRoot, snapshotDiagnostics));
    assert(resolvedSnapshot.verifySourcesUnchanged(snapshotDiagnostics));
    assert(!std::filesystem::is_symlink(resolvedSnapshot.path("sdk/value.h")));
    assert(!std::filesystem::is_symlink(resolvedSnapshot.path("sdk/alias/value.h")));
    assert(arp::core::readTextFile(resolvedSnapshot.path("sdk/alias/value.h")).text
        == "resolved-input-v1");
    assert(arp::core::readTextFile(resolvedSnapshot.path("sdk/headers/self/value.h")).text
        == "resolved-input-v1");
    const auto capturedAlias = resolvedSnapshot.path("sdk/alias");
    std::filesystem::remove(capturedAlias);
    std::filesystem::create_directory_symlink(resolvedRoot / "headers", capturedAlias);
    assert(!resolvedSnapshot.verifySourcesUnchanged(snapshotDiagnostics));
    std::filesystem::remove(capturedAlias);
    std::filesystem::create_directory_symlink("headers", capturedAlias);
    assert(resolvedSnapshot.verifySourcesUnchanged(snapshotDiagnostics));
    arp::core::ImmutableBuildInputSnapshot strictLinks(lockRoot / "strict-snapshot-v1");
    assert(!strictLinks.captureDirectory("sdk", resolvedRoot, snapshotDiagnostics));
    std::filesystem::remove(resolvedRoot / "value.h");
    std::filesystem::create_symlink("alias/value.h", resolvedRoot / "value.h");
    // Changing the lexical link chain but retaining the canonical binding is
    // semantically equivalent. A different canonical file is rejected below.
    assert(resolvedSnapshot.verifySourcesUnchanged(snapshotDiagnostics));
    std::filesystem::copy_file(resolvedRoot / "headers/value.h", resolvedRoot / "headers/other.h");
    std::filesystem::remove(resolvedRoot / "value.h");
    std::filesystem::create_symlink("headers/other.h", resolvedRoot / "value.h");
    assert(!resolvedSnapshot.verifySourcesUnchanged(snapshotDiagnostics));
    std::filesystem::create_symlink("cycle-b", resolvedRoot / "cycle-a");
    std::filesystem::create_symlink("cycle-a", resolvedRoot / "cycle-b");
    arp::core::ImmutableBuildInputSnapshot cycles(lockRoot / "cycle-snapshot-v1");
    assert(!cycles.captureResolvedDirectory("sdk", resolvedRoot, snapshotDiagnostics));
    std::filesystem::remove(resolvedRoot / "cycle-a");
    std::filesystem::remove(resolvedRoot / "cycle-b");
    std::filesystem::create_symlink(output, resolvedRoot / "escape");
    arp::core::ImmutableBuildInputSnapshot escapes(lockRoot / "escape-snapshot-v1");
    assert(!escapes.captureResolvedDirectory("sdk", resolvedRoot, snapshotDiagnostics));
    std::filesystem::remove(resolvedRoot / "escape");
    const auto permissionSource = snapshotSource / "nested" / "input.txt";
    const auto originalPermissions = std::filesystem::status(permissionSource).permissions();
    arp::core::ImmutableBuildInputSnapshot permissionBaseline(
        lockRoot / "snapshot-permission-baseline");
    assert(permissionBaseline.captureDirectory("include", snapshotSource, snapshotDiagnostics));
    std::filesystem::permissions(permissionSource,
        originalPermissions | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace);
    arp::core::ImmutableBuildInputSnapshot permissionVariant(
        lockRoot / "snapshot-permission-variant");
    assert(permissionVariant.captureDirectory("include", snapshotSource, snapshotDiagnostics));
    assert(permissionVariant.fingerprint() != permissionBaseline.fingerprint());
    std::filesystem::permissions(
        permissionSource, originalPermissions, std::filesystem::perm_options::replace);
    assert(std::filesystem::status(snapshot.path("tools/snapshot-tool.bin")).permissions()
        == std::filesystem::status(executableSource).permissions());
#endif
    assert(std::filesystem::is_regular_file(snapshot.path("include/nested/input.txt")));
    assert(!snapshot.fingerprint().empty());
    assert(snapshot.verifySourcesUnchanged(snapshotDiagnostics));
    {
        std::ofstream file(
            snapshot.path("include/nested/input.txt"), std::ios::binary | std::ios::trunc);
        file << "captured-drift";
    }
    assert(!snapshot.verifySourcesUnchanged(snapshotDiagnostics));
    {
        std::ofstream file(
            snapshot.path("include/nested/input.txt"), std::ios::binary | std::ios::trunc);
        file << "stable-input";
    }
    assert(snapshot.verifySourcesUnchanged(snapshotDiagnostics));
    {
        std::ofstream file(
            snapshotSource / "nested" / "input.txt", std::ios::binary | std::ios::trunc);
        file << "changed-input";
    }
    assert(!snapshot.verifySourcesUnchanged(snapshotDiagnostics));

    std::filesystem::remove(output, cleanupError);
    std::filesystem::remove_all(lockRoot, cleanupError);
    return 0;
}
