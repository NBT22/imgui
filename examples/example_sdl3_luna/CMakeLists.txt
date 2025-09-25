cmake_minimum_required(VERSION 3.30)
project(imguiLunaExample CXX)

set(CMAKE_CXX_STANDARD 11)

set(LUNA_DEFINE_VK_NO_PROTOTYPES OFF)
include(FetchContent)
FetchContent_Declare(
        Luna
        SOURCE_DIR ../../Luna
        EXCLUDE_FROM_ALL
        SYSTEM
)
FetchContent_MakeAvailable(Luna)

add_executable(imguiLunaExample main.cpp
        ${CMAKE_SOURCE_DIR}/backends/imgui_impl_luna.cpp
        ${CMAKE_SOURCE_DIR}/backends/imgui_impl_luna.h
)
target_link_libraries(imguiLunaExample PRIVATE imgui SDL3 Luna)