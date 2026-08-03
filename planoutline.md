# Vulkan 1.4 outline

This is deliberately pseudocode. It shows the order of the important calls,
but leaves structure population, capability checks, error handling, resize
handling, and platform-specific window code to helper functions.

Vulkan objects use the Vulkan-Hpp RAII wrappers. VMA-created buffers remain
VMA-owned and are therefore destroyed through VMA rather than `vk::raii`.

```cpp
void RunVulkan14()
{
    // ========================================================================
    // MILESTONE 1 — CREATE A VULKAN 1.4 INSTANCE
    // ========================================================================

    vk::raii::Context context;

    auto instanceInfo = MakeInstanceInfo(
        apiVersion = vk::ApiVersion14,
        requiredWindowExtensions);

    vk::raii::Instance instance(context, instanceInfo);

    // ========================================================================
    // MILESTONE 2 — SELECT A DEVICE AND CREATE THE QUEUES
    // ========================================================================

    vk::raii::DebugUtilsMessengerEXT debugMessenger =
        CreateDebugMessenger(instance);

    vk::raii::SurfaceKHR surface =
        CreateWindowSurface(instance, window);

    std::vector<vk::raii::PhysicalDevice> physicalDevices =
        instance.enumeratePhysicalDevices();

    vk::raii::PhysicalDevice physicalDevice =
        ChooseVulkan14Device(
            physicalDevices,
            surface,
            requiredFeatures = {
                dynamicRendering,
                synchronization2,
                pushDescriptor,
            });

    auto deviceInfo = MakeDeviceInfo(
        physicalDevice,
        surface,
        enableVulkan13Features = {
            dynamicRendering,
            synchronization2,
        },
        enableVulkan14Features = {
            pushDescriptor,
        });

    vk::raii::Device device(physicalDevice, deviceInfo);

    vk::raii::Queue graphicsQueue =
        device.getQueue(graphicsQueueFamily, 0);
    vk::raii::Queue presentQueue =
        device.getQueue(presentQueueFamily, 0);


    // ========================================================================
    // MILESTONE 3 — CREATE VMA AND THE VERTEX BUFFER
    // ========================================================================

    VmaAllocatorCreateInfo allocatorInfo =
        MakeVmaAllocatorInfo(
            *instance,
            *physicalDevice,
            *device,
            VK_API_VERSION_1_4);

    VmaAllocator allocator;
    vmaCreateAllocator(&allocatorInfo, &allocator);

    VkBuffer vertexBuffer;
    VmaAllocation vertexAllocation;

    vmaCreateBuffer(
        allocator,
        &vertexBufferInfo,
        &vertexAllocationInfo,
        &vertexBuffer,
        &vertexAllocation,
        nullptr);

    UploadTriangleVertices(
        allocator,
        vertexBuffer,
        triangleVertices);


    // ========================================================================
    // MILESTONE 4 — CREATE THE SWAPCHAIN
    // ========================================================================

    auto swapchainInfo =
        MakeSwapchainInfo(physicalDevice, device, surface, window);

    vk::raii::SwapchainKHR swapchain(device, swapchainInfo);

    std::vector<vk::Image> swapchainImages =
        swapchain.getImages();

    std::vector<vk::raii::ImageView> swapchainViews =
        CreateSwapchainImageViews(device, swapchainImages);


    // ========================================================================
    // MILESTONE 5 — CREATE THE VULKAN 1.4 PIPELINE
    // ========================================================================

    // Push descriptors need a layout, but no descriptor pool or allocated
    // descriptor sets.
    vk::raii::DescriptorSetLayout descriptorLayout(
        device,
        MakePushDescriptorLayoutInfo());

    vk::raii::PipelineLayout pipelineLayout(
        device,
        MakePipelineLayoutInfo(descriptorLayout));

    // Dynamic rendering means the pipeline does not need a VkRenderPass.
    vk::raii::Pipeline pipeline =
        CreateDynamicRenderingPipeline(
            device,
            pipelineLayout,
            swapchainFormat,
            vertexShader,
            fragmentShader);


    // ========================================================================
    // MILESTONE 6 — CREATE COMMAND AND SYNCHRONIZATION OBJECTS
    // ========================================================================

    vk::raii::CommandPool commandPool(
        device,
        MakeCommandPoolInfo(graphicsQueueFamily));

    vk::raii::CommandBuffers commandBuffers(
        device,
        MakeCommandBufferAllocateInfo(commandPool, count = 1));

    vk::raii::CommandBuffer& commandBuffer =
        commandBuffers.front();

    vk::raii::Semaphore imageAvailable(
        device,
        semaphoreInfo);

    std::vector<vk::raii::Semaphore> renderFinished =
        CreateOneSemaphorePerSwapchainImage(device, swapchainImages);

    vk::raii::Fence frameFinished(
        device,
        MakeInitiallySignalledFenceInfo());


    // ========================================================================
    // MILESTONE 7 — RENDER AND PRESENT
    // ========================================================================

    while (!window.ShouldClose())
    {
        window.PollEvents();

        device.waitForFences(*frameFinished, true, UINT64_MAX);
        device.resetFences(*frameFinished);

        uint32_t imageIndex =
            AcquireNextImage(device, swapchain, imageAvailable);

        commandBuffer.reset();
        commandBuffer.begin(commandBufferBeginInfo);

        // Synchronization 2: make the acquired image a colour attachment.
        commandBuffer.pipelineBarrier2(
            MakeBarrier(
                swapchainImages[imageIndex],
                oldLayout = vk::ImageLayout::ePresentSrcKHR,
                newLayout = vk::ImageLayout::eAttachmentOptimal));

        commandBuffer.beginRendering(
            MakeRenderingInfo(swapchainViews[imageIndex]));

        commandBuffer.bindPipeline(
            vk::PipelineBindPoint::eGraphics,
            pipeline);

        commandBuffer.setViewport(0, viewport);
        commandBuffer.setScissor(0, scissor);

        commandBuffer.bindVertexBuffers(
            firstBinding = 0,
            vk::Buffer(vertexBuffer),
            vertexOffset = 0);

        // Core Vulkan 1.4 push descriptor call.
        commandBuffer.pushDescriptorSet(
            vk::PipelineBindPoint::eGraphics,
            pipelineLayout,
            set = 0,
            descriptorWrites);

        commandBuffer.draw(
            vertexCount = 3,
            instanceCount = 1,
            firstVertex = 0,
            firstInstance = 0);

        commandBuffer.endRendering();

        // Synchronization 2: prepare the completed image for presentation.
        commandBuffer.pipelineBarrier2(
            MakeBarrier(
                swapchainImages[imageIndex],
                oldLayout = vk::ImageLayout::eAttachmentOptimal,
                newLayout = vk::ImageLayout::ePresentSrcKHR));

        commandBuffer.end();

        graphicsQueue.submit2(
            MakeSubmitInfo(
                imageAvailable,
                commandBuffer,
                renderFinished[imageIndex]),
            frameFinished);

        presentQueue.presentKHR(
            MakePresentInfo(
                swapchain,
                imageIndex,
                renderFinished[imageIndex]));
    }


    // ========================================================================
    // MILESTONE 8 — CLEAN UP
    // ========================================================================

    device.waitIdle();

    // VMA owns this buffer and its allocation, so VMA destroys both.
    vmaDestroyBuffer(
        allocator,
        vertexBuffer,
        vertexAllocation);

    vmaDestroyAllocator(allocator);

    // The vk::raii objects destroy themselves in reverse construction order.
}
```
