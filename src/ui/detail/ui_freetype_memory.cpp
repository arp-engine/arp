#include "ui/detail/ui_freetype_memory.h"

#include "core/memory/cpu_allocator.h"

#if ARP_HAS_FREETYPE
#include FT_MODULE_H

#include <algorithm>
#include <cstring>

namespace arp::ui::detail {
namespace {

void* allocate(FT_Memory, long size) noexcept
{
    if (size <= 0)
        return nullptr;
    try {
        core::CpuAllocationDesc desc;
        desc.sizeBytes = static_cast<std::size_t>(size);
        desc.alignment = alignof(std::max_align_t);
        desc.label = "FreeType";
        return core::defaultCpuAllocator().allocate(desc).ptr;
    } catch (...) {
        return nullptr;
    }
}

void release(FT_Memory, void* pointer) noexcept
{
    if (!pointer)
        return;
    core::CpuAllocationDesc desc;
    desc.alignment = alignof(std::max_align_t);
    desc.label = "FreeType";
    core::defaultCpuAllocator().deallocate(pointer, desc);
}

void* reallocate(FT_Memory memory, long oldSize, long newSize, void* pointer) noexcept
{
    if (newSize <= 0) {
        release(memory, pointer);
        return nullptr;
    }
    if (oldSize < 0)
        return nullptr;
    auto* output = allocate(memory, newSize);
    if (!output)
        return nullptr;
    if (pointer)
        std::memcpy(output, pointer, static_cast<std::size_t>(std::min(oldSize, newSize)));
    release(memory, pointer);
    return output;
}

} // namespace

bool initializeFreeTypeLibrary(FT_Library& library) noexcept
{
    static FT_MemoryRec_ memory{nullptr, allocate, release, reallocate};
    library = nullptr;
    if (FT_New_Library(&memory, &library) || !library)
        return false;
    FT_Add_Default_Modules(library);
    return true;
}

} // namespace arp::ui::detail
#endif
