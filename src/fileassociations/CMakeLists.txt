# The file types LogSquirl opens and whether it opens them: the list declared
# in cmake/FileTypes.cmake, and one implementation per platform behind one
# interface, which the Options Dialog's File Associations page uses (#720).
add_library(
  logsquirl_fileassociations STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/filetypes.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/fileassociations.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/xdgfileassociations.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/filetypes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/fileassociations.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/xdgfileassociations.cpp
  ${LOGSQUIRL_FILE_TYPES_DIR}/file_types.inc
)

set_target_properties(logsquirl_fileassociations PROPERTIES AUTOMOC ON)
target_include_directories(logsquirl_fileassociations PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# The list as cmake/FileTypes.cmake generates it.
target_include_directories(logsquirl_fileassociations PRIVATE "${LOGSQUIRL_FILE_TYPES_DIR}")
# A build without the file types, the AppImage, registers none (#717).
if(LOGSQUIRL_FILE_TYPES)
  target_compile_definitions(logsquirl_fileassociations PRIVATE LOGSQUIRL_REGISTERS_FILE_TYPES=1)
else()
  target_compile_definitions(logsquirl_fileassociations PRIVATE LOGSQUIRL_REGISTERS_FILE_TYPES=0)
endif()
target_link_libraries(logsquirl_fileassociations PUBLIC project_options project_warnings Qt6::Core)
target_link_libraries(logsquirl_fileassociations PRIVATE logsquirl_logging)
