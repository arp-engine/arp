function(arp_reflection_generate)
    cmake_parse_arguments(
        ARP_REFLECT
        ""
        "TARGET;MODULE;OUT_CPP;OUT_JSON;MANIFEST"
        "HEADERS"
        ${ARGN}
    )

    if(NOT ARP_REFLECT_TARGET)
        message(FATAL_ERROR "arp_reflection_generate requires TARGET")
    endif()
    if(NOT ARP_REFLECT_MODULE)
        message(FATAL_ERROR "arp_reflection_generate requires MODULE")
    endif()
    if(NOT ARP_REFLECT_OUT_CPP)
        message(FATAL_ERROR "arp_reflection_generate requires OUT_CPP")
    endif()
    if(NOT ARP_REFLECT_OUT_JSON)
        message(FATAL_ERROR "arp_reflection_generate requires OUT_JSON")
    endif()
    if(NOT ARP_REFLECT_HEADERS)
        message(FATAL_ERROR "arp_reflection_generate requires at least one header")
    endif()
    if(NOT TARGET arp_reflect_codegen)
        message(FATAL_ERROR "arp_reflection_generate requires target arp_reflect_codegen")
    endif()

    set(ARP_REFLECT_HEADER_ARGS)
    foreach(ARP_REFLECT_HEADER IN LISTS ARP_REFLECT_HEADERS)
        list(APPEND ARP_REFLECT_HEADER_ARGS --header "${ARP_REFLECT_HEADER}")
    endforeach()

    set(ARP_REFLECT_MANIFEST_DEP)
    if(ARP_REFLECT_MANIFEST)
        set(ARP_REFLECT_MANIFEST_DEP "${ARP_REFLECT_MANIFEST}")
    endif()

    get_filename_component(ARP_REFLECT_OUT_DIR "${ARP_REFLECT_OUT_CPP}" DIRECTORY)
    set(ARP_REFLECT_GENERATED_TARGET
        "${ARP_REFLECT_TARGET}_${ARP_REFLECT_MODULE}_reflection_generated")

    # Cross targets cannot execute the target-format codegen binary and the
    # generator reads headers from the host checkout. Use an explicitly
    # configured host-native tool for both Emscripten and Android (and any
    # future cross toolchain). Empty generated sources are never a valid
    # substitute: reflection participates in the runtime contract, so an
    # unavailable native tool must fail closed at configure time.
    if(CMAKE_CROSSCOMPILING OR EMSCRIPTEN)
        set(ARP_NATIVE_REFLECT_CODEGEN "" CACHE FILEPATH
            "Host-native arp_reflect_codegen executable used by cross builds")
        if(ARP_NATIVE_REFLECT_CODEGEN)
            if(NOT EXISTS "${ARP_NATIVE_REFLECT_CODEGEN}")
                message(FATAL_ERROR
                    "ARP_NATIVE_REFLECT_CODEGEN does not exist: ${ARP_NATIVE_REFLECT_CODEGEN}")
            endif()
            set(_reflect_codegen "${ARP_NATIVE_REFLECT_CODEGEN}")
        else()
            find_program(_reflect_codegen NAMES arp_reflect_codegen.exe arp_reflect_codegen
                PATHS
                    "${CMAKE_CURRENT_SOURCE_DIR}/build/win-vulkan-debug"
                    "${CMAKE_CURRENT_SOURCE_DIR}/build/win-auto-debug"
                    "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/cmake/ninja-mingw-debug"
                    "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/cmake/mingw-debug"
                NO_DEFAULT_PATH
                NO_CACHE
            )
        endif()
        if(NOT _reflect_codegen)
            message(FATAL_ERROR
                "arp_reflection_generate (cross build): native arp_reflect_codegen "
                "not found. Build a native preset first or set ARP_NATIVE_REFLECT_CODEGEN.")
        else()
            add_custom_command(
                OUTPUT "${ARP_REFLECT_OUT_JSON}" "${ARP_REFLECT_OUT_CPP}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARP_REFLECT_OUT_DIR}"
                COMMAND "${_reflect_codegen}"
                    --module "${ARP_REFLECT_MODULE}"
                    --out-json "${ARP_REFLECT_OUT_JSON}"
                    --out-cpp "${ARP_REFLECT_OUT_CPP}"
                    ${ARP_REFLECT_HEADER_ARGS}
                DEPENDS
                    "${_reflect_codegen}"
                    ${ARP_REFLECT_MANIFEST_DEP}
                    ${ARP_REFLECT_HEADERS}
                VERBATIM
            )
        endif()
    else()
        add_custom_command(
            OUTPUT "${ARP_REFLECT_OUT_JSON}" "${ARP_REFLECT_OUT_CPP}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARP_REFLECT_OUT_DIR}"
            COMMAND "$<TARGET_FILE:arp_reflect_codegen>"
                --module "${ARP_REFLECT_MODULE}"
                --out-json "${ARP_REFLECT_OUT_JSON}"
                --out-cpp "${ARP_REFLECT_OUT_CPP}"
                ${ARP_REFLECT_HEADER_ARGS}
                DEPENDS
                arp_reflect_codegen
                ${ARP_REFLECT_MANIFEST_DEP}
                ${ARP_REFLECT_HEADERS}
            VERBATIM
        )
    endif()

    add_custom_target("${ARP_REFLECT_GENERATED_TARGET}"
        DEPENDS
            "${ARP_REFLECT_OUT_JSON}"
            "${ARP_REFLECT_OUT_CPP}"
    )
    add_dependencies("${ARP_REFLECT_TARGET}" "${ARP_REFLECT_GENERATED_TARGET}")
    target_sources("${ARP_REFLECT_TARGET}" PRIVATE "${ARP_REFLECT_OUT_CPP}")
    if(MINGW)
        target_compile_options("${ARP_REFLECT_TARGET}" PRIVATE "-Wa,-mbig-obj")
    endif()

    set("${ARP_REFLECT_MODULE}_REFLECTION_GENERATED_TARGET"
        "${ARP_REFLECT_GENERATED_TARGET}"
        PARENT_SCOPE)
endfunction()
