cmake_minimum_required(VERSION 3.16)

project(QtSplitterHello VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.2 COMPONENTS Widgets REQUIRED)

qt_add_executable(QtSplitterHello
  main.cpp
)

target_link_libraries(QtSplitterHello
  PRIVATE
    Qt6::Widgets
)

if(APPLE)
  set(_qt_sdk "${CMAKE_OSX_SYSROOT}")
  if(_qt_sdk AND EXISTS "${_qt_sdk}/usr/include/c++/v1")
    target_include_directories(QtSplitterHello
      SYSTEM BEFORE
      PRIVATE
        "${_qt_sdk}/usr/include/c++/v1"
    )
  endif()
endif()
