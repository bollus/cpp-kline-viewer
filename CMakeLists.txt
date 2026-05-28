cmake_minimum_required(VERSION 3.21)

project(q4j_kline_viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets OpenGLWidgets)

qt_add_executable(q4j_kline_viewer WIN32
  src/main.cpp
  resources/resources.qrc
)

if(WIN32)
  target_sources(q4j_kline_viewer PRIVATE resources/app.rc)
endif()

target_link_libraries(q4j_kline_viewer
  PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::OpenGLWidgets
)
