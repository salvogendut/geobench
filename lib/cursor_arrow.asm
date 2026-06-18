; Cursor sprite descriptor. The bitmaps themselves are NOT resident (#65): they
; live in low RAM at CUR_LOW, loaded from DEFAULT.SPR at boot (cursor_init), which
; saves ~256 B of resident kernel space. This file keeps only the geometry equs and
; the pre-shifted phase tables, which point into that low-RAM buffer.
;
; 2 pre-shifted sub-pixel phases (hand-reduced from 4, #62): the tables alias sub
; 0,1 -> shift0 and 2,3 -> shift2 (monotonic, no wobble; 2px horizontal snap).
; Buffer layout at CUR_LOW: d0 @ +0, m0 @ +#40, d2 @ +#80, m2 @ +#C0 (each 64 B =
; cursor_arrow_w*cursor_arrow_h) - see lib/cursor_data.asm (-> DEFAULT.SPR).
cursor_arrow_w        equ   4
cursor_arrow_h        equ   16
cursor_arrow_hx       equ   1
cursor_arrow_hy       equ   0
; Interleaved (mask,data) sprite, one pointer per phase. Buffer at CUR_LOW: phase0
; (shift 0) @ +0, phase2 (shift 2) @ +#80; each cursor_arrow_w*cursor_arrow_h*2 =
; 128 B. sub 0,1 -> phase0, 2,3 -> phase2 (the 4->2 phase alias, #62).
cursor_arrow_spr      dw    CUR_LOW,CUR_LOW,CUR_LOW+#80,CUR_LOW+#80
