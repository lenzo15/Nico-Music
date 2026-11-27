set(provider_out "${CMAKE_CURRENT_BINARY_DIR}")
if(CMAKE_CONFIGURATION_TYPES)
  string(APPEND provider_out "/$<CONFIG>")
endif()
add_custom_target(copy_source_plugins
  COMMAND ${CMAKE_COMMAND} -E make_directory "${provider_out}/plugins"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_LIST_DIR}/bundled-plugins.tsv" "${provider_out}/plugins" VERBATIM)
# Use the same package list as the updater; do not package build-system files at the root.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/bundled-plugins.tsv")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/bundled-plugins.tsv" bundles REGEX "^[a-z0-9]")
foreach(bundle IN LISTS bundles)
  string(REGEX MATCH "^[a-z0-9-]+" id "${bundle}")
  add_custom_command(TARGET copy_source_plugins POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_LIST_DIR}/${id}" "${provider_out}/plugins/${id}" VERBATIM)
endforeach()
