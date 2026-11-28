; B=x and D=w in four-pixel columns; C=y and E=h in scanlines.
draw_fill       ld hl,logical_colours
                and 3
                add a,l
                ld l,a
                jr nc,pen_ready
                inc h
pen_ready       ld a,(hl)
                ld (plot_pen),a
                ld a,d
                or a
                ret z
                ld a,e
                or a
                ret z
                ld a,(AG_YBASE)
                ld l,a
                ld h,0
                ld a,c
                add a,l
                ld l,a
                jr nc,fill_top
                inc h
fill_top        ld (plot_y0),hl
                ld a,e
                dec a
                add a,l
                ld l,a
                jr nc,fill_bottom
                inc h
fill_bottom     ld (plot_y1),hl
                ld h,0
                ld l,b
                add hl,hl
                add hl,hl
                ld (plot_x0),hl
                ld l,d
                ld h,0
                add hl,hl
                add hl,hl
                ld de,(plot_x0)
                add hl,de
                dec hl
                ld (plot_x1),hl
                ld hl,rect_packet
                ld bc,rect_end-rect_packet
                jp send

; Match the other 4-pixel-column backends: one scanline top/bottom,
; one byte column on either side. Each edge is a single VDP rectangle.
draw_frame      push af
                push bc
                push de
                ld e,1
                call draw_fill
                pop de
                pop bc
                pop af
                push af
                push bc
                push de
                dec e
                push af
                ld a,c
                add a,e
                ld c,a
                pop af
                ld e,1
                call draw_fill
                pop de
                pop bc
                pop af
                push af
                push bc
                push de
                ld d,1
                call draw_fill
                pop de
                pop bc
                ld a,b
                add a,d
                dec a
                ld b,a
                ld d,1
                pop af
                jp draw_fill

; GB_TEXT input: HL string, B/C origin, D/E ink/paper. Titles can stop
; before the gadgets; all other text stops at the right screen edge.
draw_text       ld a,(AG_VGA)
                or a
                ld a,SCR_COLS
                jr z,draw_text_limit
                add a,a
draw_text_limit ld (AG_TEXT_ARGS+4),a
                ld (AG_TEXT_ARGS),bc
                ld (AG_TEXT_ARGS+2),de
                ld de,font_data
                jp AG_FILES+39

logical_colours db 4,15,0,9
rect_packet     db 18,0
plot_pen        db 0
                db 25,4
plot_x0         dw 0
plot_y0         dw 0
                db 25,101
plot_x1         dw 0
plot_y1         dw 0
rect_end
font_data       incbin "../../QA/CPC/CARD/GBENCH/DEFAULT.FNT",16
                assert $-font_data == 100*8
