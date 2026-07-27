# Fire Engine Tutorial

A tutorial for building a 3D engine step-by-step.
Based on [FireEngine](https://github.com/nnewson/fireEngine) and documented
under the development blog at [nnewson.github.io](https://nnewson.github.io)

## Build and run

vpkg is used to manage dependencies. Install it from
[vcpkg.io](https://vcpkg.io/en/getting-started.html).

Set `VCPKG_ROOT` to a vcpkg checkout, then configure and build:

```sh
cmake --preset vcpkg
cmake --build --preset default
./build/vulkan_instance
```

Or run the same executable through CTest:

```sh
ctest --preset default
```

A Vulkan implementation must be installed at runtime: for example, MoltenVK on
macOS or a Vulkan-capable graphics driver on Linux or Windows.