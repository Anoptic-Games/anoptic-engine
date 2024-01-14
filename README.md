## Anoptic Game Engine

The Anoptic Game Engine is designed to create games that can handle large numbers of events and entities occurring simultaneously. It is tailored for 4X, RTS, or any other kind of complex simulation.

### Getting this Repo

Make sure to use `--recursive` to fetch all submodules!

```bash
git clone --recursive https://github.com/Anoptic-Games/anoptic-engine.git
```

### Runtime Features

- **ECS**: Easily create and manage large numbers of entities with dynamic properties and behaviors.
- **Events**: Enable interactions between different systems via a generalized event bus.
- **Vulkan Renderer**: Vulkan rendering backend to enable the use of the latest GPU capabilities.
- **Custom Allocators**: Uses mimalloc for a fast global allocator implementation, as well as several special-purpose local allocators.
- **Platform Compatibility**: Built and tested for full feature parity on both Linux and Windows.
- **Networking**: Built-in networking support for p2p or authoritative server.

### Installation

All builds of Anoptic Engine require the latest version of `clang 17`, `CMake`, and the `Vulkan SDK`.

Acquire a copy of the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/), version 1.3.2 or later.

#### Building on Linux

Use `build.sh {build-type}`, where {build-type} is 1 for Release, 2 for Debug, and 3 for Tests.

The resulting binaries can be found under `build/{build-type}/`.

#### Building on Windows

Make sure you have `CMake` installed and in your path: https://cmake.org/install/.

Have a copy of the `Mingw-w64` toolkit in your path.
We recommend [MSYS2](https://www.msys2.org/) with the [mingw-w64-x64_64-clang](https://packages.msys2.org/package/mingw-w64-x86_64-clang) package. 

Additional guidance:
- [Microsoft Documentation](https://learn.microsoft.com/en-us/vcpkg/users/platforms/mingw)
- [CLion Configuration](https://www.jetbrains.com/help/clion/quick-tutorial-on-configuring-clion-on-windows.html#clang-mingw)

Once Mingw-w64 is installed with `gcc` working on your system, go to the repository and run 

Use `build.bat {build-type}`, where {build-type} is 1 for Release, 2 for Debug, and 3 for Tests.

### More

Check out the `.md` files in each subdirectory for more information on a given module.