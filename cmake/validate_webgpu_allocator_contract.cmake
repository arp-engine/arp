if(NOT DEFINED ARP_SOURCE_DIR)
    message(FATAL_ERROR "ARP_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE _webgpu_sources LIST_DIRECTORIES false
    "${ARP_SOURCE_DIR}/src/rhi/backends/webgpu/*")
list(APPEND _webgpu_sources
    "${ARP_SOURCE_DIR}/src/rhi/frontend/webgpu_runtime_telemetry.h")

set(_source_extensions
    ".c" ".cc" ".cpp" ".cxx" ".h" ".hpp" ".inc" ".inl" ".ipp" ".mm")
set(_forbidden_pattern
    "std[ \t]*::[ \t]*(pmr[ \t]*::[ \t]*)?vector|#[ \t]*include[ \t]*[<\"]vector[>\"]")

foreach(_source IN LISTS _webgpu_sources)
    get_filename_component(_extension "${_source}" EXT)
    list(FIND _source_extensions "${_extension}" _extension_index)
    if(_extension_index EQUAL -1)
        continue()
    endif()

    file(READ "${_source}" _contents)
    string(REGEX MATCH "${_forbidden_pattern}" _match "${_contents}")
    if(_match)
        file(RELATIVE_PATH _relative_source "${ARP_SOURCE_DIR}" "${_source}")
        message(FATAL_ERROR
            "WebGPU allocator contract forbids '${_match}' in ${_relative_source}")
    endif()
endforeach()

message(STATUS "WebGPU allocator contract: passed")
