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
