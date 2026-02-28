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
- Game target: packaged project (assets + Lua scripts + metadata)
- Optional extension target: loadable native module (e.g., `.dylib` on macOS)
- Contract: stable engine API and script API versioning
- Execution flow:
	1. User installs SpringEngine.
	2. User downloads a SpringEngine-compatible game package.
	3. User runs the game through SpringEngine.

## Unity-Like Direction (Lua-Based)

SpringEngine aims for a Unity-style authoring model, with Lua as the primary scripting language instead of C#.

- **Scenes**: authored as data files that define objects, transforms, and attached components.
- **Entities + Components**: runtime objects are composed from reusable components.
- **Script Components**: behavior is defined in Lua scripts attached to entities.
- **Lifecycle Hooks**: scripts use hooks similar to `Awake`, `Start`, `Update`, `OnDestroy`.
- **Prefabs**: reusable object templates serialized as data.
- **Engine Systems**: rendering, physics, input, audio, and resource loading remain native (C).

### Lua Scripting Role

- Lua is the default gameplay language.
- Runtime embeds Lua and exposes a curated API surface (transform, input, audio, scene queries, logging, spawning/despawning, etc.).
- Game packages primarily ship Lua scripts and content files.
- Native modules are optional for performance-critical or platform-specific features.

### API Stability Strategy

- Version the runtime API exposed to Lua.
- Include required engine API version in each game package manifest.
- Reject package load when versions are incompatible, with clear diagnostics.

### Suggested Milestones

1. Embed Lua runtime and execute a boot script from a game package.
2. Implement scene loader (JSON/TOML/YAML) and entity/component instantiation.
3. Add script component lifecycle (`Awake`, `Start`, `Update`, `OnDestroy`).
4. Expose core engine API bindings to Lua (input, transform, drawing, audio, time).
5. Add prefab support and package manifest version checks.
6. Add optional native extension interface for advanced modules.

## Long-Term Direction

- Keep engine/runtime and game code as separate deliverables.
- Version and validate API compatibility at launch.
- Provide an SDK surface for game developers to build SpringEngine-compatible games.
- Prioritize Lua-first gameplay authoring and tooling.

## Config-First Roadmap

- See `docs/config-system-vision.md` for the full `.conf`-driven architecture (project config, scenes, actor data, prefabs, and persistence model).

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
