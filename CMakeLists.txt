cmake_minimum_required(VERSION 3.16)

project(LightTrackPlugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(LIGHTTRACK_MINIMUM_QT5_VERSION 5.10)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
if(QT_VERSION_MAJOR EQUAL 5)
  find_package(
    Qt5 ${LIGHTTRACK_MINIMUM_QT5_VERSION}
    REQUIRED COMPONENTS Core Gui Widgets OpenGL
  )
elseif(QT_VERSION_MAJOR EQUAL 6)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
else()
  message(FATAL_ERROR "LightTrackPlugin requires Qt 5 or Qt 6")
endif()

set(OPENRGB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/OpenRGB)
set(OPENRGB_EFFECTS_PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/OpenRGBEffectsPlugin)
set(MINIAUDIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/miniaudio)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenRGBEffects.cmake)

# Stable value types and header-only rules shared by every project layer.
add_library(lighttrack_core INTERFACE)
add_library(lighttrack::core ALIAS lighttrack_core)
target_include_directories(lighttrack_core INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_link_libraries(lighttrack_core INTERFACE
  "Qt${QT_VERSION_MAJOR}::Core"
  "Qt${QT_VERSION_MAJOR}::Gui"
)

# Project-owned ports. Implementations live in infrastructure/integrations.
add_library(lighttrack_application INTERFACE)
add_library(lighttrack::application ALIAS lighttrack_application)
target_link_libraries(lighttrack_application INTERFACE
  lighttrack_core
)

add_library(lighttrack_timeline STATIC
  src/ui/UiHelpers.h
  src/ui/timeline/TimelineEditor.h
  src/ui/timeline/TimelineEditor.cpp
  src/ui/timeline/TimelineMetrics.h
  src/ui/timeline/internal/TimelineEditorInternal.h
  src/ui/timeline/internal/TimelineEditorCommon.cpp
  src/ui/timeline/internal/TimelineEditorGraphicsItems.cpp
  src/ui/timeline/internal/TimelineEditorScene.cpp
  src/ui/timeline/internal/TimelineEditorPanels.cpp
  src/ui/timeline/internal/TimelineEditorView.cpp
)
add_library(lighttrack::timeline ALIAS lighttrack_timeline)
target_link_libraries(lighttrack_timeline PUBLIC
  lighttrack_core
  "Qt${QT_VERSION_MAJOR}::Widgets"
)
set_target_properties(lighttrack_timeline PROPERTIES
  AUTOMOC ON
  POSITION_INDEPENDENT_CODE ON
)

add_library(lighttrack_page STATIC
  src/ui/pages/LightTrackPage.h
  src/ui/pages/LightTrackPagePrivate.h
  src/ui/pages/LightTrackPage.cpp
  src/ui/pages/LightTrackPageLayout.cpp
  src/ui/pages/LightTrackPagePlayback.cpp
  src/ui/pages/LightTrackPageSettings.cpp
)
add_library(lighttrack::page ALIAS lighttrack_page)
target_link_libraries(lighttrack_page
  PUBLIC
    lighttrack_application
    "Qt${QT_VERSION_MAJOR}::Widgets"
  PRIVATE
    lighttrack_timeline
)
set_target_properties(lighttrack_page PROPERTIES
  AUTOMOC ON
  POSITION_INDEPENDENT_CODE ON
)

# miniaudio.c is compiled exactly once here. No miniaudio type escapes this target.
add_library(lighttrack_audio STATIC
  src/integrations/audio/AudioWaveformAnalyzer.h
  src/integrations/audio/AudioWaveformAnalyzer.cpp
  src/integrations/audio/MiniaudioAudioService.h
  src/integrations/audio/MiniaudioAudioService.cpp
  "${MINIAUDIO_DIR}/miniaudio.c"
)
add_library(lighttrack::audio ALIAS lighttrack_audio)
target_include_directories(lighttrack_audio PRIVATE
  "${MINIAUDIO_DIR}"
)
target_compile_definitions(lighttrack_audio PRIVATE
  MA_NO_ENCODING
)
target_link_libraries(lighttrack_audio
  PUBLIC
    lighttrack_application
  PRIVATE
    "Qt${QT_VERSION_MAJOR}::Core"
)
set_target_properties(lighttrack_audio PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
if(WIN32)
  target_link_libraries(lighttrack_audio PRIVATE
    winmm
    ole32
  )
endif()

add_library(lighttrack_persistence STATIC
  src/infrastructure/persistence/LayoutFileRepository.h
  src/infrastructure/persistence/LayoutFileRepository.cpp
)
add_library(lighttrack::persistence ALIAS lighttrack_persistence)
target_include_directories(lighttrack_persistence PRIVATE
  "${OPENRGB_DIR}/dependencies/json"
)
target_link_libraries(lighttrack_persistence
  PUBLIC
    lighttrack_application
  PRIVATE
    "Qt${QT_VERSION_MAJOR}::Core"
)
set_target_properties(lighttrack_persistence PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

# Object linkage is intentional: every effect translation unit contains a
# static registrar and must reach the final plugin without archive stripping.
add_library(lighttrack_openrgb OBJECT
  src/integrations/openrgb/OpenRgbEffectCatalog.h
  src/integrations/openrgb/OpenRgbEffectCatalog.cpp
  src/integrations/openrgb/OpenRgbEffectFactory.h
  src/integrations/openrgb/OpenRgbEffectFactory.cpp
  src/integrations/openrgb/OpenRgbEffectsRuntime.h
  src/integrations/openrgb/OpenRgbEffectsRuntime.cpp
  src/integrations/openrgb/OpenRgbTimelineBackend.h
  src/integrations/openrgb/OpenRgbTimelineBackend.cpp
)
add_library(lighttrack::openrgb ALIAS lighttrack_openrgb)
target_link_libraries(lighttrack_openrgb PUBLIC
  lighttrack_application
)
lighttrack_configure_openrgb_effects(
  TARGET lighttrack_openrgb
  OPENRGB_DIR ${OPENRGB_DIR}
  OPENRGB_EFFECTS_PLUGIN_DIR ${OPENRGB_EFFECTS_PLUGIN_DIR}
)

# Keep the host ABI adapter and project resource entry in the final DLL only.
add_library(LightTrackPlugin SHARED
  LightTrackPlugin.qrc
  src/plugin/LightTrackPlugin.h
  src/plugin/LightTrackPlugin.cpp
)
target_link_libraries(LightTrackPlugin PRIVATE
  lighttrack_application
  lighttrack_page
  lighttrack_audio
  lighttrack_persistence
  lighttrack_openrgb
  "Qt${QT_VERSION_MAJOR}::Widgets"
)
set_target_properties(LightTrackPlugin PROPERTIES
  AUTOMOC ON
  AUTORCC ON
)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
