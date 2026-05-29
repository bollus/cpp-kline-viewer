cmake_minimum_required(VERSION 3.21)

project(q4j_kline_viewer VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets OpenGLWidgets)

set(Q4J_UPDATE_REPO "" CACHE STRING "GitHub repository used by the in-app updater, for example owner/repo")
set(Q4J_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Application version shown in the app and embedded in packages")
string(REPLACE "." "," Q4J_APP_VERSION_COMMA "${Q4J_APP_VERSION}.0")

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

target_compile_definitions(q4j_kline_viewer
  PRIVATE
    Q4J_APP_VERSION="${Q4J_APP_VERSION}"
    Q4J_UPDATE_REPO="${Q4J_UPDATE_REPO}"
)
