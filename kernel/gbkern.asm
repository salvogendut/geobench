; gbkern - GEOBENCH banked-kernel skeleton (Phase 2).
;
; Resident at GB_KERNEL (#8000), it owns the machine and the stack and never
; lives in the #4000-#7FFF window, so it can page apps and its own data buffers
; in and out. It exposes a fixed API jump table (see lib/gbapp.inc), loads a
; real app binary off disk into PAGE_APP0, and runs it there.
;
; Services with banked buffers follow the page-swap discipline: a service saves
; the caller's page (bank_cur), swaps to PAGE_DATA, touches its buffer, and
; restores the caller's page. gb_text_draw is the first real one - it renders
; the 6x8 font, whose glyphs live in PAGE_DATA. Strings come from the caller's
; page, so they are copied to a resident scratch BEFORE swapping to the font.
;
; Build: tools/build_kernel.sh   Run: 1984 --memory=128 --disk-a=build/gbkern.dsk --autostart=GBKERN

; PLATFORM_MSX (#287): -DPLATFORM_MSX=1 builds the MSX2 target - an MSX-DOS 2 /
; Nextor application with a selectable V9938 Screen 6 or Screen 7 driver,
; mapper-segment banking and BDOS storage. GBMSX.COM selects the mode-specific
; image at boot. The kernel body (WM, services, menus) is shared; the
; platform-specific pieces swap at the include sites below.
                include "../lib/gbapp.inc"
                ifdef PLATFORM_MSX
                include "../lib/msx/bios.inc"
                include "../lib/msx/glue.inc"
                else
                ifdef PLATFORM_PCW
                include "../lib/pcw/glue.inc"
                else
                include "../lib/firmware.inc"
                endif
                endif
                ifdef PLATFORM_MSX
PIC_RUNTIME_CONVERT equ 1             ; one canonical Mode-1 .PIC payload on every platform
                else
                ifdef PLATFORM_PCW
PIC_RUNTIME_CONVERT equ 1
                endif
                endif
                ifndef TITLEBAR_TILE
TITLEBAR_TILE   equ   0             ; issue #458: 16x14 repeated title-bar tile
                endif
                ifndef PREEMPTIVE
PREEMPTIVE      equ   0             ; issue #477: opt-in scheduler while it is being proven
                endif               ; normal distribution remains byte-identical
                ifndef PREEMPTIVE_CONTEXT
PREEMPTIVE_CONTEXT equ 0            ; stack-copy engine; enabled by scheduler test builds
                endif
                assert !PREEMPTIVE_CONTEXT|PREEMPTIVE,"PREEMPTIVE_CONTEXT requires PREEMPTIVE=1"

; STORAGE_ALBIREO (#104): build-time drive-0 backend select. 0 (default) = the
; SYMBiFACE IDE FAT32 backend; 1 (pass -DSTORAGE_ALBIREO=1) = the Albireo CH376
; backend. STORAGE_M4 selects the M4 board file-level backend. These are mutually
; exclusive - only one drive-0 backend is ever resident, which keeps the IDE FAT32
; core out of Albireo/M4 builds (those firmwares do FAT themselves).
                ifndef STORAGE_ALBIREO
STORAGE_ALBIREO equ   0
                endif
                ifndef STORAGE_M4             ; #174/#259: -DSTORAGE_M4=1 = M4 file backend.
STORAGE_M4      equ   0
                endif
                assert STORAGE_ALBIREO+STORAGE_M4<=1,"pick ONE drive-0 backend (albireo XOR m4 XOR ide)"
                ifndef SPIKE                  ; #130: -DSPIKE=1 builds a minimal storage spike -
SPIKE           equ   0                       ; load DESKTOP.APP right after fs_init, report, hang
                endif
                ifndef SPIKE_STAGED           ; -DSPIKE_STAGED=1 (Albireo): stage the boot loads
SPIKE_STAGED    equ   0                       ; (mount/GBCFG/DESKTOP), distinct border per stage, hang
                endif
                ifndef SPIKE_M4               ; #174: -DSPIKE_M4=1 (M4): stage present/55AA/GBCFG-load,
SPIKE_M4        equ   0                       ; distinct border per stage, hang (de-risk the transport)
                endif
                ifndef GB_ROM_REQ             ; #152: -DGB_ROM_REQ=1 routes the offloadable drivers
GB_ROM_REQ      equ   0                       ; through GEOBENCH.ROM if present. Both the IDE and the
                endif                          ; Albireo build can offload (floppy read + the backend).
                if GB_ROM_REQ
GB_ROM          equ   1
                endif
                assert !PREEMPTIVE|!GB_ROM_REQ,"preemptive CPC builds are RAM-resident and do not use GEOBENCH.ROM"
; #156: the window maximize/restore + close-X title-bar gadgets AND the banked picture buffer
; (#164: lets the Viewer open .PICs larger than its 8K in-page buffer) are resident chrome, so
; they only build where there is headroom - the GB_ROM (offloaded) kernels, the smaller Albireo
; kernel, or the M4 file-level kernel. The plain no-ROM IDE kernel is full,
; so it keeps the simple close box + the in-page picture buffer.
                ifdef PLATFORM_MSX             ; #287: the MSX2 kernel always has headroom
WM_GADGETS      equ   1                        ; (no resident storage drivers)
                else
                ifdef PLATFORM_PCW             ; #331: the fresh PCW kernel likewise
WM_GADGETS      equ   1
                else
                if GB_ROM_REQ | STORAGE_ALBIREO | STORAGE_M4
WM_GADGETS      equ   1
                endif
                endif
                endif
                ifndef BACKDROP_TILE           ; CPC resident headroom is at its guarded stack
                ifdef PLATFORM_MSX             ; limit, so its desktop draws canonical Mode-1
BACKDROP_TILE   equ   1                        ; BDPs. MSX/PCW transcode + tile in the kernel.
                else
                ifdef PLATFORM_PCW
BACKDROP_TILE   equ   1
                else
BACKDROP_TILE   equ   0
                endif
                endif
                endif
                ifdef WM_GADGETS
                if TITLEBAR_TILE
THEMED_GADGETS  equ   1
                else
THEMED_GADGETS  equ   0
                endif
                else
THEMED_GADGETS  equ   0
                endif
                include "lowram.inc"

                include "api_table.inc"

                ifdef PLATFORM_MSX
                include "boot_msx.asm"
                else
                ifdef PLATFORM_PCW
                include "boot_pcw.asm"
                else
                include "boot.asm"
                endif
                endif

; --- palette -------------------------------------------------------------
INK_DESKTOP     equ   1            ; blue  -> pen 0 (paper / backdrop)
INK_LIGHT       equ   26           ; white -> pen 1 (text)
INK_DARK        equ   0            ; black -> pen 2 (outlines / title bar)
INK_ACCENT      equ   6            ; red   -> pen 3 (accents)
pal_def         db    INK_DESKTOP,INK_LIGHT,INK_DARK,INK_ACCENT,INK_DESKTOP  ; default INKS= seed (+border)
; set_palette: apply the 4 Mode-1 pens from KCFG_INKS (INKS=, or the defaults seeded
; above). Each ink is reloaded from memory per pen so it survives SCR_SET_INK's clobber.
; Called once early (defaults) and again after the GBCFG module parses INKS= (#129).
; KCFG_INKS stays in CPC firmware ink numbers on BOTH platforms (the canonical config
; colour space, #287); the MSX driver (lib/msx/screen.asm) maps them to V9938 GRB.
                ifndef PLATFORM_MSX
                ifndef PLATFORM_PCW            ; (#331: the PCW palette is fixed CGA2 -
                                               ;  set_palette is a stub in lib/pcw/screen.asm)
set_palette
                ld    a,(KCFG_INKS+0)
                ld    b,a
                ld    c,a
                ld    a,0
                call  SCR_SET_INK            ; pen 0 (paper / backdrop)
                ld    a,(KCFG_INKS+1)
                ld    b,a
                ld    c,a
                ld    a,1
                call  SCR_SET_INK            ; pen 1 (text)
                ld    a,(KCFG_INKS+2)
                ld    b,a
                ld    c,a
                ld    a,2
                call  SCR_SET_INK            ; pen 2 (outlines / title bar)
                ld    a,(KCFG_INKS+3)
                ld    b,a
                ld    c,a
                ld    a,3
                call  SCR_SET_INK            ; pen 3 (accents)
                ld    a,(KCFG_INKS+4)        ; border = its own ink (INKS= 5th value)
                ld    b,a
                ld    c,a
                jp    SCR_SET_BORDER
                endif
                endif                          ; (MSX set_palette + k_setink live in lib/msx/screen.asm)
; (No CPC k_setink: the resident kernel is full - GB_SETINK is an MSX-only slot,
;  and CPC Settings drives SCR_SET_INK/BORDER directly. #287)

                ifdef PLATFORM_MSX
                include "input_api_msx.asm"
                else
                ifdef PLATFORM_PCW
                include "input_api_pcw.asm"
                else
                include "input_api.asm"
                endif
                endif

; fill_xywh: B=x C=y D=w E=h -> set fb_* then fill_block (fb_val preset). Shared
; rectangle/edge filler so k_frame and kwin_frame don't each repeat the fb_* stores.
fill_xywh
                ld    a,b
                ld    (fb_x),a
                ld    a,c
                ld    (fb_y),a
                ld    a,d
                ld    (fb_w),a
                ld    a,e
                ld    (fb_h),a
                jp    fill_block

; copy11: HL=src DE=dst -> copy 11 bytes (an 8.3 name). Shared by the many name-copy
; sites (fs_req_name / launch_arg / window arg) to drop the per-site ld bc,11 + ldir.
copy11
                ld    bc,11
                ldir
                ret

; k_frame (GB_FRAME): draw a 1px rectangle outline. B=x C=y D=w E=h (screen
; byte/line), A=pen (0..3). Screen only (no banked buffer), so no page swap.
k_frame
                ld    (gf_pen),a             ; save params FIRST: pen_to_byte below
                ld    a,b                     ; clobbers E (height), so capture it now
                ld    (gf_x),a
                ld    a,c
                ld    (gf_y),a
                ld    a,d
                ld    (gf_w),a
                ld    a,e
                ld    (gf_h),a
                ld    a,(gf_pen)
                call  pen_to_byte            ; pen -> Mode 1 fill byte
                ld    (fb_val),a
                ld    a,(gf_x)               ; top edge: (x, y, w, 1)
                ld    b,a
                ld    a,(gf_y)
                ld    c,a
                ld    a,(gf_w)
                ld    d,a
                ld    e,1
                call  fill_xywh
                ld    a,(gf_x)               ; bottom edge: (x, y+h-1, w, 1)
                ld    b,a
                ld    a,(gf_y)
                ld    c,a
                ld    a,(gf_h)
                add   a,c
                dec   a
                ld    c,a
                ld    a,(gf_w)
                ld    d,a
                ld    e,1
                call  fill_xywh
                ld    a,(gf_x)               ; left edge: (x, y, 1, h)
                ld    b,a
                ld    a,(gf_y)
                ld    c,a
                ld    d,1
                ld    a,(gf_h)
                ld    e,a
                call  fill_xywh
                ld    a,(gf_x)               ; right edge: (x+w-1, y, 1, h)
                ld    b,a
                ld    a,(gf_w)
                add   a,b
                dec   a
                ld    b,a
                ld    a,(gf_y)
                ld    c,a
                ld    d,1
                ld    a,(gf_h)
                ld    e,a
                jp    fill_xywh
gf_x            equ   #14CD        ; #202: relocated to low RAM (was resident db) to reclaim 5
gf_y            equ   #14CE        ; resident bytes for the backdrop_init tile-scratch fix.
gf_w            equ   #14CF        ; gb_frame writes all five before reading them (#188 pattern).
gf_h            equ   #14D0
gf_pen          equ   #14D1

; --- firmware text (kernel boot messages) --------------------------------
                ifndef PLATFORM_MSX
                ifndef PLATFORM_PCW            ; (PCW k_cls lives in lib/pcw/screen.asm)
k_cls
                ld    a,1
                jp    SCR_SET_MODE            ; mode 1 clears; returns to caller
                endif
                endif                          ; (MSX k_cls lives in lib/msx/screen.asm)
k_noop                                         ; shared no-op for dead ABI slots (#148):
                ret                            ; GB_PRINT/QUIT/LAUNCH/XORFRAME/ONREPAINT/WMLAUNCH/ONEVENT
k_ret0          xor   a                        ; shared "return 0" (e.g. GB_PICOPEN when a kernel
                ret                            ; has no banked-picture support, #164)

; to_data / from_data: save the caller's bank page and map PAGE_DATA (the font /
; icon page), then restore it. One shared save slot (dp_save) - these swaps are
; never nested (each service swaps, calls only non-swapping helpers, restores).
; Replaces the open-coded save/swap/restore at every PAGE_DATA service.
to_data
                ld    a,(bank_cur)
                ld    (dp_save),a
                LD_A_PAGE_DATA
                jp    bank_set
from_data
                ld    a,(dp_save)
                jp    bank_set
dp_save         equ   #123E        ; low-RAM kernel scratch (see lowram.tsv)

; --- gb_text_draw: 6x8 font text service ---------------------------------
; B = byte col, C = line, D = pen, E = paper, HL = string (caller's page).
; Copy the string to resident scratch (caller page still mapped), set up the
; renderer, then swap to PAGE_DATA for the glyphs, draw, and restore.
gb_text_draw
                ld    a,b                     ; position
                ld    (tc_x),a
                ld    a,c
                ld    (tc_y),a
                ld    b,d                     ; pens: B = pen, C = paper
                ld    c,e
                push  hl
                call  set_text_pens
                pop   hl
                ld    de,gtd_scratch          ; copy string out of the caller's page
                call  gtd_copy
                call  to_data                 ; swap to the font page
                ld    hl,gtd_scratch
                call  draw_text
                jr    from_data               ; restore caller's page
gtd_copy                                       ; (HL) -> (DE) until NUL, cap 48
                ld    b,48
gtd_cloop
                ld    a,(hl)
                ld    (de),a
                or    a
                ret   z
                inc   hl
                inc   de
                djnz  gtd_cloop
                xor   a                        ; truncate + terminate
                ld    (de),a
                ret
gtd_scratch     equ   #1450        ; #188: relocated to low RAM (was resident defs 49)

; --- gb_open_window: draw a window frame + title -------------------------
; B = x (byte col), C = y (line), D = w (bytes), E = h (lines), HL = title.
; The frame is plain fills (screen only); the title needs the font, so swap to
; PAGE_DATA for it. No save-under: closing a window redraws the desktop.
gb_open_window
                ld    a,b
                ld    (kw_x),a
                ld    a,c
                ld    (kw_y),a
                ld    a,d
                ld    (kw_w),a
                ld    a,e
                ld    (kw_h),a
                ld    de,kw_title            ; copy title out of the caller's page
                call  gtd_copy
                if TITLEBAR_TILE
                call  to_data                 ; GBTITLE.MOD shares PAGE_DATA with font/icons
                call  kwin_frame             ; frame: fills to screen, no font
                else
                call  kwin_frame
                call  to_data                 ; title needs the font page
                endif
                ld    b,1                     ; white on black title bar
                ld    c,2
                call  set_text_pens
                ld    a,(kw_x)
                add   a,4
                ld    (tc_x),a
                ld    a,(kw_y)
                add   a,3
                ld    (tc_y),a
                ld    hl,kw_title
                call  draw_text
                if !THEMED_GADGETS
                ifdef WM_GADGETS
                ld    b,2                     ; close 'X' glyph: pen 2 (black) on pen 1 (white box)
                ld    c,1
                call  set_text_pens
                ld    a,(kw_x)
                inc   a
                ld    (tc_x),a
                ld    a,(kw_y)
                add   a,3
                ld    (tc_y),a
                ld    hl,gad_x_str
                call  draw_text
                endif
                endif
                jp    from_data
                if !THEMED_GADGETS
                ifdef WM_GADGETS
gad_x_str       db    "X",0
                endif
                endif

; Window-chrome fill bytes in the compact four-pen UI representation (NOT
; pen_to_byte), so the CPC image stays byte-identical. MSX Screen 6 uses this
; representation natively and Screen 7 expands it at the display boundary.
; CPC's split-bitplane #F0/#0F values therefore cannot be used for MSX; PCW's
; CGA2 permutation likewise needs its own constants.
                ifdef PLATFORM_PCW
KWB_PAPER       equ   #55           ; pen 0 (cyan interior)
KWB_LIGHT       equ   #FF           ; pen 1 (white title base / gadgets)
KWB_DARK        equ   #00           ; pen 2 (black stripes / squares)
                else
                ifdef PLATFORM_MSX
KWB_LIGHT       equ   #55           ; packed UI pen 1: 01 01 01 01
KWB_DARK        equ   #AA           ; packed UI pen 2: 10 10 10 10
                else
KWB_LIGHT       equ   #F0           ; CPC Mode-1 pen 1
KWB_DARK        equ   #0F           ; CPC pen 2
                endif
                endif

; kwin_frame: paper interior, striped/tiled title bar, borders and gadgets.
kwin_frame
                ifdef PLATFORM_PCW
                ld    a,KWB_PAPER             ; interior (pen 0)
                else
                xor   a                       ; interior (blue): (x, y, w, h)
                endif
                ld    (fb_val),a
                ld    a,(kw_x)
                ld    b,a
                ld    a,(kw_y)
                ld    c,a
                ld    a,(kw_w)
                ld    d,a
                ld    a,(kw_h)
                ld    e,a
                call  fill_xywh
                if TITLEBAR_TILE
                ; The tile renderer clips like fill_block and phases the
                ; 16x14 motif from this window's own top-left corner.
                ld    a,(kw_x)
                ld    (fb_x),a
                ld    a,(kw_y)
                ld    (fb_y),a
                ld    a,(kw_w)
                ld    (fb_w),a
                ld    a,14
                ld    (fb_h),a
                ld    a,(TITLE_READY)
                or    a
                jr    z,kf_tile_missing
                ifdef PLATFORM_MSX
                call  fill_title_pattern
                else
                ifdef PLATFORM_PCW
                call  fill_title_pattern
                else
                call  DATA_TITLE_RUN         ; CPC renderer executes from PAGE_DATA
                endif
                endif
                jr    kf_title_done
kf_tile_missing
                ld    a,KWB_LIGHT             ; missing sample module: safe plain title bar
                ld    (fb_val),a
                call  fill_block
kf_title_done
                else
                ld    a,KWB_LIGHT             ; title bar: light base (x, y, w, 14)
                ld    (fb_val),a
                ld    a,(kw_x)
                ld    b,a
                ld    a,(kw_y)
                ld    c,a
                ld    a,(kw_w)
                ld    d,a
                ld    e,14
                call  fill_xywh
                ld    a,KWB_DARK              ; ... dark horizontal stripes (1-line
                ld    (fb_val),a             ; fills, every other line; fb_x/fb_w
                ld    a,1                     ; stay kw_x/kw_w from the fill above)
                ld    (fb_h),a
                ld    a,(kw_y)
                ld    (kf_sy),a
                ld    b,7
kf_stripe       ld    a,(kf_sy)
                ld    (fb_y),a
                push  bc
                call  fill_block
                pop   bc
                ld    a,(kf_sy)
                add   a,2
                ld    (kf_sy),a
                djnz  kf_stripe
                endif
                ld    hl,(kw_x)              ; black borders via k_frame (all 4 edges; the
                ld    b,l                    ; top edge coincides with the first title
                ld    c,h                    ; stripe, so it is behavior-neutral) - was
                ld    hl,(kw_w)              ; three separate left/right/bottom fills
                ld    d,l
                ld    e,h
                ld    a,(KCFG_FRAMEPEN)      ; Edge, or a preselected contrasting UI pen
                call  k_frame
                if THEMED_GADGETS
                jp    kf_theme_gadgets
                else
                ld    a,KWB_LIGHT             ; close gadget (light): (x+1, y+2, 2, 10)
                ld    (fb_val),a
                ld    a,(kw_x)
                inc   a
                ld    b,a
                ld    a,(kw_y)
                add   a,2
                ld    c,a
                ld    d,2
                ld    e,10
                call  fill_xywh              ; (the 'X' glyph is drawn in gb_open_window, font)
                ifdef WM_GADGETS
; maximize gadget on the right: white box (x+w-4, y+2, 3, 10) + a centered black square.
; (3 bytes wide so the black square sits in the MIDDLE byte with white either side - a 2-byte
; box would have both edge bytes filled by k_frame, i.e. solid black. 1 byte = 4 px in mode 1.)
                ld    a,KWB_LIGHT
                ld    (fb_val),a
                ld    a,(kw_x)
                ld    hl,kw_w
                add   a,(hl)                  ; A = x + w
                sub   4
                ld    (kf_gx),a              ; gadget x (also reused by the hit-test)
                ld    b,a
                ld    a,(kw_y)
                add   a,2
                ld    c,a
                ld    d,3
                ld    e,10
                call  fill_xywh
                ld    a,KWB_DARK              ; dark (pen 2) filled square in the centre byte
                ld    (fb_val),a
                ld    a,(kf_gx)
                inc   a                        ; centre byte (gx+1)
                ld    b,a
                ld    a,(kw_y)
                add   a,5
                ld    c,a
                ld    d,1                       ; 4 px wide
                ld    e,4                       ; 4 px tall
                jp    fill_xywh
kf_gx           db    0            ; maximize-gadget x byte-col (set by kwin_frame)
                else
                ret                            ; plain (no-room) build: just the close box
                endif
                endif

                if THEMED_GADGETS
; Reusable gadget tiles live beside the repeated background in PAGE_DATA. The
; shared opaque bitmap path performs each target's native write/transcoding.
kf_theme_gadgets
                xor   a
                ld    (bm_keep),a
                ld    hl,DATA_TITLE_CLOSE
                ld    (bm_src),hl
                ld    a,(kw_x)
                inc   a
                ld    (bm_x),a
                ld    a,(kw_y)
                add   a,2
                ld    (bm_y),a
                ld    a,2
                ld    (bm_w),a
                ld    a,10
                ld    (bm_h),a
                call  blit_bitmap

                ld    hl,DATA_TITLE_MAX
                ld    (bm_src),hl
                ld    a,(kw_x)
                ld    hl,kw_w
                add   a,(hl)
                sub   4
                ld    (bm_x),a
                ld    a,(kw_y)
                add   a,2
                ld    (bm_y),a
                ld    a,3
                ld    (bm_w),a
                ld    a,10
                ld    (bm_h),a
                jp    blit_bitmap
                endif
kw_x            db    0
kw_y            db    0
kw_w            db    0
kw_h            db    0
kf_sy           db    0            ; kwin_frame: current title-bar stripe line
kw_title        equ   #12D1        ; 24-byte window-title scratch, relocated to low RAM (always
                                   ; mapped) to reclaim resident space for the icon keep-mask gate
                                   ; (#182). Sits in the free #12D1..#12E8 block below APP_HANDLER.

; --- directory enumeration services --------------------------------------
; Return CF set with HL -> a resident "NAME.EXT" string (0-term) for the entry,
; or NC at end of directory. fs_ent_* are resident, so no page swap is needed;
; the floppy backend is di-safe and uses its own resident buffers.
gb_fs_dir_first
                ifdef PLATFORM_PCW
                call  fs_dir_first_quick
                else
                call  fs_dir_first
                endif
                jr    gdir_done
gb_fs_dir_next
                call  fs_dir_next
gdir_done
                ret   nc                       ; no/Last entry
                call  fmt_entry              ; fs_ent_name -> dir_namebuf
                ld    hl,dir_namebuf
                scf
                ret

; fmt_entry: fs_ent_name (8.3, space padded) -> dir_namebuf as the bare "NAME", 0.
; (Name only - the type is conveyed by the entry's icon; the old extension-display
; branch was dead, showext was never set.)
fmt_entry
                ld    de,dir_namebuf
                ld    hl,fs_ent_name
                ld    b,8
fe_name
                ld    a,(hl)
                cp    ' '
                jr    z,fe_end
                ld    (de),a
                inc   de
                inc   hl
                djnz  fe_name
fe_end
                xor   a
                ld    (de),a
                ret
dir_namebuf     equ   #12E9        ; 14-byte dir-name scratch, relocated to low RAM (always
                                   ; mapped) to reclaim resident space for GB_RELOAD (#185).
                                   ; Sits in the free #12E9..#12F6 block below APP_HANDLER.

; --- k_icon_half (GB_ICONHALF): half-height (middle-band) blit of icon A=slot at
; B=x, C=y. The file->slot mapping now lives in the File Manager (#103); the kernel
; only blits a given slot (it owns PAGE_DATA where the .IST lives). The grid view
; uses k_icon (full icon), the list view uses this (compact 16px middle band).
k_icon_half
                ld    (be_slot),a            ; save the slot (A is reused below)
                ld    a,b
                ld    (be_x),a
                ld    a,c
                ld    (be_y),a
                xor   a                        ; half icons (File Manager list view) are opaque (#182)
                ld    (bm_keep),a
                call  to_data
                ld    a,(be_slot)
                call  icon_geom              ; sets bm_src/bm_w/bm_h/bm_x/bm_y
                call  blit_bitmap
                jp    from_data
be_x            db    0
be_y            db    0
be_slot         db    0
ig_w            db    0
ig_h            db    0

; icon_geom: A = slot -> from the .IST directory (DATA_ICONS) set up a
; half-height blit (middle band) at (be_x, be_y). PAGE_DATA must be mapped.
icon_geom
                ld    l,a                     ; dir entry = DATA_ICONS+16 + slot*4
                ld    h,0
                add   hl,hl
                add   hl,hl
                ld    de,DATA_ICONS+16
                add   hl,de
                ld    e,(hl)                  ; offset (word)
                inc   hl
                ld    d,(hl)
                inc   hl
                ld    a,(hl)                  ; width (bytes)
                ld    (ig_w),a
                inc   hl
                ld    a,(hl)                  ; height (rows)
                ld    (ig_h),a
                ld    hl,DATA_ICONS          ; bitmap base = DATA_ICONS + offset
                add   hl,de
                ld    a,(ig_h)               ; + (h/4)*w  (skip the top quarter)
                srl   a
                srl   a
                ld    b,a
                ld    a,(ig_w)
                ld    e,a
                ld    d,0
ig_skip
                ld    a,b
                or    a
                jr    z,ig_done
                add   hl,de
                dec   b
                jr    ig_skip
ig_done
                ld    (bm_src),hl
                ld    a,(ig_w)
                ld    (bm_w),a
                ld    a,(ig_h)               ; half height
                srl   a
                ld    (bm_h),a
                ld    a,(be_x)
                ld    (bm_x),a
                ld    a,(be_y)
                ld    (bm_y),a
                ret

; (file -> icon-slot mapping moved to the File Manager, #103 - the kernel now
; only blits a given slot via k_icon / k_icon_half; the .IST lives in PAGE_DATA.)

                include "config_module.asm"

                include "assets.asm"

; --- desktop boot loader -------------------------------------------------
; The old GB_RUN modal/nested app launcher is gone. Boot only needs to load
; DESKTOP.APP once; later apps use the co-resident WM path.
boot_desktop
                ld    hl,name_desktop
                ld    de,fs_req_name
                call  copy11
                call  wm_alloc_page
                or    a
                ret   z
                ld    c,a
                ld    a,(bank_cur)
                push  af
                xor   a
                ld    (MENU_DEF),a
                ld    h,a
                ld    l,a
                ld    (APP_HANDLER),hl
                di
                ld    a,c
                call  bank_set
                ld    hl,APP_LOAD_MAX             ; keep sector/record padding below app data RAM
                ld    (fs_load_max),hl
                ld    hl,APP_BASE
                ld    (fs_load_dst),hl
                call  fs_load_sys             ; #134: app binaries live in the /GEOBENCH system dir
                jr    nc,bd_done
                ei
                call  APP_BASE
                di
bd_done         pop   af
                call  bank_set
                ld    a,c
                call  wm_free_page
                ei
                ret

                ifdef WM_GADGETS
; --- #164 banked Viewer picture buffer ------------------------------------------------------
; Borrow one or two free app-pool pages, map them at #4000, stream the opened .PIC into them
; through the low-RAM module buffer, then blit from them with restore_block (which reads its
; sb_buf in the #4000 window). Lets the Viewer show .PIC files bigger than its own 16K page.
;
                if PREEMPTIVE
PIC_CHUNK       equ   #1A00
                else
PIC_CHUNK       equ   #1C00
                endif

; k_pic_open (GB_PICOPEN): load the opened file into borrowed bank(s) + parse the .PIC header.
; -> A=bank with PIC_WB/PIC_H/PIC_OFF set if it's a .PIC loaded OK; A=0 (no free bank, or not a
; .PIC - the bank is released) so the Viewer falls back to its in-page buffer.
k_pic_open
                xor   a
                ld    (PIC_PAGE2),a
                ifdef PLATFORM_MSX
                ld    (PIC_PAGE3),a
                ld    (PIC_PAGE4),a
                ld    (PIC_MODE),a
                ld    (PIC_STRIDE),a
                ld    (PIC_STRIDE+1),a
                endif
                call  wm_alloc_page          ; A = a free app-pool page, or 0 = none free
                or    a
                ret   z                        ; -> A=0, caller uses its in-page buffer
                ld    (PIC_PAGE),a
                ld    a,(bank_cur)            ; save the Viewer's page
                push  af
                ld    hl,0
                ld    (PIC_OFS),hl
kpo_loop        call  focus_arg_ptr          ; fs_req_name = the focused window's file arg each
                ld    de,fs_req_name         ; chunk; data-module loads overwrite fs_req_name.
                call  copy11
                ld    hl,(PIC_OFS)
                ifndef MSX_SCREEN7
                bit   7,h
                jp    nz,kpo_fail            ; <32K: two 16K picture banks
                endif
                ld    a,l
                ld    (FS_LOAD_OFS),a
                ld    a,h
                ld    (FS_LOAD_OFS+1),a
                xor   a
                ld    (FS_LOAD_OFS+2),a
                ld    a,1
                ld    (FS_XFLAGS),a
                ifdef MSX_SCREEN7
                ld    a,h                     ; one 16K mapper segment per file quarter
                and   #C0
                jr    z,kpo_page1
                cp    #40
                jr    z,kpo_page2
                cp    #80
                jr    z,kpo_page3
                jr    kpo_page4
kpo_page1       ld    a,(PIC_PAGE)
                jr    kpo_have_page
kpo_page2       ld    a,(PIC_PAGE2)
                or    a
                jr    nz,kpo_have_page
                call  wm_alloc_page
                or    a
                jp    z,kpo_fail
                ld    (PIC_PAGE2),a
                jr    kpo_have_page
kpo_page3       ld    a,(PIC_PAGE3)
                or    a
                jr    nz,kpo_have_page
                call  wm_alloc_page
                or    a
                jp    z,kpo_fail
                ld    (PIC_PAGE3),a
                jr    kpo_have_page
kpo_page4       ld    a,(PIC_PAGE4)
                or    a
                jr    nz,kpo_have_page
                call  wm_alloc_page
                or    a
                jp    z,kpo_fail
                ld    (PIC_PAGE4),a
kpo_have_page   call  bank_set
                ld    hl,(PIC_OFS)
                ld    a,h                     ; CPU address = #4000 + (offset & #3FFF)
                and   #3F
                or    #40
                ld    h,a
                ld    de,#8000                ; bytes left in this mapper window
                ex    de,hl                   ; DE = destination, HL = #8000
                or    a
                sbc   hl,de
                push  hl
                ld    (fs_load_dst),de
                jr    kpo_have_room
                else
                bit   6,h
                jr    nz,kpo_page2
                ld    a,(PIC_PAGE)
                call  bank_set
                ld    hl,#4000
                ld    de,(PIC_OFS)
                or    a
                sbc   hl,de                   ; room in first bank
                push  hl
                ld    hl,(PIC_OFS)
                ld    de,#4000
                add   hl,de                   ; destination #4000+offset
                jr    kpo_have_dst
kpo_page2       ld    a,(PIC_PAGE2)
                or    a
                jr    nz,kpo_have2
                call  wm_alloc_page
                or    a
                jp    z,kpo_fail
                ld    (PIC_PAGE2),a
kpo_have2       call  bank_set
                ld    hl,#8000
                ld    de,(PIC_OFS)
                or    a
                sbc   hl,de                   ; room in second bank
                push  hl
                ld    hl,(PIC_OFS)            ; offset #4000..#7FFF maps directly in bank 2
kpo_have_dst    ld    (fs_load_dst),hl
                endif
kpo_have_room
                pop   hl
                ld    de,PIC_CHUNK
                push  hl
                or    a
                sbc   hl,de
                pop   hl
                jr    c,kpo_setmax
                ld    hl,PIC_CHUNK
kpo_setmax      ld    (fs_load_max),hl
                di
                call  fs_load_file           ; chunk-read into the mapped picture bank
                ei
                jp    nc,kpo_fail            ; not found / too big
                ld    hl,(fs_ent_size)
                ld    a,h
                or    l
                jr    nz,kpo_got
                ld    hl,(PIC_OFS)           ; EOF is OK after at least one chunk; empty is not
                ld    a,h
                or    l
                ifdef PIC_RUNTIME_CONVERT
                jp    z,kpo_fail
                else
                jr    z,kpo_fail
                endif
                jr    kpo_done_load
kpo_got         ld    b,h
                ld    c,l
                ld    de,(PIC_OFS)
                add   hl,de
                ifdef MSX_SCREEN7
                jp    c,kpo_fail              ; this service deliberately caps files below 64K
                endif
                ld    (PIC_OFS),hl
                ld    hl,(fs_load_max)
                or    a
                sbc   hl,bc
                jp    z,kpo_loop             ; full chunk: there may be more
kpo_done_load   ld    a,(PIC_PAGE)
                call  bank_set               ; parse the header from the first picture bank
                ifdef PLATFORM_MSX
                ld    a,(#4000)              ; validate the "GBPC" magic
                cp    'G'
                jp    nz,kpo_fail
                ld    a,(#4001)
                cp    'B'
                jp    nz,kpo_fail
                ld    a,(#4002)
                cp    'P'
                jp    nz,kpo_fail
                ld    a,(#4003)
                cp    'C'
                jp    nz,kpo_fail
                ld    a,(#4004)              ; version: 2 = v2 (else v1)
                cp    2
                jr    nz,kpo_v1
                ld    a,(#4005)              ; 1=canonical 2bpp, 6=legacy native
                cp    1
                jr    z,kpo_v2_mode1
                cp    6
                jr    z,kpo_v2_mode1
                ifdef MSX_SCREEN7
                cp    7                       ; Screen-7 linear 4bpp (two pixels/byte)
                jp    nz,kpo_fail
                ld    a,7
                ld    (PIC_MODE),a
                jr    kpo_v2_dims
                else
                jp    kpo_fail                ; this backend cannot display Screen-7 pictures
                endif
kpo_v2_mode1
                ifdef PLATFORM_MSX
                ld    a,1
                ld    (PIC_MODE),a
                endif
kpo_v2_dims
                ld    hl,(#4006)            ; v2: pic_wb = (width + 3) >> 2
                inc   hl
                inc   hl
                inc   hl
                srl   h
                rr    l
                srl   h
                rr    l
                ld    a,h
                or    a
                jp    nz,kpo_fail
                ld    a,l
                or    a
                jp    z,kpo_fail
                cp    129                    ; display width must fit 512 Screen-7 pixels
                jp    nc,kpo_fail
                ld    (PIC_WB),a
                ifdef PLATFORM_MSX
                ld    a,(PIC_MODE)
                cp    7
                jr    z,kpo_v2_stride7
                ld    a,(PIC_WB)
                ld    l,a
                ld    h,0
                jr    kpo_v2_stride_done
kpo_v2_stride7 ld    hl,(#4006)              ; mode 7: two pixels per source byte
                inc   hl
                srl   h
                rr    l
kpo_v2_stride_done
                ld    (PIC_STRIDE),hl
                endif
                ld    hl,(#4008)            ; height (rows), 16-bit (tall pics > 255, #166)
                ld    (PIC_H),hl
                ld    a,14                   ; bitmap offset
                ld    (PIC_OFF),a
                jr    kpo_ok
kpo_v1          ld    a,(#4004)             ; v1: byte width, then height, bitmap at +6
                ld    (PIC_WB),a
                ifdef PLATFORM_MSX
                ld    (PIC_STRIDE),a
                xor   a
                ld    (PIC_STRIDE+1),a
                inc   a
                ld    (PIC_MODE),a
                endif
                ld    a,(#4005)
                ld    l,a
                ld    h,0
                ld    (PIC_H),hl
                ld    a,6
                ld    (PIC_OFF),a
kpo_ok
                else
                ; Keep the compact CPC/PCW parser byte-identical. Those targets
                ; never stage the MSX-only mode-7 pictures, and resident CPC
                ; kernels do not have room for the extended validation path.
                ld    a,(#4000)
                cp    'G'
                jr    nz,kpo_fail
                ld    a,(#4001)
                cp    'B'
                jr    nz,kpo_fail
                ld    a,(#4002)
                cp    'P'
                jr    nz,kpo_fail
                ld    a,(#4003)
                cp    'C'
                jr    nz,kpo_fail
                ld    a,(#4004)
                cp    2
                jr    nz,kpo_v1
                ifdef PIC_RUNTIME_CONVERT
                ld    a,(#4005)
                cp    1
                jr    z,kpo_v2_mode_ok
                cp    6
                jp    nz,kpo_fail
kpo_v2_mode_ok
                endif
                ld    hl,(#4006)
                inc   hl
                inc   hl
                inc   hl
                srl   h
                rr    l
                srl   h
                rr    l
                ld    a,l
                ld    (PIC_WB),a
                ld    hl,(#4008)
                ld    (PIC_H),hl
                ld    a,14
                ld    (PIC_OFF),a
                jr    kpo_ok
kpo_v1          ld    a,(#4004)
                ld    (PIC_WB),a
                ld    a,(#4005)
                ld    l,a
                ld    h,0
                ld    (PIC_H),hl
                ld    a,6
                ld    (PIC_OFF),a
kpo_ok
                endif
                ifdef PIC_RUNTIME_CONVERT
                call  kpo_normalize_pic        ; legacy mode 6 -> canonical mode 1 in the bank
                endif
                pop   af                       ; restore the Viewer's page
                call  bank_set
                xor   a
                ld    (FS_XFLAGS),a           ; chunked picture loads must not poison later app/file loads
                ld    a,(PIC_PAGE)             ; return the borrowed bank (nonzero = truthy), so each
                ret                            ; Viewer window keeps its own and re-selects PIC_PAGE
                                               ; before blit/close (#164 two-window fix)
kpo_fail        pop   af                       ; restore the Viewer's page, release the bank
                call  bank_set
                xor   a
                ld    (FS_XFLAGS),a
                ifdef MSX_SCREEN7
                ld    a,(PIC_PAGE4)
                or    a
                jr    z,kpo_fail_3
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE4),a
kpo_fail_3      ld    a,(PIC_PAGE3)
                or    a
                jr    z,kpo_fail_2
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE3),a
kpo_fail_2
                endif
                ld    a,(PIC_PAGE2)
                or    a
                jr    z,kpo_fail_1
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE2),a
kpo_fail_1      ld    a,(PIC_PAGE)
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE),a
                ret

; k_pic_blit (GB_PICBLIT): B=x C=y D=wbytes E=h HL=src_off. Map the picture bank and blit the
; region to the screen at (x,y) via restore_block, then restore the page.
; The cursor is already down (the WM's cur_paintlock holds during the Viewer's on_draw).
k_pic_blit
                ld    a,b
                ld    (sb_x),a
                ld    a,c
                ld    (sb_y),a
                ld    a,d
                ld    (sb_w),a
                ifdef MSX_SCREEN7
                ld    a,1
                ld    (sb_h),a
                ld    a,e
                ld    (bl_rows),a
                ld    a,c
                ld    (bl_y),a
                ld    (kpb_off),hl
                ld    a,(bank_cur)
                push  af
kpb_row
                ld    hl,(kpb_off)
                ld    (PIC_EDIT_OFF),hl       ; copy helper may advance this at a bank edge
                ld    a,(sb_w)
                ld    l,a
                ld    h,0
                ld    a,(PIC_MODE)
                cp    7
                jr    nz,kpb_count_ready
                add   hl,hl                   ; mode 7 has two source bytes/4px column
kpb_count_ready ld    b,h
                ld    c,l
                ld    de,MSX_ROWBUF
                call  kpb_copy_to_row
                or    a
                jr    z,kpb_done
                ld    hl,MSX_ROWBUF
                ld    (sb_buf),hl
                ld    a,(bl_y)
                ld    (sb_y),a
                ld    a,(PIC_MODE)
                cp    7
                jr    nz,kpb_mode1
                call  restore_pic16_block
                jr    kpb_drawn
kpb_mode1       call  restore_pic_block
kpb_drawn
                ld    hl,(kpb_off)            ; next source row uses the file stride,
                ld    de,(PIC_STRIDE)         ; not the clipped draw width
                add   hl,de
                ld    (kpb_off),hl
                ld    a,(bl_y)
                inc   a
                ld    (bl_y),a
                ld    a,(bl_rows)
                dec   a
                ld    (bl_rows),a
                jr    nz,kpb_row
kpb_done        pop   af
                call  bank_set
                ret

; Copy BC bytes (at most one Screen-7 row) from banked picture offset
; PIC_EDIT_OFF to DE. Remap once if the span crosses a 16K segment edge.
kpb_copy_to_row
                ld    (PIC_EDIT_BUF),de
                ld    (fs_save_len),bc
kpb_cp_map      ld    hl,(PIC_EDIT_OFF)
                ld    a,h
                and   #C0
                ld    (kpb_pageq),a
                jr    z,kpb_cp_page1
                cp    #40
                jr    z,kpb_cp_page2
                cp    #80
                jr    z,kpb_cp_page3
                ld    a,(PIC_PAGE4)
                jr    kpb_cp_have_page
kpb_cp_page3    ld    a,(PIC_PAGE3)
                jr    kpb_cp_have_page
kpb_cp_page2    ld    a,(PIC_PAGE2)
                jr    kpb_cp_have_page
kpb_cp_page1    ld    a,(PIC_PAGE)
kpb_cp_have_page
                or    a
                ret   z
                call  bank_set
                ld    hl,(PIC_EDIT_OFF)
                ld    a,h
                and   #3F
                or    #40
                ld    h,a                     ; HL = mapped CPU source address
                ld    de,(PIC_EDIT_BUF)
                ld    bc,(fs_save_len)
kpb_cp_byte     ldi
                ld    a,b
                or    c
                jr    z,kpb_cp_ok
                bit   7,h                     ; #8000 = next mapper segment
                jr    z,kpb_cp_byte
                ld    (PIC_EDIT_BUF),de
                ld    (fs_save_len),bc
                ld    a,(kpb_pageq)
                add   a,#40
                ret   c                       ; past the fourth/64K segment
                ld    h,a
                ld    l,0
                ld    (PIC_EDIT_OFF),hl
                jr    kpb_cp_map
kpb_cp_ok       ld    a,1
                ret
kpb_off         dw    0
kpb_pageq       db    0
                else
                ld    a,e
                ld    (sb_h),a
                ld    a,(bank_cur)
                push  af
                bit   6,h
                jr    nz,kpb_page2
                ld    bc,#4000
                add   hl,bc                   ; HL = #4000 + src_off in the first bank
                ld    (sb_buf),hl
                ld    a,(PIC_PAGE)
                jr    kpb_map
kpb_page2       ld    a,(PIC_PAGE2)
                or    a
                jr    z,kpb_done
                ld    (sb_buf),hl             ; src_off #4000..#7FFF maps directly in bank 2
kpb_map         call  bank_set
                ifdef PIC_RUNTIME_CONVERT
                call  restore_pic_block      ; canonical Mode-1 bytes -> native screen bytes
                else
                call  restore_block          ; reads sb_buf (#4000..bank), writes the screen
                endif
kpb_done        pop   af
                call  bank_set
                ret
                endif

                ifdef PIC_RUNTIME_CONVERT
; Normalize old platform-native mode-6 pictures once at load. New files already
; carry canonical CPC Mode-1 bytes and skip this path. The inverse table is target
; specific: Screen 6 on MSX, Screen 6 + CGA2 permutation on PCW.
kpo_normalize_pic
                ld    a,(#4004)
                cp    2
                ret   nz
                ld    a,(#4005)
                cp    6
                ret   nz
                ld    a,1
                ld    (#4005),a
                ld    hl,(PIC_OFS)           ; payload bytes = complete file size - 14
                ld    de,14
                or    a
                sbc   hl,de
                ret   c
                ld    a,h
                or    l
                ret   z
                ld    de,#3FF2               ; first bank room after the 14-byte header
                or    a
                sbc   hl,de
                jr    c,kpn_first_short
                push  hl                     ; bytes remaining for page 2 (possibly zero)
                ld    bc,#3FF2
                ld    hl,#400E
                call  kpn_convert
                pop   bc
                ld    a,b
                or    c
                ret   z
                push  bc
                ld    a,(PIC_PAGE2)
                or    a
                jr    z,kpn_no_page2
                call  bank_set
                pop   bc
                ld    hl,#4000
                jr    kpn_convert
kpn_no_page2    pop   bc
                ret
kpn_first_short add   hl,de                   ; restore payload length after the compare
                ld    b,h
                ld    c,l
                ld    hl,#400E
kpn_convert     ld    a,(hl)
                ld    (kpn_lut+1),a            ; both generated tables are page-aligned
kpn_lut         ld    a,(pic_native_to_m1)
                ld    (hl),a
                inc   hl
                dec   bc
                ld    a,b
                or    c
                jr    nz,kpn_convert
                ret
                endif

                ifdef PLATFORM_MSX
; k_pic_edit (GB_PICEDIT, #288): Paint edits a 100x100 byte-aligned tile of a banked
; .PIC. A=0 copies the tile at PIC_EDIT_OFF into PIC_EDIT_BUF, A=1 writes PIC_EDIT_BUF
; back to that tile, A=2 copies fs_save_len bytes from PIC_EDIT_OFF to PIC_EDIT_BUF,
; and A=3 copies fs_save_len bytes in the other direction. Browser uses the generic
; chunk operations for its rendered-page and source caches.
; The app supplies PIC_PAGE/PIC_PAGE2 before each call, like gb_pic_blit/gb_pic_close.
PIC_TILE_WB     equ   25
PIC_TILE_H      equ   100
PIC_TILE_SZ     equ   PIC_TILE_WB*PIC_TILE_H
PIC_TMP         equ   #2200
k_pic_edit
                ifdef MSX_SCREEN7
                cp    5
                jp    z,kpe_native16
                endif
                cp    4
                jp    z,kpe_native
                cp    3
                jp    z,kpe_write
                cp    2
                jp    z,kpe_chunk
                cp    1
                jp    z,kpe_put
                or    a
                jp    z,kpe_get
                xor   a
                ret

; Convert a short canonical Mode-1 app buffer to the active MSX UI-bitmap
; representation without changing the source. Paint uses this for its small
; in-page canvas; banked
; pictures use restore_pic_block directly. PIC_EDIT_BUF=source,
; PIC_EDIT_OFF=destination, fs_save_len=count.
kpe_native      ld    hl,(PIC_EDIT_BUF)
                ld    de,(PIC_EDIT_OFF)
                ld    bc,(fs_save_len)
                ld    a,b
                or    c
                jr    z,kpen_done
kpen_loop       ld    a,(hl)
                inc   hl
                ld    (kpen_lut+1),a
kpen_lut        ld    a,(pic_m1_to_native)
                ld    (de),a
                inc   de
                dec   bc
                ld    a,b
                or    c
                jr    nz,kpen_loop
kpen_done       ld    a,1
                ret

                ifdef MSX_SCREEN7
; Draw a short native Screen-7 resource without routing it through the shared
; four-pen UI representation. PIC_EDIT_BUF=source; PIC_EDIT_OFF packs x,y in
; its low/high bytes; fs_save_len packs logical width,height likewise.
kpe_native16    ld    a,(PIC_EDIT_OFF)
                ld    (sb_x),a
                ld    a,(PIC_EDIT_OFF+1)
                ld    (sb_y),a
                ld    a,(fs_save_len)
                ld    (sb_w),a
                ld    a,(fs_save_len+1)
                ld    (sb_h),a
                ld    hl,(PIC_EDIT_BUF)
                ld    (sb_buf),hl
                call  restore_pic16_block
                ld    a,1
                ret
                endif

kpe_get         ld    a,(PIC_PAGE)
                or    a
                ret   z
                ld    a,(bank_cur)
                push  af
                call  kpe_pic_to_tmp
                ld    c,a
                pop   af
                call  bank_set
                ld    a,c
                or    a
                ret   z
                ld    hl,PIC_TMP
                ld    de,(PIC_EDIT_BUF)
                ld    bc,PIC_TILE_SZ
                ldir
                ld    a,1
                ret

kpe_put         ld    a,(PIC_PAGE)
                or    a
                ret   z
                ld    hl,(PIC_EDIT_BUF)
                ld    de,PIC_TMP
                ld    bc,PIC_TILE_SZ
                ldir
                ld    a,(bank_cur)
                push  af
                call  kpe_tmp_to_pic
                ld    c,a
                pop   af
                call  bank_set
                ld    a,c
                ret

kpe_chunk       ld    a,(PIC_PAGE)
                or    a
                ret   z
                ld    a,(bank_cur)
                push  af
                ld    hl,(PIC_EDIT_OFF)
                ld    de,(PIC_EDIT_BUF)
                ld    bc,(fs_save_len)
                call  kpe_seg_get16
                ld    c,a
                pop   af
                call  bank_set
                ld    a,c
                ret

kpe_write       ld    a,(PIC_PAGE)
                or    a
                ret   z
                ld    a,(bank_cur)
                push  af
                ld    hl,(PIC_EDIT_OFF)
                ld    de,(PIC_EDIT_BUF)
                ld    bc,(fs_save_len)
                call  kpe_seg_put16
                ld    c,a
                pop   af
                call  bank_set
                ld    a,c
                ret

; Copy the 100x100 tile from the banked .PIC into PIC_TMP. PIC_EDIT_OFF is used as
; a row cursor and may be clobbered; the app resets it before each request.
kpe_pic_to_tmp  ld    de,PIC_TMP
                ld    b,PIC_TILE_H
kpt_row         push  bc
                ld    hl,(PIC_EDIT_OFF)
                ld    c,PIC_TILE_WB
                call  kpe_row_get
                or    a
                jr    z,kpt_fail
                pop   bc
                push  bc
                ld    hl,(PIC_EDIT_OFF)
                ld    a,(PIC_WB)
                ld    c,a
                ld    b,0
                add   hl,bc
                ld    (PIC_EDIT_OFF),hl
                pop   bc
                djnz  kpt_row
                ld    a,1
                ret
kpt_fail        pop   bc
                xor   a
                ret

; Copy PIC_TMP back into the banked .PIC tile.
kpe_tmp_to_pic  ld    de,PIC_TMP
                ld    b,PIC_TILE_H
kpp_row         push  bc
                ld    hl,(PIC_EDIT_OFF)
                ld    c,PIC_TILE_WB
                call  kpe_row_put
                or    a
                jr    z,kpp_fail
                pop   bc
                push  bc
                ld    hl,(PIC_EDIT_OFF)
                ld    a,(PIC_WB)
                ld    c,a
                ld    b,0
                add   hl,bc
                ld    (PIC_EDIT_OFF),hl
                pop   bc
                djnz  kpp_row
                ld    a,1
                ret
kpp_fail        pop   bc
                xor   a
                ret

; Row helpers split the rare case where a 25-byte tile row straddles the first/second
; picture bank boundary. Input: HL=picture offset, DE=low-RAM tile pointer, C=count.
; Return: A=1/0, DE advanced through the low-RAM tile on success.
kpe_row_get     ld    a,h
                cp    #3F
                jr    nz,kpe_seg_get
                ld    a,l
                add   a,c
                jr    nc,kpe_seg_get
                ld    a,l
                cpl
                inc   a                         ; first part = #4000 - offset
                ld    b,a
                ld    a,c
                sub   b                         ; second part
                push  af
                ld    c,b
                call  kpe_seg_get
                or    a
                jr    z,kprg_fail
                pop   af
                ld    c,a
                ld    hl,#4000
                jr    kpe_seg_get
kprg_fail       pop   af
                xor   a
                ret

kpe_row_put     ld    a,h
                cp    #3F
                jr    nz,kpe_seg_put
                ld    a,l
                add   a,c
                jr    nc,kpe_seg_put
                ld    a,l
                cpl
                inc   a
                ld    b,a
                ld    a,c
                sub   b
                push  af
                ld    c,b
                call  kpe_seg_put
                or    a
                jr    z,kprp_fail
                pop   af
                ld    c,a
                ld    hl,#4000
                jr    kpe_seg_put
kprp_fail       pop   af
                xor   a
                ret

; Copy one non-crossing segment from the banked .PIC to low RAM. Input:
; HL=picture offset, DE=destination, BC=count. A=1 on success.
kpe_seg_get     ld    b,0
kpe_seg_get16   bit   6,h
                jr    nz,kpeg_second
                ld    a,(PIC_PAGE)
                or    a
                ret   z
                push  bc
                ld    bc,#4000
                add   hl,bc
                pop   bc
                jr    kpeg_map
kpeg_second     ld    a,(PIC_PAGE2)
                or    a
                ret   z
kpeg_map        push  bc
                call  bank_set
                pop   bc
                ldir
                ld    a,1
                ret

; Copy one non-crossing segment from low RAM to the banked .PIC. Input:
; HL=picture offset, DE=source, C=count. A=1 on success, DE=source advanced.
kpe_seg_put     ld    b,0
kpe_seg_put16   bit   6,h
                jr    nz,kpep_second
                ld    a,(PIC_PAGE)
                or    a
                ret   z
                push  bc
                ld    bc,#4000
                add   hl,bc
                pop   bc
                jr    kpep_map
kpep_second     ld    a,(PIC_PAGE2)
                or    a
                ret   z
kpep_map        push  bc
                call  bank_set
                pop   bc
                ex    de,hl
                ldir
                ex    de,hl                    ; DE = low-RAM source advanced
                ld    a,1
                ret
                endif                            ; PLATFORM_MSX resident PICEDIT

; k_pic_close (GB_PICCLOSE): release the borrowed picture bank(s).
k_pic_close
                ifdef MSX_SCREEN7
                ld    a,(PIC_PAGE4)
                or    a
                jr    z,kpc_third
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE4),a
kpc_third       ld    a,(PIC_PAGE3)
                or    a
                jr    z,kpc_second
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE3),a
kpc_second
                endif
                ld    a,(PIC_PAGE2)
                or    a
                jr    z,kpc_first
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE2),a
kpc_first       ld    a,(PIC_PAGE)
                or    a
                ret   z
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE),a
                ret
                endif

                include "app_pool.asm"

; GB_RUN is a reserved legacy modal-run slot; live apps use GB_WMOPEN/GB_WMLAUNCHAS.

; k_launch (GB_LAUNCH): the old MODAL open-the-current-entry. Superseded by the
; co-resident open path and no longer called by any app; the slot -> k_noop (#148).

; Directory navigation (issue #54). The FAT backend enumerates the directory at
; fs_dir_clus; gb_chdir descends into the positioned entry (pushing the parent on
; a small stack) and gb_back pops it. On the floppy backend these touch FAT-only
; state the AMSDOS reader ignores, and gb_isdir is always 0 (no subdirectories),
; so the file manager simply never descends there.

; k_isdir (GB_ISDIR): A = 1 if the last-enumerated entry is a directory, else 0.
k_isdir
                ld    a,(fs_ent_attr)
                and   #10
                ret   z
                ld    a,1
                ret

; k_chdir (GB_CHDIR): descend into the positioned entry's directory. The IDE backend
; pushes the current dir cluster and sets it to the entry's start cluster; the
; Albireo backend (#104) appends "/<name>" to its path string instead. The next
; listing rewinds there. Refuses (no-op) when the stack/path is full.
k_chdir
                ifdef PLATFORM_MSX
                jp    fsmx_chdir               ; #287: BDOS _CHDIR into the entry
                else
                ifdef PLATFORM_PCW
                ret                              ; #331: CP/M is flat - no directories
                else
                if STORAGE_ALBIREO
                jp    fsalb_chdir
                else
                if STORAGE_M4                    ; #174: M4 is path-based like Albireo
                jp    fsm4_chdir
                else
                ld    a,(fs_dir_sp)
                cp    4                          ; DIRSTACK depth
                ret   nc
                add   a,a                         ; slot = fs_dir_stack + sp*4
                add   a,a
                ld    e,a
                ld    d,0
                ld    hl,fs_dir_stack
                add   hl,de
                ex    de,hl                       ; DE = slot
                ld    hl,fs_dir_clus             ; push current dir
                ld    bc,4
                ldir
                ld    hl,fs_dir_sp
                inc   (hl)
                ld    hl,fs_ent_clus             ; descend: dir = entry's cluster
                ld    de,fs_dir_clus
                ld    bc,4
                ldir
                ret
                endif                            ; STORAGE_M4
                endif                            ; STORAGE_ALBIREO
                endif                            ; PLATFORM_PCW
                endif                            ; PLATFORM_MSX

; k_back (GB_BACK): go to the parent directory (no-op at the top). IDE pops the dir
; cluster stack; Albireo/M4 (#104/#174) truncate their path string at the last '/'.
k_back
                ifdef PLATFORM_MSX
                jp    fsmx_back                  ; #287: BDOS _CHDIR ".."
                else
                ifdef PLATFORM_PCW
                ret                              ; #331: CP/M is flat - no directories
                else
                if STORAGE_ALBIREO
                jp    fsalb_back
                else
                if STORAGE_M4
                jp    fsm4_back
                else
                ld    a,(fs_dir_sp)
                or    a
                ret   z
                dec   a
                ld    (fs_dir_sp),a
                add   a,a                         ; slot = fs_dir_stack + sp*4
                add   a,a
                ld    e,a
                ld    d,0
                ld    hl,fs_dir_stack
                add   hl,de
                ld    de,fs_dir_clus
                ld    bc,4
                ldir
                ret
                endif                            ; STORAGE_M4
                endif                            ; STORAGE_ALBIREO
                endif                            ; PLATFORM_PCW
                endif                            ; PLATFORM_MSX

; k_entname (GB_ENTNAME): HL = the last-enumerated entry's raw 11-byte 8.3 name
; (space-padded, no dot) so an app can show the extension (gb_dir* return name-only).
k_entname
                ld    hl,fs_ent_name
                ret

; focus_arg_ptr: HL = the focused window's 11-byte file arg (per-window, so two
; editors keep separate files). Each window captured the pending launch_arg at
; gb_wm_add; getarg/setname/fsload/fssave all act on the focused window's copy.
focus_arg_ptr
                ld    a,(WM_FOCUS)
                call  wm_entry
                ld    de,WM_FR_ARG
                add   hl,de
                ret

; GB_GETARG (HL = the launch arg, the 8.3 file name the app opened): the slot
; jumps straight to focus_arg_ptr (k_getarg wrapper collapsed, #148).

; k_setname (GB_SETNAME): set the current file name so a later GB_FSLOAD/GB_FSSAVE
; targets it - how an app does New / Save As / open a picked file. HL = an 11-byte
; 8.3 name in the caller's page. Writes the focused window's per-window arg.
k_setname
                push  hl                       ; HL = src name
                call  focus_arg_ptr           ; HL = dst (focused window's arg)
                ex    de,hl                     ; DE = dst
                pop   hl                       ; HL = src
                call  copy11
                ret

; The shared clipboard (#142): a fixed low-RAM buffer (CLIP_LEN word + CLIP_DATA bytes)
; that survives app switches, so copy in one app and paste in another. Resident, so the
; code isn't duplicated into every app's bank. Low RAM is always mapped.

; k_clip_set (GB_CLIPSET): copy DE bytes from HL into the clipboard, clamped to CLIP_CAP.
; HL = src (caller page, mapped), DE = length.
k_clip_set
                or    a                        ; clamp DE to CLIP_CAP
                push  hl                        ; save src
                ld    hl,CLIP_CAP
                sbc   hl,de                     ; CLIP_CAP - len ; NC = len <= CLIP_CAP
                jr    nc,kcs_len
                ld    de,CLIP_CAP
kcs_len         pop   hl                        ; HL = src
                ld    (CLIP_LEN),de            ; store the length
                ld    b,d
                ld    c,e                        ; BC = count
                ld    a,b
                or    c
                ret   z                          ; nothing to copy
                ld    de,CLIP_DATA
                ldir                             ; src -> clipboard
                ret

; k_clip_get (GB_CLIPGET): copy up to DE bytes of the clipboard into HL. HL = dst (caller
; page), DE = max. Returns BC = bytes copied (the trampoline maps BC -> the C return).
k_clip_get
                ld    bc,(CLIP_LEN)            ; BC = stored length
                push  hl                        ; save dst
                ld    h,b
                ld    l,c
                or    a
                sbc   hl,de                     ; len - max ; CF = len < max
                jr    c,kcg_n                    ; copy len (BC already)
                ld    b,d
                ld    c,e                        ; else copy max
kcg_n           pop   de                         ; DE = dst
                ld    hl,CLIP_DATA              ; HL = src
                ld    a,b
                or    c
                ret   z                          ; count 0 -> BC=0
                push  bc                        ; save count
                ldir                             ; clipboard -> dst
                pop   bc                         ; BC = count (return)
                ret

; k_clip_len (GB_CLIPLEN): BC = the clipboard length (for sizing a paste).
k_clip_len
                ld    bc,(CLIP_LEN)
                ret

                include "modules.asm"

; k_fsload (GB_FSLOAD): load the file the app was opened with (launch_arg) into a
; caller buffer. HL = dst (in the caller's page, which stays mapped through the
; FDC read - fsam_buf is resident, so no page swap), DE = max bytes. Returns
; BC = byte count (0 = missing / too big), CF set on success.
k_fsload
                ld    (fs_load_dst),hl
                ex    de,hl
                ld    (fs_load_max),hl
                call  focus_arg_ptr          ; load by the focused window's file name
                ld    de,fs_req_name
                call  copy11
                di
                call  fs_load_file
                ei
                ld    bc,0
                ret   nc                      ; not found / too big -> BC=0, NC
                ld    bc,(fs_ent_size)        ; content length (header already stripped)
                scf
                ret

; k_fssave (GB_FSSAVE): save the file the app was opened with (launch_arg). HL =
; src (caller's page, stays mapped), DE = byte count. Overwrites in place. CF set
; on success.
k_fssave
                ld    (fs_save_src),hl
                ex    de,hl
                ld    (fs_save_len),hl
                call  focus_arg_ptr          ; save by the focused window's file name
                ld    de,fs_req_name
                call  copy11
                di
                call  fs_save_file
                ei
                ret                            ; CF = saved

; k_getkey (GB_GETKEY): A = a typed character from the keyboard buffer, or 0 if
; none is waiting. Non-blocking (the firmware's IRQ scan fills the buffer).
; k_drive_poll (GB_DRIVES, #65): probe the drives GEOBENCH can reach and return a
; bitmask in A: bit0 = floppy A, bit1 = floppy B, bit2 = Disk C (the hard volume),
; bit3 = Disk C is an Albireo SD/USB card (vs IDE) so the desktop can pick its icon
; (#104). Probing a floppy spins the motor + recalibrates (slow) - call on demand.
k_drive_poll
                ifdef PLATFORM_MSX
                jp    fsmx_drive_poll              ; actual DOS letters/media map
                else
                ifdef PLATFORM_PCW
                jp    fspc_probe_b                 ; #331: floppy A + a probed floppy B
                else
                ld    c,0                          ; result bits
                if STORAGE_ALBIREO
                call  fsalb_present                ; Albireo -> Disk C (bit2) + SD flag (bit3)
                jr    nc,kdp_a
                set   2,c
                set   3,c                          ; bit3 = Disk C is an Albireo SD/USB card
                else
                if STORAGE_M4                      ; #174: M4 -> Disk C (bit2) + SD card icon (bit3)
                call  fsm4_present
                jr    nc,kdp_a
                set   2,c
                set   3,c
                jr    kdp_a
                else
                call  fs_ide_present               ; IDE -> Disk C (bit2)
                jr    nc,kdp_a
                set   2,c
                endif
                endif
kdp_a
                push  bc
                xor   a                             ; floppy A = unit 0
                ld    (fsam_unit),a
                call  fsam_present
                pop   bc
                jr    nc,kdp_b
                set   0,c
kdp_b
                push  bc
                ld    a,1                           ; floppy B = unit 1
                ld    (fsam_unit),a
                call  fsam_present
                pop   bc
                jr    nc,kdp_done
                set   1,c
kdp_done
                xor   a                             ; leave the floppy backend on unit 0
                ld    (fsam_unit),a
                ld    a,c
                ret
                endif                               ; (PLATFORM_PCW drive poll)
                endif                               ; (PLATFORM_MSX drive poll)

; k_get_drive (GB_GETDRIVE, #65): A = the current active drive (0=IDE/C, 1=A, 2=B).
; A window reads this at startup to learn which drive it was opened on. (GB_SETDRIVE
; jumps straight to fs_set_drive.)
k_get_drive
                ld    a,(fs_cur_drive)
                ret

; Cross-drive copy (#65 phase 3): the orchestration moved into the File Manager (C)
; to reclaim resident space (#74). The kernel just flips the active drive+dir context
; between the dragged file's source (captured at drag start) and the drop target; the
; app does the load->save itself through the shared staging buffer (GBFAT_DATA).
;
; k_copy_begin (GB_COPYBEGIN): save the current (target) drive+dir, then switch to the
; source drive+dir, so the caller can load the dragged file. Pairs with k_copy_end.
k_copy_begin
                ld    a,(fs_cur_drive)        ; save the caller's (target) context
                ld    (cb_tdrv),a
                ld    hl,fs_dir_clus
                ld    de,cb_tdir
                ld    bc,4
                ldir
                ld    a,(WM_DRAGDRV)         ; switch to the drag source drive + dir
                call  fs_set_drive
                ld    hl,WM_DRAGDIR
                ld    de,fs_dir_clus
                ld    bc,4
                ldir
                ret
; k_copy_end (GB_COPYEND): restore the saved (target) drive+dir, so the caller can
; re-list its own directory.
k_copy_end
                ld    a,(cb_tdrv)
                call  fs_set_drive
                ld    hl,cb_tdir
                ld    de,fs_dir_clus
                ld    bc,4
                ldir
                ret
cb_tdrv         equ   #123F        ; low-RAM kernel scratch (see lowram.tsv)
cb_tdir         equ   #1481        ; #188: relocated to low RAM (was resident defs 4)

; k_fs_delete (GB_FSDELETE, #62): HL = 11-byte 8.3 name in the caller page. Delete
; that file from the current directory (free clusters + clear the dir entry) via
; the paged GBFAT module. CF set = deleted. Used by drag-to-Trash.
k_fs_delete
                ld    de,fs_req_name
                call  copy11
                call  fs_delete_file
                ret

                ifndef PLATFORM_MSX           ; (MSX k_getkey lives in lib/msx/input.asm)
                ifndef PLATFORM_PCW           ; (PCW k_getkey lives in lib/pcw/input.asm)
k_getkey
                call  KM_READ_CHAR           ; CF + A = char, or NC = none. (Apps frame
                jr    nc,kgk_none            ; on IY now, so KM_READ_CHAR clobbering IX
                                              ; is harmless - see build_capp.sh.)
                ; The pointer is the joystick/cursor keys, and the firmware also
                ; buffers those as characters - so moving the pointer would flood a
                ; keyboard app (Notepad redrawing on every move). While any pointing
                ; direction is physically held, the "char" is the pointer, not
                ; typing: drop it. KM_TEST_KEY preserves B/DE/HL.
                push  af                     ; save char; KM_TEST_KEY may corrupt A.
                ld    hl,kgk_dirkeys
                ld    b,(hl)
kgk_test
                inc   hl
                ld    a,(hl)
                push  hl
                push  bc
                call  KM_TEST_KEY
                pop   bc
                pop   hl
                jr    nz,kgk_drop            ; a direction held -> discard the char
                djnz  kgk_test
                pop   af                     ; a genuine typed character
                ret
kgk_drop
                pop   af
kgk_none
                xor   a
                ret
                endif
                endif                          ; (ifndef PLATFORM_MSX around k_getkey)
kgk_dirkeys     db    10, 0,1,2,8, 72,73,74,75, 76,77 ; cursor + joystick dirs + fire
                                              ; (fire = the click; it also buffers a
                                              ; char, e.g. 'Z' - drop it while held)

; k_vsync (old GB_VSYNC): retired (#274) - apps are frame-paced by the WM loop.
; The #8048 ABI slot is now GB_PICEDIT under WM_GADGETS (#288); GB_BLITE remains
; the shared k_ret0 ("no ESC" A=0 return). Addresses stay fixed.

; k_onevent (GB_ONEVENT): retired (#274) - no caller left; apps register their handler
; via the gb_win_t descriptor (wm_register/k_wm_managed write APP_HANDLER through
; wm_map_focus). The slot -> k_noop; the APP_HANDLER cell itself stays core WM state.

; k_onrepaint (GB_ONREPAINT): now a no-op kept for ABI. Under the window manager
; an app's repaint handler lives in its gb_win_t (on_repaint); the old per-depth
; REPAINT_HDLR table is unused. The slot stays fixed and -> k_noop (#148).

; k_restore_parent (GB_RESTPAR): repaint everything behind the caller. Under the
; window manager (issue #45) the live windows below a modal app are exactly the WM
; windows, so this repaints them all bottom-up (wm_repaint_all) - e.g. a modal
; Notepad dragging its window restores the desktop + file manager underneath, then
; redraws itself on top. The jump table goes straight to wm_repaint_all now (#148,
; reclaimed the k_restore_parent wrapper).

; ---- cooperative window manager (issue #45, phase 2) --------------------------
; The kernel owns the master loop (wm_loop) and a table of co-resident windows.
; The root window (desktop) enters the loop via GB_WMRUN; further windows are
; opened non-blocking with GB_WMOPEN (load app -> its main calls GB_WMADD and
; returns). The loop polls, then on a fresh click hit-tests the windows top-down
; (z-order) and raises+focuses the one clicked, then calls the focused window's
; on_frame. A window closes itself with GB_WMCLOSE.
;
; A descriptor is { u8 x,y,w,h; on_frame(2); on_repaint(2); on_event(2) }.

; wm_register: HL = descriptor in the caller's page. Allocate a table slot, store
; page = caller, copy the descriptor, mark alive, push onto z-order top and focus.
; --- z-order manager (#148): the ONLY code that mutates WM_Z / WM_NWIN. ---------
; Three callers (register/close/raise) used to open-code the compaction; one held
; the slot in C across wm_free_page (which does ld c,a) and dropped the wrong
; window. Centralising it kills that bug class and dedups the loops.
;
; wm_z_append: A = slot -> WM_Z[WM_NWIN++] = slot (new z-top).
wm_z_append
                if PREEMPTIVE
                ld    hl,WM_NWIN
                ld    e,(hl)                       ; preserve/return A = slot for claimed drops
                inc   (hl)                          ; NWIN++
                ld    d,0
                ld    hl,WM_Z
                add   hl,de                         ; HL = &WM_Z[NWIN]
                ld    (hl),a
                else
                ld    b,a                          ; B = slot
                ld    hl,WM_NWIN
                ld    a,(hl)                       ; A = NWIN
                inc   (hl)                          ; NWIN++
                ld    hl,WM_Z
                add   a,l
                ld    l,a                           ; HL = &WM_Z[NWIN]
                ld    (hl),b
                endif
                ret

; wm_z_remove: C = slot -> compact WM_Z dropping slot C, dec NWIN once. Keeps C.
; Clobbers A,B,DE,HL (NOT C - callers may still need the slot).
wm_z_remove
                ld    hl,WM_Z
                ld    de,WM_Z
                ld    a,(WM_NWIN)
                ld    b,a
wzr_l           ld    a,(hl)
                cp    c
                jr    z,wzr_skip
                ld    (de),a
                inc   de
wzr_skip        inc   hl
                djnz  wzr_l
                ld    hl,WM_NWIN
                dec   (hl)
                ret

; wm_focus_top: WM_FOCUS = WM_Z[NWIN-1] (the live z-top). NWIN>=1 (desktop is [0]).
; The z-top is always alive (the manager keeps WM_Z == the live slots).
wm_focus_top
                ld    a,(WM_NWIN)
                dec   a
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)
                ld    (WM_FOCUS),a
                ret

wm_register
                ld    (wm_desc),hl
                call  wm_free_slot               ; A = first dead slot
                ld    (wm_slot),a
                call  wm_entry                    ; HL = WM_TABLE[slot]
                ld    a,(bank_cur)               ; +0 page = caller
                ld    (hl),a
                inc   hl
                ex    de,hl                       ; DE = entry+1
                ld    hl,(wm_desc)
                ld    bc,12                       ; x,y,w,h,on_frame,on_repaint,on_event,menu
                ldir
                ld    a,1                          ; entry+13 flags = alive
                ld    (de),a
                inc   de                            ; entry+14 = arg: capture the pending
                ld    hl,launch_arg               ; launch arg as this window's own file
                call  copy11
                ld    a,(wm_slot)                 ; focus + append the new window (z-top)
                ld    (WM_FOCUS),a
                jr    wm_z_append                  ; A = slot; tail-call (rets to wm_register's caller)

; ===== managed windows (#146): the kernel owns the chrome ====================
; k_wm_managed (GB_WMMANAGED): HL = a gb_mwin_t descriptor in the caller's page.
; Register a window the WM draws + drives: the descriptor pointer goes in WM_FR_FRAME
; and FLAGS gets MW_MANAGED, so wm_loop / wm_repaint_all route it to wm_chrome_frame /
; wm_chrome_draw instead of the app's on_frame/on_repaint. Descriptor layout:
;   +0 x +1 y +2 w +3 h +4 min_w +5 min_h +6 proc +8 title
;   +10 task_worker (0 for normal managed windows)
k_wm_managed
                call  wm_register            ; HL = desc; register as a normal window (slot,
                                             ; page, focus, z-order, arg, flags=alive; copies
                                             ; desc[0..11] -> entry+1..12). wm_register stashed
                                             ; the desc ptr in wm_desc. Now patch for managed:
                ld    a,(WM_FOCUS)           ; the just-registered window
                call  wm_entry               ; HL = entry
                push  hl
                ld    de,WM_FR_FLAGS         ; FLAGS |= managed
                add   hl,de
                set   1,(hl)
                pop   hl
                push  hl
                ld    de,WM_FR_FRAME         ; WM_FR_FRAME (entry+5,6) = the descriptor ptr
                add   hl,de
                ld    de,(wm_desc)
                ld    (hl),e
                inc   hl
                ld    (hl),d
                pop   hl
                ld    de,WM_FR_EVENT         ; WM_FR_EVENT = desc.on_event (so bar clicks/drops
                add   hl,de                  ; reach the app's menu handler via the normal path)
                push  hl                     ; HL = entry+9
                ld    hl,(wm_desc)
                ld    de,6                   ; WM_FR_EVENT = desc.proc (#148): menu/drop come
                add   hl,de                  ; through here too, so every message reaches the
                ld    e,(hl)                 ; one WndProc (keyed by gb_msg.type)
                inc   hl
                ld    d,(hl)                 ; DE = proc ptr
                pop   hl                     ; HL = entry+9
                ld    (hl),e
                inc   hl
                ld    (hl),d                 ; REPAINT (entry+7,8) is garbage but the managed
                                             ; flag means wm_repaint_all skips it; MENU (entry+
                                             ; 11,12) temporarily contains task_worker but gb_doc
                                             ; replaces it before the app returns to the WM loop
                ld    a,(WM_FOCUS)           ; publish MW_RECT so the app can read gb_wm_x/y/w/h
                call  wm_entry               ; in main, but DON'T draw yet: the app loads its
                call  mw_publish             ; content then calls gb_restore_parent for the first
                                             ; paint, so a window never shows empty during a slow
                                             ; load (#146). A managed app MUST paint when ready.
                                             ; mw_publish preserves A (still = WM_FOCUS), so:
                jp    wm_set_clip            ; #153: pre-set the clip to the new window's rect so
                                             ; that first gb_restore_parent repaints only OUR area,
                                             ; not the whole desktop (the open "flash"); then
                                             ; wm_repaint_all restores the full-screen clip.

; mw_publish: HL = entry. Copy x,y,w,h -> MW_RECT (the app reads it via gb_wm_x/y/w/h);
; (mw_desc) = the descriptor pointer. HL preserved.
mw_publish
                push  hl
                inc   hl                     ; entry+1
                ld    de,MW_RECT
                ld    bc,4
                ldir                         ; MW_RECT = x,y,w,h ; HL = entry+5
                ld    e,(hl)
                inc   hl
                ld    d,(hl)
                ld    (mw_desc),de           ; desc ptr
                pop   hl
                ret

; mw_hook: A = a GB_MSG_* window message. Set gb_msg.type, then dispatch to the
; window's single proc (desc+6). The proc switches on the type. Clobbers HL,DE,A.
mw_hook
                ld    (GB_MSG),a              ; gb_msg.type = the message being delivered
                ld    hl,(mw_desc)
                ld    de,6
                add   hl,de                  ; HL = &desc.proc
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a                     ; HL = proc ptr
                ld    a,h
                or    l
                ret   z                       ; no proc -> skip (shouldn't happen)
                jp    md_call                 ; jp (hl); the proc rets to mw_hook's caller

; wm_chrome_draw: HL = entry. Draw frame+title (gb_open_window) then the content (on_draw).
wm_chrome_draw
                call  mw_publish
                ld    hl,(mw_desc)           ; title = *(desc+8)
                ld    de,8
                add   hl,de
                ld    e,(hl)
                inc   hl
                ld    d,(hl)
                ex    de,hl                  ; HL = title ptr (caller page)
                ld    a,(MW_RECT)
                ld    b,a                     ; x
                ld    a,(MW_RECT+1)
                ld    c,a                     ; y
                ld    a,(MW_RECT+2)
                ld    d,a                     ; w
                ld    a,(MW_RECT+3)
                ld    e,a                     ; h
                call  gb_open_window         ; frame + title
                ld    a,GB_MSG_DRAW          ; -> the proc draws the content
                jr    mw_hook

; wm_chrome_frame: HL = entry. The per-frame router: idle hook, then QUIT/close,
; close-gadget, content click. (Title drag + grip resize land in slice 2.)
wm_chrome_frame
                call  mw_publish
                ld    a,GB_MSG_FRAME         ; per-frame (idle/menus/tick)
                call  mw_hook
                ld    a,(POLL_FLAGS)
                bit   1,a                     ; GB_QUIT
                jp    nz,mw_do_close          ; (jp: mwf_max grew the block past jr range)
                bit   0,a                     ; GB_CLICK
                ret   z
                ld    a,(POLL_MY)            ; my - win_y in title band?
                ld    e,a
                ld    a,(MW_RECT+1)
                ld    d,a
                ld    a,e
                sub   d
                jr    c,mwf_content           ; my < win_y
                cp    14                       ; TITLE_H
                jr    nc,mwf_content           ; my >= win_y+14 -> content
                ld    a,(POLL_MX)            ; in title bar: which gadget?
                ld    e,a
                ld    a,(MW_RECT)
                add   a,5
                cp    e
                jr    c,mwf_notclose          ; win_x+5 < mx -> not the close gadget
                jr    z,mwf_notclose
                jr    mw_do_close             ; mx < win_x+5 -> close gadget
mwf_notclose
                ifdef WM_GADGETS
                ld    a,(MW_RECT)            ; maximize gadget? mx >= win_x + win_w - 4
                ld    hl,MW_RECT+2
                add   a,(hl)                  ; A = win_x + win_w
                sub   4
                cp    e
                jr    c,mwf_max              ; (win_x+win_w-4) < mx -> maximize/restore
                jr    z,mwf_max
                endif
mwf_title
                ld    a,GB_MSG_DRAG          ; otherwise a title-bar press -> drag the window
                jp    mw_hook
                ifdef WM_GADGETS
; mwf_max: the maximize/restore gadget does a WINDOWED maximize (chrome stays) - distinct from
; the borderless View>Fullscreen / 'F' (the app's on_fullscreen). Toggle the focused window
; between its saved size and full-below-the-bar (0,8 .. 80x192); flags bit2 (MW_MAXED) tracks
; which way. The pre-max geometry is one global, so restoring the earlier of two maximized
; windows would use the later one's size - a rare case not worth a per-window store.
mwf_max         ld    a,(WM_FOCUS)
                call  wm_entry               ; HL = focused entry
                push  hl
                ld    de,WM_FR_FLAGS
                add   hl,de
                bit   2,(hl)                  ; already maximized?
                jr    nz,mwf_unmax
                set   2,(hl)
                pop   hl                      ; HL = entry; save geom, then fill the screen
                push  hl
                inc   hl
                ld    a,(hl)
                ld    (wm_sav_x),a
                ld    (hl),0
                inc   hl
                ld    a,(hl)
                ld    (wm_sav_y),a
                ld    (hl),8
                inc   hl
                ld    a,(hl)
                ld    (wm_sav_w),a
                ld    (hl),SCR_COLS
                inc   hl
                ld    a,(hl)
                ld    (wm_sav_h),a
                ld    (hl),SCR_LINES-8
                jr    mwf_max_paint
mwf_unmax       res   2,(hl)
                pop   hl                      ; HL = entry; restore the saved geometry
                push  hl
                inc   hl
                ld    a,(wm_sav_x)
                ld    (hl),a
                inc   hl
                ld    a,(wm_sav_y)
                ld    (hl),a
                inc   hl
                ld    a,(wm_sav_w)
                ld    (hl),a
                inc   hl
                ld    a,(wm_sav_h)
                ld    (hl),a
mwf_max_paint   pop   hl                      ; HL = entry
                call  mw_publish             ; refresh MW_RECT (the app reads gb_wm_x/y/w/h)
                call  clip_set_full
                jp    wm_repaint_all
wm_sav_x        db    0
wm_sav_y        db    0
wm_sav_w        db    0
wm_sav_h        db    0
                endif
mwf_content
                ld    a,GB_MSG_CLICK         ; content (incl. grip) -> a content press
                jp    mw_hook

; mw_do_close: a close was requested - deliver GB_MSG_CLOSE to the window's proc
; (it confirms + calls gb_wm_close). Every managed window has a proc, so there is
; no "no handler" path; mw_hook's null guard falls through to no-op if it's ever 0.
mw_do_close
                ld    a,GB_MSG_CLOSE
                jp    mw_hook

mw_desc         dw    0                       ; scratch: the focused managed window's descriptor

; wm_free_slot: -> A = lowest table slot whose alive flag is clear.
wm_free_slot
                ld    hl,WM_TABLE+WM_FR_FLAGS
                ld    de,WM_ESZ
                ld    c,0
wfs_l           ld    a,(hl)
                and   1
                jr    z,wfs_found
                add   hl,de
                inc   c
                ld    a,c
                cp    WM_MAXWIN
                jr    c,wfs_l
                ld    c,WM_MAXWIN-1               ; full (shouldn't happen) -> reuse last
wfs_found       ld    a,c
                ret

; k_wm_run (GB_WMRUN): register the caller (the root/desktop window) and enter the
; master loop. Never returns.
k_wm_run
                call  wm_register
                if PREEMPTIVE_CONTEXT
                xor   a                           ; first registration is always root slot zero
                ld    (SCHED_CURRENT),a
                inc   a
                ld    (SCHED_RUNNABLE),a
                endif
wm_loop
                call  wm_map_focus               ; bank focused page; APP_HANDLER = its
                call  k_poll                      ; on_event, so a top-bar click reaches
                call  wm_focus_click             ; the right app. then route the click.
                call  wm_map_focus               ; focus may have changed
                call  clip_set_full              ; #281: each window's frame starts from a full clip.
                                                 ; A direct-drawing full-screen app (a .SAV saver) never
                                                 ; goes through wm_repaint_all (which restores the clip),
                                                 ; so without this it inherits the stale damage clip the
                                                 ; desktop bar / a menu action left (#153/#279) - and
                                                 ; clip_fb_copy's 8-bit maths turns that into full-width
                                                 ; fill bands. Managed windows set their own damage
                                                 ; in-frame, so a full default here is harmless.
                ld    a,(WM_FOCUS)               ; call the focused window's on_frame
                call  wm_entry                   ; HL = entry
                push  hl                          ; #146: managed window -> kernel router
                ld    de,WM_FR_FLAGS
                add   hl,de
                if PREEMPTIVE_CONTEXT
                ld    a,(hl)
                pop   hl
                bit   1,a
                else
                bit   1,(hl)
                pop   hl                          ; HL = entry
                endif
                jr    z,wmf_legacy
                call  wm_chrome_frame
                jr    wmf_done
wmf_legacy
                ld    de,WM_FR_FRAME
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                call  md_call
wmf_done
                ld    hl,(BAR_HANDLER)            ; top-bar hook (#77): run the bar handler every
                ld    a,h                          ; frame in the desktop's page so the bar stays
                or    l                            ; live regardless of which window has focus.
                if PREEMPTIVE_CONTEXT
                jr    z,wm_loop_tail               ; bar_draw only repaints lines 0-7 when the clock
                else
                jr    z,wm_loop
                endif
                ifdef PLATFORM_MSX                 ; (#287: APP_PAGES[0] = the TPA segment)
                ld    a,(MSX_TPASEG)
                else
                ld    a,PAGE_APP0
                endif                              ; minute or menu actually changes (and brackets
                call  bank_set                     ; its own redraws with gb_curhide/show), so the
                ld    hl,(BAR_HANDLER)            ; pointer over the bar is left untouched - do NOT
                call  md_call                      ; erase/show it every frame here: that landed the
                if PREEMPTIVE_CONTEXT
wm_loop_tail
                ld    a,(SCHED_RUNNABLE)
                cp    2                            ; avoid a stack copy when the desktop is alone
                jr    c,wm_loop
                ld    a,(WM_TABLE)                 ; system task snapshot belongs to the root bank
                call  bank_set
                call  SCHED_YIELD_ENTRY
                endif
                jr    wm_loop                      ; erase at the beam and dropped the bar pointer.

; k_on_bar (GB_ONBAR #77): HL = a handler the WM loop runs every frame in PAGE_APP0
; (the desktop's page) to draw the top bar, independent of focus. 0 to clear.
k_on_bar
                ld    (BAR_HANDLER),hl
                ret

; GB_WMADD registers the caller's window (legacy gb_wm_add); the jump table goes
; straight to wm_register now (#148, reclaimed the k_wm_add wrapper).

; k_wm_open (GB_WMOPEN): HL = 8.3 app name in the caller page. Load it into a free
; bank page and CALL its entry once (its main registers a window via GB_WMADD and
; returns), then restore the caller's page. Non-blocking: the app becomes a live
; window the master loop services, rather than running its own loop.
k_wm_open
                ld    de,fs_req_name
                call  copy11
                ld    hl,launch_arg               ; opened with no file -> blank the arg
                ld    b,11                          ; so the app starts file-less
                xor   a
kwo_blank       ld    (hl),a
                inc   hl
                djnz  kwo_blank
                jr    wm_open_go

; k_wm_launch (GB_WMLAUNCH): superseded. File-type -> app routing moved into the
; File Manager (C); it now calls GB_WMLAUNCHAS with the app it chose. The slot
; stays fixed (addresses) and -> k_noop (#148).

; k_wm_launch_as (GB_WMLAUNCHAS): HL = the 8.3 app name to open as a co-resident
; window. The current dir entry (fs_ent_name) becomes the new window's file arg, so
; a data file auto-opens in the app the caller chose (the File Manager picks it by
; extension - see apps/filemgr/main.c).
k_wm_launch_as
                push  hl                           ; app name
                ld    hl,fs_ent_name              ; current entry -> the launch file arg
                ld    de,launch_arg
                call  copy11
                pop   hl                           ; app name -> fs_req_name
                ld    de,fs_req_name
                call  copy11
wm_open_go
                call  wm_alloc_page
                or    a
                ret   z                            ; no free page
                ld    (wm_open_page),a
                ld    a,(bank_cur)
                ld    (wm_open_back),a
                di
                ld    a,(wm_open_page)
                call  bank_set
                ld    hl,APP_LOAD_MAX
                ld    (fs_load_max),hl
                ld    hl,APP_BASE
                ld    (fs_load_dst),hl
                ld    a,(WM_OPEN_STRICT)
                or    a
                jr    z,wmo_load_sys
                xor   a
                ld    (WM_OPEN_STRICT),a
                call  fs_load_cur_sys             ; strict open: current drive, no boot fallback
                jr    wmo_loaded
wmo_load_sys    call  fs_load_sys                 ; normal app open: boot-first, browse-fallback
wmo_loaded      jr    nc,wmo_fail
                ei
                call  APP_BASE                    ; main -> GB_WMADD + paint, then ret
                di
                ld    a,(wm_open_back)
                call  bank_set
                ei
                ret
wmo_fail
                ld    a,(wm_open_back)
                call  bank_set
                ld    a,(wm_open_page)
                call  wm_free_page
                ei
                ret

; k_wm_close (GB_WMCLOSE): close the focused (calling) window - free its page, drop
; it from the table and z-order, refocus the new z-top, and repaint. The caller's
; on_frame must return immediately after. Mapping is unchanged on return so the
; closing on_frame can still execute (its page content survives until reused).
k_wm_close
                ld    a,(WM_FOCUS)
                ld    c,a                          ; C = slot being closed
                call  wm_set_clip                 ; damage only the rectangle being exposed;
                                                  ; B = page, HL = closing entry+4
                ld    a,b                          ; page
                push  af                           ; save it: wm_free_page clobbers everything
                ld    de,WM_FR_FLAGS-4
                add   hl,de
                if PREEMPTIVE_CONTEXT
                bit   3,(hl)
                jr    z,kwc_not_task
                push  hl
                ld    hl,SCHED_RUNNABLE
                dec   (hl)
                pop   hl
kwc_not_task
                endif
                ld    (hl),0                       ; mark dead (clear the alive flag)
                call  wm_z_remove                 ; drop slot C from the z-order (keeps C)
                call  wm_focus_top                ; refocus the new live z-top
                cpl                                ; force wm_map_focus to reinstall its menu even
                ld    (WM_FPREV),a                ; if this window closed before its first frame
                call  wm_map_focus               ; install the new focus's handler + menu NOW,
                                                  ; before we repaint/return. This avoids a dead
                                                  ; top bar after closing a no-menu child that
                                                  ; dirtied global menu state via a modal picker.
                pop   af                           ; A = the closed window's page
                call  bank_set                     ; wm_map_focus mapped the new focus, but the
                                                  ; closing handler must return in its own page;
                                                  ; bank_set preserves A for wm_free_page
                call  wm_free_page                 ; release it (z-order already updated)
                jp    wm_repaint_all               ; repaint remaining layers only inside that rect

; k_wm_setpos (GB_WMSETPOS): A = x, L = y -> move the focused window's hit rect to
; (x,y), so click-to-focus follows a window the app has dragged. Also sets the fill
; clip to the damage = the bounding box of the old and new window rects, so the
; following gb_restore_parent only repaints there (no full-screen flicker).
k_wm_setpos
                ld    (sp_x),a                    ; new x
                ld    a,l
                ld    (sp_y),a                    ; new y
                ld    a,(WM_FOCUS)
                call  wm_entry                    ; HL = entry (+0); damage_axis keeps HL
                inc   hl                            ; +1 old x
                ld    b,(hl)
                inc   hl
                inc   hl                            ; +3 w
                ld    c,(hl)
                ld    a,(sp_x)
                call  damage_axis                 ; D = min(ox,nx), E = span
                ld    a,d
                ld    (clip_x),a
                ld    a,e
                ld    (clip_w),a
                dec   hl                            ; +2 old y
                ld    b,(hl)
                inc   hl
                inc   hl                            ; +4 h
                ld    c,(hl)
                ld    a,(sp_y)
                call  damage_axis
                ld    a,d
                ld    (clip_y),a
                ld    a,e
                ld    (clip_h),a
                dec   hl                            ; +4 -> +1 (x)
                dec   hl
                dec   hl
                ld    a,(sp_x)                     ; commit the new position
                ld    (hl),a
                inc   hl
                ld    a,(sp_y)
                ld    (hl),a
                ret
sp_x            equ   #124B        ; low-RAM WM scratch (see lowram.tsv)
sp_y            equ   #124C

; k_wm_setsize (GB_WMSETSIZE, #81): A = new w, L = new h. Resize the focused window's
; rect (top-left stays put). Damage clip = its (x,y) covering max(old,new) size, so the
; following gb_restore_parent repaints any area a shrink vacated.
k_wm_setsize
                ld    (ss_w),a
                ld    a,l
                ld    (ss_h),a
                ld    a,(WM_FOCUS)
                call  wm_entry                    ; HL = entry (+0 page)
                inc   hl                            ; +1 x
                ld    a,(hl)
                ld    (clip_x),a                  ; clip x = window x (unchanged)
                inc   hl                            ; +2 y
                ld    a,(hl)
                ld    (clip_y),a
                inc   hl                            ; +3 w
                ld    b,(hl)                       ; old w
                ld    a,(ss_w)                     ; clip w = max(old, new)
                cp    b
                jr    nc,kss_w
                ld    a,b
kss_w
                ld    (clip_w),a
                ld    a,(ss_w)
                ld    (hl),a                       ; commit new w
                inc   hl                            ; +4 h
                ld    b,(hl)                       ; old h
                ld    a,(ss_h)
                cp    b
                jr    nc,kss_h
                ld    a,b
kss_h
                ld    (clip_h),a
                ld    a,(ss_h)
                ld    (hl),a                       ; commit new h
                ret
ss_w            equ   #124D        ; low-RAM WM scratch (see lowram.tsv)
ss_h            equ   #124E

; damage_axis: B = old, A = new, C = size -> D = min(old,new), E = span =
; max(old,new) + size - min. The 1-D damage extent of a move. Preserves HL.
damage_axis
                cp    b
                jr    nc,dax_oldmin              ; new >= old -> min = old, max = new
                ld    d,a                           ; min = new
                ld    a,b                           ; max = old
                jr    dax_span
dax_oldmin      ld    d,b                           ; min = old (A = new = max)
dax_span        add   a,c                           ; max + size
                sub   d                             ; - min
                ld    e,a
                ret

; wm_map_focus: bank to the focused window's page and point APP_HANDLER at its
; on_event (so menu_dispatch in k_poll delivers top-bar clicks to the focused app).
; On a focus change, also swap the top-bar menu to the focused window's (or clear
; it) so the bar shows the right menu and clicks reach the right handler.
wm_map_focus
                ld    a,(WM_FOCUS)
                call  wm_entry                    ; HL = entry
                ld    a,(hl)                       ; page (+0)
                call  bank_set                     ; preserves HL
                push  hl
                ld    de,WM_FR_EVENT
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    (APP_HANDLER),hl
                pop   hl                            ; HL = entry
                ld    a,(WM_FOCUS)               ; focus changed since last frame?
                ld    de,WM_FPREV
                ld    a,(de)
                ld    b,a
                ld    a,(WM_FOCUS)
                cp    b
                ret   z                            ; no change -> leave the bar as is
                ld    (de),a                       ; WM_FPREV = focus
                ld    de,WM_FR_MENU               ; focused window's menu def ptr
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    a,h
                or    l
                jp    z,menu_clear                 ; no menu -> empty the bar
                jp    menu_install                 ; install the focused window's menu

; wm_focus_click: if a fresh click landed on a window other than the focused one,
; move focus there. App windows also rise to the z-top (and the stack repaints) if
; not already on top; the desktop (slot 0) stays pinned at the bottom. The click is
; not consumed - it is then delivered to the newly focused window's on_frame.
wm_focus_click
                ld    a,(POLL_FLAGS)
                bit   0,a
                ret   z                            ; no fresh click
                call  wm_hit_test                  ; CF set = no window hit (e.g. top bar)
                ret   c
                ld    b,a
                ld    a,(WM_FOCUS)
                cp    b
                ret   z                            ; already focused -> deliver
                ld    a,b
                ld    (WM_FOCUS),a               ; focus the clicked window
                or    a
                ret   z                            ; desktop: keep it at the bottom
                ld    c,a                          ; c = clicked app slot
                ld    a,(WM_NWIN)               ; already the z-top? then nothing to raise
                dec   a
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)
                cp    c
                ret   z
                ld    a,c
                call  wm_raise                     ; bring it to the front
                ld    a,c                          ; damage = the raised window's rect
                call  wm_set_clip
                jp    wm_repaint_top               ; opaque top window: avoid repainting layers through it

; wm_hit_test: -> A = slot of the top-most window whose rect contains the pointer
; (POLL_MX, POLL_MY), scanning z-order top->bottom; CF set if none.
wm_hit_test
                ld    a,(WM_NWIN)
                ld    (wm_hz),a
wht_l           ld    a,(wm_hz)
                or    a
                jr    z,wht_none
                dec   a
                ld    (wm_hz),a
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)                       ; slot = WM_Z[i]
                call  wm_entry                     ; HL = entry
                inc   hl                            ; +1 x
                ld    a,(POLL_MX)
                sub   (hl)
                jr    c,wht_l                       ; mx < x
                ld    c,a                            ; mx - x
                inc   hl                            ; +2 y
                inc   hl                            ; +3 w
                ld    a,c
                cp    (hl)
                jr    nc,wht_l                      ; mx-x >= w
                dec   hl                            ; +2 y
                ld    a,(POLL_MY)
                sub   (hl)
                jr    c,wht_l                       ; my < y
                ld    c,a                            ; my - y
                inc   hl                            ; +3 w
                inc   hl                            ; +4 h
                ld    a,c
                cp    (hl)
                jr    nc,wht_l                      ; my-y >= h
                ld    a,(wm_hz)                     ; hit -> recover slot
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)
                or    a                             ; clear CF
                ret
wht_none        scf
                ret

; k_drag_start (GB_DRAGSTART, #62): HL = 11-byte 8.3 name of the dragged item in
; the caller (source) page. Record it, then run a drag loop (the WM loop is
; suspended) that follows the pointer until the fire button is released. On release,
; if the pointer moved and the drop point is over a different live window that has
; an on_event handler, deliver a GB_MSG_DROP to it (its page mapped) - it reads the
; name from WM_DRAGNAME and the drop position from gb_mx/gb_my. Returns A = 1 if it
; was dropped on a target, A = 0 if it was just a click (no move / no target /
; dropped on itself) so the source can fall through to its normal click handling.
k_drag_start
                ld    de,WM_DRAGNAME              ; stash the dragged name
                call  copy11
                ld    a,(bank_cur)               ; remember the source page to restore
                ld    (wm_drag_pg),a
                ld    a,(WM_FOCUS)               ; source = the focused (calling) window
                ld    (WM_DRAGSRC),a
                ld    a,(fs_cur_drive)           ; capture the source drive + directory now
                ld    (WM_DRAGDRV),a             ; (active = the source FM's) for a later
                ld    hl,fs_dir_clus             ; cross-drive copy (#65 phase 3)
                ld    de,WM_DRAGDIR
                ld    bc,4
                ldir
                xor   a
                ld    (WM_DRAGMOV),a             ; moved = 0, ghost not shown yet
                ld    (ghost_on),a
                ld    a,(POLL_MX)                ; seed the last position
                ld    (WM_DRAGX0),a
                ld    a,(POLL_MY)
                ld    (WM_DRAGY0),a
ds_loop
                call  k_poll                      ; pace 50Hz + track pointer + POLL_*
                ld    a,(POLL_MX)                ; moved since last frame?
                ld    hl,WM_DRAGX0
                cp    (hl)
                jr    nz,ds_moved
                ld    a,(POLL_MY)
                ld    hl,WM_DRAGY0
                cp    (hl)
                jr    z,ds_held                   ; stationary -> leave the ghost as is
ds_moved        ld    a,(POLL_MX)                ; record the new last position
                ld    (WM_DRAGX0),a
                ld    a,(POLL_MY)
                ld    (WM_DRAGY0),a
                ld    a,1
                ld    (WM_DRAGMOV),a
                ld    a,(ghost_on)               ; ghost already up? -> move it
                or    a
                jr    nz,ds_gtrack
                call  cursor_erase                ; first movement: hide the arrow and
                ld    a,1                          ; show a box that follows the pointer
                ld    (cur_supp),a
                ld    (ghost_on),a
                ifdef PLATFORM_PCW
                ld    hl,#FFFF                     ; the ghost borrows fs_secbuf
                ld    (fsp_cslsn),hl               ; without changing its dir cache
                endif
                ld    a,GHOST_W                   ; the box's save-under is fixed-size in
                ld    (sb_w),a                     ; the idle IDE sector buffer; set once
                ld    a,GHOST_H
                ld    (sb_h),a
                ld    hl,fs_secbuf
                ld    (sb_buf),hl
                jr    ds_gpaint
ds_gtrack       call  restore_block               ; erase the box at its old place
ds_gpaint       call  ghost_place
                call  ghost_draw
ds_held         ld    a,(POLL_FLAGS)            ; fire still held -> keep dragging
                bit   2,a
                jr    nz,ds_loop
                ld    a,(ghost_on)               ; released: tear the ghost down and
                or    a                            ; bring the arrow back
                jr    z,ds_norel
                call  restore_block               ; erase the box
                xor   a
                ld    (ghost_on),a
                ld    (cur_supp),a               ; A = 0 -> cursor visible again
                call  cursor_draw
ds_norel
                ld    a,(WM_DRAGMOV)            ; never moved -> a plain click
                or    a
                jr    z,ds_click
                call  wm_hit_test                 ; CF = nothing under the drop point
                jr    c,ds_click
                ld    b,a                          ; B = target slot
                ld    a,(WM_DRAGSRC)
                cp    b
                jr    z,ds_click                   ; dropped on itself -> no-op
                ld    a,b
                ld    (WM_FOCUS),a                 ; target owns setname/fsload/fssave
                call  wm_entry                     ; HL = target entry
                ld    c,(hl)                       ; C = target page (+0)
                ld    de,WM_FR_EVENT               ; target's on_event handler
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    a,h
                or    l
                jr    nz,ds_drop
                ld    a,(WM_DRAGSRC)
                ld    (WM_FOCUS),a
                jr    ds_click                      ; no handler -> treat as a click
ds_drop
                ld    a,GB_MSG_DROP
                ld    (GB_MSG),a
                ld    a,c                           ; map the target's page, call it
                call  bank_set                      ; (it reads WM_DRAGNAME + POLL_MX/MY)
                call  md_call
                if PREEMPTIVE
                ld    a,(WM_DRAGMOV)                ; carry = target claimed this drop as a job
                add   a,a
                ld    a,(WM_DRAGSRC)                ; loading the source slot preserves carry
                jr    nc,ds_drop_source
                ld    a,(WM_FOCUS)                  ; claimed: keep and raise the target
                call  wm_raise
ds_drop_source
                ld    (WM_FOCUS),a                  ; commit target (claimed) or source (ordinary)
                else
                ld    a,(WM_DRAGSRC)
                ld    (WM_FOCUS),a
                endif
                ld    a,(wm_drag_pg)              ; restore the source page
                call  bank_set
                ld    a,1
                ret
ds_click
                ld    a,(wm_drag_pg)              ; ensure the source page is mapped
                call  bank_set
                xor   a
                ret
wm_drag_pg      equ   #12FF        ; low-RAM WM scratch (see lowram.tsv)

; ghost_place: sb_x/sb_y = the pointer (POLL_MX/MY), clamped so the GHOST_W x
; GHOST_H box stays on screen. sb_w/sb_h/sb_buf were set once when the ghost first
; appeared (they only change when the cursor save-under runs, which is suppressed
; during the drag), so erase = restore_block and draw = save_block + outline.
ghost_place
                ld    a,(POLL_MX)
                cp    SCR_COLS-GHOST_W+1
                jr    c,gpl_x
                ld    a,SCR_COLS-GHOST_W
gpl_x           ld    (sb_x),a
                ld    a,(POLL_MY)
                cp    SCR_LINES-GHOST_H+1
                jr    c,gpl_y
                ld    a,SCR_LINES-GHOST_H
gpl_y           ld    (sb_y),a
                ret

; ghost_draw: save the screen under the box, then draw a red outline there.
ghost_draw
                call  save_block
                ld    a,(sb_x)
                ld    b,a
                ld    a,(sb_y)
                ld    c,a
                ld    d,GHOST_W
                ld    e,GHOST_H
                ld    a,3
                jp    k_frame

ghost_on        equ   #124A        ; low-RAM WM scratch (see lowram.tsv)

; wm_raise: A = slot -> move it to z-order top and focus it (keeps the others'
; relative order). Compacts WM_Z removing the slot, then re-appends it at the end.
wm_raise
                ld    (WM_FOCUS),a
                ld    c,a
                call  wm_z_remove                 ; drop it from the z-order...
                ld    a,c
                jp    wm_z_append                  ; ...and re-append on top (tail-call)

; wm_repaint_all: repaint every window bottom-up (each in its own page, via its
; on_repaint), then restore the caller's page. The cursor is left to the handlers:
; each on_repaint redraws over the old pointer (a full backdrop fill, or a leading
; gb_curhide) and ends with gb_curshow, so the cursor stays correct with no double
; save-under here. WM_Z[0] is always the desktop, so its full paint runs first.
wm_repaint_all
                xor   a
                jr    wra_seed
; wm_repaint_top: repaint only the current z-top. Used after click-to-focus raises an
; opaque managed window and through GB_REPAINTTOP for a newly published managed
; window; redrawing lower layers first makes the stack visibly flash.
wm_repaint_top
                ld    a,(WM_NWIN)
                dec   a
wra_seed        ld    (wm_rp_i),a
                ld    a,(bank_cur)
                ld    (wm_rp_back),a
                ld    a,(cur_supp)              ; #148: hide the pointer before the repaint so it
                or    a                           ; can't pollute its save-under (else a later move
                call  z,cursor_erase             ; restores stale content = a pointer-sized hole).
                ld    a,1                         ; lock the cursor for the whole loop: app on_draw
                ld    (cur_paintlock),a           ; handlers also bracket with gb_curhide/show, and
                                                  ; that 2nd erase restores a stale save-under over
                                                  ; chrome a window drew earlier this pass (the XAOS
                                                  ; title hole, on File>New AND drag). Bracket ONCE.
                di                                ; Skip during a DnD ghost (cur_supp owns the screen)
wra_l           ld    a,(wm_rp_i)
                ld    hl,WM_NWIN
                cp    (hl)
                jr    nc,wra_done
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)                       ; slot = WM_Z[i]
                call  wm_entry                     ; HL = entry
                push  hl                           ; #148 guard: never paint a dead slot
                ld    de,WM_FR_FLAGS
                add   hl,de
                ld    a,(hl)                       ; flags
                pop   hl                            ; HL = entry
                bit   0,a                          ; alive?
                jr    z,wra_next                   ; dead -> skip (z-order should exclude it)
                push  af                           ; keep flags across bank_set
                ld    a,(hl)                       ; page
                call  bank_set                     ; (preserves HL = entry)
                pop   af                            ; #146: managed -> kernel draws chrome
                bit   1,a                          ; managed?
                jr    z,wra_legacy
                call  wm_chrome_draw
                jr    wra_next
wra_legacy
                ld    de,WM_FR_REPAINT
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    a,h
                or    l
                jr    z,wra_next                   ; no handler
                call  md_call
wra_next        ld    a,(wm_rp_i)
                inc   a
                ld    (wm_rp_i),a
                jr    wra_l
wra_done        ld    a,(wm_rp_back)
                call  bank_set
                call  clip_set_full              ; repaints are clip-limited; restore the
                ei                                ; full-screen clip for normal drawing
                xor   a                           ; unlock: loop done, now show the pointer ONCE
                ld    (cur_paintlock),a           ; over the final composited screen (fresh save-under)
                ld    a,(cur_supp)              ; #148: ensure the pointer is up with a FRESH
                or    a                           ; save-under over the just-repainted content.
                call  z,cursor_show               ; GUARDED (#153): a handler that ends in gb_curshow
                ret                                ; (e.g. the desktop paint) already drew it - don't
                                                  ; redraw, or we'd save cur_bg OVER the cursor's own
                                                  ; pixels and stamp a ghost on the next move.

; k_wm_damage (GB_WMDAMAGE): set the repaint clip to a caller-supplied damage rect,
; so the next gb_restore_parent only repaints that region instead of the whole
; screen (#153 - kills the full-desktop flash after a dropdown menu). gb_popup
; passes its box UNION the focused window's rect, so the repaint covers both the
; dropdown's footprint (the part that overhangs other windows) and the window the
; menu action changed. Registers are pre-swapped by the trampoline for two word
; stores: C=x B=y (-> clip_x,clip_y) and E=w D=h (-> clip_w,clip_h).
k_wm_damage
                ld    (clip_x),bc                 ; clip_x = x, clip_y = y
                ld    (clip_w),de                 ; clip_w = w, clip_h = h
                ret

; clip_set_full: reset the fill clip to the whole screen (no clipping). Two word
; stores (clobbers BC,DE - the only caller reloads A and ignores BC/DE, #153).
clip_set_full
                ld    bc,0                       ; clip_x = 0, clip_y = 0
                ld    (clip_x),bc
                ld    de,(SCR_LINES*256)|SCR_COLS ; clip_w / clip_h = the full screen
                ld    (clip_w),de
                ret

; wm_set_clip: A = slot -> set the fill clip to that window's rect plus one icon
; cell at the right. Returns B = page and HL = entry+4; close reuses both.
wm_set_clip
                call  wm_entry                    ; HL = entry
                ld    b,(hl)                       ; page for close
                inc   hl                            ; +1 x
                ld    a,(hl)
                ld    (clip_x),a
                inc   hl                            ; +2 y
                ld    a,(hl)
                ld    (clip_y),a
                inc   hl                            ; +3 w. Bitmap/glyph clipping culls whole
                ld    a,(hl)                        ; draw atoms: one which starts inside this
                add   a,8                           ; damage may extend one 32px icon cell right.
                ld    (clip_w),a
                inc   hl                            ; +4 h
                ld    a,(hl)
                ld    (clip_h),a
                ret

; wm_entry: A = slot index -> HL = WM_TABLE + A*WM_ESZ. Clobbers A,B,DE.
wm_entry
                ld    hl,WM_TABLE
                or    a
                ret   z
                ld    b,a
                ld    de,WM_ESZ
we_add
                add   hl,de
                djnz  we_add
                ret

wm_desc         equ   #12F7        ; low-RAM WM scratch (see lowram.tsv)
wm_slot         equ   #12F9
wm_open_page    equ   #12FA
wm_open_back    equ   #12FB
wm_hz           equ   #12FC
wm_rp_back      equ   #12FD
wm_rp_i         equ   #12FE

; menu_dispatch: called from k_poll. If a fresh click (D bit0) landed in the
; kernel-owned top bar and the app registered a handler, deliver a GB_MSG_MENU
; message (p0 = byte column) and consume the click. The app's page is mapped
; (it called GB_POLL), so the handler is a direct CALL - no bank trampoline.
menu_dispatch
                ld    a,(UI_MODAL)            ; a paged UI dialog is up? then the focused app's
                or    a                        ; bank is swapped out for the module - do NOT call
                ret   nz                        ; its handler. The dialog's own poll sees the click
                bit   0,d                     ; fresh click this frame?
                ret   z
                ld    a,(poll_line)
                cp    8                        ; inside the 8px top bar (rows 0..7)?
                ret   nc
                ld    hl,(APP_HANDLER)        ; handler registered?
                ld    a,h
                or    l
                ret   z
                res   0,d                      ; consume the click (the app's poll
                ld    a,GB_MSG_MENU            ; loop won't also see it)
                ld    (GB_MSG),a
                ld    a,(poll_byte)
                ld    (GB_MSG+1),a            ; p0 = clicked byte column
                push  de                       ; preserve the poll flags across the
                call  md_call                  ; C handler (call (hl))
                pop   de
                ret
md_call         jp    (hl)

; k_menu (GB_MENU): register the calling app's top-bar menu. HL = a blob in the
; app page: count, then per title { col (byte), 8-byte NUL/space-padded label }.
; Copied into resident MENU_DEF; the desktop's bar handler watches MENU_DEF and
; redraws the titles when it changes (#77).
k_menu                                          ; HL = def ptr in the focused app's page.
                ; Also record it as the focused window's menu, so a later focus change
                ; re-installs it instead of clearing the bar (#142: gb_menu must persist
                ; like a static gb_win_t.menu does).
                push  hl
                ld    a,(WM_FOCUS)
                call  wm_entry                  ; HL = focused window's WM entry
                ld    de,WM_FR_MENU
                add   hl,de                     ; -> its menu-def-ptr field
                pop   de                         ; DE = the def ptr
                ld    (hl),e
                inc   hl
                ld    (hl),d
                ex    de,hl                       ; HL = def ptr (fall into the copy)
menu_install                                    ; HL = menu def (mapped page) -> MENU_DEF
                ld    a,(hl)                  ; count
                cp    5
                ret   nc                       ; > 4 titles unsupported -> ignore
                ld    e,a                       ; len = count*9 + 1
                add   a,a
                add   a,a
                add   a,a
                add   a,e
                inc   a
                ld    c,a
                ld    b,0
                ld    de,MENU_DEF
                ldir
                ret
; menu_clear: empty the top-bar menu (focus moved to a window with no menu).
menu_clear
                xor   a
                ld    (MENU_DEF),a
                ret

; (menu titles are rendered by the desktop's bar handler now, from MENU_DEF - #77)

; (File-type -> app routing lives in the File Manager now; the kernel just opens
; the app the FM names, via GB_WMLAUNCHAS. The old resident app_for_ext + its name
; tables were removed to reclaim space - the kernel->C migration that funds the
; .APP launch model, #70.)
launch_arg      equ   #1485        ; #188: relocated to low RAM (was resident defs 11)
GBNET_RES       equ   #1491        ; #238: GBNET.MOD result byte (the op/args/data block is
                                   ; #1490..#149F + #2200; only k_net reads the result here)

; k_icon (GB_ICON): blit a full icon. A = slot, B = x, C = y. Reads the bitmap
; from the .IST in PAGE_DATA, so swap pages around it.
k_icon
                ld    (gi_slot),a
                ld    a,b
                ld    (gi_x),a
                ld    a,c
                ld    (gi_y),a
                ld    a,(bank_cur)           ; only the desktop (PAGE_APP0) blits icons pen-0
                ifdef PLATFORM_MSX            ; transparent so the backdrop shows through (#182);
                ld    hl,MSX_TPASEG           ; (#287: the desktop rides the TPA segment)
                sub   (hl)
                else
                sub   PAGE_APP0
                endif                          ; every other app draws opaque icons.
                sub   1
                sbc   a,a                      ; A = #FF iff bank_cur == PAGE_APP0, else #00
                ld    (bm_keep),a
                call  to_data
                ld    a,(gi_slot)
                call  icon_full_geom
                call  blit_bitmap
                jp    from_data
icon_full_geom                                 ; A = slot -> bm_src/w/h, bm_x/y
                ld    l,a
                ld    h,0
                add   hl,hl
                add   hl,hl                     ; slot*4
                ld    de,DATA_ICONS+16
                add   hl,de
                ld    e,(hl)                  ; offset
                inc   hl
                ld    d,(hl)
                inc   hl
                ld    a,(hl)                  ; width
                ld    (bm_w),a
                inc   hl
                ld    a,(hl)                  ; height (full)
                ld    (bm_h),a
                ld    hl,DATA_ICONS
                add   hl,de
                ld    (bm_src),hl
                ld    a,(gi_x)
                ld    (bm_x),a
                ld    a,(gi_y)
                ld    (bm_y),a
                ret
gi_slot         db    0
gi_x            db    0
gi_y            db    0

; k_fill (GB_FILL): filled rectangle. B=x C=y D=w E=h A=pen. Capture the params
; before pen_to_byte (it clobbers E).
k_fill
                ld    (kf_pen),a
                ld    a,b
                ld    (fb_x),a
                ld    a,c
                ld    (fb_y),a
                ld    a,d
                ld    (fb_w),a
                ld    a,e
                ld    (fb_h),a
                ld    a,(kf_pen)
                call  pen_to_byte
                ld    (fb_val),a
                jp    fill_block
kf_pen          db    0

; k_saverect / k_restorerect (GB_SAVERECT / GB_RESTORERECT): save/restore a screen
; rectangle to/from a caller buffer. B=x C=y D=w E=h HL=buffer (w*h bytes, in the
; caller's page, mapped during the call). Thin wrappers over the cursor save-under
; primitives save_block/restore_block (lib/screen.asm), which manage the upper-ROM
; shadow when reading screen RAM. The sb_* params are shared with the cursor, so a
; caller must bracket with gb_curhide/gb_curshow (#114: PAINT canvas blit).
k_saverect
                call  rect_args
                jp    save_block
k_restorerect
                call  rect_args
                jp    restore_block
rect_args                                      ; B=x C=y D=w E=h HL=buf -> sb_*
                ld    a,b
                ld    (sb_x),a
                ld    a,c
                ld    (sb_y),a
                ld    a,d
                ld    (sb_w),a
                ld    a,e
                ld    (sb_h),a
                ld    (sb_buf),hl
                ret

; k_mxp (GB_MXP, #114): HL = the pointer's PIXEL x (0-319), for pixel-accurate
; drawing (gb_mx only gives the byte column). cursor_x is in 1/2-pixel units.
k_mxp
                ld    hl,(cursor_x)
                srl   h
                rr    l                          ; HL = cursor_x / 2 = pixel x
                ret

; k_xorframe (GB_XORFRAME): stub. The rubber-band XOR frame was never wired up
; (no app uses it, no gb_xorframe binding); body removed to reclaim resident
; space. The jump-table slot stays (addresses fixed) and -> k_noop (#148).

                ifdef PLATFORM_MSX
                include "clock_msx.asm"        ; software clock only (H.TIMI-paced, #287)
                include "clock_state.inc"

                include "../lib/msx/screen.asm" ; selected V9938 driver (+ shared screen_clip)
                include "clock_msx_rtc.asm"    ; RTC seed helper after page-aligned picture LUTs
                include "../lib/msx/text.asm"
                include "../lib/msx/cursor.asm" ; hardware-sprite pointer
                include "../lib/msx/input.asm"
                include "../lib/msx/fs.asm"     ; MSX-DOS 2 BDOS backend
                include "../lib/msx/bank.asm"   ; mapper-segment paging (PUT_P1)
                else
                ifdef PLATFORM_PCW
                include "clock_pcw.asm"        ; software clock (poll-paced, #331)
                include "clock_state.inc"

                include "../lib/pcw/screen.asm" ; roller-RAM/CGA2 driver (+ shared clip)
                include "../lib/pcw/text.asm"
                include "../lib/cursor_arrow.asm"
                include "../lib/pcw/cursor.asm" ; software pointer (save-under composite)
                include "../lib/pcw/input.asm"
                include "../lib/pcw/fdc.asm"    ; polled uPD765
                include "../lib/pcw/fs.asm"     ; CP/M 2.2 backend
                include "../lib/pcw/bank.asm"   ; slot-1 block paging
                else
                include "clock.asm"
                include "memdetect.asm"
                include "clock_state.inc"
                include "memdetect_state.inc"

                include "../lib/screen.asm"
                include "../lib/text.asm"
                include "../lib/cursor_arrow.asm"
                include "../lib/cursor.asm"
                include "../lib/input.asm"
                include "../lib/fs.asm"
                ifdef GB_ROM                  ; #152: the GEOBENCH.ROM seam, shared by every backend
                include "../lib/fs_rom_seam.asm"
                endif
                if STORAGE_ALBIREO
                include "../lib/fs_albireo.asm"
                else
                if STORAGE_M4                  ; #174: M4 raw-sector backend (FAT core + M4 driver)
                include "../lib/fs_m4.asm"
                else
                include "../lib/fs_ide_fat.asm"
                endif
                endif
                include "../lib/fs_amsdos.asm"
                include "../lib/bank.asm"
                endif                          ; (PLATFORM_PCW platform-include swap, #331)
                endif                          ; (PLATFORM_MSX platform-include swap, #287)
kern_end                                        ; GBKERN.BIN = #8000..kern_end only.

                ifdef PLATFORM_MSX
; --- MSX2 STABILITY GUARD + output (#287) -----------------------------------------
; The MSX resident kernel must stay below the page-3 glue at #C000 (the stack lives
; at #F380 down, far above). The stub (kernel/msx_stub.asm) incbins this image.
                assert kern_end<=#C000,"GBKERN(MSX) too big - past the #C000 page-3 glue"
                save  "GBKERNM.RAW",GB_KERNEL,kern_end-GB_KERNEL
                else
                ifdef PLATFORM_PCW
; --- PCW STABILITY GUARD + output (#331) ------------------------------------------
; The PCW kernel and its stack share physical block 2: SP starts at #C000 and grows
; down toward kern_end. Keep at least 512 bytes between them.
                assert kern_end<=#BE00,"GBKERN(PCW) too big - into the #C000-down stack"
                save  "GBKERNP.RAW",GB_KERNEL,kern_end-GB_KERNEL
                else
; --- STABILITY GUARD: the resident kernel must not eat the stack -----------------
; The CPC runs on the UniDOS stack, which grows DOWN from HIMEM (&A288) toward
; kern_end - the only stack space is the gap between them. If the kernel grows into
; the reserve the stack smashes the resident image on a deep call + interrupt (a
; silent crash). This assert turns that into a LOUD build failure so a too-big
; kernel is caught here, not in the field. (Reclaim, or move scratch out, to fix.)
HIMEM           equ   #A288        ; UniDOS HIMEM (stack top); see docs/AMSDOS notes
                ifndef STACK_RESERVE
STACK_RESERVE   equ   256          ; min bytes kept free below HIMEM for the stack (#95)
                endif
                if SPIKE|SPIKE_STAGED|SPIKE_M4 ; #130: the throwaway spike hangs early (tiny stack)
                else
                assert HIMEM-kern_end>=STACK_RESERVE,"GBKERN too big - reclaim resident bytes (see #104)"
                endif

; --- scratch buffers live in LOW RAM (always the main bank, below the stack) -----
; They used to sit just above kern_end, but as the kernel grew they landed in the
; UniDOS stack's path: HIMEM &A288 grows DOWN toward kern_end, so a 512-byte sector
; write during a deep call (a Notepad save) smashed the return stack and the IDE
; loop ran away. Low RAM #1800+ (the retired GBFAT transfer area) is clear of both
; the descending stack and the resident image. Fixed addresses, not loaded data.
                endif                          ; (PLATFORM_PCW output tail, #331)
                endif                          ; (PLATFORM_MSX: no CPC stack guard/packaging)
; (shared on both platforms: fsam_buf doubles as the backdrop-tile load scratch in
; kernel/assets.asm, and the drag ghost saves under fs_secbuf)
fs_secbuf       equ   #1800            ; IDE sector buffer / aliased AMSDOS write sector
fsam_buf        equ   #1A00            ; floppy whole-directory buffer
                ifndef PLATFORM_MSX
                ifndef PLATFORM_PCW             ; (#331: PCW files are staged by mkpcwdsk)
                                                ; The packaging incbins below are
                                                ; never loaded at runtime, only read
                                                ; by `save`. The payload outgrew a
                                                ; single free region, so it is split:
                                                ; the modules/fonts/assets sit ABOVE
                                                ; the kernel, the (larger) app binaries
                                                ; in low memory at #0100 - both within
                                                ; the 64K image and clear of #8000..
                                                ; kern_end (GBKERN.BIN).
cfg_img         incbin "../build/GBCFG.RAW"     ; config-parser C module, as GBCFG.BIN
cfg_imgend
fat_img         incbin "../build/GBFAT.RAW"     ; FAT16/IDE write module, as GBFAT.BIN
fat_imgend
net_img         incbin "../build/GBNET.RAW"     ; W5100 networking module, as GBNET.MOD (#238)
net_imgend
font_img        incbin "../build/DEFAULT.FNT"   ; packaged on the disk as DEFAULT.FNT
font_imgend
cfont_img       incbin "../build/CLASSIC.FNT"   ; alternate 8x8 font (FONT=CLASSIC)
cfont_imgend
icon_img        incbin "../build/DEFAULT.IST"   ; packaged on the disk as DEFAULT.IST
icon_imgend
                ; STABILITY GUARD: the icon set loads into PAGE_DATA at DATA_ICONS and
                ; must stay below DATA_MODTOP (the write module shares PAGE_DATA) - else a
                ; save/delete/copy overwrites the end of the set (garbled icons, #88).
                assert icon_imgend-icon_img<=DATA_MODTOP-DATA_ICONS-#200,"DEFAULT.IST too big: would collide with the write module in PAGE_DATA - fewer icons or raise DATA_MODTOP"
splash_img      incbin "../build/SPLASH.BIN"    ; #196: bootsplash lollipop (raw Mode-1, 24x144)
splash_imgend
                include "../lib/cursor_data.asm" ; cur_spr_data..cur_spr_end -> DEFAULT.SPR
                include "../lib/cursor_hand_data.asm" ; cur_hand_data..end -> HAND.SPR
                ; Overflow apps are packaged by FURTHER rasm passes: this 64K image
                ; filled up, and the .dsk save accumulates across rasm invocations, so
                ; the largest binaries get their own passes - PAINT/XAOS/ICONED in
                ; pack_apps.asm (#114), and the gb_doc-grown VIEWER + FILEMGR in
                ; pack_apps2.asm (#142). Both are too big for this image's free regions.
                org   #0100                     ; --- app binaries, low region ---
dtp_img         incbin "../build/DESKTOP.RAW"   ; packaged on the disk as DESKTOP.APP
dtp_imgend
npd_img         incbin "../build/NOTEPAD.RAW"   ; packaged on the disk as NOTEPAD.APP
npd_imgend
                save  "GBKERN.BIN",GB_KERNEL,kern_end-GB_KERNEL,DSK,"build/gbkern.dsk"
                save  "DESKTOP.APP",dtp_img,dtp_imgend-dtp_img,DSK,"build/gbkern.dsk"
                save  "NOTEPAD.APP",npd_img,npd_imgend-npd_img,DSK,"build/gbkern.dsk"
                save  "GBCFG.MOD",cfg_img,cfg_imgend-cfg_img,DSK,"build/gbkern.dsk"
                save  "GBFAT.MOD",fat_img,fat_imgend-fat_img,DSK,"build/gbkern.dsk"
                save  "GBNET.MOD",net_img,net_imgend-net_img,DSK,"build/gbkern.dsk"
                save  "DEFAULT.FNT",font_img,font_imgend-font_img,DSK,"build/gbkern.dsk"
                save  "CLASSIC.FNT",cfont_img,cfont_imgend-cfont_img,DSK,"build/gbkern.dsk"
                save  "DEFAULT.IST",icon_img,icon_imgend-icon_img,DSK,"build/gbkern.dsk"
                save  "SPLASH.MOD",splash_img,splash_imgend-splash_img,DSK,"build/gbkern.dsk"
                save  "build/DEFAULT.SPR",cur_spr_data,cur_spr_end-cur_spr_data
                ; Keep this composite cursor/helper asset headerless on the floppy.
                ; An AMSDOS header would make its 512-byte payload occupy 640 bytes,
                ; exceeding cursor_init's exact 512-byte load window at #1500.
                edsk  putfile,"build/gbkern.dsk","build/DEFAULT.SPR","DEFAULT.SPR"
                ; HAND.SPR is optional and ships on the Companion floppy; keeping it off
                ; Main leaves room for the header-aware ICONED picker (#426).
                save  "build/HAND.SPR",cur_hand_data,cur_hand_end-cur_hand_data
                save  "build/GBKERN.RAW",GB_KERNEL,kern_end-GB_KERNEL
                endif                          ; (ifndef PLATFORM_PCW: CPC packaging tail)
                endif                          ; (ifndef PLATFORM_MSX: CPC packaging tail)
