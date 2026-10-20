#include "scene/diagnostics.h"

#include <utility>

namespace arp::scene {

SceneResult::SceneResult(SceneDiagnostic diagnostic) : diagnostic_(std::move(diagnostic))
{}

SceneResult SceneResult::success()
{
    return SceneResult(SceneDiagnostic{});
}

SceneResult SceneResult::failure(SceneDiagnosticCode code, arp::core::pmr::String message)
{
    return SceneResult(SceneDiagnostic{code, std::move(message)});
}

} // namespace arp::scene
