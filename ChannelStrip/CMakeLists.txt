cmake_minimum_required(VERSION 3.22)

# Intel (x86_64) macOS target. Override with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal build.
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for macOS")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

project(ChannelStrip VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CHANNELSTRIP_BUILD_TESTS "Build the offline DSP/GUI test executable" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(_copy_default TRUE)
else()
    set(_copy_default FALSE)
endif()

juce_add_plugin(ChannelStrip
    COMPANY_NAME                "Local"
    PRODUCT_NAME                "Channel Strip"
    PLUGIN_MANUFACTURER_CODE    Locl
    PLUGIN_CODE                 Chst
    FORMATS                     VST3
    VST3_CATEGORIES             Fx Dynamics EQ
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    COPY_PLUGIN_AFTER_BUILD     ${_copy_default})

set(CS_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_sources(ChannelStrip PRIVATE ${CS_SOURCES})

target_compile_definitions(ChannelStrip PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ChannelStrip
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(CHANNELSTRIP_BUILD_TESTS)
    juce_add_console_app(ChannelStripTests PRODUCT_NAME "ChannelStripTests")
    target_sources(ChannelStripTests PRIVATE ${CS_SOURCES} Tests/Tests.cpp)
    target_include_directories(ChannelStripTests PRIVATE Source)
    target_compile_definitions(ChannelStripTests PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChannelStripTests
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
