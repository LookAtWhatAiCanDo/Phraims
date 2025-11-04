cmake_minimum_required(VERSION 3.16)

project(LiveStreamMultiChat VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.2 COMPONENTS Widgets WebEngineWidgets REQUIRED)

qt_add_executable(LiveStreamMultiChat
  main.cpp
)

# On macOS build a GUI app bundle (prevents launching a Terminal).
if(APPLE)
  set_target_properties(LiveStreamMultiChat PROPERTIES MACOSX_BUNDLE TRUE)
elseif(WIN32)
  # On Windows prefer a GUI subsystem build to avoid a console window.
  # This sets the WIN32_EXECUTABLE property so the linker uses the
  # WINDOWS subsystem instead of CONSOLE.
  set_target_properties(LiveStreamMultiChat PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(LiveStreamMultiChat
  PRIVATE
    Qt6::Widgets
)

target_link_libraries(LiveStreamMultiChat
  PRIVATE
    Qt6::WebEngineWidgets
)

if(APPLE)
  set(_qt_sdk "${CMAKE_OSX_SYSROOT}")
  if(_qt_sdk AND EXISTS "${_qt_sdk}/usr/include/c++/v1")
    target_include_directories(LiveStreamMultiChat
      SYSTEM BEFORE
      PRIVATE
        "${_qt_sdk}/usr/include/c++/v1"
    )
  endif()
endif()
