; ---------------------------------------------------------------------------
; lib/msx/screen6.asm - GEOBENCH V9938 Screen 6 driver for the MSX2 target (#287).
;
; The MSX counterpart of lib/screen.asm: same entry points, same low-RAM
; parameter cells (bm_*, fb_*, sb_*, clip_*), so every caller in the shared
; kernel body compiles unchanged. The difference is the surface: Screen 6
; (512x212, 4 colours, 2bpp) lives in VRAM behind the V9938 ports, not in a
; CPU-addressable framebuffer.
;
;   - layout is LINEAR: vram = y*128 + xbyte (no CPC CRTC interleave)
;   - in-byte encoding is 2-bit fields, leftmost pixel in bits 7-6:
;       a byte of one pen p is p*#55: p0->#00 p1->#55 p2->#AA p3->#FF
;   - rectangle fills use the HMMV blitter command
;   - row copies stream through port #98 with autoincrement (OTIR/INIR pacing
;     is safe in bitmap modes); read-modify-write rows bounce via MSX_ROWBUF
;   - every primitive waits for the command engine (CE) before touching VRAM,
;     since a previous HMMV may still be running
;   - multi-byte VDP address/register sequences run under DI: the BIOS ISR
;     reads status port #99, which would reset the two-byte address latch
;
; Coordinates keep the CPC conventions: x in BYTE columns (0..127 here),
; y in lines (0..211). Clipping/culling is the shared lib/screen_clip.asm.
; ---------------------------------------------------------------------------

; --- VDP access helpers ----------------------------------------------------

; vdp_setwr: D = xbyte, E = y -> set the VRAM write address to y*128 + xbyte.
; Leaves autoincrement writing via port #98. Clobbers A. DI/EI inside.
vdp_setwr
                di
                ld    a,e                     ; R#14 = A16..A14 = y>>7 (bit14 of y*128)
                rlca
                and   1
                out   (VDP_CTRL),a
                ld    a,14|#80
                out   (VDP_CTRL),a
                ld    a,e                     ; low byte = (y&1)<<7 | xbyte
                rrca                          ; CF = y bit0... use bit0 via rrca into b7
                ld    a,0
                rra                           ; A = (y&1) ? #80 : 0  (rrca left CF = y&1)
                or    d
                out   (VDP_CTRL),a
                ld    a,e                     ; high byte = (y>>1) & #3F, +#40 for write
                srl   a
                and   #3F
                or    #40
                out   (VDP_CTRL),a
                ei
                ret

; vdp_setrd: D = xbyte, E = y -> set the VRAM read address (reads via port #98).
vdp_setrd
                di
                ld    a,e
                rlca
                and   1
                out   (VDP_CTRL),a
                ld    a,14|#80
                out   (VDP_CTRL),a
                ld    a,e
                rrca
                ld    a,0
                rra
                or    d
                out   (VDP_CTRL),a
                ld    a,e
                srl   a
                and   #3F
                out   (VDP_CTRL),a
                ei
                ret

; vdp_reg: A = value, C = register number. Clobbers A.
vdp_reg
                di
                out   (VDP_CTRL),a
                ld    a,c
                or    #80
                out   (VDP_CTRL),a
                ei
                ret

; vdp_wait_ce: block until the command engine is idle (S#2 bit0 clear).
; Restores R#15 = 0 (the BIOS ISR expects S#0). Clobbers A.
vdp_wait_ce
                di
                ld    a,2
                out   (VDP_CTRL),a
                ld    a,15|#80
                out   (VDP_CTRL),a
vwc_poll        in    a,(VDP_CTRL)
                rrca
                jr    c,vwc_poll
                xor   a
                out   (VDP_CTRL),a
                ld    a,15|#80
                out   (VDP_CTRL),a
                ei
                ret

; vdp_hmmv: fill the rectangle DX=(vc_dx) DY=(vc_dy) NX=(vc_nx) NY=(vc_ny)
; with byte (vc_clr) via the HMMV command. Waits CE first. Pixel units.
vdp_hmmv
                call  vdp_wait_ce
                ld    a,36                    ; R#17 = 36, autoincrement -> R#36..R#46
                ld    c,17
                call  vdp_reg
                di
                ld    hl,vc_dx
                ld    bc,#0B00|VDP_IND        ; B = 11 bytes, C = indirect data port
vhm_out         outi
                jr    nz,vhm_out
                ei
                ret

; --- pen_to_byte: A = pen (0..3) -> A = solid Screen 6 byte (pen * #55) -----
pen_to_byte
                and   3
                ld    e,a
                add   a,a
                add   a,a
                or    e                       ; pen * 5 (bits in both low fields)
                ld    e,a
                add   a,a
                add   a,a
                add   a,a
                add   a,a
                or    e                       ; * #11 -> all four 2-bit fields
                ret

; --- blit_bitmap: bm_w x bm_h bytes at (bm_x, bm_y) from (bm_src) -----------
; Opaque (bm_keep=0): stream each row straight to VRAM. Transparent
; (bm_keep=#FF, desktop icons): read the row back, composite in MSX_ROWBUF
; with the pen-0 mask, write it out.
blit_bitmap
                ld    a,(bm_x)               ; cull against the clip rect (shared code)
                ld    b,a
                ld    a,(bm_y)
                ld    c,a
                ld    a,(bm_w)
                ld    d,a
                ld    a,(bm_h)
                ld    e,a
                call  rect_cull
                ret   c
                call  vdp_wait_ce
                ld    a,(bm_h)
                ld    (bl_rows),a
                ld    a,(bm_y)
                ld    (bl_y),a
bl_loop
                ld    a,(bm_keep)
                or    a
                jr    nz,bl_trans
                ; --- opaque row: set write addr, OTIR the source row --------
                ld    a,(bm_x)
                ld    d,a
                ld    a,(bl_y)
                ld    e,a
                call  vdp_setwr
                ld    hl,(bm_src)
                ld    a,(bm_w)
                ld    b,a
                ld    c,VDP_DATA
bl_orow         outi
                jr    nz,bl_orow
                ld    (bm_src),hl            ; advance past this row
                jr    bl_next
bl_trans
                ; --- transparent row: read back, composite, write -----------
                ld    a,(bm_x)
                ld    d,a
                ld    a,(bl_y)
                ld    e,a
                call  vdp_setrd
                ld    hl,MSX_ROWBUF
                ld    a,(bm_w)
                ld    b,a
                ld    c,VDP_DATA
bl_trd          ini
                jr    nz,bl_trd
                ld    de,MSX_ROWBUF          ; DE = screen row copy
                ld    hl,(bm_src)            ; HL = icon row
                ld    a,(bm_w)
                ld    (bl_cnt),a
bl_tbyte
                ld    a,(hl)                  ; mask: both bits of every non-pen-0 field
                ld    c,a
                srl   a
                or    c
                and   #55
                ld    c,a
                add   a,a
                or    c                        ; A = opaque mask
                cpl                            ; ~mask = keep-background bits
                ld    c,a
                ld    a,(de)                  ; screen byte
                and   c                        ; keep background where transparent
                or    (hl)                     ; add the icon's opaque pixels
                ld    (de),a
                inc   hl
                inc   de
                ld    a,(bl_cnt)
                dec   a
                ld    (bl_cnt),a
                jr    nz,bl_tbyte
                ld    (bm_src),hl            ; advance the icon row
                ld    a,(bm_x)               ; write the composited row back
                ld    d,a
                ld    a,(bl_y)
                ld    e,a
                call  vdp_setwr
                ld    hl,MSX_ROWBUF
                ld    a,(bm_w)
                ld    b,a
                ld    c,VDP_DATA
bl_twr          outi
                jr    nz,bl_twr
bl_next
                ld    a,(bl_y)
                inc   a
                ld    (bl_y),a
                ld    a,(bl_rows)
                dec   a
                ld    (bl_rows),a
                jp    nz,bl_loop
                ret

; --- fill_block: fb_w x fb_h bytes at (fb_x, fb_y) with fb_val --------------
; One HMMV after clipping - the V9938 does the whole rectangle.
fill_block
                call  clip_fb_copy           ; fbw_* = fb_* clipped (shared code)
                ret   c
                ld    a,(fbw_x)              ; DX = xbyte * 4 (pixels)
                ld    l,a
                ld    h,0
                add   hl,hl
                add   hl,hl
                ld    (vc_dx),hl
                ld    a,(fbw_y)
                ld    l,a
                ld    h,0
                ld    (vc_dy),hl
                ld    a,(fbw_w)              ; NX = wbytes * 4
                ld    l,a
                ld    h,0
                add   hl,hl
                add   hl,hl
                ld    (vc_nx),hl
                ld    a,(fbw_h)
                ld    l,a
                ld    h,0
                ld    (vc_ny),hl
                ld    a,(fb_val)
                ld    (vc_clr),a
                xor   a
                ld    (vc_arg),a
                ld    a,#C0                  ; HMMV
                ld    (vc_cmd),a
                jp    vdp_hmmv

                if BACKDROP_TILE
; --- fill_pattern: select the backdrop tile at absolute screen phase --------
fill_pattern
                ld    hl,BD_TILE
                ld    (fp_base+1),hl
                xor   a
                ld    (fp_xphase+1),a
                ld    (fp_yphase+1),a
                jr    fp_begin
                endif

                if TITLEBAR_TILE
; --- fill_title_pattern: select the title tile at window-relative phase -----
fill_title_pattern
                ld    hl,DATA_TITLE
                ld    (fp_base+1),hl
                ld    a,(kw_x)
                neg
                ld    (fp_xphase+1),a
                ld    a,(kw_y)
                neg
                ld    (fp_yphase+1),a
                endif

                if BACKDROP_TILE | TITLEBAR_TILE
; Build each native row in MSX_ROWBUF, then stream it to VRAM.
fp_begin
                call  clip_fb_copy
                ret   c
                call  vdp_wait_ce
                ld    a,(fbw_h)
                ld    (fb_rows),a
                ld    a,(fbw_y)
                ld    (fb_cy),a
fp_row
                ld    a,(fb_cy)              ; DE = tile row base + column phase
fp_yphase       add   a,0
                and   15
                add   a,a
                add   a,a
                ld    c,a
                ld    a,(fbw_x)
fp_xphase       add   a,0
                and   3
                add   a,c
                ld    e,a
                ld    d,0
fp_base         ld    hl,BD_TILE
                add   hl,de
                ex    de,hl                   ; DE = tile ptr (phase-corrected)
                ld    hl,MSX_ROWBUF          ; build the row
                ld    a,(fbw_w)
                ld    b,a
fp_col
                ld    a,(de)
                ld    (hl),a
                inc   hl
                inc   e                       ; advance tile ptr; wrap every 4 bytes
                ld    a,e
                and   3
                jr    nz,fp_nw
                dec   e
                dec   e
                dec   e
                dec   e
fp_nw
                djnz  fp_col
                ld    a,(fbw_x)              ; stream the row to VRAM
                ld    d,a
                ld    a,(fb_cy)
                ld    e,a
                call  vdp_setwr
                ld    hl,MSX_ROWBUF
                ld    a,(fbw_w)
                ld    b,a
                ld    c,VDP_DATA
fp_out          outi
                jr    nz,fp_out
                ld    a,(fb_cy)
                inc   a
                ld    (fb_cy),a
                ld    a,(fb_rows)
                dec   a
                ld    (fb_rows),a
                jr    nz,fp_row
                ret
                endif

                include "../screen_clip.asm"  ; clip_fb_copy + rect_cull (shared, #287)

; --- save_block / restore_block ----------------------------------------------
; Same caller-RAM buffer contract as the CPC (w*h bytes, byte geometry is
; identical at 4px/byte). No ROM juggling: VRAM reads go through the port.
save_block
                call  vdp_wait_ce
                ld    a,(sb_h)
                ld    (sb_rows),a
                ld    a,(sb_y)
                ld    (sb_cy),a
                ld    hl,(sb_buf)
                ld    (sb_ptr),hl
sv_loop
                ld    a,(sb_x)
                ld    d,a
                ld    a,(sb_cy)
                ld    e,a
                call  vdp_setrd
                ld    hl,(sb_ptr)            ; HL = buffer (dest)
                ld    a,(sb_w)
                ld    b,a
                ld    c,VDP_DATA
sv_row          ini
                jr    nz,sv_row
                ld    (sb_ptr),hl
                ld    a,(sb_cy)
                inc   a
                ld    (sb_cy),a
                ld    a,(sb_rows)
                dec   a
                ld    (sb_rows),a
                jr    nz,sv_loop
                ret

restore_block
                call  vdp_wait_ce
                ld    a,(sb_h)
                ld    (sb_rows),a
                ld    a,(sb_y)
                ld    (sb_cy),a
                ld    hl,(sb_buf)
                ld    (sb_ptr),hl
rs_loop2
                ld    a,(sb_x)
                ld    d,a
                ld    a,(sb_cy)
                ld    e,a
                call  vdp_setwr
                ld    hl,(sb_ptr)            ; HL = buffer (source)
                ld    a,(sb_w)
                ld    b,a
                ld    c,VDP_DATA
rs_row          outi
                jr    nz,rs_row
                ld    (sb_ptr),hl
                ld    a,(sb_cy)
                inc   a
                ld    (sb_cy),a
                ld    a,(sb_rows)
                dec   a
                ld    (sb_rows),a
                jr    nz,rs_loop2
                ret

; restore_pic_block: the .PIC payload stays in canonical CPC Mode-1 packing on
; every platform. Translate each source byte to raw Screen-6 packing while it is
; sent to VRAM; the banked source remains unchanged for Paint/save portability.
restore_pic_block
                call  vdp_wait_ce
                ld    a,(sb_h)
                ld    (sb_rows),a
                ld    a,(sb_y)
                ld    (sb_cy),a
                ld    hl,(sb_buf)
                ld    (sb_ptr),hl
rpb_loop
                ld    a,(sb_x)
                ld    d,a
                ld    a,(sb_cy)
                ld    e,a
                call  vdp_setwr
                ld    hl,(sb_ptr)
                ld    a,(sb_w)
                ld    b,a
                ld    c,VDP_DATA
rpb_row
                ld    a,(hl)
                inc   hl
                ld    (rpb_lut+1),a            ; patch the aligned table's low address byte
rpb_lut         ld    a,(pic_m1_to_native)
                out   (c),a
                djnz  rpb_row
                ld    (sb_ptr),hl
                ld    a,(sb_cy)
                inc   a
                ld    (sb_cy),a
                ld    a,(sb_rows)
                dec   a
                ld    (sb_rows),a
                jr    nz,rpb_loop
                ret

; --- k_line (GB_LINE #8009, #287): draw a line via the V9938 LINE command ----
; Endpoints + pen come from the GLINE_* glue cells (screen pixels, top-left
; origin) - the clock's inline asm fills them. The command block: DX/DY = the
; start point, NX = the long (major) axis distance, NY = the short one, ARG
; bit2 = DIX (1 = leftward), bit3 = DIY (1 = upward), bit0 = MAJ (1 = Y major).
k_line
                ld    hl,(GLINE_X0)
                ld    (vc_dx),hl
                ld    hl,(GLINE_Y0)
                ld    (vc_dy),hl
                xor   a
                ld    (vc_arg),a
                ; DE = |dx|, sets DIX if x1 < x0
                ld    hl,(GLINE_X1)
                ld    de,(GLINE_X0)
                or    a
                sbc   hl,de
                jr    nc,kl_dxok
                ld    a,(vc_arg)
                or    #04                     ; DIX = leftward
                ld    (vc_arg),a
                ex    de,hl                   ; negate HL
                ld    hl,0
                or    a
                sbc   hl,de
kl_dxok
                ex    de,hl                   ; DE = |dx|
                ; HL = |dy|, sets DIY if y1 < y0
                ld    hl,(GLINE_Y1)
                push  de
                ld    de,(GLINE_Y0)
                or    a
                sbc   hl,de
                pop   de
                jr    nc,kl_dyok
                push  de
                ex    de,hl
                ld    hl,0
                or    a
                sbc   hl,de
                pop   de
                ld    a,(vc_arg)
                or    #08                     ; DIY = upward
                ld    (vc_arg),a
kl_dyok
                ; major axis: NX = max(|dx|,|dy|), NY = min; MAJ set if Y major
                push  hl                      ; save |dy|
                or    a
                sbc   hl,de                   ; |dy| - |dx|
                pop   hl
                jr    c,kl_xmajor             ; |dy| < |dx| -> X major
                ld    a,(vc_arg)
                or    #01                     ; MAJ = Y is the major axis
                ld    (vc_arg),a
                ld    (vc_nx),hl              ; NX = |dy| (long)
                ex    de,hl
                ld    (vc_ny),hl              ; NY = |dx| (short)
                jr    kl_go
kl_xmajor
                ex    de,hl
                ld    (vc_nx),hl              ; NX = |dx| (long)
                ex    de,hl
                ld    (vc_ny),hl              ; NY = |dy| (short)
kl_go
                ld    a,(GLINE_PEN)
                and   3
                ld    (vc_clr),a              ; LINE colour = the 2-bit pen value
                ld    a,#70                   ; LINE command, IMP logical op
                ld    (vc_cmd),a
                jp    vdp_hmmv                ; same R#17-indirect block streamer

; --- k_cls (GB_CLS): clear the whole bitmap to pen 0 -------------------------
k_cls
                ld    hl,0
                ld    (vc_dx),hl
                ld    (vc_dy),hl
                ld    hl,512
                ld    (vc_nx),hl
                ld    hl,212
                ld    (vc_ny),hl
                xor   a
                ld    (vc_clr),a
                ld    (vc_arg),a
                ld    a,#C0
                ld    (vc_cmd),a
                jp    vdp_hmmv

; --- set_palette: KCFG_INKS (CPC firmware ink numbers) -> V9938 palette ------
; KCFG_INKS stays in the CPC colour space on both platforms (the canonical
; config values); each ink maps through cpc2grb to a 9-bit GRB pair. Also seeds
; the cursor-sprite entries (12 = black outline, 13 = white fill) and the
; border (R#7, both 2-bit fields = the pen whose ink matches the border ink,
; else pen 0).
set_palette
                ld    b,4                     ; pens 0..3
                ld    c,0
sp_pen
                push  bc
                ld    a,c                     ; palette index = pen
                ld    c,16                    ; R#16 = palette pointer
                call  vdp_reg
                pop   bc
                push  bc
                ld    hl,KCFG_INKS
                ld    e,c
                ld    d,0
                add   hl,de
                ld    a,(hl)                  ; A = CPC ink number for this pen
                call  ink_to_grb              ; DE = GRB pair
                di
                ld    a,e
                out   (VDP_PAL),a
                ld    a,d
                out   (VDP_PAL),a
                ei
                pop   bc
                inc   c
                djnz  sp_pen
                ld    a,12                    ; cursor entries: 12 black, 13 white
                ld    c,16
                call  vdp_reg
                di
                xor   a
                out   (VDP_PAL),a            ; 12: R=0,B=0
                out   (VDP_PAL),a            ;     G=0
                ld    a,#77
                out   (VDP_PAL),a            ; 13: R=7,B=7
                ld    a,#07
                out   (VDP_PAL),a            ;     G=7
                ei
                ; Border: in Graphic 5 (Screen 6) the ACTIVE pen-0 pixels AND the
                ; overscan border both take the colour that R#7 points at - they
                ; are NOT palette[0] directly. So R#7 must select palette entry 0,
                ; making pen-0 fills and the border show Paper (palette[0]). Any
                ; other R#7 turns the whole pen-0 backdrop that colour (the "red
                ; desktop" bug, #287). The INKS= border value can't be shown
                ; independently on this VDP mode, so it's folded into Paper.
                xor   a                        ; R#7 = 0 -> backdrop/border = palette[0]
                ld    c,7
                jp    vdp_reg

; k_setink (GB_SETINK #8006, #287): A = pen (0-3, or 4 = border), B = CPC ink.
; Live colour change for Settings' preview. Pens 0-3 write their palette entry;
; pen 4 (border) is a no-op here - the Screen 6 border tracks Paper (see above).
k_setink
                cp    4
                ret   nc                       ; border: not independently settable in G5
                push  bc                      ; A = pen = palette index
                ld    c,16
                call  vdp_reg
                pop   bc
                ld    a,b                     ; B = ink number
                call  ink_to_grb
                di
                ld    a,e
                out   (VDP_PAL),a
                ld    a,d
                out   (VDP_PAL),a
                ei
                ret

; ink_to_grb: A = CPC firmware ink number (0..26) -> DE: E = (R<<4)|B, D = G.
ink_to_grb
                cp    27
                jr    c,itg_ok
                xor   a                       ; out of range -> black
itg_ok
                add   a,a
                ld    l,a
                ld    h,0
                ld    de,cpc2grb
                add   hl,de
                ld    e,(hl)
                inc   hl
                ld    d,(hl)
                ret

; CPC firmware colours 0..26 as V9938 GRB pairs (byte0 = R<<4|B, byte1 = G).
; CPC channel levels 0/50%/100% -> 0/4/7.
cpc2grb
                db    #00,#00                 ;  0 black
                db    #04,#00                 ;  1 blue
                db    #07,#00                 ;  2 bright blue
                db    #40,#00                 ;  3 red
                db    #44,#00                 ;  4 magenta
                db    #47,#00                 ;  5 mauve
                db    #70,#00                 ;  6 bright red
                db    #74,#00                 ;  7 purple
                db    #77,#00                 ;  8 bright magenta
                db    #00,#04                 ;  9 green
                db    #04,#04                 ; 10 cyan
                db    #07,#04                 ; 11 sky blue
                db    #40,#04                 ; 12 yellow
                db    #44,#04                 ; 13 white
                db    #47,#04                 ; 14 pastel blue
                db    #70,#04                 ; 15 orange
                db    #74,#04                 ; 16 pink
                db    #77,#04                 ; 17 pastel magenta
                db    #00,#07                 ; 18 bright green
                db    #04,#07                 ; 19 sea green
                db    #07,#07                 ; 20 bright cyan
                db    #40,#07                 ; 21 lime
                db    #44,#07                 ; 22 pastel green
                db    #47,#07                 ; 23 pastel cyan
                db    #70,#07                 ; 24 bright yellow
                db    #74,#07                 ; 25 pastel yellow
                db    #77,#07                 ; 26 bright white

; --- msx_video_init: post-CHGMOD screen setup (called once from boot) --------
; 212-line mode (R#9 LN), 16x16 sprites (R#1 SI), park the pointer sprites and
; terminate the sprite scan. The stub already ran CHGMOD 6 via the BIOS.
; MSX2 VDP register mirrors: R#0-R#7 at RGnSAV = #F3DF + n, but R#8-R#23 at
; #FFE7 + (n-8) - two separate tables. (Using #F3E8 for R#9 set its DC bit,
; which blacks the screen + halts the CPU on real hardware, #287.)
msx_video_init
                ld    a,(#FFE8)               ; RG9SAV: current R#9 mirror
                or    #80                     ; LN = 212 lines
                ld    (#FFE8),a
                ld    c,9
                call  vdp_reg
                ld    a,(#F3E0)               ; RG1SAV: current R#1 mirror
                or    #02                     ; SI = 16x16 sprites
                ld    (#F3E0),a
                ld    c,1
                call  vdp_reg
                jp    cursor_hide_both       ; no cursor during the boot splash

; --- HMMV register block (streamed to R#36..R#46) ----------------------------
vc_dx           dw    0                       ; R36-37 DX (pixels)
vc_dy           dw    0                       ; R38-39 DY
vc_nx           dw    0                       ; R40-41 NX (pixels)
vc_ny           dw    0                       ; R42-43 NY
vc_clr          db    0                       ; R44 colour byte
vc_arg          db    0                       ; R45 direction
vc_cmd          db    #C0                     ; R46 command

; --- parameter / scratch cells: same low-RAM homes as the CPC driver ---------
bm_src          equ   #14A4        ; source bitmap pointer (advanced by blit)
bm_x            equ   #14A6        ; destination x in bytes (0..127)
bm_y            equ   #14A7        ; destination y (0..211)
bm_w            equ   #14A8        ; width in bytes
bm_h            equ   #14A9        ; height in rows
bl_rows         equ   #14AA
bl_y            equ   #14AB
bl_cnt          equ   #14AC
bm_keep         equ   #14AD        ; #FF = pen-0 transparent (desktop), #00 = opaque

fb_x            equ   #14B8
fb_y            equ   #14B9
fb_w            equ   #14BA
fb_h            equ   #14BB
fb_val          db    0            ; Screen 6 fill byte (pen * #55)
fb_rows         equ   #14AE
fb_cy           equ   #14AF

clip_x          equ   #1338        ; clip rect (shared contract addresses)
clip_y          equ   #1339
clip_w          equ   #133A
clip_h          equ   #133B
fbw_x           equ   #14B0
fbw_y           equ   #14B1
fbw_w           equ   #14B2
fbw_h           equ   #14B3

sb_x            db    0
sb_y            db    0
sb_w            db    0
sb_h            db    0
sb_buf          dw    0

                include "pic_lut.inc"
sb_rows         equ   #14B4
sb_cy           equ   #14B5
sb_ptr          equ   #14B6
