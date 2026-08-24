# The MSX2 target

The same OS, cross-built for the MSX2 (issue #287). It is **one source tree, two
platforms**: the resident kernel assembles from the same `kernel/` sources under
`-DPLATFORM_MSX`, and the apps compile from the same `main.c` files under
`-DGB_MSX2` — the platform differences live in the kernel's video/input/storage
back-ends, not in the apps.

GEOBENCH on real MSX2 hardware (a 1chipMSX / "1chipbook", 4080K detected): the
desktop with the File Manager, the Viewer showing `PENGUIN.PIC`, and the analog
Clock — all co-resident under the kernel window manager on a V9938.

![GEOBENCH on MSX2 hardware](../screenshots/msx_1chipbook.jpg)

- **Runs under MSX-DOS 2 / Nextor.** `GBMSX.COM` is a small selector that reads
  `MSXMODE=` and chain-loads `GBMSX6.COM` or `GBMSX7.COM`; no custom boot ROM is
  required. Storage goes through BDOS file calls, so anything Nextor mounts
  (Sunrise IDE, SD interfaces, …) works.
- **Video:** V9938 **Screen 6** (512x212, 4 colours) is the compatibility mode.
  **Screen 7** keeps the same 512x212 desktop geometry and four-pen UI while
  enabling sixteen-colour pictures in Viewer and as desktop wallpaper. Both use
  a **hardware sprite pointer** and VDP-command drawing (`GB_SETINK` palette
  mapping, `GB_LINE`).
- **Input:** joystick / keyboard pointer, plus the standard **MSX mouse** in
  joystick port 1 (GTPAD). The clock is 50/60 Hz aware.
- **Ported so far:** Desktop, File Manager, Notepad, Settings, ICONED, Paint,
  Viewer (with portable `.PIC` files translated while drawing), Shell, XAOS,
  Calculator, Mahjong,
  Clock, Browser, Telnet, and **all 16 screensavers**. NETTEST and WGET are not
  built for MSX yet.
- **Assets:** icon sets and backdrop tiles stay in canonical Mode-1 bytes and are
  decoded by either MSX video backend. Portable four-colour pictures use the same
  [GBPC v2 format](PIC_FORMAT.md) on every platform. Screen 7 additionally accepts
  mode-7 sixteen-colour `.PIC` files in Viewer and as wallpaper. MSX pictures live
  under root-level `PICS/`; low-level diagnostics such as `GBSPIKE.COM` are
  under `DIAG/`, while app diagnostics such as `SNDTEST.APP` live in `GBENCH/`.

## Selecting 4 or 16 colours

Open **System > Settings**, select **Video mode**, and choose **4 colors** (Screen 6)
or **16 colors** (Screen 7). Settings writes `MSXMODE=6` or `MSXMODE=7` to
`GEOBENCH.CFG`; the new mode takes effect at the next GEOBENCH boot. A missing or
invalid key falls back to Screen 6. Newly built images explicitly default to
Screen 7.

Keep `GBMSX.COM`, `GBMSX6.COM`, and `GBMSX7.COM` together in the root of the boot
drive. For diagnostics, either mode-specific executable can also be run directly.
Only the selected kernel becomes resident, so the option costs disk space rather
than kernel headroom.

## Mouse input

Newly built MSX images set `MSXMOUSE=TRUE` in `GEOBENCH.CFG`. This makes the
`GBMSX.COM` selector pass `/M` to the selected Screen 6/7 loader, which enables
the BIOS GTPAD driver for a standard MSX mouse in joystick port 1. Set
`MSXMOUSE=FALSE` to retain keyboard/joystick-only input. A manual
`GBMSX /M`, `GBMSX6 /M`, or `GBMSX7 /M` also enables the mouse for that run.
The driver ignores both known offset pairs returned by an enabled but unplugged
port (`FF/FF` on hardware and `01/01` from some BIOS/emulator combinations), so
a missing mouse does not pull the pointer into a corner. Keyboard and joystick
input remain active as a fallback.

## App-linked sound

Apps may opt into the MSX PSG backend with `SOUND=1`. It uses channel A for
chromatic tone or noise while preserving the current channel B/C mixer state.
Timing and sequencing stay in the app's frame handler, so the resident Screen 6
and Screen 7 kernels gain no code or ABI entries. `GBENCH/SNDTEST.APP` provides
Scale, Noise, and Stop checks for real hardware and emulators.

`tools/run_msx.sh` attaches openMSX's `mouse` pluggable to `joyporta` by
default. Set `MSX_MOUSE=0` to leave openMSX's normal joystick attachment in
place. In the 1983 emulator, select **General > Joy Port A > Mouse** (or set
`joy_port_a = mouse` in `1983.conf`), then click inside the emulator window to
capture relative mouse input; `Ctrl+Enter` releases it.

Build and test in **openMSX** (emulating a Philips NMS 8250 with the Sunrise IDE
Nextor extension and a 512K RAM expansion):

```bash
bash tools/fetch_msx_deps.sh       # one-time: Nextor system files + NMS 8250 ROMs
bash tools/build_kernel_msx.sh     # selector + both kernels + staged/bootable images
tools/run_msx.sh                   # interactive session
tools/run_msx.sh QA/MSX/Floppies/GEOBENCH.DSK
MSX_SHOTS="25 40" tools/run_msx.sh # headless: screenshots into build/msx/
```

`run_msx.sh` uses a native `openmsx` from `$PATH`, the Flatpak
(`org.openmsx.openMSX`) as a fallback, or an explicit `OPENMSX="…"` override.

## Floppy distribution

The build creates two standard 720K FAT12 images in `QA/MSX/Floppies/`:

- `GEOBENCH.DSK` boots to GEOBENCH and contains the complete system and
  diagnostics. It includes `UNAPINET.COM`, starts it before GEOBENCH, and keeps
  `PICS/LOGO.PIC` on the system disk for the default wallpaper.
- `EXTRAS.DSK` contains the `PICS/` gallery. Mount it as drive B when available,
  or swap it into drive A after the desktop has booted.

The system floppy carries `NEXTOR.SYS` and `COMMAND2.COM`, but the Nextor kernel
itself lives in ROM supplied by the machine or a cartridge. A Sunrise IDE Nextor
ROM is used by the openMSX harness because it is a readily available compatible
kernel provider; the floppy is still read through the NMS 8250 FDC and does not
require IDE storage. RainBIOS likewise provides the BIOS and floppy firmware,
not the Nextor kernel, so a Nextor cartridge remains necessary when testing that
firmware in 1983. GEOBENCH also runs under MSX-DOS 2, but this redistributable
image does not include the separately licensed `MSXDOS2.SYS` required by a
DOS2-ROM-only setup.

The Nextor components are distributable for non-commercial use when their
copyright and permission notice is included. The exact notice is stored in
[`licenses/NEXTOR.md`](licenses/NEXTOR.md) and copied to the system floppy as
`NEXTOR.TXT`. Proprietary `MSXDOS2.SYS` is not included.

The system floppy also carries openMSXnet's guest TSR under the MIT License.
Its notice is stored in [`licenses/OPENMSXNET.md`](licenses/OPENMSXNET.md) and
copied as `UNAPI.TXT`.

## DOS drive letters and media icons

GEOBENCH does not assign fixed meanings to A:, B:, or C: on MSX. At startup it
reads the drive letters assigned by MSX-DOS/Nextor, keeps the launch volume as
the first desktop drive, and exposes up to two additional mounted, accessible
volumes in letter order. Under Nextor, empty SD slots and floppy units without
readable media are omitted. The desktop label, File Manager title, Shell path,
Settings media picker, and save destinations all use those actual DOS letters.

Under Nextor, GEOBENCH also asks which driver owns each letter. The driver name
and type select the desktop icon independently of the letter: SD Mapper volumes
use the SD-card icon, Sunrise IDE volumes use the IDE icon, and legacy drive-based
DOS devices use the floppy icon. Unknown device-based drivers use the generic
storage icon. Plain MSX-DOS 2 falls back to its assigned-drive vector; A:/B: are
treated as floppies and later letters as generic storage.

The current desktop and per-drive state have three slots, so only three accessible
DOS volumes are shown. The mapping is captured when GEOBENCH starts; restart it
after changing a Nextor drive assignment or inserting/removing media.

## Browser and Telnet networking

The MSX Browser and Telnet apps discover a standard **TCP/IP UNAPI**
implementation at runtime. The transport is linked into those app banks; it
does not consume resident kernel headroom. This initial version supports UNAPI
implementations in mapped RAM (through the standard RAM helper) or page 3.
Page-1 ROM-slot implementations are not supported yet. A mapped implementation
uses one mapper segment of its own, so the usual 512K expansion remains the
recommended setup when networking and several windows are used together.

[openMSXnet](https://github.com/antxiko/openMSXnet) is the initial emulator
target. It needs both its custom openMSX build with the `unapinet` extension and
its `UNAPINET.COM` TSR. `tools/fetch_msx_deps.sh` downloads the official v0.9.7
guest driver into the ignored `QA/MSXDEPS/` directory, and the normal MSX build
stages and runs it before GEOBENCH:

```bash
bash tools/fetch_msx_deps.sh
bash tools/build_kernel_msx.sh
```

`MSX_UNAPI_TSR=/path/to/UNAPINET.COM` selects another local driver binary;
`MSX_UNAPI_TSR=` explicitly builds an image without the TSR.

Then run that image with the openMSXnet executable and data directory:

```bash
OPENMSX=/path/to/openmsxnet/openmsx \
OPENMSX_SYSTEM_DATA=/path/to/openmsxnet/share \
tools/run_msx.sh
```

For openMSXnet, both pieces are mandatory: `UNAPINET.COM` publishes the UNAPI
implementation inside MSX-DOS, while the custom emulator extension provides its
host-side sockets. `run_msx.sh` enables that extension by default; use
`MSX_UNAPI=0` for an explicitly non-networked run. A bundle placed at
`QA/MSXDEPS/openmsxnet/` is selected automatically; `OPENMSXNET_HOME` selects a
different location. When its libraries are not available on the host, the
launcher runs it through `my-distrobox` (override with `MSX_DISTROBOX`). The
standalone fetched executable and generated card image remain untracked. The
released `GEOBENCH.DSK`, however, embeds the v0.9.7 guest TSR and its MIT notice
so floppy networking is ready when the host-side extension is enabled. On an
existing Nextor installation, copy `UNAPINET.COM` to the boot drive and run it
before `GBMSX.COM`; another compatible mapped-RAM or page-3 TCP/IP UNAPI
implementation may be used instead. Browser and Telnet report a
network-initialisation error when discovery fails. Browser currently supports
plain HTTP only. Its bounded HTML subset includes simple tables: up to three
cells are shown across a centered row, wider source rows reflow, and linked cell
images remain clickable. It does not implement CSS table layout or spanning
cells.

When Browser starts under Screen 7, it reads the live BIOS `SCRMOD` work-area
byte and tries to reserve a second app-pool page in addition to its normal
history/cache page. If both checks succeed, every page and inline-image request
includes `X-GBPC: 7,1`: mode 7 is preferred and portable mode 1 remains an
accepted fallback. The mode-7 image occupies the dedicated page and may be up
to 160x96 pixels (7,694 bytes including its GBPC header). Screen 6 and a
low-memory Screen-7 session omit the capability header and retain the existing
four-colour image path. The dedicated page is returned when Browser closes.
