# Scripting Guide (Lua)

## Script Component Contract

A script file must return a table. Optional lifecycle methods:

- `awake(self)`
- `start(self)`
- `update(self)`
- `on_destroy(self)`

SpringEngine calls `awake` and `start` during component initialization, then `update` every frame.

## Attach Scripts in Actor Config

Inside `[[Actors]]`:

```toml
[Actors.Components.Script]
module = "scripts/player.lua"
```

Or multiple scripts:

```toml
[Actors.Components.Script]
modules = [
  ["scripts/player.lua", "player-controller"],
  "scripts/ui_hud.lua"
]
```

Notes:

- `module` accepts string or `[path, alias]`.
- `modules` accepts an array of those entries.
- Duplicate module paths are de-duplicated.
- `Overrides.Components.Script` can replace prefab script config.

## Built-in Helpers Injected Into Script Tables

Each script table gets:

- `get_component(component_name, actor_id?)`
- `destroy(actor_id?)`
- `get_material_by_name(material_alias)`

`actor_id` is optional and defaults to the owning actor.

`get_material_by_name` returns a small material handle table with:

- `set(property_name, value)`

Example:

```lua
local vignette = self:get_material_by_name("vignette")
if vignette then
  vignette:set("intensity", player.health)
end
```

For `intensity`, runtime accepts either direct `[0..1]` values or health-like values where `100 -> 0` and `<=15 -> 1`.

## UI Callback Format

UI button callbacks must use:

```text
path/to/script.lua:function_name
```

Example:

```toml
on_click = "scripts/ui_hud.lua:on_close_menu_click"
```

The callback receives `(self, document_id, node_id)` style parameters through runtime invocation.

## Engine Lua Modules

SpringEngine preloads these modules:

- `Engine`
- `Engine.Input`
- `Engine.Time`
- `Engine.Transform`
- `Engine.Actor`
- `Engine.Camera`
- `Engine.Collider`
- `Engine.Rigidbody`
- `Engine.Scene`
- `Engine.DJ`
- `Engine.UI`
- `Engine.Disk`

## Input API (from `Engine.Input`)

Configured by `input.conf` and runtime schema selection.

- `Engine.Input.current_schema()`
- `Engine.Input.set_schema(name)`
- `Engine.Input.accepted(action_name)`
- `Engine.Input.is_key_down(key_name)`
- `Engine.Input.was_key_pressed(key_name)`

## UI API (from `Engine.UI`)

- `find(document_id, node_id)` → handle string (`"doc::node"`)
- `set_visible(handle, visible)`
- `set_text(handle, text)`
- `push_document(path)`
- `pop_document(document_id)`
- `set_document_layer(document_id, layer)`
- `bring_to_front(document_id)`
- `send_to_back(document_id)`
- `current_documents()`

## Disk API (from `Engine.Disk`)

- `resolve(path)`
- `exists(path)`
- `read_text(path)`
- `write_text(path, text)`
- `append_text(path, text)`
- `save(path, text)`
- `ensure_directory(path)`

## Common Pattern

```lua
local Engine = require("Engine")
local Input = require("Engine.Input")

local script = {}

function script:update()
  if Input.accepted("Jump") then
    Engine.log("Jump pressed")
  end
end

return script
```
