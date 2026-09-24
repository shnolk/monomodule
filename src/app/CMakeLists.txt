# Monomodule Library: the standalone library app, and mnm-libtool (terminal front end + test driver).
set(MNM_APP_VIEW_SOURCES LibraryViews.cpp ProjectViews.cpp LibraryComponent.cpp
  ${CMAKE_SOURCE_DIR}/src/plugin/one/Lcd.cpp ${CMAKE_SOURCE_DIR}/src/plugin/one/SkinDialog.cpp ${CMAKE_SOURCE_DIR}/src/plugin/one/SpecData.cpp ${CMAKE_SOURCE_DIR}/src/plugin/one/RomArt.cpp)
set(MNM_APP_INCLUDES ${CMAKE_SOURCE_DIR}/src/plugin ${CMAKE_SOURCE_DIR}/src/plugin/one)
set(MNM_APP_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JucePlugin_VersionString="${PROJECT_VERSION}")

juce_add_gui_app(MnmLibraryApp
  PRODUCT_NAME "Monomodule Library"
  COMPANY_NAME "Shnolk"
  BUNDLE_ID "world.shnolk.mnm.libraryapp"
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS ${MNM_HARDENED_OPTS}
  DOCUMENT_EXTENSIONS syx)
target_sources(MnmLibraryApp PRIVATE LibraryApp.cpp ${MNM_APP_VIEW_SOURCES})
target_include_directories(MnmLibraryApp PRIVATE ${MNM_APP_INCLUDES})
target_compile_definitions(MnmLibraryApp PRIVATE ${MNM_APP_DEFS} JUCE_APPLICATION_NAME_STRING="Monomodule Library" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(MnmLibraryApp PRIVATE mnmlibrary juce::juce_gui_extra juce::juce_audio_basics juce::juce_audio_devices juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(MnmLibTool PRODUCT_NAME "mnm-libtool")
target_sources(MnmLibTool PRIVATE LibTool.cpp ${MNM_APP_VIEW_SOURCES})
target_include_directories(MnmLibTool PRIVATE ${MNM_APP_INCLUDES})
target_compile_definitions(MnmLibTool PRIVATE ${MNM_APP_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)   # playtest pumps the message loop
target_link_libraries(MnmLibTool PRIVATE mnmlibrary juce::juce_gui_basics juce::juce_audio_basics juce::juce_audio_devices juce::juce_recommended_config_flags)
