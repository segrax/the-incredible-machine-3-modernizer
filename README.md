# TimWin Modernization Patch

<img width="2521" height="1226" alt="image" src="https://github.com/user-attachments/assets/18dd49ff-0fdc-4ab2-9bcd-aea7aedbe99e" />

Open-source patch project for the Windows release of *The Incredible Machine 3*
(`TIMWIN.EXE`). The goal is to make the original game usable on modern Windows
without distributing Sierra/Dynamix game files.

The release artifact is a native `timwin-patch.exe`.

This is not an official Sierra/Dynamix release.

## Quick Start

With a built or downloaded `timwin-patch.exe`:

```powershell
timwin-patch.exe --game-dir D:\Games\TIM3\TIMWIN
```

For a local development copy where the installed files are already patched, use
known-clean originals as the source and update the game directory as the target:

```powershell
build\cmake\timwin-patch.exe --source-dir .\local\original --game-dir .\TIM3\TIMWIN
```

Check what would happen without writing files:

```powershell
timwin-patch.exe --game-dir D:\Games\TIM3\TIMWIN --check
```

The patcher creates timestamped backups next to changed files before replacing
them.

## Downloads

The Windows patcher is built by the repository workflow. Download the
`timwin-patch-windows-x86` artifact from the latest successful build run.

## Patcher Options

- `--game-dir DIR`: directory containing the `TIMWIN.EXE` and `SOS9502.DLL` to
  update.
- `--source-dir DIR`: directory containing clean original `TIMWIN.EXE` and
  `SOS9502.DLL` to patch from. If omitted, `--game-dir` is used as the source.
- `--check`: validate the source files and report whether the target is already
  up to date.
- `--dry-run`: build patched files in memory without writing them.
- `--help`: show command-line help.

## Compatibility Fixes

- Fixes the `SOS9502.DLL` MIDI crash in `sosMIDIInitSong`, where invalid song
  table state could dereference bad memory.
- Enables MIDI playback through the WinMM path used on modern Windows.
- Fixes digital sound effects initialization where valid wave devices could be
  detected but never acquired.
- Routes sound effects through `WAVE_MAPPER`, so Windows can use the default
  output device instead of relying on old device enumeration behavior.
- Adds a relocation guard in `SOS9502.DLL` to avoid bad relocation handling on
  modern systems.
- Adds object/shape frame guards in `TIMWIN.EXE` to avoid crashes when play
  begins or invalid shape frame data is encountered.
- Suppresses the obsolete 16-bit `TIMHELP.EXE` launch path, avoiding a modern
  Windows compatibility error when quitting the game.
- Fixes professor/help/goal/hint voice lookup when `sierra.ini` is missing
  or has a stale `CDPath`; the game now falls back to the directory containing
  `TIMWIN.EXE` when the `.VOC` banks are present there.
  
The MIDI issue is treated as a latent SOS driver compatibility bug, not as
"Windows 11 cannot play old MIDI." The original driver assumes the song table
contains valid entries after initialization. On the modern WinMM path, that
assumption can be false, so the patch accepts the modern return path and clears
invalid entries instead of dereferencing them.

## Display Enhancements

- Enlarges the playfield so it uses most of the main window.
- Adds dynamic child-window layout when the main window is resized.
- Positions the goal window at the bottom-right by default.
- Keeps the parts window to the right of the playfield.
- Scales playfield, parts, goal, and toolbar drawing while preserving practical
  aspect ratios.
- Allows Enter to dismiss modal dialogs such as round-start prompts.
- Makes the toolbar full-screen/window toggle restore to a centered half-screen
  window instead of remaining effectively full-screen.
- Centers the main menu and top-level menu panels inside the current game
  window after switching between full-screen and windowed layouts, and keeps
  them centered while the window is resized.
- Keeps manual main-window resizing locked to the startup screen aspect ratio.

## Supported Input Files

The patcher validates exact bytes before applying patches. The currently
supported clean input files are:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `TIMWIN.EXE` | 454656 | `A2562AE1FF8355666E2C55F8DC7CB5F262F5B2798680F390CB374E3FCAAE3080` |
| `SOS9502.DLL` | 82944 | `81257DBE0994545348299A9BE0FB683023E2DCF5E319EC744625200569D973D3` |

## Building From Source

Source builds need:

- CMake
- Ninja
- Visual Studio Build Tools with 32-bit MSVC `cl.exe`, `ml.exe`, and `link.exe`
- Windows 10 SDK `mt.exe`

Configure and build the native patcher:

```powershell
cmake -S . -B build\cmake -G Ninja
cmake --build build\cmake --target native-patcher
```

If CMake or Ninja are not on `PATH`, use the copies bundled with Visual Studio:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

& $cmake -S . -B build\cmake -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja"
& $cmake --build build\cmake --target native-patcher
```

Build output:

- `build\cmake\timwin-patch.exe`

## Local Files

For local development, clean originals can be kept under:

- `local\original\TIMWIN.EXE`
- `local\original\SOS9502.DLL`

`local\` is ignored by git and is intended for proprietary binaries, IDA
databases, backup files, screenshots, and old investigation artifacts.

## Architecture

- `src\timwin_patch.c`: native release patcher. It validates original bytes,
  adds the `.patch` section, applies EXE/DLL byte patches, embeds hook bytes,
  and writes backups.
- `hooks\timwin_hooks.c`: injected TIMWIN routines written in C for larger
  logic such as layout, scaling, mouse coordinate handling, and repaint hooks.
- `hooks\timwin_trampolines.asm`: register-sensitive hook trampolines and tail
  jumps back into the original executable.
- `hooks\sos9502_hooks.asm`: compiled SOS9502 DLL cave code for the MIDI and
  relocation fixes.
- `tools\hooklink.c`: native build helper that links hook object code into the
  generated blob embedded by the patcher.
- `CMakeLists.txt`: builds the hook objects, generated hook blob, and native
  patcher.

The hook code is embedded into `timwin-patch.exe` as bytes. It is not linked as
normal patcher code, because the TIMWIN hooks must run later inside
`TIMWIN.EXE` from the game's `.patch` section, and the SOS9502 hooks must run
from fixed DLL cave addresses.

Generated build products are ignored:

- `build\`
- `patched\`
- `hooks\build\`

Local game/debug artifacts are also ignored:

- `TIM3\`
- `local\`
