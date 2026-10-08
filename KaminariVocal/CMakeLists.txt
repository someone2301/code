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

# Copy the built plug-ins into ~/Library/Audio/Plug-Ins after each build (macOS). The installer turns this off.
if(APPLE)
    option(KV_COPY_AFTER_BUILD "Copy plug-ins to the user plug-in folders after building" ON)
else()
    option(KV_COPY_AFTER_BUILD "Copy plug-ins to the user plug-in folders after building" OFF)
endif()
set(_copy_default ${KV_COPY_AFTER_BUILD})

# Identity. This is the "Alt" edition (original tuner, LA-2A style compressor, basic de-esser, no sends, Flanger or
# Distortion). It has its own name, plug-in code and bundle ID so it installs beside the full Kaminari Vocal
# (Kmni/KmVc, kept at commit 231f676, see docs/KaminariVocal/VERSIONS.md) and both can be used in one session.
set(KV_PRODUCT_NAME "Kaminari Vocal Alt")
set(KV_PLUGIN_CODE  KmVa)
set(KV_BUNDLE_ID    com.kaminariaudio.kaminarivocalalt)

juce_add_plugin(KaminariVocal
    COMPANY_NAME                "Kaminari Audio"
    PRODUCT_NAME                "${KV_PRODUCT_NAME}"
    BUNDLE_ID                   ${KV_BUNDLE_ID}
    PLUGIN_MANUFACTURER_CODE    Kmni
    PLUGIN_CODE                 ${KV_PLUGIN_CODE}
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
    Source/PluginEditor.cpp
    Source/state/PresetManager.cpp)

# factory presets (BinaryData::factory_json) and the Barlow fonts (SIL Open Font License, Resources/fonts/OFL.txt)
juce_add_binary_data(KaminariVocalData SOURCES
    Presets/factory.json
    Resources/fonts/Barlow-Regular.ttf
    Resources/fonts/Barlow-SemiBold.ttf
    Resources/fonts/Barlow-Bold.ttf
    Resources/fonts/BarlowCondensed-Bold.ttf)

target_sources(KaminariVocal PRIVATE ${KV_SOURCES})

set(KV_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    KV_PRODUCT_NAME="${KV_PRODUCT_NAME}"
    KV_EDITION_ALT=1)

target_compile_definitions(KaminariVocal PUBLIC ${KV_DEFINITIONS})

# Same floating-point results on Intel and Apple Silicon: no fused multiply-add contraction (clang fuses by default
# on arm64, which changes the last bit of some results and breaks the bit-exact bypass guarantees).
set(KV_FP_FLAGS $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-ffp-contract=off>)
target_compile_options(KaminariVocal PRIVATE ${KV_FP_FLAGS})

target_link_libraries(KaminariVocal
    PRIVATE
        KaminariVocalData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(KV_BUILD_TESTS)
    juce_add_console_app(KaminariVocalTests PRODUCT_NAME "KaminariVocalTests")
    target_sources(KaminariVocalTests PRIVATE ${KV_SOURCES} Tests/Tests.cpp)
    target_include_directories(KaminariVocalTests PRIVATE Source)
    target_compile_options(KaminariVocalTests PRIVATE ${KV_FP_FLAGS})
    target_compile_definitions(KaminariVocalTests PUBLIC
        ${KV_DEFINITIONS}
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(KaminariVocalTests
        PRIVATE KaminariVocalData juce::juce_audio_utils juce::juce_dsp
        PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
