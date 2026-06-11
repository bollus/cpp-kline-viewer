cmake_minimum_required(VERSION 3.21)

project(q4j_kline_viewer VERSION 1.0.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets OpenGLWidgets)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Qml)

set(Q4J_UPDATE_REPO "" CACHE STRING "GitHub repository used by the in-app updater, for example owner/repo")
set(Q4J_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Application version shown in the app and embedded in packages")
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" Q4J_APP_VERSION_MATCH "${Q4J_APP_VERSION}")
if(Q4J_APP_VERSION_MATCH)
  set(Q4J_APP_VERSION_RC "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
else()
  set(Q4J_APP_VERSION_RC "1,0,0,0")
endif()

if(WIN32)
  configure_file(
    resources/app.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/app.rc
    @ONLY
  )
endif()

qt_add_executable(q4j_kline_viewer
  WIN32
  MACOSX_BUNDLE
  src/main.cpp
  src/app/core.hpp
  src/app/chart_widget.hpp
  src/app/candle_client.hpp
  src/app/main_window.hpp
  resources/resources.qrc
)

if(WIN32)
  target_sources(q4j_kline_viewer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

if(APPLE)
  set(MACOSX_BUNDLE_ICON_FILE app-icon.icns)
  set_source_files_properties(resources/app-icon.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )
  target_sources(q4j_kline_viewer PRIVATE resources/app-icon.icns)
  set_target_properties(q4j_kline_viewer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Q4J K-Line Viewer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.q4j.klineviewer"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${Q4J_APP_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${Q4J_APP_VERSION}"
  )
endif()

target_link_libraries(q4j_kline_viewer
  PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::OpenGLWidgets
)

if(Qt6Qml_FOUND)
  target_link_libraries(q4j_kline_viewer PRIVATE Qt6::Qml)
  target_compile_definitions(q4j_kline_viewer PRIVATE Q4J_HAS_QJS_ENGINE=1)
else()
  target_compile_definitions(q4j_kline_viewer PRIVATE Q4J_HAS_QJS_ENGINE=0)
endif()

target_compile_definitions(q4j_kline_viewer
  PRIVATE
    Q4J_APP_VERSION="${Q4J_APP_VERSION}"
    Q4J_UPDATE_REPO="${Q4J_UPDATE_REPO}"
)

add_custom_command(TARGET q4j_kline_viewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/indicators"
    "$<TARGET_FILE_DIR:q4j_kline_viewer>/indicators"
)
