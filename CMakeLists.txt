cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(SubGuard VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ----------------------------------------------------------------- engine
# Portable analysis engine, no dependencies.
add_library(subguard_core STATIC core/LfeEngine.cpp core/TransientDetector.cpp core/PlaybackSim.cpp core/AnalysisRunner.cpp core/RoomSim.cpp core/RoomPro.cpp core/TestSignals.cpp core/Timecode.cpp core/EventLog.cpp core/OfflineAnalysis.cpp core/DeliverableChecks.cpp)
target_include_directories(subguard_core PUBLIC core)
find_package(Threads REQUIRED)
target_link_libraries(subguard_core PUBLIC Threads::Threads)

enable_testing()
add_executable(engine_tests tests/EngineTests.cpp)
target_link_libraries(engine_tests PRIVATE subguard_core)
add_test(NAME engine_tests COMMAND engine_tests)

# Writes the 5.1 test files and what the engine reads on each of them.
add_executable(subguard_testfiles tools/TestFiles.cpp)
target_link_libraries(subguard_testfiles PRIVATE subguard_core)

# -------------------------------------------------------------------- app
option(SUBGUARD_BUILD_APP "Build the application (needs JUCE)" ON)

if(SUBGUARD_BUILD_APP)
    # Use a local JUCE checkout with -DSUBGUARD_JUCE_PATH=..., else download it.
    set(SUBGUARD_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout")

    if(SUBGUARD_JUCE_PATH)
        add_subdirectory("${SUBGUARD_JUCE_PATH}" juce EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.9
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(juce)
    endif()

    # Optional artwork: drop the files in assets/ and they are built in.
    set(icon_args "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
        set(icon_args ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
    endif()

    juce_add_gui_app(SubGuard
        PRODUCT_NAME "SubGuard"
        COMPANY_NAME "SubGuard"
        BUNDLE_ID "com.subguard.analyzer"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "SubGuard needs access to the audio input to analyze low frequencies."
        ${icon_args})

    target_sources(SubGuard PRIVATE
        app/Main.cpp
        app/MainComponent.cpp
        app/Widgets.cpp
        app/RoomProView.cpp
        app/TestView.cpp
        app/LogView.cpp
        app/Lang.cpp)

    target_compile_definitions(SubGuard PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SubGuard,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SubGuard,JUCE_VERSION>")

    target_link_libraries(SubGuard
        PRIVATE
            subguard_core
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/logo.png")
        juce_add_binary_data(SubGuardAssets
            HEADER_NAME SubGuardAssets.h
            NAMESPACE SubGuardAssets
            SOURCES assets/logo.png)
        target_link_libraries(SubGuard PRIVATE SubGuardAssets)
        target_compile_definitions(SubGuard PRIVATE SUBGUARD_HAS_LOGO=1)
    endif()
endif()
