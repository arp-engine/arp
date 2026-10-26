#pragma once

namespace arp::editor {
class GamePreviewModel final {
public:
    enum class Request { None, Start, Stop };

    bool requestStart() noexcept;
    bool requestStop() noexcept;
    Request consumeRequest() noexcept;
    void setState(bool running, bool starting) noexcept;

    bool running() const noexcept { return running_; }

    bool starting() const noexcept { return starting_; }

    bool hasPendingRequest() const noexcept { return request_ != Request::None; }

private:
    Request request_ = Request::None;
    bool running_ = false;
    bool starting_ = false;
};
} // namespace arp::editor
