cmake_minimum_required(VERSION 3.22)

project(uhdrviewer)

if(NOT DEPS_DIR)
    set(DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../deps")
endif()
file(TO_CMAKE_PATH "${DEPS_DIR}" DEPS_DIR)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-DANDROID -DSDL_MAIN_HANDLED)
add_compile_options(-Wall -Wextra)

# -------------------- SDL2 --------------------
set(SDL2_DIR ${DEPS_DIR}/sdl2)
set(SDL2_INCLUDE_DIRS ${SDL2_DIR}/include)
set(SDL2_STATIC ON CACHE BOOL "" FORCE)
set(SDL2_TESTS OFF CACHE BOOL "")
set(SDL2_INSTALL OFF CACHE BOOL "")
set(SDL2_GAME_CONTROLLER OFF CACHE BOOL "")
set(SDL2_HIDAPI OFF CACHE BOOL "")
add_subdirectory(${SDL2_DIR} ${CMAKE_CURRENT_BINARY_DIR}/sdl2-build)

# -------------------- libjpeg-turbo (add_subdirectory for Android) --------------------
set(TURBOJPEG_INCLUDE ${DEPS_DIR}/turbojpeg)
set(TURBOJPEG_IS_SUBPROJECT ON CACHE BOOL "")
set(WITH_JAVA OFF CACHE BOOL "")
set(WITH_TURBOJPEG ON CACHE BOOL "")
set(WITH_CJPEG OFF CACHE BOOL "")
set(WITH_DJPEG OFF CACHE BOOL "")
set(WITH_JPEGTRAN OFF CACHE BOOL "")
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "" FORCE)
set(CMAKE_INSTALL_BINDIR "bin" CACHE PATH "")
set(CMAKE_INSTALL_DOCDIR "doc" CACHE PATH "")
set(CMAKE_INSTALL_MANDIR "man" CACHE PATH "")
set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "")
add_subdirectory(${DEPS_DIR}/turbojpeg ${CMAKE_CURRENT_BINARY_DIR}/turbojpeg-build)
set(TURBOJPEG_LIB ${CMAKE_CURRENT_BINARY_DIR}/turbojpeg-build/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX})

# -------------------- libultrahdr --------------------
set(UHDR_DIR ${DEPS_DIR}/libultrahdr)
set(UHDR_BUILD_DEPS OFF CACHE BOOL "")
set(UHDR_BUILD_EXAMPLES OFF CACHE BOOL "")
set(UHDR_BUILD_TESTS OFF CACHE BOOL "")
set(UHDR_ENABLE_GLES OFF CACHE BOOL "")
set(UHDR_ENABLE_INSTALL OFF CACHE BOOL "")
set(JPEG_PREBUILT_INCLUDE "${DEPS_DIR}/turbojpeg;${CMAKE_CURRENT_BINARY_DIR}/turbojpeg-build")
set(JPEG_PREBUILT_LIB ${CMAKE_CURRENT_BINARY_DIR}/turbojpeg-build/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
set(THIRD_PARTY_DIR ${DEPS_DIR})
add_subdirectory(${UHDR_DIR} libultrahdr EXCLUDE_FROM_ALL)

# -------------------- stb_image --------------------
set(STB_IMAGE ${DEPS_DIR}/stb_image.h)

# -------------------- ImGui sources --------------------
set(IMGUI_DIR ${DEPS_DIR}/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
add_library(imgui_lib STATIC EXCLUDE_FROM_ALL ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_DIR}/include
)
target_link_libraries(imgui_lib PUBLIC SDL2)
target_compile_definitions(imgui_lib PUBLIC UHDR_VIEWER_OPENGL_ENABLED IMGUI_IMPL_OPENGL_ES3)

# -------------------- Application sources --------------------
set(APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/native.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/image_loader_android.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exif_parser_android.cpp
)

add_library(${CMAKE_PROJECT_NAME} SHARED ${APP_SOURCES})

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${UHDR_DIR}
    ${DEPS_DIR}
    ${DEPS_DIR}/turbojpeg
    ${CMAKE_CURRENT_BINARY_DIR}/turbojpeg-build
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    imgui_lib
    SDL2
    uhdr
    android
    log
    EGL
    GLESv3
    dl
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    UHDR_VIEWER_OPENGL_ENABLED
)

# Define the nativeOnCreate etc. JNI functions
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -fno-rtti -fno-exceptions)
