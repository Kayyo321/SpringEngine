# SpringEngine Lighting System Vision

SpringEngine should use a config-first lighting pipeline that mirrors existing project config patterns: explicit files, strict validation, deterministic scene application, and clear schema versioning.

This document proposes a per-scene lighting architecture where:

- Lighting definitions live in dedicated `*.lighting.conf` files.
- A root `global.lighting.conf` ties all lighting files together.
- Each scene resolves and applies a concrete `lighting.Schema` the same way input uses named schemas.

## Goals

- Make lighting setup data-driven and editable without rebuilding the engine.
- Keep scene lighting deterministic across platforms and frame rates.
- Support both global baseline lighting and scene-specific overrides.
- Keep authoring mental model aligned with `input.conf` schema selection.
- Allow future growth (post-processing, probes, weather modulation) without breaking v1 files.

## File Roles

Use TOML `.conf` files, consistent with the rest of SpringEngine.

- `global.lighting.conf`: global lighting registry and defaults, loaded once at boot.
- `*.lighting.conf`: scene-owned lighting libraries containing one or more named schemas.
- `*.scene.conf`: points to the lighting file and selects which schema to activate.

### Why split this way?

- `global.lighting.conf` is the authoritative map for all lighting assets and defaults.
- `*.lighting.conf` keeps scene lighting isolated and easy to reason about.
- Scene manifests stay lightweight: they only reference *what to use*, not full lighting data.

## Config Contract

## `global.lighting.conf` (project-level lighting registry)

```toml
[LightingGlobal]
schema = 1
active_profile = "default"
allow_missing_scene_lighting = false

[LightingGlobal.Paths]
lighting_dir = "./lighting"

# Global fallback when a scene does not explicitly define one.
[LightingGlobal.Defaults]
file = "default.lighting.conf"
schema = "GameplayDay"

# Profiles let you swap a whole lighting setup set (default, low, cinematic, accessibility).
[[LightingGlobal.Profiles]]
id = "default"
description = "Authoring defaults"

[[LightingGlobal.Profiles]]
id = "low"
description = "Reduced light count + cheaper shadows"

# Optional hard map by scene id to avoid implicit naming assumptions.
[[LightingGlobal.SceneMap]]
scene_id = "starting_scene"
file = "starting_scene.lighting.conf"
default_schema = "OutdoorDay"

[[LightingGlobal.SceneMap]]
scene_id = "game_over"
file = "game_over.lighting.conf"
default_schema = "MenuNeutral"
```

### Behavior

- `LightingGlobal.schema` versions the file format and must match runtime support.
- `LightingGlobal.Paths.lighting_dir` is the root for all `*.lighting.conf` lookups.
- `LightingGlobal.Defaults` is the fallback pair (`file`, `schema`) when scene-level data is absent.
- `LightingGlobal.SceneMap` is optional but recommended for strict projects.
- `active_profile` allows selecting quality/content variants without changing scene files.

## `*.scene.conf` additions (scene picks a lighting schema)

```toml
[Scene]
schema = 1
id = "starting_scene"
title = "Starting Scene"
data_file = "starting_scene.dat.conf"

[Scene.Lighting]
file = "starting_scene.lighting.conf"
schema = "OutdoorDay"
blend_in_seconds = 0.35
```

### Scene-level policy

- `Scene.Lighting.file` is optional when `global.lighting.conf` has a matching `SceneMap` entry.
- `Scene.Lighting.schema` is optional when the target lighting file defines a default schema.
- If both scene and global mappings exist, scene values win.
- `blend_in_seconds` controls cross-fade to avoid visible pop on scene loads.

## `*.lighting.conf` (scene lighting library)

Each lighting file can contain multiple named schemas. This is intentionally parallel to input:
`[Schema.Normal]`, `[Schema.Menu]` in input becomes `[Schema.OutdoorDay]`, `[Schema.OutdoorNight]` in lighting.

```toml
[Lighting]
schema = 1
id = "starting_scene"
default_schema = "OutdoorDay"
color_space = "srgb"            # srgb | linear
units = "meters"

[Schema.OutdoorDay.Ambient]
mode = "hemisphere"             # flat | hemisphere | sh
sky_color = [180, 205, 255]
ground_color = [78, 90, 120]
intensity = 0.65

[Schema.OutdoorDay.Fog]
enabled = true
color = [170, 190, 215]
start = 20.0
end = 220.0
density = 0.0                    # used by exponential fog modes later

[[Schema.OutdoorDay.Lights]]
id = "sun_key"
type = "directional"            # directional | point | spot
enabled = true
color = [255, 246, 224]
intensity = 1.4
cast_shadows = true
shadow_bias = 0.0012
shadow_map_size = 2048

direction = [-0.35, -0.90, -0.10]

[[Schema.OutdoorDay.Lights]]
id = "campfire_fill"
type = "point"
color = [255, 158, 90]
intensity = 1.1
range = 9.0
position = [12.0, 1.2, -4.0]
falloff = "inverse_square"      # inverse_square | linear | custom

[Schema.OutdoorDay.Post]
exposure = 1.0
gamma = 2.2
bloom_threshold = 1.15
bloom_intensity = 0.12

# Alternate schema in same file
[Schema.OutdoorNight.Ambient]
mode = "flat"
color = [22, 28, 45]
intensity = 0.25
```

## Load Pipeline (Deterministic)

1. Parse `springengine.conf` and resolve project root.
2. Parse `global.lighting.conf` once at boot and validate registry.
3. On scene load, parse scene manifest and resolve `(lighting file, schema)` in this order:
   - `Scene.Lighting.file` + `Scene.Lighting.schema`.
   - `LightingGlobal.SceneMap[scene_id]` entries.
   - `LightingGlobal.Defaults`.
4. Parse target `*.lighting.conf` and validate required sections.
5. Resolve final schema (`Scene.Lighting.schema` > scene-map default > file default).
6. Build immutable `LightingSchemaResolved` runtime object.
7. Apply lighting state in render system before world draw.
8. If scene specifies blend time, cross-fade old→new state over `blend_in_seconds`.

## Runtime Data Structures (C)

- `LightingGlobalConfig`
  - schema version
  - active profile
  - defaults
  - scene map table
- `LightingFileDescriptor`
  - file metadata (`id`, format settings)
  - named schema descriptors
- `LightingSchemaDescriptor`
  - ambient/fog/post blocks
  - ordered light descriptors
- `LightingSchemaResolved`
  - GPU-ready, validated values (colors normalized, matrices prepared)
- `LightingRuntimeState`
  - current schema
  - previous schema (for blending)
  - blend clock / duration

## Validation Rules

Hard-fail scene load when:

- Missing target lighting file after resolution.
- Resolved schema name does not exist in file.
- Duplicate `Lights.id` inside one schema.
- Unsupported light type or invalid required fields.
- Negative intensity/range where not allowed.
- Direction vector has zero length for directional/spot lights.

Warn in development (but load) when:

- Light count exceeds recommended budget for active profile.
- `shadow_map_size` is non-power-of-two.
- Optional post-processing keys are unknown (forward compatibility mode).

## Authoring Conventions

- Keep one `*.lighting.conf` per scene family (e.g., all dungeon variants can share one file).
- Use schema names by purpose/time (`OutdoorDay`, `OutdoorNight`, `BossIntro`, `Paused`).
- Keep global defaults simple and safe; scene files carry richer artistic intent.
- Prefer relative paths from `LightingGlobal.Paths.lighting_dir`.

## Scene Transition Semantics

- Lighting is scene-scoped by default.
- When loading a new scene, previous lighting remains active until next schema is fully validated.
- If validation fails, scene load should abort (recommended default), preserving old scene and lighting.
- If blending is enabled, interpolation runs in unscaled time so pause/time-scale does not freeze transition.

## Lua API Surface (Planned)

Add `Engine.Lighting` as a thin control layer over resolved scene state.

Suggested initial methods:

- `current_schema()`
- `set_schema(name, blend_seconds?)`
- `set_light_enabled(light_id, enabled)`
- `set_light_intensity(light_id, intensity)`
- `set_exposure(value)`
- `reload_current()` (development only)

Policy: Lua may tweak runtime values, but authoritative schema definitions remain in `.lighting.conf` files.

## Performance + Quality Profiles

`global.lighting.conf` profiles should select constraints, not rewrite scene art data.

Example profile policy:

- `default`: full configured light count and shadows.
- `low`: clamp active dynamic lights and disable expensive post effects.
- `cinematic`: higher shadow maps and bloom allowance.

Recommended profile knobs:

- `max_dynamic_lights`
- `max_shadowed_lights`
- `max_shadow_map_size`
- `enable_bloom`
- `enable_volumetrics` (future)

## Error Diagnostics Standard

All lighting parse/validation errors should include:

- file path
- schema name
- section/key
- reason and expected value shape

Example:

`lighting parse error: file='lighting/starting_scene.lighting.conf', schema='OutdoorDay', key='Lights[1].range' -> must be > 0`

## Integration Plan (Phased)

### Phase 1: File Contracts + Parsing

- Add `src/config/lighting_global_config.c/.h`.
- Add `src/config/lighting_file_config.c/.h`.
- Parse and validate `global.lighting.conf` and `*.lighting.conf`.
- Extend scene parsing with `[Scene.Lighting]`.

### Phase 2: Runtime Binding

- Add `src/windowman/lighting_runtime.c/.h` (or existing render module equivalent).
- Convert parsed schemas into render-ready state.
- Apply resolved lighting each frame before world draw.

### Phase 3: Blending + Lua Hooks

- Implement schema blending over `blend_in_seconds`.
- Add `Engine.Lighting` Lua bindings for runtime control.
- Add development-only live reload for active lighting files.

### Phase 4: Profile Policy + Tooling

- Connect `active_profile` constraints to runtime culling/quality toggles.
- Emit richer diagnostics and budget warnings.
- Add maker scaffolding for new projects:
  - `global.lighting.conf`
  - `lighting/default.lighting.conf`
  - per-scene lighting stub files

## Non-Goals for First Milestone

- Real-time global illumination.
- Probe baking pipeline.
- Volumetric lighting.
- Full HDR/post stack parity with AAA engines.
- Automatic day-night simulation authored outside schemas.

## Summary

The lighting architecture should follow the same reliable pattern already used by SpringEngine config systems:

- global registry file (`global.lighting.conf`),
- per-scene lighting libraries (`*.lighting.conf`),
- explicit scene-selected `lighting.Schema` at load time,
- deterministic validation and runtime application.

This keeps authoring clear, scene transitions stable, and implementation incremental while leaving room for higher-end rendering features later.