# Include Directory

The `include/` directory in the game engine project contains the public header files for the engine. These header files define the public API that games will use to interact with the engine.

## Directory Structure
TODO: Crystalize

The directory structure for `include/` might look something like this:

```plaintext
include/
├── graphics/       # Public graphics API
├── audio/          # Public audio API
├── physics/        # Public physics API
├── input/          # Public input API
├── scripting/      # Public scripting API
└── utils/          # Public utilities API
```

## Purpose of Each Subdirectory
TODO: Crystalize

- `graphics/`: This directory contains the public API for the engine's graphics system. It includes headers defining structures, functions, and constants related to rendering graphics.

- `audio/`: This directory contains the public API for the engine's audio system. It includes headers defining structures, functions, and constants for handling audio.

- `physics/`: This directory contains the public API for the engine's physics system. It includes headers defining structures, functions, and constants related to physics simulation.

- `input/`: This directory contains the public API for the engine's input system. It includes headers defining structures, functions, and constants for handling user input.

- `scripting/`: This directory contains the public API for the engine's scripting system. It includes headers defining structures, functions, and constants for scripting game behavior.

- `utils/`: This directory contains the public API for the engine's utilities. It includes headers defining utility structures and functions that games may find useful, such as math functions or data structures.


## Usage

The `include/` directory contains the public API of the game engine. This is the interface that games, editors, and external modules should use to interact with the engine. 

While the implementation details of the engine are contained within the `src/` directory, they should largely be invisible to the game. The game should interact with the engine through the interfaces, structures, functions, and constants defined in the `include/` directory.

This approach allows for the internals of the engine to change without affecting games that use it, as long as the public API remains consistent. This separation of public and private code helps to improve the maintainability and usability of the engine.
