# AGENTS.md — Splatterhouse Recompiled (X360 → PC)

Native port of **Splatterhouse (2010) X360** via **static recompilation** with [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). In static recompilation the "guest stack" **IS** the native host stack: `ctx->lr` is useless (returns 0); the real guest caller shows up as a `splatterhouse!__imp__sub_*` frame (resolvable with `llvm-symbolizer --relative-address`).

## Linux port (active work, read first)

The Windows notes further down describe the original author's setup. This section is the current state of the **Linux/Vulkan** build and is where multiple agents coordinate.

### Status (2026-10-25)

- Builds and boots: intro, title, menus, story mode, cutscenes and first combat render with correct colours on Vulkan (`fsi` path, AMD RADV tested).
- **Intermittent freeze while loading a level (W1): fixed** on branch `agent-w1/stale-block` (not merged yet), see W1.
- Performance: the user reports slow runs and once "a ton of graphical glitches" (W4, W5). Pinning the game to one core avoids W1 but performance is unacceptable, so that is not a fix.
- All work so far is committed on the `linux-port` branches (main repo and `extern/rexglue-sdk`), not pushed. See "Multi-agent setup".

### Build (Linux)

```bash
cmake --preset linux-amd64-relwithdebinfo
cmake --build out/build/linux-amd64-relwithdebinfo -j$(nproc)    # ~15 min full, seconds incremental
```

- Clang 18+ (20 tested). The build stages `librexruntimerd.so`, `librexgpu-xenosrd.so`, `libTracyClientrd.so` next to the executable automatically.
- No `splatterhouse.toml` needed: Linux always uses Vulkan + `render_target_path_vulkan=fsi` + `vulkan_async_skip_incomplete_frames=false` (`src/splatterhouse_app.h`, `IsVulkanBackend()`).
- **Codegen changes** (anything under `extern/rexglue-sdk/src/codegen/`) need: `cmake --build out/build/linux-amd64-relwithdebinfo --target rexglue`, then `extern/rexglue-sdk/out/linux-amd64/rexgluerd codegen splatterhouse_manifest.toml --ignore-stamp` (~2.5 min), then the full build (~15 min).
- `game/` holds the extracted ISO (gitignored). The run directory needs an `assets -> game` symlink.

### Hard rules (the user is using this PC while agents work)

1. **Never test on the user's screens or speakers.** All runs go through `tools/linux/headless_run.sh` (Xvfb + software WSI, `--audio_mute`). Don't take desktop screenshots.
2. **Never open the user's real controller.** A killed test once left their pad rumbling nonstop. The harness passes `--input_backend=none` on the **command line** (the toml value is not honoured) and checks the log for `OnControllerDeviceAdded`; it warns if a real pad was opened. Input comes from the keyboard-emulated pad (`mnk_mode = true`; Return = Start, Space = A, arrows = right stick, Shift+arrows = D-pad).
3. **Never build into or kill a game you didn't start.** `out/build/linux-amd64-relwithdebinfo/` is the user's play directory and its `splatterhouse.toml` holds their saved F5 settings; don't overwrite it. Check `pgrep -a splatterhouse` first: rebuilding replaces the `.so` files under a running game and corrupts it. The harness always runs a **copy** in its own run directory.
4. Don't commit unless the user says so (see Conventions).

### Headless testing

```bash
# one run, default script (Start at 30/40 s, A every 10 s from 50 s) for 130 s:
DISPLAY_NUM=91 tools/linux/headless_run.sh mytag 130
# crash statistics for W1 (about 95 s in, during the first level load):
DISPLAY_NUM=91 RUNS=6 GDB=1 tools/linux/headless_run.sh w1 130
# custom key script: time:key pairs (xdotool key names)
DISPLAY_NUM=91 tools/linux/headless_run.sh menu 60 30:Return 40:Return 50:space
```

- Output (`$RUN_DIR/out/`): `<tag>_<n>.log` (full game log, rotation disabled), `<tag>_<n>.stdout.txt` (gdb output when `GDB=1`), and a 640x360 PNG at each key press plus `_end.png`. Look at the PNGs with an image viewer or the Read tool; don't screenshot the desktop.
- `GDB=1`: breaks on the first `Unhandled guest access violation`, prints the faulting backtrace plus all threads, then kills the game. Frames show up as `__imp__sub_XXXXXXXX` with `generated/` file:line.
- `EXTRA="--flag=value"` passes cvars (e.g. `--ignore_thread_affinities=false`, which raises W1 to ~80% and is the best reproduction). `PIN=2` runs on one core.
- Timeline with the default script: title ~25 s; Start at 30 s; auto-save notice, then Story mode, then mask selection (A presses); loading screen ~70 s; cutscene; gameplay ~100 s.
- Use a **different `DISPLAY_NUM` per agent** (91, 92, …), which also gives a separate run directory. Each run is heavy (software presentation copies every frame); don't run more than two at once on this machine.
- Gotchas:
  - ImageMagick `fx:mean.g` returns 0 on greyscale PNGs; use `identify -verbose`.
  - With log rotation on (the game's default) a crash flood pushes the start of the problem out of the log. The harness sets `--log_max_file_size_mb=2000`.
  - Each run maps a 4.8 GB `/dev/shm` file. It's unlinked at creation now (`memory_posix.cpp`), so killed runs don't leak; if `/dev/shm` ever fills up again, the game SIGBUSes on level load.

### Work board

Claim a task by putting your agent name and date in **Owner** before starting, and keep **Notes** current. One owner per task. Tasks marked *codegen* regenerate `generated/`; only one agent may do that at a time (see Multi-agent setup).

| ID | Task | Owner | Notes |
|---|---|---|---|
| W1 | **Level-load stale-block race** (blocker) | agent-w1 (2026-10-25) | **Fixed on branch `agent-w1/stale-block`** (not merged): stale XCTD completion signal makes `GetOverlappedResult` return while the read is pending; fix in `src/sh_overlapped_fix.cpp`. C1 passed 2026-10-25: 0/10 default, 0/10 `--ignore_thread_affinities=false` (was 3/3 crashing on the same branch without the fix). See W1 below. |
| W2 | Interim mitigation for W1 without the single-core cost | | Obsolete if the W1 fix is merged. Idea: find the guest threads in the NIF streaming path (I/O thread runs `sub_8290DB88`, reader runs `sub_82991488`/`sub_82991388`) and pin only those to one host core via `XThread::SetActiveCpu`/`set_affinity_mask`, leaving render/GPU threads free. Must not regress fps. Exit: C1 with fps within 10% of unpinned. |
| W3 | Rumble watchdog check on a real pad | user | `sh_rumble_timeout_ms` (SDL driver). Needs the user's controller; agents can't test it (rule 2). Expect `[sh-rumble]` in the log if a rumble gets stuck. |
| W4 | Linux performance measurement | | No fps counter on Vulkan yet (`sh_log_fps` is D3D12-only). Add a vblank/VdSwap rate log to the Vulkan path, measure headless at 720p/1080p, then profile (perf) a gameplay scene. |
| W5 | "Ton of graphical glitches" (1 user run) | | Unreproduced. Capture PNGs every few seconds through a full load+gameplay run and compare against Xenia; check `vulkan_async_skip_incomplete_frames` and pipeline-compile pop-in first. |
| W6 | Remaining missing private SDK patches | | See the Linux checkout note under "Local SDK patches": 11 (`[sh-uf]` continue mode) would speed up crash hunting; 24/16 are D3D12-only. |
| W7 | Commit/branch plan | user | Done 2026-10-25: `linux-port` branches, local only. Pushing is the user's call. |
| W8 | Port the level warp (`sh_warp`) | | The cvar exists (`src/main.cpp`, F5 row) but nothing reads it on this SDK: the original implementation was in the missing private `VdSwap_entry` patch (see "Dev tool: level warp"). Re-implement it port-side (e.g. a hook on a per-frame guest function calling `GameState::RequestLoadLevelByLevelName` `0x82795E48` with `*(0x82FFA3AC)`). Lets the harness hit every level load quickly (C2). |

### W1: level-load stale-block race (root cause found and fixed, 2026-10-25, agent-w1)

- **Symptom:** about 95 s into the default script (first level load) the game freezes with thousands of `Unhandled guest access violation: write of guest 0x00000000`. Sometimes it's preceded by `BaseHeap::Alloc failed to find contiguous range: ... request 20010000 bytes`; sometimes the variant is a null read (`0x4`/`0x8`). Rate before the fix: about 50% of runs, 80-100% with `--ignore_thread_affinities=false`, 0/4 with `PIN=2`.
- **Chain:** `.nif` loader `sub_8233B6A8` → `sub_8233E2A8` → `sub_822B3E98` → per-block loaders (e.g. `sub_8233D480`, a "count + 4-byte entries" deserializer) → the vector grows to 128M entries (`sub_8247FF20` asks for 512 MB) → alloc fails → null write in a loop.
- **Data symptom (measured earlier):** exactly one 64 KB block of the stream replaced by the previous 64 KB block (`bad[0x4E2000:+64K] == ref[0x4D2000:+64K]` for `g_lvl1_act1_sec_a_lobby.nif`), correct data again afterwards.
- **Root cause: a stale signal on the XCTD completion event makes `GetOverlappedResult` return while the read is still pending.**
  - The NIF I/O thread (`sub_8290DB88`) streams the file **uncompressed** (`[streamer+265] == 0`) with **2 descriptors** in async mode: `ReadFile` (`sub_82231F88`) into one descriptor (`sub_8290D7D8`), then `GetOverlappedResult` (`sub_829F0958`, via `sub_8290D890`) and push to the filled queue.
  - Its `OVERLAPPED.hEvent` is 0 (memset in `sub_8290D280`), so `GetOverlappedResult` waits on the handle itself. For XCTD files that handle is a **manual-reset `XEvent`** owned by the XCTD layer, not an `XFile`.
  - `ReadFile` → XCTD `sub_829F2370` sets the IOSB to `STATUS_PENDING`, `NtClearEvent(handle)`, and queues the request to the XCTD worker (`sub_829F3910` → `sub_829F3178`). The worker decompresses, writes the IOSB (status, then information), then `NtSetEvent(handle)`.
  - `GetOverlappedResult` only waits if the status is pending, and doesn't re-check after the wait. If the I/O thread sees request N already complete, skips the wait and issues request N+1 (`NtClearEvent`) **before** the worker's `NtSetEvent` for N lands, the event stays signalled during N+1. The wait returns at once, the status is still `0x103`, and `GetOverlappedResult` returns success with 0 bytes.
  - From there each completion is matched to the wrong request: the I/O thread hands the reader a descriptor whose buffer was never written, which still holds the previous 64 KB block (2 descriptors alternate). Measured with a cheap probe: every crashing run had `GetOverlappedResult ... status 103->103 got 0 ret 1` followed by a run of "read completed but buffer unchanged".
  - Why only on PC: on the console the worker isn't preempted by the I/O thread between writing the IOSB and `NtSetEvent` (guest thread priorities). On the host it is, most often when affinities are honoured and both threads share a core.
- **Fix:** `src/sh_overlapped_fix.cpp` overrides `sub_829F0958`. When the wait returns with the status still pending, it resets the event and calls the original again. The original re-reads the status first and the worker writes the status before signalling, so no completion is lost and nothing spins (2-7 stale signals per run, one reset each; log `[sh-ovl]` at powers of two). SDK side, only an unrelated bug found on the way: `XEvent::Initialize` never stored `manual_reset_` (wrong `Query`/`Save` results).
- **Ruled out** (don't redo):
  - The LfQu queues: Michael-Scott FIFO (`sub_822A3DD0`), no descriptor is ever double-pushed or popped from the wrong queue in crashing runs. All list ops use 64-bit counted pointers (`ldarx`/`stdcx.`), so value-CAS emulation is ABA-safe there.
  - Two threads inflating one `z_stream` at once (never seen in crashing runs). The crashing stream isn't zlib-compressed at all.
  - `RtlEnter/LeaveCriticalSection`, APC delivery (`ExecuteTrap` saves/restores context), `NtReadFile`/`NtReadFileScatter`, completion ports (unused by the game), zeroing on commit. All match Xenia.
  - Missing memory barriers: codegen now emits `atomic_thread_fence` for `sync`, compiler fences for `lwsync`/`eieio`, and an acquire load for `lwarx`/`ldarx`. Guest loads/stores were already `volatile`.
  - The XCTD cache reader-count race: it's real, and `src/sh_xctd_cache_fix.cpp` fixes it, but it is not this bug.
  - The XCTD compressed-block prefetch (`sub_829F40A8`/`sub_829F3FD8`): the SDK's `NtReadFile` completes synchronously, so the slot buffers are always filled before use.
  - File-object event semantics (`NtSetEvent`/`NtClearEvent` on an `XFile` handle are no-ops in the SDK, and `XFile::Read` signals the file even when the caller passed its own event): real differences from NT, but this path never waits on an `XFile`. Changing them made no difference.
  - User settings and the 1 kHz vblank (`vsync=false`).
- **Heisenbug warning:** heavy instrumentation hides it. `src/sh_stream_diag.cpp` was actually **compiled into the `linux-port` build** (mutex + hash map on every queue push/pop and inflate, a copy of every read); this branch takes it out of `SPLATTERHOUSE_SOURCES`.
- **Tools:** `src/sh_w1_probe.cpp` (not built): samples 8 words of the descriptor buffer when a read starts and completes, and logs `[w1] read completed but buffer unchanged` only for full-size reads. Hits at `pos 00000000` with no cascade are benign (same file read again into the same buffer). `src/sh_stream_diag.cpp` (not built): parser byte dumps (`SH_NIF_DUMP_DIR`, `SH_NIF_REF` + `PIN=2`) and the old heavy queue probes.

### Checkpoints

Run each checkpoint headless with the harness and record the result (date, commit or diff hash, numbers) in the work board Notes.

- **C0: Boot.** `RUNS=1` reaches the Story mode menu (`_50`/`_60` PNGs), no `guest-AV`, no `real-pads-opened`. Passing today.
- **C1: Level load is stable.** `RUNS=10 GDB=1` with the default script **and** `RUNS=10 GDB=1 EXTRA=--ignore_thread_affinities=false`: 0 crashes, and the `_end.png` shows gameplay. (Before the fix: ~5/10 and ~8/10. With the W1 fix, 2026-10-25: 0/10 and 0/10; all `_end.png` frames are past the load, in the level's opening cutscene.)
- **C2: Soak.** One 15-minute run with a key script that keeps playing (attack/move presses), no crash. Then load every level (needs W8).
- **C3: Performance.** fps measured (W4) at 1080p in the first combat arena, recorded here, with no regression against the previous checkpoint.
- **C4: User sign-off.** The user plays a session with their controller (also covers W3).

### Multi-agent setup

- **Branches:** the work lives on `linux-port` in both the main repo and `extern/rexglue-sdk` (committed 2026-10-25, **not pushed**). The SDK commit exists only in the local submodule, so worktrees must fetch it from there.
- **One worktree and one build directory per agent:**
  ```bash
  cd /mnt/sdb1/Games/splatterhouse-decomp
  git worktree add -b <agent>/<task> ../sh-<agent> linux-port
  cd ../sh-<agent>
  git config submodule.extern/rexglue-sdk.url /mnt/sdb1/Games/splatterhouse-decomp/extern/rexglue-sdk
  git -c protocol.file.allow=always submodule update --init extern/rexglue-sdk
  git -C extern/rexglue-sdk submodule update --init --recursive   # SDK third-party deps, from their public URLs
  ln -s /mnt/sdb1/Games/splatterhouse-decomp/game game
  cmake --preset linux-amd64-relwithdebinfo && cmake --build out/build/linux-amd64-relwithdebinfo -j$(nproc)
  ```
  Each worktree needs its own `generated/` (codegen ~2.5 min, run by the first build) and full build (~15 min). Don't share `generated/` or a build directory between agents: codegen rewrites all 208 files and ninja relinks under other agents.
- **SDK changes in a worktree:** commit them on a branch inside that worktree's `extern/rexglue-sdk`. To share them, fetch that branch into the main checkout's submodule (`git -C extern/rexglue-sdk fetch ../sh-<agent>/extern/rexglue-sdk <branch>`) and merge it into `linux-port` there, then commit the new submodule SHA in the main repo. Merge into `linux-port` only after the change passes C0 (and C1 if it touches W1).
- `game/` (assets) is shared read-only: symlink it into each worktree (`ln -s /mnt/sdb1/Games/splatterhouse-decomp/game game`).
- Pick a unique `DISPLAY_NUM` (and so a unique `RUN_DIR`) per agent. Run at most two headless games at once in total.
- **Codegen changes are serialized:** announce them on the work board before touching `extern/rexglue-sdk/src/codegen/` or `config/codegen.toml`, because they change every generated file and conflict with everyone's builds.
- **Hand-off:** when stopping, update the work board row and add any new facts to the W1 notes (or a new section). Keep `Ruled out` lists current so others don't repeat experiments.

## Environment (Windows)

- VS 2026 "VS 18" Community + Ninja + LLVM 22.1.8 (`C:\Program Files\LLVM\bin`). Python 3.9.
- SDK submodule: `extern/rexglue-sdk` (nightly `0c7b01a`).
- Required PATH:
  - `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`
  - `...\CMake\Ninja` (`ninja.exe`)

## Build

```powershell
cmake --preset win-amd64-debug
cmake --build out\build\win-amd64-debug -j 4            # everything (exe + DLL)
# specific targets:
#   rexruntime  -> rexruntimed.dll (runtime; fixed SDK output)
#   rexglue     -> rexglued.exe   (codegen CLI; re-embeds the inja templates)
#   splatterhouse -> the .exe
```

- The DLL is **always** emitted to `extern\rexglue-sdk\out\win-amd64\rexruntimed.dll` (fixed output config in the SDK's CMake), even in Debug.
- The exe loads the DLL from **its own directory** → copy it after every `rexruntime` rebuild:

```powershell
Copy-Item extern\rexglue-sdk\out\win-amd64\rexruntimed.dll out\build\win-amd64-debug\rexruntimed.dll -Force
Copy-Item extern\rexglue-sdk\out\win-amd64\rexruntimed.pdb out\build\win-amd64-debug\rexruntimed.pdb -Force
```

- The exe build **auto-runs codegen** (`rexglued.exe codegen splatterhouse_manifest.toml`) when the stamp is stale. Full regeneration (208 files): ~800 s. To regenerate by hand:
  ```powershell
  extern\rexglue-sdk\out\win-amd64\rexglued.exe codegen splatterhouse_manifest.toml --ignore-stamp
  ```
- `generated/` is **gitignored**: generated files are lost if not regenerated. Any manual patch in `generated/*.cpp` is lost on regeneration. **Warning:** hand-editing a file in `generated/` updates its mtime and makes ninja **re-run codegen (~800 s)**; if it was only cosmetic, skip it with `(Get-Item generated\codegen.build.stamp).LastWriteTime = Get-Date`. That's why patches go in `src/` (weak override), not in `generated/`.

> **Performance (important):** `win-amd64-debug` compiles the 208 recompiled files **unoptimized** → the *command processor* (PM4 decoding) and guest code become a bottleneck (low FPS + stutter). To **play/measure** use the **`win-amd64-relwithdebinfo`** preset:
> ```powershell
> cmake --preset win-amd64-relwithdebinfo   # (from a VS environment: rc.exe on PATH)
> cmake --build out\build\win-amd64-relwithdebinfo --target splatterhouse -j 6
> ```
> Output in `out\build\win-amd64-relwithdebinfo\` (DLLs `*rd.dll`); create the `assets`→`game` junction there and copy `splatterhouse.toml`. The Debug and Release exes share `generated/`.

> **FSR (FidelityFX) — must be enabled at configure time:** the D3D12 presenter ships EASU/RCAS/CAS + temporal FSR2/3, but it is **compiled out** (`REXGLUE_ENABLE_FIDELITYFX=OFF`). To get it:
> ```powershell
> cmake --preset win-amd64-relwithdebinfo -DREXGLUE_ENABLE_FIDELITYFX=ON
> ```
> On Win32 it does **not** require the Vulkan SDK; it does a FetchContent of the `rexglue/FidelityFX-SDK` fork and auto-compiles shaders (DXC). It rebuilds `rexruntimerd.dll` and produces **`amd_fidelityfx_dx12drel.dll`** (in `bin/`), which must be copied **next to the exe**. With FFX ON, `present_effect` accepts `bilinear` (default), `cas`, `fsr`, `fsr2`, `fsr3`. The **Debug** preset keeps FFX **OFF** (if you want it there, pass the same `-D`).
> **Watch out:** with FFX ON, `rexruntimerd.dll` depends on `amd_fidelityfx_dx12drel.dll`; it must **also** be copied to `extern\rexglue-sdk\out\win-amd64\` (next to the runtime and `rexgluerd.exe`, the codegen), or **codegen fails with `0xC0000135` (DLL not found)**.

## End-user installation

`install.bat` (in the root) + **`tools\extract-xiso.exe`**: the user **drags the Splatterhouse ISO** onto `install.bat`. An Xbox 360 ISO **can't be mounted** (it's a video disc, not standard UDF/ISO9660), so it is extracted with **`extract-xiso -x -s`** (`tools\extract-xiso.exe`, [xboxdev/extract-xiso](https://github.com/xboxdev/extract-xiso)); `-s` skips `$SystemUpdate`. It then moves the contents to **`assets\`** next to the exe and **decrypts** the XEX with **`tools\xextool.exe -e u -c b -o assets\default.dec.xex assets\default.xex`** (xorloser's XexTool v6.3). `SplatterhouseApp::OnLoadXexImage` uses `game:\default.dec.xex` if it exists in `assets\` (otherwise the retail `default.xex`). **Without `splatterhouse.toml` the GPU isn't loaded** (empty `gpu_plugin` → black screen); the installer **creates a default `splatterhouse.toml`** (`gpu_plugin="xenos"`, `render_target_path_d3d12="rov"`, `present_effect="fsr"`, `vsync=false`, `d3d12_present_frame_limiter[_fps]=60`) if it doesn't exist, and `SplatterhouseApp::OnPreSetup` also forces `gpu_plugin="xenos"`+ROV+FSR as a safety net. The default **internal resolution** depends on the GPU (AMD/Intel → 1280x720, others → 1920x1080; see issue #3): on the ROV path the cost scales with pixels and on AMD/Intel it is much more expensive. After extracting, the installer verifies that `default.xex` + `data\` exist. Then the user just runs `splatterhouse.exe`.
- **Precompiled shader cache** (avoids first-playthrough stutter): the SDK stores pipeline descriptions and translated shaders in `%MyDocuments%\splatterhouse\cache\shaders\shareable\{4E4D07F0.rov.d3d12.xpso, 4E4D07F0.xsh}` and loads them on startup (the SDK marks them as *"mostly shareable across devices"*). To ship it: play once to generate it, **`tools\pack_shadercache.bat`** copies the cache to **`shadercache\`** in the release; `install.bat` copies them to the user's folder. If a GPU/driver has problems, deleting the cache regenerates it.
- **Shader/PSO compilation**: the SDK **already** does Xenon→DXBC translation and PSO creation on **background threads**, **skips the draw** if the pipeline isn't ready (`async_shader_compilation=true`) and **pre-creates in parallel** all the cached pipelines on startup (log `Created N graphics pipelines ... in M ms`). Measured (RTX 5060 Ti): 401 PSOs in ~530 ms; **~2–11 ms per PSO** → the cost is "hundreds in a row", so the lever is a **complete cache**, not more async. Diagnostics: cvar **`sh_pso_log`** (log `[sh-pso] created VS… PS… in X ms` + `draw skipped`). Analysis in `docs/rov-performance.md` §3c.
- **Decrypting the XEX isn't required**: the runtime loads `game:\default.xex` (retail, `XEX2` with the encrypted flag) and decrypts it itself (`src/system/xex_module.cpp`, `xe_xex2_retail_key`). That's why the manifest uses `default.dec.xex` only for build/codegen.

## Running

```powershell
# cwd REQUIRED: out\build\win-amd64-debug
out\build\win-amd64-debug\splatterhouse.exe     # log -> splatterhouse.log
```

- cvars in `splatterhouse.toml`: `gpu_plugin="xenos"` required, `log_level=trace`.
- Junction `assets` → `game`. The XEX (`game/default.dec.xex`) is **LZX-compressed**.
- Instrumentation variables (see section): `REX_DEBUG_CS`, `REX_DEBUG_CS_LEVEL`, `REX_TRACE_LO`, `REX_TRACE_HI`.

## Local SDK patches (preserve / report upstream)

Full diff: `C:\Users\josel\AppData\Local\Temp\opencode\sdk_patches.diff` (regenerate with `git -C extern/rexglue-sdk diff > ...`).

> **Linux checkout (2026-10-25):** the submodule in this repo is clean upstream `0c7b01a`; the patches below lived in the original author's private SDK copy and are **not** in it. Re-applied here so far (`git -C extern/rexglue-sdk diff`): **5** (`dataPointerScan`, only fills gaps outside known functions), **12** (`vmaxuw`), **14** (Xctd log one-shot DEBUG), **17** (`SaveConfig` merge + `.bak`; command-line/env overrides are not persisted; `Source::kConfig` no longer marks pending restart), **22** (SDL rumble watchdog, `sh_rumble_timeout_ms`), **18** (60 Hz guest vblank with `vsync=false`, cvar `sh_no_vsync_vblank_fast`), plus the Vulkan `exp_adjust` fix (see the Vulkan note in item 23). Added on Linux:
- `src/core/memory_posix.cpp`: `shm_unlink` right after `shm_open`, so killed runs don't leak 4.8 GB files in `/dev/shm`. Leaked files filled `/dev/shm` and caused SIGBUS on level load.
- `src/system/xmemory.cpp`: `BaseHeap::Alloc failed` now logs the request size and the free and largest free page runs.
- `src/codegen/builders/system.cpp` + `helpers.h`: `sync` → `atomic_thread_fence(seq_cst)`, `lwsync`/`eieio` → compiler fence, `lwarx`/`ldarx` → atomic acquire load. This is a codegen change, so regenerate.
- Port side, `src/sh_xctd_cache_fix.cpp`: XCTD cache writer waits for the reader count to reach 0.
- Port side, `src/sh_overlapped_fix.cpp`: `GetOverlappedResult` ignores a stale signal while the request is still pending (W1 fix).
- `src/system/xevent.cpp`: `XEvent::Initialize` stores `manual_reset_`.

**6** is replaced by `non_argument_as_local = false` in `config/codegen.toml`. **23** (`gpu_backend`) is replaced by `IsVulkanBackend()` returning true off Windows. Debug/D3D12-only patches (4, 7–11, 13, 15, 16, 21, 24) are not needed for Linux. `CMakeLists.txt` stages `librexruntime`/`librexgpu-xenos`/`libTracyClient` next to the executable on every Linux build; **don't build while the game is running** (replacing its `.so` files corrupts the running process).

Originals (2):
1. `include/rex/platform/exceptions.h` — SEH tweak.
2. `resources/templates/codegen/pch_h.inja` — **embedded template**: after editing it, **rebuild the `rexglue` target** (re-embeds) and regenerate.

> **Removed (it was the Scaleform blocker bug):** the old patch 3 in `src/kernel/xboxkrnl/xboxkrnl_io_info.cpp` made `NtQueryInformationFile(XFileXctdCompressionInformation)` return **success with a zeroed struct** ("not compressed"). The engine decides whether to decompress XCTD based on that query (`sub_829F5600` returns `2` only if the query **fails**; with success+0 it returns `0` and skips decompression) → the XCompress `.gfx`/`.dds` files (`0F F5 12 ED`) reached Scaleform raw and parsing failed. It has been **reverted to HEAD** (the SDK already returns `X_STATUS_INVALID_PARAMETER`, same as Xenia/upstream), which restores detection by the `0x0FF512ED` magic. It must not be re-added.

Added in this session:
4. `src/kernel/xboxkrnl/xboxkrnl_rtl.cpp` — CS instrumentation `[sh-diag-instr]` (ring buffer, threshold, frames with module+RVA) + softened `assert_true` in Debug (don't abort with the MSVC dialog). **IMPORTANT:** the old `[sh-fix]` (retry loop with "REPAIR" that stole the lock when it saw `owner==0`) has been **removed**: it was the cause of the thousands of `RtlLeaveCriticalSection MISMATCH` and of memory corruption (intermittent AVs). The **blocking wait from Xenia/upstream** is used now. The original ABBA that motivated it was already solved by the r12 codegen fix (`__finally`).
5. `src/codegen/phase_discover.cpp` — **`dataPointerScan`**: registers functions pointed to by dwords in data sections that fall in a code region and are **not** inside an already-known function. Complements the `VTableScanner` (MSVC RTTI only; UE3 doesn't use it).
6. `src/codegen/builders/context.cpp` + `src/codegen/function_graph.cpp` — **non-ABI register propagation fix** (`r0/r2/r11/r12`): loads them from `ctx` on entry and spills them to `ctx` before each call. Fixes the `__finally` bug (r12 = establisher frame) and `__chkstk`.
7. `src/core/seh_win.cpp` — `[sh-av]`: logs `RIP`+module+RVA+fault address on every access violation.
8. `src/kernel/xboxkrnl/xboxkrnl_io.cpp` — `[sh-io]`: caller backtrace for every `.gfx`/`.dds` open + one-shot dump of the guest image to `guest_image.bin`. **Now opt-in** (`REX_SH_IO=1`, it used to be always on): 22 lines per asset to the unbuffered console stall streaming (hitches with ~0 % CPU).
9. `include/.../pch_h.inja` — `[sh-trace]`: trace (one-shot per function) of entries in `[REX_TRACE_LO, REX_TRACE_HI]`.
10. `src/system/xthread.cpp` — logging of `guest_object` in `XThread::Execute`.
11. `src/system/function_dispatcher.cpp` — `[sh-uf]`: caller backtrace for every `Call to invalid or unregistered function` (to find the table that computes the target); with `REX_SH_UF_CONTINUE=1` it logs and continues instead of aborting (enumeration mode).
12. `src/codegen/builders/vector.cpp` + `builders.h` + `src/codegen/instruction_dispatch.cpp` — implements **`vmaxuw`** (`build_vmaxuw` → `simde_mm_max_epu32`, it was `REX_UNIMPLEMENTED`). It's a codegen instruction, so after touching it you must **rebuild the `rexglue` target** (rexglued.exe) and regenerate (codegen no longer warns about `vmaxuw`).
13. `src/graphics/pipeline/shader/translator.cpp` — `[sh-gpu]`: softens the `GatherVertexFetchInformation` asserts (`assert_not_zero(stride)` and the inconsistent-stride one) to a `warn`; there are legitimate `vfetch`es with stride 0 and in Release the assert was compiled out. It is built into **`rexgpu-xenosd.dll`** (GPU plugin), not `rexruntimed.dll`. The `warn`s are now **opt-in** (`REX_SH_GPU=1`) to avoid flooding the console during gameplay.
14. `src/kernel/xboxkrnl/xboxkrnl_io_info.cpp` — `XFileXctdCompressionInformation` still returns `X_STATUS_INVALID_PARAMETER` (required for the engine to decompress XCTD), but the log goes from `ERROR` (flooding) to a one-shot `DEBUG`.
15. `src/graphics/d3d12/pipeline_cache.cpp` — cvar **`sh_pso_async`** (default on): `PipelineCache::EndSubmission()` no longer creates pipelines on the present thread or waits for the background threads (that was the stall of up to 673 ms); it only wakes them. With off, original behavior.
16. `src/graphics/d3d12/command_processor.cpp` + `src/ui/d3d12/d3d12_presenter.cpp` — **`[sh-frame]`** instrumentation (cvars `sh_frame_log`/`sh_frame_log_ms`): timing scopes in the present/submission phases (see the diagnosis in the performance section). Opt-in.
17. `src/core/cvar.cpp` — **`SaveConfig` with merge**: it used to serialize only flags != default and **rewrite the whole file**, deleting keys not registered at that moment (e.g. GPU plugin cvars or hand-written ones) → the F5 menu "didn't save" (lost previous options). Now it parses the existing toml, updates/inserts the current flags and **preserves the rest**. Also (2026-10): it **copies the previous file to `<config>.bak`** before rewriting (toml++ doesn't keep comments), and **`Source::kConfig` no longer marks "pending restart"** (toml values are applied before initialization; before, the F5 footer said "Restart to apply: video_mode_*" right after startup).
18. `src/graphics/graphics_system.cpp` — **guest vblank always at 60 Hz** (before, with `vsync=false` it ran at ~1 kHz and the game ran sped up). Cvar `sh_no_vsync_vblank_fast` restores the old mode.
19. `src/system/xam/content_manager.cpp` + `include/.../content_manager.h` — **`InstallContentFromDirectory`**: scans a folder for STFS packages (`CON `/`LIVE`/`PIRS`) and installs them with the existing `InstallContent` (extraction + `.header` + license mask). No longer required: the port now does the scan itself with the public `InstallContent`/`ContentExists` (`SplatterhouseApp::InstallStfsPackagesFrom`), so it also builds against upstream SDK `0c7b01a`.
20. `src/splatterhouse_app.h` — **`InstallDlcPackages()`** (in `OnPostLoadXexImage`): installs the DLC the user drops in `Documents\splatterhouse\<title>\00000002`, `…\0000000000000000\<title>\00000002` or any `<xuid>\<title>\00000002`.
21. `src/kernel/xam/xam_content.cpp` — **`[sh-dlc]`** opt-in (`REX_SH_DLC=1`): logs which content the game enumerates/opens (`XamContentCreateEnumerator`/`XamContentCreate`).
22. `src/input/sdl/sdl_input_driver.{h,cpp}` + `src/input/xinput/xinput_input_driver.cpp` — **rumble watchdog** (see "Solved: controller rumble that doesn't stop"). Cvar **`sh_rumble_timeout_ms`** (10 s, 0 = off).
23. `src/ui/rex_app.cpp` — **GPU backend selection**: new cvar **`gpu_backend`** (`any`/`d3d12`/`vulkan`, default `any`) passed to `LoadGpuPlugin`. With `REXGLUE_USE_VULKAN=ON` (CMake) the SDK's **Vulkan** backend is enabled (full Xenos renderer, Xenia port, already in the tree). The port (`src/splatterhouse_app.h`) sets the render target path in **`OnFinalizePaths`** (Vulkan→`fsi`, D3D12→`rov`): it's the only hook that runs with the GPU plugin already loaded (cvars registered) and before GPU init; in `OnPreSetup` it would be a no-op.
    - **Vulkan backend status (2026-10): it works and is stable (0 AV/MISMATCH, 60 VdSwap/s).** The old "Vulkan renders GLOBALLY DARKER than D3D12" bug is **fixed** (SDK `src/graphics/pipeline/shader/spirv_translator_fetch.cpp`): the SPIR-V translator took the texture fetch **exponent bias** (`exp_adjust`, bits 13:18) from fetch-constant **word 4** (where those bits are part of `lod_bias`) instead of **word 3**, so every texture fetch was scaled by a garbage power of two (dark image, green often lost entirely). The DXBC translator always read word 3, which is why D3D12 looked right. It was **not** gamma, the swapchain or the resolve: verified by forcing pixel shaders / gamma ramp / texture fetches to white. Linux always uses Vulkan (`IsVulkanBackend()` returns true off Windows) with `render_target_path_vulkan='fsi'` (+ `vulkan_async_skip_incomplete_frames=false` to avoid skipping whole frames while pipelines compile; the D3D12 equivalent is skipping only draws). `present_effect='fsr'` doesn't apply to Vulkan (FidelityFX is built for D3D12).
24. `src/graphics/d3d12/command_processor.cpp` — **`[sh-render]`** instrumentation (cvars `sh_render_profile`, `sh_render_profile_interval_s`, `sh_render_profile_top`): render workload profile per interval (draws/frame, VS:PS pairs by ucode hash, primitive type, and "passes" by format/base/pitch/msaa/depth with a pixel proxy from the scissor). Opt-in; used to characterize combat (see `docs/rov-performance.md` §3d): in combat ~1350 draws/frame, two dominant passes (scene + depth/shadows). Without `fsi`, Vulkan's default path (host render targets) leaves the scene almost black, same as RTV on D3D12.

(`thirdparty/libmspack` is a pre-existing dirty submodule.)

## Status and diagnosis

The original `main`→crash chain is solved; the **UI/Scaleform GFx** init (XCTD atlas) and **critical section synchronization** **too**. The game loads the full atlas, starts the UI, reaches the **cutscene** and stays up **>2.5 min** without AV, FATAL or `MISMATCH`.

### Solved

1. **CS ABBA deadlock** (`0x40000618`): it was a **ratchet from a lost `RtlLeave`**, caused by a **codegen bug**: the `__finally` handler `sub_822379F8` read the lock flag from `[ctx.r31+96]` with `ctx.r31 = r12 - 320`, but `r12` (establisher frame, non-ABI) **wasn't propagated to `ctx`** → it read garbage → skipped `RtlLeaveCriticalSection`. Fixed in the SDK (item 6). Verified: 0 ratchet flushes, no `lock_count=90`.
2. **Functions reached only through indirect calls** not discovered (`Call to invalid or unregistered function`): solved with `dataPointerScan` (item 5). The `[functions]` config in `config/codegen.toml` is still available for one-off cases.
3. **Non-ABI registers `r0/r2/r11/r12`** not propagated across calls (`__chkstk` took size 0): fixed (item 6). Static audit (read-before-write) → 0 residual cases in those registers.
4. **CS `assert_true` in Debug** (`xboxkrnl_rtl.cpp:599`, `cs->owning_thread == 0`): modal MSVC dialog when starting the UI. It was a lock bookkeeping race (in Release the assert is compiled out and the code steals the lock). Softened in the SDK (item 4).
5. **`Call to invalid or unregistered function`** after the storage selector (`XamShowDeviceSelectorUI`): codegen analysis **merged contiguous functions** and the secondary entries (thunks `lwz;lwz;mtctr;bctr` / `b tgt` with padding) became unreachable. **63 addresses** were added to `[functions]` in `config/codegen.toml` (detected with `C:\Users\josel\AppData\Local\Temp\opencode\scan_missing3.py`; list in `config/missing_functions.txt`). Codegen warns about 3 `bdz ... branches outside function` (0x8235B878/0x8235B888) — check whether that area is used.
6. **`REX_UNIMPLEMENTED: vmaxuw`** (`splatterhouse_recomp.96.cpp:17341` and `recomp.137.cpp:18886`): unsigned max VMX instruction not implemented in codegen. Implemented `build_vmaxuw` (item 12). **Unrelated to the CS `MISMATCH`** (independent problems).
7. **Vertex fetch assert in the cutscene** (`translator.cpp:411`, `assert_not_zero(fetch_instr.attributes.stride)`): softened (item 13). Runs in `rexgpu-xenosd.dll`.
8. **`RtlLeaveCriticalSection MISMATCH cs=40651480` + intermittent AVs**: **root cause** the `[sh-fix]` "REPAIR" loop (item 4). It stole the lock from a legitimate owner by sampling the transient `owner=0`/`lock_count` window of `RtlLeaveCriticalSection`; the owner then desynchronized `lock_count`/`owning_thread` → cascading MISMATCH and corruption (AV reading garbage pointers 0x5F7264 / 0x0700002C). Reverted to Xenia's blocking wait: **0 MISMATCH, 0 AV**.
9. **`Call to invalid or unregistered function` in the UI callback table** (`0x825CB158` on reaching the menu/journal, `0x825CB170` in gameplay): they are virtual dispatch thunks (`lwz;mr;lwz;lwz;mtctr;bctr`) merged by the analysis. Added the **full contiguous table `0x825CB108..0x825CB1B8`** to `[functions]` in `config/codegen.toml`. Verified: 3 runs of 60 s without AV.

### Solved: 60 FPS (official patch) + FSR

- The game was locked at **30 FPS**. With `vsync=false` it reaches **300-500 FPS** → **there is no hard cap**: the 30 was **GPU-bound** (ROV path) + vblank-synced presentation. CPU ~2%.
- **Official Xenia patch** (title `4E4D07F0`, `xenia-canary/game-patches`, author *illusion*), applied **durably** (NOT touching `generated/`):
  - **Data**: floats `0x82F8FF58/5C` (1/15 and 1/30) → **1/60**. In `src/splatterhouse_app.h::ApplyGuestMemoryPatches` (`OnPostLoadXexImage`), enabled by the cvar **`sh_60fps`** (on by default; defined in `src/main.cpp`).
  - **Instruction** `0x8247A4AC`: `stb r30,202(r31)` → `stb r11,202(r31)`. Replicated with a **strong override of the codegen's weak function** in `src/sh_60fps_hook.cpp` (defines `sub_8247A368`, calls `__imp__sub_8247A368` and sets `[obj+202]=arg`); added to `CMakeLists.txt`.
  - **Both are required**: floats alone → still 30.
- **FSR** enabled (see Build). With `present_effect="fsr"` + `video_mode 1920x1080` + fullscreen + vsync, gameplay holds **60 FPS** (steady state 100% ≥55; the 30s are loading/intro). `bilinear` gives similar FPS (FSR is mostly about upscaling **quality**); FSR2/3 are **experimental** temporal modes (synthesized motion/depth, may fall back to spatial FSR). **DLSS/XeSS aren't viable** (UE3 doesn't expose motion vectors).
- Diagnostics: cvar **`sh_log_fps`** (log `[sh-fps] guest N FPS` every 2 s) and log `[sh-fps] vblank pacing`, in the GPU plugin (`rexgpu-xenosrd.dll`).
- **VSync OFF + present frame limiter (default)**: the port uses **`vsync=false` + `d3d12_present_frame_limiter=true` + `d3d12_present_frame_limiter_fps=60`**. Note: the limiter **alone** doesn't prevent 60/30 — a frame that exceeds 16.6 ms still waits for the next present vblank and stays at 30 (see the *Tearing* item: that's why **adaptive tearing** exists). **Gotcha**: `vsync` and `d3d12_present_frame_limiter*` are registered by the **GPU plugin**, which loads **after** `OnPreSetup` → there `SetFlagByName` is a no-op. The defaults are set in **`OnPostSetup`** (`src/splatterhouse_app.h`), which runs with the plugin already loaded (and the vblank worker reads `vsync` live), or directly in the installer's `splatterhouse.toml`. The `[sh-perf]`/`[sh-fps] vblank pacing` logs confirm the values.
- **Tearing**: with `vsync=false` the present uses `Present(0, ...)`. Strategy (2026-10):
  - **`d3d12_present_adaptive_tearing=true`** (default): the swapchain is always created with `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` (if DXGI supports it) and the presenter measures how long the frame takes to be ready (from when the previous present returned until **before** waiting on the limiter). If it nears the budget (>0.95 × 16.6 ms) it passes `DXGI_PRESENT_ALLOW_TEARING` (doesn't block → the pace drops gradually 58, 52, 45…); if there's headroom (<0.80 ×) it removes it (vblank-synced present → **no tearing** at 60). Hysteresis to avoid flip-flopping. It's needed because with a vblank-synced present a 17 ms frame waits 33 ms and the game **gets stuck at 30** (the limiter only spreads presents within that grid).
  - **`d3d12_allow_variable_refresh_rate_and_tearing=true`** ("always (VRR)"): passes `ALLOW_TEARING` on every frame, unsynced. That's the right choice with **G-Sync/FreeSync** (no tearing and minimal latency), but on a fixed-refresh panel it **always tears** (a line was visible at the top because presents land right after the vblank). That's why it is **no longer the default**.
  - Both `false` ("off (60/30)"): old behavior.
  - F5 **Tearing / VRR** row with those 3 states (**no restart**: the swapchain is already created with the flag); setting it in the toml is still the most convenient.
- **Speed-up ("fast motion") — SOLVED**: the **`vsync` cvar was overloaded** and controlled both the host present **and** the cadence of the **guest vblank**. With `vsync=false` the vblank worker emitted vblanks at **~1000 Hz** (`guest_tick_frequency/1000`) instead of 60 Hz → games that tie time to the vblank ran **sped up (×~16)**. Fix in `graphics_system.cpp`: the guest vblank **always** runs at the video mode rate (60 Hz), regardless of `vsync`. Cvar **`sh_no_vsync_vblank_fast`** (default off) restores the old behavior. It was **not** related to `sh_60fps`.
- **Definitive measurement (gameplay forced with `sh_warp=lvl2_shanty`, run >2 min)**: with `vsync=false`+limiter the guest **presents at ~60 VdSwap/s** steadily (`[sh-swap]`, instrumentation in `VdSwap`). CPU **1–5 % of 12 cores**; **GPU 3D 60–89 %**. Conclusion: **the port is GPU-bound (ROV path)**, not CPU-bound → spreading work across cores doesn't help. The "hitches" are moments when a frame **exceeds 16.6 ms** (GPU) and drops to **30** (half vblank/limiter). Real levers: lower `video_mode_*` (F5 preset), make ROV cheaper, or **`fullscreen=true`** (`sh_warp`/starting windowed gives fewer FPS).
- Note: the cvar `video_mode_refresh_rate` (double with `.allowed`) **rejects `"120.0"`** — use `"120"`.

### Added: in-game graphics screen (F5)

- **The real menu can't be edited**: the UI is **Scaleform GFx** and the `.gfx` files are compiled Flash + **XCompress** (`0F F5 12 ED`), without `.fla` sources; the SDK has no offline XCompress decompressor (`XMemDecompress` is implemented by the guest engine). The layout lives inside the `.gfx`.
- **Solution (option A)**: our own **full-screen game-styled** screen (dark background + red), NOT a floating window. `src/graphics_menu.h` (`GraphicsMenuDialog : rex::ui::ImGuiDialog`), opened/closed by the keybind **`bind_graphics`** (F5 by default). Integrated in `src/splatterhouse_app.h::OnCreateDialogs`. The dialog registers itself with the `ImGuiDrawer` and self-deletes on `Close()`; the global pointer `g_graphics_menu` avoids dangling.
- **F5 (important)**: the keybind **didn't fire** because the `ImGuiDrawer` registers as an input listener with **z-order 64** and consumes the key first. Solution: our own **`WindowInputListener` with z-order 128** (`SplatterhouseApp::GraphicsKeyListener`, added in `OnCreateDialogs` with `window()->AddInputListener(..., 128)` and removed in `OnShutdown`) that handles F5 before ImGui. `ReXApp::OnKeyDown` is **private** → it can't be overridden.
- **Menu keyboard (don't use `ImGui::IsKeyPressed`)**: the menu **doesn't read keys from ImGui**. `SplatterhouseApp::GraphicsKeyListener` (z-order 128, the first to see them) calls `GraphicsMenuDialog::PushKey`/`ReleaseKey` and the menu consumes the queue in `OnDraw`; with the menu open the keys are marked `handled` so the game doesn't see them. Reason: the `ImGuiDrawer` does call `io.AddKeyEvent`, but ImGui's keyboard path isn't reliable here (it depends on window focus and on a frame actually being drawn). Only the mouse still comes from ImGui (`io.MouseWheel`).
- **Hover must not pin the selection** (bug): if `selected_ = hovered_` is reassigned every frame, the row under the cursor stays pinned and **keyboard and wheel do nothing**; scrolling only seemed to work by moving the mouse to the end of the list (where the row slips out from under the cursor and stops overriding the selection). Now hover only moves the selection **if the mouse actually moved** (`last_mouse_`/`hovered_`), click selects+changes, and the wheel moves the selection with the mouse inside the panel (`mouse_in_panel`).
- **Options** (read/write cvars; `S` = save to `splatterhouse.toml`, Esc/F5 = close), grouped into sections with titles (`Row::header`, not selectable) and with **scrolling** (the list doesn't fit; `first_row_`/`visible_rows_` keep the selection in view, mouse wheel or arrows to move):
  - **Interface**: **Menu language** (`sh_menu_language`: english/spanish). Translates the F5 screen itself at draw time (`GraphicsMenuDialog::L()`: English strings are the keys of a Spanish lookup table, so any new menu string needs an entry there); no restart.
  - **Image**: Preset (Low/Medium/High/Ultra; sets `video_mode_*`, `texture_cache_memory_limit_soft` and `present_effect="fsr"`; keeps the aspect and the label is derived from the config), Internal resolution (640x360…**3440x1440**, includes 2560x1080), **Internal aspect** (`sh_aspect`: auto/16:9/21:9/32:9), Draw scale (`resolution_scale`, capped at 2560 px wide because of the ROV hang), Render path (`auto`/`rtv`/`rov`).
  - **Upscaling and presentation**: Presentation (`bilinear`/`cas`/`fsr`/`fsr2`/`fsr3`), **FSR quality** (`present_fsr_quality_mode`: auto/nativeaa/quality/balanced/performance/ultra_performance), Sharpness (FSR), **Presentation antialiasing** (`swap_post_effect`: none/fxaa/fxaa_extreme), Dither, **Letterbox** (`present_letterbox`).
  - **Display**: Fullscreen (`fullscreen`), **Window size** (`window_width`/`window_height`, 0 = automatic), **Refresh rate** (`video_mode_refresh_rate`, only values from its `.allowed`), VSync, FPS limit, **Tearing/VRR** (`d3d12_allow_variable_refresh_rate_and_tearing`).
  - **Quality**: **Texture cache (MB)** (`texture_cache_memory_limit_soft`), **Native 2x MSAA** (`native_2x_msaa`), **Anisotropic filtering** (`anisotropic_override`: auto/-1, off/0, 2/4/8/16).
  - **Performance**: Background shaders (`async_shader_compilation`), Non-blocking PSO (`sh_pso_async`), **PSO creation threads** (`d3d12_pipeline_creation_threads`: auto/-1, 2/4/8/16).
  - **Game**: 60 FPS patch (`sh_60fps`), Unlock levels, **Max level** (`sh_unlock_levels_max`), Game language (`sh_language`).
  - **Input and audio**: **Input backend** (`input_backend`: sdl/xinput), **Max rumble (ms)** (`sh_rumble_timeout_ms`: 0 = off), Mute (`audio_mute`).
  - **Development**: Level warp (dev), Quit game.
  - Rows whose cvar **isn't registered** are **hidden** (`Available()` → `rex::cvar::GetFlagInfo(name)`): some options depend on how the runtime was built (FSR quality only exists with `REXGLUE_ENABLE_FIDELITYFX=ON`, and in `src/` there's an `#else` that only declares `present_effect="bilinear"`) or on the GPU plugin being loaded. Without that filter the menu would offer values that `SetFlagByName` then rejects (`ValidateConstraints`).
  - `cycle_string` **respects `.allowed()`**: the written values must appear in the cvar's `.allowed` list or the write is rejected (e.g. `video_mode_refresh_rate` doesn't accept `"120.0"`, it has to be `"120"`).
  - The footer shows the **cvars pending restart** (`rex::cvar::GetPendingRestartFlags()`) and the visible range (`[X-Y of N]`). Changing any row other than Preset switches the Preset to "Custom".
- **Quit game**: last row, in red, **two-step** so it isn't closed by mistake — the first change arms it ("confirm (Yes/No)") and the second (right/Enter/click) closes; left disarms it. It saves `splatterhouse.toml` if there were unsaved changes and calls `window()->RequestClose()` (`SplatterhouseApp::QuitGame`). Note: `Close()` deletes the dialog, so the callback is copied to a local **before** calling it.
- **Quit game ≠ `RequestDeferredQuit` (bug)**: `app_context().RequestDeferredQuit()` **crashes** here. That path leaves the message loop → `ReXApp::OnDestroy()`, which destroys the `ImGuiDrawer` and the presenter **while the guest thread is still emulating**, and then does `module_thread_.join()` on a thread that never ends (the emulated game doesn't finish on its own) → crash/hang. The SDK itself avoids it: `ReXApp::OnClosing` (`src/ui/rex_app.cpp:498`) does `TerminateTitle()` + `rex::FlushLogging()` + **`std::_Exit(0)`** instead of the normal teardown, with the comment that the teardown can deadlock with a straggling `TerminateTitle`. The correct path is **`window()->RequestClose()`** (same as the X): `RequestCloseImpl` → `PerformClose` → `Window::OnBeforeClose` → `listener->OnClosing` → `_Exit(0)`. The process dies inside the call, so nothing is touched afterwards.
- **Layout note**: the value column is placed at `panel_x + panel_w*0.58` so `[restart]` isn't cut off; in **windowed** mode with `window_width` larger than the desktop the overlay may be clipped (use fullscreen or `window_width=0`).
- **Pending**: hook it into the **game's pause menu** (today it's opened with a key). Would require RE of the pause state.
- Diagnostics: cvar `sh_graphics_menu` (if true, opens it on startup; useful for screenshots).

### Solved: localization (text) with audio always in English

- The game (Gamebryo) gets the language name with **`sub_827B65E0(this)`** = `table[this+120]` (guest table **`0x82F90110`** = `{English,French,Italian,German,Spanish,Japanese}`) and builds `data/localization/%s.xml`. It does **not** use `XGetLanguage`.
- That function has **two** call sites: **`sub_82E786E8`** (early startup/audio) and **`sub_8277CFC8`** (`LocalizationManager::LoadLanguage`). Forcing the language at **both** crashes (strlen on a bad pointer in `sub_829C3AF0`) — this copy is only dubbed in English(US).
- **Solution** (`src/sh_language_hook.cpp`): weak override of `sub_827B65E0` that **only** changes the language when the call comes from `sub_8277CFC8`, distinguished by the **host return address** (`__builtin_return_address(0)` vs `&sub_8277CFC8`/`&sub_82E786E8`). Returns the name in **lowercase** (the files are `spanish.xml`). Startup/audio stays in English → **localized text, English audio, no crash** (verified: menu in Spanish).
- Cvar **`sh_language`** (`auto` = host locale, `default` = original, or `english/french/italian/german/spanish/japanese`); **Game language** row in the F5 screen. There's also the SDK cvar `user_language` for `XGetLanguage`.

### Solved: unlocking levels/chapters (it is NOT "Global Conditions")

- **The "Global Conditions" hypothesis was wrong**: the save (`GameSave.sav`, **plaintext**) only stores skill/collectible/arena conditions, not chapters. The real unlock is the `GameSaveManager`'s **level progression**.
- **Mechanism** (verified with runtime instrumentation): the Scaleform frontend asks for `GetHighestLevelCompleted`. It resolves in the stats-by-name getter **`sub_82E5C540(this, name)`** (`generated/splatterhouse_recomp.81.cpp:37020`), which for that name returns **`[statsObj+392]`** as a **`char*`** (`r3`); the AS handler that calls it is `sub_82E21EF8` (registered as `GetStat` in `sub_82F044C8`/`recomp.84.cpp:39024`). Alternative source: `GetHighestReachedLevel` → handler `sub_82F037B8` → **`sub_82EF0FE0(saveObj)`** (`recomp.161.cpp:37663`), which walks the array `[saveObj+16]` (the size comes from the difficulty manager at `0x831A7570`; **count=3 = difficulties, NOT levels**).
- **Solution** (`src/sh_unlock_hook.cpp`): weak overrides of **`sub_82E5C540`** (returns `sh_unlock_levels_max`, **7** by default, as a string in a guest buffer) and of **`sub_82EF0FE0`** (returns the maximum). Cvar **`sh_unlock_levels`** (on by default) + **`sh_unlock_levels_max`**; **Unlock levels** row in the F5 screen. Cvar **`sh_unlock_debug`** (one-shot) logs the queries. Verified: `GetHighestLevelCompleted original=0 -> 7`, no AV.
- **Note**: the level directories are `lvl1..lvl5` and `lvl7` (there's no `lvl6`), but the **selector shows 8 chapters** (confirmed by the user). With `sh_unlock_levels_max=7` all of them appear: the unlock condition is `chapter <= completed+1`, so 7 is enough for the 8th. If any stayed locked, raise the cvar (e.g. 8).

### Solved: DLC (Survival Arenas, masks, etc.)

- **DLC can be played** if the user has the packages. The chain: on X360 DLC was an **STFS package** (`CON `/`LIVE`/`PIRS`) that the console mounted as `XContent` with an `info.xml` declaring levels (`DownloadPackage` → `<Level><Flag name="Is DLC" val="1"/>`). The engine (`DownloadManagerXenon` → `LevelDatabase::AddDLCLevel`) enumerates/opens the content and adds the levels to the `leveldb`.
- **The runtime already implemented** `XamContent*` (`ContentManager`: enumerate/create/open/mount + license mask) and mounts the package as a VFS device; the engine reads the `info.xml` from the mounted package. **What was missing was installing** the packages.
- **Implemented**: `SplatterhouseApp::InstallDlcPackages()` (`OnPostLoadXexImage`) looks for STFS in `Documents\splatterhouse\<title>\00000002`, `…\0000000000000000\<title>\00000002` and **any `<xuid>\<title>\00000002`**; `SplatterhouseApp::InstallStfsPackagesFrom` detects them by magic and installs them via the public `ContentManager::InstallContent`, skipping ones `ContentExists` already reports (`InstallContent`: extracts to `…/0000000000000000/<title>/00000002/<file>/` + writes `.header` + license mask). Idempotent.
- **Verified**: with 6 packages in `B13EBABEBABEBABE\4E4D07F0\00000002`: all 6 install; the game does `XamContentCreateEnumerator type=00000002 -> 6 item(s)` and `XamContentCreate type=00000002` (opens the package); **the user confirms seeing the DLC and the masks in the menu**.
- **`XamContentOpenFile` is a stub** in the SDK (returns not-found) but **isn't needed**: the engine reads the package files through the mounted VFS.
- Diagnostics: **`REX_SH_DLC=1`** (opt-in) logs enumeration/opening. Full doc in **`docs/dlc.md`**.

### Solved: repo issues (2026-10)

**#4 — `splatterhouse.exe` starts to a black screen and closes after extracting the ISO.**
- **Cause**: the assets weren't where the runtime looks for them (`data\` was missing), the guest started anyway, couldn't find `data\localization\english.xml` or `data\swf\...`, and died with `Unhandled guest access violation: write of guest 0x00000008` (`sub_82232798`), without any useful message.
- **Fix** (`src/splatterhouse_app.h`): `OnConfigurePaths` **locates the data root by content** (`default.xex`/`default.dec.xex` + `data\`), not by the folder merely existing: it tries the `game_data_root` cvar, `assets\`, the exe folder (development layout) and **nested folders inside `assets\`** (ISO extracted by hand without flattening). Then it validates `default.xex`, `data\`, `data\localization\*.xml` and `data\swf\`; if anything is missing it shows the details in **`assets_error.txt`** (next to the exe) and in a console, and exits cleanly **before** creating the window/GPU. Escape hatch: cvar **`sh_skip_assets_check=true`**.
- **Installer**: `install.bat` now verifies after extracting that `default.xex` + `data\` exist (warns instead of half-installing) and also looks for the ISO one level deeper.
- **Note**: previously `assets\` was used as the root just by existing (in this repo it only contains a `README.md`), which broke the development layout.

**#6 — ultrawide support.**
- **Automatic** (cvar **`sh_aspect`**, default `auto`): on startup the **width** of the guest video mode is matched to the aspect of the **window/screen** (the height is kept), so the game renders 21:9/32:9 **natively** and the presenter doesn't add bars. Verified with a 2560x1080 window + `video_mode 1920x1080`: it's rewritten to 2560x1080 and the scene/UI fill the window (no bars or stretching). Log: `[sh-aspect] auto: internal mode 2560x1080 (was 1920x1080, screen 2.37:1)`.
- Values: `auto`, `16:9`, `21:9`, `32:9` (`keep` = don't touch; set by the "Internal resolution" row when picking a mode by hand). **Internal aspect** row in F5.
- **Added** (F5): internal resolutions **2560x1080 / 3440x1440** (guest video mode limit: 4095 px wide) and window sizes **2560x1080 / 2560x1440 / 3440x1440 / 3840x1080**.
- **Presets no longer break the aspect**: they set the *vertical* resolution (540/720/900/1080) and compute the width from the current aspect, rounded to a multiple of 8.
- If `sh_aspect=16:9` is forced with a 21:9 window, the presenter **pillarboxes** or stretches depending on **`present_letterbox`**.

**F5: saving/loading settings (reported together with #6).**
- **False "pending restart" flags**: `SetFlagFromSource` also marked as pending the values loaded from the **toml** (and the ones the port sets on startup), so the menu footer said "Restart to apply: video_mode_*" right after opening. Now `Source::kConfig` doesn't mark pending and the port clears the list at the end of `OnPostSetup`.
- **Displayed preset**: the Preset row always said "Ultra". Now it's derived from the configuration (`DetectPresetIndex()`: height + texture cache).
- **Saving erased the toml's comments** (toml++ doesn't keep them on rewrite): before rewriting, the previous file is copied to **`splatterhouse.toml.bak`**.
- Verified with simulated real keys (WM_KEYDOWN + scancode): F5 → down to "Internal resolution" → right (1280x720 → 1600x900) → S → Esc; the toml ends up with `video_mode_width=1600`, `video_mode_height=900`, `sh_aspect='keep'` and the `.bak` with the previous file.

**#3 — combat performance (ROV on AMD/Intel).**
- **It's not a port bug**: it's the cost of the ROV path's **interlock pixel shader**, which scales with pixels and overdraw. In combat the profile (`sh_render_profile`) gives **~1350 draws/frame** (scene ~850 + depth/shadows ~475) with heavy overlap. On NVIDIA (RTX 5060 Ti) it holds 60; **AMD (RX 9070 XT / 7800 XT) and Arc B580** GPUs report it as a slideshow. `render_target_path_d3d12="rtv"` is fast but **breaks the lighting** (design limitation of the RTV path), and **Vulkan** (FSI) is fast but today renders darker and is unstable.
- **Real mitigation today**: lower the **internal resolution** (it's the lever that scales the ROV cost) + FSR. The installer detects AMD/Intel and defaults to **1280x720** (previously 1080p); the F5 presets go from 960x540 to 1920x1080. At 720p ROV has **~2.2x fewer pixels** than at 1080p.
- **Root fix** (pending, upstream): specialize the ROV shader with the static RT parameters (requires widening the shader key; see `docs/rov-performance.md` §3/§3d).

**#1 — English + DLC.**
- The port is **English by default** (audio and text) with optional text localization (`sh_language`), and **DLC installs itself** from `Documents\splatterhouse\<title>\00000002` (see `docs/dlc.md`).
- **Added**: a **`dlc\` folder next to the exe** (created on startup and scanned) so the user only has to drop the STFS packages there. The log always shows the path (`[sh-dlc] No DLC packages. Put the packages ... in ...`).

### Solved: controller rumble that doesn't stop

- **Symptom**: when killing an enemy (sometimes the 2nd or 3rd, random) the controller keeps vibrating and **doesn't stop**.
- **Guest chain** (for future sessions): the game's rumble object has the **targets** at `[obj+84]`/`[obj+86]` and the **sent values** at `[obj+76]`/`[obj+78]`. `sub_82482990` (vtable slot, "apply") copies 84/86 → 76/78 and jumps to `sub_829F0B08`, which calls `XamInputSetState(user=0, 0, &vib)`. `sub_82482268` is the "stop": sets 84/86 to 0 and calls the vtable. **There is no interpolation**: the only places that write 76/78 are `sub_82482990`.
- **Cause in the runtime**: on PC rumble **persists until changed**, and the driver **had no safety net** to stop it. In the default backend (`input_backend="sdl"`) `SDL_RumbleGamepad` was armed with `kRumbleDurationMs = 0xFFFF` (~**65 s**, `SDL_MAX_RUMBLE_DURATION_MS`) and SDL **resends the effect every 2 s** (`SDL_RUMBLE_RESEND_MS`) while it's alive (`SDL_joystick.c:2071` / `:2777`). With that, any lost "stop" command (a rejected write, the controller being reopened, or simply the game not sending it) leaves the motor buzzing for **almost a minute**, and SDL refreshes it every 2 s. With the `xinput` backend it's worse: `XInputSetState` has no expiry or resend, so a lost "stop" = **eternal** rumble.
- **Fix** (SDK, input drivers): the last requested rumble per controller and its timestamp are remembered. If the game stops refreshing it for more than **`sh_rumble_timeout_ms`** (10 s, `0` = off) the motors are forced to 0 and `[sh-rumble]` is logged. The watchdog runs in `GetDeviceState` (the guest polls input every frame), and `OnControllerDeviceAddedLocked` stops the motors when a controller is opened (one that was already buzzing after a disconnect). In XInput, the same per slot from `EnumerateDevices`. The watchdog is armed **from the request**, not from the result, to also cover failed writes.
- **Why 10 s**: a game that pushes its rumble envelope per frame refreshes this every ~16 ms; it only fires when the rumble is truly stuck. If the `[sh-rumble]` log appeared constantly, the game **doesn't** refresh and the cvar needs raising.
- Cvar **`sh_rumble_timeout_ms`** (SDL backend only; the XInput one uses a fixed 10 s).

### Dev tool: level warp (`sh_warp`)

- To **warm up the shader cache chapter by chapter** without playing them: cvar **`sh_warp`** (level name) → the runtime loads that level. **Level warp (dev)** row in the F5 menu (cycles `lvl1_manor_interior`, `lvl2_shanty`, `lvl3_manor_catacombs`, `lvl4_carnival`, `lvl5_manor_grounds`, `lvl7_manor_chapel`, `survival_arena`, `frontend`).
- Implemented in `VdSwap_entry` (SDK, `xboxkrnl_video.cpp`): reads `sh_warp`, gets the global `GameState` (`*(0x82FFA3AC)`) and calls `GameState::RequestLoadLevelByLevelName` (`0x82795E48`, `r3=GameState`, `r4=name`) via `FunctionDispatcher::Execute` (guest thread; retries until the `GameState` exists and clears the cvar). **Splatterhouse-specific.** Verified: fires without crashing.
- Flow: in-game/menu → **F5** → **Level warp** → pick a level → wait for it to load → repeat. Then `tools\pack_shadercache.bat`.

### (historical) Localization — first attempt, blocked

- The game uses **Gamebryo**: the language comes from the **`LanguageDef`** key in its config (`game/data/options.xml`, **XCompress**); it does **NOT** use `XGetLanguage`/`XamGetLocale` (verified with logs).
- The language name is returned by **`sub_827B65E0(this)`** = `table[this+120]`, with the table in guest data at **`0x82F90110`** = `{English, French, Italian, German, Spanish, Japanese}` (pointers to names). `LocalizationManager::LoadLanguage` builds `%s\%s.xml` and falls back to `english.xml` if it isn't found.
- **Implemented** (`src/sh_language_hook.cpp`): weak override of `sub_827B65E0` that returns the language from the cvar **`sh_language`** (`auto` = host locale via `GetUserDefaultUILanguage`; `default` = original behavior). Added a **Language** row to the F5 screen. Also the SDK cvar **`user_language`** for `XGetLanguage` (useful for games that do use it).
- **Blocker**: this copy only ships **`English(US)`** audio banks (`D:\Data\sounds\soundbanks\XBox360\English(US)\`). Forcing another language makes it look for nonexistent banks → **crash** (wild pointer). The **text** would be localized (`data/localization/<lang>.xml` exist); what's missing is multi-language audio (EU version) or a fallback. Left `sh_language = "default"` (stable).
- Pending: audio fallback (use English(US) if the language is missing) so only the text can be localized.

### Solved: Scaleform/XCTD blocker (`.dds` atlas)

- The AV `sub_82823E28` (`generated/splatterhouse_recomp.86.cpp:18045`) was a **symptom**: `[singleton+520]==0` because the `subtitles.gfx` movie didn't load. Chain: `sub_828249C0` → `sub_828245F8` → vtable+44 = `sub_828419C8` → `sub_825ED970` → `sub_825F0AD0` → `sub_82604518` → `sub_82609F50` (Scaleform loader).
- **Root cause**: `.gfx`/`.dds` assets compressed with **Xbox 360 XCompress** (signature `0F F5 12 ED`, `XMemDecompressSegmentTD`). The engine (`sub_829F5E70` → `sub_829F5668`) decompresses them transparently, but to do so it queries `NtQueryInformationFile(XFileXctdCompressionInformation)` (`sub_829F5600`, class `27`); **only if the query fails** does it fall back to the magic-detection path. Local patch 3 returned success+0 → the engine read raw → Scaleform didn't parse → `+520==0` → AV. Reverting patch 3 (the SDK already returns `INVALID_PARAMETER`, like Xenia) fixes it.
- **Verified**: 0 access violations; the full UI atlas opens (**243+ `.dds`**: `savingicon.dds`, `bloodspiral01.dds`, `errorscreen_i*.dds`, …), `sh_menu_01.gfx`, `loadingscreen.gfx`; the game renders and stays up >20 s.
- Note: `REX_TRACE`/`sh-io` are still available; the XCTD decompressor lives in the engine (`sub_829F2EB8`/`sub_829F4B58`, detection in `sub_829F5668`/`sub_829F5600`).

### Instrumentation (env vars)

| var | effect |
|---|---|
| `REX_DEBUG_CS=40000618` | watches that CS (enter/leave + frames) |
| `REX_DEBUG_CS_LEVEL=4` | recursion threshold for dumping (default 4) |
| `REX_TRACE_LO=82800000` `REX_TRACE_HI=82A00000` | traces (once per function) function entries in that range |
| `[sh-av]` (always) | PC+RVA of the access violation |
| `[sh-swap]` (with `sh_log_fps`) | real `VdSwap/s` rate (guest present) — instrumented in `VdSwap_entry` |
| `REX_SH_IO=1` | caller backtrace for `.gfx`/`.dds` opens; dumps `guest_image.bin` (19.6 MB). **Opt-in**: it was `[sh-io]`, which wrote 22 lines **per asset** to the unbuffered console (I/O stall in streaming), so it's now off by default |
| `REX_SH_GPU=1` | `[sh-gpu]` warnings for `vfetch` (stride 0 / mismatch). **Opt-in** for the same reason |
| `REX_SH_DLC=1` | `[sh-dlc]`: which content the game enumerates/opens (`XamContentCreateEnumerator`/`XamContentCreate`). Opt-in |
| `sh_render_profile=true` | `[sh-render]`: render workload profile per interval (draws/frame, VS:PS pairs by ucode hash, primitive type, and "passes" by RT format + base/pitch/msaa/depth with a pixel proxy from the scissor). Settings: `sh_render_profile_interval_s` (2 s), `sh_render_profile_top` (8). In the D3D12 command processor. See `docs/rov-performance.md` §3d |

### Pending (priority: crashes > performance > graphics)

1. **Remaining crashes**: gameplay is reachable now (enemies can be hit). Each `Call to invalid or unregistered function` is solved by adding the target to `[functions]` (`[sh-uf]` gives the address; `scan_missing4.py` enumerates the `mtctr;bctr` family). **Lesson learned**: adding candidates **in bulk is not safe** — the isolated thunks the strict detector returns can split init functions and introduce a **deterministic AV at ~6 s** (tested: batch of 108 → AV in `sub_824C1558`; only the **contiguous table** `0x825CB108..0x825CB1B8` is safe). Rule: add a few at a time and verify 3 runs of 60 s without AV.
2. **Performance**: mitigated (60 FPS with the `4E4D07F0` patch + FSR at 1080p). For weak GPUs: the F5 screen **presets** (Low/Medium/High/Ultra). The root optimization is making the **ROV** path cheaper; analysis and plan in **`docs/rov-performance.md`** (the SDK TODO is to specialize the pixel shader with the static RT parameters).
   - **Diagnosis of "hitches" (measured, >2 min of real gameplay)**: it's **GPU-bound**. CPU 1–5 % of 12 cores; **GPU 3D 60–89 %**. The guest presents at ~60 `VdSwap/s` (`[sh-swap]`, log added to `VdSwap` in the SDK); the stutters are frames >16.6 ms that drop to **30** (half vblank/limiter). Spreading across cores **doesn't help** (there's no CPU load). Levers: lower `video_mode_*` (F5 preset), `fullscreen=true`, or make ROV cheaper.
   - **Root cause of the big hitch (SOLVED): `PipelineCache::EndSubmission()`**. Even though `async_shader_compilation=true` skips the draw, `EndSubmission` **created the queued pipelines on the present thread** (`CreateQueuedPipelinesOnProcessorThread`) and **waited for the background threads** (`Wait`) → stall of up to **673 ms** on the UI thread during the present (when entering areas with new materials/PSOs). **Fix**: cvar **`sh_pso_async`** (default **on**) → `EndSubmission` only wakes the background threads and **doesn't wait**; draws without a pipeline are skipped (popping). Verified: `EndSubmission(swap)` 40.9→4.1 ms; max frame 673→51 ms. Reversible with `sh_pso_async=false`.
   - **Diagnostic instrumentation** (opt-in, cvar `sh_frame_log` + `sh_frame_log_ms`): `[sh-frame]` scopes in `IssueSwap`, `Begin/EndSubmission` (per phase: `pipeline_cache_EndSubmission`, `SubmitBarriers`, `allocator_Reset`, `Signal`, `deferred_execute`, `ExecuteCommandLists`), `CheckSubmissionFence/wait-GPU`, `IssueDraw`, `IssueCopy`, and in the presenter (`PaintAndPresentImpl`, `frame_limiter_wait`, `swapchain_Present`, `RefreshGuestOutputImpl`, `refresher-lambda`, host presents/s). **Note**: `host PaintAndPresent` measures ~33 ms but **32.5 ms of it is the frame limiter's voluntary wait** (not work); the real render is <1 ms and the host presents at **60/s**. Don't mistake it for a stall.
   - **"Fast motion" (x2) note**: with `vsync=false` the game does **not** speed up (the guest still presents at 60); the x2 was the **60 FPS patch applied on a 30 base**. Check `sh_60fps` if it shows up again.
   - Shader translation and PSO creation **already** run on background threads (`d3d12_pipeline_creation_threads` = cores×3/4) and the cache is pre-created on startup (401→1072 PSOs in <0.3 s; **0** during gameplay). `async_shader_compilation=true` skips the draw until the pipeline is ready (popping, not freezing).
   - Heavy instrumentation was moved to **opt-in** (`REX_SH_IO`/`REX_SH_GPU`): `[sh-io]` wrote a 22-line backtrace to the **unbuffered** console on every `.dds`/`.gfx` open (stall in the streaming path). Rebuilt `rexruntimerd.dll` + `rexgpu-xenosrd.dll`.
   - If the stutters persist: a frame that exceeds 16.6 ms makes **vsync drop to 30** (half vblank). Levers: lower `video_mode_*` (preset), `vsync=false`+`d3d12_present_frame_limiter`, or make ROV cheaper. Look into why loading/intro starts at 30 before settling at 60.
3. **Graphics bugs (lighting) — RTV can't be fixed by config**: 3D scene almost black, only emissives/lamps and UI visible (silhouette) with **RTV**. It's the **design limitation** of the RTV path (confirmed in the Xenia wiki and PGR4 issue #161): the X360 eDRAM is reinterpreted in several formats and has formats that don't exist on PC (7e3 HDR, fixed −32..32…); the RT path renders to PC textures and **approximates/copies**, losing the eDRAM overlap/reinterpretation the lighting passes use. **ROV renders directly to the eDRAM** (blending/depth in the shader) and reproduces it correctly.
   - **Required** `render_target_path_d3d12 = "rov"` (on non-Intel GPUs the default is `rtv` → black scene).
   - **Tried and does NOT fix it** (RTV + `readback_resolve="full"`, `direct_host_resolve=false`, `mrt_edram_used_range_clamp_to_min=false`, `gamma_render_target_as_unorm16=false` → still dark). Really fixing it = reimplementing in the RTV path what ROV does → a large graphics project.
   - With **ROV + FSR** gameplay already holds **60 FPS** at 1080p, so RTV isn't needed for performance.
4. Excessive **post-processing/DoF blur** at the start: probably UE3 low-res DoF + 720p→window upscale; mitigable with `resolution_scale = 2` (GPU cost).
5. Review the 3 codegen warnings `bdz ... branches outside function` (0x8235B878/0x8235B888) — heuristic.
6. Xenia tooling: still in `C:\Users\josel\Downloads\xenia_canary_windows` (verbose log; `trace_functions`+`trace_dump_path`).

## Useful static audits

- File: `C:\Users\josel\AppData\Local\Temp\opencode\scan_rbw.py` — read-before-write of local registers over `generated/*.cpp` (filters LR spills and `__save*`/`__rest*` helpers).
- File: `C:\Users\josel\AppData\Local\Temp\opencode\find_addr.py` — locates `lis/addi` references to a guest address.
- Note: the thousands of `r30/r31` warnings are **dead spills** from the prologue; the 15 "without terminator" are **harmless** (implicit host return).

## Conventions

- Do NOT add comments to the code unless asked.
- Never commit without explicit instruction. Never commit assets or secrets (`game/` is gitignored).
- The old custom runtime lives in `legacy/` (reference only, doesn't build with the new stack).
- User-facing text (F5 menu, installer, logs) and documentation are in **English**.
