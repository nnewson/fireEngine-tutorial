# Require a CMake version new enough for the project and vcpkg features used
# below. Configuration stops with a clear error when an older version is used.
cmake_minimum_required(VERSION 3.20)

# Declare the project metadata and tell CMake that this repository contains
# C++ sources. No C compiler is needed for this tutorial.
project(fire_engine_tutorial
    VERSION 0.3.0
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

# Locate the window-system integration, memory allocator, Vulkan 1.4 headers,
# and loader supplied by the vcpkg toolchain.
find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanHeaders 1.4 CONFIG REQUIRED)
find_package(VulkanLoader 1.4 CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

# Strict warnings keep tutorial code honest. The option allows a consumer to
# disable only the "warnings are errors" policy when experimenting locally.
option(FIRE_ENGINE_TUTORIAL_WARNINGS_AS_ERRORS
    "Treat first-party compiler warnings as errors"
    ON
)

# Build the tutorial checkpoint as an executable.
add_executable(fireEngineTutorial
    src/core/debug.cpp
    src/main.cpp
    src/platform/glfw.cpp
    src/platform/window.cpp
    src/render/allocator.cpp
    src/render/device.cpp
    src/render/swapchain.cpp
)

target_include_directories(fireEngineTutorial PRIVATE include)

# State the language requirement on the target as well, so it remains explicit
# if this executable is later moved into a larger parent CMake project.
target_compile_features(fireEngineTutorial PRIVATE cxx_std_23)

# Pass the project version to the Vulkan application metadata so the version
# reported to the driver always matches the one declared by project() above.
target_compile_definitions(fireEngineTutorial PRIVATE
    FIRE_ENGINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FIRE_ENGINE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FIRE_ENGINE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# Removing Vulkan-Hpp's convenience constructors makes Vulkan structures
# aggregates. The context uses the linked loader's vkGetInstanceProcAddr rather
# than Vulkan-Hpp's optional dlopen-based loader discovery helper.
target_compile_definitions(fireEngineTutorial PRIVATE
    VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0
    VULKAN_HPP_NO_CONSTRUCTORS
)

# Validation is most useful while developing and has a runtime cost, so it is
# tied to the Debug configuration. A generator expression is used rather than a
# CMAKE_BUILD_TYPE test because multi-configuration generators such as Visual
# Studio choose the configuration at build time rather than at configure time.
target_compile_definitions(fireEngineTutorial PRIVATE
    $<$<CONFIG:Debug>:FIRE_ENGINE_ENABLE_VALIDATION>
)

# GLFW creates the platform surface; Vulkan::Loader supplies the dispatching
# library that discovers the machine's installed Vulkan drivers. VMA manages
# the memory allocations that later render resources will use.
target_link_libraries(fireEngineTutorial PRIVATE
    GPUOpen::VulkanMemoryAllocator
    glfw
    Vulkan::Headers
    Vulkan::Loader
)

# MSVC and Clang/GCC spell equivalent warning options differently. Keep warning
# levels consistent across platforms and add the appropriate error flag when
# the option above is enabled.
if(MSVC)
    # Imported dependency headers are marked external. Lower their warning level
    # so VMA's implementation is not held to first-party /W4 /WX policy.
    target_compile_options(fireEngineTutorial PRIVATE /W4 /external:W0)
    if(FIRE_ENGINE_TUTORIAL_WARNINGS_AS_ERRORS)
        target_compile_options(fireEngineTutorial PRIVATE /WX)
    endif()
else()
    target_compile_options(fireEngineTutorial PRIVATE -Wall -Wextra -Wpedantic)
    if(FIRE_ENGINE_TUTORIAL_WARNINGS_AS_ERRORS)
        target_compile_options(fireEngineTutorial PRIVATE -Werror)
    endif()
endif()

# Register the executable as a CTest smoke test. A successful test proves that
# it created a Vulkan 1.4 device, VMA allocator, and window swapchain with views.
enable_testing()
add_test(NAME fireEngineTutorial COMMAND fireEngineTutorial)
set_tests_properties(fireEngineTutorial PROPERTIES TIMEOUT 30)
