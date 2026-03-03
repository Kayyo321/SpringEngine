# Example Project Deep Walkthrough

This page explains what each part of `example-project/` is doing and how the pieces connect at runtime.

## Directory Map

- `springengine.conf` — project boot config
- `autoload.dat.conf` — persistent/global actors loaded every scene load pass
- `input.conf` — named input schemas
- `*.scene.conf` — scene manifests
- `*.dat.conf` — actor lists used by each scene
- `prefabs/*.prefab.conf` — reusable actor templates
- `lighting/*.lighting.conf` + `global.lighting.conf` — scene lighting selection
- `ui/*.ui.conf` — UI document definitions
- `scripts/*.lua` — gameplay and UI behavior
- `assets/` — textures, sounds, animation definitions

## Boot + Global Runtime Files

### `springengine.conf`

- Boots into `starting_scene.scene.conf`.
- Sets `autoload_data = "autoload.dat.conf"`.
- Defines scene/prefab/ui roots in `[Paths]`.
- Declares persistent actor ids:
  - `global_audio`
  - `input_router`
  - `game_state`

### `autoload.dat.conf`

Defines three always-available runtime actors:

- `global_audio` — audio bus style data
- `input_router` — input map style data
- `game_state` — state storage values

They are loaded from `Persistence.autoload_actor_ids` each time a scene is loaded.

### `input.conf`

Two input schemas:

- `Normal` for gameplay (`Move*`, `Run`, `Jump`, `CloseGame`)
- `Menu` for menu navigation (`MoveUp/Down`, `Confirm`, `Back`)

## Scene Manifests

### `starting_scene.scene.conf`

- Uses `starting_scene.dat.conf`.
- Selects lighting schema `OutdoorNight` from `starting_scene.lighting.conf`.
- Loads HUD UI document: `hud.ui.conf`.
- Spawns actors in explicit order:
  - background
  - camera
  - lights
  - player + systems/spawners + trigger + fader

### `dark_scene.scene.conf` / `game_over.scene.conf`

Both use `game_over.dat.conf`, but different lighting schemas:

- `DarkVoid` for `dark_scene`
- `DarkMenu` for `game_over`

Both load `game_over.ui.conf`.

## Actor Data Files

### `starting_scene.dat.conf`

Contains the main game actors:

- `starting_background` (`StaticSprite`)
- `main_camera` (`Camera` + `Script` = `camera_follow.lua`)
- `sun_light` (`DirectionalLight`)
- `mage_spawner` (`Script` = `mage_spawner.lua`)
- `player_spawn` (prefab `Player` + overrides)
- `player_point_light` (child of player via `parent = "player_spawn"`)
- `crate_spawner` (`Script` = `crate_create.lua`)
- `scene_fader` (`StaticColor` fullscreen fade)
- `fall_reset_trigger` (`Collider` trigger + script)

### `game_over.dat.conf`

Simple menu scene payload:

- `game_over_background` (`StaticColor` fullscreen)
- `game_over_controller` (`Script` = `game_over.lua`)
- `game_over_fader` (`StaticColor` fade overlay)

## Prefabs

### `player.prefab.conf`

Base player setup:

- transform + centered anchor
- health/motor data
- collider + rigidbody
- script (`scripts/player.lua`)
- animated sprite (`assets/anims/player.anim.conf`)

`starting_scene.dat.conf` overrides script stack, collider/rigidbody, health, and animation block.

### `mage.prefab.conf`

Enemy actor template:

- collider + rigidbody
- script (`scripts/mage.lua`)
- animated sprite (`assets/anims/mage.anim.conf`)

### `crate.prefab.conf`

Static prop template:

- collider + rigidbody
- script (`scripts/crate.lua`)
- static sprite (`assets/Sprites/crate.jpeg`)

## Lighting

### `global.lighting.conf`

Maps scenes to files + default schemas:

- `starting_scene` → `starting_scene.lighting.conf` (`OutdoorDay` default)
- `game_over` → `dark_scene.lighting.conf` (`DarkMenu` default)
- `dark_scene` → `dark_scene.lighting.conf` (`DarkVoid` default)

### `lighting/starting_scene.lighting.conf`

Defines `OutdoorDay` and `OutdoorNight` schemas.

### `lighting/dark_scene.lighting.conf`

Defines `DarkVoid` and `DarkMenu` schemas.

## UI Documents

### `hud.ui.conf`

Gameplay HUD with labels for:

- health value/bar
- time survived
- mages cleared/active

Updated at runtime by `scripts/ui_hud.lua`.

### `game_over.ui.conf`

Single message label centered on screen.

### `pause_menu.ui.conf`, `toast.ui.conf`, `ui_lab.ui.conf`

Extra UI examples for:

- button callbacks
- layering/front-back control
- dynamic doc interactions

## Scripts (what each one does)

- `player.lua`
  - movement, jumping, damage/death flow
  - animation params and flip handling
  - fade-to-black transition to `game_over.scene.conf`
  - updates global HUD stats table
- `camera_follow.lua`
  - smooth camera follow to `player_spawn`
- `mage_spawner.lua`
  - periodic `Scene.instantiate_prefab("Mage")`
  - maintains spawned/cleared counters
- `mage.lua`
  - seek-and-hit behavior
  - calls player script method `take_damage`
  - despawns after pass and increments cleared count on destroy
- `crate_create.lua`
  - spawns a row/floor of crates around player start
- `crate.lua`
  - basic lifecycle logging template
- `fall_reset_trigger.lua`
  - collider trigger damage + reset using player script methods
- `music_start.lua`
  - loads/plays looping music via `Engine.DJ`
  - stores music channel in global state
- `ui_hud.lua`
  - reads global stat table and writes UI labels
  - includes UI button callback (`on_close_menu_click`)
- `game_over.lua`
  - background pulse + fade transitions
  - Enter key restart to `starting_scene.scene.conf`

## Runtime Flow in This Example

1. Boot `starting_scene` + autoload actors.
2. Spawn player/camera/spawners/trigger/lights/background + HUD.
3. `mage_spawner` periodically creates mage prefab instances.
4. `player.lua` and `mage.lua` interact through script calls and shared actor tags.
5. On player death, `player.lua` fades scene and loads `game_over.scene.conf`.
6. `game_over.lua` waits for Enter and loads back into `starting_scene`.
