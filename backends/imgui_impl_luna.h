// dear imgui: Renderer Backend for Vulkan
// This needs to be used along with a Platform Backend (e.g. GLFW, SDL, Win32, custom..)

// Implemented features:
//  [!] Renderer: User texture binding. Use 'VkDescriptorSet' as texture identifier. Call ImGui_ImplVulkan_AddTexture() to register one. Read the FAQ about ImTextureID/ImTextureRef + https://github.com/ocornut/imgui/pull/914 for discussions.
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(ImGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.

// The aim of imgui_impl_vulkan.h/.cpp is to be usable in your engine without any modification.
// IF YOU FEEL YOU NEED TO MAKE ANY CHANGE TO THIS CODE, please share them and your feedback at https://github.com/ocornut/imgui/

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Important note to the reader who wish to integrate imgui_impl_vulkan.cpp/.h in their own engine/app.
// - Common ImGui_ImplVulkan_XXX functions and structures are used to interface with imgui_impl_vulkan.cpp/.h.
//   You will use those if you want to use this rendering backend in your engine/app.
// - Helper ImGui_ImplVulkanH_XXX functions and structures are only used by this example (main.cpp) and by
//   the backend itself (imgui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// Read comments in imgui_impl_vulkan.h.

#pragma once
#ifndef IMGUI_DISABLE
#include "imgui.h" // IMGUI_IMPL_API

// [Configuration] in order to use a custom Vulkan function loader:
// (1) You'll need to disable default Vulkan function prototypes.
//     We provide a '#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES' convenience configuration flag.
//     In order to make sure this is visible from the imgui_impl_vulkan.cpp compilation unit:
//     - Add '#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES' in your imconfig.h file
//     - Or as a compilation flag in your build system
//     - Or uncomment here (not recommended because you'd be modifying imgui sources!)
//     - Do not simply add it in a .cpp file!
// (2) Call ImGui_ImplVulkan_LoadFunctions() before ImGui_ImplVulkan_Init() with your custom function.
// If you have no idea what this is, leave it alone!
//#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES

// Convenience support for Volk
// (you can also technically use IMGUI_IMPL_VULKAN_NO_PROTOTYPES + wrap Volk via ImGui_ImplVulkan_LoadFunctions().)
//#define IMGUI_IMPL_VULKAN_USE_VOLK

#if defined(IMGUI_IMPL_VULKAN_NO_PROTOTYPES) && !defined(VK_NO_PROTOTYPES)
#define VK_NO_PROTOTYPES
#endif
#if defined(VK_USE_PLATFORM_WIN32_KHR) && !defined(NOMINMAX)
#define NOMINMAX
#endif

// Vulkan includes
#include <luna/luna.h>

// Backend uses a small number of descriptors per font atlas + as many as additional calls done to ImGui_ImplVulkan_AddTexture().
#define IMGUI_IMPL_LUNA_MINIMUM_IMAGE_SAMPLER_POOL_SIZE (8) // Minimum per atlas

// Initialization data, for ImGui_ImplVulkan_Init()
// [Please zero-clear before use!]
// - About descriptor pool:
//   - A VkDescriptorPool should be created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
//     and must contain a pool size large enough to hold a small number of VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER descriptors.
//   - As an convenience, by setting DescriptorPoolSize > 0 the backend will create one for you.
// - About dynamic rendering:
//   - When using dynamic rendering, set UseDynamicRendering=true and fill PipelineRenderingCreateInfo structure.
struct ImGui_ImplLuna_InitInfo
{
        LunaRenderPassSubpass renderPassSubpass{};
        union
        {
                LunaDescriptorPool descriptorPool = LUNA_NULL_HANDLE; // See requirements in note above
                uint32_t descriptorPoolSize; // Set to automatically create descriptor pool
        };
        bool useDescriptorPoolSize = false;
        uint32_t minImageCount = -1; // Ignored if -1
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

        // const VkAllocationCallbacks *Allocator;
        void (*CheckVkResultFn)(VkResult err) = nullptr; // (Optional) called to check if the return value was ok
};

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
IMGUI_IMPL_API bool ImGui_ImplLuna_Init(ImGui_ImplLuna_InitInfo *initInfo);
IMGUI_IMPL_API void ImGui_ImplLuna_Shutdown();
IMGUI_IMPL_API void ImGui_ImplLuna_RenderDrawData(ImDrawData *drawData,
                                                  LunaGraphicsPipeline pipeline = LUNA_NULL_HANDLE);

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting ImDrawData::Textures = NULL to handle this manually.
IMGUI_IMPL_API void ImGui_ImplLuna_UpdateTexture(ImTextureData *textureData);

// Register a texture (VkDescriptorSet == ImTextureID)
// FIXME: This is experimental in the sense that we are unsure how to best design/tackle this problem
// Please post to https://github.com/ocornut/imgui/pull/914 if you have suggestions.
IMGUI_IMPL_API LunaDescriptorSet ImGui_ImplLuna_AddTexture(LunaSampler sampler,
                                                           LunaImage image,
                                                           VkImageLayout imageLayout);
IMGUI_IMPL_API void ImGui_ImplLuna_RemoveTexture(LunaDescriptorSet descriptorSet);

#endif // #ifndef IMGUI_DISABLE
