# Fire Engine Tutorial
[![CI](https://github.com/nnewson/fireEngine-tutorial/actions/workflows/ci.yml/badge.svg)](https://github.com/nnewson/fireEngine-tutorial/actions/workflows/ci.yml)

A tutorial for building a 3D engine step-by-step.
Based on [FireEngine](https://github.com/nnewson/fireEngine) and documented
under the development blog at [nnewson.github.io](https://nnewson.github.io)

## Build and run

vcpkg is used to manage dependencies. Install it from
[vcpkg.io](https://vcpkg.io/en/getting-started.html).

Set `VCPKG_ROOT` to a vcpkg checkout, then configure and build:

```sh
cmake --preset vcpkg
cmake --build --preset default
./build/fireEngineTutorial
```

Or run the same executable through CTest:

```sh
ctest --preset default
```

A Vulkan implementation must be installed at runtime.

On macOS, this tutorial targets the KosmicKrisp technical preview from the
[LunarG Vulkan SDK](https://vulkan.lunarg.com/doc/sdk/1.4.357.0/mac/getting_started.html).
It requires Apple Silicon and macOS 26. Load the SDK environment and select its
driver before running:

```sh
source ~/VulkanSDK/1.4.357.0/setup-env.sh
export VK_DRIVER_FILES="$VULKAN_SDK/share/vulkan/icd.d/libkosmickrisp_icd.json"
./build/fireEngineTutorial
```

The executable links to the Vulkan loader. It does not directly link
KosmicKrisp or enable the unsupported Vulkan portability extensions.
