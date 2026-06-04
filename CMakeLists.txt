cmake_minimum_required(VERSION 3.22)
project(SIME VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_gui_app(SIME
    PRODUCT_NAME        "SIME"
    COMPANY_NAME        "Diddy San"
    BUNDLE_ID           "com.diddysan.sime"
    MICROPHONE_PERMISSION_ENABLED  FALSE
    CAMERA_PERMISSION_ENABLED      FALSE
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/Resources/SIME.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/SIME.png"
)

target_sources(SIME PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/SidebarComponent.cpp
    Source/Camera.cpp
    Source/Raycaster.cpp
    Source/Renderer.cpp
    Source/ViewPortComponent.cpp
    Source/AudioEngine.cpp
    Source/SequencerEngine.cpp
    Source/TransportClock.cpp
    Source/Blockeditpopup.cpp
    Source/MuteSchedulePopup.cpp
    Source/SoundSchedulePopup.cpp
    Source/KeyframeEditorPopup.cpp
    Source/CameraPathPopup.cpp
    Source/HelpPopup.cpp
    Source/TransportBarComponent.cpp
    Source/SceneFile.cpp
    Source/SceneAudioExporter.cpp
    Source/ExportAudioDialog.cpp
    Source/StartupMenuComponent.cpp
    Source/TimelineComponent.cpp
    Source/SoundLibrary.cpp
    Source/SoundPickerComponent.cpp
    Source/AudioAnalysis.cpp
    Source/SynthRenderer.cpp
    Source/SynthComponent.cpp
)

target_compile_definitions(SIME PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SIME,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SIME,JUCE_VERSION>"
)

# Force MSVC to read source files as UTF-8 and emit UTF-8 narrow string
# literals.  Without this, multi-byte characters like '·' (U+00B7) or '—'
# (U+2014) embedded in our string literals get reinterpreted as Windows-1252
# at compile time, then re-encoded back into UTF-8 by JUCE -> they render
# on screen as "Â·" / garbage glyphs.  The /utf-8 switch is a no-op on any
# non-MSVC compiler (g++/clang already treat sources as UTF-8 by default).
if (MSVC)
    target_compile_options(SIME PRIVATE /utf-8)
endif()

target_link_libraries(SIME PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_opengl
    juce::juce_audio_basics      # AudioBuffer, FloatVectorOperations
    juce::juce_audio_devices     # AudioDeviceManager, AudioSourcePlayer
    juce::juce_audio_formats     # AudioFormatManager, AudioFormatReader
)

juce_generate_juce_header(SIME)

target_include_directories(SIME PRIVATE Source)