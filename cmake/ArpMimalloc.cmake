# Shared core allocator dependency for full engine and isolated runtime products.
include_guard(GLOBAL)

set(ARP_MIMALLOC_ROOT "${ARP_THIRD_PARTY_DIR}/mimalloc" CACHE PATH "Optional local mimalloc source root")
set(ARP_MIMALLOC_VERSION "2.1.7" CACHE STRING "Pinned mimalloc version for FetchContent")
set(ARP_MIMALLOC_URL "https://codeload.github.com/microsoft/mimalloc/zip/refs/tags/v${ARP_MIMALLOC_VERSION}" CACHE STRING "Pinned mimalloc source archive URL")
set(ARP_MIMALLOC_SHA256 "FA61CF01E3DD869B35275BFD8BE95BFDE77F0B65DFA7E34012C09A66E1EA463F" CACHE STRING "Pinned mimalloc source archive SHA256")

set(_arp_mimalloc_target "")
find_package(mimalloc CONFIG QUIET)
if(TARGET mimalloc-static)
    set(_arp_mimalloc_target mimalloc-static)
elseif(TARGET mimalloc)
    set(_arp_mimalloc_target mimalloc)
elseif(EXISTS "${ARP_MIMALLOC_ROOT}/CMakeLists.txt")
    set(MI_OVERRIDE OFF CACHE BOOL "Disable global malloc override for arp core CPU allocator" FORCE)
    set(MI_BUILD_SHARED OFF CACHE BOOL "Disable mimalloc shared library" FORCE)
    set(MI_BUILD_STATIC ON CACHE BOOL "Build mimalloc static library" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "Disable mimalloc object library" FORCE)
    set(MI_BUILD_TESTS OFF CACHE BOOL "Disable vendored mimalloc tests" FORCE)
    add_subdirectory("${ARP_MIMALLOC_ROOT}" "${CMAKE_BINARY_DIR}/_deps/mimalloc" EXCLUDE_FROM_ALL)
    if(TARGET mimalloc-static)
        set(_arp_mimalloc_target mimalloc-static)
    endif()
else()
    include(FetchContent)
    set(MI_OVERRIDE OFF CACHE BOOL "Disable global malloc override for arp core CPU allocator" FORCE)
    set(MI_BUILD_SHARED OFF CACHE BOOL "Disable mimalloc shared library" FORCE)
    set(MI_BUILD_STATIC ON CACHE BOOL "Build mimalloc static library" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "Disable mimalloc object library" FORCE)
    set(MI_BUILD_TESTS OFF CACHE BOOL "Disable vendored mimalloc tests" FORCE)
    FetchContent_Declare(
        arp_mimalloc
        URL "${ARP_MIMALLOC_URL}"
        URL_HASH "SHA256=${ARP_MIMALLOC_SHA256}"
        TLS_VERIFY OFF
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(arp_mimalloc)
    if(TARGET mimalloc-static)
        set(_arp_mimalloc_target mimalloc-static)
    endif()
endif()

if(NOT _arp_mimalloc_target)
    message(FATAL_ERROR "mimalloc is required for arp_core CPU allocation but no mimalloc target was produced")
endif()

get_target_property(_arp_mimalloc_is_imported ${_arp_mimalloc_target} IMPORTED)
if(NOT _arp_mimalloc_is_imported)
    # arp embeds the static allocator in process-lifetime runtime libraries.
    # Its atexit hook can otherwise run before DLL/static C++ destructors that
    # still own PMR allocations.
    target_compile_definitions(${_arp_mimalloc_target}
        PRIVATE ARP_MIMALLOC_PROCESS_LIFETIME=1)
endif()

add_library(arp_mimalloc INTERFACE)
target_link_libraries(arp_mimalloc INTERFACE ${_arp_mimalloc_target})
target_compile_definitions(arp_mimalloc INTERFACE ARP_HAS_MIMALLOC=1)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Emscripten's mimalloc port is built against emmalloc.  Propagate the
    # matching system allocator through the allocator target so every final
    # web consumer links the required emmalloc_* symbols.
    target_link_options(arp_mimalloc INTERFACE "-sMALLOC=emmalloc")
endif()
set(ARP_HAS_MIMALLOC ON)
