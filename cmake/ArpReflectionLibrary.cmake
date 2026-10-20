# One original reflection implementation shared by native and Web products.
function(arp_add_reflection_library target core_target yaml_target)
    set(reflection_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/reflection")
    add_library(${target}
        "${reflection_root}/builtin.cpp"
        "${reflection_root}/class_info.cpp"
        "${reflection_root}/db.cpp"
        "${reflection_root}/registration_transaction.cpp"
        "${reflection_root}/enum.cpp"
        "${reflection_root}/function.cpp"
        "${reflection_root}/instance.cpp"
        "${reflection_root}/object.cpp"
        "${reflection_root}/property.cpp"
        "${reflection_root}/serialization.cpp"
        "${reflection_root}/type.cpp"
        "${reflection_root}/variant.cpp"
    )
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")
    target_link_libraries(${target} PUBLIC ${core_target} ${yaml_target})
endfunction()
