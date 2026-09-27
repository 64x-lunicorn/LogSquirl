# The text Encodings a Log File can be read with, and nothing else.
#
# Its own library below both the log data and the settings: the log data
# reads Log Files with them, and deriving the Settings Policies makes the
# default Encoding one this build knows (#552). Neither the settings nor
# what links them has to bring the engine along for that.
add_library(
  logsquirl_textencoding STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/textencoding.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/textencoding.cpp
)

target_include_directories(logsquirl_textencoding PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(logsquirl_textencoding PUBLIC project_options project_warnings Qt6::Core)

# Qt for macOS has neither ICU nor iconv, so the Encodings Qt5Compat used to
# bring along are converted with the iconv of the system (#442).
if(APPLE)
  target_sources(
    logsquirl_textencoding PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/iconvconverter.h
                                   ${CMAKE_CURRENT_SOURCE_DIR}/src/iconvconverter.cpp)
  target_link_libraries(logsquirl_textencoding PRIVATE iconv)
endif()
