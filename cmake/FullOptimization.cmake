# The flags of a RelWithDebInfo build, rewritten to optimize fully while
# keeping the debug information crash reports are symbolicated with (#280).
#
#   logsquirl_full_optimization_flags(<out_var>
#                                     KIND <COMPILE|LINK>
#                                     MSVC <ON|OFF>
#                                     FLAGS <flags>)
#
# The releases for Windows and Linux are RelWithDebInfo builds: that build type
# is what produces the PDB and the split .debug file Sentry needs. Its CMake
# defaults optimize less than Release does, though: GCC and Clang get -O2
# instead of -O3, MSVC inlines only functions marked inline (/Ob1) and links
# incrementally, which also keeps unreferenced and identical functions.
#
# For GCC and Clang, -O, -O1 and -O2 become -O3; -O3, -Os and -Oz stay. For
# MSVC, /Ob0 and /Ob1 become /Ob2, and the link is non-incremental with
# unreferenced functions removed and identical ones folded, as in Release.
# The debug flags (-g, /Zi, /debug) and the defines are kept as they are.
function(logsquirl_full_optimization_flags out_var)
  cmake_parse_arguments(ARG "" "KIND;MSVC;FLAGS" "" ${ARGN})

  set(flags "${ARG_FLAGS}")
  if(ARG_KIND STREQUAL "COMPILE")
    if(ARG_MSVC)
      if(flags MATCHES "(^| )[/-]Ob[0-2]( |$)")
        string(REGEX REPLACE "(^| )[/-]Ob[01]( |$)" "\\1/Ob2\\2" flags "${flags}")
      else()
        string(APPEND flags " /Ob2")
      endif()
    else()
      string(REGEX REPLACE "(^| )-O[012]?( |$)" "\\1-O3\\2" flags "${flags}")
    endif()
  elseif(ARG_KIND STREQUAL "LINK")
    if(ARG_MSVC)
      string(REGEX REPLACE "(^| )[/-][Ii][Nn][Cc][Rr][Ee][Mm][Ee][Nn][Tt][Aa][Ll](:[Yy][Ee][Ss])?( |$)" "\\1\\3" flags
                           "${flags}"
      )
      string(REGEX REPLACE "  +" " " flags "${flags}")
      string(STRIP "${flags}" flags)
      foreach(flag /INCREMENTAL:NO /OPT:REF /OPT:ICF)
        if(NOT flags MATCHES "(^| )${flag}( |$)")
          string(APPEND flags " ${flag}")
        endif()
      endforeach()
    endif()
  else()
    message(FATAL_ERROR "logsquirl_full_optimization_flags: KIND must be COMPILE or LINK, not '${ARG_KIND}'")
  endif()

  set(${out_var} "${flags}" PARENT_SCOPE)
endfunction()
