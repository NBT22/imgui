#include <array>
#include <cstdio> // printf, fprintf
#include <cstdlib> // abort
#include <iostream>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include "imgui.h"
#include "imgui_impl_luna.h"
#include "imgui_impl_sdl3.h"

static bool g_SwapChainRebuild = false;

static void CheckVkResult(const VkResult err)
{
    if (err == VK_SUCCESS)
    {
        return;
    }
    std::cerr << "[vulkan] Error: VkResult = " << err << '\n';
    if (err < 0)
    {
        abort();
    }
}

static void CleanupVulkan()
{
    // CheckVkResult(lunaDestroyInstance());
}

static void FrameRender(const uint32_t width,
                        const uint32_t height,
                        const VkClearValue *clearValue,
                        const LunaRenderPass renderPass,
                        ImDrawData *drawData)
{
    const LunaRenderPassBeginInfo renderPassBeginInfo = {
        .renderArea = {.extent = {width, height}},
        .colorAttachmentClearValue = *clearValue,
        .allowSuboptimalSwapchain = true,
    };
    CheckVkResult(lunaBeginRenderPass(renderPass, &renderPassBeginInfo));

    ImGui_ImplLuna_RenderDrawData(drawData);

    lunaEndRenderPass();
}

static void FramePresent()
{
    if (g_SwapChainRebuild)
    {
        return;
    }
    const VkResult err = lunaPresentSwapchain();
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
    {
        g_SwapChainRebuild = true;
    }
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return;
    }
    if (err != VK_SUBOPTIMAL_KHR)
    {
        CheckVkResult(err);
    }
}

// Main code
int main(const int argc, const char *argv[])
{
    for (int i = 0; i < argc; i++)
    {
        if (strncmp(argv[i], "-x11", 4) == 0)
        {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11,wayland");
            break;
        }
    }
    // Setup SDL
    // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit() function]
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        std::cout << "Error: SDL_Init(): " << SDL_GetError() << '\n';
        return -1;
    }

    // Create window with Vulkan graphics context
    const float mainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    int width = static_cast<int>(1280 * mainScale);
    int height = static_cast<int>(720 * mainScale);
    constexpr SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN |
                                            SDL_WINDOW_RESIZABLE |
                                            SDL_WINDOW_HIDDEN |
                                            SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *window = SDL_CreateWindow("Dear ImGui SDL3+Luna example", width, height, windowFlags);
    if (window == nullptr)
    {
        std::cout << "Error: SDL_CreateWindow(): " << SDL_GetError() << '\n';
        return -1;
    }

    CheckVkResult(lunaInitializeVolk());

    uint32_t instanceExtensionCount = 0;
    const char *const *instanceExtensionsPtr = SDL_Vulkan_GetInstanceExtensions(&instanceExtensionCount);
    std::vector<const char *> instanceExtensions(instanceExtensionsPtr, instanceExtensionsPtr + instanceExtensionCount);
    LunaInstanceCreationInfo instanceCreationInfo = {
        .apiVersion = VK_HEADER_VERSION_COMPLETE,
        .enableValidation = true,
    };
    if (lunaIsInstanceExtensionAvailable(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
    {
        instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }
    if (lunaIsInstanceExtensionAvailable(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instanceCreationInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    instanceCreationInfo.extensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instanceCreationInfo.extensionNames = instanceExtensions.data();
    CheckVkResult(lunaCreateInstance(&instanceCreationInfo));
    // SetupVulkan(instanceExtensions);

    // Create Window Surface
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, lunaGetInstance(), nullptr, &surface))
    {
        std::cout << "Failed to create Vulkan surface." << '\n';
        return 1;
    }

    constexpr const char *extensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    constexpr LunaPhysicalDevicePreferenceDefinition physicalDevicePreferenceDefinition = {
        .preferredDeviceType = VK_PHYSICAL_DEVICE_TYPE_CPU,
    };
    const LunaDeviceCreationInfo deviceCreationInfo = {
        .extensionCount = 1,
        .extensionNames = &extensionName,
        .surface = surface,
        .physicalDevicePreferenceDefinition = &physicalDevicePreferenceDefinition,
    };
    CheckVkResult(lunaAddNewDevice(&deviceCreationInfo));

    // Create Framebuffers
    const VkExtent3D extent = {
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .depth = 1,
    };
    constexpr std::array<VkSurfaceFormatKHR, 4> formatPriorityList = {
        VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR},
        VkSurfaceFormatKHR{VK_FORMAT_R8G8B8A8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR},
        VkSurfaceFormatKHR{VK_FORMAT_B8G8R8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR},
        VkSurfaceFormatKHR{VK_FORMAT_R8G8B8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR},
    };
    constexpr std::array<VkPresentModeKHR, 1> presentModePriorityList = {
        VK_PRESENT_MODE_FIFO_KHR,
    };
    const LunaSwapchainCreationInfo swapchainCreationInfo = {
        .surface = surface,
        .width = extent.width,
        .height = extent.height,
        .formatCount = formatPriorityList.size(),
        .formatPriorityList = formatPriorityList.data(),
        .presentModeCount = presentModePriorityList.size(),
        .presentModePriorityList = presentModePriorityList.data(),
    };
    CheckVkResult(lunaCreateSwapchain(&swapchainCreationInfo));
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    constexpr VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    constexpr LunaSubpassCreationInfo subpassCreationInfo = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .useColorAttachment = true,
    };
    const LunaRenderPassCreationInfo renderPassCreationInfo = {
        .createColorAttachment = true,
        .colorAttachmentLoadMode = LUNA_ATTACHMENT_LOAD_CLEAR,
        .subpassCount = 1,
        .subpasses = &subpassCreationInfo,
        .dependencyCount = 1,
        .dependencies = &dependency,
        .extent = extent,
    };
    LunaRenderPass renderPass = LUNA_NULL_HANDLE;
    CheckVkResult(lunaCreateRenderPass(&renderPassCreationInfo, &renderPass));

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle &style = ImGui::GetStyle();
    // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.ScaleAllSizes(mainScale);
    // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)
    style.FontScaleDpi = mainScale;

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplLuna_InitInfo initInfo{};
    initInfo.renderPassSubpass = lunaGetRenderPassSubpassByName(renderPass, nullptr);
    initInfo.descriptorPoolSize = IMGUI_IMPL_LUNA_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    initInfo.useDescriptorPoolSize = true;
    initInfo.CheckVkResultFn = CheckVkResult;
    ImGui_ImplLuna_Init(&initInfo);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        // [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
            {
                done = true;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
            {
                done = true;
            }
        }

        // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) == SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        SDL_GetWindowSize(window, &width, &height);
        if (width > 0 && height > 0 && g_SwapChainRebuild)
        {
            const LunaRenderPassResizeInfo renderPassResizeInfo = {
                .renderPass = renderPass,
                .width = static_cast<uint32_t>(width),
                .height = static_cast<uint32_t>(height),
            };
            CheckVkResult(lunaResizeSwapchain(1, &renderPassResizeInfo, nullptr, nullptr));
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
        {
            ImGui::ShowDemoWindow(&show_demo_window);
        }

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

            ImGui::TextUnformatted("This is some useful text."); // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window); // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f); // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color",
                              reinterpret_cast<float *>(&clearColor)); // Edit 3 floats representing a color

            if (ImGui::Button(
                        "Button")) // Buttons return true when clicked (most widgets return true when edited/activated)
            {
                counter++;
            }
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin(
                    "Another Window",
                    &show_another_window); // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::TextUnformatted("Hello from another window!");
            if (ImGui::Button("Close Me"))
            {
                show_another_window = false;
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        ImDrawData *drawData = ImGui::GetDrawData();
        if (drawData->DisplaySize.x > 0.0f && drawData->DisplaySize.y > 0.0f)
        {
            VkClearValue clearValue{};
            clearValue.color.float32[0] = clearColor.x * clearColor.w;
            clearValue.color.float32[1] = clearColor.y * clearColor.w;
            clearValue.color.float32[2] = clearColor.z * clearColor.w;
            clearValue.color.float32[3] = clearColor.w;
            FrameRender(width, height, &clearValue, renderPass, drawData);
            FramePresent();
        }
    }

    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    ImGui_ImplLuna_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    CleanupVulkan();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
