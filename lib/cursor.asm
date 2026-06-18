; ---------------------------------------------------------------------------
; lib/cursor.asm - masked, pre-shifted bitmap mouse cursor with save-under.
;
; Drop-in replacement for the old cross cursor: same interface (cursor_x/
; cursor_y in graphics coords; cursor_show / cursor_move_to / cursor_draw /
; cursor_erase). Uses lib/screen.asm (scr_addr, save_block, restore_block) and a
; sprite from png2spr (cursor_arrow_*).
;
; Draw: save the screen block under the sprite, then composite
;       screen = (saved_background AND mask) OR data
; using the just-saved RAM buffer as the background (so no extra screen reads).
; Erase: restore the saved block.
;
; Requires cursor_arrow.asm + lib/screen.asm assembled in before this.
; ---------------------------------------------------------------------------

CUR_W           equ   cursor_arrow_w
CUR_H           equ   cursor_arrow_h

cursor_show
                ld    a,(cur_shown)          ; already on screen? a bare re-draw would
                or    a                       ; save the cursor into its own save-under
                ret   nz                      ; -> a ghost on the next move (#126). cursor_
                jp    cursor_draw             ; move_to already guards this way (it erases)

; cursor_move_to: DE = x, HL = y (graphics). No-op if unchanged.
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
                jr    nz,cm_go
                ret
cm_go
                ld    a,(cur_supp)           ; suppressed (during a DnD drag, #62)?
                or    a                       ; update the position but don't draw, so
                jr    nz,cm_storeonly         ; the pointer still tracks under the ghost
                push  de
                push  hl
                call  cursor_erase
                pop   hl
                pop   de
                ld    (cursor_x),de
                ld    (cursor_y),hl
                jp    cursor_draw
cm_storeonly
                ld    (cursor_x),de
                ld    (cursor_y),hl
                ret

; cursor_erase: restore the screen block saved at the last draw position.
cursor_erase
                xor   a                       ; cursor no longer on screen (#126)
                ld    (cur_shown),a
                call  cur_setsb
                jp    restore_block

; cur_setsb: point the shared save_block/restore_block params (sb_*) at the
; cursor's clipped on-screen rectangle + cur_bg buffer (used by draw and erase).
cur_setsb
                ld    a,(cur_xbyte)
                ld    (sb_x),a
                ld    a,(cur_line)
                ld    (sb_y),a
                ld    a,(cur_dcols)           ; clipped extent (edge overhang trimmed)
                ld    (sb_w),a
                ld    a,(cur_drows)
                ld    (sb_h),a
                ld    hl,cur_bg
                ld    (sb_buf),hl
                ret

; cursor_draw: place the sprite at (cursor_x, cursor_y) minus the hotspot.
cursor_draw
                ld    hl,(cursor_x)          ; px = cursor_x/2 - hotspot_x
                srl   h
                rr    l
                ld    de,cursor_arrow_hx
                or    a
                sbc   hl,de
                jr    nc,cd_xok
                ld    hl,0
cd_xok
                ld    a,l                     ; sub = px AND 3
                and   3
                ld    (cur_sub),a
                srl   h                       ; xbyte = px >> 2
                rr    l
                srl   h
                rr    l
                ld    a,l
                cp    80                       ; clamp hotspot byte to [0, 79] so the tip can
                jr    c,cd_xb                  ; reach the right border; the sprite clips there
                ld    a,79
cd_xb
                ld    (cur_xbyte),a
                sub   80-CUR_W                 ; cur_skip = max(0, xbyte-(80-CUR_W)): sprite cols
                jr    nc,cd_xclip              ; that fall off the right edge (0 = fully on screen)
                xor   a
cd_xclip
                ld    (cur_skip),a
                ld    b,a                       ; cur_dcols = CUR_W - cur_skip (cols on screen)
                ld    a,CUR_W
                sub   b
                ld    (cur_dcols),a
                ld    hl,(cursor_y)          ; line = 199 - cursor_y/2 - hotspot_y
                srl   h
                rr    l
                ld    a,199
                sub   l
                sub   cursor_arrow_hy          ; a = line in [0, 199] (hotspot row on screen)
                ld    (cur_line),a
                sub   200-CUR_H                ; rows that fall off the bottom edge
                jr    nc,cd_yclip
                xor   a
cd_yclip
                ld    b,a                       ; cur_drows = CUR_H - (clipped rows)
                ld    a,CUR_H
                sub   b
                ld    (cur_drows),a

                call  cur_setsb              ; save the (clipped) background block under the sprite
                call  save_block
                ld    a,(cur_sub)            ; HL = the interleaved (mask,data) sprite for this
                add   a,a                      ; sub-phase; kept in HL across rows
                ld    e,a
                ld    d,0
                ld    hl,cursor_arrow_spr
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    de,cur_bg              ; DE = saved-background read ptr, kept across rows
                ld    a,(cur_drows)
                ld    (cc_rows),a
                ld    a,(cur_line)
                ld    (cc_y),a
                ; Fast composite: the sprite is mask,data INTERLEAVED, so the inner loop keeps all
                ; three pointers in registers (no per-pixel (nn) reloads) - several x the old
                ; composite, so it stays ahead of the CRTC beam over the pointer's own scanlines
                ; sideways (#cursor-tear: the old reload-per-pixel composite was ~1 raster line per
                ; pixel and the beam caught it mid-write = a sliced band).
cc_row
                push  hl                      ; keep sprite + save ptrs across scr_addr
                push  de
                ld    a,(cur_xbyte)
                ld    d,a
                ld    a,(cc_y)
                ld    e,a
                call  scr_addr               ; HL = screen row addr
                ld    b,h
                ld    c,l                     ; BC = screen ptr
                pop   de                      ; DE = save-under ptr
                pop   hl                      ; HL = sprite ptr
                ld    a,(cur_dcols)
                ld    (cc_cols),a
cc_col
                ld    a,(de)                  ; bg from the saved buffer
                inc   de
                and   (hl)                    ; (bg AND mask)
                inc   hl
                or    (hl)                    ;         OR data
                inc   hl
                ld    (bc),a                  ; composite to the screen
                inc   bc
                ld    a,(cc_cols)
                dec   a
                ld    (cc_cols),a
                jr    nz,cc_col
                ld    a,(cur_skip)            ; skip the clipped sprite cols (x2 = mask+data)
                add   a,a
                ld    c,a
                ld    b,0
                add   hl,bc                   ; advance the sprite ptr to the next row
                ld    a,(cc_y)
                inc   a
                ld    (cc_y),a
                ld    a,(cc_rows)
                dec   a
                ld    (cc_rows),a
                jr    nz,cc_row
                ld    a,1                      ; cursor now on screen (#126)
                ld    (cur_shown),a
                ret

; --- State ---------------------------------------------------------------
cursor_x        dw    320
cursor_y        dw    200
cur_xbyte       db    0
cur_line        db    0
cur_supp        db    0            ; 1 = suppress drawing (DnD drag shows a ghost instead)
cur_shown       db    0            ; 1 = the arrow is currently composited on screen (#126)
cur_sub         db    0
cc_rows         db    0
cc_y            db    0
cc_cols         db    0
cur_dcols       db    cursor_arrow_w           ; on-screen sprite cols (clipped at right edge)
cur_drows       db    cursor_arrow_h           ; on-screen sprite rows (clipped at bottom edge)
cur_skip        db    0                        ; CUR_W - cur_dcols (sprite cols to skip per row)
cur_bg          defs  cursor_arrow_w*cursor_arrow_h
