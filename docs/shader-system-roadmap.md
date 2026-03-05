# SpringEngine Shader System Roadmap

SpringEngine should support author-driven, high-impact shaders that can reshape rendering behavior, not just tweak tint values.

This roadmap targets a Unity-style workflow where creators can:

- author shader assets and materials in config files,
- bind runtime values from Lua and engine systems,
- define multi-pass effects (world, lighting, post),
- choose render pipeline behavior per scene/profile.

## Product Goals

- Enable large visual changes: stylized lighting, outlines, distortion, CRT/glitch, toon ramps, heat haze, custom fog, and full-screen post stacks.
- Keep the existing config-first design language (`*.conf`, strict validation, deterministic load behavior).
- Keep runtime stable under shader failures (safe fallback shaders and clear diagnostics).
- Preserve cross-platform targets in your current build matrix.

## Constraints From Current Runtime

- Rendering currently happens in `frame_update` with direct draw calls (`StaticSprite`, `AnimatedSprite`, `StaticColor`) and no shader orchestration.
- Scene lighting currently resolves config and applies global multiplier/clear color, but no GPU lighting pipeline exists yet.
- Lua modules are exposed via `Engine.*` preload modules, which is the right extension point for runtime shader control.
- UI has its own runtime draw stage and should remain separable from world/post processing.

## End-State Architecture

## 1. Asset Model

- `global.shaders.conf`: project-wide shader registry, quality/profile knobs, fallback policy.
- `shaders/*.shader.conf`: shader definitions (vertex/fragment files, variant keywords, declared uniforms, render states, pass metadata).
- `materials/*.mat.conf`: material instances referencing shader + parameter values + textures.
- `*.scene.conf` additions: render pipeline profile, post stack document, optional scene override keyword sets.

## 2. Runtime Model

- `ShaderLibrary`: loaded shader descriptors and compiled GPU programs.
- `MaterialLibrary`: immutable material templates + runtime instances.
- `RenderGraph` (minimal scriptable graph): ordered passes, inputs/outputs, temporary render targets.
- `ShaderParamStore`: per-frame uniform buffers and material overrides.
- `FallbackPolicy`: robust substitution when shader compile/link/validation fails.

## 3. Scripting Model

- `Engine.Shader`: global shader controls (keywords, globals, profile reload).
- `Engine.Material`: per-actor/per-instance parameter writes.
- `Engine.PostFX`: enable/disable and configure post passes during runtime.

## File Contract Draft (Config-First)

## `global.shaders.conf`

```toml
[ShaderGlobal]
schema = 1
active_profile = "default"
allow_compile_fallback = true

[ShaderGlobal.Paths]
shader_dir = "./shaders"
material_dir = "./materials"

[ShaderGlobal.Defaults]
sprite_material = "default_sprite.mat.conf"
ui_material = "default_ui.mat.conf"
post_stack = "default.postfx.conf"

[[ShaderGlobal.Profiles]]
id = "default"
max_variants_per_shader = 64
allow_expensive_post = true

[[ShaderGlobal.Profiles]]
id = "low"
max_variants_per_shader = 16
allow_expensive_post = false
```

## `*.shader.conf`

```toml
[Shader]
schema = 1
id = "sprite_lit"
vertex = "sprite_lit.vert.glsl"
fragment = "sprite_lit.frag.glsl"

[Shader.States]
blend = "alpha"
depth_test = false
depth_write = false
cull = "none"

[[Shader.Keywords]]
name = "USE_RIM"

[[Shader.Keywords]]
name = "USE_NORMAL_MAP"

[[Shader.Uniforms]]
name = "u_tint"
type = "vec4"
default = [1.0, 1.0, 1.0, 1.0]

[[Shader.Uniforms]]
name = "u_time"
type = "float"
source = "Engine.Time.elapsed"
```

## `*.mat.conf`

```toml
[Material]
schema = 1
id = "player_body"
shader = "sprite_lit"

[Material.Textures]
main = "assets/Sprites/player.png"
normal = "assets/Sprites/player_n.png"

[Material.Params]
u_tint = [1.0, 0.95, 0.95, 1.0]
u_rim_power = 2.2

[Material.Keywords]
USE_RIM = true
USE_NORMAL_MAP = true
```

## Phased Implementation Roadmap

## Phase 0: Platform and Pipeline Foundation (1-2 weeks)

- Audit raylib/GL backend constraints across macOS, Linux, Windows targets.
- Define supported shader language/versions and forbidden features for v1.
- Introduce `src/render/` module boundary so rendering no longer lives only in `runtime_loader.c`.
- Add feature flag: `Runtime.Rendering.enable_shader_pipeline`.

Exit criteria:

- Rendering can run in "legacy path" and "new render module path" without behavior changes.

## Phase 1: Shader Asset Registry + Validation (1-2 weeks)

- Implement parser/validator for `global.shaders.conf` and `*.shader.conf`.
- Add validation errors with file + table + key context (same style as config loader diagnostics).
- Add startup compilation for default shaders with fallback handling.
- Add `--validate-config` CLI mode to validate shader/material files offline.

Exit criteria:

- Invalid shader config fails fast with actionable diagnostics.
- Valid shader config compiles and registers programs in runtime cache.

## Phase 2: Material System + Actor Binding (2-3 weeks)

- Add `Material` runtime type and parser for `*.mat.conf`.
- Add `RenderMaterial` builtin component (or extend `StaticSprite`/`AnimatedSprite` with material reference).
- Replace direct `DrawTexturePro` sprite path with material-driven draw submission.
- Add texture slot caching and shader location caching.

Exit criteria:

- Sprites can render with per-material shaders and parameters.
- Missing materials/shaders cleanly fall back and log once.

## Phase 3: RenderGraph MVP + Multi-Pass (2-3 weeks)

- Introduce a small deterministic render graph:
  - World pass
  - Light accumulation pass
  - Post process chain
  - UI pass (kept isolated)
- Support render targets and pass-to-pass texture handoff.
- Add scene-level post stack declaration and profile-based toggles.

Exit criteria:

- At least 3 built-in post effects (bloom-lite, color grading LUT, vignette/scanline).
- A custom pass can read previous pass texture and write to next target.

## Phase 4: Lua Runtime Control Surface (1-2 weeks)

- Add `Engine.Shader` module:
  - `set_global_float(name, value)`
  - `set_global_vec4(name, x, y, z, w)`
  - `set_keyword(name, enabled)`
  - `reload_all()` (development only)
- Add `Engine.Material` module:
  - `set_float(actor_id, slot, value)`
  - `set_color(actor_id, slot, r, g, b, a)`
  - `set_texture(actor_id, slot, path)`
- Add strict runtime guards for missing uniforms/types.

Exit criteria:

- Scripts can animate shader parameters safely at runtime.
- Type mismatches fail with readable diagnostics, not crashes.

## Phase 5: Advanced Lighting + Feature Parity Push (3-4 weeks)

- Move from global tint style lighting toward shader-driven light evaluation.
- Feed point/spot/directional light buffers into world shaders.
- Add optional normal mapping and ramp/toon lighting support.
- Add shadow plan for v1.5 (single shadowed directional first).

Exit criteria:

- Scene light components materially influence shaded output.
- At least one sample scene demonstrates dramatic visual style changes via shader swaps.

## Phase 6: Tooling, Hot Reload, and Authoring UX (2 weeks)

- Add hot reload for shader source and material config in development mode.
- Add debug overlay for active passes, shader variants, compile times, and fallback hits.
- Add documentation pages and sample assets in `example-project/`.

Exit criteria:

- Author can edit shader code and see update in running project with safe rollback on compile failure.

## Phase 7: Stabilization and Performance Gates (2 weeks)

- Benchmark GPU/CPU cost by profile and scene scale.
- Add shader variant stripping policy at load/build time.
- Add automated tests for parser validation and runtime fallback behavior.
- Lock v1 schema and publish migration notes.

Exit criteria:

- Performance budgets met for default + low profiles.
- No hard crashes on shader compile/link/lookup failures.

## Recommended Initial Feature Set (MVP)

- Unlit sprite shader + lit sprite shader + UI shader.
- Material parameters: float, vec2/3/4, color, texture2D.
- Global uniforms: time, screen size, camera data, scene exposure.
- Post stack with max 4 passes.
- Development-only hot reload.

## Deferred (Post-v1)

- Node-based shader editor.
- Compute shaders.
- Full deferred rendering pipeline.
- Advanced volumetrics.
- Automatic shader reflection tooling across backends.

## Risks and Mitigations

- Variant explosion:
  - Mitigation: keyword budget per shader/profile + stripping rules.
- Platform shader incompatibility:
  - Mitigation: strict supported GLSL profile matrix + startup conformance checks.
- Runtime instability from user shader errors:
  - Mitigation: fallback shader policy + compile sandbox + one-time log suppression.
- Complexity creep in `runtime_loader.c`:
  - Mitigation: migrate rendering into dedicated `src/render/*` modules early (Phase 0).

## Suggested Deliverables by Milestone

- M1: Shader config parser + compile cache + fallback shader.
- M2: Material-bound sprite rendering.
- M3: RenderGraph MVP with post stack.
- M4: Lua shader/material API.
- M5: Light-buffer integration and stylized lighting demo.
- M6: Hot reload + debug tooling + docs.
- M7: Stabilization, performance, release checklist.

## Definition of Done (v1)

- A game can define and load custom shaders/materials from config files only.
- A game can apply both per-object and full-screen shader effects.
- Lua can safely drive shader parameters at runtime.
- Shader failures degrade gracefully with logs and fallback rendering.
- Example project ships with at least two visibly distinct shader-driven visual styles.
