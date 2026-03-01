# SpringEngine Config-First Vision

SpringEngine treats `.conf` files as TOML 1.0 data documents.

## File Roles

- `springengine.conf`: project-level runtime config (window, boot, persistence, runtime settings).
- `*.scene.conf`: scene manifest (what data file to load, ordered actor ids, environment and transition metadata).
- `*.dat.conf`: actor data file (actor definitions and prefab references).
- `prefabs/*.prefab.conf`: reusable actor templates.

## Required Load Pipeline

1. Parse `springengine.conf`.
2. Load `Boot.autoload_data` and instantiate actors listed in `Persistence.autoload_actor_ids`.
3. Parse `Boot.first_scene` (`*.scene.conf`).
4. Parse scene `data_file` (`*.dat.conf`).
5. Instantiate actors in `Scene.Load.actors` order.
6. On scene switch:
   - Destroy all actors where `lifetime = "scene"`.
   - Keep all actors where `lifetime = "game"`.
   - Optionally unload unused assets if enabled.

## Actor Definition Rules (`*.dat.conf`)

Actors are an array-of-tables (`[[Actors]]`), each with:

- `id` (required, unique across runtime while loaded)
- `enabled` (optional, default `true`)
- `lifetime` (optional, default `"scene"`; allowed `"scene" | "game"`)
- `tags` (optional string array)
- `prefab` (optional prefab key from `PrefabRefs`)
- `Transform` (recommended table)
- `Components.*` (component payload tables)
- `Overrides.*` (only used when `prefab` is set)

`Transform.anchor` is optional and supports either `[x, y]` (manual anchor offset in sprite pixels) or `"center"`.
Default anchor behavior is top-left (`[0, 0]`).

Actors are Unity-style GameObjects:

- An actor can have any number of components.
- Components can be `builtin`, `script`, or `custom` kinds.
- Component names map to runtime component constructors/handlers.
- Builtin `StaticSprite` renders a texture every frame without animation (keys: `texture`, optional `position`, `scale`, `rotation`, `tint`); when present, `Transform.anchor` controls sprite draw origin.
- Builtin `AnimatedSprite` renders a TOML-driven animation graph (keys: `anim`, optional `position`, `scale`, `rotation`, `tint`). Animation file format:
   - `[Animation]` with `default = "StateName"`
   - `[Sheets]` key/value map of `sheet_key = "texture/path.png"`
   - Per-state tables (`[Idle]`, `[Walk]`, etc.) with `sheet`, optional `fps`, optional `loop`, and `frames = [[x, y, w, h], ...]` (or `[[x, y, w, h, duration], ...]`)
   - Optional transition graph under `[State.plug]`, e.g. `[Idle.plug.Walk] condition = "moving"` or `[Walk.plug.Idle] condition = "not_moving"`; optional `speed_threshold` (default `0.01`).

### Identity and Validation

- Duplicate `id` in the same load pass is an error.
- If `prefab` is set but not found in `PrefabRefs`, fail scene load.
- Unknown component names are warnings in development and errors in release (recommended policy).

## Prefab Rules

`PrefabRefs` maps logical names to prefab files.

Merge order when spawning an actor from prefab:

1. Prefab defaults (`Prefab.Defaults`, `Prefab.Transform`, `Prefab.Components.*`)
2. Actor base values (`Actors.*` and `Actors.Components.*`)
3. Actor overrides (`Actors.Overrides.*`)

Last writer wins at field level.

Example: prefab provides `Health.max = 100`; actor override sets `Health.current = 80` and keeps `max = 100`.

## Component Initialization Lifecycle

When actors are loaded, component initialization is mandatory and ordered:

1. Actor record is created.
2. Components are attached in source order.
3. `initialize` is called for each component in that same order.
4. If any component initialization fails, actor load fails and scene load should abort (recommended default behavior).

This guarantees that both script components and builtin components are activated as soon as actors are instantiated.

## Persistent Objects (Not Unloaded Between Scenes)

Use both patterns together:

- **Global autoload set**: declared once in `springengine.conf` and sourced from `autoload.dat.conf`.
- **Per-actor lifetime**: any actor with `lifetime = "game"` survives scene transitions.

This allows always-on systems (audio, input routing, game state) and scene-local gameplay actors to coexist.

## Recommended Runtime Data Structures (C)

- `SceneDescriptor`: parsed data from `*.scene.conf`.
- `ActorBlueprint`: parsed actor record from `*.dat.conf` after prefab merge.
- `ActorInstance`: runtime object id + component handles.
- `WorldRegistry`:
  - `scene_actors`
  - `persistent_actors`
  - `actor_id -> instance` hash table

## Best `.conf` Interpreter Library for This Project

Recommended: **tomlc99** (TOML parser for C).

Why this is the best fit for SpringEngine right now:

- Native C library, matches your current C11 codebase.
- Small footprint, easy to vendor directly under `lib/`.
- Works with your existing Makefile static-link style.
- Strong fit for your section/table-heavy config style.
- TOML gives typed values (arrays, ints, bools, strings) without custom parsing.

Repository: `https://github.com/cktan/tomlc99`

## Integration Plan (Short)

1. Vendor tomlc99 source into `lib/tomlc99/`.
2. Add source files to build in `Makefile`.
3. Implement a `src/config/` module:
   - `config_project.c` for `springengine.conf`
   - `config_scene.c` for `*.scene.conf`
   - `config_data.c` for `*.dat.conf` and prefab merge
4. Validate all required keys and emit diagnostics via existing logger.
5. Wire `run_program()` to boot from parsed configs instead of `DefaultWindowConfig` only.

## Non-Goals for First Milestone

- No live schema migrations; hard-fail on unknown schema version.
- No cyclic prefab inheritance.
- No dynamic component type registration at load-time.