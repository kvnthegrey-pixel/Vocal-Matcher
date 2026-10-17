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

# Reference analysis code shared by the plugin and the tests (no UI in here).
set(VA_CORE_SOURCES
    Source/NoteSegmenter.cpp
    Source/PyinAnalyser.cpp
    Source/MidiReference.cpp
    Source/ReferenceModel.cpp
    Source/ReferenceJob.cpp
    Source/Matcher.cpp
    Source/EqMatch.cpp)

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
    Source/PitchEngine.h
    Source/NoteBlocks.h
    Source/NoteSegmenter.h
    Source/PyinAnalyser.h
    Source/MidiReference.h
    Source/ReferenceModel.h
    Source/ReferenceJob.h
    Source/Matcher.h Source/TraceStore.h Source/PerformanceSource.h Source/SungNotes.h Source/EqMatch.h Source/ToneSequence.h Source/Auditioner.h
    Source/BlockEditor.h
    Source/BlockEditor.cpp
    Source/EqView.h
    Source/EqView.cpp
    ${VA_CORE_SOURCES})

target_compile_definitions(VocalAssistor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")

target_link_libraries(VocalAssistor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Vocal Chain: second plugin (auto-tune, compressor, de-esser, colour, reverb)
# ---------------------------------------------------------------------------
set(VC_SOURCES
    Source/Chain/ChainProcessor.cpp
    Source/Chain/ChainEditor.cpp)

juce_add_plugin(VocalChain
    COMPANY_NAME                "Vocal Matcher"
    PLUGIN_MANUFACTURER_CODE    VcMt
    PLUGIN_CODE                 VcCh
    FORMATS                     VST3
    PRODUCT_NAME                "Vocal Chain"
    BUNDLE_ID                   com.vocalmatcher.vocalchain
    VST3_CATEGORIES             Fx
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(VocalChain PRIVATE
    ${VC_SOURCES}
    Source/Chain/Dsp.h Source/Chain/Compressor.h Source/Chain/PitchCorrector.h
    Source/Chain/PlateReverb.h Source/Chain/ChainEngine.h Source/Chain/ChainParams.h
    Source/Chain/ChainProcessor.h Source/Chain/ChainEditor.h)

target_compile_definitions(VocalChain PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")

target_link_libraries(VocalChain
    PRIVATE
        juce::juce_audio_utils
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Vocal Stack: third plugin (stacked, hard, upfront vocal texture)
# ---------------------------------------------------------------------------
set(VS_SOURCES
    Source/Stack/StackProcessor.cpp
    Source/Stack/StackEditor.cpp)

juce_add_plugin(VocalStack
    COMPANY_NAME                "Vocal Matcher"
    PLUGIN_MANUFACTURER_CODE    VcMt
    PLUGIN_CODE                 VcSt
    FORMATS                     VST3
    PRODUCT_NAME                "Vocal Stack"
    BUNDLE_ID                   com.vocalmatcher.vocalstack
    VST3_CATEGORIES             Fx
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(VocalStack PRIVATE
    ${VS_SOURCES}
    Source/Stack/StackEngine.h Source/Stack/StackParams.h Source/Stack/StackProcessor.h Source/Stack/StackEditor.h)

target_compile_definitions(VocalStack PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")

target_link_libraries(VocalStack
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Performance Assist: fourth plugin (coaching: your voice as a wave hitting the
# note blocks, scoring, takes, heatmap, comp plan). Separate from Vocal Assistor;
# it shares only the pitch engine and the reference-vocal analysis code.
# ---------------------------------------------------------------------------
set(PA_SOURCES
    Source/Assist/AssistProcessor.cpp
    Source/Assist/AssistEditor.cpp
    Source/Assist/GameView.cpp
    Source/Assist/TakesView.cpp
    Source/BlockEditor.cpp
    ${VA_CORE_SOURCES})

set(PA_HEADERS
    Source/Assist/AssistProcessor.h Source/Assist/AssistEditor.h Source/Assist/GameView.h Source/Assist/TakesView.h
    Source/Assist/ScoreKeeper.h Source/Assist/TakeStore.h Source/Assist/EnvelopeStore.h
    Source/BlockEditor.h Source/PitchEngine.h Source/PitchTracker.h Source/NoteBlocks.h
    Source/ReferenceModel.h Source/ReferenceJob.h Source/Matcher.h Source/TraceStore.h Source/PerformanceSource.h Source/SungNotes.h)

juce_add_plugin(PerformanceAssist
    COMPANY_NAME                "Vocal Matcher"
    PLUGIN_MANUFACTURER_CODE    VcMt
    PLUGIN_CODE                 VcPa
    FORMATS                     VST3
    PRODUCT_NAME                "Performance Assist"
    BUNDLE_ID                   com.vocalmatcher.performanceassist
    VST3_CATEGORIES             Fx Analyzer
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(PerformanceAssist PRIVATE ${PA_SOURCES} ${PA_HEADERS})

target_compile_definitions(PerformanceAssist PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")

target_link_libraries(PerformanceAssist
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Scoring, takes and envelope tests: plain C++, no JUCE.
add_executable(AssistCoreTests tests/AssistCoreTests.cpp
    Source/Assist/ScoreKeeper.h Source/Assist/TakeStore.h Source/Assist/EnvelopeStore.h Source/NoteBlocks.h)

# Headless host test for Performance Assist: the real processor and editor without Cubase.
juce_add_console_app(AssistHostTests PRODUCT_NAME "AssistHostTests")
target_sources(AssistHostTests PRIVATE tests/AssistHostTests.cpp ${PA_SOURCES} ${PA_HEADERS})
target_compile_definitions(AssistHostTests PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="Performance Assist"
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")
target_link_libraries(AssistHostTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
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

# ---------------------------------------------------------------------------
# Unit tests for reference analysis, note blocks, MIDI and saved state (Phase 3).
# ---------------------------------------------------------------------------
juce_add_console_app(BlockTests PRODUCT_NAME "BlockTests")

target_sources(BlockTests PRIVATE tests/BlockTests.cpp ${VA_CORE_SOURCES})

target_compile_definitions(BlockTests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(BlockTests
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless host test (Phase 3): the real processor and editor without Cubase.
# ---------------------------------------------------------------------------
juce_add_console_app(HostTests PRODUCT_NAME "HostTests")

target_sources(HostTests PRIVATE
    tests/HostTests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/BlockEditor.cpp
    Source/EqView.cpp
    ${VA_CORE_SOURCES})

target_compile_definitions(HostTests PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="Vocal Assistor"
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")

target_link_libraries(HostTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Vocal Chain DSP tests (no plugin, no UI).
# ---------------------------------------------------------------------------
juce_add_console_app(ChainTests PRODUCT_NAME "ChainTests")
target_sources(ChainTests PRIVATE tests/ChainTests.cpp)
target_compile_definitions(ChainTests PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ChainTests
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Headless host test for the Vocal Chain plugin (layouts, latency, presets, editor).
juce_add_console_app(ChainHostTests PRODUCT_NAME "ChainHostTests")
target_sources(ChainHostTests PRIVATE tests/ChainHostTests.cpp ${VC_SOURCES})
target_compile_definitions(ChainHostTests PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="Vocal Chain"
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")
target_link_libraries(ChainHostTests
    PRIVATE
        juce::juce_audio_utils
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Vocal Stack DSP + host tests (one console app).
juce_add_console_app(StackTests PRODUCT_NAME "StackTests")
target_sources(StackTests PRIVATE tests/StackTests.cpp ${VS_SOURCES})
target_compile_definitions(StackTests PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="Vocal Stack"
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")
target_link_libraries(StackTests
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Mix Seat: fifth plugin (upload a beat, it nudges the vocal EQ so the voice sits in the
# gaps the beat leaves). Separate from the others; it shares only the spectrum measuring
# and EQ filter code in Source/EqMatch.h and the audio decoder in Source/ReferenceModel.cpp.
# ---------------------------------------------------------------------------
set(MS_SOURCES
    Source/Seat/SeatProcessor.cpp
    Source/Seat/SeatEditor.cpp
    ${VA_CORE_SOURCES})

set(MS_HEADERS
    Source/Seat/SeatProcessor.h Source/Seat/SeatEditor.h Source/Seat/SeatEngine.h
    Source/EqMatch.h Source/ReferenceModel.h)

juce_add_plugin(MixSeat
    COMPANY_NAME                "Vocal Matcher"
    PLUGIN_MANUFACTURER_CODE    VcMt
    PLUGIN_CODE                 VcMs
    FORMATS                     VST3
    PRODUCT_NAME                "Mix Seat"
    BUNDLE_ID                   com.vocalmatcher.mixseat
    VST3_CATEGORIES             Fx EQ
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(MixSeat PRIVATE ${MS_SOURCES} ${MS_HEADERS})

target_compile_definitions(MixSeat PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")

target_link_libraries(MixSeat
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Maths tests for Mix Seat (profiles, the nudge, the plan, reasons, saved text).
juce_add_console_app(SeatCoreTests PRODUCT_NAME "SeatCoreTests")
target_sources(SeatCoreTests PRIVATE tests/SeatCoreTests.cpp ${VA_CORE_SOURCES} Source/Seat/SeatEngine.h)
target_compile_definitions(SeatCoreTests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(SeatCoreTests
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Headless host test for Mix Seat: the real processor and editor without Cubase.
juce_add_console_app(SeatHostTests PRODUCT_NAME "SeatHostTests")
target_sources(SeatHostTests PRIVATE tests/SeatHostTests.cpp ${MS_SOURCES} ${MS_HEADERS})
target_compile_definitions(SeatHostTests PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JucePlugin_Name="Mix Seat"
    VA_VERSION_LABEL="${VA_VERSION_LABEL}")
target_link_libraries(SeatHostTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        pitch_detector
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
