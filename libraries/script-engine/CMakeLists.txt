set(TARGET_NAME script-engine)
# FIXME Move undo scripting interface to application and remove Widgets
setup_hifi_library(Gui Network Script ScriptTools WebSockets Widgets)

target_zlib()

if (NOT ANDROID)

  add_dependency_external_projects(quazip)
  find_package(QuaZip REQUIRED)
  target_include_directories(${TARGET_NAME} SYSTEM PUBLIC ${QUAZIP_INCLUDE_DIRS})
  target_link_libraries(${TARGET_NAME} ${QUAZIP_LIBRARIES})

  if (WIN32)
    add_paths_to_fixup_libs(${QUAZIP_DLL_PATH})
  endif ()

endif ()

link_hifi_libraries(shared networking octree gpu procedural graphics model-networking ktx recording avatars fbx entities controllers animation audio physics image midi)
# ui includes gl, but link_hifi_libraries does not use transitive includes, so gl must be explicit
include_hifi_library_headers(gl)
