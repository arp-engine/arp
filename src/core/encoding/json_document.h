#pragma once

#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <rapidjson/document.h>
#include <string_view>

namespace arp::core::json {
// Allocator-aware JSON values for bounded wire/encoding consumers. Domain
// schemas, authorization and file/resource semantics remain with their owners.
struct alignas(std::max_align_t) JsonAllocationHeader {
    std::size_t size = 0;
    std::pmr::memory_resource* resource = nullptr;
};

class CoreJsonAllocator final {
public:
    static constexpr bool kNeedFree = true;

    explicit CoreJsonAllocator(
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : resource_(resource)
    {}

    CoreJsonAllocator(const CoreJsonAllocator&) = default;
    CoreJsonAllocator& operator=(const CoreJsonAllocator&) = default;

    void* Malloc(std::size_t size)
    {
        const auto payloadSize = std::max<std::size_t>(size, 1u);
        if (payloadSize > std::numeric_limits<std::size_t>::max() - sizeof(JsonAllocationHeader))
            throw std::bad_alloc();
        auto* header = static_cast<JsonAllocationHeader*>(resource_->allocate(
            sizeof(JsonAllocationHeader) + payloadSize, alignof(JsonAllocationHeader)));
        header->size = payloadSize;
        header->resource = resource_;
        return header + 1;
    }

    void* Realloc(void* original, std::size_t originalSize, std::size_t newSize)
    {
        if (!original)
            return Malloc(newSize);
        if (newSize == 0) {
            Free(original);
            return nullptr;
        }
        void* replacement = Malloc(newSize);
        std::memcpy(replacement, original, std::min(originalSize, newSize));
        Free(original);
        return replacement;
    }

    static void Free(void* ptr)
    {
        if (!ptr)
            return;
        auto* header = static_cast<JsonAllocationHeader*>(ptr) - 1;
        header->resource->deallocate(
            header, sizeof(JsonAllocationHeader) + header->size, alignof(JsonAllocationHeader));
    }

private:
    std::pmr::memory_resource* resource_;
};

inline CoreJsonAllocator& jsonAllocator()
{
    static CoreJsonAllocator allocator;
    return allocator;
}

// Passing both allocators explicitly also prevents RapidJSON from creating
// default-owned allocator objects behind an otherwise core-backed parser.
class JsonDocument final
    : public rapidjson::GenericDocument<rapidjson::UTF8<>, CoreJsonAllocator, CoreJsonAllocator> {
    using Base =
        rapidjson::GenericDocument<rapidjson::UTF8<>, CoreJsonAllocator, CoreJsonAllocator>;

public:
    JsonDocument() : Base(&jsonAllocator(), 0, &jsonAllocator()) {}

    JsonDocument(
        CoreJsonAllocator* allocator, std::size_t stackCapacity, CoreJsonAllocator* stackAllocator)
        : Base(allocator, stackCapacity, stackAllocator)
    {}

    JsonDocument(JsonDocument&&) = default;
    JsonDocument& operator=(JsonDocument&&) = default;
};

using JsonValue = rapidjson::GenericValue<rapidjson::UTF8<>, CoreJsonAllocator>;

inline const JsonValue* jsonMember(const JsonValue& object, const char* name)
{
    if (!object.IsObject())
        return nullptr;
    const auto it = object.FindMember(name);
    return it == object.MemberEnd() ? nullptr : &it->value;
}

inline std::string_view jsonText(const JsonValue& value)
{
    return value.IsString() ? std::string_view(value.GetString(), value.GetStringLength())
                            : std::string_view{};
}

inline bool jsonOnlyUniqueKeys(
    const JsonValue& object, std::initializer_list<std::string_view> allowed)
{
    if (!object.IsObject() || object.MemberCount() > allowed.size())
        return false;
    for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
        const auto name = jsonText(it->name);
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
            return false;
        for (auto before = object.MemberBegin(); before != it; ++before)
            if (jsonText(before->name) == name)
                return false;
    }
    return true;
}

} // namespace arp::core::json
