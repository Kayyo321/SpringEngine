# SpringEngine

<table>
  <tr>
    <td valign="top" width="45%">
      <img src="imgs/springengine-ex.gif" alt="SpringEngine example" />
    </td>
    <td valign="top" style="font-size: 150%;">
      <p>SpringEngine is a runtime for games, not a single game executable.</p>
      <p>The goal is the same spirit as Java’s “write once, run anywhere” ideology:</p>
      <ul>
        <li>Game creators build against one stable runtime contract.</li>
        <li>Players install SpringEngine once.</li>
        <li>The same packaged game should run anywhere SpringEngine runs.</li>
      </ul>
    </td>
  </tr>
</table>

## Runtime 

- Runtime binary: `springengine`
- Game input: project folder or `.targame`
- Optional extension model: native module loading (platform dependent)

Execution flow:

1. Install SpringEngine.
2. Download a SpringEngine-compatible game package.
3. Run it with SpringEngine.

## Lua Scripting

SpringEngine follows a Unity-like composition approach with Lua as the default gameplay language.

- Scenes define entities/components.
- Scripts provide behavior hooks (`awake`, `start`, `update`, `on_destroy` style lifecycle).
- Prefabs enable reusable actor templates.
- Runtime systems (rendering, audio, input, physics-facing integration) remain native C.

Typical imports:

```lua
local Engine = require("Engine")
local Input = require("Engine.Input")
local Time = require("Engine.Time")
local Scene = require("Engine.Scene")
```

Current module surface includes:

- `Engine`
- `Engine.Input`
- `Engine.Time`
- `Engine.Transform`
- `Engine.Actor`
- `Engine.Camera`
- `Engine.Scene`
- `Engine.DJ`
- `Engine.UI`

## Current Project Priorities

- Right now, this is just a proof of concept, there isn't really a 
  springengine project gui editor, so all your `.conf` files have to be edited
  manually :|. I've cobbled together a game this way in `example-project` if you 
  want to dig around to find out how the conf system works.
- Strengthen runtime/package compatibility boundaries.
- Keep engine/game deliverables cleanly separated.
- Expand Lua API while preserving stability.
- Improve package execution reliability for both folder and `.targame` flows.
- Continue config-first architecture work.

Related design docs:

- `docs/config-system-vision.md`
- `docs/ui-system-vision.md`
- `docs/lighting-system-vision.md`
- `docs/child-actor-system-vision.md`

## Wiki

Practical guides and references live in `wiki/`:

- `wiki/README.md`
- `wiki/getting-started.md`
- `wiki/project-workflow.md`
- `wiki/scripting-guide.md`
- `wiki/config-reference.md`
- `wiki/example-project.md`

## Build and Run

This projects uses the libraries in `/lib` which contains source code for all of the
libraries. You need to build those to static libraries first before building the 
springengine executable. There's a helper for this: `rebuild_libs.sh`

### Build Commands

- Build: `make`
- Debug build (ASan): `make debug`
- Tests: `make test`
- Clean objects: `make clean`
- Clean objects + binaries: `make fclean`

### Runtime Commands

- Version: `bin/springengine --version`
- Run project folder/archive: `bin/springengine --run <project_or_archive_path>`
- Pack folder to archive: `bin/springengine --pack <source_directory> <archive.targame>`
- Unpack archive: `bin/springengine --unpack <archive.targame> <destination_directory>`

Scaffolding helpers:

- `--make-proj <path_to_put_it> <project_name>`
- `--make-script <project_root> <script_name>`
- `--make-scene <project_root> <scene_name>`
- `--make-ui-doc <project_root> <doc_name>`

Thanks :)
