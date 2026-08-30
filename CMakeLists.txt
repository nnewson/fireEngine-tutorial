# Require a CMake version new enough for the project and vcpkg features used
# below. Configuration stops with a clear error when an older version is used.
cmake_minimum_required(VERSION 3.20)

# Declare the project metadata and tell CMake that this repository contains
# C++ sources. No C compiler is needed for this tutorial.
project(fire_engine_tutorial
    VERSION 0.9.0
    DESCRIPTION "A development tutorial to building your own fireEngine"
    LANGUAGES CXX
)

# Keep every current and future C++ target on the C++23 language baseline.
# REQUIRED prevents CMake from silently falling back to an older standard;
# disabling extensions selects portable C++23 rather than compiler variants
# such as gnu++23.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Locate the window-system integration, memory allocator, shader compiler,
# Vulkan 1.4 headers, and loader supplied by the vcpkg toolchain.
#
# The slang package exports slang::slangc as an imported executable target, so
# the shader command below names that target rather than searching for a binary.
# This uses the compiler pinned by the "shader-slang" entry in vcpkg.json instead
# of whichever slangc happens to be installed on the machine, and needs no
# knowledge of where vcpkg lays out its tools directory.
find_package(glfw3 CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(VulkanHeaders 1.4 CONFIG REQUIRED)
find_package(VulkanLoader 1.4 CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

# Strict warnings keep tutorial code honest. The option allows a consumer to
# disable only the "warnings are errors" policy when experimenting locally.
option(FIRE_ENGINE_TUTORIAL_WARNINGS_AS_ERRORS
    "Treat first-party compiler warnings as errors"
    ON
)

# Build the reusable engine separately from the application entry point. This
# lets CPU-only unit tests exercise maths, scene traversal, and render planning
# without creating a window or Vulkan device.
add_library(fireEngineTutorialEngine STATIC
    src/animation/animation_playback.cpp
    src/animation/animation_validation.cpp
    src/content/scene_content_validation.cpp
    src/gltf/gltf_loader.cpp
    src/gltf/image_decoder.cpp
    src/graphics/asset_validation.cpp
    src/graphics/render_assets.cpp
    src/graphics/render_preparation.cpp
    src/platform/glfw.cpp
    src/platform/window.cpp
    src/render/allocator.cpp
    src/render/buffer.cpp
    src/render/camera.cpp
    src/render/compiled_resources.cpp
    src/render/depth_buffer.cpp
    src/render/device.cpp
    src/render/draw_binding_state.cpp
    src/render/frame_slot.cpp
    src/render/image.cpp
    src/render/instance_support.cpp
    src/render/pipeline.cpp
    src/render/recording_context.cpp
    src/render/recording_input.cpp
    src/render/renderer.cpp
    src/render/resource_compiler.cpp
    src/render/secondary_recording_worker.cpp
    src/render/spirv_loader.cpp
    src/render/swapchain.cpp
    src/render/swapchain_selection.cpp
    src/scene/scene.cpp
    src/scene/scene_node.cpp
)

target_include_directories(fireEngineTutorialEngine PUBLIC include)
target_include_directories(fireEngineTutorialEngine PRIVATE ${Stb_INCLUDE_DIR})

# The application owns only its event loop and high-level scene construction.
add_executable(fireEngineTutorial
    src/app/benchmark.cpp
    src/app/main.cpp
)
target_link_libraries(fireEngineTutorial PRIVATE fireEngineTutorialEngine)

# State the language requirement on every first-party target so each remains
# explicit if it is later moved into a larger parent CMake project.
target_compile_features(fireEngineTutorialEngine PUBLIC cxx_std_23)
target_compile_features(fireEngineTutorial PRIVATE cxx_std_23)

# Pass the project version to the Vulkan application metadata so the version
# reported to the driver always matches the one declared by project() above.
target_compile_definitions(fireEngineTutorialEngine PRIVATE
    FIRE_ENGINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FIRE_ENGINE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FIRE_ENGINE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# Removing Vulkan-Hpp's convenience constructors makes Vulkan structures
# aggregates. The context uses the linked loader's vkGetInstanceProcAddr rather
# than Vulkan-Hpp's optional dlopen-based loader discovery helper.
target_compile_definitions(fireEngineTutorialEngine PUBLIC
    VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0
    VULKAN_HPP_NO_CONSTRUCTORS
)

# Pipeline creation loads build-time-compiled SPIR-V from this directory. An
# absolute path keeps direct runs and CTest independent of their working folder.
set(FIRE_ENGINE_SHADER_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
target_compile_definitions(fireEngineTutorialEngine PRIVATE
    FIRE_ENGINE_SHADER_DIRECTORY="${FIRE_ENGINE_SHADER_OUTPUT_DIRECTORY}"
)

# The example scene is also addressed through its build-tree location rather
# than the process working directory. This keeps command-line, CTest, and
# multi-configuration Visual Studio runs on the same deterministic path.
set(FIRE_ENGINE_ASSET_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets")
target_compile_definitions(fireEngineTutorial PRIVATE
    FIRE_ENGINE_ASSET_DIRECTORY="${FIRE_ENGINE_ASSET_OUTPUT_DIRECTORY}"
    FIRE_ENGINE_BUILD_CONFIGURATION="$<CONFIG>"
)

# Validation is most useful while developing and has a runtime cost, so it is
# tied to the Debug configuration. A generator expression is used rather than a
# CMAKE_BUILD_TYPE test because multi-configuration generators such as Visual
# Studio choose the configuration at build time rather than at configure time.
target_compile_definitions(fireEngineTutorialEngine PRIVATE
    $<$<CONFIG:Debug>:FIRE_ENGINE_ENABLE_VALIDATION>
)

# GLFW creates the platform surface; Vulkan::Loader supplies the dispatching
# library that discovers the machine's installed Vulkan drivers. VMA manages
# the prepared mesh buffers and per-frame uniform allocation used by the renderer.
target_link_libraries(fireEngineTutorialEngine
    PUBLIC
        Vulkan::Headers
    PRIVATE
        fastgltf::fastgltf
        GPUOpen::VulkanMemoryAllocator
        glfw
        Vulkan::Loader
)

# Compile the shader once into a single SPIR-V module. Slang finds every
# [shader(...)] entry point in the file, and SPIR-V permits any number of entry
# points per module, so both stages come from one compiler invocation and one
# file that src/render/pipeline.cpp reads once.
#
# Slang emits SPIR-V 1.6 directly, which Vulkan 1.4 accepts, and every shader
# warning is treated as a build failure.
#
# -fvk-use-entrypoint-name preserves "vertexMain" and "fragmentMain" in the
# output. Those names must match the pName values in src/render/pipeline.cpp.
# Slang only collapses an entry point to "main" when it emits exactly one, so
# this flag is what keeps the names stable if the shader is ever split up.
#
# DEPENDS lists the shader source and the compiler itself, so upgrading slangc
# through vcpkg forces a recompile. It does not track Slang `import`ed or
# `#include`d modules; add `-depfile` and DEPFILE once the shader spans more
# than this one file.
set(FIRE_ENGINE_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.slang")
set(FIRE_ENGINE_SHADER_BINARY "${FIRE_ENGINE_SHADER_OUTPUT_DIRECTORY}/scene.spv")

add_custom_command(
    OUTPUT "${FIRE_ENGINE_SHADER_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FIRE_ENGINE_SHADER_OUTPUT_DIRECTORY}"
    COMMAND slang::slangc
        "${FIRE_ENGINE_SHADER_SOURCE}"
        -target spirv
        -profile spirv_1_6
        -matrix-layout-column-major
        -fvk-use-entrypoint-name
        -restrictive-capability-check
        -warnings-as-errors all
        -o "${FIRE_ENGINE_SHADER_BINARY}"
    DEPENDS "${FIRE_ENGINE_SHADER_SOURCE}" slang::slangc
    COMMENT "Compiling Slang shader to SPIR-V"
    VERBATIM
)

add_custom_target(fireEngineShaders ALL DEPENDS "${FIRE_ENGINE_SHADER_BINARY}")
add_dependencies(fireEngineTutorial fireEngineShaders)

# Keep the example's external glTF buffer and images beside the executable.
# An ALL target deliberately checks the directory on every build, so asset-only
# edits reach the runtime tree without forcing an unrelated executable relink.
add_custom_target(fireEngineAssets ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/AnimatedCube"
        "${FIRE_ENGINE_ASSET_OUTPUT_DIRECTORY}/AnimatedCube"
    COMMENT "Copying AnimatedCube runtime assets"
    VERBATIM
)
add_dependencies(fireEngineTutorial fireEngineAssets)

# MSVC and Clang/GCC spell equivalent warning options differently. Apply one
# strict policy to the library, executable, and unit tests.
function(fire_engine_enable_warnings target)
    if(MSVC)
        # Imported dependency headers are marked external. Lower their warning
        # level so dependencies are not held to first-party /W4 /WX policy.
        target_compile_options(${target} PRIVATE /W4 /external:W0)
        if(FIRE_ENGINE_TUTORIAL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(FIRE_ENGINE_TUTORIAL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

fire_engine_enable_warnings(fireEngineTutorialEngine)
fire_engine_enable_warnings(fireEngineTutorial)

# Catch2 covers the Vulkan-free layers. The bounded application run remains a
# separate integration test for actual acquisition, rendering, and presentation.
enable_testing()

add_executable(fireEngineTutorialTests
    tests/animation/test_animation_playback.cpp
    tests/animation/test_animation_validation.cpp
    tests/content/test_scene_content_validation.cpp
    tests/gltf/test_gltf_loader.cpp
    tests/graphics/test_asset_validation.cpp
    tests/graphics/test_render_preparation.cpp
    tests/math/test_mat4.cpp
    tests/render/test_camera.cpp
    tests/render/test_draw_binding_state.cpp
    tests/render/test_recording_input.cpp
    tests/render/test_secondary_recording_worker.cpp
    tests/render/test_spirv_loader.cpp
    tests/render/test_swapchain_selection.cpp
    tests/scene/test_scene.cpp
)
target_compile_definitions(fireEngineTutorialTests PRIVATE
    FIRE_ENGINE_TEST_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    FIRE_ENGINE_TEST_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
target_link_libraries(fireEngineTutorialTests
    PRIVATE
        Catch2::Catch2WithMain
        fireEngineTutorialEngine
)
fire_engine_enable_warnings(fireEngineTutorialTests)

include(Catch)
catch_discover_tests(fireEngineTutorialTests)

# These bounded scenarios retain the real application integration path. They
# cover imported animation, changed preparation dependencies, the untextured
# fallback descriptor, and presentation-resource replacement on a Vulkan device.
add_test(NAME fireEngineTutorialAnimatedCubeSmoke
    COMMAND fireEngineTutorial --smoke basic
)
add_test(NAME fireEngineTutorialPrepareTwiceSmoke
    COMMAND fireEngineTutorial --smoke prepare-twice
)
add_test(NAME fireEngineTutorialUntexturedSmoke
    COMMAND fireEngineTutorial --smoke untextured
)
add_test(NAME fireEngineTutorialRecreationSmoke
    COMMAND fireEngineTutorial --smoke resize
)
add_test(NAME fireEngineTutorialBenchmarkCorrectness
    COMMAND fireEngineTutorial --benchmark 1
)
add_test(NAME fireEngineTutorialDirectPrimaryBenchmarkCorrectness
    COMMAND fireEngineTutorial --benchmark 1 --direct-primary
)
add_test(NAME fireEngineTutorialSplitRecordingBenchmarkCorrectness
    COMMAND fireEngineTutorial --benchmark 2 --recording-threads 2
)
# One draw cannot form two non-empty ranges, so this proves the fallback keeps
# the single-participant path working when a split is requested.
add_test(NAME fireEngineTutorialSplitRecordingFallbackCorrectness
    COMMAND fireEngineTutorial --benchmark 1 --recording-threads 2
)
add_test(NAME fireEngineTutorialSplitRecordingMixedResourceSmoke
    COMMAND fireEngineTutorial --smoke prepare-twice --recording-threads 2
)
# Without an override the workload policy selects the participant count. Cover
# both branches at the call site: the compile-time assertions only protect the
# calculation, not that the renderer consults it.
add_test(NAME fireEngineTutorialAutomaticRecordingCorrectness
    COMMAND fireEngineTutorial --benchmark 2
)
add_test(NAME fireEngineTutorialAutomaticSplitCorrectness
    COMMAND fireEngineTutorial --benchmark 10000
)

# Synchronization validation is compiled into Debug configurations only. Keep
# its device scenarios out of Release test lists instead of registering silent
# duplicates with an environment variable that cannot enable the absent layer.
set(FIRE_ENGINE_SYNC_VALIDATION_TESTS)
set(FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS)
if(CMAKE_CONFIGURATION_TYPES)
    list(APPEND FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS CONFIGURATIONS Debug)
endif()
if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_test(NAME fireEngineTutorialPrepareTwiceSyncValidation
        COMMAND fireEngineTutorial --smoke prepare-twice
        ${FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS}
    )
    add_test(NAME fireEngineTutorialRecreationSyncValidation
        COMMAND fireEngineTutorial --smoke resize
        ${FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS}
    )
    add_test(NAME fireEngineTutorialDirectPrimarySyncValidation
        COMMAND fireEngineTutorial --benchmark 1 --direct-primary
        ${FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS}
    )
    add_test(NAME fireEngineTutorialSplitRecordingSyncValidation
        COMMAND fireEngineTutorial --benchmark 2 --recording-threads 2
        ${FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS}
    )
    add_test(NAME fireEngineTutorialSplitRecordingMixedResourceSyncValidation
        COMMAND fireEngineTutorial --smoke prepare-twice --recording-threads 2
        ${FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS}
    )
    # The ordinary benchmark controls check CLI/reporting and standard validation;
    # these separate runs deliberately enable synchronization checks.
    set(FIRE_ENGINE_SYNC_VALIDATION_TESTS
        fireEngineTutorialPrepareTwiceSyncValidation
        fireEngineTutorialRecreationSyncValidation
        fireEngineTutorialDirectPrimarySyncValidation
        fireEngineTutorialSplitRecordingSyncValidation
        fireEngineTutorialSplitRecordingMixedResourceSyncValidation
    )
    set_tests_properties(${FIRE_ENGINE_SYNC_VALIDATION_TESTS} PROPERTIES
        ENVIRONMENT VK_LAYER_VALIDATE_SYNC=1
    )
endif()

set_tests_properties(
    fireEngineTutorialAnimatedCubeSmoke
    fireEngineTutorialPrepareTwiceSmoke
    fireEngineTutorialUntexturedSmoke
    fireEngineTutorialRecreationSmoke
    fireEngineTutorialBenchmarkCorrectness
    fireEngineTutorialDirectPrimaryBenchmarkCorrectness
    fireEngineTutorialSplitRecordingBenchmarkCorrectness
    fireEngineTutorialSplitRecordingFallbackCorrectness
    fireEngineTutorialSplitRecordingMixedResourceSmoke
    fireEngineTutorialAutomaticRecordingCorrectness
    fireEngineTutorialAutomaticSplitCorrectness
    ${FIRE_ENGINE_SYNC_VALIDATION_TESTS}
    PROPERTIES
        FAIL_REGULAR_EXPRESSION "Vulkan validation error:"
        RESOURCE_LOCK fireEngineTutorialVulkan
        TIMEOUT 30
)

set_tests_properties(fireEngineTutorialBenchmarkCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Ownership: cycled frame slots with per-slot recording contexts
  Frames in flight: 2
  Presentation: [^\n]*
  Workload: instances=1, nodes=2, draws=1"
)
# Assert the effective participant count, not the requested one: a split that
# silently fell back to a single participant would otherwise pass. These
# scenarios force a split below the production threshold on purpose, so the
# two-participant path keeps its validation coverage wherever the policy would
# select one.
set_tests_properties(fireEngineTutorialSplitRecordingBenchmarkCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Secondary recording participants: 2 forced, 2 effective(.|\n)*participant 1 recording"
)
set_tests_properties(fireEngineTutorialSplitRecordingFallbackCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Secondary recording participants: 2 forced, 1 effective"
)
set_tests_properties(fireEngineTutorialAutomaticRecordingCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Secondary recording participants: automatic[^\n]*, 1 effective"
)
set_tests_properties(fireEngineTutorialAutomaticSplitCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Secondary recording participants: automatic[^\n]*, 2 effective"
)
set_tests_properties(fireEngineTutorialDirectPrimaryBenchmarkCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Recording path: direct primary command buffer(.|\n)*Its worker pool has no command-buffer allocations or recorded work."
)
# Run one scenario away from the executable and copied assets to prove that the
# compiled absolute asset and shader paths do not depend on the working folder.
set(FIRE_ENGINE_SMOKE_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke-work")
file(MAKE_DIRECTORY "${FIRE_ENGINE_SMOKE_WORKING_DIRECTORY}")
set_tests_properties(fireEngineTutorialAnimatedCubeSmoke PROPERTIES
    WORKING_DIRECTORY "${FIRE_ENGINE_SMOKE_WORKING_DIRECTORY}"
)
