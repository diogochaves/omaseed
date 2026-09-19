# Throwaway toolchain check: builds a Qt Quick window with DBus and Network linked.
# Not part of the app. `cmake -S scripts/dev/hello -B /tmp/hello -G Ninja && cmake --build /tmp/hello`
cmake_minimum_required(VERSION 3.21)
project(omaseed-hello LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Qml Network DBus Test)
qt_standard_project_setup()
qt_add_executable(hello main.cpp)
qt_add_qml_module(hello URI Hello VERSION 1.0 QML_FILES Main.qml)
target_link_libraries(hello PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Network Qt6::DBus)
