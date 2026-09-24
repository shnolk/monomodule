# dsp56300 DSP emulator (GPLv3, https://github.com/dsp56300/dsp56300).
#
# CMake downloads the emulator at configure time, pinned to the exact commit that ext/patches was made
# against, and applies the patch once. Point MNM_DSP56300_DIR at an existing (already patched) checkout
# to build offline instead.
#
# Only the emulator's static libraries are built: the emulator's own top-level CMake project adds tools and
# global optimisation flags that this project does not want.
set(MNM_DSP56300_COMMIT "c051afad31612c2d2c7a81a7ab23e1c5ac9e61af")
set(MNM_DSP56300_DIR "" CACHE PATH "Optional: an existing, already patched dsp56300 checkout (skips the download)")

if(MNM_DSP56300_DIR)
  set(_dsp_src "${MNM_DSP56300_DIR}")
else()
  include(FetchContent)
  FetchContent_Declare(dsp56300
    GIT_REPOSITORY https://github.com/dsp56300/dsp56300.git
    GIT_TAG        ${MNM_DSP56300_COMMIT}
    GIT_SUBMODULES source/asmjit
    PATCH_COMMAND  ${CMAKE_COMMAND} -DPATCH=${CMAKE_SOURCE_DIR}/ext/patches/0001-dsp56300-mnm.patch -P ${CMAKE_SOURCE_DIR}/cmake/apply_patch.cmake
    SOURCE_SUBDIR  _no_top_level_project)   # populate only; the libraries are added below
  FetchContent_MakeAvailable(dsp56300)
  set(_dsp_src "${dsp56300_SOURCE_DIR}")
endif()

if(NOT EXISTS "${_dsp_src}/source/dsp56kEmu/dsp.h")
  message(FATAL_ERROR "dsp56300 not found at ${_dsp_src}")
endif()

set(ASMJIT_STATIC TRUE)
set(ASMJIT_NO_INSTALL TRUE)
add_subdirectory("${_dsp_src}/source/asmjit"     "${CMAKE_BINARY_DIR}/dsp56300/asmjit"     EXCLUDE_FROM_ALL)
add_subdirectory("${_dsp_src}/source/dsp56kBase" "${CMAKE_BINARY_DIR}/dsp56300/dsp56kBase" EXCLUDE_FROM_ALL)
add_subdirectory("${_dsp_src}/source/dsp56kEmu"  "${CMAKE_BINARY_DIR}/dsp56300/dsp56kEmu"  EXCLUDE_FROM_ALL)
set(_dsp_libs asmjit dsp56kBase dsp56kEmu)
if(WIN32 OR (UNIX AND NOT APPLE))   # dsp56kEmu links Intel's JIT profiling API there, as dsp56300's own build does
  add_subdirectory("${_dsp_src}/source/vtuneSdk" "${CMAKE_BINARY_DIR}/dsp56300/vtuneSdk" EXCLUDE_FROM_ALL)
  list(APPEND _dsp_libs vtuneSdk)
endif()
foreach(t ${_dsp_libs})
  set_target_properties(${t} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W0)   # third-party warnings
  else()
    target_compile_options(${t} PRIVATE -w)
  endif()
endforeach()
