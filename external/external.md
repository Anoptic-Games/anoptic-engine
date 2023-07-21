# External Directory

The `external/` directory stores third-party source libraries and dependencies for the Anoptic Game Engine. Its purpose is to cleanly separate the engine's native codebase from external components, ensuring clear boundaries for maintenance and licensing.

## Directory Structure

Typical layout:
```
external/
├── glfw/           # GLFW for windowing and input
└── others/         # Other third-party libraries or tools
```

## Purpose of Each Subdirectory

- **`glfw/`**: [GLFW](https://www.glfw.org/) - A library for OpenGL, Vulkan, windows, contexts, and input.

- **`others/`**: A placeholder for other third-party libraries.

## Usage

To use libraries from `external/`, reference them in the engine's source files and adjust CMake configurations.
  ``#include <external/[library_name]/header_file.h>``


## Git Submodules

Third-party libraries hosted on platforms like GitHub can be integrated as Git submodules. 

Procedure:
- **Adding a Submodule**:
  ``git submodule add [repository_url] external/[library_name]``

- **Updating a Submodule**:
  Navigate to the submodule directory and pull the desired updates or use:
  ``git submodule update --remote external/[library_name]``

- **Cloning a Project with Submodules**:
  When cloning a project with submodules, the submodules will initially be empty. To fetch all data:
  ``git clone --recursive [project_url]``
  Or, if you've already cloned the project:
  ``git submodule update --init --recursive``

## CMake Integration

External libraries typically provide CMakeLists.txt for easier integration.

1. **Include Library**:
  `add_subdirectory(${CMAKE_SOURCE_DIR}/external/[library_name])`
2. **Link to Target**:
  `target_link_libraries(anopticengine [library_target])`

## Licensing

When integrating new libraries, verify their licenses and update the "ATTRIBUTIONS.md" document in the root directory.