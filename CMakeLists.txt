cmake_minimum_required(VERSION 3.21)
project(Stackies VERSION 0.4.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Prefer vcpkg's configuration-aware SQLite target on Windows.
# FindSQLite3's single cached SQLite3_LIBRARY can otherwise select the
# Debug static library for a multi-config Release MSVC build.
if(WIN32 AND VCPKG_TARGET_TRIPLET)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  set(STACKIES_SQLITE_TARGET unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  # CMake 4.3 renamed the imported target; keep compatibility with 3.21+.
  if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
  endif()
  set(STACKIES_SQLITE_TARGET SQLite3::SQLite3)
endif()
add_library(sticky_core src/model.cpp src/storage.cpp)
target_include_directories(sticky_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(sticky_core PUBLIC ${STACKIES_SQLITE_TARGET})
option(STICKY_BUILD_UI "Build Qt 6 desktop application" ON)
if(STICKY_BUILD_UI)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Svg Network LinguistTools)
  set(CMAKE_AUTORCC ON)
  set(CMAKE_AUTOMOC ON)
  add_executable(Stackies WIN32 MACOSX_BUNDLE
      src/main.cpp src/singleinstance.h src/perf.h src/i18n.h src/i18n.cpp src/platform.h src/platform.cpp src/diagnostics.cpp src/diagnostics.h
      src/colorpalette.cpp src/colorpalette.h
      src/controller.cpp src/controller.h
      src/window.cpp src/window.h src/noteeditor.h src/readpane.cpp src/overview.cpp src/overview.h
      resources.qrc)
  target_link_libraries(Stackies PRIVATE sticky_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Network)
  # Use native EWMH window-state requests when Qt runs on Linux/X11 (xcb).
  # X11 headers are optional: pure Wayland environments still build, using
  # the existing Qt fallback. Windows and macOS are completely unaffected.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 QUIET)
    if(X11_FOUND)
      target_link_libraries(Stackies PRIVATE X11::X11)
      target_compile_definitions(Stackies PRIVATE STACKIES_HAVE_X11=1)
      message(STATUS "Stackies: native X11 topmost support enabled")
    else()
      message(STATUS "Stackies: X11 development files not found; Qt fallback used")
    endif()
  endif()
  # The runtime/log version must match the build metadata, not a stale literal.
  target_compile_definitions(Stackies PRIVATE "STACKIES_APP_VERSION=\"${PROJECT_VERSION}\"")
  qt_add_translations(Stackies TS_FILES translations/stackies_zh_CN.ts translations/stackies_zh_TW.ts
          translations/stackies_ja.ts translations/stackies_ko.ts translations/stackies_es.ts
      RESOURCE_PREFIX "/i18n")
  if(MSVC)
    # Generate matching debugging symbols even for portable Release builds.
    target_compile_options(Stackies PRIVATE /W4 /Zi)
    target_link_options(Stackies PRIVATE /DEBUG:FULL)
  else()
    target_compile_options(Stackies PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The desktop entry + icon are required for reliable GNOME dock/taskbar
    # identity. Also copy the assets alongside the build-tree executable so
    # a portable Linux archive can include them.
    include(GNUInstallDirs)
    configure_file(packaging/linux/stackies.desktop
        "${CMAKE_CURRENT_BINARY_DIR}/stackies.desktop" COPYONLY)
    configure_file(assets/app-icon.svg
        "${CMAKE_CURRENT_BINARY_DIR}/stackies.svg" COPYONLY)
    # Stage portable-package metadata and our own license notices next to
    # the executable. The final packaging step still must copy these files
    # and audit licenses of the actual bundled Qt libraries/plugins.
    configure_file(LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE" COPYONLY)
    configure_file(THIRD_PARTY_NOTICES.md
        "${CMAKE_CURRENT_BINARY_DIR}/THIRD_PARTY_NOTICES.md" COPYONLY)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/licenses")
    configure_file(assets/tabler/LICENSE
        "${CMAKE_CURRENT_BINARY_DIR}/licenses/Tabler-Icons-MIT.txt" COPYONLY)
    install(TARGETS Stackies RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/stackies.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/app-icon.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME stackies.svg)
    install(FILES LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/Stackies)
    install(FILES assets/tabler/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/Stackies/licenses
        RENAME Tabler-Icons-MIT.txt)
  endif()
  if(WIN32)
    target_link_libraries(Stackies PRIVATE dbghelp dwmapi gdi32 psapi)
    # Embed an actual multi-size .ico in the PE executable, not just a
    # QApplication icon (Explorer/Start Menu read the PE icon resource).
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/generate_icon.py"
              "${CMAKE_CURRENT_BINARY_DIR}/Stackies.ico"
      RESULT_VARIABLE STICKY_ICON_RESULT)
    if(NOT STICKY_ICON_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to generate Windows app icon")
    endif()
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}/Stackies.ico" STICKY_ICON_PATH)
    configure_file(src/windows-icon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/windows-icon.rc" @ONLY)
    enable_language(RC)
    target_sources(Stackies PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows-icon.rc")
  endif()
endif()
include(CTest)
if(BUILD_TESTING)
  add_executable(sticky_core_tests tests/test_core.cpp)
  target_link_libraries(sticky_core_tests PRIVATE sticky_core)
  add_test(NAME core_merge_storage COMMAND sticky_core_tests)
  # Manual performance probe, intentionally excluded from CTest pass/fail.
  add_executable(sticky_storage_bench tests/bench_storage.cpp)
  target_link_libraries(sticky_storage_bench PRIVATE sticky_core)
endif()

if(STICKY_BUILD_UI AND BUILD_TESTING)
  add_executable(sticky_single_instance_tests tests/test_single_instance.cpp src/singleinstance.h)
  target_link_libraries(sticky_single_instance_tests PRIVATE Qt6::Core Qt6::Network)
  target_include_directories(sticky_single_instance_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME single_instance COMMAND sticky_single_instance_tests)
  add_executable(sticky_ui_tests
      tests/test_ui.cpp src/perf.h src/i18n.h src/i18n.cpp src/platform.h src/platform.cpp
      src/diagnostics.cpp src/diagnostics.h
      src/colorpalette.cpp src/colorpalette.h
      src/controller.cpp src/controller.h
      src/window.cpp src/window.h src/noteeditor.h src/readpane.cpp src/overview.cpp src/overview.h
      resources.qrc)
  target_link_libraries(sticky_ui_tests PRIVATE sticky_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND)
    target_link_libraries(sticky_ui_tests PRIVATE X11::X11)
    target_compile_definitions(sticky_ui_tests PRIVATE STACKIES_HAVE_X11=1)
  endif()
  if(WIN32)
    target_link_libraries(sticky_ui_tests PRIVATE dbghelp dwmapi gdi32 psapi)
  endif()
  add_test(NAME ui_visibility_overview COMMAND sticky_ui_tests)
  set_tests_properties(ui_visibility_overview PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25)
endif()
