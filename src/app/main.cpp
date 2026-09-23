/**
 * @file
 * @brief Program entry point, benchmark, AnimatedCube scenarios, and platform event loop.
 */

#include "benchmark.hpp"
#include "procedural_shadow_receiver.hpp"
#include "tutorial_frame_descriptions.hpp"

#include <algorithm>
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

/** @brief Complete command-line grammar appended to every parsing failure. */
constexpr std::string_view kCommandLineUsage =
    "Usage: fireEngineTutorial [--benchmark positive-instances [--forward-direct-primary] "
    "[--forward-recording-participants count] | --frames positive-count "
    "[--recreate-every-frame] | --smoke scenario "
    "[--forward-recording-participants count]] "
    "[--capture path --capture-frame positive-ordinal] (options may appear in any order)";

/* --- File-local types --- */

/** @brief Device-level integration paths available to bounded CTest runs. */
enum class SmokeScenario : std::uint8_t
{
    eNone,         ///< Interactive or explicitly frame-limited normal rendering.
    eBasic,        ///< Load, animate, prepare, and draw the imported textured scene.
    ePrepareTwice, ///< Change dependencies and replace compiled GPU resources.
    eUntextured,   ///< Draw the imported mesh through the persistent white fallback texture.
    eResize,       ///< Recreate presentation-dependent state after every presented frame.
    eShadow,       ///< Add the procedural receiver and use the shadow-demonstration camera.
};

/** @brief Mutually exclusive top-level application modes selected by the command line. */
enum class RunMode : std::uint8_t
{
    eInteractive, ///< Unbounded interactive rendering with no top-level option.
    eBenchmark,   ///< Synthetic phase-level benchmark.
    eFrames,      ///< Ordinary rendering bounded by a presented-frame count.
    eSmoke,       ///< Named bounded device-level integration scenario.
};

/** @brief Command-line controls used by automated integration runs. */
struct RunOptions
{
    RunMode mode = RunMode::eInteractive;    ///< Mutually exclusive top-level application mode.
    std::optional<std::uint64_t> frameLimit; ///< Presented frames requested before exit.
    std::optional<std::uint64_t>
        reprepareAfterFrame; ///< Presented-frame count that triggers repeated preparation.
    std::optional<std::size_t> benchmarkInstanceCount;  ///< Repeated cubes in benchmark mode.
    SmokeScenario smokeScenario = SmokeScenario::eNone; ///< Optional device scenario.
    bool recreateEveryFrame = false; ///< Whether every presented frame replaces presentation state.
    bool recordForwardDirectly = false; ///< Whether the benchmark bypasses forward secondaries.
    /// Diagnostic override forcing a participant count, or unset to use the workload policy.
    std::optional<std::size_t> forcedForwardRecordingParticipantCount;
    std::optional<fire_engine::FrameCaptureRequest> captureRequest; ///< Grouped one-shot capture.
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
constexpr std::array<SmokeDefinition, 5> kSmokeDefinitions{{
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
        // Two original frames populate both submission slots before preparation
        // quiesces them. Three replacement frames then revisit both slots while
        // image count and acquisition order remain driver-selected.
        .frameLimit = 5,
        .reprepareAfterFrame = 2,
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
    {
        .name = "shadow",
        .scenario = SmokeScenario::eShadow,
        // Five frames populate both submission slots and then reuse one.
        .frameLimit = 5,
        .reprepareAfterFrame = std::nullopt,
        .recreateEveryFrame = false,
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
 * @brief Parses the supported forward secondary-recording participant count.
 * @param text Argument text following --forward-recording-participants.
 * @return Participant count including the coordinator.
 * @throws std::invalid_argument if the count is not positive or exceeds the supported maximum.
 */
[[nodiscard]] std::size_t parseForwardRecordingParticipantCount(std::string_view text);

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
 * @brief Adds draws so each half of a split recording sees reuse and a change.
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
    const bool shadowDemonstration =
        options.mode == RunMode::eInteractive || options.smokeScenario == SmokeScenario::eShadow;
    const fire_engine::FrameDescription& frameDescription =
        shadowDemonstration ? fire_engine::tutorial::shadowDemonstrationFrameDescription()
                            : fire_engine::tutorial::animatedCubeFrameDescription();

    fire_engine::Glfw glfw;
    fire_engine::Window window{800, 600, applicationName};
    const fire_engine::RendererConfiguration rendererConfiguration{
        .forwardRecordingMode = options.recordForwardDirectly
                                    ? fire_engine::ForwardRecordingMode::eDirectPrimary
                                    : fire_engine::ForwardRecordingMode::eSecondaryCommandBuffer,
        .forcedForwardRecordingParticipantCount = options.forcedForwardRecordingParticipantCount,
        .captureRequest = options.captureRequest,
    };
    fire_engine::Renderer renderer{glfw, window, applicationName, rendererConfiguration};
    if (options.smokeScenario == SmokeScenario::eResize &&
        renderer.recreatePresentation(fire_engine::FramebufferExtent{}))
    {
        throw std::logic_error("A zero framebuffer extent unexpectedly replaced presentation");
    }
    fire_engine::SceneContent content = fire_engine::GltfLoader{}.load(
        std::filesystem::path{FIRE_ENGINE_ASSET_DIRECTORY} / "AnimatedCube" / "AnimatedCube.gltf");
    if (shadowDemonstration)
    {
        static_cast<void>(fire_engine::tutorial::addProceduralShadowReceiver(content));
    }
    std::optional<fire_engine::tutorial::BenchmarkRun> benchmark;
    if (options.benchmarkInstanceCount.has_value())
    {
        benchmark.emplace(content, *options.benchmarkInstanceCount);
    }
    else if (options.smokeScenario == SmokeScenario::eUntextured)
    {
        selectUntexturedScene(content);
    }
    fire_engine::SceneDrawListArena drawListArena;
    content.scene.updateWorldTransforms();
    std::size_t initialForwardDrawCount = 0;
    std::size_t initialShadowCasterCount = 0;
    {
        const fire_engine::SceneDrawList initialDrawList =
            content.scene.buildDrawItems(drawListArena);
        renderer.prepare(content.assets, initialDrawList);
        if (shadowDemonstration)
        {
            initialForwardDrawCount = initialDrawList.drawItems.size();
            initialShadowCasterCount =
                std::ranges::count_if(initialDrawList.drawItems,
                                      [&content](const auto& drawItem)
                                      {
                                          return content.assets.renderObjects()
                                              .at(drawItem.renderObject.value)
                                              .castsShadow;
                                      });
        }
    }

    const fire_engine::RendererInfo rendererInfo = renderer.info();
    std::println("Selected Vulkan 1.4 device: {}", rendererInfo.deviceName);
    std::println("Driver: {} ({})", rendererInfo.driverName, rendererInfo.driverInfo);
    std::println("Graphics queue family: {}", rendererInfo.graphicsQueueFamily);
    std::println("Present queue family: {}", rendererInfo.presentQueueFamily);
    std::println("Logical device, queues, VMA allocator, and {} frame slots created.",
                 rendererInfo.frameSlotCount);
    std::println("Swapchain created: {} images at {}x{} ({}, {}), {} presentation semaphores.",
                 rendererInfo.swapchainImageCount, rendererInfo.width, rendererInfo.height,
                 rendererInfo.imageFormat, rendererInfo.presentMode,
                 rendererInfo.presentationSemaphoreCount);
    std::println("Forward depth format: {}.", rendererInfo.forwardDepthFormat);
    std::println("Shadow maps: {} at {}x{}, format {}, {} created.", rendererInfo.shadowMapCount,
                 rendererInfo.shadowMapWidth, rendererInfo.shadowMapHeight,
                 rendererInfo.shadowMapFormat, rendererInfo.shadowMapCreationCount);
    const std::string_view preparedContent =
        benchmark.has_value() ? "Synthetic benchmark"
                              : (shadowDemonstration ? "Shadow demonstration" : "AnimatedCube");
    std::println("{} content prepared for drawing.", preparedContent);
    if (shadowDemonstration)
    {
        std::println("Shadow demonstration content: {} forward draws, {} object marked as a "
                     "shadow caster.",
                     initialForwardDrawCount, initialShadowCasterCount);
    }

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

        std::chrono::steady_clock::time_point drawListStart;
        if (benchmark.has_value())
        {
            drawListStart = std::chrono::steady_clock::now();
        }
        const fire_engine::SceneDrawList drawList = content.scene.buildDrawItems(drawListArena);
        std::chrono::nanoseconds drawListBuild{};
        if (benchmark.has_value())
        {
            drawListBuild = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - drawListStart);
        }

        fire_engine::RendererCpuTimings rendererTimings;
        const fire_engine::RenderResult result = renderer.drawFrame(
            drawList, frameDescription, benchmark.has_value() ? &rendererTimings : nullptr);
        if (benchmark.has_value())
        {
            benchmark->record(result, transformUpdate, drawListBuild, rendererTimings);
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
            renderer.prepare(content.assets, content.scene.buildDrawItems(drawListArena));
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

    if (options.captureRequest.has_value() && !renderer.captureComplete())
    {
        throw std::runtime_error("The requested frame capture did not complete");
    }

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
try
{
    RunOptions options;
    bool forwardDirectPrimarySeen = false;
    bool recreateEveryFrameSeen = false;
    bool forwardRecordingParticipantsSeen = false;
    bool captureSeen = false;
    bool captureFrameSeen = false;
    std::optional<std::filesystem::path> capturePath;
    std::optional<std::uint64_t> captureFrame;

    const auto selectMode = [&options](RunMode selectedMode, std::string_view optionName)
    {
        if (options.mode == selectedMode)
        {
            throw std::invalid_argument{"Repeated option: " + std::string{optionName}};
        }
        if (options.mode != RunMode::eInteractive)
        {
            throw std::invalid_argument(
                "Only one of --benchmark, --frames, and --smoke may be supplied");
        }
        options.mode = selectedMode;
    };
    const auto requireValue =
        [argumentCount, arguments](int& argumentIndex, std::string_view optionName)
    {
        if (argumentIndex + 1 >= argumentCount ||
            std::string_view{arguments[argumentIndex + 1]}.starts_with("--"))
        {
            throw std::invalid_argument{std::string{optionName} + " requires a value"};
        }
        return std::string_view{arguments[++argumentIndex]};
    };

    for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex)
    {
        const std::string_view option{arguments[argumentIndex]};
        if (option == "--benchmark")
        {
            selectMode(RunMode::eBenchmark, option);
            const std::uint64_t instanceCount =
                parsePositiveInteger(requireValue(argumentIndex, option), option);
            if (instanceCount > std::numeric_limits<std::size_t>::max())
            {
                throw std::invalid_argument(
                    "--benchmark instance count exceeds this platform's limit");
            }
            options.benchmarkInstanceCount = static_cast<std::size_t>(instanceCount);
        }
        else if (option == "--frames")
        {
            selectMode(RunMode::eFrames, option);
            options.frameLimit = parsePositiveInteger(requireValue(argumentIndex, option), option);
        }
        else if (option == "--smoke")
        {
            selectMode(RunMode::eSmoke, option);
            const std::string_view scenario = requireValue(argumentIndex, option);
            const auto definition =
                std::ranges::find(kSmokeDefinitions, scenario, &SmokeDefinition::name);
            if (definition == kSmokeDefinitions.end())
            {
                std::string requirement{"--smoke requires one of:"};
                for (const SmokeDefinition& candidate : kSmokeDefinitions)
                {
                    requirement.append(" ").append(candidate.name);
                }
                throw std::invalid_argument{requirement};
            }
            options.frameLimit = definition->frameLimit;
            options.reprepareAfterFrame = definition->reprepareAfterFrame;
            options.smokeScenario = definition->scenario;
            options.recreateEveryFrame = definition->recreateEveryFrame;
        }
        else if (option == "--forward-direct-primary")
        {
            if (forwardDirectPrimarySeen)
            {
                throw std::invalid_argument("Repeated option: --forward-direct-primary");
            }
            forwardDirectPrimarySeen = true;
            options.recordForwardDirectly = true;
        }
        else if (option == "--recreate-every-frame")
        {
            if (recreateEveryFrameSeen)
            {
                throw std::invalid_argument("Repeated option: --recreate-every-frame");
            }
            recreateEveryFrameSeen = true;
            options.recreateEveryFrame = true;
        }
        else if (option == "--forward-recording-participants")
        {
            if (forwardRecordingParticipantsSeen)
            {
                throw std::invalid_argument("Repeated option: --forward-recording-participants");
            }
            forwardRecordingParticipantsSeen = true;
            options.forcedForwardRecordingParticipantCount =
                parseForwardRecordingParticipantCount(requireValue(argumentIndex, option));
        }
        else if (option == "--capture")
        {
            if (captureSeen)
            {
                throw std::invalid_argument("Repeated option: --capture");
            }
            captureSeen = true;
            capturePath = std::filesystem::path{requireValue(argumentIndex, option)};
        }
        else if (option == "--capture-frame")
        {
            if (captureFrameSeen)
            {
                throw std::invalid_argument("Repeated option: --capture-frame");
            }
            captureFrameSeen = true;
            captureFrame = parsePositiveInteger(requireValue(argumentIndex, option), option);
        }
        else if (option == "--direct-primary")
        {
            throw std::invalid_argument("--direct-primary was renamed to --forward-direct-primary");
        }
        else if (option == "--recording-threads")
        {
            throw std::invalid_argument(
                "--recording-threads was renamed to --forward-recording-participants");
        }
        else
        {
            throw std::invalid_argument{"Unknown option: " + std::string{option}};
        }
    }

    if (options.recordForwardDirectly && options.mode != RunMode::eBenchmark)
    {
        throw std::invalid_argument("--forward-direct-primary requires --benchmark");
    }
    if (recreateEveryFrameSeen && options.mode != RunMode::eFrames)
    {
        throw std::invalid_argument("--recreate-every-frame requires --frames");
    }
    if (forwardRecordingParticipantsSeen && options.mode != RunMode::eBenchmark &&
        options.mode != RunMode::eSmoke)
    {
        throw std::invalid_argument(
            "--forward-recording-participants requires --benchmark or --smoke");
    }
    // The forward direct control records no secondary at all, so a split request
    // there would silently have no effect.
    if (options.recordForwardDirectly &&
        options.forcedForwardRecordingParticipantCount.value_or(1) > 1)
    {
        throw std::invalid_argument(
            "--forward-direct-primary records no forward secondary command buffer, so it "
            "cannot be combined with more than one forward recording participant");
    }
    if (capturePath.has_value() != captureFrame.has_value())
    {
        throw std::invalid_argument("--capture and --capture-frame must be supplied together");
    }
    if (capturePath.has_value())
    {
        if (options.mode == RunMode::eBenchmark)
        {
            throw std::invalid_argument("--capture cannot be combined with --benchmark");
        }
        if (options.mode != RunMode::eFrames && options.mode != RunMode::eSmoke)
        {
            throw std::invalid_argument("--capture requires --frames or --smoke");
        }
        // Mode validation above guarantees a positive bound. value_or(0)
        // nevertheless fails closed if a future mode forgets to populate it.
        if (*captureFrame > options.frameLimit.value_or(0))
        {
            throw std::invalid_argument("--capture-frame exceeds the requested frame limit");
        }
        options.captureRequest = fire_engine::FrameCaptureRequest{
            .outputPath = std::move(*capturePath),
            .frameOrdinal = *captureFrame,
        };
    }
    return options;
}
catch (const std::invalid_argument& error)
{
    throw std::invalid_argument{std::string{error.what()} + '\n' + std::string{kCommandLineUsage}};
}

[[nodiscard]] std::size_t parseForwardRecordingParticipantCount(std::string_view text)
{
    const std::uint64_t participants =
        parsePositiveInteger(text, "--forward-recording-participants");
    if (participants > fire_engine::kMaxForwardRecordingParticipants)
    {
        throw std::invalid_argument(
            "--forward-recording-participants exceeds the supported participant count");
    }
    return static_cast<std::size_t>(participants);
}

std::uint64_t parsePositiveInteger(std::string_view text, std::string_view optionName)
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

    // The imported cube already supplies the first A, so appending A, B, A, A, B
    // gives the six-draw order A, A, B, A, A, B. Splitting that in half hands
    // each recording participant a first-draw bind, a redundant skip, and a
    // resource change, rather than leaving one branch to a single chunk.
    const std::array<fire_engine::RenderObjectId, 5> appendedObjects{
        repeatedObject, differentObject, repeatedObject, repeatedObject, differentObject,
    };
    float offset = -3.0f;
    for (const fire_engine::RenderObjectId object : appendedObjects)
    {
        fire_engine::SceneNode& node = content.scene.addRoot("Mixed resource instance");
        node.localTransform(fire_engine::Transform{
            .translation = {.x = offset, .y = 0.0f, .z = 0.0f},
        });
        node.component(object);
        offset += 1.5f;
    }
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
