# Resources Directory

The `resources/` directory serves as a central place for various built-in resources needed by the engine.


## Directory Structure

The directory structure for `resources/` might look something like this:

```
resources/
├── shaders/          # Shaders used by the engine
├── textures/         # Default or fallback textures
├── fonts/            # Fonts used by the engine
├── audio/            # Audio files, like default sound effects or music
└── scripts/          # Engine scripts, like default AI scripts or game logic
```

## Purpose of Each Subdirectory

- `shaders/`: This directory contains GLSL (or any other shading language) files that are used for various rendering tasks in the game engine, such as lighting, shadows, or post-processing effects.

- `textures/`: This directory contains any textures used by the game engine. This could include default or fallback textures that are used when a game-specific texture is not available.

- `fonts/`: This directory contains any font files that the engine uses. This could be used for rendering text in the game, in the engine's debug UI, or elsewhere.

- `audio/`: This directory includes any audio files used by the game engine. This could include default sound effects, background music, or any other audio.

- `scripts/`: This directory contains scripts that define various behaviours in the engine. These could be default AI scripts, game logic, or other types of scripts.


## Usage

The resources in this directory should be used as fallbacks or defaults for when a game does not provide its own resources. The game should be able to override or supplement these resources with its own.

In general, game-specific resources should not be put in this directory; instead, they should be included with the game itself. This helps keep the engine generic and reusable across multiple games.

