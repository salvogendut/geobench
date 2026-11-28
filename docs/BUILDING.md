# Building, deploying and running

The **Amstrad CPC** build is below; the **MSX2** build is in
[Building for MSX2](#building-for-msx2), and PCW-specific media details are in
[The PCW target](PCW.md#what-ships-where). All three share the RASM kernel +
SDCC apps and are normally built inside the project distrobox (RASM, SDCC,
`mtools`/`dosfstools` on `PATH`). Full distribution builds also require sibling
`GB-PAINT` and `GB-BASIC` checkouts; override their locations with
`GB_PAINT_DIR=` and `GB_BASIC_DIR=`.

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
`make pcw`, and `make all` build the default preemptive distributions, while
`make cpc-cooperative`, `make msx-cooperative`, and `make pcw-cooperative`
retain scheduler-free regression builds. `make check` runs the static and host
tests. Direct `tools/build_kernel*.sh` invocations are preemptive by default;
set `PREEMPTIVE=0` explicitly for a cooperative build.
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
The space-constrained CPC and PCW boot floppies carry the default
`ORIGINAL.TBR` plus `ORIGINAL.GDT`; their `EXTRAS.DSK` carries the remaining
themes. Available assets appear in Settings > Title bar and Settings > Gadgets.
`TITLEBAR=ORIGINAL` and `GADGETS=ORIGINAL` are the defaults on every platform.
Legacy 106-byte combined `.TBR` files remain loadable.

The About dialog reads the release version from the root `VERSION` file and bakes
that value plus the current 12-character Git commit into `GBUI.MOD`; installed
RAM is read at runtime. Override either build value for a packaged build with
`VERSION=...` or `GIT_COMMIT=...`.

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
  Built by `tools/build_card_img.sh`; a 32 MB local artifact by default,
  rebuilt every build and not committed. Pass a size in megabytes as the third
  argument to build a different 4-2048 MB image.
- **`QA/CPC/Floppies/GEOBENCH.DSK`** — the bootable **Main** floppy image.
- **`QA/CPC/Floppies/COMPANION.DSK`** — the **Companion** floppy with the larger apps
  (including Telnet, WGET, Browser, Shell, Mahjong and Calculator) and extra savers for drive B.
  Browser's `BRSAVE.APP` worker and `GBIMG.MOD` renderer, Paint's `PAINT.IST`
  tool set, and the optional `HAND.SPR` cursor are kept beside those apps.
  Configurable savers carry their
  same-stem Configure companions (`XMATRIX.MOD`, `STARFLD.MOD`) on this disk;
  the shared `GBWEB.MOD` module remains on Main.
- **`QA/CPC/Floppies/EXTRAS.DSK`** — the complete `.PIC` gallery plus
  `DISKUTIL.APP`, `XROACH.SAV`, `CATCLK.SAV`, `HELIX.SAV`, `FOREST.SAV`,
  `MOUNTAIN.SAV`, `MOUNTAIN.MOD`, and `WELCOME.TXT` on an extended 80-track,
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

## Building for PCW

```bash
bash tools/build_kernel_pcw.sh
# or: make pcw
```

This rebuilds `QA/PCW/Floppies/GEOBENCH.DSK`, `COMPANION.DSK`, and the 720K
`EXTRAS.DSK`. The build imports Paint and BASIC from their sibling repositories.
Use `../1985/1985` with the current 1985 configuration for emulator testing;
see [PCW.md](PCW.md) for boot, disk geometry, serial networking, and deployment
details.

## Optional: the GEOBENCH ROM

`tools/build_rom.sh` builds an optional legacy 16K upper ROM:
`rom/GBALB.ROM` for Albireo or the archived IDE
`rom/GEOBENCH.ROM`. Neither is required or included in normal release media.
The proof-of-concept does two things:

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

- **`QA/MSX/CARD/`** — the loose MSX distribution (committed): the `GBMSX.COM`
  mode selector, `GBMSX6.COM`, `GBMSX7.COM`, an `AUTOEXEC.BAT` that runs the
  selector, `GEOBENCH.CFG`, the `GBENCH/` system folder
  (fonts/icons/cursor/modules/apps/savers), the complete gallery in `PICS/`, and
  development diagnostics in `DIAG/`.
- **`QA/MSX/GBMSX.IMG`** — a bootable, 32 MB by default **FAT16 hard-disk
  image** (a local artifact, git-ignored like the CPC card image).
  `tools/build_msx_img.sh` fills it from `QA/MSX/CARD` plus the Nextor system
  files, so Nextor's Sunrise IDE driver boots it straight to the desktop. Pass
  a size in megabytes as the third argument to build a different 4-2048 MB
  image.
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

Copy the contents of **`QA/MSX/CARD/`** onto storage your MSX-DOS 2 / Nextor setup
mounts (an SD card, IDE disk, …) and run **`GBMSX.COM`** (the `AUTOEXEC.BAT` runs
it for you) — or write the whole `QA/MSX/GBMSX.IMG` to the device. It needs **MSX-DOS
2** (mapper support); a bare MSX2 with only Disk BASIC / MSX-DOS 1 won't run it.
`GBMSX.COM` reads `MSXMODE=6|7` from `GEOBENCH.CFG` and loads the matching
mode-specific binary. System Settings updates that key for the next boot.

When testing the SD Mapper card in 1983, SD images are read-only by default.
To test saves, use a working copy of `QA/MSX/GBMSX.IMG` with
`--sd-a /path/to/copy.img --sd-mode read-write`. A protected card should
still let you dismiss a save error and continue using the desktop.

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

## Experimental AgonLight2 target

For the remaining desktop, runtime and application work, see the [Agon port plan](AGON_PORT_PLAN.md).

From the checkout root, run `make run-agon`. This builds a standalone MOS
program, then launches **Fab Agon Emulator** (Flatpak
`io.github.tomm.fab_agon_emulator`) with a writable SD copy in
`build/agon-sd/`. Settings can persist between runs without changing the
distribution in `QA/AGON/CARD/`. At the MOS prompt enter `geobench`.
The desktop uses 320x240 (4:3) video. Click the SD card icon or its **SD:**
caption to open the card at its root. With no mouse, use the four
**cursor keys** to move the pointer and **Space** to left-click. Tap an arrow
to move one pixel, hold it for continuous motion, or hold two arrows to move
diagonally. **Shift+arrow** takes an eight-pixel step. This works
without an input setting, including on Calculator's buttons. **F** also opens
File Manager. Drag a title bar to move File Manager, Calculator or picture
preview. Drag the bottom-right square to resize File Manager or picture preview;
Calculator's keypad is fixed-size. On the keyboard, position the pointer,
hold **Space**, use the arrows, then release **Space** to place the outline.
Click an entry to select it; double-click to open. Click **View**
in the title bar or press **V** to switch
between icon and list views. Open **PICS** and double-click a picture such as
`1984.PIC` (four pens) or `46416.PIC` (sixteen pens). Viewer opens the
picture at actual size. **Esc** or **X** returns
to the same folder and selection. Alternatively, open
**GBENCH**, then **CALC.APP**, and type `6x7=` to calculate 42. **Esc** returns
to the same directory. Double-click **..** to go up; **Esc** or the window's
**X** closes File Manager.

**Experimental native 640x480 desktop:** run `make run-agon-vga` and enter
`gbvga` at MOS. For hardware, copy the **contents** of `build/agon-vga-sd/`
(generated by `make agon-vga`) to a spare SD card. Alternatively, use the
matching `QA/AGON/CARD/` and add `QA/AGON/VGA/GBVGA.BIN` to its root.
The two launchers share the same apps: `gbvga` requests 640x480, 16 colours;
`geobench` still requests 320x240. No saved preference is changed.
VGA gives windows more room to move, not doubled pixels: fonts, artwork
and controls keep their native size. Individual guest windows still fit
the existing 320x240 app coordinate space; they do not maximize to 640x480.
To check the port, move Files to the bottom-right, open a folder there,
then close it and check the exposed wallpaper. Run `make test-agon-vga`
for the emulator regression (same host options as `test-agon`).

In Files, use **File > New window** to open another view at the SD root.
Each keeps its own folder, scroll position and selection when you switch.
**New folder** and **Rename** accept an 8.3 name. To copy a file, select
it once, choose **Copy**, switch to the destination folder or window, and
choose **Paste**. **Esc** or the **Cancel** button stops an active copy.
Paste never overwrites an existing file. Use **M** to open the menu from
the keyboard, or **O/N/R/C/P** for New window/New folder/Rename/Copy/Paste.

As a keyboard alternative, select a `.PIC` in Files and press **I** to open Viewer.
Use the boxed **+** and **-** or the keyboard to choose half, normal,
double or quadruple size. Use the left and bottom scrollbars to pan;
with the keyboard pointer, **Space** clicks the track. Each picture window
keeps its own zoom and scroll position. **File > Open** changes the picture
in that window. You can also launch `GBENCH/VIEWER.APP` directly.
For a fit-to-window preview instead, select the picture in Files and use
**File > Quick preview** or press **Q**.

Double-click `GBENCH/WELCOME.TXT` to open Notepad. **Space** types a space
while editing, even if the pointer rests over a menu. Use **Shift+Space** or
the mouse to click. In menus, dialogs and Files, **Space** still clicks.
**File > Open** and **File > Save As** start in the document's folder. Click
a numbered row (or press its number); brackets denote folders. **Backspace**
goes up, **-** and **+** change pages, and **Esc** cancels. For Save As,
choose **Use this folder** or press **Enter**, then enter an 8.3 filename
(for example `TEST.TXT`). **Save** updates that copy. Canceling either
prompt does not change the save target or File Manager's directory.
**File > Exit** closes Notepad, not GEOBENCH. A clean document offers Exit
or Cancel; with unsaved edits choose Save, Don't Save or Cancel. If the save
fails, the editor stays open with the edits intact.
Notepad accepts up to 4096 bytes. For board tests use a spare writable card.
If a failed replacement leaves `GBSAVE.BAK` or `GBSAVE.TMP`, inspect it in
MOS before removing it; the editor won't overwrite a recovery file.

Open **GBENCH/XAOS.APP** to explore the Mandelbrot set. Click to recenter,
use **+** or **-** to zoom and **R** to reset. A render continues while you
browse Files (**Ctrl+W** to switch). **File > Save As** saves a small
`.TXT` definition: `XAOS`, the two signed centre coordinates, then the
positive step, one decimal integer per line after the header (Q4.12 units).
Use **File > Open** inside XAOS to resume it; double-clicking a text file in
Files still opens Notepad. **File > Export PIC** writes the completed 80x56
four-pen image as a portable mode-1 GBPC v2 picture. Give it an unused name;
export doesn't overwrite files or change where **Save** writes the definition.
Double-click the exported `.PIC` in Files to view it.
On the desktop **C** is a Calculator shortcut, **W** toggles the wallpaper,
and **Esc** returns to MOS.
Click **System** at the top left for **Settings**, **Clock**, or **Run saver**.
The menu is also available over File Manager and its guests. Click the
top-right time or desktop Clock icon to open the analog face. Both clocks
read the MOS RTC; if it is unavailable the bar shows `--:--`.
Desktop shortcuts are **S** (Settings), **T** (Clock) and **V** (run saver).
In Settings, click a value to cycle it. **Preview** runs the edited saver;
**Cancel** discards edits. **Save** writes just `/GBENCH/AGON.CFG`. A read-only
card still boots and can preview savers, but cannot save preferences.
The defaults are Squares after two idle minutes and no seconds hand. Squares
and Blank are built into this port; it cannot load CPC/MSX/PCW `.SAV` files.
Choose an Off timeout to disable automatic activation. A key, click, or pointer movement
wakes the saver without passing the wake action to the app underneath.
Both savers check that the VDP can restore the display before painting over
it. If it cannot, the current desktop and windows stay visible.
Try `dir` or launch it a second time. No changes to the emulator's normal SD
card or startup scripts are needed.

`make agon` stages `QA/AGON/CARD/GEOBENCH.BIN`, the sample `PICS/` directory,
and `GBENCH/{AGON.CAT,DEFAULT.IST,CALC.APP,FILEMGR.APP,NOTEPAD.APP,PICVIEW.APP,VIEWER.APP,XAOS.APP,WELCOME.TXT}`.
When updating a physical SD, copy the card contents together. The launcher
checks each executable against the Agon catalogue's length and CRC before
entering it; changing an app requires updating the catalogue, not rebuilding
the resident kernel. Calculator is byte-for-byte the
shipped CPC binary, not a reimplementation. This target uses RASM and SDCC and is not
part of the default CPC/MSX/PCW distribution build. The test baseline is
Fab Agon Emulator 1.2.6 with its default Platform firmware; the graphics
code requires the [buffer-backed bitmap commands](https://agonplatform.github.io/agon-docs/vdp/Bitmaps-API/)
(VDP 2.2 or later). The browser uses the
[FatFS directory calls in MOS 2.2 or later](https://agonplatform.github.io/agon-docs/mos/API/#0x91-ffs_dopen).
Desktop startup and readable File Manager titles and filenames have been
confirmed on a physical AgonLight2. Picture preview still needs a board test.

Up to three managed guests, including multiple Files windows, can remain open.
Click an exposed part of a window to raise it, or use **Ctrl+W** to cycle.
The focused app receives input and frame callbacks. The workspace copies
16 KiB guest pages on switches. UI callbacks remain cooperative. Applications
keep their own variables, window geometry and document paths. File Manager
uses the shared app source and cached VDP bitmaps from the portable icon set.
The optional read-only `PICVIEW.APP` is the Quick preview action:
it accepts GBPC v2 modes 1, 6 and 7 up to 512x512, maps their pens to the
active Agon palette, and fits the image to the window (initially 240x144).
Moving the preview reuses its bitmap; resizing refits the source. It has no zoom or
editing controls. The other supported document association is `.TXT`.
The resident jump table at `0x8000` handles window registration, drawing with
GEOBENCH's 6x8 font, keyboard input, pointer clicks and close. Closing the window restores
the previous desktop from the cached VDP bitmap; quitting restores the MOS mode.
File Manager uses 8.3 names. **B** on the desktop still opens the compact MOS
browser for long-name-only files. Neither browser changes the MOS working
directory. The compact browser is fixed in place and read-only. Managed
Files can copy individual files, not directory trees; deleting files and
dragging them between windows are not implemented yet. General preemptive
scheduling is not enabled on Agon.

For an automated check, `make test-agon` needs the Flatpak, Xvfb, xdotool,
ImageMagick and Python with Pillow. It uses a private display and a temporary
copy of the staged card. On the development machine those X11 tools are in
`my-distrobox`; use
`distrobox enter my-distrobox -- make test-agon AGON_TEST_HOST=distrobox-host-exec`.

### Optional Agon timer diagnostic

`make run-agon-taskdiag` launches Fab with a separate card in
`build/agon-task-sd/`. At MOS run `geobench`, open the SD icon, launch
`TASKDEMO.APP` from the root, then return to Files with **Ctrl+W**.
Open `GBENCH/WELCOME.TXT` and type: the hexadecimal **Task** counter in
the top bar should change while Notepad owns the keyboard. Close the demo
window: the counter should disappear and the document should remain editable.
Exit to MOS and repeat. The automated equivalent is `make test-agon-taskdiag`
with the same `AGON_TEST_HOST` setting as above.

Close TASKDEMO before starting **GBENCH/XAOS.APP** to see its background row count. A home Mandelbrot render
starts immediately. Press **Ctrl+W**, browse Files and watch **Task** count the
completed rows in hexadecimal (`0038` = all 56). Switch back to see the
finished picture. Use **+** or **-** to zoom, click the picture to recenter,
or **R** to reset. Start a zoom, close XAOS before the counter finishes,
then reopen it. Both cards run the same XAOS app; only the diagnostic card
shows a **Task** counter and includes TASKDEMO.

Only one compute worker can own timer 5. It has a private Z80 stack; a
one-shot eZ80 PRT5 interrupt records its PC. The dispatcher preserves its
registers and restores the foreground guest page before returning to input.
Only published rows are drawn, by the foreground callback. The ISR never
calls MOS or the VDP. The previous vector is restored on completion,
cancellation, owner close or exit to MOS. The normal card installs the hook
only when requested. If timer 5 is busy, XAOS renders eight pixels per
focused frame instead. New compute guests must reserve the top 512 bytes of
their guest page and follow the contract in [gbagon.h](../lib/gb/gbagon.h).

On physical hardware first use a spare card with the normal build: Save As
from `GBENCH/WELCOME.TXT` to `/TEST.TXT`, close and reboot, then verify both
files and try switching between Files, Notepad and a picture. For the timer
test copy the **entire** diagnostic card to a separate SD. Check typing,
counter movement, background rendering while browsing, closing mid-render,
MOS return and relaunch. On the normal card also save and reopen a fractal
definition and open its exported PIC from Files.
