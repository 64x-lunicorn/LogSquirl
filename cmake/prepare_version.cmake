# Everything a build needs to know about its version: the version itself,
# which cmake/project_version.cmake derives (#372), and the Windows resource
# and the generated header that carry it into the binaries.

include(project_version)

include_directories(${CMAKE_BINARY_DIR}/generated)

include(win32_rc)

generate_product_version(
  ProductVersionResourceFiles
  NAME
  "${PROJECT_DESCRIPTION}"
  ORIGINAL_FILENAME
  ${PROJECT_NAME}
  ICON
  "${ICON_FILE}"
  VERSION_MAJOR
  ${PROJECT_VERSION_MAJOR}
  VERSION_MINOR
  ${PROJECT_VERSION_MINOR}
  VERSION_PATCH
  ${PROJECT_VERSION_PATCH}
  VERSION_REVISION
  ${PROJECT_VERSION_TWEAK}
  COMPANY_NAME
  ${COMPANY}
  COMPANY_COPYRIGHT
  ${COPYRIGHT}
)

add_custom_target(
  generate_version ALL
  COMMAND ${CMAKE_COMMAND} -DBUILD_VERSION=${PROJECT_VERSION}.${PROJECT_VERSION_TWEAK} -P
          ${CMAKE_SOURCE_DIR}/cmake/generate_version_h.cmake
  DEPENDS ${ProductVersionResourceFiles}
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_version_h.cmake
          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_info.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_resource.rc.in
)
