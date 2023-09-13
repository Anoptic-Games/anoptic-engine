# Build System

The `cmake/` directory in the game engine project contains the public header files for the engine. These header files define the public API that games will use to interact with the engine.

## Directory Structure

The directory structure for `cmake/` might look something like this:

```
cmake/
├── platforms/            # Toolchain files for cross-compiling
└── config/               # Configuration files, presets
```

## Purpose of Each Subdirectory

- `platforms/`: Contains the toolchain  files for compiling on different platforms and compilers. Specifies which tools and architectures to be used.

- `config/`: Holds CMake configuration files for packaging, installation, and global properties of CMake presets.