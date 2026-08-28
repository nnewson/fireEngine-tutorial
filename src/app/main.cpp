/**
 * @file
 * @brief Program entry point, benchmark, AnimatedCube scenarios, and platform event loop.
 */

#include "benchmark.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <fire_engine/animation/animation_playback.hpp>
#include <fire_engine/content/scene_content.hpp>
#include <fire_engine/core/log.hpp>
#include <fire_engine/gltf/gltf_loader.hpp>
#include <fire_engine/graphics/material.hpp>
#include <fire_engine/graphics/render_object.hpp>
#include <fire_engine/math/transform.hpp>
#include <fire_engine/platform/glfw.hpp>
#include <fire_engine/platform/window.hpp>
#include <fire_engine/render/renderer.hpp>
#include <fire_engine/scene/scene.hpp>

namespace
{
/** @cond INTERNAL */
/* --- File-local constants --- */

/**
 * @brief Deterministic step that samples both intervals and wraps AnimatedCube's two-second clip.
 */
constexpr float kSmokeAnimationStepSeconds = 0.8f;

/* --- File-local types --- */

/** @brief Device-level integration paths available to bounded CTest runs. */
enum class SmokeScenario : std::uint8_t
{
    eNone,         ///< Interactive or explicitly frame-limited normal rendering.
    eBasic,        ///< Load, animate, prepare, and draw the imported textured scene.
    ePrepareTwice, ///< Change dependencies and replace compiled GPU resources.
    eUntextured,   ///< Draw the imported mesh through the persistent white fallback texture.
    eResize,       ///< Recreate presentation-dependent state after every presented frame.
};

/** @brief Command-line controls used by automated integration runs. */
struct RunOptions
{
    std::optional<std::uint64_t> frameLimit; ///< Presented frames requested before exit.
    std::optional<std::uint64_t>
        reprepareAfterFrame; ///< Presented-frame count that triggers repeated preparation.
    std::optional<std::size_t> benchmarkInstanceCount;  ///< Repeated cubes in benchmark mode.
    SmokeScenario smokeScenario = SmokeScenario::eNone; ///< Optional device scenario.
    bool recreateEveryFrame = false; ///< Whether every presented frame replaces presentation state.
    bool recordDirectly = false; ///< Whether the benchmark bypasses the secondary command buffer.
};

/** @brief Data defining one named device-level integration scenario. */
struct SmokeDefinition
{
    std::string_view name;    ///< Command-line scenario name.
    SmokeScenario scenario;   ///< Integration behavior selected by the name.
    std::uint64_t frameLimit; ///< Presented frames required before exit.
    std::optional<std::uint64_t> reprepareAfterFrame; ///< Optional repeated-preparation trigger.
    bool recreateEveryFrame; ///< Whether to recreate after every presentation.
};

/** @brief Named integration-scenario metadata consumed by the command-line parser. */
constexpr std::array<SmokeDefinition, 4> kSmokeDefinitions{{
    {
        .name = "basic",
        .scenario = SmokeScenario::eBasic,
        .frameLimit = 3,
        .reprepareAfterFrame = std::nullopt,
        .recreateEveryFrame = false,
    },
    {
        .name = "prepare-twice",
        .scenario = SmokeScenario::ePrepareTwice,
        // After the first frame uses the original resources, three more frames
        // exercise the replacement. That typically revisits every per-image
        // presentation fence on the current three-image swapchain, although
        // image count and acquisition order remain driver-selected.
        .frameLimit = 4,
        .reprepareAfterFrame = 1,
        .recreateEveryFrame = false,
    },
    {
        .name = "untextured",
        .scenario = SmokeScenario::eUntextured,
        .frameLimit = 3,
        .reprepareAfterFrame = std::nullopt,
        .recreateEveryFrame = false,
    },
    {
        .name = "resize",
        .scenario = SmokeScenario::eResize,
        .frameLimit = 3,
        .reprepareAfterFrame = std::nullopt,
        .recreateEveryFrame = true,
    },
}};

/* --- File-local function declarations --- */

/**
 * @brief Reads the optional benchmark, frame limit, and integration-scenario mode.
 * @param argumentCount Number of command-line arguments including the executable.
 * @param arguments Null-terminated argument strings supplied by the host environment.
 * @return Parsed controls, or interactive defaults when no arguments are supplied.
 * @throws std::invalid_argument if the command line does not match the documented usage.
 */
[[nodiscard]] RunOptions parseOptions(int argumentCount, char* arguments[]);

/**
 * @brief Parses a positive integer used by one command-line option.
 * @param text Complete argument text to parse.
 * @param optionName Option named in the failure diagnostic.
 * @return Parsed positive value.
 * @throws std::invalid_argument if text is not a positive integer.
 */
[[nodiscard]] std::uint64_t parsePositiveInteger(std::string_view text,
                                                 std::string_view optionName);

/**
 * @brief Adds an untextured render object that reuses AnimatedCube's imported mesh.
 * @param content Loaded content whose asset catalog receives the material and relationship.
 * @return ID of the newly added render object.
 */
[[nodiscard]] fire_engine::RenderObjectId
addUntexturedRenderObject(fire_engine::SceneContent& content);

/**
 * @brief Replaces the imported hierarchy with one untextured use of its mesh.
 * @param content Loaded AnimatedCube content whose mesh remains the geometry source.
 */
void selectUntexturedScene(fire_engine::SceneContent& content);

/**
 * @brief Adds A/A/B draws so repeated preparation exercises binding reuse and changes.
 * @param content Loaded AnimatedCube content extended with mixed-resource instances.
 */
void addMixedResourceInstances(fire_engine::SceneContent& content);

/**
 * @brief Waits without spinning and retries recreation until the framebuffer is drawable.
 * @param renderer Renderer whose presentation-dependent state is replaced.
 * @param window Window whose events and framebuffer extent are inspected.
 * @return False when closure was requested before recreation succeeded.
 */
[[nodiscard]] bool recreateWhenDrawable(fire_engine::Renderer& renderer,
                                        const fire_engine::Window& window);

/** @endcond */
} // namespace

/* --- Public functions --- */

/**
 * @brief Runs the Vulkan application and owns its platform event loop.
 * @param argumentCount Number of command-line arguments including the executable.
 * @param arguments Null-terminated argument strings supplied by the host environment.
 * @return Zero after a clean shutdown; otherwise one.
 */
int main(int argumentCount, char* arguments[])
try
{
    const RunOptions options = parseOptions(argumentCount, arguments);
    const std::string applicationName = "fireEngine Tutorial";

    fire_engine::Glfw glfw;
    fire_engine::Window window{800, 600, applicationName};
    const fire_engine::RendererConfiguration rendererConfiguration{
        .commandRecordingMode = options.recordDirectly
                                    ? fire_engine::CommandRecordingMode::eDirectPrimary
                                    : fire_engine::CommandRecordingMode::eSecondaryCommandBuffer,
    };
    fire_engine::Renderer renderer{glfw, window, applicationName, rendererConfiguration};
    if (options.smokeScenario == SmokeScenario::eResize &&
        renderer.recreatePresentation(fire_engine::FramebufferExtent{}))
    {
        throw std::logic_error("A zero framebuffer extent unexpectedly replaced presentation");
    }
    fire_engine::SceneContent content = fire_engine::GltfLoader{}.load(
        std::filesystem::path{FIRE_ENGINE_ASSET_DIRECTORY} / "AnimatedCube" / "AnimatedCube.gltf");

    std::optional<fire_engine::tutorial::BenchmarkRun> benchmark;
    if (options.benchmarkInstanceCount.has_value())
    {
        benchmark.emplace(content, *options.benchmarkInstanceCount);
    }
    else if (options.smokeScenario == SmokeScenario::eUntextured)
    {
        selectUntexturedScene(content);
    }
    content.scene.updateWorldTransforms();
    renderer.prepare(content.assets, content.scene);

    const fire_engine::RendererInfo rendererInfo = renderer.info();
    std::println("Selected Vulkan 1.4 device: {}", rendererInfo.deviceName);
    std::println("Driver: {} ({})", rendererInfo.driverName, rendererInfo.driverInfo);
    std::println("Graphics queue family: {}", rendererInfo.graphicsQueueFamily);
    std::println("Present queue family: {}", rendererInfo.presentQueueFamily);
    std::println("Logical device, queues, and VMA allocator created.");
    std::println("Swapchain created: {} images at {}x{} ({}, {}, depth {}), {} presentation "
                 "semaphores.",
                 rendererInfo.swapchainImageCount, rendererInfo.width, rendererInfo.height,
                 rendererInfo.imageFormat, rendererInfo.presentMode, rendererInfo.depthFormat,
                 rendererInfo.presentationSemaphoreCount);
    std::println("{} content prepared for drawing.",
                 benchmark.has_value() ? "Synthetic benchmark" : "AnimatedCube");

    std::uint64_t renderedFrameCount = 0;
    bool repeatedPreparationComplete = false;
    auto previousFrameTime = std::chrono::steady_clock::now();
    const auto runIncomplete = [&]()
    {
        if (benchmark.has_value())
        {
            return !benchmark->complete();
        }
        return !options.frameLimit.has_value() || renderedFrameCount < *options.frameLimit;
    };
    while (!window.shouldClose() && runIncomplete())
    {
        window.pollEvents();
        if (window.shouldClose())
        {
            break;
        }

        std::chrono::nanoseconds transformUpdate{};
        if (benchmark.has_value())
        {
            benchmark->advanceScene(content.scene);
            const auto transformStart = std::chrono::steady_clock::now();
            content.scene.updateWorldTransforms();
            transformUpdate = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - transformStart);
        }
        else
        {
            const auto currentFrameTime = std::chrono::steady_clock::now();
            const float elapsedSeconds =
                options.smokeScenario == SmokeScenario::eNone
                    ? std::chrono::duration<float>{currentFrameTime - previousFrameTime}.count()
                    : kSmokeAnimationStepSeconds;
            previousFrameTime = currentFrameTime;
            fire_engine::advanceAnimations(content.scene, content.animations, elapsedSeconds);
            content.scene.updateWorldTransforms();
        }

        if (window.consumeFramebufferResize())
        {
            if (!recreateWhenDrawable(renderer, window))
            {
                break;
            }
        }

        fire_engine::RendererCpuTimings rendererTimings;
        const fire_engine::RenderResult result =
            renderer.drawFrame(content.scene, benchmark.has_value() ? &rendererTimings : nullptr);
        if (benchmark.has_value())
        {
            benchmark->record(result, transformUpdate, rendererTimings);
        }
        if (result != fire_engine::RenderResult::eNotPresented)
        {
            ++renderedFrameCount;
        }
        if (!repeatedPreparationComplete && options.reprepareAfterFrame == renderedFrameCount)
        {
            // Replace compiled resources only after a submitted frame has used
            // the original set, exercising prepare()'s retirement wait.
            addMixedResourceInstances(content);
            content.scene.updateWorldTransforms();
            renderer.prepare(content.assets, content.scene);
            repeatedPreparationComplete = true;
        }
        if (result != fire_engine::RenderResult::ePresented || options.recreateEveryFrame)
        {
            // Coalesce a resize callback with the out-of-date or suboptimal
            // result that the same surface change may have produced.
            static_cast<void>(window.consumeFramebufferResize());
            if (!recreateWhenDrawable(renderer, window))
            {
                break;
            }
        }
    }

    // Presentation is not covered by the per-frame fence. Renderer::waitIdle
    // first waits for device work, then waits for every presentation fence
    // supplied through the KHR or equivalent EXT swapchain-maintenance extension.
    // Together they make submitted and presentation resources safe to destroy.
    renderer.waitIdle();

    if (benchmark.has_value())
    {
        if (!benchmark->complete())
        {
            throw std::runtime_error("The benchmark ended before collecting every measured frame");
        }
        benchmark->printReport(rendererInfo);
    }
    else if (options.frameLimit.has_value() && renderedFrameCount != *options.frameLimit)
    {
        throw std::runtime_error("The smoke test ended before presenting every requested frame");
    }
    std::println("Presented {} frame{}.", renderedFrameCount, renderedFrameCount == 1 ? "" : "s");
    return 0;
}
catch (const std::exception& error)
{
    fire_engine::log("fireEngine Tutorial failed: {}", error.what());
    return 1;
}

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

[[nodiscard]] RunOptions parseOptions(int argumentCount, char* arguments[])
{
    if (argumentCount == 1)
    {
        return {};
    }
    const std::string_view option{arguments[1]};
    if (argumentCount == 3 && option == "--smoke")
    {
        const std::string_view scenario{arguments[2]};
        for (const SmokeDefinition& definition : kSmokeDefinitions)
        {
            if (scenario == definition.name)
            {
                return {
                    .frameLimit = definition.frameLimit,
                    .reprepareAfterFrame = definition.reprepareAfterFrame,
                    .benchmarkInstanceCount = std::nullopt,
                    .smokeScenario = definition.scenario,
                    .recreateEveryFrame = definition.recreateEveryFrame,
                    .recordDirectly = false,
                };
            }
        }
        std::string requirement{"--smoke requires one of:"};
        for (const SmokeDefinition& definition : kSmokeDefinitions)
        {
            requirement.append(" ").append(definition.name);
        }
        throw std::invalid_argument{requirement};
    }
    if (option == "--benchmark" && argumentCount >= 3)
    {
        const std::uint64_t instanceCount = parsePositiveInteger(arguments[2], "--benchmark");
        if (instanceCount > std::numeric_limits<std::size_t>::max())
        {
            throw std::invalid_argument("--benchmark instance count exceeds this platform's limit");
        }
        bool recordDirectly = false;
        for (int argumentIndex = 3; argumentIndex < argumentCount; ++argumentIndex)
        {
            const std::string_view benchmarkOption{arguments[argumentIndex]};
            if (benchmarkOption == "--direct-primary" && !recordDirectly)
            {
                recordDirectly = true;
            }
            else
            {
                throw std::invalid_argument{"Unknown or repeated benchmark option: " +
                                            std::string{benchmarkOption}};
            }
        }
        return {
            .frameLimit = std::nullopt,
            .reprepareAfterFrame = std::nullopt,
            .benchmarkInstanceCount = static_cast<std::size_t>(instanceCount),
            .smokeScenario = SmokeScenario::eNone,
            .recreateEveryFrame = false,
            .recordDirectly = recordDirectly,
        };
    }
    if ((argumentCount != 3 && argumentCount != 4) || option != "--frames" ||
        (argumentCount == 4 && std::string_view{arguments[3]} != "--recreate-every-frame"))
    {
        throw std::invalid_argument("Usage: fireEngineTutorial [--benchmark positive-instances "
                                    "[--direct-primary] | "
                                    "--frames positive-count [--recreate-every-frame] | "
                                    "--smoke scenario]");
    }

    const std::uint64_t value = parsePositiveInteger(arguments[2], "--frames");
    return {
        .frameLimit = value,
        .reprepareAfterFrame = std::nullopt,
        .benchmarkInstanceCount = std::nullopt,
        .smokeScenario = SmokeScenario::eNone,
        .recreateEveryFrame = argumentCount == 4,
        .recordDirectly = false,
    };
}

[[nodiscard]] std::uint64_t parsePositiveInteger(std::string_view text, std::string_view optionName)
{
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0)
    {
        throw std::invalid_argument(std::string{optionName} + " requires a positive integer");
    }
    return value;
}

[[nodiscard]] fire_engine::RenderObjectId
addUntexturedRenderObject(fire_engine::SceneContent& content)
{
    if (content.assets.meshes().size() != 1)
    {
        throw std::logic_error("The AnimatedCube smoke fixture must supply exactly one mesh");
    }
    const fire_engine::MaterialId material = content.assets.addMaterial({
        .baseColor = {.r = 0.25f, .g = 0.7f, .b = 1.0f, .a = 1.0f},
        .baseColorTexture = std::nullopt,
    });
    return content.assets.addRenderObject({
        .mesh = fire_engine::MeshId{.value = 0},
        .material = material,
    });
}

void selectUntexturedScene(fire_engine::SceneContent& content)
{
    const fire_engine::RenderObjectId object = addUntexturedRenderObject(content);
    fire_engine::Scene scene;
    scene.addRoot("Untextured AnimatedCube").component(object);
    content.scene = std::move(scene);
    // The replacement scene omits the imported nodes targeted by the clip, so
    // it deliberately has no animation playback state.
    content.animations.clear();
}

void addMixedResourceInstances(fire_engine::SceneContent& content)
{
    if (content.assets.meshes().size() != 1 || content.assets.renderObjects().size() != 1)
    {
        throw std::logic_error(
            "The AnimatedCube mixed-resource fixture requires one mesh and render object");
    }

    const fire_engine::RenderObjectId repeatedObject{.value = 0};
    fire_engine::SceneNode& repeatedNode = content.scene.addRoot("Repeated resource instance");
    repeatedNode.localTransform(fire_engine::Transform{
        .translation = {.x = -1.5f, .y = 0.0f, .z = 0.0f},
    });
    repeatedNode.component(repeatedObject);

    fire_engine::Mesh duplicatedMesh = content.assets.meshes().front();
    const fire_engine::MeshId duplicatedMeshId = content.assets.addMesh(std::move(duplicatedMesh));
    const fire_engine::MaterialId material = content.assets.addMaterial({
        .baseColor = {.r = 0.25f, .g = 0.7f, .b = 1.0f, .a = 1.0f},
        .baseColorTexture = std::nullopt,
    });
    const fire_engine::RenderObjectId differentObject = content.assets.addRenderObject({
        .mesh = duplicatedMeshId,
        .material = material,
    });
    fire_engine::SceneNode& differentNode = content.scene.addRoot("Different resource instance");
    differentNode.localTransform(fire_engine::Transform{
        .translation = {.x = 1.5f, .y = 0.0f, .z = 0.0f},
    });
    differentNode.component(differentObject);
}

[[nodiscard]] bool recreateWhenDrawable(fire_engine::Renderer& renderer,
                                        const fire_engine::Window& window)
{
    while (!window.shouldClose())
    {
        const fire_engine::FramebufferExtent extent = window.framebufferExtent();
        if (extent.width != 0 && extent.height != 0 && renderer.recreatePresentation(extent))
        {
            return true;
        }
        window.waitEvents();
    }
    return false;
}

/** @endcond */
} // namespace
