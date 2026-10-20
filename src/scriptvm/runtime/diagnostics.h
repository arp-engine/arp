#pragma once

// ArpScript: compile/runtime diagnostics + Result type.
// Follows the SceneResult pattern (src/scene/diagnostics.h) but adds line/column.

#include "core/cpu_allocator.h"

#include <string_view>

namespace arp::scriptvm {

enum class ScriptDiagnosticCode {
    None,
    // Lexer
    UnterminatedString,
    InvalidEscape,
    InvalidNumber,
    UnexpectedChar,
    // Parser
    UnexpectedToken,
    UnexpectedDedent,
    UnexpectedIndent,
    UnexpectedEof,
    MissingTypeHint,
    MissingColon,
    MissingCloseParen,
    MissingCloseBracket,
    MissingCloseBrace,
    InvalidAssignment,
    // Sema
    UndeclaredName,
    RedeclaredName,
    TypeMismatch,
    NotCallable,
    WrongArgCount,
    NotIndexable,
    NoMember,
    ImmutableAssign,
    // Runtime
    StackUnderflow,
    InvalidOpcode,
    BadCast,
    DivisionByZero,
    IndexError,
    KeyError,
    NilDeref,
    HostError,
    UserRaise,
    Suspended, // Yield: function suspended (not an error)
    TypedIrVersionMismatch,
    TypedIrInvalid,
    AotArtifactInvalid,
    AotVersionMismatch,
    AotFingerprintMismatch,
    AotMissingEntry,
    AotDuplicateEntry,
    AotSwitchUnsupported,
    AotModuleUnavailable,
    HybridPolicyRejected,
    HybridModuleVersionMismatch,
    HybridPlanInvalid,
    ExecutionBudgetExceeded,
    ResourceLimitExceeded,
    RecursionLimitExceeded,
    IntegerOverflow,
    ModuleRetentionBudgetExceeded,
};

// Typed except filters are deliberately bound to the stable runtime
// diagnostic vocabulary. They are not Python exception classes: accepting an
// unknown name would make AOT and Interpreter dispatch diverge.
inline const char* scriptDiagnosticCodeName(ScriptDiagnosticCode code) noexcept
{
    switch (code) {
    case ScriptDiagnosticCode::StackUnderflow:
        return "StackUnderflow";
    case ScriptDiagnosticCode::InvalidOpcode:
        return "InvalidOpcode";
    case ScriptDiagnosticCode::BadCast:
        return "BadCast";
    case ScriptDiagnosticCode::DivisionByZero:
        return "DivisionByZero";
    case ScriptDiagnosticCode::IndexError:
        return "IndexError";
    case ScriptDiagnosticCode::KeyError:
        return "KeyError";
    case ScriptDiagnosticCode::NilDeref:
        return "NilDeref";
    case ScriptDiagnosticCode::HostError:
        return "HostError";
    case ScriptDiagnosticCode::UserRaise:
        return "UserRaise";
    case ScriptDiagnosticCode::ExecutionBudgetExceeded:
        return "ExecutionBudgetExceeded";
    case ScriptDiagnosticCode::ResourceLimitExceeded:
        return "ResourceLimitExceeded";
    case ScriptDiagnosticCode::RecursionLimitExceeded:
        return "RecursionLimitExceeded";
    case ScriptDiagnosticCode::IntegerOverflow:
        return "IntegerOverflow";
    case ScriptDiagnosticCode::ModuleRetentionBudgetExceeded:
        return "ModuleRetentionBudgetExceeded";
    default:
        return nullptr;
    }
}

inline bool scriptDiagnosticCodeFromName(std::string_view name, ScriptDiagnosticCode& out) noexcept
{
    constexpr ScriptDiagnosticCode kCodes[] = {
        ScriptDiagnosticCode::StackUnderflow,
        ScriptDiagnosticCode::InvalidOpcode,
        ScriptDiagnosticCode::BadCast,
        ScriptDiagnosticCode::DivisionByZero,
        ScriptDiagnosticCode::IndexError,
        ScriptDiagnosticCode::KeyError,
        ScriptDiagnosticCode::NilDeref,
        ScriptDiagnosticCode::HostError,
        ScriptDiagnosticCode::UserRaise,
        ScriptDiagnosticCode::ExecutionBudgetExceeded,
        ScriptDiagnosticCode::ResourceLimitExceeded,
        ScriptDiagnosticCode::RecursionLimitExceeded,
        ScriptDiagnosticCode::IntegerOverflow,
        ScriptDiagnosticCode::ModuleRetentionBudgetExceeded,
    };
    for (const auto code : kCodes) {
        const char* canonical = scriptDiagnosticCodeName(code);
        if (canonical != nullptr && name == canonical) {
            out = code;
            return true;
        }
    }
    return false;
}

struct ScriptDiagnostic {
    ScriptDiagnosticCode code = ScriptDiagnosticCode::None;
    core::pmr::String message{&core::defaultCpuMemoryResource()};
    core::pmr::String file{&core::defaultCpuMemoryResource()};
    int line = 0;
    int column = 0;

    ScriptDiagnostic() = default;

    ScriptDiagnostic(ScriptDiagnosticCode c, core::pmr::String msg, int ln = 0, int col = 0)
        : code(c), message(std::move(msg)), line(ln), column(col)
    {}

    ScriptDiagnostic(
        ScriptDiagnosticCode c, core::pmr::String msg, core::pmr::String f, int ln = 0, int col = 0)
        : code(c), message(std::move(msg)), file(std::move(f)), line(ln), column(col)
    {}
};

class ScriptResult {
public:
    ScriptResult() = default;

    static ScriptResult success() { return {}; }

    static ScriptResult failure(ScriptDiagnostic diag)
    {
        ScriptResult r;
        r.diagnostic_ = std::move(diag);
        return r;
    }

    static ScriptResult failure(
        ScriptDiagnosticCode code, core::pmr::String msg, int line = 0, int col = 0)
    {
        return failure(ScriptDiagnostic{code, std::move(msg), line, col});
    }

    bool ok() const { return diagnostic_.code == ScriptDiagnosticCode::None; }

    bool failed() const { return !ok(); }

    const ScriptDiagnostic& diagnostic() const { return diagnostic_; }

private:
    ScriptDiagnostic diagnostic_;
};

} // namespace arp::scriptvm
