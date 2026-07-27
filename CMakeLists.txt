# Require a CMake version new enough for the project and vcpkg features used
# below. Configuration stops with a clear error when an older version is used.
cmake_minimum_required(VERSION 3.20)

# Declare the project metadata and tell CMake that this repository contains
# C++ sources. No C compiler is needed for this tutorial.
project(fire_engine_tutorial
    VERSION 0.1.0
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

# Locate Vulkan 1.4 headers and the loader supplied by the vcpkg toolchain.
# REQUIRED makes configuration fail immediately if that dependency is missing
# or older than Vulkan 1.4.
find_package(Vulkan 1.4 REQUIRED)

# Strict warnings keep tutorial code honest. The option allows a consumer to
# disable only the "warnings are errors" policy when experimenting locally.
option(FIRE_ENGINE_TUTORIAL_WARNINGS_AS_ERRORS
    "Treat first-party compiler warnings as errors"
    ON
)

# Build the tutorial's single translation unit as an executable.
add_executable(fireEngineTutorial
    src/main.cpp
)

# State the language requirement on the target as well, so it remains explicit
# if this executable is later moved into a larger parent CMake project.
target_compile_features(fireEngineTutorial PRIVATE cxx_std_23)

# Removing Vulkan-Hpp's convenience constructors makes Vulkan structures
# aggregates, allowing the clear designated-initializer style used in main.cpp.
target_compile_definitions(fireEngineTutorial PRIVATE
    VULKAN_HPP_NO_CONSTRUCTORS
)

# Vulkan::Vulkan is CMake's imported target for both the Vulkan include path and
# the platform's Vulkan loader library.
target_link_libraries(fireEngineTutorial PRIVATE
    Vulkan::Vulkan
)

# MSVC and Clang/GCC spell equivalent warning options differently. Keep warning
# levels consistent across platforms and add the appropriate error flag when
# the option above is enabled.
if(MSVC)
    target_compile_options(fireEngineTutorial PRIVATE /W4)
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
# it linked to Vulkan and completed the RAII instance create/destroy lifecycle.
enable_testing()
add_test(NAME fireEngineTutorial COMMAND fireEngineTutorial)
