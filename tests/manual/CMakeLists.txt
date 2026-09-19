# Manual, human-driven checks. Built alongside the tests but never registered with ctest.
qt_add_executable(omaseed-reveal-manual reveal.cpp)
target_link_libraries(omaseed-reveal-manual PRIVATE omaseed::integration omaseed::warnings Qt6::Core)
set_target_properties(omaseed-reveal-manual PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/manual"
)
