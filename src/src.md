# Source Directory

The `src/` directory is the root directory of the game engine's source code.

``main.c``, the entry point of the engine, is here. 
(This is subject to change as the engine expands and the entry point for a final game build is moved to the games themselves, with the engine core becoming a dynamic library).

Other subdirectories within ``src/`` are for the various submodules

## Directory Structure
TODO: Crystalize

The directory structure for `src/` might look something like this:

```
src/
├── graphics/       # Graphics rendering code
├── core/           # Core data management like ECS and memory management
├── audio/          # Audio processing code
├── physics/        # Physics simulation code
├── input/          # Input handling code
├── scripting/      # Game scripting interfaces
└── utils/          # General utility and helper functions
```

## Purpose of Each Subdirectory
TODO: Crystalize

- `graphics/`: This directory contains the code for rendering graphics, processing shaders, handling textures, and any other tasks related to graphics.

- `core/`: This directory contains the core functionality of the engine, including entity-component-system (ECS) management, memory management, and other central systems of the engine.

- `audio/`: This directory contains the code for playing audio, processing audio files, and handling any other audio-related tasks.

- `physics/`: This directory contains the code for the physics engine, including collision detection, physics simulation, and any other physics-related tasks.

- `input/`: This directory contains the code for handling user input, such as keyboard, mouse, or game controller input.

- `scripting/`: This directory contains the interfaces for the game's scripting system, which allows game developers to script game behavior using a scripting language.

- `utils/`: This directory contains any utility or helper functions that are used throughout the engine, such as math functions, data structures, or debugging tools.


## Usage

The `src/` directory contains the implementation of the game engine. Each subdirectory corresponds to a major component of the engine, and the organization of these directories and files should help to keep the codebase clean and understandable. Code related to a specific component of the engine should be placed in the corresponding subdirectory.

While the source code in `src/` comprises the implementation details of the engine, the game should primarily interface with the engine through the public API in the `include/` directory. The code in `src/` should be thought of as "internal" to the engine, while the headers in `include/` expose a public, stable API for games to use.
