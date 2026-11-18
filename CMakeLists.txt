cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) and a minimum macOS version.
# These must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

project(SNSynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is fetched at configure time. To use a local checkout instead, pass
# -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SNSynth
    PRODUCT_NAME                "SN Synth"
    COMPANY_NAME                "SN Audio"
    BUNDLE_ID                   "com.snaudio.snsynth"
    PLUGIN_MANUFACTURER_CODE    Snau
    PLUGIN_CODE                 Sn01
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(SNSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Visualisers.cpp)

target_include_directories(SNSynth PRIVATE Source)

target_compile_definitions(SNSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SNSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline checks (not shipped). Build with -DSN_BUILD_TESTS=ON
option(SN_BUILD_TESTS "Build the offline render test" OFF)
if(SN_BUILD_TESTS)
    add_executable(SNSynthTests tests/RenderTest.cpp)
    # Reuse the plugin's compiled code, include paths and settings.
    target_include_directories(SNSynthTests PRIVATE
        $<TARGET_PROPERTY:SNSynth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(SNSynthTests PRIVATE
        $<TARGET_PROPERTY:SNSynth,COMPILE_DEFINITIONS>)
    target_link_libraries(SNSynthTests PRIVATE SNSynth)
endif()
