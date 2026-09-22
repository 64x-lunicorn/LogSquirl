add_library(
  logsquirl_regex STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hsregularexpression.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hyperscanruntime.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/regularexpression.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/booleanevaluator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/regularexpressionpattern.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/regularexpression.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/hsregularexpression.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/hyperscanruntime.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/booleanevaluator.h
)
target_include_directories(logsquirl_regex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_regex
  PUBLIC project_options
         project_warnings
         logsquirl_logging
         logsquirl_utils
         Qt6::Core
         robin_hood
         exprtk
)

if(LOGSQUIRL_USE_VECTORSCAN)
  target_link_libraries(logsquirl_regex PUBLIC logsquirl_vectorscan)
  target_compile_definitions(logsquirl_regex PUBLIC LOGSQUIRL_HAS_HS)
  # The Windows release loads one of two Hyperscan DLLs at run time (#281):
  # both are next to every executable that links the regex library.
  if(TARGET logsquirl_hyperscan_avx2)
    add_dependencies(logsquirl_regex logsquirl_hyperscan_sse42 logsquirl_hyperscan_avx2)
  endif()
endif()
