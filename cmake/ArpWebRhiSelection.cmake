# WebGL1ProductionBuild/1: product selection only, never GPU authorization.
include_guard(GLOBAL)

function(arp_select_web_rhi system_name requested output_variable)
    if(NOT "${requested}" STREQUAL ""
        AND NOT "${requested}" STREQUAL "none"
        AND NOT "${requested}" STREQUAL "webgpu"
        AND NOT "${requested}" STREQUAL "webgl1")
        message(FATAL_ERROR "RHI-WEB-BUILD-SELECTION-UNKNOWN: ARP_WEB_RHI must be none, webgpu or webgl1")
    endif()

    if("${system_name}" STREQUAL "Emscripten")
        if("${requested}" STREQUAL "none")
            message(FATAL_ERROR "RHI-WEB-BUILD-SELECTION-REQUIRED: Emscripten requires a Web RHI product")
        elseif("${requested}" STREQUAL "webgl1")
            # Replace only after P2's real target and dependency isolation pass.
            # Never continue through the existing WebGPU platform branch.
            message(FATAL_ERROR "RHI-WEBGL1-BUILD-UNAVAILABLE: production WebGL1 target is not implemented; webgl1-probe is ProbeOnly")
        endif()
        set(${output_variable} "webgpu" PARENT_SCOPE)
    else()
        if(NOT "${requested}" STREQUAL "" AND NOT "${requested}" STREQUAL "none")
            message(FATAL_ERROR "RHI-WEB-BUILD-PLATFORM: Web RHI product selection requires Emscripten")
        endif()
        set(${output_variable} "none" PARENT_SCOPE)
    endif()
endfunction()
