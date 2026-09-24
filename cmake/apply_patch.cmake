# Applies ${PATCH} to the current directory once: skipped when the patch is already in place.
# Used as the dsp56300 PATCH_COMMAND (cmake/dsp56300.cmake), so it must be safe to run again.
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${PATCH}
  RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
if(already_applied EQUAL 0)
  message(STATUS "dsp56300 patch already applied")
  return()
endif()
execute_process(COMMAND ${GIT_EXECUTABLE} apply --whitespace=nowarn ${PATCH} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "could not apply ${PATCH}")
endif()
message(STATUS "applied ${PATCH}")
