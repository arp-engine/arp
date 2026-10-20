#include "core/cpu_allocator.h"
#include "scriptvm/editor/editor_hot_update.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

using namespace arp::scriptvm;

std::shared_ptr<Program> makeProgram(std::uint32_t version, int result, const char*)
{
    ScriptInterpreterBuildRequest request;
    request.source = "def main() -> int:\n    return ";
    request.source += std::to_string(result);
    request.source += "\n";
    request.filename = "editor-hot-update.arp";
    request.moduleId = "editor-hot-update";
    request.moduleVersion = version;
    request.dependencyGraphHash = arp::scriptvm::test::fixtureHash('2');
    request.reflectionRegistryHash = arp::scriptvm::test::fixtureHash('3');
    const auto built = ScriptInterpreterPipeline::build(request);
    return built.ok() ? arp::core::makeShared<Program>(built.program) : nullptr;
}

std::shared_ptr<Program> makeComplexHandlerProgram()
{
    const auto built = arp::scriptvm::test::buildInterpreter("def main() -> int:\n"
                                                             "    try:\n"
                                                             "        raise \"outer\"\n"
                                                             "    except as outer:\n"
                                                             "        try:\n"
                                                             "            raise outer\n"
                                                             "        except as inner:\n"
                                                             "            return 42\n"
                                                             "    return 0\n",
        "editor-complex-handler.arp",
        "editor-hot-update");
    return built.ok() ? arp::core::makeShared<Program>(built.program) : nullptr;
}

std::shared_ptr<Program> makePendingProgram(std::uint32_t version)
{
    ScriptInterpreterBuildRequest request;
    request.source = "def main() -> int:\n"
                     "    return 33\n"
                     "def pending(value: any) -> any:\n"
                     "    return await value\n";
    request.filename = "editor-pending.arp";
    request.moduleId = "editor-hot-update";
    request.moduleVersion = version;
    request.dependencyGraphHash = arp::scriptvm::test::fixtureHash('2');
    request.reflectionRegistryHash = arp::scriptvm::test::fixtureHash('3');
    const auto built = ScriptInterpreterPipeline::build(request);
    return built.ok() ? arp::core::makeShared<Program>(built.program) : nullptr;
}

InterpreterHotUpdateRequest requestFor(const std::shared_ptr<Program>& program,
    std::uint32_t version,
    const char* hostAbi = "host-abi:v1")
{
    InterpreterHotUpdateRequest request;
    request.moduleId = "editor-hot-update";
    request.moduleVersion = version;
    request.program = program;
    request.programOwner = program;
    request.identity.artifact = program->artifactIdentity;
    request.identity.hostAbiHash = hostAbi;
    request.maxRetainedValues = 4;
    return request;
}

#if defined(_WIN32)
bool createHiddenWindow(HWND& window)
{
    constexpr const char* className = "ArpScriptEditorHotUpdateSmoke";
    WNDCLASSA windowClass{};
    windowClass.lpfnWndProc = DefWindowProcA;
    windowClass.hInstance = GetModuleHandleA(nullptr);
    windowClass.lpszClassName = className;
    if (RegisterClassA(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    window = CreateWindowExA(WS_EX_TOOLWINDOW,
        className,
        "Arp Script Editor Hot Update",
        WS_OVERLAPPED,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        windowClass.hInstance,
        nullptr);
    return window != nullptr;
}
#endif

} // namespace

int main()
{
#if defined(_WIN32)
    HWND window = nullptr;
    if (!createHiddenWindow(window))
        return 1;
#endif

    auto initialProgram = makeProgram(1, 11, "sha256:source-v1");
    arp::scriptvm::VmError error;

    auto incomplete = requestFor(initialProgram, 1);
    incomplete.identity = {};
    if (InterpreterHotUpdateService::create(std::move(incomplete), &error)
        || error.code != ScriptDiagnosticCode::AotFingerprintMismatch) {
        return 2;
    }

    auto moduleMismatch = requestFor(initialProgram, 1);
    moduleMismatch.moduleId = "editor-hot-update-other";
    if (InterpreterHotUpdateService::create(std::move(moduleMismatch), &error)
        || error.code != ScriptDiagnosticCode::HybridModuleVersionMismatch) {
        return 3;
    }

    auto artifactMismatch = requestFor(initialProgram, 1);
    artifactMismatch.identity.artifact.sourceHash = "sha256:not-the-program";
    if (InterpreterHotUpdateService::create(std::move(artifactMismatch), &error)
        || error.code != ScriptDiagnosticCode::AotFingerprintMismatch) {
        return 4;
    }

    auto ownerMismatch = requestFor(initialProgram, 1);
    ownerMismatch.programOwner = makeProgram(1, 11, "sha256:source-v1");
    if (InterpreterHotUpdateService::create(std::move(ownerMismatch), &error)
        || error.code != ScriptDiagnosticCode::AotFingerprintMismatch) {
        return 5;
    }

    auto service = InterpreterHotUpdateService::create(requestFor(initialProgram, 1), &error);
    if (service == nullptr || error.code != ScriptDiagnosticCode::None)
        return 6;

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    int result = 0;
    if (!tryGetInt(service->invoke("main", args, &error), result) || result != 11
        || error.code != ScriptDiagnosticCode::None) {
        return 7;
    }

    // Mutable request owners cannot change the service's validated snapshot.
    initialProgram->bytecodeFingerprint = "sha256:mutated-after-create";
    initialProgram->functions.clear();
    if (!tryGetInt(service->invoke("main", args, &error), result) || result != 11
        || error.code != ScriptDiagnosticCode::None)
        return 18;

    auto complexProgram = makeComplexHandlerProgram();
    if (complexProgram == nullptr)
        return 12;
    auto complexService =
        InterpreterHotUpdateService::create(requestFor(complexProgram, 1), &error);
    if (complexService == nullptr || error.code != ScriptDiagnosticCode::None)
        return 13;
    if (!tryGetInt(complexService->invoke("main", args, &error), result) || result != 42
        || error.code != ScriptDiagnosticCode::None) {
        return 14;
    }

    auto staleProgram = makeProgram(1, 99, "sha256:source-stale");
    if (service->replace(requestFor(staleProgram, 1), &error)
        || error.code != ScriptDiagnosticCode::HybridModuleVersionMismatch) {
        return 8;
    }

    auto hostMismatchProgram = makeProgram(2, 22, "sha256:source-v2");
    if (service->replace(requestFor(hostMismatchProgram, 2, "host-abi:v2"), &error)
        || error.code != ScriptDiagnosticCode::AotFingerprintMismatch) {
        return 9;
    }

    if (!service->replace(requestFor(hostMismatchProgram, 2), &error)
        || error.code != ScriptDiagnosticCode::None) {
        return 10;
    }
    if (!tryGetInt(service->invoke("main", args, &error), result) || result != 22
        || error.code != ScriptDiagnosticCode::None) {
        return 11;
    }

    // A hot update cannot silently discard a pending await owned by the
    // current interpreter. The caller must explicitly adopt or close it.
    auto pendingProgram = makePendingProgram(3);
    if (pendingProgram == nullptr)
        return 15;
    auto pendingService =
        InterpreterHotUpdateService::create(requestFor(pendingProgram, 3), &error);
    if (pendingService == nullptr || error.code != ScriptDiagnosticCode::None)
        return 15;
    ScriptAwaitable awaitable;
    awaitable.poll = [](Value&, arp::core::pmr::String&) {
        return ScriptAwaitStatus::Pending;
    };
    arp::core::pmr::Vector<Value> pendingArgs{&arp::core::defaultCpuMemoryResource()};
    pendingArgs.push_back(Value{awaitable});
    (void)pendingService->invoke("pending", pendingArgs, &error);
    if (error.code != ScriptDiagnosticCode::Suspended)
        return 16;
    auto pendingReplacement = makeProgram(4, 44, "sha256:source-v4");
    if (pendingService->replace(requestFor(pendingReplacement, 4), &error)
        || error.code != ScriptDiagnosticCode::HybridPlanInvalid)
        return 17;

#if defined(_WIN32)
    DestroyWindow(window);
#endif
    return 0;
}
