# Resets the dsp56300 checkout to its pinned commit and applies ${PATCH}. Used as the dsp56300 PATCH_COMMAND
# (cmake/dsp56300.cmake), which reruns whenever the patch file changes (its hash is part of the command), so the
# checkout always matches the patch exactly. Only touches the checkout CMake downloaded, never MNM_DSP56300_DIR.
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} checkout -- source RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "could not reset the dsp56300 checkout")
endif()
execute_process(COMMAND ${GIT_EXECUTABLE} apply --whitespace=nowarn ${PATCH} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "could not apply ${PATCH}")
endif()
message(STATUS "applied ${PATCH}")
