# SpringEngine UI System Vision

SpringEngine should use a config-first UI pipeline that mirrors scene loading: declarative files, strict validation, deterministic instantiation, and optional Lua behavior.

## Goals

- Make UI authoring data-driven, not hardcoded in C.
- Keep runtime behavior deterministic and testable.
- Allow game creators to customize look, layout, and behavior without rebuilding SpringEngine.
- Support both scene-local UI (HUD, prompts) and persistent UI (global overlays, notifications).
- Match existing scene/autoload mental model so users learn one system.

## File Roles

Use TOML `.conf` files, same as scenes and actor data.

- `ui/*.ui.theme.conf`: global design tokens and named styles.
- `ui/*.ui.prefab.conf`: reusable widget subtrees.
- `ui/*.ui.conf`: concrete UI documents/screens (HUD, pause menu, settings).
- `*.scene.conf`: references UI docs to load with the scene.
- `springengine.conf`: UI path roots and optional default UI behavior.

### `springengine.conf` additions

```toml
[Paths]
ui_dir = "./ui"

[UI]
default_theme = "default.ui.theme.conf"
reference_resolution = [1920, 1080]
allow_theme_hot_reload = true
```

### `*.scene.conf` additions

```toml
[Scene.UI]
documents = [
  "hud.ui.conf",
  "quest_log.ui.conf",
]

[Scene.UI.Persistence]
keep_persistent_documents = true
```

## UI Document Schema (`*.ui.conf`)

Each document contains metadata and a widget tree.

```toml
[UI]
schema = 1
id = "hud"
title = "Gameplay HUD"
theme = "default.ui.theme.conf" # optional override
input_layer = 100                # higher captures first
lifetime = "scene"              # scene | game

[[Widgets]]
id = "root_canvas"
type = "Canvas"
parent = ""
enabled = true
style = "RootCanvas"

[Widgets.Layout]
anchor_min = [0.0, 0.0]
anchor_max = [1.0, 1.0]
offset_min = [0.0, 0.0]
offset_max = [0.0, 0.0]

[[Widgets]]
id = "health_bar"
type = "ProgressBar"
parent = "root_canvas"
style = "HUD.HealthBar"

[Widgets.Bind]
source = "Actor:player.Health.current"
min = 0
max = 100

[Widgets.Layout]
anchor_min = [0.02, 0.92]
anchor_max = [0.28, 0.97]
```

## Theme Schema (`*.ui.theme.conf`)

Themes define tokens and reusable styles. Documents reference style names; users swap themes without changing widget files.

```toml
[Theme]
schema = 1
id = "default"

[Theme.Tokens.Color]
primary = [90, 170, 255, 255]
surface = [22, 24, 30, 220]
text = [235, 235, 240, 255]

[Theme.Tokens.Spacing]
xs = 4
sm = 8
md = 12
lg = 20

[[Theme.Styles]]
name = "HUD.HealthBar"
widget = "ProgressBar"
background_color = "surface"
fill_color = "primary"
padding = "sm"
corner_radius = 6

[[Theme.Styles]]
name = "HUD.Label"
widget = "Label"
font_size = 18
text_color = "text"
```

## Widget Types for First Milestones

Start with a focused, composable set:

- `Canvas` (root or subtree container)
- `Panel` (background block)
- `Label` (text)
- `Image` (texture)
- `Button` (hover/click events)
- `ProgressBar` (value visualization)
- `StackLayout` (vertical/horizontal auto layout)

Later add advanced widgets (scroll, list, input field, rich text).

## Runtime Data Structures (C)

- `UiTheme`: parsed tokens + style maps.
- `UiStyleResolved`: concrete values after token substitution.
- `UiDocumentDescriptor`: parsed `.ui.conf` metadata + node records.
- `UiNode`: runtime widget instance with parent/children links.
- `UiDocumentInstance`: active document tree + state tables.
- `UiRegistry`:
  - scene documents
  - persistent documents
  - `document_id -> instance`
  - `node_id -> node`

## Load Pipeline (Mirror Scene Runtime)

1. Parse project config (`springengine.conf`), resolve `Paths.ui_dir`.
2. Parse default theme (`UI.default_theme`) once at boot.
3. On scene load:
   - Parse scene file.
   - Parse each `Scene.UI.documents` file.
   - Resolve document-level theme override or use default theme.
   - Validate widget ids, parent references, type/style compatibility.
   - Instantiate widget trees in parent-before-child order.
4. On scene switch:
   - Destroy `lifetime = "scene"` UI documents.
   - Keep `lifetime = "game"` UI documents.
5. Optional: hot reload UI/theme files in development mode.

## Frame Pipeline

Add a dedicated UI stage to your current `frame_update` flow:

1. Collect input snapshot.
2. Update script components.
3. Update UI document state and bindings.
4. Render world actors.
5. Render UI in document order + node z-order.
6. Dispatch UI events (click, hover, focus changed, submit).

World and UI should remain separate render domains. UI should default to screen-space coordinates using reference-resolution scaling.

## Event + Binding Model

### Events

Each node can emit events:

- `on_click`
- `on_hover_enter`, `on_hover_exit`
- `on_focus`, `on_blur`
- `on_value_changed`

Events can target:

- Lua function path (`scripts/ui/hud.lua:on_health_clicked`)
- Engine command (`Scene.load`, `DJ.play`, etc.)

### Data Binding

Bindings map runtime values to widget properties.

- One-way (runtime -> UI): health, ammo, timers.
- Two-way (UI <-> runtime) for toggles/sliders in settings menus.

Recommended first release: one-way only, plus button callbacks.

## Lua API Surface

Add `Engine.UI` alongside existing modules.

Suggested initial methods:

- `find(document_id, node_id)`
- `set_visible(handle, visible)`
- `set_text(handle, text)`
- `set_value(handle, value)`
- `set_style_class(handle, style_name)`
- `push_document(path)`
- `pop_document(document_id)`
- `current_documents()`

This keeps game logic in Lua while keeping rendering/layout in native code.

## Validation Rules

Hard-fail load when:

- Duplicate widget ids in a document.
- Missing parent id (except root nodes).
- Unknown widget type.
- Style references missing from active theme.
- Cycles in parent graph.
- Invalid anchor ranges (`anchor_min > anchor_max`).

Warn in development (but allow load) for optional style fields or unknown custom metadata.

## Integration Plan (Phased)

### Phase 1: Core UI Runtime

- Add `src/ui/` module (`ui_runtime.c/.h`, `ui_theme.c/.h`, `ui_document.c/.h`).
- Implement parsing + validation of theme and document files.
- Implement `Canvas`, `Panel`, `Label`, `Image` rendering only.
- Add scene config parsing for `Scene.UI.documents`.

### Phase 2: Interactivity

- Add hit testing and focus routing.
- Add `Button` + event dispatch.
- Add minimal `Engine.UI` Lua bindings (`find`, `set_text`, `set_visible`, callbacks).

### Phase 3: Data Binding + Layout

- Add one-way binding evaluator.
- Add `ProgressBar` and `StackLayout`.
- Add reference-resolution scaling and safe-area support.

### Phase 4: Authoring Quality

- Hot reload for `.ui.conf` and `.ui.theme.conf`.
- Better diagnostics (file path + node id + property name).
- Prefab support for repeated widget subtrees.

## Testing Strategy

Add focused tests parallel to current config/runtime tests:

- `src/testing/ui_theme_test.c`
- `src/testing/ui_document_test.c`
- `src/testing/ui_runtime_test.c`

Key assertions:

- Invalid configs fail deterministically.
- Widget tree instantiation order is stable.
- Scene switch correctly preserves only `lifetime = "game"` documents.
- Button hit testing and callback dispatch are deterministic.

## Why This Fits SpringEngine

- Uses your existing config-first TOML model.
- Mirrors scene/autoload lifetime semantics users already understand.
- Keeps native performance while preserving Lua flexibility.
- Gives users broad customization (theme + layout + bindings) without changing engine code.