cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(bsbr_undertow VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(undertow
  COMPANY_NAME "bsbr"
  COMPANY_WEBSITE "https://bisbarra.com"
  BUNDLE_ID com.bisbarra.undertow
  PLUGIN_MANUFACTURER_CODE Bsbr
  PLUGIN_CODE Undr
  FORMATS VST3 AU Standalone
  PRODUCT_NAME "undertow"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  COPY_PLUGIN_AFTER_BUILD TRUE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Drum)

juce_add_binary_data(UndertowData SOURCES Resources/ShareTechMono-Regular.ttf Resources/bisbarra-logo.png)
target_sources(undertow PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(undertow PRIVATE src)
target_compile_definitions(undertow PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(undertow PRIVATE UndertowData juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
