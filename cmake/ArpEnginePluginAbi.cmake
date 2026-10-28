# SharedEngineScene3DAbi/1. The engine anchor does not reference every public
# scene3d plugin API. Retain only the first-party semantic owner archives on
# Apple; their ordinary target dependencies still supply implementation links.
if(APPLE AND TARGET arp_engine)
    foreach(plugin_abi_module arp_scene arp_2d arp_3d arp_reflection)
        target_link_options(arp_engine PRIVATE
            "LINKER:-force_load,$<TARGET_FILE:${plugin_abi_module}>")
    endforeach()
endif()
