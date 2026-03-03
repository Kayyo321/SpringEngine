# Getting Started

## Build SpringEngine

From repo root:

```bash
./rebuild_libs.sh
make
```

Useful targets:

- `make` — release build
- `make debug` — debug build (ASan)
- `make test` — test suite
- `make clean` — clean objects
- `make fclean` — clean objects + binaries

## Run a Project

```bash
bin/springengine --run <project_folder_or_archive.targame>
```

Examples:

```bash
bin/springengine --run example-project
bin/springengine --run game.targame
```

## Pack and Unpack

```bash
bin/springengine --pack <source_directory> <archive.targame>
bin/springengine --unpack <archive.targame> <destination_directory>
```

## Runtime Expectations

At runtime, SpringEngine expects:

- `springengine.conf` at project root
- a valid `[Boot]` section with:
  - `first_scene`
  - `autoload_data`
- parseable `.conf` files (TOML)

Startup flow (current implementation):

1. Mount project folder or `.targame`.
2. Parse `springengine.conf`.
3. Load window config from `[Window]`.
4. Load input config from `input.conf`.
5. Load global lighting from `global.lighting.conf`.
6. Load autoload actor IDs from `Persistence.autoload_actor_ids`.
7. Load first scene and corresponding scene data.
8. Start frame loop.
