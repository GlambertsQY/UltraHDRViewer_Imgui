enable_testing()
add_executable(UltraHDRViewerTests main.cpp)
target_link_libraries(UltraHDRViewerTests PRIVATE GTest::gtest_main uhdr)
target_include_directories(UltraHDRViewerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${UHDR_DIR}
    ${DEPS_DIR}
)
include(GoogleTest)
gtest_discover_tests(UltraHDRViewerTests)
