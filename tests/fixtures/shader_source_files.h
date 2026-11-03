#pragma once

#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "resource/resource_manager.h"

#include <algorithm>
#include <cassert>

namespace shader_source_fixture {
// Only an explicitly produced, finite test tree is enumerated. Runtime
// acquisition discovers dependencies through the existing shader loaders.
struct Files {
    arp::core::pmr::Vector<arp::resource::ResourceSourceBuffer> files;
    arp::core::pmr::Vector<arp::core::pmr::String> directories;

    Files(const std::filesystem::path& root, std::size_t fileBytes, std::size_t entries)
    {
        for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
            const auto name = arp::core::FilePath::utf8(item.path().lexically_relative(root));
            if (item.is_directory())
                directories.push_back(name);
            if (item.is_regular_file()) {
                auto input = arp::core::readBinaryFile(item.path(), fileBytes);
                assert(input.success);
                arp::resource::ResourceSourceBuffer source;
                source.path = name;
                source.bytes = std::move(input.bytes);
                files.push_back(std::move(source));
            }
            assert(files.size() + directories.size() <= entries);
        }
        std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
            return a.size() == b.size() ? a < b : a.size() < b.size();
        });
    }
};
} // namespace shader_source_fixture
