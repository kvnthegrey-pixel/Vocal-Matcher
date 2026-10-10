cmake_minimum_required(VERSION 3.22)

project(VocalAssistor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MSVC runtime so the plugin does not depend on a redistributable being
# installed on the machine running Cubase.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Label shown in the plugin window. Release builds pass the tag, for example
# -DVA_VERSION_LABEL=0.1.0-alpha
set(VA_VERSION_LABEL "${PROJECT_VERSION}-dev" CACHE STRING "Version label shown in the plugin window")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# Vendored third-party JUCE modules (see third_party/NOTICE.txt).
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/third_party/audio_fft)
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/third_party/pitch_detector)

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
juce_add_plugin(VocalAssistor
    COMPANY_NAME                "Vocal Matcher"
    PLUGIN_MANUFACTURER_CODE    VcMt
    PLUGIN_CODE                 VcAs
    FORMATS                     VST3
    PRODUCT_NAME                "Vocal Assistor"
    BUNDLE_ID                   com.vocalmatcher.vocalassistor
    VST3_CATEGORIES             Fx Analyzer
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(VocalAssistor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PitchTracker.h
    Source/PitchEngine.h)

target_compile_definitions(VocalAssistor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")

target_link_libraries(VocalAssistor
    PRIVATE
        juce::juce_audio_utils
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Unit tests for the pitch tracker (Phase 2). Not part of the plugin.
# ---------------------------------------------------------------------------
juce_add_console_app(PitchTests PRODUCT_NAME "PitchTests")

target_sources(PitchTests PRIVATE tests/PitchTests.cpp Source/PitchTracker.h Source/PitchEngine.h)

target_compile_definitions(PitchTests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(PitchTests
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
