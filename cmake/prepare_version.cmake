# Everything a build needs to know about its version: the version itself,
# which cmake/project_version.cmake derives (#372), and the Windows resource
# and the generated header that carry it into the binaries.
#
# The resource names the product PRODUCT_DISPLAY_NAME ("LogSquirl") in
# ProductName, and packaging/windows/logsquirl.nsi gives the installer the same
# name, version, company and copyright: code signing through the SignPath
# Foundation wants every signed binary's product name to be the project's name
# (#445). PROJECT_DESCRIPTION stays the packages' summary line.

include(project_version)

include_directories(${CMAKE_BINARY_DIR}/generated)

include(win32_rc)

generate_product_version(
  ProductVersionResourceFiles
  NAME
  "${PRODUCT_DISPLAY_NAME}"
  BUNDLE
  "${PRODUCT_DISPLAY_NAME}"
  FILE_DESCRIPTION
  "${PRODUCT_DISPLAY_NAME} log viewer"
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
          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_info.nsh.in
)
