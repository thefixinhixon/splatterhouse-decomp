# ROV: why it is required and how to optimize it

Performance document for the port. Summarizes the analysis of the **ROV** path
(Rasterizer-Ordered Views / Pixel Shader Interlock) of the ReXGlue SDK D3D12
backend and the ways to speed it up, aimed at modest GPUs.

## 1. Why ROV is required (and not RTV)

The Xbox 360 writes into **10 MB of eDRAM** that games constantly
**reinterpret** in different formats (clear, HDR, lighting ping-pong…), and it
uses formats that **don't exist on PC** (7e3 HDR, fixed −32…32, depth 20e4). On PC
render targets are independent resources.

- **RTV** path (RT/DSV + host↔eDRAM copies): fast, but it **approximates** format
  and reinterpretation → in games that overlap/reinterpret eDRAM (e.g.
  Splatterhouse's lighting, and PGR4 — see Xenia issue #161) the scene ends up
  almost black (only emissives/UI). **It can't be fixed with cvars** (tried:
  `readback_resolve=full`, `direct_host_resolve=false`,
  `mrt_edram_used_range_clamp_to_min=false`, `gamma_render_target_as_unorm16=false`
  → still dark).
- **ROV / FSI** path: renders **directly** into the eDRAM (an `R32_UINT` buffer) and
  does blending / depth / stencil **by hand in the pixel shader**. Correct, but
  slower.

On Vulkan the trade-off is identical: `render_target_path_vulkan = "fbo"` (like
RTV) vs `"fsi"` (like ROV, "much slower"). The problem doesn't change.

**Conclusion:** ROV is the only correct path today. All performance work for
weak GPUs comes down to **making it cheaper** or **reducing pixels**.

## 2. Why ROV is slow

The ROV pixel shader (`extern/rexglue-sdk/src/graphics/pipeline/shader/dxbc_translator_om.cpp`,
~3000 lines) emulates the output merger **in software**, with state read from the
**system-constant buffer** and **branching per pixel**:

| system constant | use |
|---|---|
| `xe_edram_rt_format_flags` | format unpacking/packing (`OpSwitch`) |
| `xe_edram_rt_blend_factors_ops` | blend factors/operations (`OpSwitch`/`OpIf`) |
| `xe_edram_rt_keep_mask` | per-component write masks |
| `xe_edram_rt_clamp` | per-format clamp |
| `xe_edram_rt_base_dwords_scaled`, `xe_edram_32bpp_tile_pitch_dwords_scaled` | tiled eDRAM addressing |
| `xe_edram_stencil`, `xe_edram_blend_constant`, sample count | stencil/blend/msaa |

Each pixel also does a **read-modify-write** of the eDRAM (UAV) with a computed
tile address. Result: lots of instructions per pixel, **divergence** and a
dependency on the eDRAM → the typical bottleneck on modest GPUs.

## 3. The optimization the SDK points to: static specialization

SDK TODO (`extern/rexglue-sdk/src/graphics/d3d12/render_target_cache.cpp`,
`render_target_path_d3d12` selection):

> *"Make ROV the default when it's optimized better (for instance, using **static
> shader modifications to pass render target parameters**)."*

The render target parameters (format, blend, write masks, clamp) are
**constant per draw** (they come from the register file). Today the translator
does **not** receive them at compile time (the `DxbcShaderTranslator` constructor
only receives `edram_rov_used`, MSAA, scale…), so the shader is generic and the
`OpIf`/`OpSwitch` are evaluated at runtime.

**Plan (upstream, large scope):**
1. Pass the RT config (color formats, blend control/ops, write masks, clamp)
   to `DxbcShaderTranslator` / `SpirvShaderTranslator` at compile time.
2. Replace the OM's `LoadSystemConstant(kEdramRTFormatFlags/…)` with those
   **static** values → constant folding; the `OpSwitch`/`OpIf` disappear.
3. Include those values in the **PSO key** (cached variants).
4. Keep dynamic whatever truly is dynamic (e.g. RT selection by control flow:
   see `command_processor.cpp`, the "lighting pass" comment).

Cost/risk: **high** (both backends, possible correctness regressions). It is the
ideal contribution to the SDK.

## 3b. Measured bound (RTX 5060 Ti, 1080p, FSR, vsync off, gameplay)

ROV vs RTV comparison (cheap OM, broken lighting) to bound how much ROV's
software output merger costs:

| Path | median | p90 | max |
|---|---|---|---|
| ROV  | 83 FPS  | 178 | 302 |
| RTV  | 134 FPS | 354 | 413 |

ROV is ~**1.6×** slower. That is the **upper bound** of what can be recovered
(part of the difference is eDRAM copies that ROV avoids, so specialization would
recover less, ~1.2-1.4×). With vsync at 60 Hz it isn't a limit on this GPU, but
on modest GPUs it is.

### Implementation blocker

Specializing requires putting the RT parameters into the **shader key**, which
today is a `uint64_t` (`DxbcShaderTranslator::Modification::value`,
`shader.h: std::unordered_map<uint64_t, Translation*>`) and **has no room**
for `edram_rt_format_flags[4]` (128 bits). The **key has to be widened** across
the whole SDK: `Modification`, `Shader`/`Translation`, `PipelineDescription`,
`.xpso`/Vulkan `.xpso` serialization and `Modification::kVersion`. It's a
multi-day change (both backends) and a candidate for an upstream PR.

## 3c. Shader / PSO compilation (the "first time" stutters)

When a new material is used, the port: **(1)** translates the Xenon shader → DXBC (**on background threads**), **(2)** creates the D3D12 PSO (driver compilation, **on background threads** `d3d12_pipeline_creation_threads`) and **(3)** if it isn't ready yet, **skips the draw** (`async_shader_compilation = true`) → brief popping instead of freezing the frame. All of this **is already in the SDK**.

On startup, the SDK **pre-creates in parallel** all the pipelines in the cache and logs it (`log_level=info`):

```
Translated 346 shaders from the storage in 28 milliseconds
Created 401 graphics pipelines (not including reading the descriptions) from the storage in 530 milliseconds
```

**Per-PSO measurement** (`sh_pso_log=true`, RTX 5060 Ti): **~2–11 ms** each. So it isn't one very expensive PSO, but **hundreds in a row** when entering a new area → the solution is a **complete cache + prewarm** (already existing), not more asynchrony.

- Diagnostics: cvar **`sh_pso_log`** → log `[sh-pso] created VS … PS … in X ms` per pipeline and `draw skipped` when one is missing; `log_level=info` shows the startup totals.
- Distribution: `d3d12_pipeline_creation_threads` (auto).
- **Shipping the cache**: play once → `tools\pack_shadercache.bat` copies `%MyDocuments%\splatterhouse\cache\shaders\shareable\*` to the release's `shadercache\`; `install.bat` installs them for the user. With the complete cache, PSOs are created **at startup** (once) and not during gameplay.
- Optional (per-machine, not distributable): `ID3D12PipelineLibrary` to store the driver-compiled blobs so the prewarm is almost instant on later runs.

## 3d. Measured combat profile (`sh_render_profile`)

Opt-in instrumentation in the D3D12 command processor (log `[sh-render]`):
aggregates draws per interval, per VS/PS pair (ucode hash), per primitive type and
per "pass" (color formats + color0 base + surface pitch + msaa + depth base and
format), with a per-pixel cost proxy (accumulated scissor area). Enable with:

```toml
sh_render_profile = true
sh_render_profile_interval_s = 2.0   # how often the summary is dumped
sh_render_profile_top = 8            # number of top entries to list
```

Measured (RTX 5060 Ti, 1080p, D3D12+ROV, real gameplay):

| Scene | draws/frame | verts/frame | dominant passes |
|---|---|---|---|
| Frontend/menu | ~75 | ~0.4k | 1 main pass (pitch 1040, 2x MSAA) |
| **Combat (3+ enemies)** | **up to ~1350** | **up to ~1.1M** | main scene (~850 d/f) + **depth/shadows** (~475 d/f) |

- Primitives: **triangle lists** (`0x4`) dominate.
- Depth pass shaders (VS-without-PS): `4B7396D6DA4F3D08`, `2EE71F998E3C653F`,
  `8A4840C848499397`, `B6C9863F710683EC`, `007A49CEB83DEFD0`.
- On the RTX 5060 Ti the swap holds ~60 (min 30); the "slideshow" is reported on
  **AMD** GPUs (RX 9070 XT / RX 7800 XT), where the interlock (ROV) is much more expensive.

**Goal**: make the ROV path's software OM cheaper in **both passes** (the scene
color pass and the depth-only shadow pass), not just shadows. In ROV, draws
without a PS use `depth_only_pixel_shader_` (`dxbc_translator_om.cpp`,
`CompletePixelShader`), which does the depth test/write in the shader under
interlock.

## 4. Partial wins (cheaper)

- Specializing only the most frequent combos (e.g. RT `k_8_8_8_8`, 1 RT, no
  blending) covers most draws.
- Remove the keep-mask/clamp computation when the format fills the whole
  register.
- Skip color unpacking/packing when the shader doesn't write that RT
  (already done: `shader_writes_color_targets`).

## 5. Levers without touching the shader (what can be shipped today)

ROV cost scales with **pixels**; on weak GPUs the most effective thing is to lower
the guest internal resolution and upscale with FSR:

- `video_mode_width/height` ← guest internal resolution (the biggest lever).
- `present_effect = "fsr"` ← upscales to the window with better quality than bilinear.
- `d3d12_present_frame_limiter[_fps]` ← cap at 30 FPS if it can't reach 60.
- `texture_cache_memory_limit_soft` ← less VRAM (helps on GPUs with little memory).

Presets in the port's graphics screen (**F5** key):

| Preset | video_mode | texture cache |
|---|---|---|
| Low    | 960x540  | 256 |
| Medium | 1280x720 | 512 |
| High   | 1600x900 | 768 |
| Ultra  | 1920x1080| 1024 |

## 6. How to measure

- cvar `sh_log_fps` → log `[sh-fps] guest N FPS` every 2 s (GPU plugin).
- Runtime debug overlay (F3) → FPS.

## 7. AMD/Intel reports (issue #3) and what can be done today

Issue #3 reports: **RX 9070 XT, RX 7800 XT and Arc B580** — fine in corridors,
but **in combat with ≥2 enemies it stutters**; the same user confirms that
forcing **Vulkan** makes combat smooth.

- **Confirmed**: it's not the CPU (measured: 1–5 % of 12 cores) nor a port bug. It's
  the **interlock of the ROV path on D3D12**, which on AMD/Intel is
  disproportionately expensive with **overdraw** (combat: characters and particles
  overlapping) and with **two passes** (color ~850 draws + depth/shadows ~475).
  On NVIDIA the same workload holds 60 FPS.
- **Vulkan (FSI) isn't the solution yet**: it's fast in combat (its interlock
  is native), but today it renders **globally darker** and in some scenes
  produces **black/opaque layers**; also `present_effect=fsr` doesn't apply. It
  remains a future path (fix gamma + stability), not the default.
- **RTV won't do**: it would fix performance but **breaks the lighting** (it's the
  design limitation of the RTV path, see §1).

**Shipped mitigation (2026-10)**: ROV cost scales with pixels, so the lever is
the **internal resolution**:

| GPU | Installer default internal resolution | Pixels vs 1080p |
|---|---|---|
| AMD / Intel | 1280x720 + FSR | ~0.44x |
| NVIDIA | 1920x1080 + FSR | 1x |

`install.bat` detects the GPU name (`Win32_VideoController`) and writes
`splatterhouse.toml` with 720p on AMD/Intel. On the **F5** screen it can be raised
or lowered live with **Preset** (Low 960x540 … Ultra 1920x1080) or with
**Internal resolution**; the preset keeps the aspect (16:9, 21:9…).

If it's still not enough: drop to **960x540** (0.25x the pixels of 1080p) or cap
at **30 FPS** (`FPS limit`), which with the limiter is stable and doesn't stutter.

## 8. Why the FPS jumped 60 ↔ 30 (and the fix)

Reported symptom: in combat the game goes from 60 to 30 and back, with no
values in between. The logs (`[sh-swap]`) confirm it: the series is **bimodal**
(60/61 and 30/31, with an odd 1 s of 38–55 at the transitions).

- **It wasn't double buffering**: the swapchain is `DXGI_SWAP_EFFECT_FLIP_DISCARD` with
  **3 buffers** (`D3D12Presenter::PaintContext::kSwapChainBufferCount`) and the
  command processor allows **3 queued frames** (`kQueueFrames = 3`).
- **Cause**: `Present(0, …)` **without** `ALLOW_TEARING` syncs to the
  vblank. A frame that takes 17 ms waits for the next vblank (33 ms), and since the
  next one is late again, the game gets **stuck at 30** (half vblank) until the
  load drops. The 60 limiter doesn't prevent it: it only spreads the presents
  within that grid.
- **Fix**: **adaptive tearing** (`d3d12_present_adaptive_tearing=true` by
  default). The swapchain is always created with `ALLOW_TEARING` (if DXGI
  supports it) and the presenter measures how long the frame takes to be ready
  (from when the previous present returned until before waiting on the limiter,
  with smoothing and hysteresis): if it's above **0.95 × 16.6 ms** it passes
  `DXGI_PRESENT_ALLOW_TEARING` (doesn't block → 58, 52, 45… gradually); if there's
  headroom (<0.80 ×) it removes it and the present is synced to vblank.
  - **At 60 FPS there is no tearing** (the present is synced again). Before
    this adjustment `ALLOW_TEARING` was passed on *every* frame, and since the
    60 limiter places presents right after the vblank, a tear line was visible
    at the **top** of the screen.
  - Below 60 there may be some occasional tearing: it's the price of not
    getting stuck at 30. With **VRR/G-Sync** (`d3d12_allow_variable_refresh_rate_and_tearing=true`,
    "always (VRR)") none is visible.
  - For the old behavior (60/30 without tearing): both cvars set to `false`
    (F5 → **Tearing / VRR** → *off (60/30)*).

## References

- Xenia wiki, *ROV*: why the path exists and its limits.
- Xenia Canary issue #161 (PGR4): "with RTV any light sources don't seem to emit
  light… ROV fixes it" — the same class of bug as Splatterhouse.
- `extern/rexglue-sdk/src/graphics/d3d12/render_target_cache.cpp` (path selection).
- `extern/rexglue-sdk/src/graphics/pipeline/shader/dxbc_translator_om.cpp` (ROV OM).
- `extern/rexglue-sdk/src/graphics/vulkan/render_target_cache.cpp` (fbo vs fsi).
