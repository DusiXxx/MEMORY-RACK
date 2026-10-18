cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS")
project(RackMemory VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RM_BUILD_TESTS "Build offline tests" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(RM_FORMATS AU)
else()
    set(RM_FORMATS VST3)
endif()

juce_add_plugin(RackMemory
    COMPANY_NAME "DusiX"
    BUNDLE_ID "com.dusix.rackmemory"
    PLUGIN_MANUFACTURER_CODE Dusx
    PLUGIN_CODE Rkmm
    FORMATS ${RM_FORMATS}
    PRODUCT_NAME "Rack Memory"
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${APPLE})

file(GLOB RM_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*")
juce_add_binary_data(RackMemoryAssets SOURCES ${RM_ASSETS})
set_target_properties(RackMemoryAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(RackMemory PRIVATE
    Source/Model.cpp
    Source/Profiles.cpp
    Source/Recognizer.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Updater.cpp
    Source/EditorParts.cpp)

# odkud se stahuji aktualizace (GitHub ucet/repozitar, verejny)
set(RM_UPDATE_REPO "DusiXxx/MEMORY-RACK" CACHE STRING "GitHub repozitar s vydanimi (ucet/repozitar)")
target_compile_definitions(RackMemory PUBLIC
    RM_UPDATE_REPO="${RM_UPDATE_REPO}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(RackMemory
    PRIVATE
        RackMemoryAssets
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags)

if(RM_BUILD_TESTS)
    add_executable(RackMemoryTests Tests/Tests.cpp)
    target_include_directories(RackMemoryTests PRIVATE Source $<TARGET_PROPERTY:RackMemory,INCLUDE_DIRECTORIES>)
    target_compile_definitions(RackMemoryTests PRIVATE $<TARGET_PROPERTY:RackMemory,COMPILE_DEFINITIONS>)
    target_compile_definitions(RackMemory PUBLIC JUCE_MODAL_LOOPS_PERMITTED=1)   # jen v testovacim buildu
    target_link_libraries(RackMemoryTests PRIVATE RackMemory RackMemoryAssets)
endif()
