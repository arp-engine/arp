if(NOT DEFINED AOTC OR NOT DEFINED MANIFEST OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR
        "AOT deterministic output check requires AOTC, MANIFEST and OUTPUT_DIR")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
execute_process(
    COMMAND "${AOTC}" package "${MANIFEST}" --output "${OUTPUT_DIR}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR
        "first AOT package generation failed: ${first_output}${first_error}")
endif()

file(GLOB_RECURSE generated_files LIST_DIRECTORIES FALSE "${OUTPUT_DIR}/*")
if(NOT generated_files)
    message(FATAL_ERROR "AOT package generation produced no files")
endif()
foreach(generated_file IN LISTS generated_files)
    file(TIMESTAMP "${generated_file}" timestamp_before "%s" UTC)
    file(SHA256 "${generated_file}" hash_before)
    string(SHA256 file_key "${generated_file}")
    set("timestamp_${file_key}" "${timestamp_before}")
    set("hash_${file_key}" "${hash_before}")
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
execute_process(
    COMMAND "${AOTC}" package "${MANIFEST}" --output "${OUTPUT_DIR}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR
        "second AOT package generation failed: ${second_output}${second_error}")
endif()

foreach(generated_file IN LISTS generated_files)
    file(TIMESTAMP "${generated_file}" timestamp_after "%s" UTC)
    file(SHA256 "${generated_file}" hash_after)
    string(SHA256 file_key "${generated_file}")
    if(NOT hash_after STREQUAL "${hash_${file_key}}")
        message(FATAL_ERROR
            "AOT output content changed across identical builds: ${generated_file}")
    endif()
    if(NOT timestamp_after STREQUAL "${timestamp_${file_key}}")
        message(FATAL_ERROR
            "AOT output timestamp changed despite identical content: ${generated_file}")
    endif()
endforeach()
