cmake_minimum_required(VERSION 3.30)
project(imgui CXX)

set(CMAKE_CXX_STANDARD 11)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

include(FetchContent)
set(LUNA_DEFINE_VK_NO_PROTOTYPES OFF)
FetchContent_Declare(
        Luna
        GIT_REPOSITORY https://github.com/NBT22/Luna
        GIT_TAG main
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        EXCLUDE_FROM_ALL
        SYSTEM
)
FetchContent_MakeAvailable(Luna)

find_package(Vulkan REQUIRED)

add_library(imgui STATIC
        imconfig.h
        imgui.cpp
        imgui.h
        imgui_demo.cpp
        imgui_draw.cpp
        imgui_internal.h
        imgui_tables.cpp
        imgui_widgets.cpp
        imstb_rectpack.h
        imstb_textedit.h
        imstb_truetype.h
        misc/cpp/imgui_stdlib.cpp
        misc/cpp/imgui_stdlib.h
        misc/fonts/binary_to_compressed_c.cpp
        misc/single_file/imgui_single_file.h
        backends/imgui_impl_sdl3.cpp
        backends/imgui_impl_sdl3.h
)
target_include_directories(imgui PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/backends)

add_subdirectory(examples/example_sdl3_luna)
add_subdirectory(examples/example_sdl3_vulkan)
