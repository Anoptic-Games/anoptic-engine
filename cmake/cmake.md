# Build System

The `cmake/` directory in contains build system toolchain configurations.

## Directory Structure

The directory structure for `cmake/`:

```
cmake/
├── platforms/            # Toolchain files for cross-compiling
└── config/               # Configuration files, presets
```

## Purpose of Each Subdirectory

- `platforms/`: Contains the toolchain  files for compiling on different platforms and compilers. Specifies which tools and architectures to be used.

- `config/`: Holds CMake configuration files for packaging, installation, and global properties of CMake presets.