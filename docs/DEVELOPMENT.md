# Development

How GEOBENCH is built and run during development. None of this runs on the CPC;
it's the host-side workflow.

## Toolchain

- **Assembler:** [RASM](http://www.roudoudou.com/rasm/) (`rasm` on PATH —
  v3.2.1+). RASM can emit raw binaries, AMSDOS-headed binaries, and `.dsk`
  images directly, so no separate disk tool is required for the floppy.
- **C compiler:** [SDCC](http://sdcc.sourceforge.net/) (`sdcc`, `sdasz80`,
  `makebin`) for the apps.
- **Card image:** `tools/build_card_img.sh` needs `sfdisk` (util-linux),
  `mkfs.fat` (dosfstools) and `mcopy` (mtools) to build the partitioned FAT16
  `QA/GEOBENCH.IMG`. The project distrobox carries all of these. `tools/build_rom.sh`
  (the driver-offload ROM) needs only `rasm`.
- **Emulators:** see below.

## Emulators

We develop against two emulators with distinct roles:

| Emulator | Role | Why |
|----------|------|-----|
| **1984** (`../1984`) | **Primary dev target** | The user's own cycle-stepped CPC emulator. Standard firmware/AMSDOS software runs well — which is all GEOBENCH is. `--screenshot-at=N:PATH` dumps a `.ppm` and exits, giving clean headless visual verification, and `--autostart=NAME` autoruns a file after boot. Dogfooding it exercises the emulator too. |
| **cap32** (`../caprice32`) | **Cross-check oracle** | Mature, widely-validated [Caprice32](https://github.com/ColinPitrat/caprice32). When a behaviour is ambiguous, run the same artifact on cap32: if the two disagree, the bug is localised. |

### Why 1984 is primary, not cap32

cap32 is arguably more *accurate* — but only for the things GEOBENCH never does
(cycle-exact CRTC tricks, undocumented hardware, demo/game edge cases). GEOBENCH
is plain firmware + AMSDOS + Mode 1 code, so that edge doesn't matter here. 1984
wins on workflow: it's ours (dogfooding), and `--screenshot-at` makes automated
visual checks trivial. cap32 stays as a second opinion.

### Mouse / pointer

The default pointer is an **AMX-style mouse read via the joystick port** — no
expansion hardware required, so it works on a bare CPC and on *both* emulators
(both emulate the joystick). During development the host's mouse or a gamepad
drives the emulated joystick directions. A SYMBiFACE II / Cyboard PS/2 mouse is
a later add behind the input layer for machines with the board.

## Running a build

cap32 and 1984 both accept a `.dsk` slot file. Two quick paths:

```bash
# Inject a raw binary at its org address (fast iteration, no disk needed):
../caprice32/cap32 -i bin/PROG.BIN -o 0x4000

# Or boot a disk image and autorun a file:
../caprice32/cap32 --autocmd 'RUN"PROG' build/geobench.dsk
```

1984 has its own invocation (see `../1984/INSTALL.md` / `1984.conf.example`);
the build artifacts (`.bin` / `.dsk`) are the same.

## Icon and font sets (GEOBENCH.CFG)

`GEOBENCH.CFG` selects a named set: `ICONS=<name>` loads `<name>.IST`, `FONT=<name>`
loads `<name>.FNT`; both fall back to `DEFAULT` if absent. Sets ship as files on
the disk (and are `incbin`/`save`d onto `build/gbkern.dsk` in `kernel/gbkern.asm`).

Build a set, then package it (add an `incbin` + a `save "<NAME>.<EXT>",...,DSK`
line in `gbkern.asm`, and copy it to the IDE image in the deploy step):

- **Font** (`.FNT`): from an 8×8 `.asm` font source — `tools/packfont.py
  build/NAME.FNT lib/font.asm` (ships `CLASSIC.FNT`, the 8×8 ROM font). The 6×8
  `DEFAULT.FNT` is generated procedurally by `tools/genfont.py`.
- **Icons** (`.IST`): each icon is a 32×32 PNG → `tools/png2cpc.py assets/x.png
  lib/icon_x.asm icon_x 32x32`, then `tools/packicons.py build/NAME.IST
  lib/icon_*.asm ...` in **slot order** (must match `ext_to_icon` in `gbkern.asm`:
  floppy ide clock trash geobench basic binary picture text folder).

  Two ways to get edited icons onto the next card:

  1. **Change the DEFAULT set** (the icons shown out of the box): edit the source
     `assets/<name>.png` in any image editor (keep the 4-colour desktop palette),
     then run **`tools/regen_icons.sh`** — it re-runs `png2cpc` for every committed
     `lib/icon_*.asm` / `assets/paint/*.asm` from its recorded source PNG + size.
     Rebuild (`tools/build_kernel.sh`) and `packicons` repacks `build/DEFAULT.IST`.
     Note: the build does **not** auto-convert `assets/` — `build/DEFAULT.IST` is a
     gitignored artifact regenerated from the committed `lib/icon_*.asm`, so a PNG
     edit only takes effect after `regen_icons.sh` updates those `.asm` files.
  2. **Ship a custom selectable set**: edit a set visually with
     `tools/iconedit.py assets/iconsets/MYSET.IST` (tracked, unlike `build/`), and
     `stage_dist.sh` / `build_ide_img.sh` copy every `assets/iconsets/*.IST` onto
     the card automatically. Select it with `ICONS=MYSET` in `GEOBENCH.CFG`. See
     `assets/iconsets/README.md`.
- **Pictures** (`.PIC`): convert a PNG to a 4-colour Mode-1 picture with
  `tools/picconv.py` — a tkinter GUI (Open / dither / width / preview / Save) or a
  CLI (`tools/picconv.py in.png out.PIC -d floyd -w 160`). `.PIC` opens in the
  Viewer and edits in PAINT; no packaging needed (it's user content, not a build
  asset).

## File line endings

Any text/data file the CPC reads must use **CR+LF** (`0x0D 0x0A`), not Unix LF.
Convert before packing onto a disk image (`unix2dos file` or `sed -i 's/$/\r/'`).
