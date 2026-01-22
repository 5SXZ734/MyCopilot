cmake_minimum_required(VERSION 3.21)
project(OverlayText LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(OverlayText
    main.cpp
)

target_link_libraries(OverlayText PRIVATE Qt6::Widgets)

# Optional: on Windows, avoid console window for GUI apps
if (WIN32)
  set_target_properties(OverlayText PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
