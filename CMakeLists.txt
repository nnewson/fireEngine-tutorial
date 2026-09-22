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
    src/graphics/frame_capture.cpp
    src/graphics/render_assets.cpp
    src/graphics/render_preparation.cpp
    src/platform/glfw.cpp
    src/platform/window.cpp
    src/render/allocator.cpp
    src/render/buffer.cpp
    src/render/camera.cpp
    src/render/capture_format_mapping.cpp
    src/render/compiled_resources.cpp
    src/render/depth_buffer.cpp
    src/render/device.cpp
    src/render/directional_shadow_view.cpp
    src/render/draw_binding_state.cpp
    src/render/forward_frame_uniform_buffer.cpp
    src/render/forward_pipeline.cpp
    src/render/forward_recorder.cpp
    src/render/forward_recording_input.cpp
    src/render/forward_secondary_recording_worker.cpp
    src/render/frame_slot.cpp
    src/render/image.cpp
    src/render/instance_support.cpp
    src/render/presentation_state.cpp
    src/render/readback_buffer.cpp
    src/render/recording_context.cpp
    src/render/renderer.cpp
    src/render/resource_compiler.cpp
    src/render/shadow_map_policy.cpp
    src/render/spirv_loader.cpp
    src/render/swapchain.cpp
    src/render/swapchain_selection.cpp
    src/scene/scene.cpp
    src/scene/scene_node.cpp
)

target_include_directories(fireEngineTutorialEngine PUBLIC include)
target_include_directories(fireEngineTutorialEngine PRIVATE ${Stb_INCLUDE_DIR})

# Application-only helpers are shared with device-free tests without becoming
# part of the reusable engine or compiling the same implementation twice.
add_library(fireEngineTutorialAppSupport STATIC
    src/app/procedural_shadow_receiver.cpp
    src/app/tutorial_frame_descriptions.cpp
)
target_include_directories(fireEngineTutorialAppSupport PUBLIC src/app)
target_link_libraries(fireEngineTutorialAppSupport PUBLIC fireEngineTutorialEngine)

# The application owns only its event loop and high-level scene construction.
add_executable(fireEngineTutorial
    src/app/benchmark.cpp
    src/app/main.cpp
)
target_link_libraries(fireEngineTutorial PRIVATE fireEngineTutorialAppSupport)

# State the language requirement on every first-party target so each remains
# explicit if it is later moved into a larger parent CMake project.
target_compile_features(fireEngineTutorialEngine PUBLIC cxx_std_23)
target_compile_features(fireEngineTutorialAppSupport PUBLIC cxx_std_23)
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

# Compile the forward shader once into a single SPIR-V module. Slang finds every
# [shader(...)] entry point in the file, and SPIR-V permits any number of entry
# points per module, so both stages come from one compiler invocation and one
# file that src/render/forward_pipeline.cpp reads once.
#
# Slang emits SPIR-V 1.6 directly, which Vulkan 1.4 accepts, and every shader
# warning is treated as a build failure.
#
# -fvk-use-entrypoint-name preserves "vertexMain" and "fragmentMain" in the
# output. Those names must match the pName values in
# src/render/forward_pipeline.cpp.
# Slang only collapses an entry point to "main" when it emits exactly one, so
# this flag is what keeps the names stable if the shader is ever split up.
#
# DEPENDS lists the shader source and the compiler itself, so upgrading slangc
# through vcpkg forces a recompile. It does not track Slang `import`ed or
# `#include`d modules; add `-depfile` and DEPFILE once the shader spans more
# than this one file.
set(FIRE_ENGINE_FORWARD_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/forward.slang")
set(FIRE_ENGINE_FORWARD_SHADER_BINARY
    "${FIRE_ENGINE_SHADER_OUTPUT_DIRECTORY}/forward.spv")

add_custom_command(
    OUTPUT "${FIRE_ENGINE_FORWARD_SHADER_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FIRE_ENGINE_SHADER_OUTPUT_DIRECTORY}"
    COMMAND slang::slangc
        "${FIRE_ENGINE_FORWARD_SHADER_SOURCE}"
        -target spirv
        -profile spirv_1_6
        -matrix-layout-column-major
        -fvk-use-entrypoint-name
        -restrictive-capability-check
        -warnings-as-errors all
        -o "${FIRE_ENGINE_FORWARD_SHADER_BINARY}"
    DEPENDS "${FIRE_ENGINE_FORWARD_SHADER_SOURCE}" slang::slangc
    COMMENT "Compiling forward Slang shader to SPIR-V"
    VERBATIM
)

add_custom_target(fireEngineShaders ALL DEPENDS "${FIRE_ENGINE_FORWARD_SHADER_BINARY}")
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
fire_engine_enable_warnings(fireEngineTutorialAppSupport)
fire_engine_enable_warnings(fireEngineTutorial)

# Catch2 covers the Vulkan-free layers. The bounded application run remains a
# separate integration test for actual acquisition, rendering, and presentation.
enable_testing()

add_executable(fireEngineTutorialTests
    tests/animation/test_animation_playback.cpp
    tests/animation/test_animation_validation.cpp
    tests/app/test_procedural_shadow_receiver.cpp
    tests/app/test_tutorial_frame_descriptions.cpp
    tests/content/test_scene_content_validation.cpp
    tests/gltf/test_gltf_loader.cpp
    tests/graphics/test_asset_validation.cpp
    tests/graphics/test_frame_capture.cpp
    tests/graphics/test_render_preparation.cpp
    tests/math/test_mat4.cpp
    tests/render/test_camera.cpp
    tests/render/test_capture_format_mapping.cpp
    tests/render/test_directional_shadow_view.cpp
    tests/render/test_draw_binding_state.cpp
    tests/render/test_forward_recording_input.cpp
    tests/render/test_forward_secondary_recording_worker.cpp
    tests/render/test_shadow_map_policy.cpp
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
        fireEngineTutorialAppSupport
)
fire_engine_enable_warnings(fireEngineTutorialTests)

include(Catch)
catch_discover_tests(fireEngineTutorialTests
    PROPERTIES
        LABELS device-free
)

# These bounded scenarios retain the real application integration path. They
# cover imported animation, the shadow demonstration, changed preparation
# dependencies, the untextured fallback descriptor, and presentation-resource
# replacement on a Vulkan device.
add_test(NAME fireEngineTutorialAnimatedCubeSmoke
    COMMAND fireEngineTutorial --smoke basic
)
add_test(NAME fireEngineTutorialShadowSmoke
    COMMAND fireEngineTutorial --smoke shadow
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
# The parser accepts mode selectors and their modifiers in either order. Keep
# both forms so later options cannot accidentally restore position-dependent
# parsing.
add_test(NAME fireEngineTutorialFramesOptionsForwardOrder
    COMMAND fireEngineTutorial --frames 3 --recreate-every-frame
)
add_test(NAME fireEngineTutorialFramesOptionsReverseOrder
    COMMAND fireEngineTutorial --recreate-every-frame --frames 3
)
add_test(NAME fireEngineTutorialSmokeOptionsForwardOrder
    COMMAND fireEngineTutorial --smoke basic --forward-recording-participants 2
)
add_test(NAME fireEngineTutorialSmokeOptionsReverseOrder
    COMMAND fireEngineTutorial --forward-recording-participants 2 --smoke basic
)
add_test(NAME fireEngineTutorialBenchmarkCorrectness
    COMMAND fireEngineTutorial --benchmark 1
)
add_test(NAME fireEngineTutorialForwardDirectPrimaryBenchmarkCorrectness
    COMMAND fireEngineTutorial --benchmark 1 --forward-direct-primary
)
add_test(NAME fireEngineTutorialForwardSplitRecordingBenchmarkCorrectness
    COMMAND fireEngineTutorial --benchmark 2 --forward-recording-participants 2
)
# One draw cannot form two non-empty ranges, so this proves the fallback keeps
# the single-participant path working when a split is requested.
add_test(NAME fireEngineTutorialForwardSplitRecordingFallbackCorrectness
    COMMAND fireEngineTutorial --benchmark 1 --forward-recording-participants 2
)
add_test(NAME fireEngineTutorialForwardSplitRecordingMixedResourceSmoke
    COMMAND fireEngineTutorial --smoke prepare-twice --forward-recording-participants 2
)
# Without an override the workload policy selects the participant count. Cover
# both branches at the call site: the compile-time assertions only protect the
# calculation, not that the renderer consults it.
add_test(NAME fireEngineTutorialAutomaticForwardRecordingCorrectness
    COMMAND fireEngineTutorial --benchmark 2
)
add_test(NAME fireEngineTutorialAutomaticForwardSplitCorrectness
    COMMAND fireEngineTutorial --benchmark 10000
)

# Keep capture registration and its Vulkan-device properties together. A new
# scenario must not be able to omit validation rejection, serialization, or its
# label by appearing in one manually maintained list but not another.
function(fire_engine_add_capture_test)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "SYNC"
        "NAME;MODE;MINIMUM_RECREATIONS;MAXIMUM_RECREATIONS;OUTPUT_PATH" ""
    )
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown fire_engine_add_capture_test arguments: ${ARG_UNPARSED_ARGUMENTS}"
        )
    endif()
    if("${ARG_NAME}" STREQUAL "" OR "${ARG_MODE}" STREQUAL "" OR
       "${ARG_MINIMUM_RECREATIONS}" STREQUAL "" OR
       "${ARG_OUTPUT_PATH}" STREQUAL "")
        message(FATAL_ERROR
            "Capture tests require NAME, MODE, MINIMUM_RECREATIONS, and OUTPUT_PATH"
        )
    endif()

    set(CAPTURE_TEST_COMMAND
        ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        -DOUTPUT_PATH=${ARG_OUTPUT_PATH}
        -DCAPTURE_MODE=${ARG_MODE}
        -DMINIMUM_RECREATIONS=${ARG_MINIMUM_RECREATIONS}
    )
    if(NOT "${ARG_MAXIMUM_RECREATIONS}" STREQUAL "")
        list(APPEND CAPTURE_TEST_COMMAND
            -DMAXIMUM_RECREATIONS=${ARG_MAXIMUM_RECREATIONS}
        )
    endif()
    list(APPEND CAPTURE_TEST_COMMAND
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/run_frame_capture.cmake
    )

    set(CAPTURE_TEST_CONFIGURATIONS)
    if(ARG_SYNC AND CMAKE_CONFIGURATION_TYPES)
        list(APPEND CAPTURE_TEST_CONFIGURATIONS CONFIGURATIONS Debug)
    endif()
    add_test(NAME ${ARG_NAME}
        COMMAND ${CAPTURE_TEST_COMMAND}
        ${CAPTURE_TEST_CONFIGURATIONS}
    )
    # Keep this CTest-side guard in addition to the wrapper's own rejection.
    # Direct application tests rely on this property; wrapped capture tests
    # expose their child output so both independent routes remain live.
    set_tests_properties(${ARG_NAME} PROPERTIES
        FAIL_REGULAR_EXPRESSION "Vulkan validation error:"
        LABELS vulkan-device
        RESOURCE_LOCK fireEngineTutorialVulkan
        TIMEOUT 30
    )
    if(ARG_SYNC)
        set_tests_properties(${ARG_NAME} PROPERTIES
            ENVIRONMENT VK_LAYER_VALIDATE_SYNC=1
        )
    endif()
endfunction()

fire_engine_add_capture_test(
    NAME fireEngineTutorialFrameCaptureCorrectness
    MODE smoke-basic
    MINIMUM_RECREATIONS 0
    MAXIMUM_RECREATIONS 0
    OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/capture-test/animated-cube.png
)
# This structural capture uses wall-clock animation and deliberately asserts no
# image hash. It proves readback remains enabled through at least the two
# requested presentation replacements before frame three is captured; an
# additional platform startup resize is permitted.
fire_engine_add_capture_test(
    NAME fireEngineTutorialFrameCaptureRecreationCorrectness
    MODE recreate-every-frame
    MINIMUM_RECREATIONS 2
    OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/capture-test/recreated-frame.png
)

# These parser failures occur before GLFW or Vulkan initialization, so they
# remain device-free even though they exercise the application executable.
add_test(NAME fireEngineTutorialRepeatedOptionRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=Repeated option: --frames"
        -DARGUMENT_COUNT=4
        -DARGUMENT_1=--frames
        -DARGUMENT_2=1
        -DARGUMENT_3=--frames
        -DARGUMENT_4=2
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialMissingOptionValueRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--frames requires a value"
        -DARGUMENT_COUNT=1
        -DARGUMENT_1=--frames
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialUnknownOptionRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=Unknown option: --unknown-option"
        "-DEXPECTED_ERROR_2=Usage: fireEngineTutorial"
        -DARGUMENT_COUNT=1
        -DARGUMENT_1=--unknown-option
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialForwardDirectPrimaryWithoutBenchmarkRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--forward-direct-primary requires --benchmark"
        -DARGUMENT_COUNT=1
        -DARGUMENT_1=--forward-direct-primary
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialRecreationWithoutFramesRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--recreate-every-frame requires --frames"
        -DARGUMENT_COUNT=1
        -DARGUMENT_1=--recreate-every-frame
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialForwardRecordingParticipantsWithoutModeRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--forward-recording-participants requires --benchmark or --smoke"
        -DARGUMENT_COUNT=2
        -DARGUMENT_1=--forward-recording-participants
        -DARGUMENT_2=1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialForwardDirectPrimarySplitRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--forward-direct-primary records no forward secondary command buffer"
        -DARGUMENT_COUNT=5
        -DARGUMENT_1=--forward-recording-participants
        -DARGUMENT_2=2
        -DARGUMENT_3=--forward-direct-primary
        -DARGUMENT_4=--benchmark
        -DARGUMENT_5=1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialLegacyDirectPrimaryRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--direct-primary was renamed to --forward-direct-primary"
        -DARGUMENT_COUNT=3
        -DARGUMENT_1=--benchmark
        -DARGUMENT_2=1
        -DARGUMENT_3=--direct-primary
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialLegacyRecordingThreadsRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--recording-threads was renamed to --forward-recording-participants"
        -DARGUMENT_COUNT=4
        -DARGUMENT_1=--benchmark
        -DARGUMENT_2=1
        -DARGUMENT_3=--recording-threads
        -DARGUMENT_4=1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialIncompleteCaptureRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--capture and --capture-frame must be supplied together"
        -DARGUMENT_COUNT=4
        -DARGUMENT_1=--frames
        -DARGUMENT_2=3
        -DARGUMENT_3=--capture
        -DARGUMENT_4=unused.png
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialRepeatedCaptureRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=Repeated option: --capture"
        -DARGUMENT_COUNT=8
        -DARGUMENT_1=--frames
        -DARGUMENT_2=3
        -DARGUMENT_3=--capture
        -DARGUMENT_4=first.png
        -DARGUMENT_5=--capture
        -DARGUMENT_6=second.png
        -DARGUMENT_7=--capture-frame
        -DARGUMENT_8=1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialZeroCaptureFrameRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--capture-frame requires a positive integer"
        -DARGUMENT_COUNT=6
        -DARGUMENT_1=--frames
        -DARGUMENT_2=3
        -DARGUMENT_3=--capture
        -DARGUMENT_4=unused.png
        -DARGUMENT_5=--capture-frame
        -DARGUMENT_6=0
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialMalformedCaptureFrameRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--capture-frame requires a positive integer"
        -DARGUMENT_COUNT=6
        -DARGUMENT_1=--frames
        -DARGUMENT_2=3
        -DARGUMENT_3=--capture
        -DARGUMENT_4=unused.png
        -DARGUMENT_5=--capture-frame
        -DARGUMENT_6=invalid
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialUnboundedCaptureRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--capture requires --frames or --smoke"
        -DARGUMENT_COUNT=4
        -DARGUMENT_1=--capture
        -DARGUMENT_2=unused.png
        -DARGUMENT_3=--capture-frame
        -DARGUMENT_4=1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialSmokeCaptureFrameOutOfRangeRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--capture-frame exceeds the requested frame limit"
        -DARGUMENT_COUNT=6
        -DARGUMENT_1=--smoke
        -DARGUMENT_2=basic
        -DARGUMENT_3=--capture
        -DARGUMENT_4=unused.png
        -DARGUMENT_5=--capture-frame
        -DARGUMENT_6=4
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialFrameCaptureFrameOutOfRangeRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--capture-frame exceeds the requested frame limit"
        -DARGUMENT_COUNT=6
        -DARGUMENT_1=--frames
        -DARGUMENT_2=2
        -DARGUMENT_3=--capture
        -DARGUMENT_4=unused.png
        -DARGUMENT_5=--capture-frame
        -DARGUMENT_6=3
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
add_test(NAME fireEngineTutorialBenchmarkCaptureRejected
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:fireEngineTutorial>
        "-DEXPECTED_ERROR=--capture cannot be combined with --benchmark"
        -DARGUMENT_COUNT=6
        -DARGUMENT_1=--benchmark
        -DARGUMENT_2=1
        -DARGUMENT_3=--capture
        -DARGUMENT_4=unused.png
        -DARGUMENT_5=--capture-frame
        -DARGUMENT_6=1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_command_failure.cmake
)
set_tests_properties(
    fireEngineTutorialRepeatedOptionRejected
    fireEngineTutorialMissingOptionValueRejected
    fireEngineTutorialUnknownOptionRejected
    fireEngineTutorialForwardDirectPrimaryWithoutBenchmarkRejected
    fireEngineTutorialRecreationWithoutFramesRejected
    fireEngineTutorialForwardRecordingParticipantsWithoutModeRejected
    fireEngineTutorialForwardDirectPrimarySplitRejected
    fireEngineTutorialLegacyDirectPrimaryRejected
    fireEngineTutorialLegacyRecordingThreadsRejected
    fireEngineTutorialIncompleteCaptureRejected
    fireEngineTutorialRepeatedCaptureRejected
    fireEngineTutorialZeroCaptureFrameRejected
    fireEngineTutorialMalformedCaptureFrameRejected
    fireEngineTutorialUnboundedCaptureRejected
    fireEngineTutorialSmokeCaptureFrameOutOfRangeRejected
    fireEngineTutorialFrameCaptureFrameOutOfRangeRejected
    fireEngineTutorialBenchmarkCaptureRejected
    PROPERTIES
        LABELS device-free
        TIMEOUT 5
)

# Synchronization validation is compiled into Debug configurations only. Keep
# its device scenarios out of Release test lists instead of registering silent
# duplicates with an environment variable that cannot enable the absent layer.
set(FIRE_ENGINE_SYNC_VALIDATION_TESTS)
set(FIRE_ENGINE_SHADOW_DEMONSTRATION_TESTS fireEngineTutorialShadowSmoke)
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
    add_test(NAME fireEngineTutorialShadowSyncValidation
        COMMAND fireEngineTutorial --smoke shadow
        ${FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS}
    )
    list(APPEND FIRE_ENGINE_SHADOW_DEMONSTRATION_TESTS
        fireEngineTutorialShadowSyncValidation
    )
    add_test(NAME fireEngineTutorialForwardDirectPrimarySyncValidation
        COMMAND fireEngineTutorial --benchmark 1 --forward-direct-primary
        ${FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS}
    )
    add_test(NAME fireEngineTutorialForwardSplitRecordingSyncValidation
        COMMAND fireEngineTutorial --benchmark 2 --forward-recording-participants 2
        ${FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS}
    )
    add_test(NAME fireEngineTutorialForwardSplitRecordingMixedResourceSyncValidation
        COMMAND fireEngineTutorial --smoke prepare-twice --forward-recording-participants 2
        ${FIRE_ENGINE_SYNC_VALIDATION_CONFIGURATIONS}
    )
    fire_engine_add_capture_test(
        NAME fireEngineTutorialFrameCaptureSyncValidation
        MODE smoke-basic
        MINIMUM_RECREATIONS 0
        MAXIMUM_RECREATIONS 0
        OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/capture-test/animated-cube-sync.png
        SYNC
    )
    fire_engine_add_capture_test(
        NAME fireEngineTutorialFrameCaptureRecreationSyncValidation
        MODE recreate-every-frame
        MINIMUM_RECREATIONS 2
        OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/capture-test/recreated-frame-sync.png
        SYNC
    )
    # The ordinary benchmark controls check CLI/reporting and standard validation;
    # these separate runs deliberately enable synchronization checks.
    set(FIRE_ENGINE_SYNC_VALIDATION_TESTS
        fireEngineTutorialPrepareTwiceSyncValidation
        fireEngineTutorialRecreationSyncValidation
        fireEngineTutorialShadowSyncValidation
        fireEngineTutorialForwardDirectPrimarySyncValidation
        fireEngineTutorialForwardSplitRecordingSyncValidation
        fireEngineTutorialForwardSplitRecordingMixedResourceSyncValidation
    )
    set_tests_properties(${FIRE_ENGINE_SYNC_VALIDATION_TESTS} PROPERTIES
        ENVIRONMENT VK_LAYER_VALIDATE_SYNC=1
    )
endif()

set_tests_properties(
    fireEngineTutorialAnimatedCubeSmoke
    fireEngineTutorialShadowSmoke
    fireEngineTutorialPrepareTwiceSmoke
    fireEngineTutorialUntexturedSmoke
    fireEngineTutorialRecreationSmoke
    fireEngineTutorialFramesOptionsForwardOrder
    fireEngineTutorialFramesOptionsReverseOrder
    fireEngineTutorialSmokeOptionsForwardOrder
    fireEngineTutorialSmokeOptionsReverseOrder
    fireEngineTutorialBenchmarkCorrectness
    fireEngineTutorialForwardDirectPrimaryBenchmarkCorrectness
    fireEngineTutorialForwardSplitRecordingBenchmarkCorrectness
    fireEngineTutorialForwardSplitRecordingFallbackCorrectness
    fireEngineTutorialForwardSplitRecordingMixedResourceSmoke
    fireEngineTutorialAutomaticForwardRecordingCorrectness
    fireEngineTutorialAutomaticForwardSplitCorrectness
    ${FIRE_ENGINE_SYNC_VALIDATION_TESTS}
    PROPERTIES
        FAIL_REGULAR_EXPRESSION "Vulkan validation error:"
        LABELS vulkan-device
        RESOURCE_LOCK fireEngineTutorialVulkan
        TIMEOUT 30
)

# The exact geometry and participation policy are device-free contracts. These
# scenarios prove that the demonstration selection reaches the real frame path.
set_tests_properties(${FIRE_ENGINE_SHADOW_DEMONSTRATION_TESTS} PROPERTIES
    PASS_REGULAR_EXPRESSION "Shadow demonstration content: 2 forward draws, 1 object marked as a shadow caster[.]"
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
# The report contains no vertical-bar separators. Use a negated character class
# to span its lines: CTest's conventional `(.|\n)*` becomes pathologically slow
# when the leading fragment is present but the required trailing fragment is
# deliberately removed by the assertion's positive control.
set_tests_properties(fireEngineTutorialForwardSplitRecordingBenchmarkCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Forward secondary recording participants: 2 forced, 2 effective[^|]*forward participant 1 recording"
)
set_tests_properties(fireEngineTutorialForwardSplitRecordingFallbackCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Forward secondary recording participants: 2 forced, 1 effective"
)
set_tests_properties(fireEngineTutorialAutomaticForwardRecordingCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Forward secondary recording participants: automatic[^\n]*, 1 effective"
)
set_tests_properties(fireEngineTutorialAutomaticForwardSplitCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Forward secondary recording participants: automatic[^\n]*, 2 effective"
)
set_tests_properties(fireEngineTutorialForwardDirectPrimaryBenchmarkCorrectness PROPERTIES
    PASS_REGULAR_EXPRESSION "Forward recording path: direct primary command buffer[^|]*Its forward secondary pool has no command-buffer allocations or recorded work."
)
# Run one scenario away from the executable and copied assets to prove that the
# compiled absolute asset and shader paths do not depend on the working folder.
set(FIRE_ENGINE_SMOKE_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke-work")
file(MAKE_DIRECTORY "${FIRE_ENGINE_SMOKE_WORKING_DIRECTORY}")
set_tests_properties(fireEngineTutorialAnimatedCubeSmoke PROPERTIES
    WORKING_DIRECTORY "${FIRE_ENGINE_SMOKE_WORKING_DIRECTORY}"
)
