cmake_minimum_required(VERSION 3.22)
# The minimum macOS the binaries load on. Set before project(), which would otherwise fix it to the build
# machine's OS version.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
project(Monomodule VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
  add_compile_options(/utf-8 /bigobj)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
endif()

option(MNM_BUILD_PLUGIN "Build the plugins and the Library app (needs JUCE, downloaded at configure time)" ON)
option(MNM_BUILD_TESTS  "Build the unit tests" ON)

# Optional: your Monomachine OS file. Used as the default by the command-line tools and by the tests that run the
# emulated DSP (they are skipped without it). The plugins never use it: they ask for the file at run time.
set(MNM_OS_SYX "" CACHE FILEPATH "Optional: path to Elektron_SFX6-60_OS1.32B.syx for the tests and CLI tools")

include(cmake/dsp56300.cmake)

add_subdirectory(src/core)
add_subdirectory(src/cli)
if(MNM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(MNM_BUILD_PLUGIN)
  add_subdirectory(src/library)
  add_subdirectory(src/plugin)
  add_subdirectory(src/app)
endif()
