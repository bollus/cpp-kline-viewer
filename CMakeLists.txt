cmake_minimum_required(VERSION 3.21)

project(q4j_kline_viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets OpenGLWidgets)

qt_add_executable(q4j_kline_viewer
  WIN32
  MACOSX_BUNDLE
  src/main.cpp
  resources/resources.qrc
)

if(WIN32)
  target_sources(q4j_kline_viewer PRIVATE resources/app.rc)
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
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
  )
endif()

target_link_libraries(q4j_kline_viewer
  PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::OpenGLWidgets
)
