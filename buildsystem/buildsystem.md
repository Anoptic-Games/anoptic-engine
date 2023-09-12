# Build System

The `buildsystem/` directory in the game engine project contains the public header files for the engine. These header files define the public API that games will use to interact with the engine.

## Directory Structure

The directory structure for `buildsystem/` might look something like this:

```plaintext
cmake/
├── platforms/            # Toolchain files for cross-compiling
├── scripts/              # Custom CMake scripts
├── templates/            # File or project templates
└── config/               # Configuration files, presets
```

## Purpose of Each Subdirectory

- `platforms/`: Contains the toolchain  files for compiling on different platforms and compilers. Specifies which tools and architectures to be used.

- `scripts/`: Contains scripts for automating build tasks like code generation, documentation, build steps, etc.

- `templates/`: Contains project and file templates for generating new parts of the engine, such as new modules or components.

- `config/`: Holds CMake configuration files for packaging, installation, and global properties of CMake presets.


## Usage

The `buildsystem/` directory is mainly used by the CMake build system and typically doesn't have to be interacted with directly by game developers using the engine. However, engine developers will find it useful for adding new features, dependencies, or platforms to the build system.