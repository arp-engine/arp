if(NOT DEFINED MANIFEST OR NOT DEFINED SOURCE_DIRECTORY)
    message(FATAL_ERROR
        "AOT source inventory requires MANIFEST and SOURCE_DIRECTORY")
endif()
if(NOT EXISTS "${MANIFEST}" OR NOT IS_DIRECTORY "${SOURCE_DIRECTORY}")
    message(FATAL_ERROR "AOT source inventory input is missing")
endif()

file(STRINGS "${MANIFEST}" manifest_sources
    REGEX "^[ \t]*source:[ \t]*[^#]+")
set(declared_sources)
foreach(line IN LISTS manifest_sources)
    string(REGEX REPLACE "^[ \t]*source:[ \t]*" "" source "${line}")
    string(STRIP "${source}" source)
    if(source STREQUAL "" OR IS_ABSOLUTE "${source}"
        OR source MATCHES "[/\\]")
        message(FATAL_ERROR "AOT manifest source path is invalid: ${source}")
    endif()
    list(APPEND declared_sources "${source}")
endforeach()
list(SORT declared_sources)
list(REMOVE_DUPLICATES declared_sources)

file(GLOB source_files RELATIVE "${SOURCE_DIRECTORY}" "${SOURCE_DIRECTORY}/*.arp")
list(SORT source_files)
if(NOT declared_sources STREQUAL source_files)
    message(FATAL_ERROR
        "AOT production source inventory differs from manifest.\n"
        "Declared: ${declared_sources}\n"
        "On disk: ${source_files}")
endif()

message(STATUS
    "AOT production source inventory passed: ${SOURCE_DIRECTORY} (${source_files})")
