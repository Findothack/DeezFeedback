cmake_minimum_required(VERSION 3.22)
project(FeedbackStopper VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is downloaded at configure time (needs internet once). Change GIT_TAG to any JUCE 8 release.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(FeedbackStopper
    COMPANY_NAME "YourName"
    PLUGIN_MANUFACTURER_CODE Fbst
    PLUGIN_CODE Fbs1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Feedback Stopper"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE      # installs the VST3 into your system VST3 folder
    VST3_CATEGORIES Fx)

target_sources(FeedbackStopper PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(FeedbackStopper PRIVATE Source)

target_compile_definitions(FeedbackStopper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FeedbackStopper
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional: native DSP test, no JUCE needed.   cmake -DFBS_BUILD_TESTS=ON ...
option(FBS_BUILD_TESTS "Build the DSP core test" OFF)
if(FBS_BUILD_TESTS)
    add_executable(core_test tests/core_test.cpp)
    target_include_directories(core_test PRIVATE Source)
endif()
