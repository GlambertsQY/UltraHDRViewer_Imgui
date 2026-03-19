cmake_minimum_required(VERSION 3.16)
project(UltraHDRViewer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(UHDR_VIEWER_VULKAN "Enable Vulkan backend" ON)
option(UHDR_VIEWER_OPENGL "Enable OpenGL backend" ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build type defaults to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")

# -------------------------------------------------------------------
# Dependencies - source code clones in deps/
# -------------------------------------------------------------------
set(DEPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps)

# --- GLFW ---
set(GLFW_DIR ${DEPS_DIR}/glfw)
if(NOT EXISTS ${GLFW_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "GLFW not found at ${GLFW_DIR}. Please run clone_deps.bat first.")
endif()
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${GLFW_DIR} glfw)

# --- Dear ImGui ---
set(IMGUI_DIR ${DEPS_DIR}/imgui)
if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
    message(FATAL_ERROR "Dear ImGui not found at ${IMGUI_DIR}. Please run clone_deps.bat first.")
endif()

# --- libultrahdr ---
set(UHDR_DIR ${DEPS_DIR}/libultrahdr)
if(NOT EXISTS ${UHDR_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "libultrahdr not found at ${UHDR_DIR}. Please run clone_deps.bat first.")
endif()
set(UHDR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UHDR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UHDR_BUILD_DEPS ON CACHE BOOL "" FORCE)
add_subdirectory(${UHDR_DIR} libultrahdr)

# -------------------------------------------------------------------
# ImGui source files
# -------------------------------------------------------------------
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
)

# -------------------------------------------------------------------
# OpenGL backend
# -------------------------------------------------------------------
if(UHDR_VIEWER_OPENGL)
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
endif()

# -------------------------------------------------------------------
# Vulkan backend
# -------------------------------------------------------------------
if(UHDR_VIEWER_VULKAN)
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
endif()

add_library(imgui_lib STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)

if(UHDR_VIEWER_OPENGL)
    target_compile_definitions(imgui_lib PUBLIC UHDR_VIEWER_OPENGL_ENABLED)
endif()

if(UHDR_VIEWER_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(imgui_lib PUBLIC Vulkan::Vulkan)
    target_compile_definitions(imgui_lib PUBLIC UHDR_VIEWER_VULKAN_ENABLED)
endif()

# -------------------------------------------------------------------
# Application
# -------------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/app.cpp
    src/image_loader.cpp
    src/exif_parser.cpp
    src/file_dialog.cpp
    src/stb_image_impl.cpp
)

if(UHDR_VIEWER_OPENGL)
    list(APPEND APP_SOURCES src/renderer_gl.cpp)
endif()

# Always compile renderer_vk.cpp - it has #ifdef guards inside
list(APPEND APP_SOURCES src/renderer_vk.cpp)

add_executable(${PROJECT_NAME} ${APP_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${UHDR_DIR}
    ${DEPS_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    imgui_lib
    glfw
    uhdr
)

if(UHDR_VIEWER_OPENGL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UHDR_VIEWER_OPENGL_ENABLED)
endif()

if(UHDR_VIEWER_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UHDR_VIEWER_VULKAN_ENABLED)
endif()

# -------------------------------------------------------------------
# Platform-specific settings
# -------------------------------------------------------------------
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /utf-8)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE dwmapi opengl32)
endif()
