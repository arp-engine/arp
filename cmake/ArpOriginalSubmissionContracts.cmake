# Shared host contracts for the original submission prerequisites.
# Root and build.py-generated projects consume this same registration.
include_guard(GLOBAL)

get_filename_component(_arp_original_submission_root "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)

add_executable(arp_rhi_original_submission_read_tests
    "${CMAKE_CURRENT_LIST_DIR}/../tests/contract/rhi_original_submission_read_tests.cpp")
target_include_directories(arp_rhi_original_submission_read_tests PRIVATE
    "${_arp_original_submission_root}/." "${_arp_original_submission_root}/src")
target_link_libraries(arp_rhi_original_submission_read_tests PRIVATE arp_rhi_semantics)
if(MSVC)
    target_compile_options(arp_rhi_original_submission_read_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_rhi_original_submission_read_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME arp_rhi_original_submission_read_contract
    COMMAND arp_rhi_original_submission_read_tests)
add_test(NAME arp_rhi_original_submission_read_validator_smoke
    COMMAND arp_rhi_original_submission_read_tests --validator-smoke)
set_tests_properties(arp_rhi_original_submission_read_contract
    arp_rhi_original_submission_read_validator_smoke PROPERTIES TIMEOUT 30)

add_executable(arp_rhi_original_submission_completion_tests
    "${CMAKE_CURRENT_LIST_DIR}/../tests/contract/rhi_original_submission_completion_tests.cpp")
target_include_directories(arp_rhi_original_submission_completion_tests PRIVATE
    "${_arp_original_submission_root}/." "${_arp_original_submission_root}/src")
target_link_libraries(arp_rhi_original_submission_completion_tests PRIVATE arp_rhi arp_rhi_semantics)
if(MSVC)
    target_compile_options(arp_rhi_original_submission_completion_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_rhi_original_submission_completion_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME arp_rhi_original_submission_completion_contract
    COMMAND arp_rhi_original_submission_completion_tests)
add_test(NAME arp_rhi_original_submission_completion_null_smoke
    COMMAND arp_rhi_original_submission_completion_tests --null-smoke)
add_test(NAME arp_rhi_original_submission_completion_retirement_process
    COMMAND "${Python3_EXECUTABLE}"
        "${_arp_original_submission_root}/tests/contract/original_submission_retirement_process_test.py"
        "$<TARGET_FILE:arp_rhi_original_submission_completion_tests>")
set_tests_properties(arp_rhi_original_submission_completion_contract
    arp_rhi_original_submission_completion_null_smoke
    arp_rhi_original_submission_completion_retirement_process PROPERTIES TIMEOUT 30)

unset(_arp_original_submission_root)
