cmake_minimum_required(VERSION 3.22)

project(github-repo-updater
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(github-repo-updater
    src/main.cpp
)

target_link_libraries(github-repo-updater
    PRIVATE Qt6::Widgets
)

set_target_properties(github-repo-updater PROPERTIES
    WIN32_EXECUTABLE ON
)
