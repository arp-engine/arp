#include "editor/workspace/game_preview_model.h"

namespace arp::editor {
bool GamePreviewModel::requestStart() noexcept
{
    if (running_ || starting_ || request_ != Request::None)
        return false;
    starting_ = true;
    request_ = Request::Start;
    return true;
}

bool GamePreviewModel::requestStop() noexcept
{
    if ((!running_ && !starting_) || request_ == Request::Stop)
        return false;
    request_ = Request::Stop;
    return true;
}

GamePreviewModel::Request GamePreviewModel::consumeRequest() noexcept
{
    const auto request = request_;
    request_ = Request::None;
    return request;
}

void GamePreviewModel::setState(bool running, bool starting) noexcept
{
    running_ = running;
    starting_ = starting;
}
} // namespace arp::editor
