# Recompilation — Splatterhouse Xbox 360

## ReXGlue flow

```
default.xex (encrypted XEX2)
   |  xextool -r -c
   v
default.dec.xex (decrypted PPC IMAGE)
   |  rexglue codegen splatterhouse_manifest.toml
   v
generated/*.cpp + _init.h/.cpp (PPC functions as C++ with PPCContext)
   |  + SDK runtime (rex::Runtime, KernelState, FunctionDispatcher)
   v
splatterhouse.exe (native x64, Clang)
```

## Kinds of code

- **Pure recompiled**: game logic (update, AI, physics). Goes to `generated/` (auto-generated, do not edit).
- **Overrides/HLE**: kernel calls are reimplemented as `REX_EXPORT`/`REX_HOOK` (see the Function Overrides wiki page). The SDK already ships xboxkrnl/xam shims; add any missing ones in `src/`.
- **Mid-ASM hooks**: fine-grained per-instruction patching via `[[midasm_hook]]` in `config/codegen.toml`.

## How to figure out what to implement first

1. `rexglue codegen splatterhouse_manifest.toml --force` and build. The first time there will be `UnresolvedCall`s: list them in `config/codegen.toml` under `[functions]`.
2. Run it and look at the first runtime logs (enable `--log_level trace` / cvar `log_level`).
3. Implement the missing imports with `REX_EXPORT` or `REX_EXPORT_STUB` stubs.
4. Repeat.

Useful tools:
- `xenia` (reference implementation; ReXGlue derives from its runtime)
- IDA/Ghidra with a PPC plugin to see strings and xrefs for `MaxSmoothedFrameRate`, `ResX`, etc.
- `python tools/recompile.py --dry-run` to validate paths without the binary.

## UE3 specifics (Splatterhouse)

Splatterhouse 2010 uses Unreal Engine 3 (~2009). Hints:
- The binary contains the strings `Unreal`, `GWorld`, `GEngine`, `GFx`, `Bink`
- Configs in `Coalesced.ini` / `*.ini` inside `game/CookedXenon/`
- To unlock FPS look for `bSmoothFrameRate`, `Min/MaxSmoothedFrameRate` in memory or .ini.
- Scaleform GFx for HUD/menus -> may need a `GFx` HLE or simply let the recompiled code call it (it's part of the game, not an OS import).

## Patches

See `config/patches.toml.example`. Two kinds:
- **Bin patch at recompilation**: XenonRecomp can NOP out branches.
- **Runtime patch**: patch guest memory after `mem::Initialize()` (e.g. write `60 00 00 00` at an addr).

## Debugging

- `config.toml` -> `logLevel=0` + `devMode=true`
- In MSVC: `F5` with the `windows-debug` preset keeps a console open with logs.
- For PPC tracing: enable `XenonUtils` trace (if integrated).
