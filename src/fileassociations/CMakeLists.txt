# The file types LogSquirl opens and whether it opens them: the list declared
# in cmake/FileTypes.cmake, and one implementation per platform behind one
# interface, which the Options Dialog's File Associations page uses (#720).
add_library(
  logsquirl_fileassociations STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/filetypes.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/fileassociations.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/fileassociationchoice.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/xdgfileassociations.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/macfileassociations.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/windowsfileassociations.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/filetypes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/fileassociations.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/fileassociationchoice.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/xdgfileassociations.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/macfileassociations.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/windowsfileassociations.cpp
  ${LOGSQUIRL_FILE_TYPES_DIR}/file_types.inc
)

set_target_properties(logsquirl_fileassociations PROPERTIES AUTOMOC ON)
target_include_directories(logsquirl_fileassociations PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# The list as cmake/FileTypes.cmake generates it.
target_include_directories(logsquirl_fileassociations PRIVATE "${LOGSQUIRL_FILE_TYPES_DIR}")
# A build without the file types, the AppImage, registers none on Linux, the
# only platform that reads this (#717).
if(LOGSQUIRL_FILE_TYPES)
  target_compile_definitions(logsquirl_fileassociations PRIVATE LOGSQUIRL_REGISTERS_FILE_TYPES=1)
else()
  target_compile_definitions(logsquirl_fileassociations PRIVATE LOGSQUIRL_REGISTERS_FILE_TYPES=0)
endif()
target_link_libraries(logsquirl_fileassociations PUBLIC project_options project_warnings Qt6::Core)
target_link_libraries(logsquirl_fileassociations PRIVATE logsquirl_logging logsquirl_datalocation)

# What talks to the system: the registry and the shell on Windows (#722), and
# NSWorkspace on macOS (#721), in Objective-C++ compiled as C++ with the
# Objective-C++ front end, so the project needs no language of its own for one
# file. The rest is compiled everywhere, so its tests run everywhere.
if(WIN32)
  target_sources(logsquirl_fileassociations PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/windowssystem.cpp)
  target_link_libraries(logsquirl_fileassociations PRIVATE advapi32 shell32)
elseif(APPLE)
  set(_launch_services ${CMAKE_CURRENT_SOURCE_DIR}/src/maclaunchservices.mm)
  set_source_files_properties(${_launch_services} PROPERTIES LANGUAGE CXX COMPILE_OPTIONS
                                                                       "-xobjective-c++;-fobjc-arc")
  target_sources(logsquirl_fileassociations PRIVATE ${_launch_services})
  target_link_libraries(logsquirl_fileassociations PRIVATE "-framework AppKit"
                                                           "-framework UniformTypeIdentifiers")
endif()
