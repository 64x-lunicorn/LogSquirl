# from here:
#
# https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Avai
# lable.md

# logsquirl_project_warnings(<compile_out> <link_out>
#                            COMPILER_ID <CMAKE_CXX_COMPILER_ID>
#                            COMPILER_VERSION <CMAKE_CXX_COMPILER_VERSION>
#                            MSVC <ON|OFF>
#                            AS_ERRORS <ON|OFF>
#                            SANITIZERS <ON|OFF>)
#
# The warnings the project's own code is compiled with, and the ones its link
# step is given. Everything this decides it decides from its arguments, so the
# decisions can be read back in a check (tests/project_warning_flags.cmake)
# without a compiler of that version to hand.
function(logsquirl_project_warnings compile_out link_out)
  cmake_parse_arguments(ARG "" "COMPILER_ID;COMPILER_VERSION;MSVC;AS_ERRORS;SANITIZERS" "" ${ARGN})

  set(MSVC_WARNINGS
      /W4 # Baseline reasonable warnings
      /w14242 # 'identifier': conversion from 'type1' to 'type1', possible loss
              # of data
      /w14254 # 'operator': conversion from 'type1:field_bits' to
              # 'type2:field_bits', possible loss of data
      /w14263 # 'function': member function does not override any base class
              # virtual member function
      /w14265 # 'classname': class has virtual functions, but destructor is not
              # virtual instances of this class may not be destructed correctly
      /w14287 # 'operator': unsigned/negative constant mismatch
      /we4289 # nonstandard extension used: 'variable': loop control variable
              # declared in the for-loop is used outside the for-loop scope
      /w14296 # 'operator': expression is always 'boolean_value'
      /w14311 # 'variable': pointer truncation from 'type1' to 'type2'
      /w14545 # expression before comma evaluates to a function which is missing
              # an argument list
      /w14546 # function call before comma missing argument list
      /w14547 # 'operator': operator before comma has no effect; expected
              # operator with side-effect
      /w14549 # 'operator': operator before comma has no effect; did you intend
              # 'operator'?
      /w14555 # expression has no effect; expected expression with side- effect
      #/w14619 # pragma warning: there is no warning number 'number'
      /w14640 # Enable warning on thread un-safe static member initialization
      /w14826 # Conversion from 'type1' to 'type_2' is sign-extended. This may
              # cause unexpected runtime behavior.
      /w14905 # wide string literal cast to 'LPSTR'
      /w14906 # string literal cast to 'LPWSTR'
      /w14928 # illegal copy-initialization; more than one user-defined
              # conversion has been implicitly applied
      /permissive- # standards conformance mode for MSVC compiler.
      /wd4996 #Your code uses a function, class member, variable, or typedef that's marked deprecated.
      /wd4702 #unreachable code
      /wd4756 #overflow in constant arithmetic -- false positive in catch2
  )

  set(CLANG_WARNINGS
      -Wall
      -Wextra # reasonable and standard
      -Wshadow # warn the user if a variable declaration shadows one from a
               # parent context
      -Wnon-virtual-dtor # warn the user if a class with virtual functions has a
                         # non-virtual destructor. This helps catch hard to
                         # track down memory errors
      #-Wold-style-cast # warn for c-style casts
      -Wcast-align # warn for potential performance problem casts
      -Wunused # warn on anything being unused
      -Woverloaded-virtual # warn if you overload (not override) a virtual
                           # function
      -Wpedantic # warn if non-standard C++ is used
      -Wconversion # warn on type conversions that may lose data
      -Wsign-conversion # warn on sign conversions
      -Wno-null-dereference # warn if a null dereference is detected
      -Wdouble-promotion # warn if float is implicit promoted to double
      -Wformat=2 # warn on security issues around functions that format output
                 # (ie printf)
  )

  # The link step of a build with link time optimization is a second round of
  # code generation, and the warnings it reports never passed through the
  # options above: CMake puts a target's compile options on its compile lines
  # only, so a whole class of GCC diagnostics could appear in a CI log without
  # anything turning red (#454). GCC and Clang carry the warning switches
  # themselves into the intermediate code, so the link line needs no more than
  # the one flag that says what to do with what they report. MSVC is left out
  # on purpose -- see docs/adr/0008-lto-link-diagnostics-fail-the-build.md.
  set(CLANG_LINK_WARNINGS)
  set(GCC_LINK_WARNINGS)
  set(MSVC_LINK_WARNINGS)

  if(ARG_AS_ERRORS)
    set(CLANG_WARNINGS ${CLANG_WARNINGS} -Werror)
    set(MSVC_WARNINGS ${MSVC_WARNINGS} /WX)
    set(CLANG_LINK_WARNINGS ${CLANG_LINK_WARNINGS} -Werror)
    set(GCC_LINK_WARNINGS ${GCC_LINK_WARNINGS} -Werror)
  endif()

  set(GCC_WARNINGS
      ${CLANG_WARNINGS}
      -Wmisleading-indentation # warn if indentation implies blocks where blocks
                               # do not exist
      -Wduplicated-cond # warn if if / else chain has duplicated conditions
      -Wduplicated-branches # warn if if / else branches have duplicated code
      -Wlogical-op # warn about logical operations being used where bitwise were
                   # probably wanted
  )

  if(ARG_SANITIZERS)
    # GCC's -Wmaybe-uninitialized (pulled in by -Wall) false-positives inside
    # inlined library templates once sanitizer instrumentation changes
    # codegen -- seen through QRegularExpression's move-assign (Qt) and
    # libstdc++'s <regex> (pulled in by Catch2). Not a finding about this
    # project's own code, so it stays a warning instead of failing the
    # sanitizer build; ordinary (non-sanitizer) GCC builds keep it as an
    # error. Appended after -Werror (already in GCC_WARNINGS via
    # CLANG_WARNINGS) so it takes priority for this one diagnostic.
    list(APPEND GCC_WARNINGS -Wno-error=maybe-uninitialized)
  endif()

  if(ARG_COMPILER_ID STREQUAL "GNU" AND ARG_COMPILER_VERSION VERSION_LESS 14)
    # GCC 12 and 13 report -Wstringop-overflow from the link step for two
    # setters that write a Policy into a member of a class inheriting more than
    # one base -- LogSquirl's two Presentations both do, because Qt forbids
    # deriving from two QObjects (docs/adr/0003). Inlining across that
    # inheritance loses the pointer adjustment, and GCC then reports "writing 13
    # bytes into a region of size 0 ... at offset -856 into destination object
    # of size 8": the offset is measured from the interface base subobject,
    # which is the bare vptr the "size 8" names, instead of from the complete
    # object. Both writes are a plain assignment of one struct to a member of
    # the same type, so there is nothing to write past. GCC 16 (the fedora job)
    # does not report it. Not a finding about this project's own code, so it
    # stays a warning; the guard takes it back as soon as the oldest GCC
    # LogSquirl builds with reaches 14, and tests/project_warning_flags.cmake
    # holds the guard to that (#454).
    list(APPEND GCC_LINK_WARNINGS -Wno-error=stringop-overflow)
  endif()

  if(ARG_MSVC)
    set(PROJECT_WARNINGS ${MSVC_WARNINGS})
    set(PROJECT_LINK_WARNINGS ${MSVC_LINK_WARNINGS})
  elseif(ARG_COMPILER_ID MATCHES ".*Clang")
    set(PROJECT_WARNINGS ${CLANG_WARNINGS})
    set(PROJECT_LINK_WARNINGS ${CLANG_LINK_WARNINGS})
  elseif(ARG_COMPILER_ID STREQUAL "GNU")
    set(PROJECT_WARNINGS ${GCC_WARNINGS})
    set(PROJECT_LINK_WARNINGS ${GCC_LINK_WARNINGS})
  else()
    message(AUTHOR_WARNING "No compiler warnings set for '${ARG_COMPILER_ID}' compiler.")
    set(PROJECT_WARNINGS)
    set(PROJECT_LINK_WARNINGS)
  endif()

  set(${compile_out} "${PROJECT_WARNINGS}" PARENT_SCOPE)
  set(${link_out} "${PROJECT_LINK_WARNINGS}" PARENT_SCOPE)
endfunction()

function(set_project_warnings project_name)
  option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" TRUE)

  if(MSVC)
    set(_msvc ON)
  else()
    set(_msvc OFF)
  endif()

  if(ENABLE_SANITIZER_ADDRESS
     OR ENABLE_SANITIZER_UNDEFINED_BEHAVIOR
     OR ENABLE_SANITIZER_THREAD
     OR ENABLE_SANITIZER_MEMORY
  )
    set(_sanitizers ON)
  else()
    set(_sanitizers OFF)
  endif()

  logsquirl_project_warnings(
    PROJECT_WARNINGS
    PROJECT_LINK_WARNINGS
    COMPILER_ID
    "${CMAKE_CXX_COMPILER_ID}"
    COMPILER_VERSION
    "${CMAKE_CXX_COMPILER_VERSION}"
    MSVC
    ${_msvc}
    AS_ERRORS
    ${WARNINGS_AS_ERRORS}
    SANITIZERS
    ${_sanitizers}
  )

  target_compile_options(${project_name} INTERFACE ${PROJECT_WARNINGS})
  target_link_options(${project_name} INTERFACE ${PROJECT_LINK_WARNINGS})

endfunction()
