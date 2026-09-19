cmake_minimum_required(VERSION 3.24)

project(omaseed
    VERSION 0.1.0
    DESCRIPTION "Torrents for Omarchy, powered by qBittorrent"
    HOMEPAGE_URL "https://github.com/diogochaves/omaseed"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OMASEED_WERROR "Treat compiler warnings as errors (CI turns this on)" OFF)
option(BUILD_TESTING "Build the unit and QML tests" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 QuickLayouts DBus)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test QuickTest)
endif()
find_package(Qt6Keychain REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

# Every Qt CMake policy this Qt knows about, explicitly on its modern behaviour, so the
# configure step is silent no matter which Qt version builds us.
foreach(policy QTP0001 QTP0002 QTP0003 QTP0004 QTP0005)
    if(QT_KNOWN_POLICY_${policy})
        qt_policy(SET ${policy} NEW)
    endif()
endforeach()

# All QML modules land under build/qml, so tooling needs a single import path.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/OmaseedWarnings.cmake)

add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

add_custom_target(lint
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dev/lint.sh" "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running qmllint and clang-format --dry-run --Werror"
    USES_TERMINAL
    VERBATIM
)
