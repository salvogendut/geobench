; ---------------------------------------------------------------------------
; lib/msx/cursor.asm - V9938 hardware-sprite pointer for the MSX2 target (#287).
;
; Same interface as lib/cursor.asm (cursor_x/cursor_y in the CPC's 2-units-per-
; pixel virtual space, cursor_show / cursor_erase / cursor_move_to /
; cursor_draw, cur_supp / cur_paintlock flags) - but the arrow is two 16x16
; hardware sprites (plane 0 = white outline; plane 1 = red fill), so there is
; NO save-under, no composite, and no way for a stale
; pointer to be baked into a window's save-under (#126-class bugs impossible).
; "Erase" terminates the sprite list at its first entry.
;
; The sprite pattern comes from the loaded .SPR asset (CUR_LOW): the MSX .SPR
; layout is +0 hotspot_x, +1 hotspot_y, +2..33 outline plane, +34..65 fill
; plane (png2spr.py --platform msx2). cursor_apply uploads it to VRAM; the
; shared cursor_init calls it after every (re)load.
; ---------------------------------------------------------------------------

; Set the VDP's sprite-table write address. Keep interrupts disabled until
; the caller finishes its data burst so the address latch cannot be disturbed.
cursor_begin_write
                di
                ld    a,h
                rlca
                rlca
                and   3
                out   (VDP_CTRL),a
                ld    a,14|#80
                out   (VDP_CTRL),a
                ld    a,l
                out   (VDP_CTRL),a
                ld    a,h
                and   #3F
                or    #40
                out   (VDP_CTRL),a
                ret

; cursor_apply: upload the .SPR at CUR_LOW to the sprite pattern + colour
; tables. Pattern 0 = outline (sprite 0), pattern 4 = fill (sprite 1).
cursor_apply
                call  vdp_wait_ce
                ld    hl,SPR_PATTERN         ; both 32-byte planes, back to back
                call  cursor_begin_write
                ld    hl,CUR_LOW+2
                ld    b,64
                ld    c,VDP_DATA
                call  cursor_write_bytes
                ; Screen 7 uses direct palette indices. Screen 6 repeats each
                ; 2-bit pen in both halves of the colour nibble.
                ld    hl,SPR_COLOUR          ; sprite 0 lines = pen 1 (white border)
                call  cursor_begin_write
                ld    b,16
                ifdef MSX_SCREEN7
                ld    a,1
                else
                ld    a,%0101
                endif
ca_c0           out   (VDP_DATA),a
                djnz  ca_c0
                ld    b,16                    ; sprite 1 lines = pen 3 (red fill)
                ifdef MSX_SCREEN7
                ld    a,3
                else
                ld    a,%1111
                endif
ca_c1           out   (VDP_DATA),a
                djnz  ca_c1
                ei
                ret

; cursor_show: place the sprites at the current position (honours the locks).
cursor_show
                ld    a,(cur_paintlock)
                or    a
                ret   nz
                ld    a,(cur_shown)
                or    a
                ret   nz
                jp    cursor_draw

; cursor_move_to: DE = x, HL = y (virtual units). No-op if unchanged.
cursor_move_to
                ld    a,(cursor_x)
                cp    e
                jr    nz,cm_go
                ld    a,(cursor_x+1)
                cp    d
                jr    nz,cm_go
                ld    a,(cursor_y)
                cp    l
                jr    nz,cm_go
                ld    a,(cursor_y+1)
                cp    h
                ret   z
cm_go
                ld    (cursor_x),de
                ld    (cursor_y),hl
                ld    a,(cur_supp)           ; suppressed (DnD ghost): track, don't draw
                or    a
                ret   nz
                ld    a,(cur_paintlock)
                or    a
                ret   nz
                jp    cursor_draw

; cursor_erase: Y=216 terminates a mode-2 sprite list (also at boot).
cursor_erase
                ld    a,(cur_paintlock)
                or    a
                ret   nz
cursor_hide_both
                xor   a
                ld    (cur_shown),a
                ld    hl,SPR_ATTR
                call  cursor_begin_write
                ld    a,216
                out   (VDP_DATA),a
                ei
                ret

; cursor_draw: position both sprites at (cursor_x, cursor_y) minus the hotspot.
; line = 211 - cursor_y/2 (the shared Y-flipped virtual space); sprite x is in
; 2-screen-pixel units in the 512-dot mode, so attr x = pixel/2 = cursor_x/4.
cursor_draw
                ld    hl,(cursor_y)
                srl   h
                rr    l
                ld    a,211
                sub   l                       ; A = line (0..211)
                ld    hl,CUR_LOW+1
                sub   (hl)                    ; - hotspot_y
                jr    nc,cd_yok
                xor   a
cd_yok
                dec   a                       ; sprite displays at y+1
                ld    (cursor_sat+4),a
                ld    hl,(cursor_x)
                srl   h
                rr    l
                srl   h
                rr    l                       ; HL = cursor_x/4 = sprite x (0..255)
                ld    a,l
                ld    hl,CUR_LOW
                sub   (hl)                    ; - hotspot_x (sprite-pixel units)
                jr    nc,cd_xok
                xor   a
cd_xok
                ld    (cursor_sat+1),a
                ld    (cursor_sat+5),a
                ; Keep sprite 0's terminator in place while updating both
                ; records. Reveal them together by writing its real Y last.
                push  bc
                ld    hl,SPR_ATTR
                call  cursor_begin_write
                ld    hl,cursor_sat
                ld    bc,9*256+VDP_DATA
                call  cursor_write_bytes
                pop   bc
                ld    hl,SPR_ATTR
                call  cursor_begin_write
                ld    a,(cursor_sat+4)
                out   (VDP_DATA),a
                ei
                ld    a,1
                ld    (cur_shown),a
                ret

; OUTI + taken JR spaces data writes by 28 Z80 T-states. Consecutive
; OUTs can replace a pending V9938 write and shift the fill's coordinates.
cursor_write_bytes
                outi
                jr    nz,cursor_write_bytes
                ei
                ret

; --- State (same names/semantics as the CPC pointer) --------------------------
cursor_x        dw    512                    ; virtual units (2 per screen pixel)
cursor_y        dw    212
cur_supp        db    0            ; 1 = suppress drawing (DnD drag shows a ghost)
cur_shown       db    0
cur_paintlock   db    0            ; 1 = inside wm_repaint_all
cursor_sat      db    216,0,0,0, 216,0,4,0, 216  ; outline, fill, end marker
