enable_testing()
file(GLOB TEST_SOURCES *.cpp)
# baseline_gen.cpp is a standalone tool (has its own main), not part of the gtest binary
list(REMOVE_ITEM TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/baseline_gen.cpp)
list(APPEND TEST_SOURCES
    ../src/exif_parser.cpp
    ../src/image_loader.cpp
    ../src/tonemap.cpp
    ../src/stb_image_impl.cpp
)
add_executable(UltraHDRViewerTests ${TEST_SOURCES})
target_link_libraries(UltraHDRViewerTests PRIVATE GTest::gtest_main uhdr)
target_include_directories(UltraHDRViewerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${UHDR_DIR}
    ${DEPS_DIR}
    ${DEPS_DIR}/glfw/include
)
include(GoogleTest)
gtest_discover_tests(UltraHDRViewerTests)
