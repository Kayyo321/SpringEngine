# Config System Reference

All `.conf` files are parsed as TOML.

## 1) `springengine.conf` (project root)

### Required runtime sections/keys

- `[Boot]`
  - `first_scene` (string)
  - `autoload_data` (string)
- `[Window]` (required by current loader)
  - `title` (string)
  - `width` (int > 0)
  - `height` (int > 0)
  - `target_fps` (int > 0)
  - `resizable` (bool)

### Optional sections used by runtime

- `[Paths]`
  - `scenes_dir`
  - `prefabs_dir`
  - `ui_dir`
- `[Persistence]`
  - `autoload_actor_ids` (string array)

### Informational / currently not consumed by runtime loader

- `[Engine]`
- `[Runtime]`
- `[Streaming]`
- `Paths.assets_dir`, `Paths.scripts_dir`
- `Window.clear_color` (actual clear color currently comes from lighting selection)

## 2) `input.conf`

### Required

- `[Schema]` table with one or more named schema tables (`[Schema.Normal]`, etc.)

### Optional

- `[Input].active_schema`

If `active_schema` is missing or invalid, first valid schema is used.

### Action formats

Each action can be:

- binding table
- array of binding tables
- shorthand string key name

Binding table:

```toml
MoveUp = { kind = "key_down", keys = ["w"] }
```

`kind` values:

- `key_down`
- `key_pressed`
- `chord`

## 3) `*.scene.conf`

### Required

- `[Scene]`
  - `id`
  - `data_file`
- `[Scene.Load].actors` (ordered array of actor ids)

### Supported optional blocks

- `[Scene.Lighting]`
  - `file`
  - `schema`
  - `blend_in_seconds`
- `[Scene.UI].documents` (array of UI doc file names)

### Currently informational in runtime loader

- `[Scene.Environment]`
- `[Scene.Persistence]`
- `[Scene.Transition]`

## 4) `*.dat.conf` (scene actor data)

### Required

- `[[Actors]]` array and matching ids for `Scene.Load.actors`

### Supported actor fields

- `id`
- `enabled` (default `true`)
- `layer` (default `0`)
- `parent` (optional; validated for missing/self/cycles)
- `tags` (string array)
- `Transform.position` (`[x,y,z]`)
- `Transform.rotation_euler` (`[x,y,z]`)
- `Transform.scale` (`[x,y,z]`)
- `Transform.anchor` (for sprite/color drawables; `"center"` or `[x,y]`)
- `Components.*`
- `Overrides.Components.*` (takes precedence)

### Important current behavior

- `lifetime` is present in sample files but **not currently read** by loader.
- Scene switches rebuild actor registry and re-instantiate autoload + scene actors.

## 5) Prefab references + prefabs

### In `*.dat.conf`

- `[PrefabRefs]` maps logical names to prefab file paths
- Actor can set `prefab = "SomePrefabKey"`

### Prefab file format (`*.prefab.conf`)

- `[Prefab]` table, with optional:
  - `[Prefab.Defaults]`
  - `[Prefab.Transform]`
  - `[Prefab.Components.*]`

Runtime looks up components via actor table, checking overrides first.

## 6) Built-in actor components (currently implemented)

- `Camera`
- `Collider`
- `Rigidbody`
- `PointLight`
- `SpotLight`
- `DirectionLight` (also accepts `DirectionalLight` in config)
- `StaticSprite`
- `AnimatedSprite`
- `StaticColor`
- `Script`

Sprite component material binding (new, optional):

- `Components.StaticSprite.material` (string material id)
- `Components.AnimatedSprite.material` (string material id)

Current fallback behavior:

- if material id is missing or unknown, sprite still renders through the legacy path and logs a warning once.

Unknown component tables are ignored unless they map to implemented handlers.

## 7) Animation files (`*.anim.conf`) for `AnimatedSprite`

Expected top-level tables:

- `[Animation]` (must include `default`)
- `[Sheets]`
- one or more state tables (`[Idle]`, `[Walk]`, ...)

Supported state fields include:

- `sheet`
- `fps`
- `loop`
- `frames = [[x,y,w,h], ...]` or `[[x,y,w,h,duration], ...]`
- `plug` transition tables with `condition`, optional `param`, optional `speed_threshold`

Flip support in `[Animation]`:

- `flip_x`, `flip_y`
- `flip_x_from_actor_movement`
- `flip_x_param`
- `flip_x_deadzone`
- `flip_x_when_param_negative`

## 8) Lighting configs

### `global.lighting.conf` (required by runtime)

Expected:

- `[LightingGlobal]`
- optional `[LightingGlobal.Paths].lighting_dir`
- optional `[LightingGlobal.Defaults]`
- optional `[[LightingGlobal.SceneMap]]`

### `*.lighting.conf`

Expected:

- `[Lighting]` with optional `default_schema`
- `[Schema]` table containing named schema tables

### Resolution order for scene lighting

1. `Scene.Lighting.file` / `Scene.Lighting.schema`
2. `LightingGlobal.SceneMap` by scene id
3. `LightingGlobal.Defaults`

Current runtime applies:

- global light multiplier from Ambient (`global_multiplier` or fallback `intensity`)
- clear color from Ambient (`color` or `sky_color`)

## 9) UI docs (`ui/*.ui.conf`)

### Required

- `[UI].id`
- `[[Widgets]]`

### Supported `[UI]` fields

- `id`
- `lifetime` (`scene` or `game`)
- `layer`

### Widget types currently supported

- `Canvas`
- `Panel`
- `Label`
- `Image`
- `Button`

### Widget fields

- `id`, `type`, `enabled`, `z`, `parent`
- `color`, `text`, `font_size`, `on_click`, `texture`
- `[Widgets.Layout]`:
  - `anchor_min`
  - `anchor_max`
  - optional `offset_min`
  - optional `offset_max`

### Validation

- duplicate widget ids fail
- unknown parent id fails
- parent cycles fail
- invalid anchors fail

## 10) Shader global config

### `global-shaders.conf` (optional)

If this file is missing, runtime now loads built-in shader defaults for backward compatibility.

Runtime also accepts `global.shaders.conf` as an alternate file name.

Expected top-level table:

- `[ShaderGlobal]`

Supported optional fields:

- `allow_compile_fallback` (bool)
- `active_profile` (string)
- `[ShaderGlobal.Paths]`
  - `shader_dir`
  - `material_dir`
- `[ShaderGlobal.Defaults]`
  - `sprite_material`
  - `ui_material`
  - `post_stack`
- `[[ShaderGlobal.Profiles]]`
  - `id`
  - `max_variants_per_shader`
  - `allow_expensive_post`

Default behavior when no file exists:

- `active_profile = "default"`
- `allow_compile_fallback = true`
- `shader_dir = "./shaders"`
- `material_dir = "./materials"`
- `sprite_material = "default_sprite.mat.conf"`
- `ui_material = "default_ui.mat.conf"`
- `post_stack = "default.postfx.conf"`
- active profile defaults to `max_variants_per_shader = 64`, `allow_expensive_post = true`

## 11) Engine version requirement

### `version.conf` (optional)

When present, runtime validates the game's required SpringEngine version before startup.

Supported requirement operators:

- `>` (current runtime version must be strictly greater)
- `=` (current runtime version must match exactly)

Supported config shapes:

```toml
[Version]
springengine = "> 0.1.0"
```

or

```toml
[Version]
required = "= 0.1.1"
```

or root-level keys:

```toml
springengine = "= 0.1.1"
```

If the requirement does not pass, runtime exits with an error before opening the game window.

CLI helper:

- `bin/springengine --validate-ver <project_or_archive_path>` validates only `version.conf` compatibility.

## 12) Shader descriptor files (`shaders/*.shader.conf`)

Shader descriptors are validated at startup and by `--validate-config`.

Required table and keys:

- `[Shader]`
  - `id` (string)
  - `vertex` (string)
  - `fragment` (string)

Optional keys/tables:

- `schema` (int > 0)
- `[Shader.States]`
  - `blend` (`alpha`, `additive`, `none`)
  - `depth_test` (bool)
  - `depth_write` (bool)
  - `cull` (`none`, `back`, `front`)
- `[[Shader.Keywords]]`
  - `name` (string)
- `[[Shader.Uniforms]]`
  - `name` (string)
  - `type` (`float`, `vec2`, `vec3`, `vec4`, `color`, `texture2D`, `int`, `bool`)
  - optional `source` (string)

Validation rules currently enforced:

- shader ids must be unique across all descriptor files
- keyword names must be unique per shader
- uniform names must be unique per shader
- unsupported blend/cull/uniform type values fail validation

If the shader directory is missing, runtime continues with an empty shader registry for backward compatibility.

## 13) Material descriptor files (`materials/*.mat.conf`)

Material descriptors are validated at startup and by `--validate-config`.

Required table and keys:

- `[Material]`
  - `id` (string)
  - `shader` (string, must match an existing shader id)

Optional keys/tables:

- `schema` (int > 0)
- `[Material.Textures]`
  - arbitrary slot keys mapped to texture paths

Validation rules currently enforced:

- material ids must be unique across all descriptor files
- `Material.shader` must reference a loaded shader descriptor id
- texture slot values must be non-empty strings

If the material directory is missing, runtime continues with an empty material registry for backward compatibility.

## 14) Full-screen post shaders (`*.postfx.conf`)

Runtime supports project-level full-screen shader passes loaded from:

- `ShaderGlobal.Defaults.post_stack` (resolved under `ShaderGlobal.Paths.material_dir`)

Current format:

```toml
[PostFX]
enabled = true

[[PostFX.Passes]]
shader = "post_edge_pixel_vignette"
material_alias = "vignette"
pixel_size = 5.0
vignette_inner = 0.58
vignette_outer = 1.0
edge_glow = 0.42
pulse_speed = 1.45
intensity = 1.0
```

Rules enforced:

- `PostFX` table is required when file exists
- at least one `[[PostFX.Passes]]` entry is required when enabled
- each pass must provide `shader` id referencing a loaded shader descriptor
- optional pass aliases: `material_alias` (preferred), `alias`, or `material`
- optional `intensity` scales pass impact (`0.0` disables visual effect, `1.0` is full strength)

Current runtime behavior:

- world rendering is drawn to an offscreen target, full-screen passes are applied in order, then UI is drawn on top
- if post stack file is missing, full-screen shaders are disabled and runtime continues normally
