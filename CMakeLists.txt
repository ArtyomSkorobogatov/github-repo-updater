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
    src/gui/main_window.cpp
    src/gui/main_window.h
    src/gui/update_worker.cpp
    src/gui/update_worker.h
    src/git/git_runner.cpp
    src/git/git_runner.h
    src/git/repo_updater.cpp
    src/git/repo_updater.h
)

target_link_libraries(github-repo-updater
    PRIVATE Qt6::Widgets
)

set_target_properties(github-repo-updater PROPERTIES
    WIN32_EXECUTABLE ON
)
target_compile_definitions(github-repo-updater PRIVATE APP_VERSION="${PROJECT_VERSION}")

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(git-runner-tests
        tests/git_runner_tests.cpp
        src/git/git_runner.cpp
    )
    target_include_directories(git-runner-tests PRIVATE src)
    target_link_libraries(git-runner-tests PRIVATE Qt6::Core)
    add_test(NAME git-runner COMMAND git-runner-tests)
    qt_add_executable(repo-updater-tests
        tests/repo_updater_tests.cpp
        src/git/git_runner.cpp
        src/git/repo_updater.cpp
    )
    target_include_directories(repo-updater-tests PRIVATE src)
    target_link_libraries(repo-updater-tests PRIVATE Qt6::Core)
    add_test(NAME repo-updater COMMAND repo-updater-tests)
    qt_add_executable(gui-tests
        tests/gui_tests.cpp
        src/gui/main_window.cpp
        src/gui/main_window.h
        src/gui/update_worker.cpp
        src/gui/update_worker.h
        src/git/git_runner.cpp
        src/git/repo_updater.cpp
    )
    target_include_directories(gui-tests PRIVATE src)
    target_compile_definitions(gui-tests PRIVATE APP_VERSION="${PROJECT_VERSION}")
    target_link_libraries(gui-tests PRIVATE Qt6::Widgets Qt6::Test)
    set_target_properties(git-runner-tests repo-updater-tests gui-tests PROPERTIES WIN32_EXECUTABLE OFF)
    add_test(NAME gui COMMAND gui-tests -platform offscreen
        -platformpluginpath "$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
endif()
