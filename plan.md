# Vulkan 1.4 in pseudocode

This is a deliberately abbreviated map of a small Vulkan renderer, inspired by
the pseudocode at the end of
[Vulkan in 30 minutes](https://renderdoc.org/vulkan-in-30-minutes.html).
It shows the order in which the important objects are created and used, while
omitting most structure members, platform-window details, shader contents,
capability scoring, recovery paths, and error handling.

This is **not copy-and-paste code**. Every `CHECK`, `REQUIRE`, `Choose...`,
`Create...`, and `Destroy...` operation stands in for code that a real
application must implement and validate.

## What makes this a Vulkan 1.4 outline

- Both the loader and selected physical device must support Vulkan 1.4.
- `VkPhysicalDeviceVulkan14Features::pushDescriptor` is enabled and
  `vkCmdPushDescriptorSet` is called as a core Vulkan 1.4 command.
- Dynamic rendering replaces `VkRenderPass` and `VkFramebuffer`.
- Synchronization 2 supplies `vkCmdPipelineBarrier2` and `vkQueueSubmit2`.
- Vulkan Memory Allocator (VMA) creates and binds resource memory; the
  application never directly calls `vkAllocateMemory`.
- `VmaAllocatorCreateInfo::vulkanApiVersion` is explicitly set to 1.4.

Dynamic rendering and Synchronization 2 entered core before Vulkan 1.4, but
they are part of the modern baseline inherited by a Vulkan 1.4 application.
Push descriptors are the feature in this outline that specifically entered
core in Vulkan 1.4.

## Suggested article milestones

1. Connect to the Vulkan 1.4 loader and create an instance.
2. Select a Vulkan 1.4 physical device and create a logical device.
3. Introduce VMA and allocate a first buffer.
4. Create the surface and swapchain.
5. Upload triangle data and create the per-frame uniform buffer.
6. Create a push-descriptor layout and dynamic-rendering pipeline.
7. Create command and synchronization objects.
8. Record, submit, and present a frame using Synchronization 2.
9. Handle resize and destroy everything in dependency order.

## Pseudocode

```cpp
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

// Pseudocode conventions:
//
// - Most Vk...CreateInfo members are omitted when they are not central to the
//   idea being illustrated.
// - CHECK means "check VkResult and report enough context to diagnose it".
// - REQUIRE means "stop with a useful error if the capability is unavailable".
// - The example uses one frame in flight to make ownership and synchronization
//   easy to see. A real renderer normally duplicates per-frame resources.
// - In production, wrap owned Vulkan handles in RAII types. Destruction is
//   written explicitly here to make the required lifetime order visible.


void RunVulkan14Triangle(Window window)
{
    // ========================================================================
    // MILESTONE 1 — CONNECT TO VULKAN 1.4
    //
    // Working checkpoint:
    //   Create and destroy a VkInstance without selecting a GPU.
    // ========================================================================

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    CHECK(vkEnumerateInstanceVersion(&loaderVersion));
    REQUIRE(loaderVersion >= VK_API_VERSION_1_4);

    VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Fire Engine Tutorial",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    // The window system supplies VK_KHR_surface plus one platform extension,
    // such as VK_KHR_win32_surface, VK_KHR_xcb_surface, or VK_EXT_metal_surface.
    List<const char*> instanceExtensions =
        window.RequiredVulkanInstanceExtensions();

    if (ValidationLayerIsAvailable())
    {
        instanceExtensions.Add(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo instanceInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = validationLayerCount,
        .ppEnabledLayerNames = validationLayerNames,
        .enabledExtensionCount = instanceExtensions.Count(),
        .ppEnabledExtensionNames = instanceExtensions.Data(),
    };

    VkInstance instance = VK_NULL_HANDLE;
    CHECK(vkCreateInstance(&instanceInfo, nullptr, &instance));

    VkDebugUtilsMessengerEXT debugMessenger =
        CreateDebugMessengerIfEnabled(instance);


    // ========================================================================
    // MILESTONE 2 — SELECT A VULKAN 1.4 DEVICE
    //
    // Working checkpoint:
    //   Print the selected GPU name, create VkDevice, and fetch its queues.
    // ========================================================================

    VkSurfaceKHR surface = window.CreateVulkanSurface(instance);

    List<VkPhysicalDevice> physicalDevices =
        EnumerateAllPhysicalDevices(instance);

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = INVALID_INDEX;
    uint32_t presentQueueFamily = INVALID_INDEX;

    for (VkPhysicalDevice candidate : physicalDevices)
    {
        VkPhysicalDeviceProperties baseProperties = {};
        vkGetPhysicalDeviceProperties(candidate, &baseProperties);

        // Requesting 1.4 from VkApplicationInfo is not sufficient: the chosen
        // physical device itself must report a 1.4 API version.
        if (baseProperties.apiVersion < VK_API_VERSION_1_4)
        {
            continue;
        }

        VkPhysicalDeviceVulkan14Properties properties14 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES,
        };

        VkPhysicalDeviceProperties2 properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &properties14,
        };

        vkGetPhysicalDeviceProperties2(candidate, &properties);

        VkPhysicalDeviceVulkan14Features supported14 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        };

        VkPhysicalDeviceVulkan13Features supported13 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &supported14,
        };

        VkPhysicalDeviceFeatures2 supportedFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &supported13,
        };

        vkGetPhysicalDeviceFeatures2(candidate, &supportedFeatures);

        if (!supported13.dynamicRendering ||
            !supported13.synchronization2 ||
            !supported14.pushDescriptor ||
            properties14.maxPushDescriptors < 1)
        {
            continue;
        }

        auto [graphicsFamily, presentFamily] =
            FindGraphicsAndPresentQueueFamilies(candidate, surface);

        if (!graphicsFamily || !presentFamily)
        {
            continue;
        }

        if (!DeviceExtensionIsAvailable(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            continue;
        }

        if (!SurfaceHasFormatsAndPresentModes(candidate, surface))
        {
            continue;
        }

        physicalDevice = candidate;
        graphicsQueueFamily = *graphicsFamily;
        presentQueueFamily = *presentFamily;
        break;
    }

    REQUIRE(physicalDevice != VK_NULL_HANDLE);

    List<VkDeviceQueueCreateInfo> queueInfos =
        MakeUniqueQueueCreateInfos(graphicsQueueFamily, presentQueueFamily);

    List<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkPhysicalDeviceVulkan14Features enabled14 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pushDescriptor = VK_TRUE,
    };

    VkPhysicalDeviceVulkan13Features enabled13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &enabled14,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkDeviceCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled13,
        .queueCreateInfoCount = queueInfos.Count(),
        .pQueueCreateInfos = queueInfos.Data(),
        .enabledExtensionCount = deviceExtensions.Count(),
        .ppEnabledExtensionNames = deviceExtensions.Data(),
    };

    // Do not enable VK_KHR_push_descriptor by name here. On a Vulkan 1.4
    // device it is core functionality, gated by enabled14.pushDescriptor.
    VkDevice device = VK_NULL_HANDLE;
    CHECK(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device));

    VkQueue graphicsQueue =
        GetDeviceQueue(device, graphicsQueueFamily, queueIndex = 0);
    VkQueue presentQueue =
        GetDeviceQueue(device, presentQueueFamily, queueIndex = 0);


    // ========================================================================
    // MILESTONE 3 — CREATE THE VULKAN MEMORY ALLOCATOR
    //
    // Working checkpoint:
    //   Create VmaAllocator after VkDevice and destroy it before VkDevice.
    // ========================================================================

    // Choose one VMA function-loading model in the real project. This abstract
    // helper represents either statically linked functions or pointers loaded
    // through vkGetInstanceProcAddr/vkGetDeviceProcAddr.
    // Define VMA_IMPLEMENTATION in exactly one real .cpp file (not shown).
    VmaVulkanFunctions vmaFunctions =
        ImportVulkanFunctions(instance, device);

    VmaAllocatorCreateInfo allocatorInfo = {
        .physicalDevice = physicalDevice,
        .device = device,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
        .pVulkanFunctions = &vmaFunctions,
    };

    VmaAllocator allocator = VK_NULL_HANDLE;
    CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));

    // VMA now chooses memory types, suballocates VkDeviceMemory, and binds
    // memory to buffers/images. It does not record uploads or synchronize them.


    // ========================================================================
    // MILESTONE 4 — CREATE THE SWAPCHAIN
    //
    // Working checkpoint:
    //   Acquire and present an image, initially without drawing to it.
    // ========================================================================

    SurfaceSupport support = QuerySurfaceSupport(physicalDevice, surface);
    VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = ChoosePresentMode(support.presentModes);
    VkExtent2D swapExtent = ChooseExtent(support.capabilities, window.Size());

    VkSwapchainCreateInfoKHR swapchainInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = ChooseImageCount(support.capabilities),
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = swapExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = ChooseSharingMode(
            graphicsQueueFamily,
            presentQueueFamily),
        .preTransform = support.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
    };

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    CHECK(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain));

    List<VkImage> swapchainImages =
        GetAllSwapchainImages(device, swapchain);

    List<VkImageView> swapchainViews;
    for (VkImage image : swapchainImages)
    {
        swapchainViews.Add(
            CreateImageView(device, image, surfaceFormat.format, COLOR_ASPECT));
    }

    // Swapchain images are owned by the swapchain. Do not create or destroy
    // VMA allocations for them.


    // ========================================================================
    // MILESTONE 5 — CREATE AND UPLOAD GPU RESOURCES WITH VMA
    //
    // Working checkpoint:
    //   Upload three vertices and update one persistently mapped uniform buffer.
    // ========================================================================

    TriangleVertex triangleVertices[3] = {
        // position, colour
        { {  0.0, -0.5 }, { 1.0, 0.0, 0.0 } },
        { {  0.5,  0.5 }, { 0.0, 1.0, 0.0 } },
        { { -0.5,  0.5 }, { 0.0, 0.0, 1.0 } },
    };

    // CPU-visible staging allocation.
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(triangleVertices),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    VmaAllocationCreateInfo stagingAllocationInfo = {
        .flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
    };

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo stagingDetails = {};

    CHECK(vmaCreateBuffer(
        allocator,
        &stagingBufferInfo,
        &stagingAllocationInfo,
        &stagingBuffer,
        &stagingAllocation,
        &stagingDetails));

    CopyBytes(
        destination = stagingDetails.pMappedData,
        source = triangleVertices,
        size = sizeof(triangleVertices));

    // Safe even when the selected memory is HOST_COHERENT; VMA performs the
    // required flush only when necessary.
    CHECK(vmaFlushAllocation(
        allocator,
        stagingAllocation,
        offset = 0,
        size = VK_WHOLE_SIZE));

    // Device-preferred vertex allocation.
    VkBufferCreateInfo vertexBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(triangleVertices),
        .usage =
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    };

    VmaAllocationCreateInfo vertexAllocationInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;

    CHECK(vmaCreateBuffer(
        allocator,
        &vertexBufferInfo,
        &vertexAllocationInfo,
        &vertexBuffer,
        &vertexAllocation,
        nullptr));

    // Allocate a temporary command buffer, copy staging -> device buffer, and
    // make the transfer write visible to vertex input using Synchronization 2.
    VkCommandBuffer uploadCommand = BeginOneTimeCommands(device);

    VkBufferCopy vertexCopy = {
        .size = sizeof(triangleVertices),
    };
    vkCmdCopyBuffer(
        uploadCommand,
        stagingBuffer,
        vertexBuffer,
        1,
        &vertexCopy);

    VkBufferMemoryBarrier2 vertexReady = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
        .buffer = vertexBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };

    VkDependencyInfo uploadDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &vertexReady,
    };
    vkCmdPipelineBarrier2(uploadCommand, &uploadDependency);

    EndSubmitAndWaitWithSubmit2(uploadCommand, graphicsQueue);

    // The transfer has completed, so the staging allocation is no longer used.
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    // Persistently mapped uniform buffer. With one frame in flight, one buffer
    // is sufficient; use one per frame when the renderer is expanded.
    VkBufferCreateInfo uniformBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(FrameUniforms),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    };

    VmaAllocationCreateInfo uniformAllocationInfo = {
        .flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
    };

    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VmaAllocation uniformAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo uniformDetails = {};

    CHECK(vmaCreateBuffer(
        allocator,
        &uniformBufferInfo,
        &uniformAllocationInfo,
        &uniformBuffer,
        &uniformAllocation,
        &uniformDetails));

    // Optional depth attachment, also allocated and bound by VMA.
    VkImageCreateInfo depthImageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = chosenDepthFormat,
        .extent = { swapExtent.width, swapExtent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo depthAllocationInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthAllocation = VK_NULL_HANDLE;

    CHECK(vmaCreateImage(
        allocator,
        &depthImageInfo,
        &depthAllocationInfo,
        &depthImage,
        &depthAllocation,
        nullptr));

    VkImageView depthView =
        CreateImageView(device, depthImage, chosenDepthFormat, DEPTH_ASPECT);


    // ========================================================================
    // MILESTONE 6 — PUSH DESCRIPTORS + DYNAMIC RENDERING PIPELINE
    //
    // Working checkpoint:
    //   Create the pipeline without a render pass, framebuffer, descriptor
    //   pool, or allocated descriptor set.
    // ========================================================================

    VkDescriptorSetLayoutBinding uniformBinding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
        .bindingCount = 1,
        .pBindings = &uniformBinding,
    };

    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    CHECK(vkCreateDescriptorSetLayout(
        device,
        &descriptorLayoutInfo,
        nullptr,
        &descriptorLayout));

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptorLayout,
    };

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    CHECK(vkCreatePipelineLayout(
        device,
        &pipelineLayoutInfo,
        nullptr,
        &pipelineLayout));

    VkShaderModule vertexShader =
        CreateShaderModule(device, triangleVertexSpirv);
    VkShaderModule fragmentShader =
        CreateShaderModule(device, triangleFragmentSpirv);

    VkPipelineRenderingCreateInfo renderingPipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &surfaceFormat.format,
        .depthAttachmentFormat = chosenDepthFormat,
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingPipelineInfo,
        .stageCount = 2,
        .pStages = vertexAndFragmentStages,
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &triangleListState,
        .pViewportState = &dynamicViewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &depthState,
        .pColorBlendState = &colourBlendState,
        .pDynamicState = &viewportAndScissorDynamicState,
        .layout = pipelineLayout,
        .renderPass = VK_NULL_HANDLE, // Dynamic rendering: no VkRenderPass.
        .subpass = 0,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    CHECK(vkCreateGraphicsPipelines(
        device,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &pipeline));

    // Shader modules are no longer needed after successful pipeline creation.
    vkDestroyShaderModule(device, fragmentShader, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);


    // ========================================================================
    // MILESTONE 7 — COMMAND AND FRAME SYNCHRONIZATION OBJECTS
    //
    // Working checkpoint:
    //   Own one reusable command buffer and one frame's synchronization state.
    //   Keep presentation semaphores tied to swapchain images.
    // ========================================================================

    VkCommandPool commandPool =
        CreateResettableCommandPool(device, graphicsQueueFamily);
    VkCommandBuffer commandBuffer =
        AllocatePrimaryCommandBuffer(device, commandPool);

    VkSemaphore imageAvailable = CreateBinarySemaphore(device);

    // A submission fence can signal before presentation has finished waiting
    // on its semaphore. Give each swapchain image its own presentation
    // semaphore so it cannot be reused prematurely.
    List<VkSemaphore> renderFinishedForImage;
    for (uint32_t i = 0; i < swapchainImages.Count(); ++i)
    {
        renderFinishedForImage.Add(CreateBinarySemaphore(device));
    }

    // Start signalled so the first frame does not block.
    VkFence frameComplete = CreateFence(device, initiallySignalled = true);

    List<bool> swapchainImageHasBeenPresented(
        swapchainImages.Count(),
        false);

    bool depthLayoutInitialized = false;


    // ========================================================================
    // MILESTONE 8 — DRAW FRAMES WITH DYNAMIC RENDERING
    //
    // Working checkpoint:
    //   Clear, draw a triangle, and present repeatedly with validation clean.
    // ========================================================================

    while (!window.ShouldClose())
    {
        window.PollEvents();

        CHECK(vkWaitForFences(
            device,
            1,
            &frameComplete,
            VK_TRUE,
            UINT64_MAX));

        uint32_t imageIndex = 0;
        VkResult acquireResult = vkAcquireNextImageKHR(
            device,
            swapchain,
            UINT64_MAX,
            imageAvailable,
            VK_NULL_HANDLE,
            &imageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapchainAndDependentDepthResources();
            continue;
        }
        CHECK(acquireResult);

        VkSemaphore renderFinished =
            renderFinishedForImage[imageIndex];

        // With one frame in flight, the fence proves the previous GPU read of
        // this memory is finished before the CPU writes the next frame.
        FrameUniforms uniforms = BuildFrameUniforms(window.Time());
        CopyBytes(
            uniformDetails.pMappedData,
            &uniforms,
            sizeof(uniforms));
        CHECK(vmaFlushAllocation(
            allocator,
            uniformAllocation,
            0,
            sizeof(uniforms)));

        CHECK(vkResetFences(device, 1, &frameComplete));
        CHECK(vkResetCommandBuffer(commandBuffer, 0));

        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        // Acquire/present does not perform the image-layout transitions needed
        // for rendering. Synchronization 2 expresses them explicitly.
        VkImageMemoryBarrier2 colourToAttachment = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = swapchainImageHasBeenPresented[imageIndex]
                ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = swapchainImages[imageIndex],
            .subresourceRange = FullColourSubresourceRange(),
        };

        VkImageMemoryBarrier2 depthToAttachment = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask =
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask =
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .image = depthImage,
            .subresourceRange = FullDepthSubresourceRange(),
        };

        List<VkImageMemoryBarrier2> beginBarriers = {
            colourToAttachment,
        };
        if (!depthLayoutInitialized)
        {
            beginBarriers.Add(depthToAttachment);
            depthLayoutInitialized = true;
        }

        VkDependencyInfo beginDependency = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = beginBarriers.Count(),
            .pImageMemoryBarriers = beginBarriers.Data(),
        };
        vkCmdPipelineBarrier2(commandBuffer, &beginDependency);

        VkRenderingAttachmentInfo colourAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchainViews[imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = darkBlueClearValue,
        };

        VkRenderingAttachmentInfo depthAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depthView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = depthOneClearValue,
        };

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { { 0, 0 }, swapExtent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colourAttachment,
            .pDepthAttachment = &depthAttachment,
        };

        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(
            commandBuffer,
            0,
            1,
            &vertexBuffer,
            &vertexOffset);

        // Core Vulkan 1.4 push descriptor: the descriptor contents are copied
        // into the command buffer. No descriptor pool or VkDescriptorSet exists.
        VkDescriptorBufferInfo uniformDescriptor = {
            .buffer = uniformBuffer,
            .offset = 0,
            .range = sizeof(FrameUniforms),
        };

        VkWriteDescriptorSet pushedUniform = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &uniformDescriptor,
        };

        vkCmdPushDescriptorSet(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            set = 0,
            descriptorWriteCount = 1,
            &pushedUniform);

        vkCmdDraw(
            commandBuffer,
            vertexCount = 3,
            instanceCount = 1,
            firstVertex = 0,
            firstInstance = 0);

        vkCmdEndRendering(commandBuffer);

        VkImageMemoryBarrier2 colourToPresent = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = swapchainImages[imageIndex],
            .subresourceRange = FullColourSubresourceRange(),
        };

        VkDependencyInfo presentDependency = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &colourToPresent,
        };
        vkCmdPipelineBarrier2(commandBuffer, &presentDependency);

        CHECK(vkEndCommandBuffer(commandBuffer));

        VkSemaphoreSubmitInfo waitForImage = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = imageAvailable,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };

        VkCommandBufferSubmitInfo commandToSubmit = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commandBuffer,
        };

        VkSemaphoreSubmitInfo signalRenderingFinished = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = renderFinished,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        };

        VkSubmitInfo2 submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &waitForImage,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandToSubmit,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signalRenderingFinished,
        };

        CHECK(vkQueueSubmit2(
            graphicsQueue,
            1,
            &submitInfo,
            frameComplete));

        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinished,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
        };

        VkResult presentResult =
            vkQueuePresentKHR(presentQueue, &presentInfo);

        swapchainImageHasBeenPresented[imageIndex] = true;

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
            presentResult == VK_SUBOPTIMAL_KHR ||
            window.WasResized())
        {
            RecreateSwapchainAndDependentDepthResources();
        }
        else
        {
            CHECK(presentResult);
        }
    }


    // ========================================================================
    // MILESTONE 9 — SHUT DOWN IN REVERSE DEPENDENCY ORDER
    //
    // Working checkpoint:
    //   Exit with validation enabled and no lifetime or leak diagnostics.
    // ========================================================================

    CHECK(vkDeviceWaitIdle(device));

    vkDestroyFence(device, frameComplete, nullptr);
    for (VkSemaphore semaphore : renderFinishedForImage)
    {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    vkDestroySemaphore(device, imageAvailable, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);

    vkDestroyImageView(device, depthView, nullptr);
    vmaDestroyImage(allocator, depthImage, depthAllocation);
    vmaDestroyBuffer(allocator, uniformBuffer, uniformAllocation);
    vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);

    // VMA may own VkDeviceMemory blocks, so it must die before VkDevice.
    vmaDestroyAllocator(allocator);

    for (VkImageView view : swapchainViews)
    {
        vkDestroyImageView(device, view, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);

    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    DestroyDebugMessengerIfEnabled(instance, debugMessenger);
    vkDestroyInstance(instance, nullptr);
}
```

## Details intentionally deferred

Each milestone should add the relevant correctness work before it is considered
production-ready:

- Enumerate every layer, extension, format, present mode, queue family, and
  physical-device capability instead of assuming availability.
- Treat validation messages as actionable and add debug names with
  `VK_EXT_debug_utils`.
- Compile shaders to SPIR-V as a build step and validate their descriptor and
  vertex interfaces against the pipeline.
- Expand from one frame in flight to per-frame command buffers, fences,
  semaphores, and mapped uniform allocations.
- Track each image's actual layout instead of relying on a simplified boolean.
- Recreate the swapchain, its image views, and size-dependent VMA images when
  the window changes size.
- Choose colour/depth formats from queried support and handle separate graphics
  and present queue ownership correctly.
- Add VMA allocation names, budget reporting, and explicit upload batching.
- Remember that VMA manages allocation and binding, not resource-state
  transitions, transfer commands, or synchronization.

## References

- [RenderDoc: Vulkan in 30 minutes](https://renderdoc.org/vulkan-in-30-minutes.html)
- [Khronos Vulkan 1.4 release summary](https://docs.vulkan.org/guide/latest/vulkan_release_summary.html)
- [Khronos Vulkan 1.4 specification](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html)
- [Vulkan Memory Allocator quick start](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html)
- [Vulkan Memory Allocator mapping guidance](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html)
