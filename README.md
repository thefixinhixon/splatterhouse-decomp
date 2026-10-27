# Splatterhouse Recompiled for Linux

Native **Linux** build of **Splatterhouse (2010)** for Xbox 360 (title id `4E4D07F0`). It is made by
**static recompilation** with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk): the game's
PowerPC code is translated to C++ and compiled for x86-64. It is **not an emulator**: no code is
interpreted at runtime. Graphics run on **Vulkan**.

> **This is a fork** of [0c0de/splatterhouse-decomp](https://github.com/0c0de/splatterhouse-decomp),
> the original Windows port. The reverse engineering, the game patches, the F5 options screen and DLC
> support are the original author's work. This fork adds the Linux/Vulkan build, fixes for crashes
> and slowdowns found on Linux, and FSR on Vulkan. See [What this fork changes](#what-this-fork-changes)
> and [Credits](#credits-and-legal-notice).
>
> **Playing on Windows?** Use the original repository and its releases. This branch (`linux-port`)
> is Linux-only.

> **No game code or assets are included.** You need your own copy of the Xbox 360 game.

**Status: playable.** Menus, story mode, cutscenes, combat and chapter select work. The game runs at
60 FPS with the official 60 FPS patch. Text can be shown in other languages, and DLC loads. There are
no release binaries yet: you build it yourself (about 15 minutes, see below).

---

## Requirements

| | |
|---|---|
| **OS** | 64-bit Linux (x86-64). Developed and tested on Ubuntu 26.04 in a KDE Plasma Wayland session. |
| **CPU** | x86-64-v2 or newer (SSE4.2 and SSSE3: any Intel/AMD CPU from about 2009 on). Tested on an i7-6700K (4 cores / 8 threads). |
| **GPU** | Vulkan driver with **`VK_EXT_fragment_shader_interlock`**. Tested on an AMD RX 6600 with Mesa RADV 26.0. Check yours with `vulkaninfo \| grep fragment_shader_interlock`. |
| **Disk** | About 5 GB for the extracted game plus about 2 GB for the source and build. |
| **Game** | Your own copy of **Splatterhouse for Xbox 360** (an ISO you made from your disc). |

**Why the interlock extension?** The game relies on how the Xbox 360's eDRAM blends and stores pixels.
The only path that reproduces its lighting correctly is `fsi` (fragment shader interlock, the Vulkan
counterpart of D3D12's ROV): blending is done in the shader. Without it, the scene renders almost
black. This path costs GPU time per pixel, which is why the internal resolution matters for
performance (see [Performance](#performance-and-recommended-settings)).

---

## Getting started

### 1. Install the build tools

On Ubuntu/Debian:

```bash
sudo apt install git cmake ninja-build clang-20 python3 \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxkbcommon-dev \
  libwayland-dev libdecor-0-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev
```

- **Compiler and build tools:** the build preset calls `clang-20`/`clang++-20` by name. With another
  Clang (18 or newer), add `-DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19` (for example)
  to the configure command. Only Clang 20 has been tested. CMake must be 3.25 or newer.
- **Bundled libraries:** SDL3, the Vulkan headers and the other libraries come with the SDK.
- **Window, audio and input packages:** the `-dev` packages above are the window, audio and input
  backends that SDL looks for. Without them SDL builds, but without those backends.

### 2. Extract your game

Build [extract-xiso](https://github.com/XboxDev/extract-xiso) (it isn't in the Ubuntu archive), then:

```bash
git clone -b linux-port https://github.com/thefixinhixon/splatterhouse-decomp.git
cd splatterhouse-decomp
extract-xiso -s -d game -x "/path/to/Splatterhouse.iso"   # creates game/default.xex and game/data/
```

You only need the retail `default.xex`. The build decrypts it, so no XEX tool is needed (a
`default.dec.xex` made with XexTool also works, if you already have one).

### 3. Build

```bash
git submodule update --init extern/rexglue-sdk
git -C extern/rexglue-sdk submodule update --init --recursive
cmake --preset linux-amd64-relwithdebinfo
cmake --build out/build/linux-amd64-relwithdebinfo -j$(nproc)
```

The first build recompiles the game (about 2.5 minutes of codegen, then about 15 minutes of
compiling on 8 threads). After that, rebuilds take seconds unless the SDK or codegen changes.

**If the build stops with `XEX file not found`,** the game isn't at `game/default.xex`. Fix the path
and build again.

### 4. Run

```bash
cd out/build/linux-amd64-relwithdebinfo
ln -s ../../../game assets        # once: the game looks for its data in ./assets
./splatterhouse
```

- **Config file:** none is needed. The port picks the Vulkan `fsi` path itself. Settings you save
  from the in-game options screen (**F5**, then **S**) go to `splatterhouse.toml` in this folder.
- **Libraries:** the build copies `librexruntimerd.so`, `librexgpu-xenosrd.so` and
  `libTracyClientrd.so` next to the executable.
- **Don't rebuild while the game is running.** Replacing those libraries under a running game
  corrupts it.
- **Stutter-free first play:** shaders are compiled in the background the first time you see a new
  material. Instead of stuttering, an object can be missing for a frame or two (pop-in). The cache
  is kept in `~/.local/share/splatterhouse/cache/`, so it only happens once.

---

## Controls

**Gamepad:** Xbox-style controllers work through SDL (the `sdl` input backend, the default).

**Keyboard:** set `mnk_mode = true` in `splatterhouse.toml` to play with the keyboard as an emulated
pad. Every key can be rebound with the `keybind_*` settings (F4).

| Pad | Keys | Pad | Keys |
|---|---|---|---|
| A | Space, `;` | Left stick | W A S D (click: F) |
| B | Backspace, `'` | Right stick | Arrow keys (click: K) |
| X | L | D-pad | Shift + arrow keys |
| Y | P | LT / RT | Q or I / E or O |
| Start | Enter, X | LB / RB | 1 / 3 |
| Back | Tab, Z | | |

**Hotkeys**

| Key | Action |
|---|---|
| **F5** | **Port options** (graphics, upscaling, 60 FPS patch, languages, level unlock…) |
| **F3** | Debug overlay (FPS, frame statistics) |
| **F4** | Runtime settings (every setting, including key bindings) |
| **F7** | Achievements |
| **`** (backtick) | Console |

### The F5 options screen

The port's own options screen, in the game's style. The game's menus are compiled Flash (Scaleform)
and can't be extended, so the options live here.

- **Navigation:** ↑/↓ (or the mouse wheel) select a row, ←/→ change it, **S** saves to
  `splatterhouse.toml`, and **Esc** or **F5** closes. Clicking also works.
- **Saving:** keeps your other settings, and the previous file is kept as `splatterhouse.toml.bak`.
  Rows marked `[restart]` (the video mode) apply on the next start.
- **Interface → Menu language:** switches this screen between English and Spanish.
  **Game → Game language** is separate: it sets the game's own text.
- **Presets:** Low/Medium/High/Ultra set the *vertical* internal resolution (540p/720p/900p/1080p),
  keep your aspect ratio and turn on FSR.
- **Hidden rows:** rows for Windows-only settings (D3D12 render path, tearing modes, PSO threads)
  don't appear on Linux.

---

## Performance and recommended settings

The **internal resolution** is the biggest lever. The `fsi` path does blending inside the pixel
shader, so its cost grows with the number of pixels, and heavy fights draw a lot of them. On AMD and
Intel GPUs this per-pixel interlock is especially expensive.

**Recommended start:** 720p internal resolution (F5 → **Preset → Medium**, or **Internal
resolution → 1280x720**) with **Presentation → `fsr`**. FSR 1 upscales to your window or screen and
sharpens it. On the test machine (RX 6600, i7-6700K) this held a median of **60 FPS** through a full
play session, including heavy fights and areas whose shaders weren't cached yet.

**If it's still slow:**
- Lower the preset to **Low** (540p).
- Turn off **Presentation antialiasing** (FXAA) and **Native 2x MSAA**.

**If you have headroom:** raise the internal resolution. `cas` (sharpening only) or `bilinear` are
also available.

**Frame rate and game speed:** the game paces itself to a 60 Hz guest vblank. With the 60 FPS patch,
game time follows the real frame time (`sh_60fps_realtime`, on by default). If your system drops
below 60, the game stays at normal speed instead of going into slow motion.

**Reporting a slow scene:** start the game with `./splatterhouse --sh_perf_log=true`. It writes a
`[sh-perf]` line every 2 seconds with frame timings to the log. Attach the log to the report.

### Ultrawide (21:9, 32:9)

This works automatically: on startup, the *width* of the internal video mode is matched to your
window or screen (`sh_aspect = "auto"`). The game renders that aspect natively, with no bars and no
stretching. The log shows the result:

```
[sh-aspect] auto: internal mode 2560x1080 (was 1920x1080, screen 2.37:1)
```

- **Override:** F5 → **Internal aspect** (`auto`, `16:9`, `21:9`, `32:9`). Picking an **Internal
  resolution** by hand sets it to `keep`.
- **Cost:** a wider image means more pixels. 21:9 at 1080p is about 33% more GPU work than 16:9 at
  1080p.

---

## DLC (Survival Arenas, masks, etc.)

Put the **STFS packages** in a folder named **`dlc`** next to the `splatterhouse` executable. These
are files whose first 4 bytes are `CON `, `LIVE` or `PIRS`, often with no extension or a `.xcp`
extension. On startup the port creates the folder, logs its path and installs any packages it finds.
They then appear in the game's menus.

```
out/build/linux-amd64-relwithdebinfo/dlc/35A606675772B2C90230F20D17FA0F11E55C1B594E
```

- **Other folders scanned:** `~/.local/share/splatterhouse/4E4D07F0/00000002/`,
  `~/.local/share/splatterhouse/0000000000000000/4E4D07F0/00000002/` and
  `~/.local/share/splatterhouse/<profile-xuid>/4E4D07F0/00000002/`. The `$XDG_DATA_HOME` location
  is used instead of `~/.local/share` if that variable is set.
- **Installed once:** packages are extracted to
  `~/.local/share/splatterhouse/0000000000000000/4E4D07F0/00000002/<name>/`. After the first run
  you can delete the originals.
- **Nothing shows up?** Run `REX_SH_DLC=1 ./splatterhouse` to log which content the game looks for
  and opens. Also check that the first 4 bytes of each file are `CON `, `LIVE` or `PIRS`
  (`head -c4 file`).

More detail in `docs/dlc.md`.

---

## Troubleshooting

**The game exits right away and leaves `assets_error.txt`.**
The game data isn't where the port looks for it. `assets_error.txt`, next to the executable, says
which folder was checked and what's missing.
- **Expected layout:** `assets/default.xex` plus `assets/data/`.
- **Other accepted places:** the executable's own folder, or a single subfolder inside `assets/`.
- **Somewhere else:** set `game_data_root = "/path/to/game"` in `splatterhouse.toml`, or pass
  `--game_data_root=...`.

**The 3D scene is almost black.**
The render path is not `fsi`. Remove any `render_target_path_vulkan` line from
`splatterhouse.toml`; the port then picks `fsi` itself. Also check that your driver has
`VK_EXT_fragment_shader_interlock`.

**Fights are slow.**
Lower the internal resolution and use FSR (see [Performance](#performance-and-recommended-settings)).

**A crash with SIGBUS, often while a level loads.**
The game backs its memory with a large shared-memory file in `/dev/shm`. Check that `/dev/shm` isn't
full (`df -h /dev/shm`).

**A controller keeps rumbling after the game closed.**
Start the game again and quit normally: it stops the motors when it opens the pad. While the game
is running, a rumble that the game stops refreshing is cut after `sh_rumble_timeout_ms` (10 s).

**Where are the logs?**
In `logs/` in the folder you start the game from, or in the file set by `log_file` in
`splatterhouse.toml`. For more detail set `log_level = "info"` or `"trace"`.

---

## Configuration (`splatterhouse.toml`)

`splatterhouse.toml` lives next to the executable and is optional. F5 writes it for you; these are
the settings you're most likely to change by hand.

| Setting | Values | Notes |
|---|---|---|
| `video_mode_width` / `video_mode_height` | e.g. `1280` / `720` | internal resolution, the main performance lever |
| `present_effect` | `bilinear`, `cas`, `fsr` | upscaling to the window |
| `swap_post_effect` | `none`, `fxaa`, `fxaa_extreme` | antialiasing on the final image |
| `native_2x_msaa` | `false` / `true` | 2x MSAA in the guest render targets (costly) |
| `sh_aspect` | `auto`, `16:9`, `21:9`, `32:9`, `keep` | internal aspect (`auto` matches the window) |
| `fullscreen` | `false` / `true` | |
| `texture_cache_memory_limit_soft` | MB, e.g. `512` | |
| `sh_60fps` / `sh_60fps_realtime` | `true` / `true` | 60 FPS patch; game time follows the real frame time |
| `sh_language` | `auto`, `english`, `french`, `italian`, `german`, `spanish`, `japanese` | game **text** (audio is always English) |
| `sh_menu_language` | `english`, `spanish` | language of the F5 screen |
| `sh_unlock_levels` / `sh_unlock_levels_max` | `true` / `7` | all chapters selectable |
| `mnk_mode` | `false` / `true` | keyboard as an emulated pad |
| `input_backend` | `sdl` | the only backend on Linux |
| `sh_rumble_timeout_ms` | `10000` (`0` = off) | cuts a rumble the game stops refreshing |
| `log_level` | `warn`, `info`, `trace` | |

These defaults are already right for Linux, so leave them out of the file unless you are testing:
- `render_target_path_vulkan = "fsi"`
- `vulkan_async_skip_incomplete_frames = false`
- `clear_memory_page_state = false`
- `vulkan_async_shaders_nonblocking = true`

---

## What this fork changes

Compared with the original Windows port. Measurements and the full investigation notes are in
`AGENTS.md`, section "Linux port".

- **Linux/Vulkan build:**
  - Linux always uses Vulkan with the `fsi` path.
  - The SDK's SPIR-V translator read the texture exponent bias from the wrong fetch-constant word,
    so the whole image came out dark and lost most of its green; this is fixed.
  - Patches from the original author's private SDK copy were re-applied where Linux needs them:
    - the 60 Hz guest vblank with vsync off;
    - saving `splatterhouse.toml` without losing other keys;
    - the rumble watchdog;
    - function discovery fixes in codegen.
- **Level-load freeze fixed:** a stale signal on the XCTD layer's completion event made
  `GetOverlappedResult` return while a read was still pending. A 64 KB block of a level file was then
  replaced by the previous one, which crashed about half of all level loads on Linux
  (`src/sh_overlapped_fix.cpp`).
- **Performance:**
  - Multi-object waits no longer spin a CPU core.
  - Repaints of an unchanged image are paced.
  - Shader translation and pipeline creation never block the renderer.
  - Vertex and index data written by the CPU is no longer uploaded again every frame.
  - Memory write-watch faults no longer re-read `/proc/self/maps` under a global lock.

  On the test machine, the median of a play session went from 33 to 60 FPS. Fights that used to drop
  to about 20 FPS now stay near 60.
- **60 FPS patch:** game time follows the real frame time, so dropping below 60 FPS no longer means
  slow motion.
- **FSR/CAS on Vulkan:** the spatial FidelityFX effects use precompiled shaders and no longer need
  the FidelityFX SDK.
- **Smaller changes:**
  - Level warp (`sh_warp`, a developer tool) works on Linux.
  - The F5 screen is in English, with Spanish selectable.
  - Documentation is in English.
  - The build reads the retail `default.xex` directly.
  - `tools/linux/headless_run.sh` runs automated tests in a virtual display (see `AGENTS.md`).

---

# Development

## How the port works

There is no source code for the game. The port starts from the **retail XEX** and is **statically
recompiled**:

1. **Codegen** (`rexglue codegen splatterhouse_manifest.toml`, run by the build): ReXGlue loads and
   decrypts `game/default.xex`, analyses the PowerPC code and emits recompiled C++. The output goes
   to `generated/` (about 200 `.cpp` files, gitignored), together with a function table.
2. **Runtime** (the SDK):
   - guest memory;
   - the Xbox kernel and XAM functions;
   - the file system and STFS content;
   - threads;
   - the GPU (Xenos → Vulkan);
   - audio (XMA → SDL) and input (SDL).
3. **SDK patches:** codegen and runtime fixes, kept on the SDK fork's `linux-port` branch. `AGENTS.md`
   lists them and explains why each one exists.
4. **Game patches**, in `src/` (never in `generated/`, which is regenerated). Each one overrides a
   recompiled function by name:
   - **60 FPS:** the frame-time data (`0x82F8FF58/5C`) plus one instruction (`0x8247A4AC`), and
     the real-time frame step (`src/sh_frame_time_hook.cpp`).
   - **Localization:** `sub_827B65E0` returns the chosen language only on the text-loading path, so
     audio stays in English.
   - **Level unlock:** `sub_82E5C540` / `sub_82EF0FE0` report the last chapter as completed.
   - **Level-load fix:** `sub_829F0958` (`GetOverlappedResult`) ignores a stale completion signal.
   - **Level warp:** a hook on the present function `sub_822551A0` (`src/sh_warp_hook.cpp`).

**After changing codegen:** changes under `extern/rexglue-sdk/src/codegen/` need the `rexglue` target
rebuilt, then `extern/rexglue-sdk/out/linux-amd64/rexgluerd codegen splatterhouse_manifest.toml
--ignore-stamp`, then a full build.

## Testing

`tools/linux/headless_run.sh` runs a copy of the build in Xvfb, with software Vulkan presentation,
muted audio and no real controllers. It drives the game with timed key presses and saves a
screenshot at each one, plus the full log. It can repeat runs and count crashes (`RUNS=10`), and stop
in gdb at the first crash (`GDB=1`). The checkpoints used for every change (boot, level-load
stability, soak, performance) are described in `AGENTS.md`.

## Project layout

```
src/
  main.cpp                  # app entry point and the port's settings
  splatterhouse_app.h       # asset paths, patches, DLC install, aspect, platform defaults
  graphics_menu.h           # F5 options screen
  sh_60fps_hook.cpp         # 60 FPS patch (instruction)
  sh_frame_time_hook.cpp    # game time follows the real frame time
  sh_overlapped_fix.cpp     # level-load freeze fix
  sh_xctd_cache_fix.cpp     # XCTD cache reader/writer race
  sh_warp_hook.cpp          # level warp (dev)
  sh_language_hook.cpp      # text localization
  sh_unlock_hook.cpp        # chapter unlock
  sh_uf_diag.cpp            # guard for a null virtual call
config/codegen.toml         # codegen flags and extra function entry points
generated/                  # recompiled code (generated; gitignored except rexglue.cmake)
game/                       # your extracted game (gitignored)
extern/rexglue-sdk/         # SDK submodule (fork, branch linux-port)
tools/linux/headless_run.sh # headless test harness
docs/                       # DLC, GPU, recompilation and ROV/FSI performance notes
AGENTS.md                   # technical log: status, investigations, SDK patches, test checkpoints
install.bat, tools/*.bat, tools/*.ps1   # Windows-only files from the original port (unused here)
legacy/                     # old XenonRecomp runtime (reference only)
```

---

## Credits and legal notice

- **Original port:** [0c0de](https://github.com/0c0de) (Jose Luis). The Windows port, the reverse
  engineering, the game patches, the F5 options screen, DLC support and the original SDK patches
  this fork builds on.
- **ReXGlue SDK:** [rexglue/rexglue-sdk](https://github.com/rexglue/rexglue-sdk) (Tom Clay and
  contributors), under the license in `extern/rexglue-sdk/LICENSE`.
- **Xenia:** the GPU/eDRAM emulation, the formats and much of the runtime come from Xenia.
- **60 FPS patch:** from `xenia-canary/game-patches` (author *illusion*), adapted to the recompiled
  game.
- **FidelityFX FSR / CAS:** AMD.
- **extract-xiso:** XboxDev.
- **Linux/Vulkan port and performance work in this fork:**
  [thefixinhixon](https://github.com/thefixinhixon), developed with Claude Code.

This project is for **educational and preservation purposes**. It is not affiliated with Microsoft,
Bandai Namco or Konami, and **it does not include any game code or assets**: you need your own legal
copy to use it.
