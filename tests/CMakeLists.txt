# Unit tests. The engine, preview and LCD-artwork tests run the emulated DSP and need a Monomachine OS file;
# without one (MNM_OS environment variable or -DMNM_OS_SYX=...) they are reported as skipped.
add_executable(mnm-tests
  mini_test.cpp
  test_engine.cpp
  test_hostmodel.cpp
  test_lfo.cpp
  test_preview.cpp
  test_uispec.cpp
  ${CMAKE_SOURCE_DIR}/src/plugin/one/SpecData.cpp
  ${CMAKE_SOURCE_DIR}/src/plugin/one/RomArt.cpp
)
target_link_libraries(mnm-tests PRIVATE mnmcore)
target_include_directories(mnm-tests PRIVATE ${CMAKE_SOURCE_DIR}/src/plugin/one)
add_test(NAME mnm-tests COMMAND mnm-tests)
