add_library(mnmcore STATIC
  firmware/Firmware.cpp
  dsp/DspEngine.cpp
  dsp/Digibank.cpp
  host/HostModel.cpp
  host/Lfo.cpp
  MonoVoice.cpp
  util/Wav.cpp
  library/MnmDump.cpp
  library/MnmEncode.cpp
  library/Catalog.cpp
  library/Project.cpp
  preview/Sequencer.cpp
  preview/KitRenderer.cpp
  preview/Preview.cpp
)
target_include_directories(mnmcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mnmcore PUBLIC dsp56kEmu)
target_compile_definitions(mnmcore PUBLIC MNM_OS_SYX="${MNM_OS_SYX}")
if(NOT MSVC)
  target_compile_options(mnmcore PRIVATE -Wall -Wextra)
endif()
