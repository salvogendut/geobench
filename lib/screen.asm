; ---------------------------------------------------------------------------
; lib/screen.asm - GEOBENCH direct screen-memory access (Mode 1)
;
; The firmware draws one call per pixel/line, which is far too slow for real
; bitmap icons. These routines write straight to screen RAM instead, so a
; bitmap row is a byte copy rather than a string of firmware calls.
;
; CPC Mode 1 screen (default base #C000, 16 KB):
;   - 320x200, 4 pens, 2 bits per pixel, 4 pixels per byte -> 80 bytes / line.
;   - Lines are interleaved in 8 banks of #800:
;         addr = #C000 + (y AND 7)*#800 + (y >> 3)*80 + xbyte
;   - In-byte pixel encoding (pen = bit0 + 2*bit1):
;         pixel 0: bit7 = pen bit0, bit3 = pen bit1
;         pixel 1: bit6 / bit2
;         pixel 2: bit5 / bit1
;         pixel 3: bit4 / bit0
;     so a byte of one pen p:  p0->#00  p1->#F0  p2->#0F  p3->#FF
;   Bitmaps are stored already encoded in this byte format (the host tool does
;   the encoding), so blitting is a plain memory copy.
;
; Writes to #C000.. always reach screen RAM even with the upper ROM paged in,
; so blitting needs no ROM juggling. (Reading screen RAM, for save-under, will -
; that comes later.)
;
; Requires nothing. Assembled into the consumer; no org.
;
; Interface:
;   D = xbyte (0..79), E = y (0..199) : scr_addr -> HL = screen address
;   blit params in bm_src/bm_x/bm_y/bm_w/bm_h, then call blit_bitmap
; ---------------------------------------------------------------------------

SCREEN_BASE     equ   #C000

; ---------------------------------------------------------------------------
; scr_addr: D = xbyte, E = y -> HL = screen address. Clobbers A, BC.
scr_addr
                ld    a,e
                and   7
                add   a,a                    ; (y AND 7) * 8 ...
                add   a,a
                add   a,a
                or    #C0                     ; ... + base high byte (#C000 >> 8)
                ld    h,a
                ld    l,0                     ; HL = #C000 + (y AND 7)*#800
                ld    a,e
                srl   a
                srl   a
                srl   a                       ; A = y >> 3  (char row 0..24)
                add   a,a                     ; word index into row80
                ld    c,a
                ld    b,0
                push  hl
                ld    hl,row80
                add   hl,bc
                ld    c,(hl)
                inc   hl
                ld    b,(hl)                  ; BC = (y >> 3) * 80
                pop   hl
                add   hl,bc
                ld    b,0                     ; + xbyte
                ld    c,d
                add   hl,bc
                ret

; ---------------------------------------------------------------------------
; blit_bitmap: copy a bm_w x bm_h byte bitmap at (bm_x, bm_y) straight to the
; screen. bm_src points at row-major, already-encoded bytes.
blit_bitmap
                ld    a,(bm_x)               ; cull if fully outside the clip rect, so
                ld    b,a                     ; an icon under a higher window is not drawn
                ld    a,(bm_y)               ; over it during a partial repaint
                ld    c,a
                ld    a,(bm_w)
                ld    d,a
                ld    a,(bm_h)
                ld    e,a
                call  rect_cull
                ret   c
                ld    a,(bm_h)
                ld    (bl_rows),a
                ld    a,(bm_y)
                ld    (bl_y),a
bl_loop
                ld    a,(bm_x)               ; dest = scr_addr(bm_x, bl_y)
                ld    d,a
                ld    a,(bl_y)
                ld    e,a
                call  scr_addr
                ld    d,h                     ; DE = dest
                ld    e,l
                ld    hl,(bm_src)            ; HL = source row
                ld    a,(bm_w)
                ld    c,a
                ld    b,0
                ldir                          ; copy one row
                ld    (bm_src),hl            ; advance source past this row
                ld    a,(bl_y)
                inc   a
                ld    (bl_y),a
                ld    a,(bl_rows)
                dec   a
                ld    (bl_rows),a
                jr    nz,bl_loop
                ret

; ---------------------------------------------------------------------------
; fill_block: fill an fb_w x fb_h byte rectangle at (fb_x, fb_y) with fb_val.
; (fb_val is a Mode 1 byte: #00 blue, #F0 white, #0F black, #FF red.)
fill_block
                call  clip_fb_copy           ; fbw_* = fb_* clipped to the clip rect
                ret   c                        ; fully outside -> nothing to fill
                ld    a,(fbw_h)
                ld    (fb_rows),a
                ld    a,(fbw_y)
                ld    (fb_cy),a
fbk_loop
                ld    a,(fbw_x)
                ld    d,a
                ld    a,(fb_cy)
                ld    e,a
                call  scr_addr
                ld    a,(fbw_w)
                ld    b,a
                ld    a,(fb_val)
                ld    c,a
fbk_row
                ld    (hl),c
                inc   hl
                djnz  fbk_row
                ld    a,(fb_cy)
                inc   a
                ld    (fb_cy),a
                ld    a,(fb_rows)
                dec   a
                ld    (fb_rows),a
                jr    nz,fbk_loop
                ret

; clip_fb_copy: fbw_* = intersection of fb_* (x,y,w,h) with the clip rect. CF set
; if the intersection is empty. Leaves fb_* untouched. Clobbers A,B,C,D,E,H,L.
clip_fb_copy
                ld    a,(fb_x)               ; X: seg [fb_x, fb_x+fb_w)
                ld    h,a
                ld    a,(fb_w)
                add   a,h
                ld    l,a                      ; L = fb_x + fb_w
                ld    a,(clip_x)
                ld    d,a
                ld    a,(clip_w)
                add   a,d
                ld    e,a                      ; E = clip_x + clip_w
                ld    a,h                      ; left = max(fb_x, clip_x)
                cp    d
                jr    nc,cfc_xl
                ld    a,d
cfc_xl          ld    c,a                      ; C = left
                ld    a,l                      ; right = min(fb_end, clip_end)
                cp    e
                jr    c,cfc_xr
                ld    a,e
cfc_xr          ld    b,a                      ; B = right
                ld    a,c
                cp    b
                jr    nc,cfc_empty            ; left >= right -> empty
                ld    (fbw_x),a
                ld    a,b
                sub   c
                ld    (fbw_w),a
                ld    a,(fb_y)               ; Y: seg [fb_y, fb_y+fb_h)
                ld    h,a
                ld    a,(fb_h)
                add   a,h
                ld    l,a
                ld    a,(clip_y)
                ld    d,a
                ld    a,(clip_h)
                add   a,d
                ld    e,a
                ld    a,h
                cp    d
                jr    nc,cfc_yl
                ld    a,d
cfc_yl          ld    c,a
                ld    a,l
                cp    e
                jr    c,cfc_yr
                ld    a,e
cfc_yr          ld    b,a
                ld    a,c
                cp    b
                jr    nc,cfc_empty
                ld    (fbw_y),a
                ld    a,b
                sub   c
                ld    (fbw_h),a
                or    a                         ; clear CF
                ret
cfc_empty       scf
                ret

; rect_cull: B=x C=y D=w E=h (byte cols / lines) -> CF set if the rectangle is
; fully outside the clip rect (so the caller skips drawing). Used to cull whole
; icons/glyphs so a lower layer never paints over a higher window during a partial
; repaint. Clobbers A,H,L.
rect_cull
                ld    a,b                       ; X: right = x + w
                add   a,d
                ld    l,a
                ld    a,(clip_x)
                cp    l
                jr    nc,rc_out                 ; clip_x >= right -> left of clip
                ld    a,(clip_x)               ; clip_right = clip_x + clip_w
                ld    h,a
                ld    a,(clip_w)
                add   a,h
                cp    b
                jr    c,rc_out                  ; clip_right < x -> right of clip
                jr    z,rc_out                  ; clip_right == x -> right of clip
                ld    a,c                       ; Y: bottom = y + h
                add   a,e
                ld    l,a
                ld    a,(clip_y)
                cp    l
                jr    nc,rc_out                 ; above clip
                ld    a,(clip_y)               ; clip_bottom = clip_y + clip_h
                ld    h,a
                ld    a,(clip_h)
                add   a,h
                cp    c
                jr    c,rc_out                  ; below clip
                jr    z,rc_out
                or    a                          ; clear CF -> visible
                ret
rc_out          scf
                ret

; ---------------------------------------------------------------------------
; save_block / restore_block: copy a sb_w x sb_h byte rectangle at (sb_x, sb_y)
; between the screen and a RAM buffer (sb_buf) - the basis for cursor/sprite
; save-under.
;
; READING screen RAM is shadowed by the upper ROM at #C000, so save_block pages
; the upper ROM out around the reads (DI + Gate Array ROM/mode register: #8D =
; mode 1, upper ROM off + lower ROM off; #85 = mode 1, upper ROM on + lower ROM
; off). WRITING always reaches RAM, so restore_block needs none of that.
;
; The ROM/mode byte MUST keep the lower ROM OFF (bit2 set) - GEOBENCH runs with
; low RAM visible at #0000-#3FFF (cursor sprite at CUR_LOW=#1500, FAT scratch,
; etc.). The old #89/#81 cleared bit2, paging the firmware LOWER ROM over that
; low RAM (the #152 "#7F81 also enabled the lower ROM" gotcha). It was usually
; masked because a deferred interrupt fired after EI and reset the banking before
; the caller read low RAM - but a SHORT save_block (a bottom-clipped cursor, just
; 1-2 rows) finishes too fast for that, so the composite then read firmware bytes
; as the sprite = garbage on the pointer's top rows near the screen bottom.

save_block
                di
                ld    bc,#7F8D               ; upper ROM off + lower ROM off (mode 1)
                out   (c),c
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
                call  scr_addr               ; HL = screen (source)
                ld    de,(sb_ptr)            ; DE = buffer (dest)
                ld    a,(sb_w)
                ld    c,a
                ld    b,0
                ldir
                ld    (sb_ptr),de
                ld    a,(sb_cy)
                inc   a
                ld    (sb_cy),a
                ld    a,(sb_rows)
                dec   a
                ld    (sb_rows),a
                jr    nz,sv_loop
                ld    bc,#7F85               ; upper ROM back on (lower ROM stays off)
                out   (c),c
                ei
                ret

restore_block
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
                call  scr_addr               ; HL = screen (dest)
                ld    d,h
                ld    e,l                     ; DE = screen
                ld    hl,(sb_ptr)            ; HL = buffer (source)
                ld    a,(sb_w)
                ld    c,a
                ld    b,0
                ldir
                ld    (sb_ptr),hl
                ld    a,(sb_cy)
                inc   a
                ld    (sb_cy),a
                ld    a,(sb_rows)
                dec   a
                ld    (sb_rows),a
                jr    nz,rs_loop2
                ret

; ---------------------------------------------------------------------------
; (y >> 3) * 80 for char rows 0..24.
row80
                dw    0,80,160,240,320,400,480,560,640,720,800,880,960
                dw    1040,1120,1200,1280,1360,1440,1520,1600,1680,1760,1840,1920

; --- Blit parameters / scratch -------------------------------------------
bm_src          dw    0            ; source bitmap pointer (advanced by blit)
bm_x            db    0            ; destination x in bytes (0..79)
bm_y            db    0            ; destination y (0..199)
bm_w            db    0            ; width in bytes
bm_h            db    0            ; height in rows
bl_rows         db    0
bl_y            db    0

; --- fill_block parameters / scratch -------------------------------------
fb_x            db    0            ; x in bytes
fb_y            db    0            ; y line
fb_w            db    0            ; width in bytes
fb_h            db    0            ; height in rows
fb_val          db    0            ; Mode 1 fill byte
fb_rows         db    0
fb_cy           db    0

; --- clip rectangle (issue #45 phase 4) ----------------------------------
; fill_block clips its rect to this window before drawing, so a partial repaint
; (a moved/closed window's damage area) only erases inside that area - the rest of
; the screen is untouched, so unchanged windows/icons don't flicker. Default =
; whole screen (no clipping). The WM narrows it around a repaint, then resets it.
clip_x          db    0            ; clip rect, byte col / line / byte-w / lines
clip_y          db    0
clip_w          db    80
clip_h          db    200
fbw_x           db    0            ; fill_block's clipped working rect (fb_* kept
fbw_y           db    0            ; intact so callers that reuse fb_* still work)
fbw_w           db    0
fbw_h           db    0

; --- save_block / restore_block parameters / scratch ---------------------
sb_x            db    0            ; rectangle x in bytes
sb_y            db    0            ; rectangle y
sb_w            db    0            ; width in bytes
sb_h            db    0            ; height in rows
sb_buf          dw    0            ; RAM buffer pointer
sb_rows         db    0
sb_cy           db    0
sb_ptr          dw    0
