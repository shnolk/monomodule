# The library layer (store, JSON, MIDI export, preview playback) shared by the standalone app and the plugins. An INTERFACE
# library: each consumer compiles the sources with its own JUCE module set, like the plugin sources.
add_library(mnmlibrary INTERFACE)
target_sources(mnmlibrary INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/Store.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/MidiExport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Transfer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/PreviewPlayer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/LibraryModel.cpp)
target_include_directories(mnmlibrary INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mnmlibrary INTERFACE mnmcore)
