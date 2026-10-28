# Run only when building the cloud browser fixture, never during unrelated Web configuration.
if(NOT EXISTS "${cloudAssetTool}")
    message(FATAL_ERROR "Cloud browser fixture requires ARP_CLOUD_ASSET_TOOL_HOST to name a built native arp_cloud_asset_tool")
endif()
execute_process(COMMAND "${cloudAssetTool}" cook "${cloudAssetRecipe}" "${cloudAssetOutput}" 4194304
    RESULT_VARIABLE cloudAssetResult)
if(NOT cloudAssetResult EQUAL 0)
    message(FATAL_ERROR "Cloud browser fixture cook failed: ${cloudAssetResult}")
endif()
