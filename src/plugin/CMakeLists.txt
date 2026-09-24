include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.9
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Copy each built plugin into the user's plugin folders (macOS ~/Library/Audio/Plug-Ins, Linux ~/.vst3). Off by
# default on Windows, where the system VST3 folder needs administrator rights.
if(WIN32)
  option(MNM_COPY_PLUGINS "Copy the plugins to the system plugin folders after building" OFF)
else()
  option(MNM_COPY_PLUGINS "Copy the plugins to the user's plugin folders after building" ON)
endif()

set(MNM_HARDENED_OPTS com.apple.security.cs.allow-jit com.apple.security.cs.allow-unsigned-executable-memory com.apple.security.cs.disable-library-validation)

# Instrument: every machine (synths + FX) in one plugin, FX machines fed from the side-chain input. The UI is drawn
# in the hardware's LCD style; the glyphs and fonts are read from the user's OS file at run time (one/RomArt.*).
juce_add_plugin(MnmOne
  PRODUCT_NAME "Monomodule One"
  COMPANY_NAME "Shnolk"
  BUNDLE_ID "world.shnolk.mnm.one"
  PLUGIN_MANUFACTURER_CODE Shnk
  PLUGIN_CODE Mnm6
  FORMATS AU VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD ${MNM_COPY_PLUGINS}
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS ${MNM_HARDENED_OPTS}
  MICROPHONE_PERMISSION_ENABLED FALSE)

# Monomodule Six: the same processor and editor running six tracks (multitimbral: MIDI channels 1-6 address
# tracks 1-6, one stereo output bus per track, FX machines fed from the previous track or the side-chain).
juce_add_plugin(MnmSix
  PRODUCT_NAME "Monomodule Six"
  COMPANY_NAME "Shnolk"
  BUNDLE_ID "world.shnolk.mnm.six"
  PLUGIN_MANUFACTURER_CODE Shnk
  PLUGIN_CODE Mnm7
  FORMATS AU VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD ${MNM_COPY_PLUGINS}
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS ${MNM_HARDENED_OPTS}
  MICROPHONE_PERMISSION_ENABLED FALSE)

# Monomodule FX: the same processor and editor as an audio effect. One track that offers the FX machines only
# (THRU, REVERB, CHORUS, DYNAMIX, RINGMOD, PHASER, FLANGER) and processes the plugin's main input.
juce_add_plugin(MnmFx
  PRODUCT_NAME "Monomodule FX"
  COMPANY_NAME "Shnolk"
  BUNDLE_ID "world.shnolk.mnm.modulefx"
  PLUGIN_MANUFACTURER_CODE Shnk
  PLUGIN_CODE Mnm8
  FORMATS AU VST3
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD ${MNM_COPY_PLUGINS}
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS ${MNM_HARDENED_OPTS}
  MICROPHONE_PERMISSION_ENABLED FALSE)

set(MNM_ONE_SOURCES one/OneProcessor.cpp one/OneEditor.cpp one/MachinePicker.cpp one/LibraryUi.cpp one/Lcd.cpp one/SkinDialog.cpp one/SpecData.cpp one/RomArt.cpp)
target_sources(MnmOne PRIVATE one/OneFactory.cpp ${MNM_ONE_SOURCES} Overlays.cpp)
target_sources(MnmSix PRIVATE one/SixFactory.cpp ${MNM_ONE_SOURCES} Overlays.cpp)
target_sources(MnmFx PRIVATE one/FxFactory.cpp ${MNM_ONE_SOURCES} Overlays.cpp)
foreach(t MnmOne MnmSix MnmFx)
  target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${t} PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(${t} PRIVATE mnmcore mnmlibrary juce::juce_audio_utils juce::juce_audio_devices
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endforeach()

# Offscreen editor renderer for UI review (a development tool, not installed)
juce_add_console_app(MnmUiSnapshot PRODUCT_NAME "mnm-uisnapshot")
target_sources(MnmUiSnapshot PRIVATE UiSnapshot.cpp Overlays.cpp ${MNM_ONE_SOURCES})
target_include_directories(MnmUiSnapshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(MnmUiSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=0 JucePlugin_VersionString="${PROJECT_VERSION}"
  JucePlugin_Name="Monomodule" JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(MnmUiSnapshot PRIVATE mnmcore mnmlibrary juce::juce_audio_utils juce::juce_recommended_config_flags)
