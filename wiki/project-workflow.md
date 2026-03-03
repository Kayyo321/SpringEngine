# Creating Projects, Scenes, and Scripts

## Create a New Project

```bash
bin/springengine --make-proj <path_to_put_it> <project_name>
```

This scaffolds:

- `springengine.conf`
- `input.conf`
- `autoload.dat.conf`
- `starting_scene.scene.conf`
- `starting_scene.dat.conf`
- `global.lighting.conf`
- `lighting/default.lighting.conf`
- directories: `scripts/`, `prefabs/`, `assets/`, `assets/Sprites/`, `assets/Sounds/`, `ui/`, `lighting/`

## Create a Script

```bash
bin/springengine --make-script <project_root> <script_name>
```

Example:

```bash
bin/springengine --make-script ./my-game player_controller
```

This writes `scripts/player_controller.lua` (adds `.lua` automatically if omitted).

## Create a Scene

```bash
bin/springengine --make-scene <project_root> <scene_name>
```

This generates both scene files:

- `<scene_name>.scene.conf`
- `<scene_name>.dat.conf`

Then wire it into gameplay by either:

- setting `[Boot].first_scene` to that scene in `springengine.conf`, or
- calling `Engine.Scene.load("<scene_name>.scene.conf")` from Lua.

## Create a UI Document

```bash
bin/springengine --make-ui-doc <project_root> <doc_name>
```

This creates `ui/<doc_name>.ui.conf`.

Load it either:

- statically through `[Scene.UI].documents = ["..."]` in a scene config, or
- dynamically via `Engine.UI.push_document("<doc_name>.ui.conf")`.

## Typical Iteration Loop

1. Add/modify actors in `*.dat.conf`.
2. Order spawn list in `Scene.Load.actors`.
3. Add/modify scripts in `scripts/*.lua`.
4. Attach scripts with `Actors.Components.Script.module` or `.modules`.
5. Run with `bin/springengine --run <project>`.
