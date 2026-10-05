cmake_minimum_required(VERSION 3.22)

# Intel (x86_64) macOS target. Override with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal build.
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for macOS")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

project(KaminariVocal VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(KV_BUILD_TESTS "Build the offline test executable" OFF)
option(KV_BUILD_AAX "Add the AAX format (macOS/Windows; needs PACE signing for retail Pro Tools)" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

set(KV_FORMATS VST3)
if(APPLE)
    list(APPEND KV_FORMATS AU)
endif()
if(KV_BUILD_AAX AND (APPLE OR WIN32))
    list(APPEND KV_FORMATS AAX)
endif()

if(APPLE)
    set(_copy_default TRUE)
else()
    set(_copy_default FALSE)
endif()

juce_add_plugin(KaminariVocal
    COMPANY_NAME                "Kaminari Audio"
    PRODUCT_NAME                "Kaminari Vocal"
    BUNDLE_ID                   com.kaminariaudio.kaminarivocal
    PLUGIN_MANUFACTURER_CODE    Kmni
    PLUGIN_CODE                 KmVc
    FORMATS                     ${KV_FORMATS}
    VST3_CATEGORIES             Fx EQ Dynamics "Pitch Shift"
    AU_MAIN_TYPE                kAudioUnitType_Effect
    AAX_CATEGORY                AAX_ePlugInCategory_EQ AAX_ePlugInCategory_Dynamics AAX_ePlugInCategory_PitchShift
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    COPY_PLUGIN_AFTER_BUILD     ${_copy_default})

set(KV_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_sources(KaminariVocal PRIVATE ${KV_SOURCES})

set(KV_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(KaminariVocal PUBLIC ${KV_DEFINITIONS})

target_link_libraries(KaminariVocal
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(KV_BUILD_TESTS)
    juce_add_console_app(KaminariVocalTests PRODUCT_NAME "KaminariVocalTests")
    target_sources(KaminariVocalTests PRIVATE ${KV_SOURCES} Tests/Tests.cpp)
    target_include_directories(KaminariVocalTests PRIVATE Source)
    target_compile_definitions(KaminariVocalTests PUBLIC
        ${KV_DEFINITIONS}
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(KaminariVocalTests
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
