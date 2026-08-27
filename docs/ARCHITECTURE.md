# GEOBENCH Architecture

This is the current system shape, not a speculative design note. The quick
summary lives in the top-level README; this file describes the actual runtime
boundaries and the places where the current implementation is intentionally
constrained.

## Layer cake

GEOBENCH is organised as layers, lowest (closest to hardware) at the bottom:

```
┌─────────────────────────────────────────────┐
│  apps/  desktop·filemgr·notepad·iconed·       │  ← banked binaries, run on demand
│         viewer·clock·network tools (C)        │
├─────────────────────────────────────────────┤
│  libgb  C bindings -> the kernel jump table,  │  ← lib/gb/ (gb.h + trampolines,
│         window + dialog helpers               │     gbwin.c, gbdlg.c/gbprompt.c)
├─────────────────────────────────────────────┤
│  kernel/   boot · API table · banking · fs ·  │  ← resident; Z80 asm
│            assets · modules · input · WM       │
├─────────────────────────────────────────────┤
│  CPC / MSX2 / PCW hardware and DOS/firmware services │
└─────────────────────────────────────────────┘
```

Each layer only calls **down** through documented entry points. Apps never touch
video, storage or input hardware directly. They go through the kernel ABI in
`lib/gbapp.inc`, reached from C via `libgb`. The desktop is itself an app; it is
the first one booted and the one other apps return to.

## Memory model

128K+ only (the banked app model needs expansion banks). A target-specific
memory mapper pages a 16K block into the `#4000-#7FFF` window:

- The **kernel is resident** in always-mapped RAM at `#8000+`. So are the stack,
  fixed low-RAM contracts, and the target's screen/firmware interface.
- The kernel's **data buffers** (font, icon set, directory scratch) live in a
  bank page (`PAGE_DATA`); a service swaps that page in, touches the buffer, and
  restores the caller's page.
- **Apps are loaded into bank pages** (`PAGE_APP0+`) at `#4000` and run there.
  They nest: desktop -> filemgr -> (notepad/paint/viewer/...), each in its own
  page; the launcher keeps the caller's page on the stack and restores it on quit.
  An app may optionally begin with a `GBAP` executable preamble: a `JP` preserves
  the `#4000` launch ABI while a canonical 32x32 icon occupies the bytes before
  the relocated entry. V2 may declare a second native Screen-7 icon in an
  explicit resource directory. File Manager probes this header on demand, so
  personalized application icons do not enlarge the boot-loaded `.IST`.

## Kernel source layout

The resident binary is still one assembled image, but the source is now split by
subsystem so responsibilities are explicit:

- `kernel/gbkern.asm` — top-level assembly order and build flags.
- `kernel/api_table.inc` — the fixed kernel jump table.
- `kernel/lowram.inc` / `kernel/lowram.tsv` — absolute low-RAM ownership and the
  checked manifest used by `tools/check_lowram_map.py`.
- `kernel/boot.asm` plus `boot_msx.asm` / `boot_pcw.asm` — target boot, desktop
  launch, and exit/warm-boot paths.
- `kernel/assets.asm` — font/icon/cursor/backdrop/wallpaper reload helpers.
- `kernel/config_module.asm` — `GBCFG.MOD` boot-time parse/load path.
- `kernel/modules.asm` — shared paged-module runners (`GBUI`, `GBWEB`, `GBIMG`, `GBNET`, config).
- `kernel/app_pool.asm` — bank-page allocation/free for apps and borrowed pages.
- `kernel/input_api.asm` — pointer/keyboard polling and top-bar dispatch.
- `kernel/clock.asm` / `kernel/memdetect.asm` — RTC/timekeeping and RAM probe.

The split is meant to keep the generated kernel image stable while making future
size work safer.

## Execution model

GEOBENCH uses a hybrid preemptive runtime:

- The resident window manager owns the master event loop and keeps several
  banked app windows resident at once.
- Each app registers frame, draw, and event callbacks. The root task invokes
  bounded app jobs and composites repaint callbacks in z-order.
- One window owns input focus. Kernel services, paged modules, storage, and
  painting execute atomically.
- Apps may explicitly register a pure compute worker. Timer preemption rotates
  those workers without allowing them to call kernel, UI, firmware, storage, or
  module services.

The scheduler is the release default on CPC, MSX2, and PCW. It is carried by
`DESKTOP.APP`, installed in fixed RAM, and does not use the optional
GEOBENCH/M4 ROM paths. Explicit cooperative builds remain available for
regression testing; see
[PREEMPTIVE_MULTITASKING.md](PREEMPTIVE_MULTITASKING.md).

## The system API

Applications reach system services through a fixed **jump table** at `#8000`.
Each entry is a 3-byte `jp`, so slot addresses stay stable as the kernel grows.
`lib/gbapp.inc` is the authority; `tools/check_abi_table.py` verifies the
exported table against the kernel source. C apps call the table through `libgb`.
Current service groups:

- **Drawing** — text (`gb_text`), filled rects (`gb_fill`), outlines
  (`gb_frame`), icons (`gb_icon`/`gb_blite`), windows (`gb_window`).
- **Input + cursor** — `gb_poll` (pointer position + click/quit/fire),
  `gb_curshow`/`gb_curhide`.
- **Files** — directory iteration, load/save/delete/copy, drive selection, and
  system-file loading through the active storage backend.
- **Apps / WM** — open/close windows, managed chrome, app launch, fullscreen,
  focus/z-order, drag-and-drop, clipboard, top-bar integration.
- **Modules / services** — config parsing, UI/file dialogs, networking, media
  reload, backdrop/wallpaper helpers, RTC/time, RAM total.

The shared `gb_net_*` contract has target-specific implementations. CPC calls
the paged `GBNET.MOD` or `GBNETM4.MOD`; PCW apps use PerryNet over the serial
interface; MSX2 Browser and Telnet link a small TCP/IP UNAPI client into their
own app banks, so the network transport does not occupy resident kernel space.

Keeping this explicit jump table is what lets apps stay as separate C binaries
without linking against private kernel internals.

## Storage and distribution shape

The shipped CPC runtime targets are:

- **Albireo / CH376 card** as the primary read/write storage path.
- **M4 board** as a shared-image card path (`GBM4.BIN` on the same FAT image as
  `GBALB.BIN`) and as a TCP networking path through M4ROM commands. M4 storage
  and TCP calls preserve the active video mode/hint so fullscreen Mode 2 apps
  such as Telnet are not forced back to Mode 1 while modules are loaded.
- **AMSDOS floppy** as the fallback path and as the bootable disk-pair format.

MSX2 uses MSX-DOS 2 / Nextor BDOS calls for mapper-aware drive access. PCW uses
its native uPD765 CF2/CF2DD backend and boots without CP/M. These target
backends expose the same app-visible file API.

The **IDE** backend still exists in source but is not built by the default workflow.
See [`ARCHIVED.md`](ARCHIVED.md) for the exact support boundary.

The CPC media layout is intentionally simple:

- card: `QA/CPC/CARD/` with `GB.BAS`, `M4DETECT.BIN`, `GBALB.BIN`, `GBM4.BIN`,
  `GEOBENCH.CFG`, `GBENCH/`, root-level `PICS/`, and root-level `DIAG/`
- floppy: `QA/CPC/Floppies/GEOBENCH.DSK` (Main),
  `QA/CPC/Floppies/COMPANION.DSK` (drive-B apps), and
  `QA/CPC/Floppies/EXTRAS.DSK` (the complete gallery)

MSX2 stages loose files under `QA/MSX/CARD/` and two FAT12 floppies under
`QA/MSX/Floppies/`; its generated `QA/MSX/GBMSX.IMG` is local and ignored. PCW
ships `QA/PCW/Floppies/GEOBENCH.DSK`, `COMPANION.DSK`, and a 720K `EXTRAS.DSK`.

Pictures, icon sets, and backdrops use portable payloads. CPC, MSX2, and PCW all
store canonical Mode-1 GBPC v2 picture bytes; the MSX2 and PCW screen backends
translate them while blitting. See [`PIC_FORMAT.md`](PIC_FORMAT.md). Canonical
Mode-1 `.IST` icon sets and 64-byte `.BDP` backdrop tiles are decoded when loaded
on MSX2/PCW, while CPC uses their bytes directly. The MSX2 Screen 7 Viewer and
desktop wallpaper loader also accept an explicitly non-portable mode-7 GBPC
payload with sixteen colours.

## Settings and media contracts

`GEOBENCH.CFG` is the user-visible configuration contract. The important current
rules are:

- on MSX2, `MSXMODE=6|7` selects the mode-specific kernel at the next boot;
- on MSX2, `MSXMOUSE=TRUE|FALSE` selects mouse or joystick interpretation for
  the joystick port;
- backdrop, wallpaper, and saver names may be **drive-qualified** (`A:NAME`,
  `B:NAME`, `C:NAME`) so the Settings app can point at either floppy or Albireo
  content explicitly;
- backdrop and wallpaper are treated as mutually exclusive desktop background
  sources;
- invalid configured media falls back safely to `SOLID` / `NONE` during boot so
  the machine still reaches the desktop;
- `TITLEBAR=<name>` selects a canonical 56-byte `.TBR` background motif and
  `GADGETS=<name>` independently selects a 50-byte `.GDT` close/maximize pair;
  both are independent of `ICONS=` and fall back to embedded `ORIGINAL` assets;
- `PROXY=` is Browser's optional persistent plain-HTTP proxy `host[:port]`; an
  empty value means direct access. Legacy values prefixed with `http://` remain
  accepted and are normalized when loaded or saved;
- screensavers are full-screen `.SAV` apps launched by the desktop idle timer;
  per-module values remain app-owned `GEOBENCH.CFG` keys. Settings derives and
  pages in an optional same-stem configuration companion (`XMATRIX.SAV` uses
  `XMATRIX.MOD`) through the existing arbitrary `GB_UI` module path. The
  companion owns its controls and returns bounded key/value updates for Settings
  to persist, so neither Settings nor the resident kernel contains a saver
  registry. STARFLD exposes speed/star count; XMATRIX exposes glyphs/speed plus
  a CPC hardware-ink or MSX Screen 7 palette-index control; MOUNTAIN exposes
  speed, peak count, and hold time while its MSX Screen 7 renderer uses eight
  elevation bands.

The intent is to keep policy in apps or modules and keep the resident kernel at
the level of asset reload, storage, and window-manager primitives.

## Hardware notes

- **Video:** CPC uses Mode 1 (320x200, 4 colours); MSX2 uses V9938 Screen 6 or
  Screen 7 at 512x212; PCW uses a monochrome 720x256 surface. Platform drawing
  backends translate canonical portable assets at the display boundary.
- **CPC vs CPC+:** addressed behind named constants; the plus's extra features
  (hardware sprites, palette) are a possible enhancement, not a dependency.
- **AMSDOS:** file I/O goes through firmware vectors. Note that USB/FAT-drive
  AMSDOS shifts some CAS IN vectors — see the sibling `n4c-nettools` notes.

## Known architectural limits

- **128K+ only.** The app model depends on banked memory.
- **Shared services are atomic.** Preemption is restricted to opted-in pure app
  workers; UI callbacks, storage, modules, firmware, and kernel work remain on
  the root task.
- **Flat-ish content layout.** Nested subdirectories deeper than one level are
  not a supported storage workflow today; see [`File_Manager_Issue.md`](File_Manager_Issue.md).
- **Legacy machine-code software is not contained.** Ordinary `.BIN` programs
  still require leaving GEOBENCH. `.BAS` files open in the external GB-BASIC
  application when that package is staged.

## CPC booting and distribution

`bash tools/build_kernel.sh` stages (and ships under `QA/`):

- **`QA/CPC/CARD/`** — for the shared Albireo/M4 card: `GB.BAS`, `M4DETECT.BIN`,
  `GBALB.BIN`, `GBM4.BIN`, `GEOBENCH.CFG`, and a `GBENCH/` subfolder holding the
  kernel-loaded payload (apps, modules, fonts, icons, cursor), plus a root-level
  `PICS/` gallery and `DIAG/` diagnostics folder.
- **`QA/CPC/GEOBENCH.IMG`** — a ready-to-flash shared **Albireo/M4** FAT16 card image,
  rebuilt by `tools/build_card_img.sh`; local artifact, not committed.
- **`QA/CPC/Floppies/GEOBENCH.DSK`** — the **Main** flat bootable floppy: the OS (kernel/loader/modules/
  fonts/icons/cursor/config), the core apps (Desktop, Notepad, Clock, File Manager, Viewer,
  Settings, Iconed), the default `SQUARES.SAV` saver, the `LOGO.PIC` wallpaper and the
  backdrops. Built by `kernel/gbkern.asm` + `pack_apps{,2,3}.asm` (#250).
- **`QA/CPC/Floppies/COMPANION.DSK`** — the **Companion** floppy (#250): a non-bootable DATA disk with
  the extras — Paint, Telnet, WGET, Browser, Shell, Mahjong, Xaos, Calculator,
  and the extra screensavers except `XROACH.SAV`, `CATCLK.SAV`, `HELIX.SAV`, and
  `FOREST.SAV`, and `MOUNTAIN.SAV` with its same-stem configuration module,
  which are kept on Extras to leave enough CF2 blocks for Calculator, Browser's
  private image/layout module, and bounded saver state.
  Built by `kernel/pack_comp{1,2,3,4,5}.asm`. It is meant for **drive B** while the Main floppy
  stays in drive A: the kernel's system loader (`fs_load_sys`, `lib/fs.asm`) tries the boot
  drive (A) first and **falls back to the browse drive** (B), so a Companion app launched
  from a drive-B File Manager loads from B. `BRSAVE.APP` and optional `HAND.SPR`
  and Browser's private `GBIMG.MOD` live beside the Companion apps; shared
  dependencies (`GBUI.MOD`, `GBAPICK.MOD`, `GBWEB.MOD`, `GBNET.MOD`) load from
  A, while `PAINT.IST` stays beside Paint. (Card
  builds are unaffected — they already ship everything on one volume, including
  `GBNET.MOD` for Net4CPC and `GBNETM4.MOD` for M4 TCP.)
- **`QA/CPC/Floppies/EXTRAS.DSK`** — all tracked pictures, including `LOGO.PIC`,
  plus `XROACH.SAV`, `CATCLK.SAV`, `HELIX.SAV`, `FOREST.SAV`, and
  `MOUNTAIN.SAV` with `MOUNTAIN.MOD`, on an extended 80-track single-sided
  AMSDOS DATA image built by `tools/mkcpcmedia.py`. The
  normal Main and Companion CF2 images contain no other `.PIC` files.

M4 TCP is intentionally still a paged service: each `gb_net_*` call loads
`GBNETM4.MOD` through the M4 storage backend before issuing the M4ROM network
command. Both layers preserve the caller's active screen mode, using the shared
`video_mode_hint` low-RAM byte when an app has taken over the display.

`RUN"GB` runs `GB.BAS`. On card media it loads and calls `M4DETECT.BIN`; M4ROM
machines `RUN"` `GBM4`, and non-M4 card machines `RUN"` `GBALB`. On floppy media
it `RUN"`s `GBKERN`. The kernel then loads from `/GBENCH` on card media and from
the flat root on floppies.

The loader is **BASIC, not machine code**, on purpose: under UniDOS (CP/M-based) a
`RUN"`-loaded binary that returns triggers a warm-boot, the firmware CAS goes to tape,
and the DOS's RSXs/BIOS are unreachable from a loaded binary — whereas a BASIC program
runs with the DOS fully active, so its `RUN"GBALB` or `RUN"GBKERN` simply works.

### Optional legacy GEOBENCH ROM (driver offload + boot banner)

For recovery and size experiments, the screen-independent low-level drivers —
the FAT read/write core, the AMSDOS floppy reader, the IDE backend and the
CH376/Albireo backend — can run from a **16K loadable
upper ROM** instead of the resident kernel, freeing `#8000` RAM (the headroom that
unblocks window-chrome work like #156). `tools/build_rom.sh` builds one per card:
`rom/GEOBENCH.ROM` (IDE) and `rom/GBALB.ROM` (Albireo).

A driver call pages the ROM in (`OUT (#7F),#85` — upper ROM on, **lower** ROM off so
low-RAM scratch stays visible; `#7F81` would overlay the firmware lower ROM and corrupt
it), `OUT (#DF)` selects the ROM number, and the kernel `CALL`s a fixed dispatch slot.
The ROM is read-only, so each backend's writable state is relocated to fixed low RAM that
both the resident stubs and the ROM agree on (the FAT core at `#1C00`, the CH376 path at
`#1293`, a transfer area at `#1270`). The resident kernel is built with `-DGB_ROM_REQ=1`
to use the stubs; without the ROM the plain kernel runs every driver resident, so the ROM
is optional and is not part of the normal release configuration.

The same image is a standard CPC **background ROM** (type-1 header at `#C000`):
the firmware initialises it at cold boot and it prints a `GEOBENCH <commit>`
banner before BASIC's prompt. The offload dispatch table lives just past the
header.
