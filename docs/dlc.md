# DLC in the port (Survival Arenas, masks, etc.)

Splatterhouse (2010) had downloadable content (DLC). The port supports it: if
the user has the packages, they can be played. This guide explains how it works
and where to put them.

## What DLC exists

- **Survival Arena Pack**: 4 arenas (`island_arena`, `heartboss_arena`,
  `mirror_rick_arena`, `white_tile_arena`) — the `info.xml` declares them with
  `<Flag name="Is DLC" val="1"/>`.
- Extra **masks / weapons / outfits** (cosmetic and gameplay content).
- Note: the **base disc already includes** the 6 original Survival Arenas and
  the *Classic Splatterhouse 1/2/3* emulations; the DLC adds extra arenas and
  items.

## How it works (full chain)

1. On Xbox 360 DLC was an **STFS package** (signature `CON `/`LIVE`/`PIRS`) that
   the console mounted as `XContent`. Its `info.xml` declares levels
   (`DownloadPackage` → `<Level ...><Path val="levels/...">`).
2. The engine (`DownloadManager` / `DownloadManagerXenon` →
   `LevelDatabase::AddDLCLevel`) enumerated the content (`XEnumerate`), opened
   the package, read `info.xml` and **added the DLC levels to the `leveldb`**.
3. The ReXGlue runtime **already implements** `XamContent*` (`ContentManager`:
   enumerate, create, open/mount, licenses) and mounts the package as a VFS
   device with its symlink (`XContent:…`). On open, the engine reads the mounted
   package's `info.xml` and adds the levels.

The only missing piece was **installing** the STFS packages into the user's
content folder. The port does this automatically on startup.

## Automatic installation (implemented)

In `SplatterhouseApp::InstallDlcPackages()` (called from
`OnPostLoadXexImage`, once the title ID is known) the port:

1. Looks for STFS packages in:
   - **`<exe folder>\dlc\`** (the most convenient: it is created on startup and
     the log always shows the path)
   - `%USERPROFILE%\Documents\splatterhouse\<title>\00000002\`
   - `%USERPROFILE%\Documents\splatterhouse\0000000000000000\<title>\00000002\`
   - **any first-level subfolder** (e.g. the profile xuid
     `B13EBABEBABEBABE`) `\<title>\00000002\`

   where `<title>` is `4E4D07F0`.
2. `SplatterhouseApp::InstallStfsPackagesFrom` detects each package by its magic
   (`CON `/`LIVE`/`PIRS`) and installs it with the SDK's public
   `ContentManager::InstallContent`:
   - mounts it with `StfsContainerDevice` and **extracts** its contents to
     `root_path_/0000000000000000/<title>/00000002/<filename>/`
   - writes the `.header` file for XAM enumeration
     (`…/<title>/Headers/00000002/<filename>.header`)
   - uses the **license mask** from the STFS package's licenses
3. It is idempotent: if `ContentManager::ContentExists` says the package is
   already installed, it is skipped (`InstallContent` itself always re-extracts).

After this, the engine calls `XamContentCreateEnumerator(type=0x00000002)` → sees
the packages, opens them and adds their levels/masks to the menu.

## Where to put it (user)

1. Get the DLC packages (STFS format: files starting with
   `CON `/`LIVE`/`PIRS`, usually without an extension or `.xcp`).
2. Copy them to, for example:
   ```
   %USERPROFILE%\Documents\splatterhouse\4E4D07F0\00000002\
   ```
   (any `<xuid>\4E4D07F0\00000002\` also works, as does `dlc\` next to the exe).
3. Start the port. The log will show:
   ```
   Installed DLC package <hash>
   [sh-dlc] Installed N DLC package(s)
   ```
4. The DLC arenas/items will appear in the menu.

## Diagnostics

- **`REX_SH_DLC=1`** (environment variable): logs in detail which content the
  game enumerates/opens (`[sh-dlc] XamContentCreateEnumerator type=…`,
  `XamContentCreate type=… file=…`) and the `ListContent`. Useful if a DLC
  doesn't show up.
- The installation log (`Installed DLC package …`) is at `info` level; with
  `log_level = "warn"` it isn't shown, but the installation still happens.

## Limitations / notes

- Cosmetic DLC (masks/outfits) uses `DLCBodyPart` and the save's condition
  system; in the port it is unlocked through the condition system (DLC arenas
  show up in the selector; masks, when choosing a chapter).
- `XamContentOpenFile` is still a stub in the SDK; it hasn't been needed because
  the engine reads the package files through the mounted VFS, not that API.
- Licenses: the port copies the license mask from the STFS package itself, so
  legitimately downloaded content is recognized without patching anything.
