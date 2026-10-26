#pragma once

#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"

namespace arp::scene {

AENUM()
enum class SceneDiagnosticCode {
    None,
    InvalidObjectId,
    InvalidParentId,
    CannotReparentSceneRoot,
    HierarchyCycle,
    InvalidChildIndex,
    TraversalBudgetExceeded,
    ScriptExecutionFailed,
    PassiveExecutionRejected,
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct SceneDiagnostic {
    APROP(Runtime)
    SceneDiagnosticCode code = SceneDiagnosticCode::None;
    APROP(Runtime)
    arp::core::pmr::String message;
};

class SceneResult {
public:
    static SceneResult success();
    static SceneResult failure(SceneDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == SceneDiagnosticCode::None; }

    const SceneDiagnostic& diagnostic() const { return diagnostic_; }

private:
    explicit SceneResult(SceneDiagnostic diagnostic);

    SceneDiagnostic diagnostic_;
};

} // namespace arp::scene
