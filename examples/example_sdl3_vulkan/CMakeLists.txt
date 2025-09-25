cmake_minimum_required(VERSION 3.30)
project(imguiVulkanExample CXX)

set(CMAKE_CXX_STANDARD 11)

find_package(Vulkan REQUIRED)

add_executable(imguiVulkanExample main.cpp
        ${CMAKE_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        ${CMAKE_SOURCE_DIR}/backends/imgui_impl_vulkan.h
)
target_link_libraries(imguiVulkanExample PRIVATE imgui SDL3 Vulkan::Vulkan)