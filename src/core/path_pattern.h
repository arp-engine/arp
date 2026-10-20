#pragma once

#include "core/cpu_allocator.h"

#include <string_view>

namespace arp::core {

// Segment patterns are reusable outside HTTP. Matching never decodes or
// normalizes input, so encoded separators cannot change segment boundaries.
class PathPattern {
public:
    using View = std::string_view;

    struct Segment {
        pmr::String text{&defaultCpuMemoryResource()};
        bool parameter = false;
    };

    struct Parameter {
        pmr::String name{&defaultCpuMemoryResource()};
        pmr::String value{&defaultCpuMemoryResource()};
    };

    using Parameters = pmr::Vector<Parameter>;

    bool assign(View pattern)
    {
        segments_.clear();
        if (!validPath(pattern) || pattern.find_first_of("?") != View::npos)
            return false;
        std::size_t cursor = 1;
        do {
            const auto end = pattern.find('/', cursor);
            const auto text = pattern.substr(cursor, end == View::npos ? end : end - cursor);
            Segment segment;
            segment.parameter = !text.empty() && text.front() == ':';
            if (segment.parameter) {
                auto name = text.substr(1);
                if (name.empty())
                    return false;
                for (char c : name)
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                            || c == '_'))
                        return false;
                for (const auto& previous : segments_)
                    if (previous.parameter && previous.text == name)
                        return false;
                segment.text.assign(name);
            } else {
                if (text.find_first_of(":*") != View::npos)
                    return false;
                segment.text.assign(text);
            }
            segments_.push_back(std::move(segment));
            if (end == View::npos)
                break;
            cursor = end + 1;
        } while (cursor <= pattern.size());
        return true;
    }

    bool matches(View path, Parameters* parameters = nullptr) const
    {
        if (!validPath(path))
            return false;
        if (parameters)
            parameters->clear();
        std::size_t cursor = 1;
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            const auto end = path.find('/', cursor);
            const auto part = path.substr(cursor, end == View::npos ? end : end - cursor);
            const auto& segment = segments_[i];
            if (segment.parameter ? part.empty() : part != segment.text)
                return false;
            if (parameters && segment.parameter) {
                Parameter p;
                p.name = segment.text;
                p.value.assign(part);
                parameters->push_back(std::move(p));
            }
            if (end == View::npos)
                return i + 1 == segments_.size();
            cursor = end + 1;
        }
        return false;
    }

    bool sameShape(const PathPattern& other) const noexcept
    {
        if (segments_.size() != other.segments_.size())
            return false;
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            const auto& a = segments_[i];
            const auto& b = other.segments_[i];
            if (a.parameter != b.parameter || (!a.parameter && a.text != b.text))
                return false;
        }
        return true;
    }

    bool moreSpecificThan(const PathPattern& other) const noexcept
    {
        for (std::size_t i = 0; i < segments_.size() && i < other.segments_.size(); ++i)
            if (segments_[i].parameter != other.segments_[i].parameter)
                return !segments_[i].parameter;
        return false;
    }

    static bool validPath(View path) noexcept
    {
        if (path.empty() || path.front() != '/')
            return false;
        return path.find('?') == View::npos && validEncodedText(path);
    }

    static bool validEncodedText(View path) noexcept
    {
        for (std::size_t i = 0; i < path.size(); ++i) {
            const auto c = static_cast<unsigned char>(path[i]);
            if (c <= 32 || c >= 127 || c == '#' || c == '\\')
                return false;
            if (c == '%') {
                if (i + 2 >= path.size() || !hex(path[i + 1]) || !hex(path[i + 2]))
                    return false;
                i += 2;
            }
        }
        return true;
    }

private:
    static bool hex(char c) noexcept
    {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    }

    pmr::Vector<Segment> segments_{&defaultCpuMemoryResource()};
};

} // namespace arp::core
