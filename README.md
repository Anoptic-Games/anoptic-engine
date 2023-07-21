# Anoptic Game Engine

![Anoptic Engine Logo](/resources/logo.png)

Anoptic Engine is a high-performance and dynamic game engine designed to handle large numbers of entities and events occurring simultaneously. 
Whether you're developing an RPG, RTS, 4X game, immersive sim, colony manager, high player-count multiplayer, or any other complex simulation, Anoptic Engine provides the tools and flexibility you need.

## Features

- **High Performance**: Anoptic Engine is optimized to handle the demanding requirements of games with numerous entities and complex simulations.
- **Dynamic Entity System**: Easily create and manage large numbers of entities with dynamic properties and behaviors.
- **Event-driven Architecture**: Design and handle events that occur in your game world with ease, allowing for complex interactions and immersive experiences.
- **Flexible Customization**: Tailor the engine to suit your specific game design needs using a highly flexible and extensible architecture.
- **Graphics and Rendering**: Leverage advanced rendering techniques and graphics capabilities to create stunning visual experiences.
- **Cross-platform Compatibility**: Develop games that can run on multiple platforms, including Windows, macOS, and Linux.
- **Networking Support**: Implement multiplayer functionality and networked gameplay with Anoptic Engine's built-in networking support.
- **Audio and Sound Effects**: Create immersive audio experiences with support for various audio formats and sound effects.


## Initial Setup

Clone this repo and its submodules, then follow your platform-specific build instructions:
```bash
git clone --recursive https://github.com/Anoptic-Games/anoptic-engine.git
```

### Building on Linux

All builds of Anoptic Engine require sure you have the latest version of `gcc 13` and `CMake`. Make sure to include both of these in your path.

cd into the repository and run `build.sh {build-type}`, where {build-type} is 1 for Release, 2 for Debug, and 3 for Tests.

The resulting binaries can be found under ``build/{build-type}/``.

### Building on Windows

Make sure you have CMake installed and in your path: https://cmake.org/install/.

Acquire a copy of the Mingw-w64 toolkit from the official website (https://www.mingw-w64.org/) or through a package manager like MSYS2. Ensure it's added to your system's PATH variable.

We recommend MSYS2 for the easiest installation process, which can be acquired here: https://www.msys2.org/. 
Follow Microsoft's documentation here https://learn.microsoft.com/en-us/vcpkg/users/platforms/mingw to make sure you have the Mingw-w64 toolkit installed and working.

Once Mingw-w64 is installed with ``gcc`` working on your system, go to the repository and run `build.bat {build-type}`, where {build-type} is 1 for Release, 2 for Debug, and 3 for Tests.

### Warning

Vulkan rendering backend may have an incompatibility with MSI Afterburner / RivaTuner Statistics Server at this time. 
If you experience errors with the swapchain creation, please try disabling MSI Afterburner / RTSS and try again.

## More

Check out the .md files in each subdirectory for more information.