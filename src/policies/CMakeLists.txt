# The Settings Policy types, and nothing else.
#
# Header-only and deliberately its own target: a library that consumes a
# Policy links this, not logsquirl_settings, so reaching for an ambient
# setting from inside it is a link error rather than something a reviewer
# has to catch (#93, #94). deriveSettingsPolicies() is declared here but
# defined in logsquirl_settings -- only the place that derives the
# Policies needs the settings store.
add_library(logsquirl_policies INTERFACE)

target_include_directories(logsquirl_policies INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# RegexpEngine, which the Search Policy names, lives in logsquirl_utils.
target_link_libraries(logsquirl_policies INTERFACE logsquirl_utils)
