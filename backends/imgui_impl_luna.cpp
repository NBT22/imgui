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

#include <imgui.h>
#include <luna/lunaBuffer.h>
#include <luna/lunaDevice.h>
#include <luna/lunaDrawing.h>
#include <luna/lunaImage.h>
#ifndef IMGUI_DISABLE
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <imgui_impl_luna.h>
#include <luna/luna.h>
#include <luna/lunaInstance.h>
#include <luna/lunaTypes.h>
#include <vector>
#include <vulkan/vulkan_core.h>
#undef Status // X11 headers are leaking this.

// Visual Studio warnings
#ifdef _MSC_VER
#pragma warning(disable : 4127) // condition expression is constant
#endif

namespace
{
namespace typedefs
{
    struct ImGui_ImplLuna_Texture
    {
            LunaImage image{};
            LunaDescriptorSet descriptorSet{};
    };

    // Luna data
    class ImGui_ImplLuna_Data
    {
        public:
            explicit ImGui_ImplLuna_Data(const ImGui_ImplLuna_InitInfo &lunaInitInfo): lunaInitInfo(lunaInitInfo) {}

            ImGui_ImplLuna_InitInfo lunaInitInfo{};
            VkDeviceSize bufferMemoryAlignment = 256; // NOLINT(*-avoid-magic-numbers)
            VkDeviceSize nonCoherentAtomSize = 64; // NOLINT(*-avoid-magic-numbers)
            LunaDescriptorSetLayout descriptorSetLayout{};
            LunaGraphicsPipeline pipeline{};
            LunaShaderModule vertexShaderModule{};
            LunaShaderModule fragmentShaderModule{};
            LunaDescriptorPool descriptorPool{};

            // Texture management
            LunaSampler textureSampler{};

            // Render buffers for main window
            VkDeviceSize vertexBufferAllocatedSize{};
            LunaBuffer vertexBuffer{};
            VkDeviceSize indexBufferAllocatedSize{};
            LunaBuffer indexBuffer{};
    };
} // namespace typedefs

namespace constants
{
    // backends/vulkan/glsl_shader.vert, compiled with:
    // # glslangValidator -V -x -o glsl_shader.vert.u32 glsl_shader.vert
    /*
    #version 450 core
    layout(location = 0) in vec2 aPos;
    layout(location = 1) in vec2 aUV;
    layout(location = 2) in vec4 aColor;
    layout(push_constant) uniform uPushConstant { vec2 uScale; vec2 uTranslate; } pc;

    out gl_PerVertex { vec4 gl_Position; };
    layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;

    void main()
    {
        Out.Color = aColor;
        Out.UV = aUV;
        gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);
    }
    */
    constexpr std::array<uint32_t, 324> VERTEX_SHADER_SPIRV = {
        0x07230203, 0x00010000, 0x00080001, 0x0000002e, 0x00000000, 0x00020011, 0x00000001, 0x0006000b, 0x00000001,
        0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x000a000f, 0x00000000,
        0x00000004, 0x6e69616d, 0x00000000, 0x0000000b, 0x0000000f, 0x00000015, 0x0000001b, 0x0000001c, 0x00030003,
        0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000009, 0x00000000,
        0x00050006, 0x00000009, 0x00000000, 0x6f6c6f43, 0x00000072, 0x00040006, 0x00000009, 0x00000001, 0x00005655,
        0x00030005, 0x0000000b, 0x0074754f, 0x00040005, 0x0000000f, 0x6c6f4361, 0x0000726f, 0x00030005, 0x00000015,
        0x00565561, 0x00060005, 0x00000019, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000, 0x00060006, 0x00000019,
        0x00000000, 0x505f6c67, 0x7469736f, 0x006e6f69, 0x00030005, 0x0000001b, 0x00000000, 0x00040005, 0x0000001c,
        0x736f5061, 0x00000000, 0x00060005, 0x0000001e, 0x73755075, 0x6e6f4368, 0x6e617473, 0x00000074, 0x00050006,
        0x0000001e, 0x00000000, 0x61635375, 0x0000656c, 0x00060006, 0x0000001e, 0x00000001, 0x61725475, 0x616c736e,
        0x00006574, 0x00030005, 0x00000020, 0x00006370, 0x00040047, 0x0000000b, 0x0000001e, 0x00000000, 0x00040047,
        0x0000000f, 0x0000001e, 0x00000002, 0x00040047, 0x00000015, 0x0000001e, 0x00000001, 0x00050048, 0x00000019,
        0x00000000, 0x0000000b, 0x00000000, 0x00030047, 0x00000019, 0x00000002, 0x00040047, 0x0000001c, 0x0000001e,
        0x00000000, 0x00050048, 0x0000001e, 0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x0000001e, 0x00000001,
        0x00000023, 0x00000008, 0x00030047, 0x0000001e, 0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
        0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040017,
        0x00000008, 0x00000006, 0x00000002, 0x0004001e, 0x00000009, 0x00000007, 0x00000008, 0x00040020, 0x0000000a,
        0x00000003, 0x00000009, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000003, 0x00040015, 0x0000000c, 0x00000020,
        0x00000001, 0x0004002b, 0x0000000c, 0x0000000d, 0x00000000, 0x00040020, 0x0000000e, 0x00000001, 0x00000007,
        0x0004003b, 0x0000000e, 0x0000000f, 0x00000001, 0x00040020, 0x00000011, 0x00000003, 0x00000007, 0x0004002b,
        0x0000000c, 0x00000013, 0x00000001, 0x00040020, 0x00000014, 0x00000001, 0x00000008, 0x0004003b, 0x00000014,
        0x00000015, 0x00000001, 0x00040020, 0x00000017, 0x00000003, 0x00000008, 0x0003001e, 0x00000019, 0x00000007,
        0x00040020, 0x0000001a, 0x00000003, 0x00000019, 0x0004003b, 0x0000001a, 0x0000001b, 0x00000003, 0x0004003b,
        0x00000014, 0x0000001c, 0x00000001, 0x0004001e, 0x0000001e, 0x00000008, 0x00000008, 0x00040020, 0x0000001f,
        0x00000009, 0x0000001e, 0x0004003b, 0x0000001f, 0x00000020, 0x00000009, 0x00040020, 0x00000021, 0x00000009,
        0x00000008, 0x0004002b, 0x00000006, 0x00000028, 0x00000000, 0x0004002b, 0x00000006, 0x00000029, 0x3f800000,
        0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007,
        0x00000010, 0x0000000f, 0x00050041, 0x00000011, 0x00000012, 0x0000000b, 0x0000000d, 0x0003003e, 0x00000012,
        0x00000010, 0x0004003d, 0x00000008, 0x00000016, 0x00000015, 0x00050041, 0x00000017, 0x00000018, 0x0000000b,
        0x00000013, 0x0003003e, 0x00000018, 0x00000016, 0x0004003d, 0x00000008, 0x0000001d, 0x0000001c, 0x00050041,
        0x00000021, 0x00000022, 0x00000020, 0x0000000d, 0x0004003d, 0x00000008, 0x00000023, 0x00000022, 0x00050085,
        0x00000008, 0x00000024, 0x0000001d, 0x00000023, 0x00050041, 0x00000021, 0x00000025, 0x00000020, 0x00000013,
        0x0004003d, 0x00000008, 0x00000026, 0x00000025, 0x00050081, 0x00000008, 0x00000027, 0x00000024, 0x00000026,
        0x00050051, 0x00000006, 0x0000002a, 0x00000027, 0x00000000, 0x00050051, 0x00000006, 0x0000002b, 0x00000027,
        0x00000001, 0x00070050, 0x00000007, 0x0000002c, 0x0000002a, 0x0000002b, 0x00000028, 0x00000029, 0x00050041,
        0x00000011, 0x0000002d, 0x0000001b, 0x0000000d, 0x0003003e, 0x0000002d, 0x0000002c, 0x000100fd, 0x00010038,
    };

    // backends/vulkan/glsl_shader.frag, compiled with:
    // # glslangValidator -V -x -o glsl_shader.frag.u32 glsl_shader.frag
    /*
    #version 450 core
    layout(location = 0) out vec4 fColor;
    layout(set=0, binding=0) uniform sampler2D sTexture;
    layout(location = 0) in struct { vec4 Color; vec2 UV; } In;
    void main()
    {
        fColor = In.Color * texture(sTexture, In.UV.st);
    }
    */
    constexpr std::array<uint32_t, 193> FRAGMENT_SHADER_SPIRV = {
        0x07230203, 0x00010000, 0x00080001, 0x0000001e, 0x00000000, 0x00020011, 0x00000001, 0x0006000b, 0x00000001,
        0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
        0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000d, 0x00030010, 0x00000004, 0x00000007, 0x00030003,
        0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00040005, 0x00000009, 0x6c6f4366,
        0x0000726f, 0x00030005, 0x0000000b, 0x00000000, 0x00050006, 0x0000000b, 0x00000000, 0x6f6c6f43, 0x00000072,
        0x00040006, 0x0000000b, 0x00000001, 0x00005655, 0x00030005, 0x0000000d, 0x00006e49, 0x00050005, 0x00000016,
        0x78655473, 0x65727574, 0x00000000, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000d,
        0x0000001e, 0x00000000, 0x00040047, 0x00000016, 0x00000022, 0x00000000, 0x00040047, 0x00000016, 0x00000021,
        0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
        0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b,
        0x00000008, 0x00000009, 0x00000003, 0x00040017, 0x0000000a, 0x00000006, 0x00000002, 0x0004001e, 0x0000000b,
        0x00000007, 0x0000000a, 0x00040020, 0x0000000c, 0x00000001, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d,
        0x00000001, 0x00040015, 0x0000000e, 0x00000020, 0x00000001, 0x0004002b, 0x0000000e, 0x0000000f, 0x00000000,
        0x00040020, 0x00000010, 0x00000001, 0x00000007, 0x00090019, 0x00000013, 0x00000006, 0x00000001, 0x00000000,
        0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001b, 0x00000014, 0x00000013, 0x00040020, 0x00000015,
        0x00000000, 0x00000014, 0x0004003b, 0x00000015, 0x00000016, 0x00000000, 0x0004002b, 0x0000000e, 0x00000018,
        0x00000001, 0x00040020, 0x00000019, 0x00000001, 0x0000000a, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
        0x00000003, 0x000200f8, 0x00000005, 0x00050041, 0x00000010, 0x00000011, 0x0000000d, 0x0000000f, 0x0004003d,
        0x00000007, 0x00000012, 0x00000011, 0x0004003d, 0x00000014, 0x00000017, 0x00000016, 0x00050041, 0x00000019,
        0x0000001a, 0x0000000d, 0x00000018, 0x0004003d, 0x0000000a, 0x0000001b, 0x0000001a, 0x00050057, 0x00000007,
        0x0000001c, 0x00000017, 0x0000001b, 0x00050085, 0x00000007, 0x0000001d, 0x00000012, 0x0000001c, 0x0003003e,
        0x00000009, 0x0000001d, 0x000100fd, 0x00010038,
    };
} // namespace constants

namespace functions
{
    namespace variables
    {
        struct PushConstants
        {
                float scaleX;
                float scaleY;

                float translateX;
                float translateY;
        } pushConstants{};
    } // namespace variables

    // Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
    // It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
    // FIXME: multi-context support is not tested and probably dysfunctional in this backend.
    inline typedefs::ImGui_ImplLuna_Data *ImGui_ImplLuna_GetBackendData()
    {
        return ImGui::GetCurrentContext() != nullptr
                       ? static_cast<typedefs::ImGui_ImplLuna_Data *>(ImGui::GetIO().BackendRendererUserData)
                       : nullptr;
    }

    inline void CheckVkResult(const VkResult result)
    {
        const typedefs::ImGui_ImplLuna_Data *backendData = ImGui_ImplLuna_GetBackendData();
        if (backendData == nullptr)
        {
            return;
        }
        if (backendData->lunaInitInfo.CheckVkResultFn != nullptr)
        {
            backendData->lunaInitInfo.CheckVkResultFn(result);
        }
    }

    // Same as IM_MEMALIGN(). 'alignment' must be a power of two.
    inline VkDeviceSize AlignBufferSize(const VkDeviceSize size, const VkDeviceSize alignment)
    {
        return size + alignment - 1 & ~(alignment - 1);
    }

    void CreateOrResizeBuffer(LunaBuffer &buffer, const VkDeviceSize newSize, const VkBufferUsageFlags usage)
    {
        if (buffer != LUNA_NULL_HANDLE)
        {
            IM_ASSERT((lunaBufferGetCreationInfo(buffer).usage & usage) == usage);
            CheckVkResult(lunaResizeBuffer(&buffer, newSize));
        } else
        {
            const LunaBufferCreationInfo bufferInfo = {
                .size = newSize,
                .usage = usage,
            };
            CheckVkResult(lunaCreateBuffer(&bufferInfo, &buffer));
        }
    }

    void ImGui_ImplLuna_SetupRenderState(const LunaGraphicsPipeline pipeline, const ImDrawData *drawData)
    {
        /*
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // Bind Vertex And Index Buffer:
        if (drawData->TotalVtxCount > 0)
        {
            constexpr VkDeviceSize vertexOffset = 0;
            const ImGui_ImplLuna_Data *backendData = ImGui_ImplLuna_GetBackendData();
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &backendData->vertexBuffer, &vertexOffset);
            vkCmdBindIndexBuffer(commandBuffer,
                                 backendData->indexBuffer,
                                 0,
                                 sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        }

        // Setup viewport:
        const VkViewport viewport = {
            .width = static_cast<float>(framebufferWidth),
            .height = static_cast<float>(framebufferHeight),
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        */

        // Setup scale and translation:
        // Our visible imgui space lies from draw_data->DisplayPps (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
        variables::pushConstants.scaleX = 2 * 1.0f / drawData->DisplaySize.x;
        variables::pushConstants.scaleY = 2 * 1.0f / drawData->DisplaySize.y;
        variables::pushConstants.translateX = -1.0f - drawData->DisplayPos.x * variables::pushConstants.scaleX;
        variables::pushConstants.translateY = -1.0f - drawData->DisplayPos.y * variables::pushConstants.scaleY;
        CheckVkResult(lunaPushConstants(pipeline));
    }

    void ImGui_ImplLuna_DestroyTexture(ImTextureData *texture)
    {
        using ImGui_ImplLuna_Texture = typedefs::ImGui_ImplLuna_Texture;
        ImGui_ImplLuna_Texture *backendTexture = static_cast<ImGui_ImplLuna_Texture *>(texture->BackendUserData);
        if (backendTexture == nullptr)
        {
            return;
        }
        IM_ASSERT(reinterpret_cast<ImTextureID>(backendTexture->descriptorSet) == texture->TexID);
        ImGui_ImplLuna_RemoveTexture(backendTexture->descriptorSet);
        lunaDestroyImage(backendTexture->image);
        IM_DELETE(backendTexture);

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        texture->SetTexID(ImTextureID_Invalid);
        texture->SetStatus(ImTextureStatus_Destroyed);
        texture->BackendUserData = nullptr;
    }

    void ImGui_ImplLuna_CreatePipeline(VkSampleCountFlagBits msaaSamples, LunaRenderPassSubpass subpass)
    {
        typedefs::ImGui_ImplLuna_Data *backendData = ImGui_ImplLuna_GetBackendData();
        if (backendData->vertexShaderModule == LUNA_NULL_HANDLE)
        {
            const LunaShaderModuleCreationInfo creationInfo = {
                .size = constants::VERTEX_SHADER_SPIRV.size() * sizeof(constants::VERTEX_SHADER_SPIRV.at(0)),
                .spirv = constants::VERTEX_SHADER_SPIRV.data(),
            };
            CheckVkResult(lunaCreateShaderModule(&creationInfo, &backendData->vertexShaderModule));
        }
        if (backendData->fragmentShaderModule == LUNA_NULL_HANDLE)
        {
            const LunaShaderModuleCreationInfo creationInfo = {
                .size = constants::FRAGMENT_SHADER_SPIRV.size() * sizeof(constants::FRAGMENT_SHADER_SPIRV.at(0)),
                .spirv = constants::FRAGMENT_SHADER_SPIRV.data(),
            };
            CheckVkResult(lunaCreateShaderModule(&creationInfo, &backendData->fragmentShaderModule));
        }

        const std::array<LunaPipelineShaderStageCreationInfo, 2> shaderStages = {
            LunaPipelineShaderStageCreationInfo{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = backendData->vertexShaderModule,
            },
            LunaPipelineShaderStageCreationInfo{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = backendData->fragmentShaderModule,
            },
        };

        constexpr VkVertexInputBindingDescription inputBindingDescription = {
            .stride = sizeof(ImDrawVert),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        constexpr std::array<VkVertexInputAttributeDescription, 3> vertexAttributeDescriptions = {
            VkVertexInputAttributeDescription{
                .location = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(ImDrawVert, pos),
            },
            VkVertexInputAttributeDescription{
                .location = 1,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(ImDrawVert, uv),
            },
            VkVertexInputAttributeDescription{
                .location = 2,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .offset = offsetof(ImDrawVert, col),
            },
        };

        const VkPipelineVertexInputStateCreateInfo vertexInput = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &inputBindingDescription,
            .vertexAttributeDescriptionCount = vertexAttributeDescriptions.size(),
            .pVertexAttributeDescriptions = vertexAttributeDescriptions.data(),
        };

        constexpr VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        constexpr VkPipelineViewportStateCreateInfo viewportState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        constexpr VkPipelineRasterizationStateCreateInfo rasterizer = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        };

        const VkPipelineMultisampleStateCreateInfo multisampling = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = msaaSamples != 0 ? msaaSamples : VK_SAMPLE_COUNT_1_BIT,
        };

        constexpr VkPipelineDepthStencilStateCreateInfo depthStencil = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        };

        constexpr VkPipelineColorBlendAttachmentState colorBlendAttachment = {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                              VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };

        const VkPipelineColorBlendStateCreateInfo colorBlending = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
        };

        constexpr std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = dynamicStates.size(),
            .pDynamicStates = dynamicStates.data(),
        };

        constexpr LunaPushConstantsRange pushConstantsRange = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(variables::pushConstants),
            .dataPointer = &variables::pushConstants,
        };
        const LunaPipelineLayoutCreationInfo layoutCreationInfo = {
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &backendData->descriptorSetLayout,
            .pushConstantRangeCount = 1,
            .pushConstantsRanges = &pushConstantsRange,
        };

        const LunaGraphicsPipelineCreationInfo pipelineCreationInfo = {
            .shaderStageCount = 2,
            .shaderStages = shaderStages.data(),
            .vertexInputState = &vertexInput,
            .inputAssemblyState = &inputAssembly,
            .viewportState = &viewportState,
            .rasterizationState = &rasterizer,
            .multisampleState = &multisampling,
            .depthStencilState = &depthStencil,
            .colorBlendState = &colorBlending,
            .dynamicState = &dynamicState,
            .layoutCreationInfo = layoutCreationInfo,
            .subpass = subpass,
        };

        CheckVkResult(lunaCreateGraphicsPipeline(&pipelineCreationInfo, &backendData->pipeline));
    }

    void ImGui_ImplLuna_CreateDeviceObjects()
    {
        typedefs::ImGui_ImplLuna_Data *backendData = ImGui_ImplLuna_GetBackendData();
        const ImGui_ImplLuna_InitInfo *initInfo = &backendData->lunaInitInfo;

        if (backendData->textureSampler == nullptr)
        {
            // Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling.
            constexpr LunaSamplerCreationInfo samplerCreateInfo = {
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .maxAnisotropy = 1.0f,
                .minLod = -1000.0f,
                .maxLod = 1000.0f,
            };
            CheckVkResult(lunaCreateSampler(&samplerCreateInfo, &backendData->textureSampler));
        }

        if (backendData->descriptorSetLayout == nullptr)
        {
            constexpr LunaDescriptorSetLayoutBinding binding = {
                .bindingName = "Texture",
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            };
            const LunaDescriptorSetLayoutCreationInfo descriptorSetLayoutCreationInfo = {
                .bindingCount = 1,
                .bindings = &binding,
            };
            CheckVkResult(lunaCreateDescriptorSetLayout(&descriptorSetLayoutCreationInfo,
                                                        &backendData->descriptorSetLayout));
        }

        if (initInfo->descriptorPoolSize != 0)
        {
            IM_ASSERT(initInfo->descriptorPoolSize >= IMGUI_IMPL_LUNA_MINIMUM_IMAGE_SAMPLER_POOL_SIZE);

            const VkDescriptorPoolSize poolSize = {
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                initInfo->descriptorPoolSize,
            };
            const LunaDescriptorPoolCreationInfo descriptorPoolCreationInfo = {
                .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets = initInfo->descriptorPoolSize,
                .poolSizeCount = 1,
                .poolSizes = &poolSize,
            };

            CheckVkResult(lunaCreateDescriptorPool(&descriptorPoolCreationInfo, &backendData->descriptorPool));
        }

        ImGui_ImplLuna_CreatePipeline(initInfo->msaaSamples, initInfo->renderPassSubpass);

        // Create command pool/buffer for texture upload
        // if (backendData->textureCommandPool == nullptr)
        // {
        //     constexpr LunaCommandPoolCreationInfo commandPoolCreationInfo = {
        //         .requiredQueueFlags = VK_QUEUE_TRANSFER_BIT,
        //     };
        //     CheckVkResult(lunaCreateCommandPool(&commandPoolCreationInfo, &backendData->textureCommandPool));
        // }
        // if (backendData->textureCommandBuffer == nullptr)
        // {
        //     VkCommandBufferAllocateInfo info = {};
        //     info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        //     info.commandPool = backendData->textureCommandPool;
        //     info.commandBufferCount = 1;
        //     CheckVkResult(vkAllocateCommandBuffers(initInfo->Device, &info, &backendData->textureCommandBuffer));
        // }
    }
} // namespace functions

using namespace typedefs;
using namespace constants;
using namespace functions;
} // namespace

bool ImGui_ImplLuna_Init(ImGui_ImplLuna_InitInfo *initInfo)
{
    IM_ASSERT(initInfo->renderPassSubpass != LUNA_NULL_HANDLE);
    IM_ASSERT(initInfo->useDescriptorPoolSize ? initInfo->descriptorPoolSize > 0
                                              : initInfo->descriptorPool != LUNA_NULL_HANDLE);
    if (initInfo->minImageCount == -1)
    {
        initInfo->minImageCount = 2;
    } else
    {
        IM_ASSERT(initInfo->minImageCount >= 2);
    }

    ImGuiIO &io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    ImGui_ImplLuna_Data *backendData = IM_NEW(ImGui_ImplLuna_Data)(*initInfo);
    io.BackendRendererUserData = static_cast<void *>(backendData);
    io.BackendRendererName = "imgui_impl_luna";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

    const VkPhysicalDeviceProperties properties = lunaGetPhysicalDeviceProperties();
    backendData->nonCoherentAtomSize = properties.limits.nonCoherentAtomSize;

    ImGui_ImplLuna_CreateDeviceObjects();

    return true;
}

void ImGui_ImplLuna_Shutdown()
{
    ImGui_ImplLuna_Data *bd = ImGui_ImplLuna_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO &io = ImGui::GetIO();

    CheckVkResult(lunaDestroyInstance()); // TODO: This isn't the correct behavior in many situations

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset |
                         ImGuiBackendFlags_RendererHasTextures |
                         ImGuiBackendFlags_RendererHasViewports);
    IM_DELETE(bd);
}

void ImGui_ImplLuna_RenderDrawData(ImDrawData *drawData, LunaGraphicsPipeline pipeline)
{
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    const float framebufferWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    const float framebufferHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        return;
    }

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
    if (drawData->Textures != nullptr)
    {
        for (ImTextureData *tex: *drawData->Textures)
        {
            if (tex->Status != ImTextureStatus_OK)
            {
                ImGui_ImplLuna_UpdateTexture(tex);
            }
        }
    }

    ImGui_ImplLuna_Data *bd = ImGui_ImplLuna_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplLuna_Init()?");
    if (pipeline == LUNA_NULL_HANDLE)
    {
        pipeline = bd->pipeline;
    }

    if (drawData->TotalVtxCount > 0)
    {
        const VkDeviceSize vertexSize = AlignBufferSize(drawData->TotalVtxCount * sizeof(ImDrawVert),
                                                        bd->bufferMemoryAlignment);
        if (bd->vertexBuffer == LUNA_NULL_HANDLE || bd->vertexBufferAllocatedSize < vertexSize)
        {
            CreateOrResizeBuffer(bd->vertexBuffer, vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            bd->vertexBufferAllocatedSize = vertexSize;
        }
        const VkDeviceSize indexSize = AlignBufferSize(drawData->TotalIdxCount * sizeof(ImDrawIdx),
                                                       bd->bufferMemoryAlignment);
        if (bd->indexBuffer == LUNA_NULL_HANDLE || bd->indexBufferAllocatedSize < indexSize)
        {
            CreateOrResizeBuffer(bd->indexBuffer, indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            bd->indexBufferAllocatedSize = indexSize;
        }

        size_t vertexOffset = 0;
        size_t indexOffset = 0;
        for (const ImDrawList *draw_list: drawData->CmdLists)
        {
            const size_t vertexBytes = draw_list->VtxBuffer.Size * sizeof(ImDrawVert);
            const size_t indexBytes = draw_list->IdxBuffer.Size * sizeof(ImDrawIdx);
            lunaWriteDataToBuffer(bd->vertexBuffer, draw_list->VtxBuffer.Data, vertexBytes, vertexOffset);
            lunaWriteDataToBuffer(bd->indexBuffer, draw_list->IdxBuffer.Data, indexBytes, indexOffset);
            vertexOffset += vertexBytes;
            indexOffset += indexBytes;
        }
    }

    // Setup desired Vulkan state
    ImGui_ImplLuna_SetupRenderState(pipeline, drawData);

    // Setup render state structure (for callbacks and custom texture bindings)
    ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();

    // Will project scissor/clipping rectangles into framebuffer space
    const ImVec2 clip_off = drawData->DisplayPos; // (0,0) unless using multi-viewports
    const ImVec2 clip_scale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    LunaDescriptorSet lastDescriptorSet = LUNA_NULL_HANDLE;
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    for (const ImDrawList *draw_list: drawData->CmdLists)
    {
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd *pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                // NOLINTNEXTLINE(*-pro-type-cstyle-cast, *-no-int-to-ptr)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                {
                    // TODO: This isn't the same functionality as the Vulkan impl
                    ImGui_ImplLuna_SetupRenderState(pipeline, drawData);
                } else
                {
                    pcmd->UserCallback(draw_list, pcmd);
                }
                lastDescriptorSet = LUNA_NULL_HANDLE;
            } else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
                if (clip_min.x < 0.0f)
                {
                    clip_min.x = 0.0f;
                }
                if (clip_min.y < 0.0f)
                {
                    clip_min.y = 0.0f;
                }
                if (clip_max.x > framebufferWidth)
                {
                    clip_max.x = static_cast<float>(framebufferWidth);
                }
                if (clip_max.y > framebufferHeight)
                {
                    clip_max.y = static_cast<float>(framebufferHeight);
                }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                {
                    continue;
                }

                // Bind DescriptorSet with font or user texture
                LunaDescriptorSet descriptorSet = reinterpret_cast<LunaDescriptorSet>(pcmd->GetTexID());
                if (descriptorSet != lastDescriptorSet)
                {
                    const LunaDescriptorSetBindInfo descriptorSetBindInfo = {
                        .descriptorSetCount = 1,
                        .descriptorSets = &descriptorSet,
                    };
                    lunaBindDescriptorSets(bd->pipeline, &descriptorSetBindInfo);
                }
                lastDescriptorSet = descriptorSet;


                const VkViewport viewport = {
                    .width = static_cast<float>(framebufferWidth),
                    .height = static_cast<float>(framebufferHeight),
                    .maxDepth = 1.0f,
                };
                const LunaViewportBindInfo viewportBindInfo = {
                    .viewportCount = 1,
                    .viewports = &viewport,
                };
                VkRect2D scissor{};
                scissor.offset.x = static_cast<int32_t>(clip_min.x);
                scissor.offset.y = static_cast<int32_t>(clip_min.y);
                scissor.extent.width = static_cast<uint32_t>(clip_max.x - clip_min.x);
                scissor.extent.height = static_cast<uint32_t>(clip_max.y - clip_min.y);
                const LunaScissorBindInfo scissorBindInfo = {
                    .scissorCount = 1,
                    .scissors = &scissor,
                };
                const std::array<LunaDynamicStateBindInfo, 2> dynamicStateBindInfos = {
                    LunaDynamicStateBindInfo{
                        .dynamicStateType = VK_DYNAMIC_STATE_VIEWPORT,
                        .bindInfo = {.viewportBindInfo = &viewportBindInfo},
                    },
                    LunaDynamicStateBindInfo{
                        .dynamicStateType = VK_DYNAMIC_STATE_SCISSOR,
                        .bindInfo = {.scissorBindInfo = &scissorBindInfo},
                    },
                };
                const LunaGraphicsPipelineBindInfo pipelineBindInfo = {
                    .dynamicStateCount = dynamicStateBindInfos.size(),
                    .dynamicStates = dynamicStateBindInfos.data(),
                };
                CheckVkResult(lunaDrawBufferIndexed(bd->vertexBuffer,
                                                    bd->indexBuffer,
                                                    sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16
                                                                           : VK_INDEX_TYPE_UINT32,
                                                    pipeline,
                                                    &pipelineBindInfo,
                                                    pcmd->ElemCount,
                                                    1,
                                                    pcmd->IdxOffset + global_idx_offset,
                                                    static_cast<int32_t>(pcmd->VtxOffset) + global_vtx_offset,
                                                    0));
            }
        }
        global_idx_offset += draw_list->IdxBuffer.Size;
        global_vtx_offset += draw_list->VtxBuffer.Size;
    }
    platform_io.Renderer_RenderState = nullptr;
}

void ImGui_ImplLuna_UpdateTexture(ImTextureData *textureData)
{
    if (textureData->Status == ImTextureStatus_OK)
    {
        return;
    }

    if (textureData->Status == ImTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        //IMGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
        IM_ASSERT(textureData->TexID == ImTextureID_Invalid && textureData->BackendUserData == nullptr);
        IM_ASSERT(textureData->Format == ImTextureFormat_RGBA32);
        ImGui_ImplLuna_Texture *backendTexture = IM_NEW(ImGui_ImplLuna_Texture)(); // TODO: This is leaked
        const LunaSampler textureSampler = ImGui_ImplLuna_GetBackendData()->textureSampler;

        // Create the Image:
        const LunaSampledImageCreationInfo imageCreationInfo = {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .width = static_cast<uint32_t>(textureData->Width),
            .height = static_cast<uint32_t>(textureData->Height),
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .sampler = textureSampler,
        };
        CheckVkResult(lunaCreateImage(&imageCreationInfo, &backendTexture->image));

        // Create the Descriptor Set
        backendTexture->descriptorSet = ImGui_ImplLuna_AddTexture(textureSampler,
                                                                  backendTexture->image,
                                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Store identifiers
        textureData->SetTexID(reinterpret_cast<ImTextureID>(backendTexture->descriptorSet));
        textureData->BackendUserData = backendTexture;
    }

    if (textureData->Status == ImTextureStatus_WantCreate || textureData->Status == ImTextureStatus_WantUpdates)
    {
        // Update full texture or selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->UpdateRect but you can use tex->Updates[] to upload individual regions.
        // We could use the smaller rect on _WantCreate but using the full rect allows us to clear the texture.
        const VkOffset3D offset = {
            .x = textureData->Status == ImTextureStatus_WantCreate ? 0 : textureData->UpdateRect.x,
            .y = textureData->Status == ImTextureStatus_WantCreate ? 0 : textureData->UpdateRect.y,
        };
        const uint32_t width = textureData->Status == ImTextureStatus_WantCreate ? textureData->Width
                                                                                 : textureData->UpdateRect.w;
        const uint32_t height = textureData->Status == ImTextureStatus_WantCreate ? textureData->Height
                                                                                  : textureData->UpdateRect.h;

        const VkDeviceSize bytesPerLine = width * textureData->BytesPerPixel;

        std::vector<uint8_t> pixels(bytesPerLine * height);
        for (int y = 0; y < height; y++)
        {
            uint8_t *ptr = static_cast<uint8_t *>(textureData->GetPixelsAt(offset.x, offset.y + y));
            pixels.insert(pixels.begin() + static_cast<ptrdiff_t>(bytesPerLine * y), ptr, ptr + bytesPerLine);
        }

        const VkExtent3D extent = {
            .width = width,
            .height = height,
        };
        const LunaImageWriteInfo imageWriteInfo = {
            .bytes = pixels.size(),
            .pixels = pixels.data(),
            .offset = &offset,
            .extent = &extent,
            .destinationStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        };
        IM_ASSERT(textureData->BackendUserData != nullptr);
        const LunaImage image = static_cast<ImGui_ImplLuna_Texture *>(textureData->BackendUserData)->image;
        CheckVkResult(lunaUpdateImage(image, &imageWriteInfo));
        textureData->SetStatus(ImTextureStatus_OK);
    }

    if (textureData->Status == ImTextureStatus_WantDestroy &&
        textureData->UnusedFrames >= 0 /*static_cast<int>(backendData->lunaInitInfo.ImageCount)*/) // TODO: This
    {
        ImGui_ImplLuna_DestroyTexture(textureData);
    }
}

// Register a texture by creating a descriptor
// FIXME: This is experimental in the sense that we are unsure how to best design/tackle this problem, please post to https://github.com/ocornut/imgui/pull/914 if you have suggestions.
LunaDescriptorSet ImGui_ImplLuna_AddTexture(const LunaSampler sampler,
                                            const LunaImage image,
                                            const VkImageLayout imageLayout)
{
    const ImGui_ImplLuna_Data *backendData = ImGui_ImplLuna_GetBackendData();
    const LunaDescriptorPool pool = backendData->descriptorPool != nullptr ? backendData->descriptorPool
                                                                           : backendData->lunaInitInfo.descriptorPool;

    LunaDescriptorSet descriptorSet = LUNA_NULL_HANDLE;
    const LunaDescriptorSetAllocationInfo allocationInfo = {
        .descriptorPool = pool,
        .descriptorSetCount = 1,
        .setLayouts = &backendData->descriptorSetLayout,
    };
    CheckVkResult(lunaAllocateDescriptorSets(&allocationInfo, &descriptorSet));

    const LunaDescriptorImageInfo descriptorImageInfo = {
        .sampler = sampler,
        .image = image,
        .imageLayout = imageLayout,
    };
    const LunaWriteDescriptorSet descriptorWrite = {
        .descriptorSet = descriptorSet,
        .bindingName = "Texture",
        .descriptorCount = 1,
        .imageInfo = &descriptorImageInfo,
    };
    lunaWriteDescriptorSets(1, &descriptorWrite);
    return descriptorSet;
}

void ImGui_ImplLuna_RemoveTexture(const LunaDescriptorSet descriptorSet)
{
    lunaDestroyDescriptorSet(descriptorSet);
}

#endif // #ifndef IMGUI_DISABLE
