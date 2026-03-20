add_library(
  logsquirl_regex STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hsregularexpression.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/regularexpression.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/booleanevaluator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/regularexpressionpattern.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/regularexpression.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/hsregularexpression.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/booleanevaluator.h
)
target_include_directories(logsquirl_regex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_regex
  PUBLIC project_options
         project_warnings
         logsquirl_logging
         logsquirl_settings
         logsquirl_utils
         Qt${QT_VERSION_MAJOR}::Core
         robin_hood
         exprtk
)

if(LOGSQUIRL_USE_HYPERSCAN)
  target_link_libraries(logsquirl_regex PUBLIC logsquirl_hyperscan)
  target_compile_definitions(logsquirl_regex PUBLIC LOGSQUIRL_HAS_HS)
elseif(LOGSQUIRL_USE_VECTORSCAN)
  target_link_libraries(logsquirl_regex PUBLIC logsquirl_vectorscan)
  target_compile_definitions(logsquirl_regex PUBLIC LOGSQUIRL_HAS_HS)
endif()

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_regex PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
