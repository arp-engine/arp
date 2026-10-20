function(arp_add_queen_content)
    set(queen_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    if(NOT TARGET arp_animation)
        add_library(arp_animation STATIC
            "${queen_root}/src/animation/animation.cpp"
            "${queen_root}/src/animation/animator_controller.cpp"
            "${queen_root}/src/animation/blend_tree.cpp"
            "${queen_root}/src/animation/foot_ik.cpp"
            "${queen_root}/src/animation/ik.cpp"
            "${queen_root}/src/animation/pose_blend.cpp"
            "${queen_root}/src/animation/skeleton.cpp"
            "${queen_root}/src/animation/skinned_mesh_player.cpp")
        target_link_libraries(arp_animation PUBLIC arp_core)
    endif()
    add_library(arp_queen_content STATIC
        "${queen_root}/examples/build_a_queen/game.cpp"
        "${queen_root}/examples/build_a_queen/scene_content.cpp"
        "${queen_root}/src/assets/glb_animated_mesh_importer.cpp"
        "${queen_root}/src/assets/glb_static_mesh_importer.cpp"
        "${queen_root}/src/assets/glb_importer_common.cpp"
        "${queen_root}/src/assets/animated_mesh_coordinate_conversion.cpp"
        "${queen_root}/src/assets/tangent_generation.cpp")
    target_include_directories(arp_queen_content PUBLIC "${queen_root}/src" "${queen_root}/examples/build_a_queen"
        "${queen_root}/3rd/fkYAML/include")
    target_link_libraries(arp_queen_content PUBLIC arp_core arp_animation)
    target_compile_features(arp_queen_content PUBLIC cxx_std_17)
endfunction()
