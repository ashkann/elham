cmake_minimum_required(VERSION 3.17)
project(ElhamC LANGUAGES C VERSION 1.0)
configure_file(config.h.in config.h)
set(CMAKE_C_STANDARD 99)

add_executable(ElhamC main.c)
target_include_directories(ElhamC PUBLIC "${PROJECT_BINARY_DIR}")
include_directories(/usr/local/include)

target_link_libraries(ElhamC vulkan glfw x265)

add_library(vulkan UNKNOWN IMPORTED)
    set_target_properties(vulkan PROPERTIES
        IMPORTED_LOCATION "/usr/local/lib/libvulkan.dylib")

add_library(glfw UNKNOWN IMPORTED)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "/usr/local/lib/libglfw.dylib")

add_library(x265 UNKNOWN IMPORTED)
    set_target_properties(x265 PROPERTIES
        IMPORTED_LOCATION "/usr/local/lib/libx265.dylib")