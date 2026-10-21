enable_testing()
file(GLOB TEST_SOURCES *.cpp)
# baseline_gen.cpp is a standalone tool (has its own main), not part of the gtest binary
list(REMOVE_ITEM TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/baseline_gen.cpp)
# android_loader_test.cpp is built by the AndroidLoaderTests target below
list(REMOVE_ITEM TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/android_loader_test.cpp)
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
gtest_discover_tests(UltraHDRViewerTests DISCOVERY_MODE PRE_TEST)

# ---------------------------------------------------------------------------
# Android loader host tests: compiles the Android decode/tone-map/EXIF units
# with the desktop toolchain so HDR decode regressions are caught without a device.
# ---------------------------------------------------------------------------
set(ANDROID_CPP_DIR ${CMAKE_SOURCE_DIR}/android/app/src/main/cpp)
add_executable(AndroidLoaderTests
    android_loader_test.cpp
    ${ANDROID_CPP_DIR}/image_loader_android.cpp
    ${ANDROID_CPP_DIR}/exif_parser_android.cpp
    ${ANDROID_CPP_DIR}/stb_image_impl.cpp
    ${ANDROID_CPP_DIR}/tonemap_android_test.cpp
    ${ANDROID_CPP_DIR}/exif_android_test.cpp
)
target_link_libraries(AndroidLoaderTests PRIVATE GTest::gtest_main uhdr)
target_include_directories(AndroidLoaderTests PRIVATE
    ${ANDROID_CPP_DIR}
    ${UHDR_DIR}
    ${DEPS_DIR}
)
gtest_discover_tests(AndroidLoaderTests DISCOVERY_MODE PRE_TEST)
