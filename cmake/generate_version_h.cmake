# Get the current working branch
execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message("Git branch: ${GIT_BRANCH}")

# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message("Git commit: ${GIT_COMMIT_HASH}")

# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND git describe --always
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

message("Git describe: ${GIT_DESCRIBE}")

string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)

file(WRITE generated/version.h "#ifndef GENERATED_LOGSQUIRL_VERSION_H\n")
file(APPEND generated/version.h "#define GENERATED_LOGSQUIRL_VERSION_H\n\n")

file(APPEND generated/version.h "#define LOGSQUIRL_DATE \"${BUILD_DATE}\"\n\n")
file(APPEND generated/version.h "#define LOGSQUIRL_GIT_VERSION \"${GIT_DESCRIBE}\"\n\n")
file(APPEND generated/version.h "#define LOGSQUIRL_COMMIT \"${GIT_COMMIT_HASH}\"\n\n")
file(APPEND generated/version.h "#define LOGSQUIRL_VERSION \"${BUILD_VERSION}\"\n\n")

file(APPEND generated/version.h "#endif // GENERATED_LOGSQUIRL_VERSION_H\n")
