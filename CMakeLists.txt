cmake_minimum_required(VERSION 3.22)

project(PotatoSynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime so the VST3 runs on any Windows 11 machine without extra redistributables
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE: use a local checkout if JUCE_DIR is given, otherwise download it
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Identity is permanent after the first release: never change these codes
juce_add_plugin(PotatoSynth
    COMPANY_NAME "Basspotato"
    COMPANY_WEBSITE "https://github.com/basspotato/potato-synth"
    BUNDLE_ID "com.basspotato.potatosynth"
    PLUGIN_MANUFACTURER_CODE Bpot
    PLUGIN_CODE PtSy
    PRODUCT_NAME "Potato-Synth"
    DESCRIPTION "Huge-by-default layered synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    FORMATS VST3
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PotatoSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(PotatoSynth PRIVATE Source)

target_compile_definitions(PotatoSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# AVX2 baseline on x64; anything wider must be runtime-dispatched
if(MSVC)
    target_compile_options(PotatoSynth PRIVATE /arch:AVX2)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(PotatoSynth PRIVATE -mavx2 -mfma)
endif()

target_link_libraries(PotatoSynth
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_plugin_client
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless developer tools (Linux cloud sessions): render the editor to a PNG
option(POTATO_DEV_TOOLS "Build headless developer tools" OFF)
if(POTATO_DEV_TOOLS)
    # Reuses the plugin's shared-code library (which already contains the JUCE modules)
    add_executable(PotatoSynthSnapshot tools/Snapshot.cpp)
    target_include_directories(PotatoSynthSnapshot PRIVATE $<TARGET_PROPERTY:PotatoSynth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(PotatoSynthSnapshot PRIVATE $<TARGET_PROPERTY:PotatoSynth,COMPILE_DEFINITIONS>)
    target_link_libraries(PotatoSynthSnapshot PRIVATE PotatoSynth)

    add_executable(PotatoSynthTests tests/StackTests.cpp)
    target_include_directories(PotatoSynthTests PRIVATE $<TARGET_PROPERTY:PotatoSynth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(PotatoSynthTests PRIVATE $<TARGET_PROPERTY:PotatoSynth,COMPILE_DEFINITIONS>)
    target_link_libraries(PotatoSynthTests PRIVATE PotatoSynth)

    enable_testing()
    add_test(NAME stack COMMAND PotatoSynthTests)
endif()
