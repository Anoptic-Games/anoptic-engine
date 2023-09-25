# Anoptic Game Engine

The Anoptic Game Engine is an ECS-first runtime designed to handle large numbers of entities and events occurring simultaneously. It generalized for RPGs, RTS, 4X, immersive sims, colony managers, or any other kinds of complex simulation.

## Getting this Repo

Make sure to use `--recursive` to fetch all submodules!

```bash
git clone --recursive https://github.com/Anoptic-Games/anoptic-engine.git
```

## Features

- **ECS**: Easily create and manage large numbers of entities with dynamic properties and behaviors.
- **Event-Driven**: Design and handle events that occur in your games, allowing for complex interactions between different systems.
- **Vulkan Renderer**: Vulkan rendering backend allows for the most modern approaches in high-performance graphics, on all supported platforms.
- **Custom Memory Allocation**: Uses mimalloc as a fast malloc() implementation, and provides a rich toolbox of optimized data structures and specialized allocators.
- **Modular Systems**: Tailor the engine to suit your specific design needs using a highly flexible and extensible architecture.
- **Cross-platform Compatibility**: Develop games that can run with full feature parity on both Linux and Windows.
- **Networking Support**: Implement multiplayer functionality and networked gameplay with Anoptic Engine's built-in networking support, enabled by ECS and smart memory allocation strategies.
- **Audio and Sound Effects**: Create immersive audio experiences with support for HRTF and other advanced systems.

### Building on Linux

All builds of Anoptic Engine require sure you have the latest version of `clang 17` and `CMake`. Make sure to include both of these in your path.

cd into the repository and run `build.sh {build-type}`, where {build-type} is 1 for Release, 2 for Debug, and 3 for Tests.

The resulting binaries can be found under ``build/{build-type}/``.

### Building on Windows

Make sure you have CMake installed and in your path: https://cmake.org/install/.

Acquire a copy of the Mingw-w64 toolkit from the official website (https://www.mingw-w64.org/) or through a package manager like MSYS2. Ensure it's added to your system's PATH variable.

We recommend MSYS2 for the easiest installation process, which can be acquired here: https://www.msys2.org/. 
Follow Microsoft's documentation here https://learn.microsoft.com/en-us/vcpkg/users/platforms/mingw to make sure you have the Mingw-w64 toolkit installed and working.

Once Mingw-w64 is installed with ``gcc`` working on your system, go to the repository and run `build.bat {build-type}`, where {build-type} is 1 for Release, 2 for Debug, and 3 for Tests.

### Troubleshooting

Vulkan rendering backend may have an incompatibility with MSI Afterburner / RivaTuner Statistics Server at this time.
If you experience errors with the swapchain creation, please try disabling MSI Afterburner / RTSS and try again.

## More

Check out the .md files in each subdirectory for more information on a given module.