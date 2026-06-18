# Custom icon sets

Drop tracked `.IST` icon-set files here and they are **automatically copied onto
every card** (the unified `QA/GEOBENCH.IMG` via `stage_dist.sh`, and the IDE
image via `build_ide_img.sh`). Unlike `build/DEFAULT.IST` — a gitignored build
artifact that `packicons.py` regenerates from `lib/icon_*.asm` on every build —
files here are version-controlled and the build never overwrites them.

## Make / edit a set

```
tools/iconedit.py assets/iconsets/MYSET.IST     # tkinter editor (.IST and .SPR)
```

`iconedit.py` opens an existing `.IST` or starts a new one; draw, then Save.

## Use it on the desktop

Select the set in the boot config `GEOBENCH.CFG` (written by `stage_dist.sh`):

```
ICONS=MYSET
```

The kernel loads `MYSET.IST` from the card at boot and falls back to
`DEFAULT.IST` if it is missing.

## Changing the DEFAULT icons instead

If you want to change the icons shown out of the box (not a separate selectable
set), edit the source PNGs and regenerate the committed asm:

```
# edit assets/<name>.png in any image editor (32x32, the desktop 4-colour palette)
tools/regen_icons.sh            # png2cpc every assets/*.png -> lib/icon_*.asm
tools/build_kernel.sh           # repacks build/DEFAULT.IST + the card
```

See `docs/DEVELOPMENT.md` for the full icon pipeline.
