add_executable(lighttrack_core_tests
  SnapshotHistoryTests.cpp
)

target_link_libraries(lighttrack_core_tests PRIVATE
  lighttrack_core
)

add_test(
  NAME lighttrack_core.snapshot_history
  COMMAND lighttrack_core_tests
)

add_executable(lighttrack_persistence_tests
  LayoutFileRepositoryTests.cpp
)

target_link_libraries(lighttrack_persistence_tests PRIVATE
  lighttrack_persistence
  Qt${QT_VERSION_MAJOR}::Core
)

if(WIN32)
  add_test(
    NAME lighttrack_persistence.layout_file_repository
    COMMAND
      ${CMAKE_COMMAND}
      -E
      env
      "PATH=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>;$ENV{PATH}"
      $<TARGET_FILE:lighttrack_persistence_tests>
  )
else()
  add_test(
    NAME lighttrack_persistence.layout_file_repository
    COMMAND lighttrack_persistence_tests
  )
endif()

add_test(
  NAME lighttrack_architecture.boundaries
  COMMAND
    ${CMAKE_COMMAND}
    -DLIGHTTRACK_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P
    ${CMAKE_CURRENT_SOURCE_DIR}/ArchitectureBoundaries.cmake
)
