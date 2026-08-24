# Building, deploying and running

The **Amstrad CPC** build is below; the **MSX2** build is in
[Building for MSX2](#building-for-msx2). Both share the RASM kernel + SDCC apps
and happen inside the project distrobox (RASM, SDCC, `mtools`/`dosfstools` on
`PATH`).

## Amstrad CPC

The kernel is assembled with **RASM**; the apps are compiled with **SDCC**
(`sdcc`, `sdasz80`, `makebin` on `PATH`). In practice development happens inside
the project distrobox, where those tools plus `mtools`/`dosfstools` already
exist. One script builds the whole distribution; app and module helper scripts
cache unchanged outputs, so a repeat full build does **not** rebuild every app
unnecessarily. `iDSK` is only needed to inject the convenience `GB.BAS` loader
into the floppy images; without it the floppy still boots via `RUN"GBKERN`:

```bash
bash tools/build_kernel.sh
```

The repository also has a thin top-level Makefile: `make cpc`, `make msx`,
`make pcw`, `make all`, and `make check` wrap the same scripts and static checks.
`make formref` builds only the CPC, MSX2, and PCW reusable-form reference
binaries for quick UI iteration; `make sndtest` likewise builds only the
three app-linked sound diagnostics.

Window title motifs are standalone 56-byte `.TBR` assets, while the close and
maximize pair is a standalone 50-byte `.GDT`. Both are selected independently
from each other and from the icon set. Open the defaults together in the visual
tile editor with:

```bash
make titlebar-editor
```

Every file in `assets/titlebars/` is packaged for card and MSX distributions.
The same applies to `assets/gadgets/`.
The space-constrained CPC and PCW boot floppies carry `ORIGINAL.TBR` and
`IMPROVED.TBR`, plus `ORIGINAL.GDT`; their `EXTRAS.DSK` carries the remaining
themes. Available assets appear in Settings > Title bar and Settings > Gadgets.
`TITLEBAR=ORIGINAL` and `GADGETS=ORIGINAL` are the defaults on every platform.
Legacy 106-byte combined `.TBR` files remain loadable.

The About dialog reads the release version from the root `VERSION` file and bakes
that value plus the current 12-character Git commit into `GBUI.MOD`. Override
either value for a packaged build with `VERSION=...` or `GIT_COMMIT=...`.

This stages these outputs (the staged media under `QA/` are committed, so you
can test or deploy without rebuilding first):

- **`QA/CPC/CARD/`** — the loose card distribution. Copy its contents onto an Albireo
  card or use it as the source for an M4 card/image. The card root holds the
  loader `GB.BAS`, `M4DETECT.BIN`, both kernels (`GBALB.BIN`, `GBM4.BIN`), and
  `GEOBENCH.CFG` — everything else the kernel loads at boot lives in a `GBENCH/`
  subfolder, while the complete gallery lives in root-level `PICS/` and diagnostic
  programs such as `NETTEST.APP`, `FORMREF.APP`, and `SNDTEST.APP` live in
  root-level `DIAG/`.
- **`QA/CPC/GEOBENCH.IMG`** — a ready-to-flash shared **Albireo/M4 card image**: a
  partitioned FAT16 disk the CH376 auto-detects and 1984's M4 image mode can mount.
  Built by `tools/build_card_img.sh`; a 32 MB local artifact, rebuilt every build
  and not committed.
- **`QA/CPC/Floppies/GEOBENCH.DSK`** — the bootable **Main** floppy image.
- **`QA/CPC/Floppies/COMPANION.DSK`** — the **Companion** floppy with the larger apps
  (including Telnet, WGET, Browser, Shell, Mahjong and Calculator) and extra savers for drive B.
  Browser's `BRSAVE.APP` worker and `GBIMG.MOD` renderer, Paint's `PAINT.IST`
  tool set, and the optional `HAND.SPR` cursor are kept beside those apps.
  Configurable savers carry their
  same-stem Configure companions (`XMATRIX.MOD`, `STARFLD.MOD`) on this disk;
  the shared `GBWEB.MOD` module remains on Main.
- **`QA/CPC/Floppies/EXTRAS.DSK`** — the complete `.PIC` gallery plus
  `DISKUTIL.APP`, `XROACH.SAV`, `CATCLK.SAV`, `HELIX.SAV`, `MOUNTAIN.SAV`,
  `MOUNTAIN.MOD`, and `WELCOME.TXT` on an extended 80-track,
  single-sided AMSDOS DATA disk. Its files
  retain AMSDOS headers and multi-extent
  layout, so the existing chunked picture reader can open the larger images. A
  standard 180K CF2 is too small; use a Gotek/emulator or compatible 80-track drive.

Boot with **`RUN"GB`**: the card loader `GB.BAS` loads `M4DETECT.BIN`, probes for
M4ROM's RSX table, and then `RUN"`s `GBM4` on M4 hardware or `GBALB` otherwise. On
the floppy, `GB.BAS` still `RUN"`s `GBKERN`. The selected kernel then drives the
card backend or falls back to the AMSDOS floppy path. On a floppy you can also
`RUN"GBKERN` directly.

```bash
1984 --memory=128 --disk-a=QA/CPC/Floppies/GEOBENCH.DSK --autostart=GB
```

`tools/build_capp.sh <app_dir> <out.RAW>` builds a single C app against `libgb`
if you just want to iterate on one.

## Optional: the GEOBENCH ROM

`tools/build_rom.sh` builds a 16K upper ROM — `rom/GBALB.ROM` (Albireo) is the shipped
one (`rom/GEOBENCH.ROM` is the archived IDE variant) — that does two things:

- **Offloads the low-level drivers.** The screen-independent storage drivers (FAT
  read/write, the AMSDOS floppy reader, the IDE backend and the CH376/Albireo backend) run
  from the ROM instead of the resident kernel, freeing `#8000` RAM. The resident kernel keeps
  thin stubs that page the ROM in and call it; build that variant with
  `EXTRA_RASM="-DGB_ROM_REQ=1"`. Without the ROM the plain kernel runs every driver resident,
  so the ROM is **optional**.
- **Announces GEOBENCH at boot.** It is a standard CPC **background ROM**, so the firmware
  prints a `GEOBENCH <commit>` banner at cold boot (like M4 or SymbOS) before BASIC's prompt.

Flash the matching ROM into a free upper-ROM slot.

## Building for MSX2

The same kernel and app sources cross-build for the MSX2 (`-DPLATFORM_MSX` for the
kernel, `-DGB_MSX2` for the apps). See [The MSX2 target](MSX2.md) for the runtime
design; this is the build.

```bash
bash tools/fetch_msx_deps.sh       # one-time: Nextor, UNAPINET + NMS 8250 ROMs
bash tools/build_kernel_msx.sh     # the whole MSX2 distribution
tools/run_msx.sh                   # boot it in openMSX (interactive)
tools/run_msx.sh QA/MSX/Floppies/GEOBENCH.DSK
MSX_SHOTS="25 40" tools/run_msx.sh # headless: screenshots into build/msx/
```

Browser and Telnet are included in the MSX distribution and use TCP/IP UNAPI.
The dependency fetcher places openMSXnet v0.9.7's guest TSR in ignored
`QA/MSXDEPS/`; the standard MSX build stages it automatically. Enable the
matching emulator extension while running:

```bash
make msx
OPENMSX=/path/to/openmsxnet/openmsx \
OPENMSX_SYSTEM_DATA=/path/to/openmsxnet/share \
tools/run_msx.sh
```

`MSX_UNAPI_TSR=/path/to/UNAPINET.COM` overrides the fetched driver, while an
explicitly empty `MSX_UNAPI_TSR=` builds a network-free image. openMSXnet does
not work without both the guest TSR and emulator extension. `run_msx.sh` enables
the `unapinet` extension by default and automatically uses a bundle installed at
`QA/MSXDEPS/openmsxnet/`; use `OPENMSXNET_HOME` for another location, or the
explicit `OPENMSX` variables shown above. If the bundle needs libraries absent
from the host (notably Tcl 8.6 on current Fedora), the launcher automatically
uses `my-distrobox`; `MSX_DISTROBOX` overrides its name. Set `MSX_UNAPI=0` for a
non-networked run. The standalone staged TSR and generated card image remain
ignored, while the released system floppy embeds the MIT-licensed TSR and its
`UNAPI.TXT` notice. See [The MSX2 target](MSX2.md#browser-and-telnet-networking)
for current UNAPI implementation coverage.

`build_kernel_msx.sh` produces:

- **`QA/MSX/`** — the loose MSX distribution (committed): the `GBMSX.COM`
  mode selector, `GBMSX6.COM`, `GBMSX7.COM`, an `AUTOEXEC.BAT` that runs the
  selector, `GEOBENCH.CFG`, the `GBENCH/` system folder
  (fonts/icons/cursor/modules/apps/savers), the complete gallery in `PICS/`, and
  development diagnostics in `DIAG/`.
- **`QA/GBMSX.IMG`** — a bootable 32 MB **FAT16 hard-disk image** (a local
  artifact, git-ignored like the CPC card image). `tools/build_msx_img.sh` fills
  it from `QA/MSX` plus the Nextor system files, so Nextor's Sunrise IDE driver
  boots it straight to the desktop.
- **`QA/MSX/Floppies/GEOBENCH.DSK`** — the bootable 720K FAT12 system floppy,
  with the selectors, complete `GBENCH/` and `DIAG/` trees, `NEXTOR.SYS`,
  `COMMAND2.COM`, `UNAPINET.COM`, both third-party distribution notices, and
  `PICS/LOGO.PIC` so the default wallpaper is available without the extras disk.
- **`QA/MSX/Floppies/EXTRAS.DSK`** — the second 720K floppy containing the
  complete `PICS/` gallery.

**Assets are packaged automatically.** Portable `.PIC` files are canonical GBPC Mode-1 and
are copied byte-for-byte into root-level `PICS/`; the kernel translates pictures
while displaying them on MSX as on other targets. Mode-7 sixteen-colour files
from the same asset directory are added only to the MSX `PICS/` gallery. Icon
sets (`.IST`) are also now
stored in canonical Mode-1 bytes and are decoded when loaded by the MSX kernel.
Backdrop tiles (`.BDP`) use the same model: one canonical file is converted once
when selected on MSX2/PCW; CPC uses those Mode-1 bytes directly in the desktop.
Hardware sprites and bootsplash bitmaps remain target-specific.
[Portable GEOBENCH picture format](PIC_FORMAT.md) explains why this works.
System assets stay in `GBENCH/`. The mouse pointer is a V9938 hardware sprite: a
hand-edited **`assets/thinner.SPR`** is staged as the MSX
`GBENCH/DEFAULT.SPR`. Edit it with
`tools/iconedit.py assets/thinner.SPR`; its 66-byte size lets the editor detect
the MSX2 sprite format automatically.

### Deploying

Copy the contents of **`QA/MSX/`** onto storage your MSX-DOS 2 / Nextor setup
mounts (an SD card, IDE disk, …) and run **`GBMSX.COM`** (the `AUTOEXEC.BAT` runs
it for you) — or write the whole `QA/GBMSX.IMG` to the device. It needs **MSX-DOS
2** (mapper support); a bare MSX2 with only Disk BASIC / MSX-DOS 1 won't run it.
`GBMSX.COM` reads `MSXMODE=6|7` from `GEOBENCH.CFG` and loads the matching
mode-specific binary. System Settings updates that key for the next boot.

### Floppy distribution

`tools/build_msx_floppy.sh` assembles two standard **720K FAT12** images under
`QA/MSX/Floppies/`. `GEOBENCH.DSK` contains the complete desktop and development
diagnostics; `EXTRAS.DSK` contains the picture gallery. FAT12 cluster overhead
makes this split necessary even though the loose files are close to 720K in
total. Run `make msx-floppies` to rebuild only these images and
`python3 tools/check_msx_floppies.py` to validate their geometry and contents.

The system disk includes the open-source `NEXTOR.SYS`, `COMMAND2.COM`, and the
required Nextor notice. It also includes openMSXnet's MIT-licensed
`UNAPINET.COM`, starts it before `GBMSX`, and carries its license as
`UNAPI.TXT`. It intentionally does **not** include proprietary
`MSXDOS2.SYS`. As shipped, the disk must therefore find a **Nextor kernel ROM**
in built-in firmware or a cartridge. A Sunrise IDE Nextor cartridge is one such
provider, but the floppy continues to use the machine's normal FDC; IDE storage
is not required by the disk format or by GEOBENCH. GEOBENCH itself also runs on
MSX-DOS 2, but users of a DOS2 kernel must supply their own licensed
`MSXDOS2.SYS` instead of the included `NEXTOR.SYS`.

The floppy builder obtains `UNAPINET.COM` from the staged tree or
`QA/MSXDEPS/`, so clean release staging retains networking. An explicitly empty
`MSX_UNAPI_TSR=` still produces a network-free local disk; the committed release
validator requires the network-enabled form.

For openMSX:

```bash
tools/run_msx.sh QA/MSX/Floppies/GEOBENCH.DSK
```

The default Philips NMS 8250 definition exposes one drive, so swap
`EXTRAS.DSK` into drive A after boot. With a two-drive machine definition, set
`MSX_MACHINE` appropriately and pass
`MSX_DISKB=QA/MSX/Floppies/EXTRAS.DSK`.
