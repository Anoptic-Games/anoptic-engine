# Build System

The `cmake/` directory in contains build system toolchain configurations.

## Directory Structure

The directory structure for `cmake/`:

```
cmake/
└── platforms/            # Toolchain files for cross-compiling
```

## Purpose of Each Subdirectory

- `platforms/`: Contains the toolchain  files for compiling on different platforms and compilers. Specifies which tools and architectures to be used.