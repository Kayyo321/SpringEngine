# SpringEngine

SpringEngine is a game runtime, not a game-specific executable.

You install SpringEngine once on your system (similar to a language runtime), then run separately downloaded games that target the SpringEngine runtime.

## Vision

- SpringEngine is distributed as a standalone runtime executable.
- Games are distributed separately from the engine.
- A game package contains game logic modules + assets, and is executed by an installed SpringEngine runtime.
- SpringEngine provides the platform layer (windowing, rendering, input, audio, filesystem, lifecycle).
- Game packages provide gameplay code and content.

## Runtime Model

- Runtime binary: `springengine`
- Game target: loadable module (e.g., `.dylib` on macOS)
- Contract: stable C ABI between runtime and game module
- Execution flow:
	1. User installs SpringEngine.
	2. User downloads a SpringEngine-compatible game package.
	3. User runs the game through SpringEngine.

## Long-Term Direction

- Keep engine/runtime and game code as separate deliverables.
- Version and validate ABI compatibility at launch.
- Provide an SDK surface for game developers to build SpringEngine-compatible games.

## C Project Build System

Project layout:

- `src/` → C source and header files
- `lib/` → prebuilt libraries (`.a`, `.so`, `.dylib`) to link (recursive, static-first)
- `bin/` → build output binaries

### Commands

- Build: `make`
- Rebuild: `make re`
- Clean all object files (`.o`): `make clean`
- Clean objects + binary: `make fclean`

By default, the binary is generated at `bin/springengine`.
