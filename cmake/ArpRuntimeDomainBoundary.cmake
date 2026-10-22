# Export actual CMake source and link properties, including interface edges.
# The validator walks transitive closure; a spelling-only include scan is not
# sufficient to establish that a runtime target can operate without PCG.
function(arp_collect_runtime_domain_targets _directory _output)
    get_property(_targets DIRECTORY "${_directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirectories DIRECTORY "${_directory}" PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        arp_collect_runtime_domain_targets("${_subdirectory}" _child_targets)
        list(APPEND _targets ${_child_targets})
    endforeach()
    set(${_output} ${_targets} PARENT_SCOPE)
endfunction()
arp_collect_runtime_domain_targets("${CMAKE_CURRENT_SOURCE_DIR}" _arp_domain_targets)
set(_arp_domain_manifest "")
foreach(_arp_domain_target IN LISTS _arp_domain_targets)
    get_target_property(_arp_domain_sources ${_arp_domain_target} SOURCES)
    get_target_property(_arp_domain_links ${_arp_domain_target} LINK_LIBRARIES)
    get_target_property(_arp_domain_interface ${_arp_domain_target} INTERFACE_LINK_LIBRARIES)
    string(APPEND _arp_domain_manifest
        "${_arp_domain_target}|${_arp_domain_sources}|${_arp_domain_links};${_arp_domain_interface}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/runtime-domain-targets.txt" "${_arp_domain_manifest}")
add_test(NAME arp_runtime_domain_dependency_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/runtime_domain_dependency_tests.py")
add_test(NAME arp_runtime_domain_target_boundary
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/runtime_domain_dependencies.py"
        --root "${CMAKE_CURRENT_SOURCE_DIR}"
        --targets "${CMAKE_CURRENT_BINARY_DIR}/runtime-domain-targets.txt")
