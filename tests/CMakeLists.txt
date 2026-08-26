add_executable(lighttrack_history_tests
  SnapshotHistoryTests.cpp
)
target_include_directories(lighttrack_history_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
)
add_test(
  NAME lighttrack.snapshot_history
  COMMAND lighttrack_history_tests
)

add_executable(lighttrack_persistence_tests
  LayoutFileRepositoryTests.cpp
  "${PROJECT_SOURCE_DIR}/src/infrastructure/persistence/LayoutFileRepository.cpp"
)
target_include_directories(lighttrack_persistence_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
  "${OPENRGB_DIR}/dependencies/json"
)
target_link_libraries(lighttrack_persistence_tests PRIVATE
  "Qt${QT_VERSION_MAJOR}::Core"
)

add_executable(lighttrack_openrgb_runtime_tests
  OpenRgbEffectManagerTests.cpp
  "${PROJECT_SOURCE_DIR}/src/integrations/openrgb/OpenRgbEffectManager.cpp"
  "${PROJECT_SOURCE_DIR}/src/integrations/openrgb/OpenRgbRenderCoordinator.cpp"
  "${OPENRGB_DIR}/RGBController/RGBController.cpp"
  "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects/RGBEffect.h"
)
target_include_directories(lighttrack_openrgb_runtime_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
  "${OPENRGB_DIR}/RGBController"
  "${OPENRGB_DIR}/dependencies/json"
  "${OPENRGB_DIR}/qt"
  "${OPENRGB_EFFECTS_PLUGIN_DIR}"
  "${OPENRGB_EFFECTS_PLUGIN_DIR}/Effects"
)
target_link_libraries(lighttrack_openrgb_runtime_tests PRIVATE
  "Qt${QT_VERSION_MAJOR}::Widgets"
)
set_target_properties(lighttrack_openrgb_runtime_tests PROPERTIES
  AUTOMOC ON
)

add_executable(lighttrack_timeline_tests
  TimelineClipboardTests.cpp
  "${PROJECT_SOURCE_DIR}/src/ui/UiHelpers.h"
  "${PROJECT_SOURCE_DIR}/src/ui/timeline/TimelineEditor.h"
  "${PROJECT_SOURCE_DIR}/src/ui/timeline/TimelineEditor.cpp"
  "${PROJECT_SOURCE_DIR}/src/ui/timeline/TimelineMetrics.h"
  "${PROJECT_SOURCE_DIR}/src/ui/timeline/internal/TimelineEditorInternal.h"
  "${PROJECT_SOURCE_DIR}/src/ui/timeline/internal/TimelineEditorCommon.cpp"
  "${PROJECT_SOURCE_DIR}/src/ui/timeline/internal/TimelineEditorGraphicsItems.cpp"
  "${PROJECT_SOURCE_DIR}/src/ui/timeline/internal/TimelineEditorScene.cpp"
  "${PROJECT_SOURCE_DIR}/src/ui/timeline/internal/TimelineEditorPanels.cpp"
  "${PROJECT_SOURCE_DIR}/src/ui/timeline/internal/TimelineEditorView.cpp"
)
target_include_directories(lighttrack_timeline_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(lighttrack_timeline_tests PRIVATE
  "Qt${QT_VERSION_MAJOR}::Widgets"
)
set_target_properties(lighttrack_timeline_tests PROPERTIES
  AUTOMOC ON
)

if(MSVC)
  foreach(test_target IN ITEMS
      lighttrack_history_tests
      lighttrack_persistence_tests
      lighttrack_openrgb_runtime_tests
      lighttrack_timeline_tests)
    target_compile_options(${test_target} PRIVATE /EHsc)
  endforeach()
endif()

if(WIN32)
  add_test(
    NAME lighttrack.layout_file_repository
    COMMAND ${CMAKE_COMMAND} -E env
      "PATH=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>;$ENV{PATH}"
      $<TARGET_FILE:lighttrack_persistence_tests>
  )
  add_test(
    NAME lighttrack.effect_manager
    COMMAND ${CMAKE_COMMAND} -E env
      "QT_QPA_PLATFORM=offscreen"
      "PATH=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Widgets>;$ENV{PATH}"
      $<TARGET_FILE:lighttrack_openrgb_runtime_tests>
  )
  add_test(
    NAME lighttrack.timeline
    COMMAND ${CMAKE_COMMAND} -E env
      "QT_QPA_PLATFORM=offscreen"
      "PATH=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Widgets>;$ENV{PATH}"
      $<TARGET_FILE:lighttrack_timeline_tests>
  )
else()
  add_test(
    NAME lighttrack.layout_file_repository
    COMMAND lighttrack_persistence_tests
  )
  add_test(
    NAME lighttrack.effect_manager
    COMMAND ${CMAKE_COMMAND} -E env
      "QT_QPA_PLATFORM=offscreen"
      $<TARGET_FILE:lighttrack_openrgb_runtime_tests>
  )
  add_test(
    NAME lighttrack.timeline
    COMMAND ${CMAKE_COMMAND} -E env
      "QT_QPA_PLATFORM=offscreen"
      $<TARGET_FILE:lighttrack_timeline_tests>
  )
endif()
