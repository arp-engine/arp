#pragma once

#include "editor/workspace/editor_scene_persistence.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>

namespace arp::editor::testfixtures::persistence_io {
using Service = EditorScenePersistence;
using Result = core::io::AtomicFileResult;

inline Result finish(Service& service, core::PoolHandle handle)
{
    assert(handle);
    Result result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (service.take(handle, result))
            return result;
        std::this_thread::yield();
    }
    assert(false && "scene IO completion deadline");
    return result;
}

inline bool equals(const Result& result, std::string_view text)
{
    return result.bytes.size() == text.size()
        && (text.empty() || std::memcmp(result.bytes.data(), text.data(), text.size()) == 0);
}

inline void noTemporaries(const std::filesystem::path& root)
{
    for (const auto& entry : std::filesystem::directory_iterator(root))
        assert(entry.path().filename().string().find(".arp-tmp-") == std::string::npos);
}

} // namespace arp::editor::testfixtures::persistence_io
