cmake_minimum_required(VERSION 3.16)

project(LightTrackPlugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_library(LightTrackPlugin SHARED
  LightTrackPlugin.qrc
  src/core/LayoutSnapshot.h
  src/core/SnapshotHistory.h
  src/core/TimelineTypes.h
  src/core/effects/EffectDescriptor.h
  src/plugin/LightTrackPlugin.h
  src/plugin/LightTrackPlugin.json
  src/plugin/LightTrackPlugin.cpp
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
  src/ui/pages/LightTrackPage.h
  src/ui/pages/LightTrackPagePrivate.h
  src/ui/pages/LightTrackPage.cpp
  src/ui/pages/LightTrackPageLayout.cpp
  src/ui/pages/LightTrackPagePlayback.cpp
  src/ui/pages/LightTrackPageSettings.cpp
  src/integrations/audio/AudioWaveformAnalyzer.h
  src/integrations/audio/AudioWaveformAnalyzer.cpp
  src/integrations/audio/MiniaudioAudioService.h
  src/integrations/audio/MiniaudioAudioService.cpp
  "${MINIAUDIO_DIR}/miniaudio.c"
  src/infrastructure/persistence/LayoutFileRepository.h
  src/infrastructure/persistence/LayoutFileRepository.cpp
  src/integrations/openrgb/OpenRgbEffectCatalog.h
  src/integrations/openrgb/OpenRgbEffectCatalog.cpp
  src/integrations/openrgb/OpenRgbEffectFactory.h
  src/integrations/openrgb/OpenRgbEffectFactory.cpp
  src/integrations/openrgb/OpenRgbEffectsRuntime.h
  src/integrations/openrgb/OpenRgbEffectsRuntime.cpp
  # Thread-safe replacement for the upstream EffectManager.cpp implementation.
  src/integrations/openrgb/OpenRgbEffectManager.cpp
  src/integrations/openrgb/OpenRgbRenderCoordinator.h
  src/integrations/openrgb/OpenRgbRenderCoordinator.cpp
  src/integrations/openrgb/OpenRgbColorRouter.h
  src/integrations/openrgb/OpenRgbColorRouter.cpp
  src/integrations/openrgb/OpenRgbTimelineBackend.h
  src/integrations/openrgb/OpenRgbTimelineBackend.cpp
)

target_include_directories(LightTrackPlugin PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${MINIAUDIO_DIR}"
)
target_compile_definitions(LightTrackPlugin PRIVATE
  MA_NO_ENCODING
)
if(MSVC)
  target_compile_options(LightTrackPlugin PRIVATE /EHsc)
endif()

lighttrack_configure_openrgb_effects(LightTrackPlugin)

option(BUILD_TESTING "Build LightTrack tests" OFF)
if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
